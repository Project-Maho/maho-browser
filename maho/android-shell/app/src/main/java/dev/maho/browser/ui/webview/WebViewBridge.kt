package dev.maho.browser.ui.webview

import android.webkit.JavascriptInterface
import dev.maho.browser.MahoBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import kotlinx.serialization.encodeToString

/**
 * WebViewBridge — @JavascriptInterface glue between the web-ai bundle and MahoBridge.
 *
 * JavaScript calls:  window.MahoBridgeAndroid.rpc(jsonRpcRequestString)
 * Native resolves:   webView.evaluateJavascript("window.__mahoBridgeResponse('$json')", null)
 *
 * Implements the JSON-RPC 2.0 method surface defined in web-ai/src/bridge/types.ts.
 * All work that touches MahoBridge is dispatched to Dispatchers.IO; the result callback
 * is delivered on the main thread via webView.post { evaluateJavascript(...) }.
 *
 * New RPC methods: add a branch to dispatchMethod() and a corresponding handler fun.
 */
class WebViewBridge(
    private val bridge: MahoBridge,
    private val evaluateJs: (String) -> Unit,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

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

    private suspend fun handleRequest(requestJson: String): String {
        val req = runCatching { JSONObject(requestJson) }.getOrNull()
            ?: return errorResponse(-1, "Invalid JSON")

        val id = req.optInt("id", -1)
        val method = req.optString("method", "")
        val params = req.optJSONArray("params") ?: JSONArray()

        return try {
            val result = dispatchMethod(method, params)
            successResponse(id, result)
        } catch (e: Exception) {
            errorResponse(id, e.message ?: "Unknown error")
        }
    }

    private suspend fun dispatchMethod(method: String, params: JSONArray): Any? =
        withContext(Dispatchers.IO) {
            when (method) {
                // --- BYOK ---
                "byokGetProviders" -> byokGetProviders()
                "byokGetKey" -> byokGetKey(params.getString(0))
                "byokSetKey" -> bridge.byokSetKey(params.getString(0), params.getString(1))
                "byokDeleteKey" -> bridge.byokDeleteKey(params.getString(0))
                "byokValidateKey" -> bridge.byokValidateKey(params.getString(0), params.getString(1))

                // --- Chat ---
                "chatSessionNew" -> bridge.chatSessionNew(
                    params.getString(0),
                    params.getString(1),
                    params.getString(2),
                    params.getString(3),
                )
                "chatSendUserTurn" -> bridge.chatSendUserTurn(params.getLong(0), params.getString(1))
                "chatSendImage" -> bridge.chatSendImage(
                    params.getLong(0),
                    params.getString(1),
                    jsonArrayToByteArray(params.getJSONArray(2)),
                )
                "chatSendTextWithImage" -> bridge.chatSendTextWithImage(
                    params.getLong(0),
                    params.getString(1),
                    params.getString(2),
                    jsonArrayToByteArray(params.getJSONArray(3)),
                )
                "chatCancel" -> { bridge.chatCancel(params.getLong(0)); null }
                "chatSessionFree" -> { bridge.chatSessionFree(params.getLong(0)); null }
                "chatSessionPollEvent" -> bridge.chatSessionPollEvent(params.getLong(0))

                // --- Conversations ---
                "createConversation" -> bridge.createConversation(
                    params.getString(0),
                    params.optString(1).ifBlank { null },
                    params.optString(2).ifBlank { null },
                    params.optString(3).ifBlank { null },
                )
                "listConversations" -> listConversations(params.optLong(0, 100L))
                "getConversationMessages" -> getConversationMessages(params.getString(0))
                "saveConversationMessage" -> bridge.saveConversationMessage(
                    params.getString(0),
                    params.getString(1),
                    params.getString(2),
                    params.optString(3).ifBlank { null },
                )
                "deleteConversation" -> bridge.deleteConversation(params.getString(0))
                "renameConversation" -> bridge.renameConversation(params.getString(0), params.getString(1))

                // --- Space AI Config ---
                "getSpaceAIConfig" -> bridge.getSpaceAIConfig(params.getString(0))?.let {
                    JSONObject(dev.maho.browser.support.MahoJson.instance.encodeToString(it))
                }
                "setSpaceAIConfig" -> {
                    val config = dev.maho.browser.support.MahoJson.instance
                        .decodeFromString<dev.maho.browser.SpaceAIConfig>(params.getString(1))
                    bridge.setSpaceAIConfig(params.getString(0), config)
                }

                // --- Offline Model ---
                "offlineModelInfo" -> bridge.offlineModelInfo()?.let {
                    JSONObject(dev.maho.browser.support.MahoJson.instance.encodeToString(it))
                }
                "offlineModelIsDownloaded" -> bridge.offlineModelIsDownloaded()
                "offlineModelDelete" -> bridge.offlineModelDelete()

                // --- Native-only stubs (resolved by the AIWebViewFragment, not here) ---
                "capturePhoto", "captureScreenshot", "openSettings", "hapticFeedback" ->
                    throw UnsupportedOperationException("Method '$method' must be handled by AIWebViewFragment")

                else -> throw IllegalArgumentException("Unknown method: $method")
            }
        }

    // ─── Helpers ─────────────────────────────────────────────────────────────

    private fun byokGetProviders(): JSONArray {
        val raw = bridge.byokGetProviders() ?: return JSONArray()
        return runCatching { JSONArray(raw) }.getOrDefault(JSONArray())
    }

    private fun byokGetKey(provider: String): String? = bridge.byokGetKey(provider)

    private fun listConversations(limit: Long): JSONArray {
        val sessions = bridge.listConversations(limit)
        val json = dev.maho.browser.support.MahoJson.instance.encodeToString(sessions)
        return JSONArray(json)
    }

    private fun getConversationMessages(sessionId: String): JSONArray {
        val turns = bridge.getConversationMessages(sessionId)
        val json = dev.maho.browser.support.MahoJson.instance.encodeToString(turns)
        return JSONArray(json)
    }

    private fun jsonArrayToByteArray(arr: JSONArray): ByteArray {
        val bytes = ByteArray(arr.length())
        for (i in 0 until arr.length()) {
            bytes[i] = arr.getInt(i).toByte()
        }
        return bytes
    }

    // ─── JSON-RPC envelope helpers ────────────────────────────────────────────

    private fun successResponse(id: Int, result: Any?): String {
        val obj = JSONObject()
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

    private fun errorResponse(id: Int, message: String): String {
        val obj = JSONObject()
        obj.put("id", id)
        obj.put("error", JSONObject().apply { put("message", message) })
        return obj.toString()
    }

    /** Wraps a JSON string so it can be safely embedded in a JS string literal. */
    private fun escapeForJs(json: String): String =
        "'${json.replace("\\", "\\\\").replace("'", "\\'")}'"
}
