package dev.maho.browser.bridge

import android.app.Activity
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.webkit.JavascriptInterface
import android.webkit.WebView
import dev.maho.browser.MahoBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.ConcurrentHashMap

/**
 * JSON-RPC 2.0 bridge between the web-ai TypeScript bundle and MahoBridge JNI.
 *
 * JavaScript calls: `window.MahoBridgeAndroid.rpc(jsonRpcEnvelope)`
 * Native resolves:  `window.__mahoBridgeResponse('...')` on the UI thread.
 *
 * All chat session pointers are managed through [SessionRegistry] — the JS side
 * only ever sees opaque UUID-v4 string handles (never raw Long pointers).
 *
 * Dispatches 27 methods across 6 groups: BYOK (5), Chat (10), Conversation (6),
 * Space AI Config (2), Offline Model (3), Pinch (1).
 */
class WebViewBridge private constructor(
    private val webView: WebView,
    private val activity: Activity,
    private val mahoBridge: MahoBridge?,
    sessionFreeFn: (Long) -> Unit,
) {
    val sessionRegistry = SessionRegistry(sessionFreeFn)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    constructor(
        webView: WebView,
        activity: Activity,
        mahoBridge: MahoBridge,
    ) : this(webView, activity, mahoBridge as MahoBridge?, BridgeChat::chatSessionFree)

    /** Test-only constructor that avoids loading the native library. */
    internal constructor(
        webView: WebView,
        activity: Activity,
    ) : this(webView, activity, null, { })

    private fun requireBridge(): MahoBridge = mahoBridge
        ?: error("MahoBridge not available in test-only mode")

    private fun aiBridge(): BridgeAi {
        requireBridge()
        return BridgeAi
    }

    private fun chatBridge(): BridgeChat {
        requireBridge()
        return BridgeChat
    }

    private fun conversationsBridge(): BridgeConversations {
        requireBridge()
        return BridgeConversations
    }

    // ─── Streaming infrastructure ───────────────────────────────────────────────

    private val streamThread = HandlerThread("MahoStreamFlush").apply { start() }
    private val streamHandler = Handler(streamThread.looper)
    private val streams = ConcurrentHashMap<String, StreamSession>()

    /**
     * Single entry point for all JSON-RPC 2.0 calls from JavaScript.
     *
     * Envelope format:
     * ```json
     * { "jsonrpc": "2.0", "id": "uuid", "method": "byokGetKey", "params": { "provider": "openai" } }
     * ```
     */
    @JavascriptInterface
    fun rpc(envelope: String) {
        scope.launch {
            val response = handleEnvelope(envelope)
            val escaped = escapeForJs(response)
            scheduleJsEvaluation("window.__mahoBridgeResponse($escaped)")
        }
    }

    /** Called by native (e.g. back-button) to push a navigation signal to JS. */
    fun onBack() {
        activity.runOnUiThread {
            webView.evaluateJavascript(
                "window.__mahoBridgeNav && window.__mahoBridgeNav('back')",
                null,
            )
        }
    }

    // ─── Streaming: coalescing token push (A6 protocol) ─────────────────────────

    /**
     * Per-session streaming state. Coalesces token chunks via a bounded buffer
     * and flushes to JS on a shared [streamHandler] thread.
     *
     * Threading contract:
     * - [pushToken] may be called from any thread; acquires [lock] briefly.
     * - [doFlush] MUST be called inside `synchronized(lock)`. It snapshots and clears
     *   the buffer, then posts to UI thread. The lock is NOT held across evaluateJavascript.
     * - [complete]/[error] flush pending tokens then dispatch terminal JS call.
     */
    private inner class StreamSession(val sessionId: String) {
        private val buffer = ArrayDeque<String>()
        private var droppedCount = 0
        private var flushScheduled = false
        private val thinkingBuffer = ArrayDeque<String>()
        private var thinkingFlushScheduled = false
        private val lock = Any()

        fun pushToken(token: String) {
            val snapshot: List<String>?
            val drops: Int
            synchronized(lock) {
                buffer.addLast(token)
                if (buffer.size > MAX_BUFFER_SIZE) {
                    buffer.removeFirst()
                    droppedCount++
                }
                if (buffer.size >= MAX_BATCH_SIZE) {
                    snapshot = buffer.toList()
                    drops = droppedCount
                    buffer.clear()
                    droppedCount = 0
                    flushScheduled = false
                    streamHandler.removeCallbacksAndMessages(this)
                } else {
                    snapshot = null
                    drops = 0
                    if (!flushScheduled) {
                        flushScheduled = true
                        streamHandler.postDelayed({ tryFlush() }, FLUSH_INTERVAL_MS)
                    }
                }
            }
            if (snapshot != null) {
                dispatchBatch(snapshot, drops)
            }
        }

        fun pushThinking(thinking: String) {
            val snapshot: List<String>?
            synchronized(lock) {
                thinkingBuffer.addLast(thinking)
                if (thinkingBuffer.size > MAX_BUFFER_SIZE) {
                    thinkingBuffer.removeFirst()
                }
                if (thinkingBuffer.size >= MAX_BATCH_SIZE) {
                    snapshot = thinkingBuffer.toList()
                    thinkingBuffer.clear()
                    thinkingFlushScheduled = false
                    streamHandler.removeCallbacksAndMessages(this)
                } else {
                    snapshot = null
                    if (!thinkingFlushScheduled) {
                        thinkingFlushScheduled = true
                        streamHandler.postDelayed({ tryFlushThinking() }, FLUSH_INTERVAL_MS)
                    }
                }
            }
            if (snapshot != null) {
                dispatchThinkingBatch(snapshot)
            }
        }

        private fun tryFlushThinking() {
            val snapshot: List<String>
            synchronized(lock) {
                thinkingFlushScheduled = false
                if (thinkingBuffer.isEmpty()) return
                snapshot = thinkingBuffer.toList()
                thinkingBuffer.clear()
            }
            dispatchThinkingBatch(snapshot)
        }

        private fun dispatchThinkingBatch(chunks: List<String>) {
            val combined = chunks.joinToString("")
            val sidJson = JSONObject.quote(sessionId)
            val thinkingJson = JSONObject.quote(combined)
            val js = "window.__mahoStreamThinking($sidJson, $thinkingJson)"
            dispatchOnUiThread { webView.evaluateJavascript(js, null) }
        }

        private fun tryFlush() {
            val snapshot: List<String>
            val drops: Int
            synchronized(lock) {
                flushScheduled = false
                if (buffer.isEmpty()) return
                snapshot = buffer.toList()
                drops = droppedCount
                buffer.clear()
                droppedCount = 0
            }
            dispatchBatch(snapshot, drops)
        }

        private fun dispatchBatch(tokens: List<String>, drops: Int) {
            val tokensJson = JSONArray(tokens).toString()
            val sidJson = JSONObject.quote(sessionId)
            val js = if (drops > 0) {
                "window.__mahoStreamBatch($sidJson, $tokensJson, $drops)"
            } else {
                "window.__mahoStreamBatch($sidJson, $tokensJson)"
            }
            dispatchOnUiThread { webView.evaluateJavascript(js, null) }
        }

        fun complete(finalMessage: String) {
            val pendingSnapshot: List<String>?
            val drops: Int
            val pendingThinking: List<String>?
            synchronized(lock) {
                flushScheduled = false
                thinkingFlushScheduled = false
                streamHandler.removeCallbacksAndMessages(this)
                if (buffer.isNotEmpty()) {
                    pendingSnapshot = buffer.toList()
                    drops = droppedCount
                    buffer.clear()
                    droppedCount = 0
                } else {
                    pendingSnapshot = null
                    drops = 0
                }
                if (thinkingBuffer.isNotEmpty()) {
                    pendingThinking = thinkingBuffer.toList()
                    thinkingBuffer.clear()
                } else {
                    pendingThinking = null
                }
            }
            if (pendingSnapshot != null) {
                dispatchBatch(pendingSnapshot, drops)
            }
            if (pendingThinking != null) {
                dispatchThinkingBatch(pendingThinking)
            }
            val sidJson = JSONObject.quote(sessionId)
            val msgJson = JSONObject.quote(finalMessage)
            dispatchOnUiThread {
                webView.evaluateJavascript(
                    "window.__mahoStreamComplete($sidJson, $msgJson)",
                    null,
                )
            }
        }

        fun error(message: String) {
            val pendingSnapshot: List<String>?
            val drops: Int
            val pendingThinking: List<String>?
            synchronized(lock) {
                flushScheduled = false
                thinkingFlushScheduled = false
                streamHandler.removeCallbacksAndMessages(this)
                if (buffer.isNotEmpty()) {
                    pendingSnapshot = buffer.toList()
                    drops = droppedCount
                    buffer.clear()
                    droppedCount = 0
                } else {
                    pendingSnapshot = null
                    drops = 0
                }
                if (thinkingBuffer.isNotEmpty()) {
                    pendingThinking = thinkingBuffer.toList()
                    thinkingBuffer.clear()
                } else {
                    pendingThinking = null
                }
            }
            if (pendingSnapshot != null) {
                dispatchBatch(pendingSnapshot, drops)
            }
            if (pendingThinking != null) {
                dispatchThinkingBatch(pendingThinking)
            }
            val sidJson = JSONObject.quote(sessionId)
            val errJson = JSONObject.quote(message)
            dispatchOnUiThread {
                webView.evaluateJavascript(
                    "window.__mahoStreamError($sidJson, $errJson)",
                    null,
                )
            }
        }
    }

    private val jsQueue = java.util.concurrent.ConcurrentLinkedQueue<String>()
    private val jsFlushScheduled = java.util.concurrent.atomic.AtomicBoolean(false)

    private fun scheduleJsEvaluation(js: String, immediate: Boolean = false) {
        jsQueue.add(js)
        if (immediate || jsQueue.size >= MAX_BATCH_SIZE) {
            flushJsQueue()
        } else {
            if (jsFlushScheduled.compareAndSet(false, true)) {
                dispatchOnUiThread {
                    flushJsQueue()
                }
            }
        }
    }

    private fun flushJsQueue() {
        jsFlushScheduled.set(false)
        if (jsQueue.isEmpty()) return
        dispatchOnUiThread {
            while (true) {
                val js = jsQueue.poll() ?: break
                webView.evaluateJavascript(js, null)
            }
        }
    }

    fun pushToken(sessionId: String, token: String) {
        streams.getOrPut(sessionId) { StreamSession(sessionId) }.pushToken(token)
    }

    fun pushToolDelta(sessionId: String, deltaJson: String) {
        val sidJson = JSONObject.quote(sessionId)
        scheduleJsEvaluation("window.__mahoStreamToolDelta($sidJson, $deltaJson)")
    }

    fun pushImageDelta(sessionId: String, deltaJson: String) {
        val sidJson = JSONObject.quote(sessionId)
        scheduleJsEvaluation("window.__mahoStreamImageDelta($sidJson, $deltaJson)")
    }

    fun pushThinking(sessionId: String, thinking: String) {
        streams.getOrPut(sessionId) { StreamSession(sessionId) }.pushThinking(thinking)
    }

    fun pushStreamComplete(sessionId: String, finalMessage: String) {
        streams.remove(sessionId)?.complete(finalMessage)
    }

    fun pushStreamError(sessionId: String, error: String) {
        streams.remove(sessionId)?.error(error)
    }

    fun shutdownStreaming() {
        streams.clear()
        streamHandler.removeCallbacksAndMessages(null)
        streamThread.quitSafely()
        flushJsQueue()
    }

    private fun dispatchOnUiThread(action: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) action()
        else activity.runOnUiThread(action)
    }

    // ─── Envelope handling ──────────────────────────────────────────────────────

    private suspend fun handleEnvelope(envelope: String): String {
        val req = runCatching { JSONObject(envelope) }.getOrNull()
            ?: return errorEnvelope("?", BridgeError.ParamsInvalid("?", "invalid JSON"))

        val id = req.optString("id", "?")
        val method = req.optString("method", "")
        val params = req.optJSONObject("params")

        return try {
            val result = dispatch(method, params)
            successEnvelope(id, result)
        } catch (e: BridgeError) {
            errorEnvelope(id, e)
        } catch (t: Throwable) {
            errorEnvelope(id, BridgeError.NativeError(method, t.message ?: "unknown"))
        }
    }

    // ─── 27-method dispatch ─────────────────────────────────────────────────────

    private suspend fun dispatch(method: String, params: JSONObject?): Any? =
        withContext(Dispatchers.IO) {
            when (method) {
                // ── BYOK (5) ──
                "byokGetProviders" -> {
                    val raw = aiBridge().byokGetProviders()
                    if (raw != null) JSONArray(raw) else JSONArray()
                }
                "byokGetKey" -> aiBridge().byokGetKey(params.req(method, "provider"))
                "byokSetKey" -> {
                    aiBridge().byokSetKey(params.req(method, "provider"), params.req(method, "key"))
                    null
                }
                "byokDeleteKey" -> {
                    aiBridge().byokDeleteKey(params.req(method, "provider"))
                    null
                }
                "byokValidateKey" -> {
                    val valid = aiBridge().byokValidateKey(
                        params.req(method, "provider"),
                        params.req(method, "key"),
                    )
                    JSONObject().put("valid", valid)
                }

                // ── Chat (10) — session handle translation via SessionRegistry ──
                "chatSessionStart" -> {
                    val model = params.req(method, "model")
                    val systemInstruction = params.opt("systemInstruction") ?: ""
                    val apiKey = params.opt("apiKey") ?: ""
                    val endpoint = params.opt("endpoint") ?: ""
                    val rawPtr = chatBridge().chatSessionNew(apiKey, endpoint, model, systemInstruction)
                    sessionRegistry.register(rawPtr) // returns UUID string
                }
                "chatSessionResume" -> {
                    val handle = params.req(method, "handle")
                    resolveSession(handle) // validate handle exists
                    // Resume is a no-op — the native session pointer is still valid.
                    null
                }
                "chatSessionFree" -> {
                    val handle = params.req(method, "handle")
                    sessionRegistry.release(handle)
                    null
                }
                "chatSendMessage" -> {
                    val ptr = resolveSession(params.req(method, "handle"))
                    val content = params?.optJSONObject("content")
                    if (content != null) {
                        val kind = content.optString("kind", "text")
                        when (kind) {
                            "text" -> {
                                val text = content.optString("text", "")
                                chatBridge().chatSendUserTurn(ptr, text)
                            }
                            "image" -> {
                                val mime = content.optString("mime", "image/png")
                                val b64 = content.optString("base64", "")
                                val text = content.optString("text", "")
                                val data = android.util.Base64.decode(b64, android.util.Base64.DEFAULT)
                                if (text.isNotEmpty()) {
                                    chatBridge().chatSendTextWithImage(ptr, text, mime, data)
                                } else {
                                    chatBridge().chatSendImage(ptr, mime, data)
                                }
                            }
                            else -> chatBridge().chatSendUserTurn(ptr, content.toString())
                        }
                    } else {
                        // Fallback: treat params.content as a string
                        val text = params.opt("content") ?: ""
                        chatBridge().chatSendUserTurn(ptr, text)
                    }
                    null
                }
                "chatCancelTurn" -> {
                    val ptr = resolveSession(params.req(method, "handle"))
                    chatBridge().chatCancel(ptr)
                    null
                }
                "chatPollEvents" -> {
                    val handle = params.req(method, "handle")
                    val ptr = resolveSession(handle)
                    val raw = chatBridge().chatSessionPollEvent(ptr)
                    if (raw != null) {
                        routeStreamEvents(handle, raw)
                        JSONArray("[$raw]")
                    } else {
                        JSONArray()
                    }
                }
                "chatRegisterTool" -> {
                    val ptr = resolveSession(params.req(method, "handle"))
                    val tool = params?.optJSONObject("tool")
                        ?: throw BridgeError.ParamsInvalid(method, "missing 'tool' object")
                    val name = tool.optString("name", "")
                    val description = tool.optString("description", "")
                    val parametersJson = tool.optJSONObject("parameters")?.toString() ?: "{}"
                    chatBridge().chatRegisterTool(ptr, name, description, parametersJson)
                    null
                }
                "chatSendToolResult" -> {
                    val ptr = resolveSession(params.req(method, "handle"))
                    val toolCallId = params.req(method, "toolCallId")
                    val resultObj = params?.optJSONObject("result")
                        ?: throw BridgeError.ParamsInvalid(method, "missing 'result' object")
                    val output = resultObj.optString("output", "")
                    // toolName is not provided by A2; use empty string
                    // trigger defaults to true (continues generation after tool result)
                    chatBridge().chatSendToolResult(ptr, toolCallId, "", output, true)
                    null
                }
                "chatAppendAssistantMessage" -> {
                    val ptr = resolveSession(params.req(method, "handle"))
                    val content = params.req(method, "content")
                    chatBridge().chatAppendAssistantMessage(ptr, content, "[]")
                    null
                }
                "chatGetHistory" -> {
                    val ptr = resolveSession(params.req(method, "handle"))
                    // Poll all events — this returns the session's serialized history
                    val raw = chatBridge().chatSessionPollEvent(ptr)
                    if (raw != null) JSONArray("[$raw]") else JSONArray()
                }

                // ── Conversation CRUD (6) ──
                "conversationCreate" -> {
                    val meta = params?.optJSONObject("meta")
                        ?: throw BridgeError.ParamsInvalid(method, "missing 'meta' field")
                    val id = meta.req(method, "id")
                    val title = meta.optString("title", "")
                    val spaceId: String? = meta.optString("spaceId", "").ifEmpty { null }
                    val model: String? = meta.optString("model", "").ifEmpty { null }
                    conversationsBridge().createConversation(id, title, spaceId, model)
                    id
                }
                "conversationList" -> {
                    val sessions = conversationsBridge().listConversations(100L)
                    val arr = JSONArray()
                    for (session in sessions) {
                        arr.put(JSONObject().apply {
                            put("id", session.id)
                            put("title", session.title ?: "")
                            put("spaceId", session.space_id ?: JSONObject.NULL)
                            put("model", session.model ?: JSONObject.NULL)
                            put("createdAt", session.created_at)
                            put("updatedAt", session.last_message_at)
                        })
                    }
                    arr
                }
                "conversationGet" -> {
                    val id = params.req(method, "id")
                    val sessions = conversationsBridge().listConversations(1000L)
                    val match = sessions.find { it.id == id }
                    if (match != null) {
                        JSONObject().apply {
                            put("id", match.id)
                            put("title", match.title ?: "")
                            put("spaceId", match.space_id ?: JSONObject.NULL)
                            put("model", match.model ?: JSONObject.NULL)
                            put("createdAt", match.created_at)
                            put("updatedAt", match.last_message_at)
                        }
                    } else {
                        JSONObject.NULL
                    }
                }
                "conversationDelete" -> {
                    conversationsBridge().deleteConversation(params.req(method, "id"))
                    null
                }
                "conversationRename" -> {
                    conversationsBridge().renameConversation(params.req(method, "id"), params.req(method, "title"))
                    null
                }
                "conversationGetMessages" -> {
                    val turns = conversationsBridge().getConversationMessages(params.req(method, "id"))
                    val arr = JSONArray()
                    for (turn in turns) {
                        arr.put(JSONObject().apply {
                            put("id", turn.id)
                            put("role", turn.role)
                            put("content", turn.content)
                            put("createdAt", turn.created_at ?: "")
                        })
                    }
                    arr
                }

                // ── Space AI Config (2) ──
                "getSpaceAIConfig" -> {
                    val json = requireBridge().getSpaceAIConfigJson(params.req(method, "spaceId"))
                    if (json != null) JSONObject(json) else JSONObject.NULL
                }
                "setSpaceAIConfig" -> {
                    val spaceId = params.req(method, "spaceId")
                    val config = params?.optJSONObject("config")?.toString()
                        ?: throw BridgeError.ParamsInvalid(method, "missing 'config' object")
                    requireBridge().setSpaceAIConfig(spaceId, config)
                    null
                }

                // ── Pinch (1) ──
                "pinchEstimateCost" -> {
                    // Stub — estimateSummarizeCost JNI not yet wired; return placeholder
                    val model = params.opt("model") ?: "unknown"
                    val pageContent = params.opt("pageContent") ?: ""
                    val inputTokens = pageContent.length / 4 // rough estimate
                    val outputTokens = minOf(inputTokens / 4, 1024)
                    JSONObject().apply {
                        put("inputTokens", inputTokens)
                        put("outputTokens", outputTokens)
                        put("estimatedCostUsd", 0.0)
                        put("model", model)
                    }
                }

                else -> throw BridgeError.MethodUnknown(method)
            }
        }

    // ─── Session resolution helper ──────────────────────────────────────────────

    private fun resolveSession(handle: String): Long =
        sessionRegistry.get(handle) ?: throw BridgeError.SessionInvalid(handle)

    // ─── Poll → push streaming router ──────────────────────────────────────────

    private fun routeStreamEvents(sessionId: String, raw: String) {
        val event = runCatching { JSONObject(raw) }.getOrNull() ?: return
        when (event.optString("kind", "")) {
            "token" -> {
                val token = event.optString("token", "")
                if (token.isNotEmpty()) pushToken(sessionId, token)
            }
            "thinking" -> pushThinking(sessionId, event.optString("data", ""))
            "tool_call_delta" -> pushToolDelta(sessionId, raw)
            "image_delta" -> pushImageDelta(sessionId, raw)
            "complete" -> {
                val finalMessage = event.optString("message", "")
                pushStreamComplete(sessionId, finalMessage)
            }
            "error" -> {
                val error = event.optString("message", event.optString("error", "unknown"))
                pushStreamError(sessionId, error)
            }
        }
    }

    // ─── Param extraction helpers ───────────────────────────────────────────────

    private fun JSONObject?.req(method: String, field: String): String {
        if (this == null || !this.has(field) || this.isNull(field)) {
            throw BridgeError.ParamsInvalid(method, "missing required field '$field'")
        }
        return this.getString(field)
    }

    private fun JSONObject?.opt(field: String): String? {
        if (this == null || !this.has(field) || this.isNull(field)) return null
        val value = this.optString(field, "")
        return value.ifEmpty { null }
    }

    // ─── JSON-RPC 2.0 envelope construction ─────────────────────────────────────

    private fun successEnvelope(id: String, result: Any?): String {
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

    private fun errorEnvelope(id: String, error: BridgeError): String {
        val obj = JSONObject()
        obj.put("jsonrpc", "2.0")
        obj.put("id", id)
        obj.put("error", error.toEnvelopeJson())
        return obj.toString()
    }

    /** Wraps a JSON string for safe embedding in a JS string literal. */
    private fun escapeForJs(json: String): String =
        "'${json.replace("\\", "\\\\").replace("'", "\\'")}'"

    /**
     * Releases all sessions and shuts down streaming infrastructure.
     * Called from [AIWebViewFragment.onDestroyView] via [sessionRegistry.releaseAll].
     */
    fun releaseAll() {
        sessionRegistry.releaseAll()
        shutdownStreaming()
    }

    companion object {
        internal const val MAX_BATCH_SIZE = 32
        internal const val MAX_BUFFER_SIZE = 256
        internal const val FLUSH_INTERVAL_MS = 60L
    }
}

// ─── Error types ────────────────────────────────────────────────────────────────

sealed class BridgeError(message: String) : Exception(message) {
    abstract fun toEnvelopeJson(): JSONObject

    data class SessionInvalid(val id: String) : BridgeError("invalid session: $id") {
        override fun toEnvelopeJson() = JSONObject().apply {
            put("kind", "session_invalid")
            put("id", id)
        }
    }

    data class MethodUnknown(val method: String) : BridgeError("unknown method: $method") {
        override fun toEnvelopeJson() = JSONObject().apply {
            put("kind", "method_unknown")
            put("method", method)
        }
    }

    data class ParamsInvalid(val method: String, val reason: String) :
        BridgeError("invalid params for $method: $reason") {
        override fun toEnvelopeJson() = JSONObject().apply {
            put("kind", "params_invalid")
            put("method", method)
            put("reason", reason)
        }
    }

    data class NativeError(val method: String, val reason: String) :
        BridgeError("native error in $method: $reason") {
        override fun toEnvelopeJson() = JSONObject().apply {
            put("kind", "native_error")
            put("method", method)
            put("reason", reason)
        }
    }
}
