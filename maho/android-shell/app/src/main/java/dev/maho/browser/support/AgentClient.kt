package dev.maho.browser.support

import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.UUID
import java.util.concurrent.TimeUnit

/** D8: cleartext emulator relay is a debug-build-only default; release talks to the production relay over TLS. */
internal fun defaultAgentRelayUrl(debug: Boolean): String =
    if (debug) "ws://10.0.2.2:8080/ws/agent" else "wss://relay.mahobrowser.com/ws/agent"

/** D8: built with org.json so goal/url_seed (quotes, backslashes, newlines) are always escaped. */
internal fun buildTaskSubmitJson(taskId: String, goal: String, urlSeed: String?): String =
    JSONObject()
        .put("type", "task_submit")
        .put("task_id", taskId)
        .put("goal", goal)
        .put("url_seed", urlSeed ?: JSONObject.NULL)
        .toString()

internal fun buildTaskCancelJson(taskId: String): String =
    JSONObject().put("type", "task_cancel").put("task_id", taskId).put("reason", "user").toString()

data class AgentEvent(
    val taskId: String,
    val type: String,
    val state: String?,
    val message: String?,
    val summary: String?,
    val raw: String,
)

class AgentClient(private val wsUrl: String) {
    private val client = OkHttpClient.Builder()
        .readTimeout(0, TimeUnit.MILLISECONDS)
        .build()

    private var ws: WebSocket? = null

    @Volatile
    private var connectionGeneration = 0

    private val _events = MutableSharedFlow<AgentEvent>(
        replay = 0,
        extraBufferCapacity = 64,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val events: SharedFlow<AgentEvent> = _events

    private val _connected = MutableSharedFlow<Boolean>(
        replay = 1,
        extraBufferCapacity = 1,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val connected: SharedFlow<Boolean> = _connected

    @Synchronized
    fun connect() {
        // D8: never leak the previous socket when connect() is called again.
        ws?.cancel()
        ws = null
        val generation = ++connectionGeneration
        val req = Request.Builder().url(wsUrl).build()
        ws = client.newWebSocket(req, object : WebSocketListener() {
            // Late callbacks from a replaced/closed socket must not flip the live connection state.
            private fun isCurrent(@Suppress("UNUSED_PARAMETER") webSocket: WebSocket) =
                generation == connectionGeneration

            override fun onOpen(webSocket: WebSocket, response: Response) {
                if (isCurrent(webSocket)) _connected.tryEmit(true)
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                if (!isCurrent(webSocket)) return
                val event = parseEvent(text)
                if (event != null) {
                    _events.tryEmit(event)
                }
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                if (isCurrent(webSocket)) _connected.tryEmit(false)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (isCurrent(webSocket)) _connected.tryEmit(false)
            }
        })
    }

    /** Returns the task id, or null when the socket is absent or refused the frame (not connected). */
    fun submitTask(goal: String, urlSeed: String? = null): String? {
        val taskId = UUID.randomUUID().toString()
        val sent = ws?.send(buildTaskSubmitJson(taskId, goal, urlSeed)) ?: false
        return if (sent) taskId else null
    }

    fun cancelTask(taskId: String) {
        ws?.send(buildTaskCancelJson(taskId))
    }

    fun ping(seq: Long = System.currentTimeMillis()) {
        val msg = """{"type":"ping","seq":$seq}"""
        ws?.send(msg)
    }

    @Synchronized
    fun close() {
        connectionGeneration++
        ws?.close(1000, "client closing")
        ws = null
        _connected.tryEmit(false)
    }

    private fun parseEvent(raw: String): AgentEvent? {
        return try {
            val taskId = extractJsonString(raw, "task_id") ?: ""
            val type = extractJsonString(raw, "type") ?: "unknown"
            val state = extractJsonString(raw, "state")
            val message = extractJsonString(raw, "message")
            val summary = extractJsonString(raw, "summary")
            AgentEvent(
                taskId = taskId,
                type = type,
                state = state,
                message = message,
                summary = summary,
                raw = raw,
            )
        } catch (_: Exception) {
            null
        }
    }

    private fun extractJsonString(json: String, key: String): String? {
        val pattern = "\"$key\"\\s*:\\s*\"([^\"]*)\""
        val match = Regex(pattern).find(json) ?: return null
        return match.groupValues[1]
    }
}
