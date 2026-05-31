package dev.maho.browser.ui.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeSettings
import dev.maho.browser.MahoBridge
import dev.maho.browser.models.MaxSettingsUpdate
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MaxSettingsScreen(
    onBack: () -> Unit = {},
) {
    var enabled by remember { mutableStateOf(false) }
    var pagePreviews by remember { mutableStateOf(false) }
    var tidyTabTitles by remember { mutableStateOf(false) }
    var tidyDownloads by remember { mutableStateOf(false) }
    var tidyTabs by remember { mutableStateOf(false) }
    var aiCommandBar by remember { mutableStateOf(false) }
    var instantLinks by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        val m = settings.max
        enabled = m.enabled
        pagePreviews = m.pagePreviews
        tidyTabTitles = m.tidyTabTitles
        tidyDownloads = m.tidyDownloads
        tidyTabs = m.tidyTabs
        aiCommandBar = m.aiCommandBar
        instantLinks = m.instantLinks
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Max AI") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(painter = painterResource(id = MahoIcon.NavBack.drawableRes), contentDescription = "Back")
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .verticalScroll(rememberScrollState()),
        ) {
            SwitchPreference("Enable Max AI", enabled) {
                enabled = it
                BridgeSettings.updateMaxSettings(MaxSettingsUpdate(enabled = it))
            }
            if (!enabled) {
                Text(
                    "Enable Max AI to unlock intelligent browsing features.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }

            if (enabled) {
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
                SectionHeader("AI Features")
                SwitchPreference("Page Previews", pagePreviews) {
                    pagePreviews = it
                    BridgeSettings.updateMaxSettings(MaxSettingsUpdate(pagePreviews = it))
                }
                SwitchPreference("Tidy Tab Titles", tidyTabTitles) {
                    tidyTabTitles = it
                    BridgeSettings.updateMaxSettings(MaxSettingsUpdate(tidyTabTitles = it))
                }
                SwitchPreference("Tidy Downloads", tidyDownloads) {
                    tidyDownloads = it
                    BridgeSettings.updateMaxSettings(MaxSettingsUpdate(tidyDownloads = it))
                }
                SwitchPreference("Tidy Tabs", tidyTabs) {
                    tidyTabs = it
                    BridgeSettings.updateMaxSettings(MaxSettingsUpdate(tidyTabs = it))
                }
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

                SectionHeader("Smart Features")
                SwitchPreference("AI Command Bar", aiCommandBar) {
                    aiCommandBar = it
                    BridgeSettings.updateMaxSettings(MaxSettingsUpdate(aiCommandBar = it))
                }
                SwitchPreference("Instant Links", instantLinks) {
                    instantLinks = it
                    BridgeSettings.updateMaxSettings(MaxSettingsUpdate(instantLinks = it))
                }
            }
        }
    }
}
