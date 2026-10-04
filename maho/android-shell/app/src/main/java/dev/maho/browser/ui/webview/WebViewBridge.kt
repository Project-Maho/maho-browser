package dev.maho.browser.ui.webview

import android.content.Context
import android.content.Intent
import android.provider.Settings
import android.util.Log
import android.webkit.JavascriptInterface
import dev.maho.browser.BuildConfig
import dev.maho.browser.MahoBridge
import dev.maho.browser.sync.GoogleCredentialSource
import dev.maho.browser.sync.GoogleSignInOutcome
import dev.maho.browser.sync.SyncManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicBoolean

/**
 * WebViewBridge — @JavascriptInterface glue between the web-ai bundle and the
 * native [WebViewNativeBridge] (in production, [MahoNativeBridge] → MahoBridge).
 *
 * JavaScript calls:  window.MahoBridgeAndroid.rpc(jsonRpcRequestString)
 * Native resolves:   window.__mahoBridgeResponse('<responseJson>')
 *
 * DUAL-MODE contract (zero-regression across bundle versions):
 *  - `id` is echoed VERBATIM — string UUID (current bundle) or int (old bundle).
 *  - `params` may be a named JSONObject OR a positional JSONArray (see RpcParams).
 *  - New method names are primary; legacy names are kept as aliases.
 *
 * Agent/chat native sessions are opaque `Long` pointers. They never cross to JS:
 * each is mapped to a string handle via a [HandleRegistry].
 *
 * New RPC methods: add a branch to dispatchMethod() and a corresponding handler.
 */
class WebViewBridge(
    private val native: WebViewNativeBridge,
    private val evaluateJs: (String) -> Unit,
    private val onNavBack: (() -> Unit)? = null,
    private val onCompleteOnboarding: (() -> Unit)? = null,
    private val browserToolExecutor: BrowserToolExecutor = BrowserToolExecutor(),
    private val googleCredentialSource: GoogleCredentialSource? = null,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    internal val agentSessions = HandleRegistry()
    internal val chatSessions = HandleRegistry()
    private val destroyed = AtomicBoolean(false)

    // Teardown scope independent of the RPC scope: draining must survive the
    // RPC scope cancellation that destroy() performs, and it must never run on
    // the caller's thread (Main) because closeAndDrain waits for parked borrows.
    private val teardownScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    /** Observe existing jobs without changing their parent, dispatcher, or cancellation. */
    internal fun requestsForTesting(): List<Job> =
        requireNotNull(scope.coroutineContext[Job]).children.toList()

    /**
     * U01 destroy contract: idempotent; closes admission immediately (new RPC
     * dispatch and response delivery stop); the borrow-draining native free
     * runs on the independent teardown scope so the calling thread (Main)
     * never waits for a parked borrow or create. Teardown failures are
     * aggregated and reported instead of being swallowed.
     */
    fun destroy() {
        if (!destroyed.compareAndSet(false, true)) return
        scope.cancel()
        teardownScope.launch {
            val failures = mutableListOf<Exception>()
            for ((_, ptr) in agentSessions.closeAndDrain()) {
                try {
                    native.agentFreeSession(ptr)
                } catch (e: Exception) {
                    failures.add(e)
                }
            }
            for ((_, ptr) in chatSessions.closeAndDrain()) {
                try {
                    native.chatSessionFree(ptr)
                } catch (e: Exception) {
                    failures.add(e)
                }
            }
            if (failures.isNotEmpty()) {
                Log.w("WebViewBridge", "native session teardown reported ${failures.size} failure(s)", failures.first())
            }
        }
    }

    private companion object {
        const val MAX_BROWSER_TOOL_RESULT_BYTES = 4_000
        const val MANAGED_AUTH_UNAVAILABLE =
            "{\"version\":1,\"kind\":\"credential_error\",\"code\":\"managed_auth_unavailable\"}"
    }

    @JavascriptInterface
    fun rpc(requestJson: String) {
        scope.launch {
            val response = handleRequest(requestJson)
            // A destroyed bridge must never deliver a late RPC response (U01:
            // cancelled delivery does not adopt a late native allocation).
            if (coroutineContext.isActive) {
                evaluateJs("window.__mahoBridgeResponse(${escapeForJs(response)})")
            }
        }
    }

    /** Called from JS (web back button) to request the native host close the surface. */
    @JavascriptInterface
    fun onBack() {
        scope.launch { onNavBack?.invoke() }
    }

    // ─── Request dispatch ────────────────────────────────────────────────────

    /** Visible for tests: parse → dispatch → envelope. */
    internal suspend fun handleRequest(requestJson: String): String {
        val req = runCatching { JSONObject(requestJson) }.getOrNull()
            ?: return errorResponse(JSONObject.NULL, "", "Invalid JSON")

        val id: Any = if (req.has("id") && !req.isNull("id")) req.get("id") else JSONObject.NULL
        val method = req.optString("method", "")
        val params = RpcParams.from(req.opt("params"))

        return try {
            successResponse(id, dispatchMethod(method, params))
        } catch (e: RpcParamsInvalidException) {
            paramsInvalidResponse(id, method, e.message ?: "Invalid params")
        } catch (e: Exception) {
            errorResponse(id, method, e.message ?: "Unknown error")
        }
    }

    private suspend fun dispatchMethod(method: String, p: RpcParams): Any? =
        withContext(Dispatchers.IO) {
            when (method) {
                // --- BYOK (shared old/new names) ---
                "byokGetProviders" -> jsonArrayOrEmpty(native.byokGetProviders())
                "byokGetKey" -> native.byokGetKey(p.string("provider", 0))
                "byokSetKey" -> native.byokSetKey(p.string("provider", 0), p.string("key", 1))
                "byokDeleteKey" -> native.byokDeleteKey(p.string("provider", 0))
                "byokValidateKey" -> native.byokValidateKey(p.string("provider", 0), p.string("key", 1))

                "getAiSettings" -> JSONObject(native.getAiSettingsJson())
                "setAiProvider" -> native.setAiProvider(p.string("provider", 0)).let { null }
                "setAiBaseUrl" -> native.setAiBaseUrl(p.string("url", 0)).let { null }
                "setAiApiKey" -> native.setAiApiKey(p.string("key", 0)).let { null }
                "setAiModel" -> native.setAiModel(p.string("model", 0)).let { null }

                // --- Chat (new names) ---
                "chatSessionStart" -> chatSessionStart(p)
                "chatSessionResume" -> chatSessionResume(p)
                "chatSessionFree" -> chatSessionFree(p)
                "chatSendMessage" -> chatSendMessage(p)
                "chatCancelTurn" -> chatSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.chatCancel(ptr)
                }
                "chatPollEvents" -> chatPollEventsArray(p)
                "chatRegisterTool" -> chatRegisterTool(p)
                "chatSendToolResult" -> chatSendToolResult(p)
                "browserToolInvoke" -> boundedBrowserToolResult(browserToolExecutor.invoke(
                    p.string("name", 0),
                    p.optObject("args", 1) ?: JSONObject(),
                ))
                "chatAppendAssistantMessage" -> chatSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.chatAppendAssistantMessage(
                        ptr,
                        p.string("content", 1),
                        p.optString("toolCallsJson", 2) ?: "[]",
                    )
                }
                "chatGetHistory" -> jsonArrayOrEmpty(native.getConversationMessagesJson(p.optString("handle", 0)?.ifBlank { p.string("sessionId", 0) } ?: p.string("sessionId", 0)))

                // --- Chat (legacy aliases, pointer-positional) ---
                "chatSessionNew" -> chatSessionStart(p)
                "chatSendUserTurn" -> chatSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.chatSendUserTurn(ptr, p.string("message", 1))
                }
                "chatCancel" -> chatSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.chatCancel(ptr)
                }
                "chatSessionPollEvent" -> chatSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.chatSessionPollEvent(ptr)
                }

                // --- Conversations (new + legacy) ---
                "conversationCreate" -> conversationCreate(p)
                "conversationList" -> {
                    val state = p.requiredNonBlankString("state", 0)
                    if (state !in setOf("active", "archived", "all")) {
                        throw RpcParamsInvalidException("'state' must be active, archived, or all")
                    }
                    val limit = p.requiredIntegralLong("limit", 1)
                    if (limit !in 1L..500L) {
                        throw RpcParamsInvalidException("'limit' must be between 1 and 500")
                    }
                    parseRequiredJsonArray(native.listConversationsJson(state, limit), "conversationList")
                }
                "listConversations" -> jsonArrayOrEmpty(native.listConversationsJson(p.long("limit", 0, 100L)))
                "conversationGet" -> conversationGet(p)
                "conversationDelete", "deleteConversation" -> native.deleteConversation(p.string("id", 0))
                "conversationRename", "renameConversation" ->
                    native.renameConversation(p.string("id", 0), p.string("title", 1))
                "conversationArchive" -> native.archiveConversation(p.requiredNonBlankString("id", 0))
                "conversationUnarchive" -> native.unarchiveConversation(p.requiredNonBlankString("id", 0))
                "conversationBulk" -> {
                    val operation = p.requiredNonBlankString("op", 0)
                    if (operation !in setOf("archive", "unarchive", "delete")) {
                        throw RpcParamsInvalidException("'op' must be archive, unarchive, or delete")
                    }
                    val values = p.optArray("ids", 1)
                        ?: throw RpcParamsInvalidException("Missing required param 'ids'")
                    if (values.length() !in 1..500) {
                        throw RpcParamsInvalidException("'ids' must contain between 1 and 500 values")
                    }
                    val ids = (0 until values.length()).map { index ->
                        val value = values.get(index)
                        if (value !is String || value.isBlank()) {
                            throw RpcParamsInvalidException("'ids' must contain only nonblank strings")
                        }
                        value
                    }
                    if (ids.toSet().size != ids.size) {
                        throw RpcParamsInvalidException("'ids' must be unique")
                    }
                    parseRequiredJsonObject(
                        native.applyConversationBulkOperationJson(operation, ids),
                        "conversationBulk",
                    )
                }
                "conversationGetAutoArchivePolicy" -> native.getConversationAutoArchivePolicy()
                "conversationSetAutoArchivePolicy" -> {
                    val afterDays = if (p.has("afterDays", 0) && p.raw("afterDays", 0) === JSONObject.NULL) {
                        -1L
                    } else {
                        p.requiredIntegralLong("afterDays", 0)
                    }
                    if (afterDays !in setOf(-1L, 3L, 7L, 30L)) {
                        throw RpcParamsInvalidException("'afterDays' must be null, -1, 3, 7, or 30")
                    }
                    native.setConversationAutoArchivePolicy(afterDays.toInt())
                }
                "conversationGetMessages", "getConversationMessages" ->
                    jsonArrayOrEmpty(native.getConversationMessagesJson(p.string("id", 0)))
                "conversationProjectList" -> parseRequiredJsonArray(native.listConversationProjectsJson(), "conversationProjectList")
                "conversationProjectCreate" -> parseRequiredJsonObject(
                    native.createConversationProjectJson(p.requiredNonBlankString("name", 0)),
                    "conversationProjectCreate",
                )
                "conversationProjectRename" -> native.renameConversationProject(
                    p.requiredNonBlankString("id", 0),
                    p.requiredNonBlankString("name", 1),
                )
                "conversationProjectDelete" -> native.deleteConversationProject(p.requiredNonBlankString("id", 0))
                "conversationProjectMove" -> {
                    val ids = p.requiredUniqueIds("ids", 0)
                    val projectId = if (p.has("projectId", 1) && p.raw("projectId", 1) === JSONObject.NULL) {
                        null
                    } else {
                        p.requiredNonBlankString("projectId", 1)
                    }
                    parseRequiredJsonObject(native.moveConversationsToProjectJson(ids, projectId), "conversationProjectMove")
                }
                "createConversation" -> native.createConversation(
                    p.string("id", 0), p.optString("title", 1), p.optString("spaceId", 2), p.optString("model", 3),
                )
                "saveConversationMessage" -> native.saveConversationMessage(
                    p.string("sessionId", 0), p.string("role", 1), p.string("content", 2), p.optString("urlContext", 3),
                )

                // --- Composer drafts ---
                "composerDraftGet" -> native.getComposerDraftJson(composerDraftScopeJson(p))
                    ?.let { runCatching { JSONObject(it) }.getOrNull() }
                "composerDraftSet" -> native.setComposerDraft(
                    composerDraftScopeJson(p),
                    p.string("text", 1),
                )
                "composerDraftDelete" -> native.deleteComposerDraft(composerDraftScopeJson(p))

                // --- Space AI Config ---
                "getSpaceAIConfig" -> native.getSpaceAIConfigJson(p.string("spaceId", 0))?.let { JSONObject(it) }
                "setSpaceAIConfig" -> setSpaceAIConfig(p)

                // --- Offline Model ---
                "offlineModelInfo" -> native.offlineModelInfoJson()?.let { JSONObject(it) }
                "offlineModelIsDownloaded" -> native.offlineModelIsDownloaded()
                "offlineModelDelete" -> native.offlineModelDelete()

                // --- Agent Session ---
                "agentCreateSession" -> agentCreateSession(p)
                "agentSendMessage" -> agentSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.agentSendMessage(ptr, p.string("message", 1))
                }
                "agentCancel" -> agentSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.agentCancel(ptr)
                }
                "agentPollEvent" -> agentSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.agentPollEvent(ptr)
                }
                "agentListTools" -> jsonArrayOrEmpty(agentSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.agentListTools(ptr)
                })
                "agentListArtifacts" -> jsonArrayOrEmpty(agentSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.agentListArtifacts(ptr)
                })
                "artifactShare" -> agentSessions.withPointer(p.string("handle", 0)) { ptr ->
                    native.artifactShare(
                        ptr,
                        p.string("artifactId", 1),
                    )
                }
                "agentFreeSession" -> agentFreeSession(p)

                // --- Pinch: no Android backing ---
                "pinchEstimateCost" -> throw unsupported(method)

                // --- Relay auth (gated onboarding) ---
                "relaySignIn" -> {
                    val email = p.string("email", 0)
                    val password = p.string("password", 1)
                    val result = JSONObject()
                    try {
                        SyncManager.logIn(SyncManager.getServerUrl(), email, password)
                        result.put("ok", true)
                    } catch (e: Exception) {
                        result.put("ok", false)
                        result.put("error", e.message ?: "Unknown login error")
                    }
                    result
                }
                "relaySignUp" -> {
                    val email = p.string("email", 0)
                    val password = p.string("password", 1)
                    val result = JSONObject()
                    try {
                        SyncManager.signUp(SyncManager.getServerUrl(), email, password)
                        result.put("ok", true)
                    } catch (e: Exception) {
                        result.put("ok", false)
                        result.put("error", e.message ?: "Unknown signup error")
                    }
                    result
                }
                "relaySignInWithGoogle" -> {
                    val result = JSONObject()
                    val credentialSource = googleCredentialSource
                    if (credentialSource == null) {
                        result.put("ok", false)
                        result.put("error", "Google sign-in is unavailable")
                    } else {
                        when (
                            val outcome = withContext(Dispatchers.Main) {
                                SyncManager.signInWithGoogle(
                                    SyncManager.getServerUrl(),
                                    BuildConfig.GOOGLE_OAUTH_SERVER_CLIENT_ID,
                                    credentialSource,
                                )
                            }
                        ) {
                            GoogleSignInOutcome.Cancelled -> {
                                result.put("ok", false)
                                result.put("cancelled", true)
                            }
                            is GoogleSignInOutcome.SignedIn -> result.put("ok", true)
                            is GoogleSignInOutcome.Failure -> {
                                result.put("ok", false)
                                result.put("error", outcome.message)
                            }
                        }
                    }
                    result
                }
                "relayAccountStatus" -> {
                    val result = JSONObject()
                    val session = SyncManager.currentSession()
                    result.put("hasValidSession", session != null)
                    result.put("isReauth", false)
                    result
                }

                // --- Default browser ---
                "openDefaultBrowserSettings" -> {
                    val context = MahoBridge.appContext
                    if (context != null) {
                        val intent = Intent(Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS).apply {
                            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        }
                        context.startActivity(intent)
                    }
                    null
                }

                // --- Finish ---
                "completeOnboarding" -> {
                    val context = MahoBridge.appContext
                    if (context != null) {
                        val prefs = context.getSharedPreferences("maho_browser_ui_prefs", Context.MODE_PRIVATE)
                        prefs.edit().putBoolean("onboardingCompleted", true).apply()
                    }
                    scope.launch(Dispatchers.Main) {
                        onCompleteOnboarding?.invoke()
                    }
                    null
                }

                // --- Native-only: resolved by NativeActionBridge, not here ---
                "capturePhoto", "captureScreenshot", "openSettings", "hapticFeedback" ->
                    throw UnsupportedOperationException("Method '$method' must be handled by NativeActionBridge")

                else -> throw IllegalArgumentException("Unknown method: $method")
            }
        }

    // ─── Agent handlers ──────────────────────────────────────────────────────

    private fun boundedBrowserToolResult(result: JSONObject): JSONObject {
        if (result.toString().toByteArray(Charsets.UTF_8).size <= MAX_BROWSER_TOOL_RESULT_BYTES) {
            return result
        }
        val entries = result.optJSONArray("result") ?: return result
        val bounded = JSONObject(result.toString())
        bounded.remove("result")
        var kept = JSONArray()
        for (index in 0 until entries.length()) {
            val candidate = JSONArray()
            for (keptIndex in 0 until kept.length()) {
                candidate.put(kept.get(keptIndex))
            }
            candidate.put(entries.get(index))
            bounded.put("result", candidate)
            bounded.put("truncated", true)
            if (bounded.toString().toByteArray(Charsets.UTF_8).size > MAX_BROWSER_TOOL_RESULT_BYTES) {
                break
            }
            kept = candidate
        }
        return bounded.put("result", kept).put("truncated", true)
    }

    private fun agentCreateSession(p: RpcParams): String {
        val ptr = native.agentCreateSession(p.string("sessionId", 0))
        if (ptr == 0L) throw IllegalStateException("agentCreateSession failed (native returned 0)")
        return agentSessions.register(ptr) ?: run {
            try { native.agentFreeSession(ptr) } catch (_: Exception) {}
            throw IllegalStateException("WebViewBridge is destroyed")
        }
    }

    private fun agentFreeSession(p: RpcParams): Any? {
        val handle = p.string("handle", 0)
        agentSessions.unregister(handle)?.let { native.agentFreeSession(it) }
        return null
    }

    // ─── Chat handlers ───────────────────────────────────────────────────────

    private data class ChatSessionConfig(
        val apiKey: String,
        val endpoint: String,
        val model: String,
        val systemInstruction: String,
    )

    private fun chatSessionStart(p: RpcParams): String {
        val ptr = newChatSession(chatSessionConfig(p, optsIndex = 0))
        if (ptr == 0L) throw IllegalStateException("chatSessionStart failed (native returned 0)")
        return chatSessions.register(ptr) ?: run {
            try { native.chatSessionFree(ptr) } catch (_: Exception) {}
            throw IllegalStateException("WebViewBridge is destroyed")
        }
    }

    private fun chatSessionResume(p: RpcParams): String {
        val conversationId = p.optString("conversationId", 0)
            ?: p.optString("handle", 0)
            ?: p.string("sessionId", 0)
        val existing = try {
            chatSessions.withPointer(conversationId) { conversationId }
        } catch (_: Exception) {
            null
        }
        if (existing != null) {
            return conversationId
        }
        val ptr = newChatSession(chatSessionConfig(p, optsIndex = 1))
        if (ptr == 0L) throw IllegalStateException("chatSessionResume failed (native returned 0)")
        val registeredHandle = chatSessions.register(ptr) ?: run {
            try { native.chatSessionFree(ptr) } catch (_: Exception) {}
            throw IllegalStateException("WebViewBridge is destroyed")
        }
        val historyJson = native.getConversationMessagesJson(conversationId)
        if (!historyJson.isNullOrEmpty()) {
            val list = JSONArray(historyJson)
            chatSessions.withPointer(registeredHandle) { leasedPtr ->
                for (i in 0 until list.length()) {
                    val item = list.optJSONObject(i) ?: continue
                    val content = item.optString("content")
                    when (item.optString("role")) {
                        "user" -> native.chatAppendUserMessage(leasedPtr, content)
                        "assistant" -> native.chatAppendAssistantMessage(leasedPtr, content, "[]")
                    }
                }
            }
        }
        return registeredHandle
    }

    private fun chatSessionConfig(p: RpcParams, optsIndex: Int): ChatSessionConfig {
        val opts = p.optObject("opts", optsIndex)
        fun option(name: String, legacyIndex: Int): String? =
            if (opts != null) opts.optString(name).takeIf { it.isNotEmpty() }
            else p.optString(name, legacyIndex)?.takeIf { it.isNotEmpty() }

        val credentialProvider = option("credentialProvider", optsIndex + 4)
        val systemInstruction = option("systemInstruction", optsIndex + 3).orEmpty()
        if (credentialProvider == "maho-managed") {
            val managed = native.resolveManagedChatConfig()
                ?: throw IllegalStateException(MANAGED_AUTH_UNAVAILABLE)
            return ChatSessionConfig(
                apiKey = managed.apiKey,
                endpoint = managed.endpoint,
                model = managed.model,
                systemInstruction = systemInstruction,
            )
        }

        val suppliedApiKey = option("apiKey", optsIndex)
        val apiKey = suppliedApiKey ?: credentialProvider?.let { provider ->
            if (provider != "openai-compatible") {
                throw IllegalArgumentException("Unsupported chat credential provider: $provider")
            }
            native.resolveChatCredential(provider)
                ?: throw IllegalStateException("No stored credential for chat provider: $provider")
        }.orEmpty()
        return ChatSessionConfig(
            apiKey = apiKey,
            endpoint = option("endpoint", optsIndex + 1).orEmpty(),
            model = option("model", optsIndex + 2).orEmpty(),
            systemInstruction = systemInstruction,
        )
    }

    private fun newChatSession(config: ChatSessionConfig): Long = try {
        native.chatSessionNew(
            config.apiKey,
            config.endpoint,
            config.model,
            config.systemInstruction,
        )
    } catch (_: Exception) {
        // Native failures are intentionally opaque so credentials can never enter RPC errors.
        throw IllegalStateException("chat session creation failed")
    }

    private fun chatSessionFree(p: RpcParams): Any? {
        val handle = p.string("handle", 0)
        chatSessions.unregister(handle)?.let { native.chatSessionFree(it) }
        return null
    }

    private fun chatSendMessage(p: RpcParams): Boolean {
        val content = p.optObject("content", 1)
            ?: throw IllegalArgumentException("Missing chat 'content'")
        val kind = content.optString("kind")
        val imageObj = content.optJSONObject("image")
        return chatSessions.withPointer(p.string("handle", 0)) { ptr ->
            if (imageObj != null || kind == "image") {
                val img = imageObj ?: content
                val text = content.optString("text")
                val mime = img.optString("mime")
                val base64 = img.optString("base64")
                val bytes = decodeBase64(base64)
                if (text.isBlank()) {
                    native.chatSendImage(ptr, mime, bytes)
                } else {
                    native.chatSendTextWithImage(ptr, text, mime, bytes)
                }
            } else {
                native.chatSendUserTurn(ptr, content.optString("text"))
            }
        }
    }

    private fun chatPollEventsArray(p: RpcParams): JSONArray {
        val single = chatSessions.withPointer(p.string("handle", 0)) { ptr ->
            native.chatSessionPollEvent(ptr)
        } ?: return JSONArray()
        return JSONArray().put(JSONObject(single))
    }

    private fun chatRegisterTool(p: RpcParams): Boolean {
        val tool = p.optObject("tool", 1) ?: throw IllegalArgumentException("Missing 'tool'")
        val paramsJson = tool.optJSONObject("parameters")?.toString() ?: "{}"
        return chatSessions.withPointer(p.string("handle", 0)) { ptr ->
            native.chatRegisterTool(ptr, tool.optString("name"), tool.optString("description"), paramsJson)
        }
    }

    private fun chatSendToolResult(p: RpcParams): Boolean {
        val result = p.optObject("result", 2)?.toString() ?: "{}"
        val trigger = p.boolean("trigger", 4, true)
        return chatSessions.withPointer(p.string("handle", 0)) { ptr ->
            native.chatSendToolResult(
                ptr,
                p.string("toolCallId", 1),
                p.optString("toolName", 3).orEmpty(),
                result,
                trigger,
            )
        }
    }

    // ─── Conversation / space handlers ────────────────────────────────────────

    private fun conversationCreate(p: RpcParams): String {
        val meta = p.optObject("meta", 0) ?: throw IllegalArgumentException("Missing 'meta'")
        val id = meta.optString("id")
        native.createConversation(
            id,
            meta.optString("title").ifBlank { null },
            meta.optString("spaceId").ifBlank { null },
            meta.optString("model").ifBlank { null },
        )
        return id
    }

    /**
     * Normalizes the `scope` RPC parameter into the exact JSON maho-core accepts.
     * Rejecting here keeps a malformed scope from ever becoming a settings key.
     */
    private fun composerDraftScopeJson(p: RpcParams): String {
        val scope = p.optObject("scope", 0)
            ?: throw IllegalArgumentException("Missing composer draft 'scope'")
        return when (val kind = scope.optString("kind")) {
            "new_task" -> JSONObject().put("kind", "new_task").toString()
            "conversation" -> {
                val conversationId = scope.optString("conversationId")
                if (conversationId.isBlank()) {
                    throw IllegalArgumentException("Missing composer draft 'scope.conversationId'")
                }
                JSONObject()
                    .put("kind", "conversation")
                    .put("conversationId", conversationId)
                    .toString()
            }
            else -> throw IllegalArgumentException("Unsupported composer draft scope kind: $kind")
        }
    }

    private fun conversationGet(p: RpcParams): JSONObject? {
        val id = p.string("id", 0)
        val list = native.listConversationsJson("all", 100L)?.let { JSONArray(it) } ?: return null
        for (i in 0 until list.length()) {
            val item = list.optJSONObject(i) ?: continue
            if (item.optString("id") == id) return item
        }
        return null
    }

    private fun setSpaceAIConfig(p: RpcParams): Boolean {
        val spaceId = p.string("spaceId", 0)
        val configJson = p.optObject("config", 1)?.toString()
            ?: p.string("config", 1) // legacy: config already a JSON string
        return native.setSpaceAIConfigJson(spaceId, configJson)
    }

    // ─── Helpers ─────────────────────────────────────────────────────────────

    private fun unsupported(method: String): Exception =
        UnsupportedOperationException("Method '$method' has no Android backing")

    private fun jsonArrayOrEmpty(raw: String?): JSONArray =
        raw?.let { runCatching { JSONArray(it) }.getOrNull() } ?: JSONArray()

    private fun parseRequiredJsonArray(raw: String?, operation: String): JSONArray {
        if (raw == null) throw IllegalStateException("$operation failed")
        return try {
            JSONArray(raw)
        } catch (_: Exception) {
            throw IllegalStateException("$operation returned invalid JSON")
        }
    }

    private fun parseRequiredJsonObject(raw: String?, operation: String): JSONObject {
        if (raw == null) throw IllegalStateException("$operation failed")
        return try {
            JSONObject(raw)
        } catch (_: Exception) {
            throw IllegalStateException("$operation returned invalid JSON")
        }
    }

    private fun RpcParams.requiredNonBlankString(name: String, index: Int): String {
        if (!has(name, index)) throw RpcParamsInvalidException("Missing required param '$name'")
        val value = raw(name, index)
        if (value !is String || value.isBlank()) {
            throw RpcParamsInvalidException("'$name' must be a nonblank string")
        }
        return value
    }

    private fun RpcParams.requiredUniqueIds(name: String, index: Int): List<String> {
        val values = optArray(name, index) ?: throw RpcParamsInvalidException("Missing required param '$name'")
        if (values.length() !in 1..500) throw RpcParamsInvalidException("'$name' must contain between 1 and 500 values")
        val ids = (0 until values.length()).map { valueIndex ->
            val value = values.get(valueIndex)
            if (value !is String || value.isBlank()) throw RpcParamsInvalidException("'$name' must contain only nonblank strings")
            value
        }
        if (ids.toSet().size != ids.size) throw RpcParamsInvalidException("'$name' must be unique")
        return ids
    }

    private fun RpcParams.requiredIntegralLong(name: String, index: Int): Long {
        if (!has(name, index)) throw RpcParamsInvalidException("Missing required param '$name'")
        val value = raw(name, index)
        if (value !is Byte && value !is Short && value !is Int && value !is Long) {
            throw RpcParamsInvalidException("'$name' must be an integer")
        }
        return (value as Number).toLong()
    }

    private fun decodeBase64(b64: String): ByteArray =
        android.util.Base64.decode(b64, android.util.Base64.DEFAULT)

    // ─── JSON-RPC envelope helpers ────────────────────────────────────────────

    private fun successResponse(id: Any, result: Any?): String {
        val obj = JSONObject()
        obj.put("jsonrpc", "2.0")
        obj.put("id", id)
        when (result) {
            null -> obj.put("result", JSONObject.NULL)
            is JSONObject -> obj.put("result", result)
            is JSONArray -> obj.put("result", result)
            is Boolean -> obj.put("result", result)
            is Number -> obj.put("result", result)
            is String -> obj.put("result", result)
            else -> obj.put("result", result.toString())
        }
        return obj.toString()
    }

    private fun paramsInvalidResponse(id: Any, method: String, reason: String): String =
        errorResponse(id, method, reason, "params_invalid")

    private fun errorResponse(id: Any, method: String, message: String): String =
        errorResponse(id, method, message, "native_error")

    private fun errorResponse(id: Any, method: String, message: String, kind: String): String {
        val obj = JSONObject()
        obj.put("jsonrpc", "2.0")
        obj.put("id", id)
        obj.put(
            "error",
            JSONObject().apply {
                put("kind", kind)
                put("method", method)
                put("reason", message)
                put("message", message)
            },
        )
        return obj.toString()
    }

    /** Wraps a JSON string so it can be safely embedded in a JS string literal. */
    private fun escapeForJs(json: String): String =
        "'${json.replace("\\", "\\\\").replace("'", "\\'")}'"
}

private class RpcParamsInvalidException(message: String) : IllegalArgumentException(message)
