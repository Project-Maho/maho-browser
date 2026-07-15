package dev.maho.browser.ui.webview

import android.annotation.SuppressLint
import android.content.Context
import android.webkit.WebSettings
import android.webkit.WebView

/**
 * Handle to a configured web-ai [WebView] plus its RPC bridge, so hosts (the
 * [AIWebViewFragment] and the Compose [WebAgentOverlay]) share one setup path.
 */
class AiWebViewHost internal constructor(
    val webView: WebView,
    private val rpcBridge: WebViewBridge,
) {
    /** Push a 'back' navigation signal into the web bundle. */
    fun onBack() = rpcBridge.onBack()

    fun destroy() = webView.destroy()
}

/**
 * AiWebViewFactory — builds a WebView wired to [MahoNativeBridge] via
 * [WebViewBridge] (as `MahoBridgeAndroid`) and [NativeActionBridge] (as
 * `MahoBridgeNative`), then loads `web-ai/index.html#<screen>[?params]`.
 *
 * Single source of truth for web-ai WebView configuration.
 */
object AiWebViewFactory {
    private const val ASSET_URL = "file:///android_asset/web-ai/index.html"
    private const val DEV_URL = "http://10.0.2.2:5173/"

    @SuppressLint("SetJavaScriptEnabled")
    fun createHost(context: Context, screen: String, params: String = ""): AiWebViewHost {
        val wv = WebView(context).apply {
            settings.apply {
                javaScriptEnabled = true
                domStorageEnabled = true
                allowFileAccess = true
                mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
                cacheMode = WebSettings.LOAD_DEFAULT
            }
        }

        val rpcBridge = WebViewBridge(
            native = MahoNativeBridge,
            evaluateJs = { js -> wv.post { wv.evaluateJavascript(js, null) } },
        )
        wv.addJavascriptInterface(rpcBridge, "MahoBridgeAndroid")
        wv.addJavascriptInterface(
            NativeActionBridge(context) { js -> wv.post { wv.evaluateJavascript(js, null) } },
            "MahoBridgeNative",
        )

        val hash = if (params.isNotEmpty()) "#$screen?$params" else "#$screen"
        wv.loadUrl(buildUrl(context, hash))
        return AiWebViewHost(wv, rpcBridge)
    }

    private fun buildUrl(context: Context, hash: String): String {
        val isDev = context
            .getSharedPreferences("maho_dev", Context.MODE_PRIVATE)
            .getBoolean("web_ai_dev", false)
        return if (isDev) "$DEV_URL$hash" else "$ASSET_URL$hash"
    }
}
