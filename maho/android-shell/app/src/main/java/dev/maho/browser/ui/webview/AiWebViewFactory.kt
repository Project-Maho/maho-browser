package dev.maho.browser.ui.webview

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.util.Log
import android.view.ViewGroup
import android.webkit.ConsoleMessage
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import androidx.webkit.WebViewAssetLoader
import androidx.webkit.WebViewClientCompat
import dev.maho.browser.BuildConfig
import dev.maho.browser.bridge.BridgeSettings
import dev.maho.browser.models.AppearanceSettingsUpdate
import dev.maho.browser.models.Theme
import dev.maho.browser.sync.CredentialManagerGoogleSource
import dev.maho.browser.ui.theme.BrowserThemeController
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Handle to a configured web-ai [WebView] plus its RPC bridge, so hosts (the
 * [AIWebViewFragment] and the Compose [WebAgentOverlay]) share one setup path.
 */
class AiWebViewHost internal constructor(
    val webView: WebView,
    private val rpcBridge: WebViewBridge,
) {
    private val destroyed = AtomicBoolean(false)

    /**
     * U01: idempotent host disposal. Closes the bridge's admission gates
     * immediately (native sessions drain and free on the bridge's background
     * teardown scope, so Main never waits for a parked borrow or create) and
     * destroys the WebView. Safe to call repeatedly.
     */
    fun destroy() {
        if (!destroyed.compareAndSet(false, true)) return
        BrowserThemeController.unregisterWebView(webView)
        rpcBridge.destroy()
        webView.destroy()
    }
}

/**
 * AiWebViewFactory — builds a WebView wired to [MahoNativeBridge] via
 * [WebViewBridge] (as `MahoBridgeAndroid`) and [NativeActionBridge] (as
 * `MahoBridgeNative`), then loads `web-ai/index.html#<screen>[?params]`.
 *
 * Assets are served over https via [WebViewAssetLoader] instead of `file://`.
 * A `file://` document cannot load its sibling `./ai-bundle.js` on the System
 * WebView (subresource access from the null origin is blocked), which leaves the
 * app blank; the virtual https origin removes that restriction.
 */
object AiWebViewFactory {
    private const val ASSET_HOST = "appassets.androidplatform.net"
    private const val APP_URL = "https://$ASSET_HOST/assets/web-ai/index.html"
    private const val DEV_URL = "http://10.0.2.2:5173/"

    @SuppressLint("SetJavaScriptEnabled")
    fun createHost(
        context: Context,
        screen: String,
        params: String = "",
        onNavBack: (() -> Unit)? = null,
        onCompleteOnboarding: (() -> Unit)? = null,
    ): AiWebViewHost {
        val assetLoader = WebViewAssetLoader.Builder()
            .setDomain(ASSET_HOST)
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(context))
            .build()

        val wv = WebView(context).apply {
            if (BuildConfig.DEBUG) {
                WebView.setWebContentsDebuggingEnabled(true)
            }
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            )
            settings.apply {
                javaScriptEnabled = true
                domStorageEnabled = true
                allowFileAccess = false
                mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
                cacheMode = WebSettings.LOAD_DEFAULT
            }
            webViewClient = object : WebViewClientCompat() {
                override fun shouldInterceptRequest(
                    view: WebView,
                    request: WebResourceRequest,
                ): WebResourceResponse? = assetLoader.shouldInterceptRequest(request.url)

                override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                    if (request.url.scheme == "maho-theme") {
                        val theme = when (request.url.host?.lowercase()) {
                            "dark" -> Theme.Dark
                            "light" -> Theme.Light
                            "system" -> Theme.System
                            else -> return true
                        }
                        BridgeSettings.updateAppearanceSettings(AppearanceSettingsUpdate(theme = theme))
                        BrowserThemeController.setTheme(theme)
                        return true
                    }
                    return false
                }

                override fun onPageFinished(view: WebView, url: String?) {
                    super.onPageFinished(view, url)
                    BrowserThemeController.applyToWebView(view)
                }

                @Deprecated("Deprecated in Java")
                override fun onReceivedError(
                    view: WebView,
                    errorCode: Int,
                    description: String?,
                    failingUrl: String?,
                ) {
                    Log.e("MahoWebAI", "load error $errorCode $description $failingUrl")
                }
            }
            webChromeClient = object : WebChromeClient() {
                override fun onConsoleMessage(message: ConsoleMessage): Boolean {
                    if (message.messageLevel() == ConsoleMessage.MessageLevel.ERROR) {
                        Log.e(
                            "MahoWebAI",
                            "${message.message()} @${message.sourceId()}:${message.lineNumber()}",
                        )
                    }
                    return true
                }
            }
        }

        val rpcBridge = WebViewBridge(
            native = MahoNativeBridge,
            evaluateJs = { js -> wv.post { wv.evaluateJavascript(js, null) } },
            onNavBack = onNavBack,
            onCompleteOnboarding = onCompleteOnboarding,
            googleCredentialSource = (context as? Activity)?.let(::CredentialManagerGoogleSource),
        )
        wv.addJavascriptInterface(rpcBridge, "MahoBridgeAndroid")
        wv.addJavascriptInterface(
            NativeActionBridge(context) { js -> wv.post { wv.evaluateJavascript(js, null) } },
            "MahoBridgeNative",
        )

        BrowserThemeController.registerWebView(wv)
        val hash = if (params.isNotEmpty()) "#$screen?$params" else "#$screen"
        wv.loadUrl(buildUrl(context, hash))
        return AiWebViewHost(wv, rpcBridge)
    }

    private fun buildUrl(context: Context, hash: String): String {
        val isDev = context
            .getSharedPreferences("maho_dev", Context.MODE_PRIVATE)
            .getBoolean("web_ai_dev", false)
        return if (isDev) "$DEV_URL$hash" else "$APP_URL$hash"
    }
}
