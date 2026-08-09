package dev.maho.browser.ui.webview

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView

/** Hosts the shared web-ai `#byok` settings screen. */
@Composable
fun BYOKWebOverlay(onExit: () -> Unit) {
    val hostState = remember { mutableStateOf<AiWebViewHost?>(null) }

    BackHandler(enabled = true) {
        onExit()
    }

    AndroidView(
        modifier = Modifier.fillMaxSize(),
        factory = { context ->
            AiWebViewFactory.createHost(context, "byok", onNavBack = onExit)
                .also { hostState.value = it }
                .webView
        },
    )

    DisposableEffect(Unit) {
        onDispose {
            hostState.value?.destroy()
            hostState.value = null
        }
    }
}
