@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui

import android.content.Intent
import android.webkit.WebView
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.material3.BottomAppBar
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Scaffold
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import dev.maho.browser.bridge.BridgeNavigation
import dev.maho.browser.models.TabId
import dev.maho.browser.ui.chrome.NavigationButtons
import dev.maho.browser.ui.chrome.RefreshControl
import dev.maho.browser.ui.chrome.UrlBar

@Composable
fun BrowserScreen(
    tabId: TabId,
    initialUrl: String,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    var currentUrl by remember { mutableStateOf(initialUrl) }
    var currentTitle by remember { mutableStateOf("") }
    var isLoading by remember { mutableStateOf(false) }
    var loadProgress by remember { mutableIntStateOf(0) }
    var canGoBack by remember { mutableStateOf(false) }
    var canGoForward by remember { mutableStateOf(false) }
    var webViewRef by remember { mutableStateOf<WebView?>(null) }

    val isSecure = currentUrl.startsWith("https://")

    Scaffold(
        modifier = modifier.fillMaxSize(),
        contentWindowInsets = WindowInsets.safeDrawing,
        topBar = {
            TopAppBar(
                title = {
                    UrlBar(
                        currentUrl = currentUrl,
                        currentTitle = currentTitle,
                        isLoading = isLoading,
                        loadProgress = loadProgress,
                        isSecure = isSecure,
                        onNavigate = { url ->
                            BridgeNavigation.navigate(tabId, url)
                            webViewRef?.loadUrl(url)
                        },
                        onShare = {
                            val sendIntent = Intent(Intent.ACTION_SEND).apply {
                                type = "text/plain"
                                putExtra(Intent.EXTRA_TEXT, currentUrl)
                                putExtra(Intent.EXTRA_TITLE, currentTitle)
                            }
                            context.startActivity(Intent.createChooser(sendIntent, null))
                        },
                        modifier = Modifier.fillMaxWidth(),
                    )
                },
            )
        },
        bottomBar = {
            BottomAppBar {
                NavigationButtons(
                    canGoBack = canGoBack,
                    canGoForward = canGoForward,
                    isLoading = isLoading,
                    onBack = {
                        BridgeNavigation.goBack(tabId)
                        webViewRef?.goBack()
                    },
                    onForward = {
                        BridgeNavigation.goForward(tabId)
                        webViewRef?.goForward()
                    },
                    onReload = {
                        BridgeNavigation.reload(tabId)
                        webViewRef?.reload()
                    },
                    onStop = {
                        BridgeNavigation.stop(tabId)
                        webViewRef?.stopLoading()
                    },
                )
            }
        },
    ) { contentPadding ->
        RefreshControl(
            isLoading = isLoading,
            onRefresh = {
                BridgeNavigation.reload(tabId)
                webViewRef?.reload()
            },
            modifier = Modifier
                .fillMaxSize()
                .then(Modifier.padding(contentPadding)),
        ) {
            WebViewHost(
                tabId = tabId,
                initialUrl = initialUrl,
                isDesktopMode = false,
                onUrlChanged = { url -> currentUrl = url },
                onTitleChanged = { title -> currentTitle = title },
                onLoadingChanged = { loading -> isLoading = loading },
                onProgressChanged = { progress -> loadProgress = progress },
                onNavigationStateChanged = { back, forward ->
                    canGoBack = back
                    canGoForward = forward
                },
                onError = { _, _, _ -> },
                onWebViewReady = { wv -> webViewRef = wv },
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}
