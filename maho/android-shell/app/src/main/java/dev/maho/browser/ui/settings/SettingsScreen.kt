package dev.maho.browser.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.maho.browser.ui.components.MahoGroupedRow
import dev.maho.browser.ui.components.MahoGroupedSection
import dev.maho.browser.ui.components.MahoRowDivider
import dev.maho.browser.ui.icons.MahoIcon
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
    onNavigateToWebAgent: () -> Unit = {},
    onNavigateToApiKeys: () -> Unit = {},
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
    var showClearDialog by remember { mutableStateOf(false) }

    if (showClearDialog) {
        AlertDialog(
            onDismissRequest = { showClearDialog = false },
            title = { Text("Clear browsing data?", fontWeight = FontWeight.SemiBold) },
            text = { Text("Cookies, local storage, and cached page data will be removed from this device.") },
            confirmButton = {
                TextButton(onClick = {
                    onClearBrowsingData()
                    showClearDialog = false
                }) {
                    Text("Clear", color = shellColors.error, fontWeight = FontWeight.SemiBold)
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearDialog = false }) {
                    Text("Cancel", color = shellColors.textSecondary)
                }
            },
            containerColor = shellColors.cardBackground,
            titleContentColor = shellColors.textPrimary,
            textContentColor = shellColors.textSecondary,
        )
    }

    Scaffold(
        containerColor = shellColors.overlayBackground,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "Settings",
                        style = MaterialTheme.typography.titleMedium.copy(
                            fontSize = 17.sp,
                            fontWeight = FontWeight.SemiBold,
                        ),
                    )
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = shellColors.overlayBackground,
                    titleContentColor = shellColors.textPrimary,
                    navigationIconContentColor = shellColors.textPrimary,
                ),
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            painter = painterResource(id = MahoIcon.NavBack.drawableRes),
                            contentDescription = "Back",
                        )
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .background(shellColors.overlayBackground)
                .verticalScroll(rememberScrollState()),
        ) {
            MahoGroupedSection(title = "Account & Sync") {
                MahoGroupedRow(
                    title = "Sync",
                    subtitle = "Account backup and device synchronization",
                    icon = painterResource(id = MahoIcon.SyncIcon.drawableRes),
                    iconTint = Color(0xFF0A84FF),
                    showChevron = true,
                    onClick = onNavigateToSync,
                )
            }

            MahoGroupedSection(title = "Browsing") {
                MahoGroupedRow(
                    title = "Search Engine",
                    subtitle = selectedSearchEngine,
                    icon = painterResource(id = MahoIcon.Search.drawableRes),
                    iconTint = Color(0xFF5E5CE6),
                    showChevron = true,
                    onClick = onNavigateToGeneral,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "General",
                    subtitle = "Startup, tabs, and downloads",
                    icon = painterResource(id = MahoIcon.Settings.drawableRes),
                    iconTint = Color(0xFF8E8E93),
                    showChevron = true,
                    onClick = onNavigateToGeneral,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "Appearance",
                    subtitle = "Theme, fonts, and zoom",
                    icon = painterResource(id = MahoIcon.Palette.drawableRes),
                    iconTint = Color(0xFFFF9F0A),
                    showChevron = true,
                    onClick = onNavigateToAppearance,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "Privacy & Security",
                    subtitle = "Blockers, cookies, and permissions",
                    icon = painterResource(id = MahoIcon.Privacy.drawableRes),
                    iconTint = Color(0xFF30D158),
                    showChevron = true,
                    onClick = onNavigateToPrivacy,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "Clear Browsing Data",
                    icon = painterResource(id = MahoIcon.Delete.drawableRes),
                    iconTint = shellColors.error,
                    onClick = { showClearDialog = true },
                )
            }

            MahoGroupedSection(title = "Features") {
                MahoGroupedRow(
                    title = "Reader Mode",
                    subtitle = "Clean reading view customization",
                    icon = painterResource(id = MahoIcon.Book.drawableRes),
                    iconTint = Color(0xFF64D2FF),
                    showChevron = true,
                    onClick = onNavigateToReader,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "Autofill & Passwords",
                    subtitle = "Saved credentials and addresses",
                    icon = painterResource(id = MahoIcon.AutofillKey.drawableRes),
                    iconTint = Color(0xFFFFD60A),
                    showChevron = true,
                    onClick = onNavigateToAutofill,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "Notifications",
                    subtitle = "Push and background alerts",
                    icon = painterResource(id = MahoIcon.Notifications.drawableRes),
                    iconTint = Color(0xFFFF453A),
                    showChevron = true,
                    onClick = onNavigateToNotifications,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "Max AI",
                    subtitle = "Model selection and AI features",
                    icon = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                    iconTint = Color(0xFFBF5AF2),
                    showChevron = true,
                    onClick = onNavigateToMax,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "Web Agent",
                    subtitle = "Autonomous browser agent tasks",
                    icon = painterResource(id = MahoIcon.Bot.drawableRes),
                    iconTint = Color(0xFF0A84FF),
                    showChevron = true,
                    onClick = onNavigateToWebAgent,
                )
            }

            MahoGroupedSection(title = "App") {
                MahoGroupedRow(
                    title = "App Icon",
                    icon = painterResource(id = MahoIcon.Spaces.drawableRes),
                    iconTint = Color(0xFFFF375F),
                    showChevron = true,
                    onClick = onNavigateToAppIcon,
                )
                MahoRowDivider()
                MahoGroupedRow(
                    title = "Feedback",
                    icon = painterResource(id = MahoIcon.HelpFeedback.drawableRes),
                    iconTint = Color(0xFF32D74B),
                    showChevron = true,
                    onClick = onNavigateToFeedback,
                )
            }

            Spacer(modifier = Modifier.height(32.dp))
        }
    }
}
