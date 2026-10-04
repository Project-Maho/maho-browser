package dev.maho.browser.ui

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import dev.maho.browser.ui.webview.AiWebViewFactory

/**
 * Android onboarding intentionally hosts the same shared web-ai flow as iOS.
 * This keeps account auth, theme selection, default-browser setup, and finish
 * behavior identical across the two mobile shells.
 */
@Composable
fun OnboardingScreen(
    onComplete: () -> Unit,
) {
    val context = LocalContext.current
    val currentOnComplete = rememberUpdatedState(onComplete)
    val host = remember(context) {
        AiWebViewFactory.createHost(
            context = context,
            screen = "onboarding",
            onCompleteOnboarding = { currentOnComplete.value() },
        )
    }

    DisposableEffect(host) {
        onDispose { host.destroy() }
    }

    AndroidView(
        factory = { host.webView },
        modifier = Modifier.fillMaxSize(),
    )
}
