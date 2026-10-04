package dev.maho.browser.ui

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.net.Uri
import android.net.http.SslError
import android.os.Message
import android.view.View
import android.webkit.GeolocationPermissions
import android.webkit.JsPromptResult
import android.webkit.JsResult
import android.webkit.PermissionRequest
import android.webkit.RenderProcessGoneDetail
import android.webkit.SslErrorHandler
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.EditText
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import dev.maho.browser.bridge.BridgeNavigation
import dev.maho.browser.models.TabId
import java.io.ByteArrayInputStream
import java.util.concurrent.ConcurrentHashMap

object OriginPermissionStore {
    private val permissions = ConcurrentHashMap<String, MutableMap<String, Boolean>>()

    fun getPermission(origin: String, resource: String): Boolean? {
        return permissions[origin]?.get(resource)
    }

    fun setPermission(origin: String, resource: String, granted: Boolean) {
        permissions.computeIfAbsent(origin) { ConcurrentHashMap() }[resource] = granted
    }

    fun clear() {
        permissions.clear()
    }
}

fun checkWebResourcesGranted(
    resources: Array<String>,
    permissionResults: Map<String, Boolean>
): Boolean {
    return resources.all { res ->
        val perm = when (res) {
            PermissionRequest.RESOURCE_VIDEO_CAPTURE -> Manifest.permission.CAMERA
            PermissionRequest.RESOURCE_AUDIO_CAPTURE -> Manifest.permission.RECORD_AUDIO
            else -> null
        }
        perm == null || permissionResults[perm] == true
    }
}

internal fun Context.findActivity(): Activity? {
    var cur: Context? = this
    while (cur is ContextWrapper) {
        if (cur is Activity) return cur
        cur = cur.baseContext
    }
    return null
}

internal fun handleExternalSchemeUrl(
    context: Context,
    url: String,
    loadFallback: (String) -> Unit = {}
): Boolean {
    val uri = try {
        Uri.parse(url)
    } catch (_: Exception) {
        return false
    }
    val scheme = uri.scheme?.lowercase() ?: return false
    if (scheme == "file" || scheme == "content" || scheme == "javascript" || scheme == "data" || scheme == "blob") {
        return false
    }
    if (scheme == "http" || scheme == "https" || scheme == "about") {
        return false
    }

    try {
        val intent = if (url.startsWith("intent://")) {
            Intent.parseUri(url, Intent.URI_INTENT_SCHEME).apply {
                addCategory(Intent.CATEGORY_BROWSABLE)
                component = null
            }
        } else {
            Intent(Intent.ACTION_VIEW, uri).apply {
                addCategory(Intent.CATEGORY_BROWSABLE)
            }
        }
        if (context !is Activity) {
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        val resolveInfo = context.packageManager?.resolveActivity(intent, 0)
        if (resolveInfo != null) {
            context.startActivity(intent)
            return true
        } else {
            if (url.startsWith("intent://")) {
                val parsed = Intent.parseUri(url, Intent.URI_INTENT_SCHEME)
                val fallbackUrl = parsed.getStringExtra("browser_fallback_url")
                if (!fallbackUrl.isNullOrBlank() && (fallbackUrl.startsWith("http://") || fallbackUrl.startsWith("https://"))) {
                    loadFallback(fallbackUrl)
                    return true
                }
            }
        }
    } catch (_: Exception) {
        if (url.startsWith("intent://")) {
            try {
                val parsed = Intent.parseUri(url, Intent.URI_INTENT_SCHEME)
                val fallbackUrl = parsed.getStringExtra("browser_fallback_url")
                if (!fallbackUrl.isNullOrBlank() && (fallbackUrl.startsWith("http://") || fallbackUrl.startsWith("https://"))) {
                    loadFallback(fallbackUrl)
                    return true
                }
            } catch (_: Exception) {}
        }
    }
    return false
}

internal class MainDocumentUrlState {
    @Volatile
    private var currentUrl: String = ""

    fun onPageStarted(url: String) {
        currentUrl = url
    }

    fun onPageFinished(url: String) {
        currentUrl = url
    }

    fun sourceFor(
        requestUrl: String,
        isForMainFrame: Boolean,
        requestHeaders: Map<String, String>,
    ): String {
        if (isForMainFrame) {
            return requestUrl
        }
        return currentUrl.ifBlank {
            requestHeaders.entries
                .firstOrNull { (name, _) -> name.equals("Referer", ignoreCase = true) }
                ?.value
                .orEmpty()
        }
    }
}

internal fun shouldBlockWebRequest(
    requestUrl: String,
    isForMainFrame: Boolean,
    requestHeaders: Map<String, String>,
    sourceState: MainDocumentUrlState,
    isSourceWhitelisted: (String) -> Boolean,
    queryFilterDecision: (url: String, sourceUrl: String, requestType: String) -> Boolean,
): Boolean {
    val sourceUrl = sourceState.sourceFor(requestUrl, isForMainFrame, requestHeaders)
    val sourceHost = try {
        java.net.URI(sourceUrl).host.orEmpty()
    } catch (_: Exception) {
        ""
    }
    if (sourceHost.isNotEmpty() && isSourceWhitelisted(sourceHost)) {
        return false
    }

    val requestType = if (isForMainFrame) "document" else "other"
    return queryFilterDecision(requestUrl, sourceUrl, requestType)
}

class MahoWebViewClient(
    private val tabIdProvider: () -> TabId,
    private val onUrlChanged: (String) -> Unit,
    private val onLoadingChanged: (Boolean) -> Unit,
    private val onNavigationStateChanged: (canGoBack: Boolean, canGoForward: Boolean) -> Unit,
    private val onError: (errorCode: Int, description: String, failingUrl: String) -> Unit,
    private val isSourceWhitelisted: (String) -> Boolean,
    private val onPageThemeApply: (WebView) -> Unit = {},
    private val onRenderProcessCrashed: (() -> Unit)? = null,
    private val isIncognito: () -> Boolean = { false },
) : WebViewClient() {
    private val mainDocumentUrlState = MainDocumentUrlState()

    private fun capturePreviewWhenPainted(view: WebView, tabId: String) {
        // A private page must never be read back into a preview bitmap.
        if (isIncognito()) return
        // The preview must come from a painted frame, so wait for the state the page
        // will actually draw instead of capturing as soon as loading finishes.
        view.postVisualStateCallback(
            tabId.hashCode().toLong(),
            object : WebView.VisualStateCallback() {
                override fun onComplete(requestId: Long) {
                    captureTabPreview(view, tabId)
                }
            },
        )
    }

    override fun onPageStarted(view: WebView, url: String, favicon: Bitmap?) {
        mainDocumentUrlState.onPageStarted(url)
        onUrlChanged(url)
        onLoadingChanged(true)
        val tabId = tabIdProvider()
        BridgeNavigation.reportUrlUpdated(tabId, url)
        BridgeNavigation.reportLoadingChanged(tabId, true)
    }

    override fun onPageFinished(view: WebView, url: String) {
        mainDocumentUrlState.onPageFinished(url)
        onPageThemeApply(view)
        onLoadingChanged(false)
        onNavigationStateChanged(view.canGoBack(), view.canGoForward())
        val tabId = tabIdProvider()
        BridgeNavigation.reportLoadingChanged(tabId, false)
        BridgeNavigation.reportNavigationStateChanged(tabId, view.canGoBack(), view.canGoForward())
        capturePreviewWhenPainted(view, tabId)
    }

    override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
        val url = request.url.toString()
        if (handleExternalSchemeUrl(view.context, url) { fallbackUrl ->
            view.loadUrl(fallbackUrl)
        }) {
            return true
        }
        val scheme = request.url.scheme?.lowercase()
        if (scheme != null && scheme != "http" && scheme != "https" && scheme != "about") {
            onError(-1, "No application available to handle this link", url)
            return true
        }
        return false
    }

    override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
        val settings = dev.maho.browser.bridge.BridgeSettings.getSettingsTyped()
        val contentBlockerEnabled = settings?.privacy?.isNativeBlockingEnabled ?: false
        if (!contentBlockerEnabled) {
            return super.shouldInterceptRequest(view, request)
        }

        val url = request.url.toString()
        val isMainFrame = request.isForMainFrame
        val isBlocked = shouldBlockWebRequest(
            requestUrl = url,
            isForMainFrame = isMainFrame,
            requestHeaders = request.requestHeaders,
            sourceState = mainDocumentUrlState,
            isSourceWhitelisted = isSourceWhitelisted,
            queryFilterDecision = dev.maho.browser.MahoBridge::queryFilterDecision,
        )
        if (isBlocked) {
            return WebResourceResponse(
                "text/plain",
                "UTF-8",
                204,
                "No Content",
                mapOf("Access-Control-Allow-Origin" to "*"),
                ByteArrayInputStream(ByteArray(0))
            )
        }
        return super.shouldInterceptRequest(view, request)
    }

    override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
        if (request.isForMainFrame) {
            onError(error.errorCode, error.description.toString(), request.url.toString())
        }
    }

    override fun onRenderProcessGone(view: WebView, detail: RenderProcessGoneDetail): Boolean {
        val didCrash = detail.didCrash()
        val tabId = tabIdProvider()
        BridgeNavigation.reportLoadingChanged(tabId, false)
        onRenderProcessCrashed?.invoke()
        onError(-1, if (didCrash) "Web process crashed unexpectedly" else "Web process terminated by system", view.url.orEmpty())
        return true
    }

    @Suppress("OVERRIDE_DEPRECATION")
    override fun onReceivedSslError(view: WebView, handler: SslErrorHandler, error: SslError) {
        handler.cancel()
        val failingUrl = error.url.orEmpty().ifEmpty { view.url.orEmpty() }
        val primaryError = error.primaryError
        val errorDesc = when (primaryError) {
            SslError.SSL_UNTRUSTED -> "The certificate authority is not trusted."
            SslError.SSL_EXPIRED -> "The security certificate has expired."
            SslError.SSL_IDMISMATCH -> "The security certificate hostname does not match the website."
            SslError.SSL_NOTYETVALID -> "The security certificate is not yet valid."
            SslError.SSL_DATE_INVALID -> "The security certificate date is invalid."
            else -> "The security certificate for this website is invalid."
        }
        onError(-1, "SSL certificate error: $errorDesc", failingUrl)
    }
}

class MahoWebChromeClient(
    private val tabIdProvider: () -> TabId,
    private val onProgressChanged: (Int) -> Unit,
    private val onTitleChanged: (String) -> Unit,
    private val onFileChooser: ((filePathCallback: ValueCallback<Array<Uri>>?, fileChooserParams: FileChooserParams?) -> Boolean)? = null,
    private val onCustomViewShow: ((view: View, callback: CustomViewCallback) -> Unit)? = null,
    private val onCustomViewHide: (() -> Unit)? = null,
    private val contextProvider: (() -> Context?)? = null,
    private val onNewTabRequested: ((String) -> Unit)? = null,
    private val onPermissionRequestDelegate: ((request: PermissionRequest, neededOsPerms: List<String>) -> Unit)? = null,
    private val onGeolocationRequestDelegate: ((origin: String, callback: GeolocationPermissions.Callback, needsOsPerms: Boolean) -> Unit)? = null,
) : WebChromeClient() {

    override fun onProgressChanged(view: WebView, newProgress: Int) {
        onProgressChanged(newProgress)
    }

    override fun onReceivedTitle(view: WebView, title: String?) {
        val resolvedTitle = title ?: return
        onTitleChanged(resolvedTitle)
        BridgeNavigation.reportTitleUpdated(tabIdProvider(), resolvedTitle)
    }

    override fun onShowCustomView(view: View, callback: CustomViewCallback) {
        if (onCustomViewShow != null) {
            onCustomViewShow.invoke(view, callback)
        } else {
            super.onShowCustomView(view, callback)
        }
    }

    override fun onHideCustomView() {
        if (onCustomViewHide != null) {
            onCustomViewHide.invoke()
        } else {
            super.onHideCustomView()
        }
    }

    override fun onPermissionRequest(request: PermissionRequest) {
        val ctx = contextProvider?.invoke()
        if (ctx == null) {
            request.deny()
            return
        }

        val originHost = try { Uri.parse(request.origin.toString()).host ?: request.origin.toString() } catch (_: Exception) { "This website" }
        val resources = request.resources

        val allPreviouslyGranted = resources.all { OriginPermissionStore.getPermission(originHost, it) == true }
        val anyPreviouslyDenied = resources.any { OriginPermissionStore.getPermission(originHost, it) == false }

        if (anyPreviouslyDenied) {
            request.deny()
            return
        }

        val neededOsPerms = mutableListOf<String>()
        if (resources.contains(PermissionRequest.RESOURCE_VIDEO_CAPTURE) &&
            ContextCompat.checkSelfPermission(ctx, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            neededOsPerms.add(Manifest.permission.CAMERA)
        }
        if (resources.contains(PermissionRequest.RESOURCE_AUDIO_CAPTURE) &&
            ContextCompat.checkSelfPermission(ctx, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            neededOsPerms.add(Manifest.permission.RECORD_AUDIO)
        }

        if (allPreviouslyGranted && neededOsPerms.isEmpty()) {
            request.grant(resources)
            return
        }

        if (onPermissionRequestDelegate != null) {
            onPermissionRequestDelegate.invoke(request, neededOsPerms)
            return
        }

        val resNames = resources.map { res ->
            when (res) {
                PermissionRequest.RESOURCE_VIDEO_CAPTURE -> "Camera"
                PermissionRequest.RESOURCE_AUDIO_CAPTURE -> "Microphone"
                PermissionRequest.RESOURCE_PROTECTED_MEDIA_ID -> "Protected Media"
                else -> "Media"
            }
        }.distinct().joinToString(" and ")

        AlertDialog.Builder(ctx)
            .setTitle("Permission Request")
            .setMessage("$originHost wants to access your $resNames.")
            .setPositiveButton("Allow") { _, _ ->
                if (neededOsPerms.isNotEmpty()) {
                    ctx.findActivity()?.let { activity ->
                        ActivityCompat.requestPermissions(activity, neededOsPerms.toTypedArray(), 1002)
                    }
                }
                resources.forEach { OriginPermissionStore.setPermission(originHost, it, true) }
                request.grant(request.resources)
            }
            .setNegativeButton("Block") { _, _ ->
                resources.forEach { OriginPermissionStore.setPermission(originHost, it, false) }
                request.deny()
            }
            .setOnCancelListener {
                resources.forEach { OriginPermissionStore.setPermission(originHost, it, false) }
                request.deny()
            }
            .show()
    }

    override fun onGeolocationPermissionsShowPrompt(origin: String, callback: GeolocationPermissions.Callback) {
        val ctx = contextProvider?.invoke()
        if (ctx == null) {
            callback.invoke(origin, false, false)
            return
        }

        val host = try { Uri.parse(origin).host ?: origin } catch (_: Exception) { origin }
        val cached = OriginPermissionStore.getPermission(host, "geolocation")
        if (cached == false) {
            callback.invoke(origin, false, false)
            return
        }

        val needsLocationPerm = ContextCompat.checkSelfPermission(ctx, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED &&
            ContextCompat.checkSelfPermission(ctx, Manifest.permission.ACCESS_COARSE_LOCATION) != PackageManager.PERMISSION_GRANTED

        if (cached == true && !needsLocationPerm) {
            callback.invoke(origin, true, true)
            return
        }

        if (onGeolocationRequestDelegate != null) {
            onGeolocationRequestDelegate.invoke(origin, callback, needsLocationPerm)
            return
        }

        AlertDialog.Builder(ctx)
            .setTitle("Location Request")
            .setMessage("$host wants to access your location.")
            .setPositiveButton("Allow") { _, _ ->
                if (needsLocationPerm) {
                    ctx.findActivity()?.let { activity ->
                        ActivityCompat.requestPermissions(
                            activity,
                            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION),
                            1003
                        )
                    }
                }
                OriginPermissionStore.setPermission(host, "geolocation", true)
                callback.invoke(origin, true, true)
            }
            .setNegativeButton("Block") { _, _ ->
                OriginPermissionStore.setPermission(host, "geolocation", false)
                callback.invoke(origin, false, false)
            }
            .setOnCancelListener {
                OriginPermissionStore.setPermission(host, "geolocation", false)
                callback.invoke(origin, false, false)
            }
            .show()
    }

    override fun onJsAlert(view: WebView, url: String, message: String, result: JsResult): Boolean {
        val context = view.context
        AlertDialog.Builder(context)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok) { _, _ -> result.confirm() }
            .setOnCancelListener { result.cancel() }
            .show()
        return true
    }

    override fun onJsConfirm(view: WebView, url: String, message: String, result: JsResult): Boolean {
        val context = view.context
        AlertDialog.Builder(context)
            .setMessage(message)
            .setPositiveButton(android.R.string.ok) { _, _ -> result.confirm() }
            .setNegativeButton(android.R.string.cancel) { _, _ -> result.cancel() }
            .setOnCancelListener { result.cancel() }
            .show()
        return true
    }

    override fun onJsPrompt(view: WebView, url: String, message: String, defaultValue: String?, result: JsPromptResult): Boolean {
        val context = view.context
        val input = EditText(context).apply {
            setText(defaultValue.orEmpty())
        }
        AlertDialog.Builder(context)
            .setMessage(message)
            .setView(input)
            .setPositiveButton(android.R.string.ok) { _, _ -> result.confirm(input.text.toString()) }
            .setNegativeButton(android.R.string.cancel) { _, _ -> result.cancel() }
            .setOnCancelListener { result.cancel() }
            .show()
        return true
    }

    override fun onShowFileChooser(
        webView: WebView?,
        filePathCallback: ValueCallback<Array<Uri>>?,
        fileChooserParams: FileChooserParams?
    ): Boolean {
        if (onFileChooser != null) {
            return onFileChooser.invoke(filePathCallback, fileChooserParams)
        }
        filePathCallback?.onReceiveValue(null)
        return false
    }

    override fun onCreateWindow(view: WebView, isDialog: Boolean, isUserGesture: Boolean, resultMsg: Message?): Boolean {
        if (!isUserGesture) return false
        val transport = resultMsg?.obj as? WebView.WebViewTransport ?: return false
        val tempWebView = WebView(view.context)
        tempWebView.webViewClient = object : WebViewClient() {
            override fun shouldOverrideUrlLoading(v: WebView, request: WebResourceRequest): Boolean {
                val url = request.url.toString()
                v.post { v.destroy() }
                if (onNewTabRequested != null) {
                    onNewTabRequested.invoke(url)
                    return true
                }
                val client = view.webViewClient
                if (client.shouldOverrideUrlLoading(view, request)) {
                    return true
                }
                view.loadUrl(url)
                return true
            }
        }
        tempWebView.postDelayed({
            try {
                tempWebView.destroy()
            } catch (_: Exception) {}
        }, 10000)
        transport.webView = tempWebView
        resultMsg.sendToTarget()
        return true
    }
}
