@file:SuppressLint("SetJavaScriptEnabled")

package dev.maho.browser.ui

import android.annotation.SuppressLint
import android.view.GestureDetector
import android.view.MotionEvent
import android.webkit.CookieManager
import android.webkit.WebSettings
import android.webkit.WebStorage
import android.webkit.WebView
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.compose.ui.platform.LocalLifecycleOwner
import dev.maho.browser.models.TabId
import dev.maho.browser.ui.theme.BrowserThemeController
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
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val applicationContext = context.applicationContext
    val lifecycleOwner = LocalLifecycleOwner.current
    val currentTabId = rememberUpdatedState(tabId)
    val currentIsIncognito = rememberUpdatedState(isIncognito)
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

    val webView = remember {
        WebView(context).apply {
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.databaseEnabled = true
            settings.setSupportMultipleWindows(false)
            settings.javaScriptCanOpenWindowsAutomatically = false
            settings.mediaPlaybackRequiresUserGesture = true
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
            )
            webChromeClient = MahoWebChromeClient(
                tabIdProvider = { currentTabId.value },
                onProgressChanged = onProgressChanged,
                onTitleChanged = onTitleChanged,
            )

            setOnScrollChangeListener { _, _, scrollY, _, oldScrollY ->
                onScrollChanged(scrollY, oldScrollY)
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

    LaunchedEffect(isDesktopMode) {
        webView.settings.userAgentString = if (isDesktopMode) {
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/123.0.0.0 Safari/537.36"
        } else {
            null
        }
        webView.settings.useWideViewPort = isDesktopMode
        webView.settings.loadWithOverviewMode = true
    }

    LaunchedEffect(tabId, initialUrl) {
        if (initialUrl.isNotBlank() && webView.url != initialUrl) {
            webView.loadUrl(initialUrl)
        }
    }

    AndroidView(
        factory = { webView },
        modifier = modifier,
    )
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
