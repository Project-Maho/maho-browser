package dev.maho.browser.support

import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.util.UUID
import java.util.concurrent.TimeUnit

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

    fun connect() {
        val req = Request.Builder().url(wsUrl).build()
        ws = client.newWebSocket(req, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                _connected.tryEmit(true)
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                val event = parseEvent(text)
                if (event != null) {
                    _events.tryEmit(event)
                }
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                _connected.tryEmit(false)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                _connected.tryEmit(false)
            }
        })
    }

    fun submitTask(goal: String, urlSeed: String? = null): String {
        val taskId = UUID.randomUUID().toString()
        val urlField = if (urlSeed != null) "\"$urlSeed\"" else "null"
        val escapedGoal = goal.replace("\\", "\\\\").replace("\"", "\\\"")
        val msg = """{"type":"task_submit","task_id":"$taskId","goal":"$escapedGoal","url_seed":$urlField}"""
        ws?.send(msg)
        return taskId
    }

    fun cancelTask(taskId: String) {
        val msg = """{"type":"task_cancel","task_id":"$taskId","reason":"user"}"""
        ws?.send(msg)
    }

    fun ping(seq: Long = System.currentTimeMillis()) {
        val msg = """{"type":"ping","seq":$seq}"""
        ws?.send(msg)
    }

    fun close() {
        ws?.close(1000, "client closing")
        ws = null
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
