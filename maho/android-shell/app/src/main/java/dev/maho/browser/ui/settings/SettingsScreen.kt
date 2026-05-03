package dev.maho.browser.ui.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Book
import androidx.compose.material.icons.filled.Bookmark
import androidx.compose.material.icons.filled.ClearAll
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Feedback
import androidx.compose.material.icons.filled.Key
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material.icons.filled.Tune
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.theme.BrowserShellTheme

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
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
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
                leadingContent = { Icon(Icons.Filled.ClearAll, contentDescription = null) },
                modifier = Modifier.clickable { showClearDialog = true },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SettingsNavItem(Icons.Filled.Settings, "General", onClick = onNavigateToGeneral)
            SettingsNavItem(Icons.Filled.Palette, "Appearance", onClick = onNavigateToAppearance)
            SettingsNavItem(Icons.Filled.Security, "Privacy & Security", onClick = onNavigateToPrivacy)
            SettingsNavItem(Icons.Filled.Sync, "Sync", onClick = onNavigateToSync)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(Icons.Filled.Book, "Reader Mode", onClick = onNavigateToReader)
            SettingsNavItem(Icons.Filled.Key, "Autofill & Passwords", onClick = onNavigateToAutofill)
            SettingsNavItem(Icons.Filled.Notifications, "Notifications", onClick = onNavigateToNotifications)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(Icons.Filled.Star, "Max AI", onClick = onNavigateToMax)
            SettingsNavItem(Icons.Filled.Tune, "Advanced", onClick = onNavigateToAdvanced)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(Icons.Filled.Code, "Profiles", onClick = onNavigateToProfiles)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(Icons.Filled.Bookmark, "Bookmarks", onClick = onNavigateToBookmarks)
            SettingsNavItem(Icons.Filled.Security, "History", onClick = onNavigateToHistory)
            SettingsNavItem(Icons.Filled.Notifications, "Downloads", onClick = onNavigateToDownloads)
            SettingsNavItem(Icons.Filled.Book, "Notes", onClick = onNavigateToNotes)
            SettingsNavItem(Icons.Filled.Star, "Reading List", onClick = onNavigateToReadingList)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            SettingsNavItem(Icons.Filled.Palette, "App Icon", onClick = onNavigateToAppIcon)
            SettingsNavItem(Icons.Filled.Language, "Language", onClick = onNavigateToLanguage)
            SettingsNavItem(Icons.Filled.Feedback, "Feedback", onClick = onNavigateToFeedback)
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            ListItem(
                headlineContent = {
                    TextButton(onClick = onResetSettings) {
                        Icon(Icons.Filled.RestartAlt, contentDescription = null)
                        Text("Reset All Settings", modifier = Modifier.padding(start = 8.dp))
                    }
                },
            )
        }
    }
}

@Composable
private fun SettingsNavItem(
    icon: ImageVector,
    title: String,
    onClick: () -> Unit,
) {
    ListItem(
        headlineContent = { Text(title) },
        leadingContent = { Icon(icon, contentDescription = title) },
        modifier = Modifier.clickable(onClick = onClick),
    )
}
