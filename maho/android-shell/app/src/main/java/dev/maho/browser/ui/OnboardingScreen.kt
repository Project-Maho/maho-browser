package dev.maho.browser.ui

import android.app.Activity
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import dev.maho.browser.ui.theme.BrowserThemeController
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

    // The window theme leaves a dark status bar with light icons; onboarding is a
    // full-bleed web page, so match its light/dark background instead.
    val darkTheme = BrowserThemeController.isDarkTheme()
    val view = LocalView.current
    SideEffect {
        val window = (context as? Activity)?.window ?: return@SideEffect
        // web-ai --color-surface: #ffffff light, #1c1c1e dark.
        window.statusBarColor = (if (darkTheme) Color(0xFF1C1C1E) else Color.White).toArgb()
        WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = !darkTheme
    }

    DisposableEffect(host) {
        onDispose { host.destroy() }
    }

    AndroidView(
        factory = { host.webView },
        modifier = Modifier.fillMaxSize(),
    )
}
