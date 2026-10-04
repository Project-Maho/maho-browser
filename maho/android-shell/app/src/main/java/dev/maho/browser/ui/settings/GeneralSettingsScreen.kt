package dev.maho.browser.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
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
import dev.maho.browser.bridge.BridgeSettings
import dev.maho.browser.models.AutoplayPolicy
import dev.maho.browser.models.GeneralSettingsUpdate
import dev.maho.browser.models.RestorePolicy
import dev.maho.browser.models.SettingsSearchEngine
import dev.maho.browser.ui.components.MahoGroupedRow
import dev.maho.browser.ui.components.MahoGroupedSection
import dev.maho.browser.ui.components.MahoRowDivider
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme

private val searchEngineOptions = listOf(
    SettingsSearchEngine("Google", "https://www.google.com/search?q={query}", true),
    SettingsSearchEngine("DuckDuckGo", "https://duckduckgo.com/?q={query}", true),
    SettingsSearchEngine("Bing", "https://www.bing.com/search?q={query}", true),
    SettingsSearchEngine("Brave", "https://search.brave.com/search?q={query}", true),
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GeneralSettingsScreen(
    onBack: () -> Unit = {},
) {
    val shellColors = BrowserShellTheme.colors
    var restorePolicy by remember { mutableStateOf(RestorePolicy.RestoreAll) }
    var autoplayPolicy by remember { mutableStateOf(AutoplayPolicy.BlockAudio) }
    var archiveTimeoutHours by remember { mutableStateOf(24.0) }
    var searchEngineName by remember { mutableStateOf("Google") }

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        restorePolicy = settings.general.restoreOnLaunch
        autoplayPolicy = settings.general.autoplayPolicy
        archiveTimeoutHours = settings.general.archiveTimeoutHours
        searchEngineName = settings.general.defaultSearchEngine.name
    }

    Scaffold(
        containerColor = shellColors.overlayBackground,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "General",
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
            MahoGroupedSection(title = "Search") {
                searchEngineOptions.forEachIndexed { index, engine ->
                    if (index > 0) MahoRowDivider()
                    MahoGroupedRow(
                        title = engine.name,
                        subtitle = if (searchEngineName == engine.name) "Default search engine" else null,
                        icon = painterResource(id = MahoIcon.Search.drawableRes),
                        iconTint = Color(0xFF5E5CE6),
                        trailingContent = {
                            if (searchEngineName == engine.name) {
                                Text(
                                    text = "✓",
                                    color = shellColors.accent,
                                    fontSize = 17.sp,
                                    fontWeight = FontWeight.Bold,
                                )
                            }
                        },
                        onClick = {
                            searchEngineName = engine.name
                            BridgeSettings.updateGeneralSettings(
                                GeneralSettingsUpdate(defaultSearchEngine = engine)
                            )
                        },
                    )
                }
            }

            MahoGroupedSection(title = "Startup") {
                val restoreOptions = listOf(
                    RestorePolicy.RestoreAll to "Restore All Tabs",
                    RestorePolicy.RestorePinned to "Restore Pinned Only",
                    RestorePolicy.StartFresh to "Start Fresh",
                )
                restoreOptions.forEachIndexed { index, (policy, label) ->
                    if (index > 0) MahoRowDivider()
                    MahoGroupedRow(
                        title = label,
                        trailingContent = {
                            if (restorePolicy == policy) {
                                Text(
                                    text = "✓",
                                    color = shellColors.accent,
                                    fontSize = 17.sp,
                                    fontWeight = FontWeight.Bold,
                                )
                            }
                        },
                        onClick = {
                            restorePolicy = policy
                            BridgeSettings.updateGeneralSettings(
                                GeneralSettingsUpdate(restoreOnLaunch = policy)
                            )
                        },
                    )
                }
            }

            MahoGroupedSection(title = "Tabs") {
                val archiveOptions = listOf(
                    "Off" to -1.0,
                    "12 hours" to 12.0,
                    "24 hours" to 24.0,
                    "7 days" to 168.0,
                    "30 days" to 720.0,
                )
                archiveOptions.forEachIndexed { index, (label, hours) ->
                    if (index > 0) MahoRowDivider()
                    MahoGroupedRow(
                        title = "Auto-Archive",
                        subtitle = label,
                        trailingContent = {
                            if (archiveTimeoutHours == hours) {
                                Text(
                                    text = "✓",
                                    color = shellColors.accent,
                                    fontSize = 17.sp,
                                    fontWeight = FontWeight.Bold,
                                )
                            }
                        },
                        onClick = {
                            archiveTimeoutHours = hours
                            BridgeSettings.updateGeneralSettings(
                                GeneralSettingsUpdate(archiveTimeoutHours = hours)
                            )
                        },
                    )
                }
            }

            MahoGroupedSection(title = "Media") {
                val mediaOptions = listOf(
                    AutoplayPolicy.Allow to "Allow All",
                    AutoplayPolicy.BlockAudio to "Block Audio",
                    AutoplayPolicy.BlockAll to "Block All",
                )
                mediaOptions.forEachIndexed { index, (policy, label) ->
                    if (index > 0) MahoRowDivider()
                    MahoGroupedRow(
                        title = "Autoplay Policy",
                        subtitle = label,
                        trailingContent = {
                            if (autoplayPolicy == policy) {
                                Text(
                                    text = "✓",
                                    color = shellColors.accent,
                                    fontSize = 17.sp,
                                    fontWeight = FontWeight.Bold,
                                )
                            }
                        },
                        onClick = {
                            autoplayPolicy = policy
                            BridgeSettings.updateGeneralSettings(
                                GeneralSettingsUpdate(autoplayPolicy = policy)
                            )
                        },
                    )
                }
            }

            Spacer(modifier = Modifier.height(32.dp))
        }
    }
}
