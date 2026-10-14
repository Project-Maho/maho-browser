package dev.maho.browser.ui.webview

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import java.net.URLEncoder

/**
 * WebAgentOverlay — hosts the shared web-ai `#agent` screen in a WebView,
 * behind [dev.maho.browser.support.FeatureFlags.agentWebEnabled].
 *
 * The goal seed is carried through as `#agent?goal=<url-encoded>` — the Preact
 * agent screen reads `route.params.goal` and auto-submits it once.
 *
 * Back handling: the OS back button closes the overlay via [onExit] (matching
 * the native AgentScreen fallback) while also signalling the web bundle. This
 * guarantees back always exits — the bundle owns no reliable native-close path.
 */
@Composable
fun WebAgentOverlay(initialGoal: String?, onExit: () -> Unit) {
    val params = remember(initialGoal) {
        "goal=" + URLEncoder.encode(initialGoal.orEmpty(), "UTF-8")
    }
    // D12: the host is lifecycle bookkeeping, not UI state. A plain holder keeps the write in the
    // AndroidView factory and the dispose callback (both Main) out of the Compose snapshot system.
    val hostHolder = remember { arrayOfNulls<AiWebViewHost>(1) }

    BackHandler(enabled = true) {
        onExit()
    }

    AndroidView(
        modifier = Modifier.fillMaxSize(),
        factory = { ctx ->
            AiWebViewFactory.createHost(ctx, "agent", params, onNavBack = onExit)
                .also { hostHolder[0] = it }
                .webView
        },
    )

    DisposableEffect(Unit) {
        onDispose {
            hostHolder[0]?.destroy()
            hostHolder[0] = null
        }
    }
}
