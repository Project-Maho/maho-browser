package dev.maho.browser.ui.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    onNavigateToGeneral: () -> Unit = {},
    onNavigateToAppearance: () -> Unit = {},
    onNavigateToPrivacy: () -> Unit = {},
    onNavigateToReader: () -> Unit = {},
    onNavigateToAutofill: () -> Unit = {},
    onNavigateToNotifications: () -> Unit = {},
    onNavigateToMax: () -> Unit = {},
    onNavigateToWebAgent: () -> Unit = {},
    onNavigateToAdvanced: () -> Unit = {},
    onNavigateToProfiles: () -> Unit = {},
    onNavigateToBookmarks: () -> Unit = {},
    onNavigateToHistory: () -> Unit = {},
    onNavigateToDownloads: () -> Unit = {},
    onNavigateToNotes: () -> Unit = {},
    onNavigateToReadingList: () -> Unit = {},
    onNavigateToAppIcon: () -> Unit = {},
    onNavigateToFeedback: () -> Unit = {},
    onNavigateToLanguage: () -> Unit = {},
    onNavigateToSync: () -> Unit = {},
    onResetSettings: () -> Unit = {},
    selectedSearchEngine: String = "Google",
    onSelectSearchEngine: (String) -> Unit = {},
    onClearBrowsingData: () -> Unit = {},
    onBack: () -> Unit = {},
) {
    val shellColors = BrowserShellTheme.colors
    val searchEngines = listOf("Google", "Bing", "DuckDuckGo", "Ecosia", "Perplexity", "Kagi")
    var showClearDialog by remember { mutableStateOf(false) }

    if (showClearDialog) {
        AlertDialog(
            onDismissRequest = { showClearDialog = false },
            title = { Text("Clear browsing data?") },
            text = { Text("Cookies, local storage, and cached page data will be removed from this device.") },
            confirmButton = {
                TextButton(onClick = {
                    onClearBrowsingData()
                    showClearDialog = false
                }) {
                    Text("Clear")
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearDialog = false }) {
                    Text("Cancel")
                }
            },
        )
    }

    Scaffold(
        containerColor = shellColors.overlayBackground,
        topBar = {
            TopAppBar(
                title = { Text("Settings") },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = shellColors.overlayBackground,
                    titleContentColor = shellColors.textPrimary,
                    navigationIconContentColor = shellColors.textPrimary,
                ),
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
            SectionHeader("Browsing")
            DropdownPreference(
                title = "Search Engine",
                value = selectedSearchEngine,
                options = searchEngines,
                onSelect = { idx -> onSelectSearchEngine(searchEngines[idx]) },
            )
            ListItem(
                headlineContent = { Text("Clear Browsing Data") },
                supportingContent = { Text("Remove cookies, storage, and cached page state") },
                leadingContent = { Icon(painter = painterResource(id = MahoIcon.ClearSweep.drawableRes), contentDescription = null) },
                modifier = Modifier.clickable { showClearDialog = true },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SettingsNavItem(MahoIcon.Settings, "General", onClick = onNavigateToGeneral)
            SettingsNavItem(MahoIcon.Palette, "Appearance", onClick = onNavigateToAppearance)
            SettingsNavItem(MahoIcon.Privacy, "Privacy & Security", onClick = onNavigateToPrivacy)
            SettingsNavItem(MahoIcon.SyncIcon, "Sync", onClick = onNavigateToSync)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(MahoIcon.Book, "Reader Mode", onClick = onNavigateToReader)
            SettingsNavItem(MahoIcon.AutofillKey, "Autofill & Passwords", onClick = onNavigateToAutofill)
            SettingsNavItem(MahoIcon.Notifications, "Notifications", onClick = onNavigateToNotifications)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(MahoIcon.Star, "Max AI", onClick = onNavigateToMax)
            SettingsNavItem(MahoIcon.SparklesAi, "Web Agent", onClick = onNavigateToWebAgent)
            SettingsNavItem(MahoIcon.Tune, "Advanced", onClick = onNavigateToAdvanced)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(MahoIcon.Code, "Profiles", onClick = onNavigateToProfiles)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(MahoIcon.Bookmarks, "Bookmarks", onClick = onNavigateToBookmarks)
            SettingsNavItem(MahoIcon.Privacy, "History", onClick = onNavigateToHistory)
            SettingsNavItem(MahoIcon.Notifications, "Downloads", onClick = onNavigateToDownloads)
            SettingsNavItem(MahoIcon.Book, "Notes", onClick = onNavigateToNotes)
            SettingsNavItem(MahoIcon.Star, "Reading List", onClick = onNavigateToReadingList)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(MahoIcon.Palette, "App Icon", onClick = onNavigateToAppIcon)
            SettingsNavItem(MahoIcon.Globe, "Language", onClick = onNavigateToLanguage)
            SettingsNavItem(MahoIcon.HelpFeedback, "Feedback", onClick = onNavigateToFeedback)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            ListItem(
                headlineContent = {
                    TextButton(onClick = onResetSettings) {
                        Icon(painter = painterResource(id = MahoIcon.RestartAlt.drawableRes), contentDescription = null)
                        Text("Reset All Settings", modifier = Modifier.padding(start = 8.dp))
                    }
                },
            )
        }
    }
}

@Composable
private fun SettingsNavItem(
    icon: MahoIcon,
    title: String,
    onClick: () -> Unit,
) {
    ListItem(
        headlineContent = { Text(title) },
        leadingContent = { Icon(painter = painterResource(id = icon.drawableRes), contentDescription = title) },
        modifier = Modifier.clickable(onClick = onClick),
    )
}
