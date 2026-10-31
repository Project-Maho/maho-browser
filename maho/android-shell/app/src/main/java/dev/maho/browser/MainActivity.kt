package dev.maho.browser

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTagsAsResourceId
import dev.maho.browser.bridge.BridgeSettings
import dev.maho.browser.ui.MainBrowserScreen
import dev.maho.browser.ui.OnboardingScreen
import dev.maho.browser.ui.theme.BrowserThemeController
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme
import dev.maho.browser.ui.theme.brandColorScheme

internal inline fun runMainActivityForegroundResume(
    sweepConversations: () -> Int,
    checkForUpdate: () -> Unit,
) {
    sweepConversations()
    checkForUpdate()
}

@OptIn(ExperimentalComposeUiApi::class)
class MainActivity : ComponentActivity() {

    private var pendingUrl by mutableStateOf<String?>(null)
    private lateinit var appUpdateManager: MahoAppUpdateManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = getSharedPreferences("maho_browser_ui_prefs", MODE_PRIVATE)
        val onboardingCompleted = prefs.getBoolean("onboardingCompleted", false)

        pendingUrl = DeepLinkHandler.parse(intent)

        appUpdateManager = MahoAppUpdateManager(this)
        appUpdateManager.register()

        BridgeSettings.getSettingsTyped()?.appearance?.theme?.let(BrowserThemeController::setTheme)

        // The POST_NOTIFICATIONS request that used to live here was removed: this shell has
        // no notification implementation at all (no NotificationManager, NotificationCompat,
        // or channel anywhere in the module), so the permission had no caller. Requesting it
        // from onCreate also pushed the activity out of RESUMED behind the system grant
        // dialog, which is what broke launch instrumentation.
        //
        // Downloads go through the system DownloadManager, which posts its completion
        // notification from its own process, not this app's -- so it needs no permission
        // here either. Do not reintroduce a request without an actual notification caller.

        setContent {
            val darkTheme = BrowserThemeController.isDarkTheme()
            MaterialTheme(colorScheme = brandColorScheme(darkTheme)) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .semantics {
                            testTagsAsResourceId = true
                        }
                        .testTag("androidShellRoot"),
                ) {
                    ProvideBrowserShellTheme(darkTheme = darkTheme) {
                        var showOnboarding by remember { mutableStateOf(!onboardingCompleted) }

                        if (showOnboarding) {
                            OnboardingScreen(
                                onComplete = { showOnboarding = false },
                            )
                        } else {
                            MainBrowserScreen(externalUrl = pendingUrl)
                        }
                    }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        intent ?: return
        pendingUrl = DeepLinkHandler.parse(intent)
    }

    override fun onResume() {
        super.onResume()
        runMainActivityForegroundResume(
            sweepConversations = { MahoBridge.autoArchiveConversations() },
            checkForUpdate = { appUpdateManager.checkForUpdate() },
        )
    }

    override fun onPause() {
        super.onPause()
        MahoBridge.saveState()
    }

    override fun onDestroy() {
        appUpdateManager.unregister()
        super.onDestroy()
    }
}
