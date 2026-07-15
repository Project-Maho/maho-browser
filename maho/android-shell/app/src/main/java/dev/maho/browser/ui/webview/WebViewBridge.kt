package dev.maho.browser.ui.webview

import android.webkit.JavascriptInterface
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

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
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val agentSessions = HandleRegistry()
    private val chatSessions = HandleRegistry()

    @JavascriptInterface
    fun rpc(requestJson: String) {
        scope.launch {
            val response = handleRequest(requestJson)
            evaluateJs("window.__mahoBridgeResponse(${escapeForJs(response)})")
        }
    }

    /** Called by native (e.g. back-button) to push a navigation signal to JS. */
    fun onBack() {
        evaluateJs("window.__mahoBridgeNav && window.__mahoBridgeNav('back')")
    }

    // ─── Request dispatch ────────────────────────────────────────────────────

    /** Visible for tests: parse → dispatch → envelope. */
    internal suspend fun handleRequest(requestJson: String): String {
        val req = runCatching { JSONObject(requestJson) }.getOrNull()
            ?: return errorResponse(JSONObject.NULL, "Invalid JSON")

        val id: Any = if (req.has("id") && !req.isNull("id")) req.get("id") else JSONObject.NULL
        val method = req.optString("method", "")
        val params = RpcParams.from(req.opt("params"))

        return try {
            successResponse(id, dispatchMethod(method, params))
        } catch (e: Exception) {
            errorResponse(id, e.message ?: "Unknown error")
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

                // --- Chat (new names) ---
                "chatSessionStart" -> chatSessionStart(p)
                "chatSessionResume" -> throw unsupported(method)
                "chatSessionFree" -> chatSessionFree(p)
                "chatSendMessage" -> chatSendMessage(p)
                "chatCancelTurn" -> native.chatCancel(chatPtr(p))
                "chatPollEvents" -> chatPollEventsArray(p)
                "chatRegisterTool" -> chatRegisterTool(p)
                "chatSendToolResult" -> chatSendToolResult(p)
                "chatAppendAssistantMessage" ->
                    native.chatAppendAssistantMessage(chatPtr(p), p.string("content", 1), "[]")
                "chatGetHistory" -> throw unsupported(method)

                // --- Chat (legacy aliases, pointer-positional) ---
                "chatSessionNew" -> native.chatSessionNew(
                    p.string("apiKey", 0), p.string("endpoint", 1), p.string("model", 2), p.string("systemInstruction", 3),
                )
                "chatSendUserTurn" -> native.chatSendUserTurn(p.long("handle", 0, 0L), p.string("message", 1))
                "chatCancel" -> native.chatCancel(p.long("handle", 0, 0L))
                "chatSessionPollEvent" -> native.chatSessionPollEvent(p.long("handle", 0, 0L))

                // --- Conversations (new + legacy) ---
                "conversationCreate" -> conversationCreate(p)
                "conversationList", "listConversations" ->
                    jsonArrayOrEmpty(native.listConversationsJson(p.long("limit", 0, 100L)))
                "conversationGet" -> conversationGet(p)
                "conversationDelete", "deleteConversation" -> native.deleteConversation(p.string("id", 0))
                "conversationRename", "renameConversation" ->
                    native.renameConversation(p.string("id", 0), p.string("title", 1))
                "conversationGetMessages", "getConversationMessages" ->
                    jsonArrayOrEmpty(native.getConversationMessagesJson(p.string("id", 0)))
                "createConversation" -> native.createConversation(
                    p.string("id", 0), p.optString("title", 1), p.optString("spaceId", 2), p.optString("model", 3),
                )
                "saveConversationMessage" -> native.saveConversationMessage(
                    p.string("sessionId", 0), p.string("role", 1), p.string("content", 2), p.optString("urlContext", 3),
                )

                // --- Space AI Config ---
                "getSpaceAIConfig" -> native.getSpaceAIConfigJson(p.string("spaceId", 0))?.let { JSONObject(it) }
                "setSpaceAIConfig" -> setSpaceAIConfig(p)

                // --- Offline Model ---
                "offlineModelInfo" -> native.offlineModelInfoJson()?.let { JSONObject(it) }
                "offlineModelIsDownloaded" -> native.offlineModelIsDownloaded()
                "offlineModelDelete" -> native.offlineModelDelete()

                // --- Agent Session ---
                "agentCreateSession" -> agentCreateSession(p)
                "agentSendMessage" -> native.agentSendMessage(agentPtr(p), p.string("message", 1))
                "agentCancel" -> native.agentCancel(agentPtr(p))
                "agentPollEvent" -> native.agentPollEvent(agentPtr(p))
                "agentListTools" -> jsonArrayOrEmpty(native.agentListTools(agentPtr(p)))
                "agentFreeSession" -> agentFreeSession(p)

                // --- Pinch: no Android backing ---
                "pinchEstimateCost" -> throw unsupported(method)

                // --- Native-only: resolved by NativeActionBridge, not here ---
                "capturePhoto", "captureScreenshot", "openSettings", "hapticFeedback" ->
                    throw UnsupportedOperationException("Method '$method' must be handled by NativeActionBridge")

                else -> throw IllegalArgumentException("Unknown method: $method")
            }
        }

    // ─── Agent handlers ──────────────────────────────────────────────────────

    private fun agentCreateSession(p: RpcParams): String {
        val ptr = native.agentCreateSession(p.string("sessionId", 0))
        if (ptr == 0L) throw IllegalStateException("agentCreateSession failed (native returned 0)")
        return agentSessions.register(ptr)
    }

    private fun agentFreeSession(p: RpcParams): Any? {
        val handle = p.string("handle", 0)
        agentSessions.unregister(handle)?.let { native.agentFreeSession(it) }
        return null
    }

    private fun agentPtr(p: RpcParams): Long {
        val handle = p.string("handle", 0)
        return agentSessions.resolve(handle)
            ?: throw IllegalArgumentException("Unknown agent handle: $handle")
    }

    // ─── Chat handlers ───────────────────────────────────────────────────────

    private fun chatSessionStart(p: RpcParams): String {
        val ptr = native.chatSessionNew(
            p.optString("apiKey", 0).orEmpty(),
            p.optString("endpoint", 1).orEmpty(),
            p.optString("model", 2).orEmpty(),
            p.optString("systemInstruction", 3).orEmpty(),
        )
        return chatSessions.register(ptr)
    }

    private fun chatSessionFree(p: RpcParams): Any? {
        val handle = p.string("handle", 0)
        chatSessions.unregister(handle)?.let { native.chatSessionFree(it) }
        return null
    }

    private fun chatPtr(p: RpcParams): Long {
        val handle = p.string("handle", 0)
        return chatSessions.resolve(handle)
            ?: throw IllegalArgumentException("Unknown chat handle: $handle")
    }

    private fun chatSendMessage(p: RpcParams): Boolean {
        val ptr = chatPtr(p)
        val content = p.optObject("content", 1)
            ?: throw IllegalArgumentException("Missing chat 'content'")
        return when (content.optString("kind")) {
            "text" -> native.chatSendUserTurn(ptr, content.optString("text"))
            "image" -> native.chatSendTextWithImage(
                ptr,
                content.optString("text"),
                content.optString("mime"),
                decodeBase64(content.optString("base64")),
            )
            else -> throw IllegalArgumentException("Unsupported chat content kind")
        }
    }

    private fun chatPollEventsArray(p: RpcParams): JSONArray {
        val single = native.chatSessionPollEvent(chatPtr(p)) ?: return JSONArray()
        return JSONArray().put(JSONObject(single))
    }

    private fun chatRegisterTool(p: RpcParams): Boolean {
        val ptr = chatPtr(p)
        val tool = p.optObject("tool", 1) ?: throw IllegalArgumentException("Missing 'tool'")
        val paramsJson = tool.optJSONObject("parameters")?.toString() ?: "{}"
        return native.chatRegisterTool(ptr, tool.optString("name"), tool.optString("description"), paramsJson)
    }

    private fun chatSendToolResult(p: RpcParams): Boolean {
        val ptr = chatPtr(p)
        val result = p.optObject("result", 2)?.toString() ?: "{}"
        return native.chatSendToolResult(ptr, p.string("toolCallId", 1), "", result, true)
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

    private fun conversationGet(p: RpcParams): JSONObject? {
        val id = p.string("id", 0)
        val list = native.listConversationsJson(100L)?.let { JSONArray(it) } ?: return null
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

    private fun errorResponse(id: Any, message: String): String {
        val obj = JSONObject()
        obj.put("jsonrpc", "2.0")
        obj.put("id", id)
        obj.put(
            "error",
            JSONObject().apply {
                put("kind", "native_error")
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
