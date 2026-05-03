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
import dev.maho.browser.ui.MainBrowserScreen
import dev.maho.browser.ui.OnboardingScreen
import dev.maho.browser.ui.theme.ProvideBrowserShellTheme

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

        setContent {
            MaterialTheme {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .semantics {
                            testTagsAsResourceId = true
                        }
                        .testTag("androidShellRoot"),
                ) {
                    ProvideBrowserShellTheme {
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
        appUpdateManager.checkForUpdate()
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
