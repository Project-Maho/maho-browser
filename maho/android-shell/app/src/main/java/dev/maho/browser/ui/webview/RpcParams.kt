package dev.maho.browser.ui.webview

import org.json.JSONArray
import org.json.JSONObject

/**
 * RpcParams — reads JSON-RPC params that may arrive either as a NAMED OBJECT
 * (current web-ai bundle, e.g. `{"handle":"..."}`) or a POSITIONAL ARRAY
 * (older bundles / [callBridge], e.g. `["..."]`).
 *
 * Every accessor takes both a param name (object form) and an index (array
 * form); whichever backing store is present is consulted. This is the single
 * shim that makes [WebViewBridge] dual-mode.
 */
class RpcParams private constructor(
    private val obj: JSONObject?,
    private val arr: JSONArray?,
) {
    companion object {
        fun from(raw: Any?): RpcParams = when (raw) {
            is JSONObject -> RpcParams(raw, null)
            is JSONArray -> RpcParams(null, raw)
            else -> RpcParams(JSONObject(), null)
        }
    }

    fun optString(name: String, index: Int): String? = when {
        obj != null -> if (obj.has(name) && !obj.isNull(name)) obj.getString(name) else null
        arr != null -> if (index < arr.length() && !arr.isNull(index)) arr.getString(index) else null
        else -> null
    }

    fun string(name: String, index: Int): String =
        optString(name, index) ?: throw IllegalArgumentException("Missing required param '$name'")

    fun has(name: String, index: Int): Boolean = when {
        obj != null -> obj.has(name)
        arr != null -> index < arr.length()
        else -> false
    }

    fun raw(name: String, index: Int): Any? = when {
        obj != null -> if (obj.has(name)) obj.get(name) else null
        arr != null -> if (index < arr.length()) arr.get(index) else null
        else -> null
    }

    fun long(name: String, index: Int, default: Long): Long = when {
        obj != null -> if (obj.has(name) && !obj.isNull(name)) obj.getLong(name) else default
        arr != null -> if (index < arr.length() && !arr.isNull(index)) arr.getLong(index) else default
        else -> default
    }

    fun boolean(name: String, index: Int, default: Boolean): Boolean = when {
        obj != null -> if (obj.has(name)) obj.getBoolean(name) else default
        arr != null -> if (index < arr.length()) arr.getBoolean(index) else default
        else -> default
    }

    fun optObject(name: String, index: Int): JSONObject? = when {
        obj != null -> obj.optJSONObject(name)
        arr != null -> arr.optJSONObject(index)
        else -> null
    }

    fun optArray(name: String, index: Int): JSONArray? = when {
        obj != null -> obj.optJSONArray(name)
        arr != null -> arr.optJSONArray(index)
        else -> null
    }
}
