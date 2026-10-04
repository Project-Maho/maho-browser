package dev.maho.browser.ui.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import dev.maho.browser.bridge.sendEvent
import dev.maho.browser.MahoBridge
import dev.maho.browser.models.ContentBlockingMode
import dev.maho.browser.models.PrivacySettingsUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PrivacySettingsScreen(
    onBack: () -> Unit = {},
) {
    var doNotTrack by remember { mutableStateOf(false) }
    var blockThirdPartyCookies by remember { mutableStateOf(true) }
    var contentBlockerEnabled by remember { mutableStateOf(false) }
    var contentBlockingMode by remember { mutableStateOf(ContentBlockingMode.NATIVE) }
    var popupBlockerEnabled by remember { mutableStateOf(true) }
    var searchSuggestionsEnabled by remember { mutableStateOf(false) }
    var secureDnsEnabled by remember { mutableStateOf(false) }
    var secureDnsProvider by remember { mutableStateOf("") }
    var clearDataOnExit by remember { mutableStateOf(false) }
    var safeBrowsingEnabled by remember { mutableStateOf(true) }
    var showClearDialog by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        val p = settings.privacy
        doNotTrack = p.doNotTrack
        blockThirdPartyCookies = p.blockThirdPartyCookies
        contentBlockerEnabled = p.contentBlockerEnabled
        contentBlockingMode = p.contentBlockingMode
        popupBlockerEnabled = p.popupBlockerEnabled
        searchSuggestionsEnabled = p.searchSuggestionsEnabled
        secureDnsEnabled = p.secureDnsEnabled
        secureDnsProvider = p.secureDnsProvider
        clearDataOnExit = p.clearDataOnExit
        safeBrowsingEnabled = p.safeBrowsingEnabled
    }

    if (showClearDialog) {
        AlertDialog(
            onDismissRequest = { showClearDialog = false },
            title = { Text("Clear All Browsing Data?") },
            text = { Text("This cannot be undone.") },
            confirmButton = {
                TextButton(onClick = {
                    MahoBridge.sendEvent(ShellEvent.ClearHistory)
                    showClearDialog = false
                }) { Text("Clear") }
            },
            dismissButton = {
                TextButton(onClick = { showClearDialog = false }) { Text("Cancel") }
            },
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Privacy & Security") },
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
            SectionHeader("Tracking Protection")
            SwitchPreference("Send Do Not Track", doNotTrack) {
                doNotTrack = it
                BridgeSettings.updatePrivacySettings(PrivacySettingsUpdate(doNotTrack = it))
            }
            SwitchPreference("Block Third-Party Cookies", blockThirdPartyCookies) {
                blockThirdPartyCookies = it
                BridgeSettings.updatePrivacySettings(PrivacySettingsUpdate(blockThirdPartyCookies = it))
            }
            SwitchPreference("Safe Browsing", safeBrowsingEnabled) {
                safeBrowsingEnabled = it
                BridgeSettings.updatePrivacySettings(PrivacySettingsUpdate(safeBrowsingEnabled = it))
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Content")
            val nativeBlockingEnabled = contentBlockingMode.isNative && contentBlockerEnabled
            SwitchPreference("Content Blocker", nativeBlockingEnabled) {
                contentBlockerEnabled = it
                BridgeSettings.updatePrivacySettings(PrivacySettingsUpdate(contentBlockerEnabled = it))
            }
            if (nativeBlockingEnabled) {
                var showAddWhitelistDialog by remember { mutableStateOf(false) }
                var newWhitelistDomain by remember { mutableStateOf("") }
                val context = androidx.compose.ui.platform.LocalContext.current
                var whitelist by remember { mutableStateOf(WhitelistManager.getWhitelist(context).toList()) }

                whitelist.forEach { domain ->
                    androidx.compose.foundation.layout.Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 4.dp),
                        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically
                    ) {
                        Text(domain, modifier = Modifier.weight(1f))
                        IconButton(onClick = {
                            WhitelistManager.removeDomain(context, domain)
                            whitelist = WhitelistManager.getWhitelist(context).toList()
                        }) {
                            Icon(
                                painter = painterResource(id = MahoIcon.Close.drawableRes),
                                contentDescription = "Remove from Whitelist"
                            )
                        }
                    }
                }

                TextButton(
                    onClick = { showAddWhitelistDialog = true },
                    modifier = Modifier.padding(horizontal = 16.dp),
                ) { Text("Add Whitelisted Domain") }

                if (showAddWhitelistDialog) {
                    AlertDialog(
                        onDismissRequest = { showAddWhitelistDialog = false },
                        title = { Text("Add Domain to Whitelist") },
                        text = {
                            OutlinedTextField(
                                value = newWhitelistDomain,
                                onValueChange = { newWhitelistDomain = it },
                                label = { Text("example.com") }
                            )
                        },
                        confirmButton = {
                            TextButton(onClick = {
                                val trimmed = newWhitelistDomain.trim()
                                if (trimmed.isNotEmpty()) {
                                    WhitelistManager.addDomain(context, trimmed)
                                    whitelist = WhitelistManager.getWhitelist(context).toList()
                                    newWhitelistDomain = ""
                                    showAddWhitelistDialog = false
                                }
                            }) { Text("Add") }
                        },
                        dismissButton = {
                            TextButton(onClick = { showAddWhitelistDialog = false }) { Text("Cancel") }
                        }
                    )
                }
            }
            SwitchPreference("Popup Blocker", popupBlockerEnabled) {
                popupBlockerEnabled = it
                BridgeSettings.updatePrivacySettings(PrivacySettingsUpdate(popupBlockerEnabled = it))
            }
            SwitchPreference("Search Suggestions", searchSuggestionsEnabled) {
                searchSuggestionsEnabled = it
                BridgeSettings.updatePrivacySettings(PrivacySettingsUpdate(searchSuggestionsEnabled = it))
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("DNS")
            SwitchPreference("Secure DNS", secureDnsEnabled) {
                secureDnsEnabled = it
                BridgeSettings.updatePrivacySettings(PrivacySettingsUpdate(secureDnsEnabled = it))
            }
            if (secureDnsEnabled) {
                OutlinedTextField(
                    value = secureDnsProvider,
                    onValueChange = { secureDnsProvider = it },
                    label = { Text("DNS Provider") },
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
                TextButton(
                    onClick = {
                        BridgeSettings.updatePrivacySettings(
                            PrivacySettingsUpdate(secureDnsProvider = secureDnsProvider.trim())
                        )
                    },
                    enabled = secureDnsProvider.isNotBlank(),
                    modifier = Modifier.padding(horizontal = 16.dp),
                ) { Text("Save DNS Provider") }
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Data")
            SwitchPreference("Clear Data on Exit", clearDataOnExit) {
                clearDataOnExit = it
                BridgeSettings.updatePrivacySettings(PrivacySettingsUpdate(clearDataOnExit = it))
            }
            TextButton(
                onClick = { showClearDialog = true },
                modifier = Modifier.padding(horizontal = 16.dp),
            ) { Text("Clear Browsing Data Now") }
        }
    }
}

object WhitelistManager {
    fun getWhitelist(context: android.content.Context): Set<String> {
        val prefs = context.getSharedPreferences("content_blocker_prefs", android.content.Context.MODE_PRIVATE)
        return prefs.getStringSet("whitelist", emptySet()) ?: emptySet()
    }

    fun addDomain(context: android.content.Context, domain: String) {
        val prefs = context.getSharedPreferences("content_blocker_prefs", android.content.Context.MODE_PRIVATE)
        val set = prefs.getStringSet("whitelist", emptySet())?.toMutableSet() ?: mutableSetOf()
        set.add(domain)
        prefs.edit().putStringSet("whitelist", set).apply()
    }

    fun removeDomain(context: android.content.Context, domain: String) {
        val prefs = context.getSharedPreferences("content_blocker_prefs", android.content.Context.MODE_PRIVATE)
        val set = prefs.getStringSet("whitelist", emptySet())?.toMutableSet() ?: mutableSetOf()
        set.remove(domain)
        prefs.edit().putStringSet("whitelist", set).apply()
    }
}
