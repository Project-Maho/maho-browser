@file:SuppressLint("SetJavaScriptEnabled")

package dev.maho.browser.ui

import android.annotation.SuppressLint
import android.Manifest
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import android.webkit.CookieManager
import android.webkit.GeolocationPermissions
import android.webkit.PermissionRequest
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebSettings
import android.webkit.WebStorage
import android.webkit.WebView
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import dev.maho.browser.models.TabId
import dev.maho.browser.ui.theme.BrowserThemeController
import dev.maho.browser.ui.tab.TabPreviewStore
import dev.maho.browser.ui.webview.AgenticBrowsingWebViewRegistry

@Composable
fun WebViewHost(
    tabId: TabId,
    initialUrl: String,
    isDesktopMode: Boolean,
    onUrlChanged: (String) -> Unit,
    onTitleChanged: (String) -> Unit,
    onLoadingChanged: (Boolean) -> Unit,
    onProgressChanged: (Int) -> Unit,
    onNavigationStateChanged: (canGoBack: Boolean, canGoForward: Boolean) -> Unit,
    onError: (errorCode: Int, description: String, failingUrl: String) -> Unit,
    onWebViewReady: (WebView) -> Unit,
    onScrollChanged: (scrollY: Int, oldScrollY: Int) -> Unit = { _, _ -> },
    onSingleTap: () -> Unit = {},
    onPinchSummarize: () -> Unit = {},
    isIncognito: Boolean = false,
    onNewTabRequested: ((String) -> Unit)? = null,
    capturePreviewSignal: Int = 0,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val applicationContext = context.applicationContext
    val lifecycleOwner = LocalLifecycleOwner.current
    val currentTabId = rememberUpdatedState(tabId)
    val currentIsIncognito = rememberUpdatedState(isIncognito)
    val currentOnNewTabRequested = rememberUpdatedState(onNewTabRequested)
    val currentOnProgressChanged = rememberUpdatedState(onProgressChanged)
    val currentOnTitleChanged = rememberUpdatedState(onTitleChanged)

    var webViewEpoch by remember { mutableIntStateOf(0) }
    var isRendererCrashed by remember { mutableStateOf(false) }
    var pendingFileChooserCallback by remember { mutableStateOf<ValueCallback<Array<Uri>>?>(null) }
    var customView by remember { mutableStateOf<View?>(null) }
    var customViewCallback by remember { mutableStateOf<WebChromeClient.CustomViewCallback?>(null) }

    var pendingMediaPermissionRequest by remember { mutableStateOf<PermissionRequest?>(null) }
    var pendingMediaHost by remember { mutableStateOf<String?>(null) }
    var pendingLocationRequest by remember { mutableStateOf<Pair<GeolocationPermissions.Callback, String>?>(null) }

    val mediaPermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        val req = pendingMediaPermissionRequest
        val host = pendingMediaHost
        if (req != null) {
            val allGranted = checkWebResourcesGranted(req.resources, results)
            if (allGranted) {
                if (host != null) {
                    req.resources.forEach { OriginPermissionStore.setPermission(host, it, true) }
                }
                req.grant(req.resources)
            } else {
                req.deny()
            }
            pendingMediaPermissionRequest = null
            pendingMediaHost = null
        }
    }

    val locationPermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        val pending = pendingLocationRequest
        if (pending != null) {
            val (callback, origin) = pending
            val fineGranted = results[Manifest.permission.ACCESS_FINE_LOCATION] == true
            val coarseGranted = results[Manifest.permission.ACCESS_COARSE_LOCATION] == true
            val host = try { Uri.parse(origin).host ?: origin } catch (_: Exception) { origin }
            if (fineGranted || coarseGranted) {
                OriginPermissionStore.setPermission(host, "geolocation", true)
                callback.invoke(origin, true, true)
            } else {
                callback.invoke(origin, false, false)
            }
            pendingLocationRequest = null
        }
    }

    val window = remember(context) { context.findActivity()?.window }
    DisposableEffect(customView, window) {
        if (customView != null && window != null) {
            val insetsController = WindowCompat.getInsetsController(window, window.decorView)
            insetsController.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            insetsController.hide(WindowInsetsCompat.Type.systemBars())
            onDispose {
                insetsController.show(WindowInsetsCompat.Type.systemBars())
            }
        } else {
            onDispose {}
        }
    }

    BackHandler(enabled = customView != null) {
        customViewCallback?.onCustomViewHidden()
        customView = null
        customViewCallback = null
    }

    val fileChooserLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { activityResult ->
        val uris = WebChromeClient.FileChooserParams.parseResult(activityResult.resultCode, activityResult.data)
        pendingFileChooserCallback?.onReceiveValue(uris)
        pendingFileChooserCallback = null
    }
    val gestureDetector = remember(context, onSingleTap) {
        GestureDetector(
            context,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onSingleTapConfirmed(event: MotionEvent): Boolean {
                    onSingleTap()
                    return false
                }
            },
        )
    }
    val scaleGestureDetector = remember(context, onPinchSummarize) {
        android.view.ScaleGestureDetector(
            context,
            object : android.view.ScaleGestureDetector.SimpleOnScaleGestureListener() {
                override fun onScaleEnd(detector: android.view.ScaleGestureDetector) {
                    if (detector.scaleFactor < 0.78f) {
                        onPinchSummarize()
                    }
                }
            }
        )
    }

    val webView = remember(webViewEpoch) {
        WebView(context).apply {
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.databaseEnabled = true
            settings.setSupportMultipleWindows(true)
            settings.javaScriptCanOpenWindowsAutomatically = true
            settings.mediaPlaybackRequiresUserGesture = true
            settings.setGeolocationEnabled(true)
            settings.setSupportZoom(true)
            settings.builtInZoomControls = true
            settings.displayZoomControls = false
            settings.loadWithOverviewMode = true
            settings.useWideViewPort = true
            settings.allowContentAccess = true
            settings.allowFileAccess = false
            settings.cacheMode = WebSettings.LOAD_DEFAULT
            overScrollMode = WebView.OVER_SCROLL_NEVER
            isVerticalScrollBarEnabled = false
            isHorizontalScrollBarEnabled = false

            webViewClient = MahoWebViewClient(
                tabIdProvider = { currentTabId.value },
                onUrlChanged = onUrlChanged,
                onLoadingChanged = onLoadingChanged,
                onNavigationStateChanged = onNavigationStateChanged,
                onError = onError,
                isSourceWhitelisted = { host ->
                    dev.maho.browser.ui.settings.WhitelistManager
                        .getWhitelist(applicationContext)
                        .contains(host)
                },
                onPageThemeApply = { view -> BrowserThemeController.applyToWebView(view) },
                onRenderProcessCrashed = {
                    isRendererCrashed = true
                },
                isIncognito = { currentIsIncognito.value },
            )
            webChromeClient = MahoWebChromeClient(
                tabIdProvider = { currentTabId.value },
                onProgressChanged = { currentOnProgressChanged.value(it) },
                onTitleChanged = { currentOnTitleChanged.value(it) },
                onFileChooser = { callback, params ->
                    pendingFileChooserCallback?.onReceiveValue(null)
                    pendingFileChooserCallback = callback
                    try {
                        val intent = params?.createIntent() ?: Intent(Intent.ACTION_GET_CONTENT).apply {
                            type = "*/*"
                            addCategory(Intent.CATEGORY_OPENABLE)
                        }
                        fileChooserLauncher.launch(intent)
                        true
                    } catch (_: Exception) {
                        pendingFileChooserCallback?.onReceiveValue(null)
                        pendingFileChooserCallback = null
                        false
                    }
                },
                onCustomViewShow = { v, callback ->
                    customViewCallback?.onCustomViewHidden()
                    customView = v
                    customViewCallback = callback
                },
                onCustomViewHide = {
                    customViewCallback?.onCustomViewHidden()
                    customView = null
                    customViewCallback = null
                },
                contextProvider = { context },
                onNewTabRequested = { url -> currentOnNewTabRequested.value?.invoke(url) },
                onPermissionRequestDelegate = { request, neededOsPerms ->
                    val originHost = try { Uri.parse(request.origin.toString()).host ?: request.origin.toString() } catch (_: Exception) { "This website" }
                    val resources = request.resources
                    val resNames = resources.map { res ->
                        when (res) {
                            PermissionRequest.RESOURCE_VIDEO_CAPTURE -> "Camera"
                            PermissionRequest.RESOURCE_AUDIO_CAPTURE -> "Microphone"
                            PermissionRequest.RESOURCE_PROTECTED_MEDIA_ID -> "Protected Media"
                            else -> "Media"
                        }
                    }.distinct().joinToString(" and ")

                    AlertDialog.Builder(context)
                        .setTitle("Permission Request")
                        .setMessage("$originHost wants to access your $resNames.")
                        .setPositiveButton("Allow") { _, _ ->
                            if (neededOsPerms.isNotEmpty()) {
                                pendingMediaPermissionRequest = request
                                pendingMediaHost = originHost
                                mediaPermissionLauncher.launch(neededOsPerms.toTypedArray())
                            } else {
                                resources.forEach { OriginPermissionStore.setPermission(originHost, it, true) }
                                request.grant(request.resources)
                            }
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
                },
                onGeolocationRequestDelegate = { origin, callback, needsOsPerms ->
                    val host = try { Uri.parse(origin).host ?: origin } catch (_: Exception) { origin }
                    AlertDialog.Builder(context)
                        .setTitle("Location Request")
                        .setMessage("$host wants to access your location.")
                        .setPositiveButton("Allow") { _, _ ->
                            if (needsOsPerms) {
                                pendingLocationRequest = callback to origin
                                locationPermissionLauncher.launch(
                                    arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
                                )
                            } else {
                                OriginPermissionStore.setPermission(host, "geolocation", true)
                                callback.invoke(origin, true, true)
                            }
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
                },
            )

            setOnScrollChangeListener { _, _, scrollY, _, oldScrollY ->
                onScrollChanged(scrollY, oldScrollY)
            }

            setDownloadListener { downloadUrl, userAgent, contentDisposition, mimetype, _ ->
                try {
                    val uri = android.net.Uri.parse(downloadUrl)
                    val filename = android.webkit.URLUtil.guessFileName(downloadUrl, contentDisposition, mimetype)
                    val cookie = CookieManager.getInstance().getCookie(downloadUrl)
                    val request = android.app.DownloadManager.Request(uri).apply {
                        setMimeType(mimetype)
                        addRequestHeader("User-Agent", userAgent)
                        if (!cookie.isNullOrBlank()) {
                            addRequestHeader("Cookie", cookie)
                        }
                        setDescription("Downloading $filename")
                        setTitle(filename)
                        setNotificationVisibility(android.app.DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                        setDestinationInExternalPublicDir(android.os.Environment.DIRECTORY_DOWNLOADS, filename)
                    }
                    val dm = context.getSystemService(Context.DOWNLOAD_SERVICE) as? android.app.DownloadManager
                    dm?.enqueue(request)
                    android.widget.Toast.makeText(context, "Downloading $filename...", android.widget.Toast.LENGTH_SHORT).show()
                } catch (_: Exception) {
                    try {
                        val intent = Intent(Intent.ACTION_VIEW, android.net.Uri.parse(downloadUrl)).apply {
                            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        }
                        context.startActivity(intent)
                    } catch (_: Exception) {}
                }
            }

            setOnTouchListener { _, event ->
                scaleGestureDetector.onTouchEvent(event)
                gestureDetector.onTouchEvent(event)
                false
            }

            BrowserThemeController.registerWebView(this)
            if (initialUrl.isNotBlank()) {
                loadUrl(initialUrl)
            }
        }
    }

    DisposableEffect(webView, gestureDetector, scaleGestureDetector) {
        webView.setOnTouchListener { _, event ->
            scaleGestureDetector.onTouchEvent(event)
            gestureDetector.onTouchEvent(event)
            false
        }
        onDispose {
            webView.setOnTouchListener(null)
        }
    }

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> webView.onResume()
                Lifecycle.Event.ON_PAUSE -> webView.onPause()
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            pendingMediaPermissionRequest?.deny()
            pendingMediaPermissionRequest = null
            pendingLocationRequest?.let { (cb, orig) -> cb.invoke(orig, false, false) }
            pendingLocationRequest = null
            webView.stopLoading()
            if (currentIsIncognito.value) {
                webView.clearCache(true)
                webView.clearHistory()
                webView.clearFormData()
                try {
                    WebStorage.getInstance().deleteAllData()
                    CookieManager.getInstance().removeAllCookies(null)
                    CookieManager.getInstance().flush()
                } catch (_: Exception) {}
            }
            BrowserThemeController.unregisterWebView(webView)
            webView.destroy()
        }
    }

    DisposableEffect(webView) {
        AgenticBrowsingWebViewRegistry.attach(webView)
        onWebViewReady(webView)
        onDispose {
            AgenticBrowsingWebViewRegistry.detach(webView)
        }
    }

    LaunchedEffect(webView, tabId, capturePreviewSignal) {
        if (capturePreviewSignal > 0) {
            captureTabPreview(webView, tabId)
        }
    }

    LaunchedEffect(isDesktopMode) {
        webView.settings.userAgentString = if (isDesktopMode) {
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36"
        } else {
            null
        }
        webView.settings.useWideViewPort = isDesktopMode
        webView.settings.loadWithOverviewMode = true
    }

    var lastRequestedUrl by remember(webView) { mutableStateOf<String?>(null) }
    LaunchedEffect(webView, tabId, initialUrl) {
        if (shouldLoadRequestedUrl(initialUrl, webView.url, lastRequestedUrl)) {
            lastRequestedUrl = initialUrl
            webView.loadUrl(initialUrl)
        }
    }

    Box(modifier = modifier) {
        // Keyed so a renderer-crash recovery swaps in the freshly created WebView;
        // AndroidView runs its factory once per key.
        key(webViewEpoch) {
            AndroidView(
                factory = { webView },
                modifier = Modifier.fillMaxSize(),
            )
        }

        val activeCustomView = customView
        if (activeCustomView != null) {
            AndroidView(
                factory = { activeCustomView },
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black),
            )
        }

        if (isRendererCrashed) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color(0xFFF8F9FA)),
                contentAlignment = Alignment.Center
            ) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    modifier = Modifier.padding(24.dp)
                ) {
                    Text(
                        "Aw, Snap!",
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        "Something went wrong while displaying this webpage.",
                        color = Color.Gray,
                        textAlign = TextAlign.Center
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    Button(
                        onClick = {
                            isRendererCrashed = false
                            webViewEpoch++
                        }
                    ) {
                        Text("Reload")
                    }
                }
            }
        }
    }
}

internal fun captureTabPreview(webView: WebView, tabId: TabId) {
    if (webView.width <= 0 || webView.height <= 0) return
    // Only read back a WebView the user is actually looking at, otherwise the
    // window already shows the tab deck or a sheet and we would store that.
    if (!webView.isShown) return
    val window = (webView.context as? android.app.Activity)?.window ?: return
    runCatching {
        val location = IntArray(2)
        webView.getLocationInWindow(location)
        // A card is 144dp tall, so a full-resolution ARGB_8888 frame would cost
        // ~9MB per tab. Read back at card scale instead.
        val scale = PREVIEW_MAX_WIDTH_PX.toFloat() / webView.width
        val bitmap = android.graphics.Bitmap.createBitmap(
            PREVIEW_MAX_WIDTH_PX,
            (webView.height * scale).toInt().coerceAtLeast(1),
            android.graphics.Bitmap.Config.RGB_565,
        )
        android.view.PixelCopy.request(
            window,
            android.graphics.Rect(
                location[0],
                location[1],
                location[0] + webView.width,
                location[1] + webView.height,
            ),
            bitmap,
            { status ->
                if (status == android.view.PixelCopy.SUCCESS && !bitmap.isBlank()) {
                    TabPreviewStore.put(tabId, bitmap)
                } else {
                    bitmap.recycle()
                }
            },
            android.os.Handler(android.os.Looper.getMainLooper()),
        )
    }
}

private const val PREVIEW_MAX_WIDTH_PX = 360

internal fun android.graphics.Bitmap.isBlank(): Boolean {
    // Sample a grid rather than one centre column: a real page is often uniform
    // straight down the middle, and treating that as blank threw the capture away.
    val first = getPixel(width / 8, height / 8)
    for (column in 1 until 8) {
        for (row in 1 until 8) {
            if (getPixel((width / 8) * column, (height / 8) * row) != first) return false
        }
    }
    return true
}

fun WebView.translatePage(targetLanguage: String = "en") {
    if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
        try {
            val method = WebView::class.java.getMethod("translate", String::class.java)
            method.invoke(this, targetLanguage)
        } catch (_: Exception) {
            injectGoogleTranslateWidget(targetLanguage)
        }
    } else {
        injectGoogleTranslateWidget(targetLanguage)
    }
}

private fun WebView.injectGoogleTranslateWidget(targetLanguage: String) {
    val script = """
        (function() {
            if (document.getElementById('maho-google-translate')) return;
            var div = document.createElement('div');
            div.id = 'maho-google-translate';
            div.innerHTML = "<div id='google_translate_element'></div>";
            document.body.insertBefore(div, document.body.firstChild);
            var script = document.createElement('script');
            script.type = 'text/javascript';
            script.src = 'https://translate.google.com/translate_a/element.js?cb=googleTranslateElementInit';
            document.body.appendChild(script);
            window.googleTranslateElementInit = function() {
                new google.translate.TranslateElement({pageLanguage: 'auto', includedLanguages: '$targetLanguage', layout: google.translate.TranslateElement.InlineLayout.SIMPLE}, 'google_translate_element');
            };
        })();
    """.trimIndent()
    evaluateJavascript(script, null)
}
