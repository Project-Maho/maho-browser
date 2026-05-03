package dev.maho.browser.ui

import android.graphics.Bitmap
import android.net.http.SslError
import android.webkit.SslErrorHandler
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import dev.maho.browser.bridge.BridgeNavigation
import dev.maho.browser.models.TabId

class MahoWebViewClient(
    private val tabIdProvider: () -> TabId,
    private val onUrlChanged: (String) -> Unit,
    private val onLoadingChanged: (Boolean) -> Unit,
    private val onNavigationStateChanged: (canGoBack: Boolean, canGoForward: Boolean) -> Unit,
    private val onError: (errorCode: Int, description: String, failingUrl: String) -> Unit,
) : WebViewClient() {

    override fun onPageStarted(view: WebView, url: String, favicon: Bitmap?) {
        onUrlChanged(url)
        onLoadingChanged(true)
        val tabId = tabIdProvider()
        BridgeNavigation.reportUrlUpdated(tabId, url)
        BridgeNavigation.reportLoadingChanged(tabId, true)
    }

    override fun onPageFinished(view: WebView, url: String) {
        onLoadingChanged(false)
        onNavigationStateChanged(view.canGoBack(), view.canGoForward())
        val tabId = tabIdProvider()
        BridgeNavigation.reportLoadingChanged(tabId, false)
        BridgeNavigation.reportNavigationStateChanged(tabId, view.canGoBack(), view.canGoForward())
    }

    override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
        val url = request.url.toString()
        if (url.startsWith("http://") || url.startsWith("https://")) {
            return false
        }
        return true
    }

    override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
        if (request.isForMainFrame) {
            onError(error.errorCode, error.description.toString(), request.url.toString())
        }
    }

    @Suppress("OVERRIDE_DEPRECATION")
    override fun onReceivedSslError(view: WebView, handler: SslErrorHandler, error: SslError) {
        handler.cancel()
    }
}

class MahoWebChromeClient(
    private val tabIdProvider: () -> TabId,
    private val onProgressChanged: (Int) -> Unit,
    private val onTitleChanged: (String) -> Unit,
) : WebChromeClient() {

    override fun onProgressChanged(view: WebView, newProgress: Int) {
        onProgressChanged(newProgress)
    }

    override fun onReceivedTitle(view: WebView, title: String?) {
        val resolvedTitle = title ?: return
        onTitleChanged(resolvedTitle)
        BridgeNavigation.reportTitleUpdated(tabIdProvider(), resolvedTitle)
    }
}
