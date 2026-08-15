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
import dev.maho.browser.models.AdvancedSettingsUpdate
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AdvancedSettingsScreen(
    onBack: () -> Unit = {},
) {
    var developerMode by remember { mutableStateOf(false) }
    var hardwareAcceleration by remember { mutableStateOf(true) }
    var experimentalFeatures by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        developerMode = settings.advanced.developerMode
        hardwareAcceleration = settings.advanced.hardwareAcceleration
        experimentalFeatures = settings.advanced.experimentalFeatures
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Advanced") },
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
            SectionHeader("Developer")
            SwitchPreference("Developer Mode", developerMode) {
                developerMode = it
                BridgeSettings.updateAdvancedSettings(AdvancedSettingsUpdate(developerMode = it))
            }
            if (developerMode) {
                Text(
                    "Web Inspector and console logging are enabled.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Performance")
            SwitchPreference("Hardware Acceleration", hardwareAcceleration) {
                hardwareAcceleration = it
                BridgeSettings.updateAdvancedSettings(AdvancedSettingsUpdate(hardwareAcceleration = it))
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Experimental")
            SwitchPreference("Experimental Features", experimentalFeatures) {
                experimentalFeatures = it
                BridgeSettings.updateAdvancedSettings(AdvancedSettingsUpdate(experimentalFeatures = it))
            }
            if (experimentalFeatures) {
                Text(
                    "Experimental features may be unstable. Use at your own risk.",
                    style = MaterialTheme.typography.bodySmall,
                    color = BrowserShellTheme.colors.warning,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }
        }
    }
}
