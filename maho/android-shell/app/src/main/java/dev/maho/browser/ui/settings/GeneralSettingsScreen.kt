package dev.maho.browser.ui.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
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
import dev.maho.browser.models.AutoplayPolicy
import dev.maho.browser.models.GeneralSettingsUpdate
import dev.maho.browser.models.RestorePolicy
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GeneralSettingsScreen(
    onBack: () -> Unit = {},
) {
    var restorePolicy by remember { mutableStateOf(RestorePolicy.RestoreAll) }
    var autoplayPolicy by remember { mutableStateOf(AutoplayPolicy.BlockAudio) }
    var archiveTimeoutHours by remember { mutableStateOf(24.0) }
    var downloadPath by remember { mutableStateOf("") }
    var searchEngineName by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        restorePolicy = settings.general.restoreOnLaunch
        autoplayPolicy = settings.general.autoplayPolicy
        archiveTimeoutHours = settings.general.archiveTimeoutHours
        downloadPath = settings.general.downloadPath
        searchEngineName = settings.general.defaultSearchEngine.name
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("General") },
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
            SectionHeader("Search")
            ListItem(
                headlineContent = { Text("Search Engine") },
                supportingContent = { Text(searchEngineName) },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Startup")
            DropdownPreference(
                title = "On Launch",
                value = restorePolicy.name,
                options = RestorePolicy.entries.map { it.name },
                onSelect = { idx ->
                    restorePolicy = RestorePolicy.entries[idx]
                    BridgeSettings.updateGeneralSettings(
                        GeneralSettingsUpdate(restoreOnLaunch = restorePolicy)
                    )
                },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Tabs")
            val archiveOptions = listOf("Off" to -1.0, "12 hours" to 12.0, "24 hours" to 24.0, "7 days" to 168.0, "30 days" to 720.0, "60 days" to 1440.0, "90 days" to 2160.0)
            val selectedArchiveLabel = archiveOptions.firstOrNull { it.second == archiveTimeoutHours }?.first ?: "24 hours"
            DropdownPreference(
                title = "Archive Timeout",
                value = selectedArchiveLabel,
                options = archiveOptions.map { it.first },
                onSelect = { idx ->
                    archiveTimeoutHours = archiveOptions[idx].second
                    BridgeSettings.updateGeneralSettings(
                        GeneralSettingsUpdate(archiveTimeoutHours = archiveTimeoutHours)
                    )
                },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Media")
            DropdownPreference(
                title = "Autoplay",
                value = autoplayPolicy.name,
                options = AutoplayPolicy.entries.map { it.name },
                onSelect = { idx ->
                    autoplayPolicy = AutoplayPolicy.entries[idx]
                    BridgeSettings.updateGeneralSettings(
                        GeneralSettingsUpdate(autoplayPolicy = autoplayPolicy)
                    )
                },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Downloads")
            ListItem(
                headlineContent = { Text("Download Path") },
                supportingContent = { Text(downloadPath.ifEmpty { "Default" }) },
            )
        }
    }
}
