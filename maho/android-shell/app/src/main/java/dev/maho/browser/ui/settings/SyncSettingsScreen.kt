package dev.maho.browser.ui.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeSync
import dev.maho.browser.bridge.BridgeTabs
import dev.maho.browser.models.AccountSyncState
import dev.maho.browser.models.ConnectedDevice
import dev.maho.browser.sync.RelaySessionStore
import dev.maho.browser.sync.SyncManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.DateFormat
import java.util.Date
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SyncSettingsScreen(
    onBack: () -> Unit = {},
) {
    val scope = rememberCoroutineScope()
    var relaySessionEmail by remember { mutableStateOf(RelaySessionStore.load()?.email) }
    var serverUrlInput by remember { mutableStateOf(SyncManager.getServerUrl()) }
    var emailInput by remember { mutableStateOf(relaySessionEmail ?: "") }
    var passwordInput by remember { mutableStateOf("") }
    var syncEnabled by remember { mutableStateOf(SyncManager.isSyncing) }
    var syncState by remember { mutableStateOf<AccountSyncState>(if (relaySessionEmail == null) AccountSyncState.Offline else AccountSyncState.Idle) }
    val devices = remember { mutableStateListOf<ConnectedDevice>() }
    var recoveryPhraseInput by remember { mutableStateOf("") }
    var generatedSyncKey by remember { mutableStateOf<String?>(null) }
    var joinResult by remember { mutableStateOf<String?>(null) }
    var syncError by remember { mutableStateOf<String?>(null) }

    fun refresh() {
        val session = RelaySessionStore.load()
        relaySessionEmail = session?.email
        if (session != null) {
            serverUrlInput = session.serverUrl ?: serverUrlInput
        }
        syncEnabled = SyncManager.isSyncing
        syncState = when {
            syncError != null -> AccountSyncState.Error(syncError!!)
            syncEnabled -> SyncManager.lastSyncTimestamp?.let {
                AccountSyncState.Synced(DateFormat.getDateTimeInstance().format(Date(it)))
            } ?: AccountSyncState.Syncing(0.0)
            session != null -> AccountSyncState.Idle
            else -> AccountSyncState.Offline
        }
        devices.clear()
        devices.addAll(BridgeSync.getConnectedDevices())
    }

    LaunchedEffect(Unit) {
        SyncManager.syncErrorListener = { message ->
            syncError = message
            refresh()
        }
        while (true) {
            refresh()
            delay(5_000)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Sync") },
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
            var showRelayServerUrl by remember { mutableStateOf(false) }

            androidx.compose.foundation.layout.Box(
                modifier = Modifier.clickable { showRelayServerUrl = !showRelayServerUrl }
            ) {
                SectionHeader("Account")
            }
            ListItem(
                headlineContent = { Text("Relay account") },
                supportingContent = {
                    Text("Use your relay email and password to enable authenticated background polling.")
                },
            )
            if (showRelayServerUrl) {
                OutlinedTextField(
                    value = serverUrlInput,
                    onValueChange = {
                        serverUrlInput = it
                        SyncManager.updateServerUrl(it)
                    },
                    label = { Text("Relay Server URL") },
                    singleLine = true,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp),
                )
            }
            OutlinedTextField(
                value = emailInput,
                onValueChange = { emailInput = it },
                label = { Text("Email") },
                singleLine = true,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 8.dp),
            )
            OutlinedTextField(
                value = passwordInput,
                onValueChange = { passwordInput = it },
                label = { Text("Password") },
                singleLine = true,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
            )
            Row(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    onClick = {
                        scope.launch {
                            try {
                                withContext(Dispatchers.IO) {
                                    SyncManager.signUp(serverUrlInput, emailInput, passwordInput)
                                }
                                relaySessionEmail = emailInput
                                syncError = null
                                passwordInput = ""
                                refresh()
                            } catch (e: Exception) {
                                syncError = e.message ?: "Unable to sign up"
                                refresh()
                            }
                        }
                    },
                    enabled = emailInput.isNotBlank() && passwordInput.isNotBlank() && serverUrlInput.isNotBlank(),
                ) {
                    Text("Sign Up")
                }
                OutlinedButton(
                    onClick = {
                        scope.launch {
                            try {
                                withContext(Dispatchers.IO) {
                                    SyncManager.logIn(serverUrlInput, emailInput, passwordInput)
                                }
                                relaySessionEmail = emailInput
                                syncError = null
                                passwordInput = ""
                                refresh()
                            } catch (e: Exception) {
                                syncError = e.message ?: "Unable to log in"
                                refresh()
                            }
                        }
                    },
                    enabled = emailInput.isNotBlank() && passwordInput.isNotBlank() && serverUrlInput.isNotBlank(),
                ) {
                    Text("Log In")
                }
                if (relaySessionEmail != null) {
                    OutlinedButton(
                        onClick = {
                            scope.launch {
                                withContext(Dispatchers.IO) {
                                    SyncManager.logout()
                                }
                                relaySessionEmail = null
                                syncEnabled = false
                                syncError = null
                                refresh()
                            }
                        },
                        colors = ButtonDefaults.outlinedButtonColors(
                            contentColor = MaterialTheme.colorScheme.error,
                        ),
                    ) {
                        Text("Sign Out")
                    }
                }
            }
            if (relaySessionEmail != null) {
                ListItem(
                    headlineContent = { Text(relaySessionEmail!!) },
                    supportingContent = { Text("Relay session saved locally for authenticated polling.") },
                )
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            if (syncError != null) {
                ListItem(
                    leadingContent = {
                        Icon(painter = painterResource(id = MahoIcon.CircleAlert.drawableRes), contentDescription = null, tint = MaterialTheme.colorScheme.error)
                    },
                    headlineContent = { Text("Sync Error") },
                    supportingContent = { Text(syncError!!) },
                )
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            }

            if (relaySessionEmail != null) {
                ListItem(
                    headlineContent = { Text("Background Polling") },
                    supportingContent = { Text("Periodically pushes local changes and pulls relay updates over HTTP.") },
                    trailingContent = {
                        Switch(
                            checked = syncEnabled,
                            onCheckedChange = {
                                syncEnabled = it
                                if (it) {
                                    SyncManager.startSync()
                                } else {
                                    SyncManager.stopSync()
                                }
                                refresh()
                            },
                        )
                    },
                )
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            }

            SectionHeader("Status")
            ListItem(
                leadingContent = {
                    val (icon, tint) = when (val state = syncState) {
                        is AccountSyncState.Idle -> MahoIcon.Cloud to MaterialTheme.colorScheme.onSurfaceVariant
                        is AccountSyncState.Syncing -> MahoIcon.SyncIcon to MaterialTheme.colorScheme.primary
                        is AccountSyncState.Synced -> MahoIcon.CheckCircle to MaterialTheme.colorScheme.primary
                        is AccountSyncState.Error -> MahoIcon.CircleAlert to MaterialTheme.colorScheme.error
                        is AccountSyncState.Offline -> MahoIcon.CloudOff to MaterialTheme.colorScheme.onSurfaceVariant
                    }
                    Icon(painter = painterResource(id = icon.drawableRes), contentDescription = null, tint = tint)
                },
                headlineContent = {
                    Text(
                        when (val state = syncState) {
                            is AccountSyncState.Idle -> "Idle"
                            is AccountSyncState.Syncing -> "Polling relay"
                            is AccountSyncState.Synced -> "Synced"
                            is AccountSyncState.Error -> "Error"
                            is AccountSyncState.Offline -> "Offline"
                        },
                    )
                },
                supportingContent = {
                    val detail = when (val s = syncState) {
                        is AccountSyncState.Syncing -> "Waiting for the next background poll"
                        is AccountSyncState.Synced -> "Last sync: ${s.lastSyncAt}"
                        is AccountSyncState.Error -> s.message
                        is AccountSyncState.Idle, is AccountSyncState.Offline -> null
                    }
                    if (detail != null) Text(detail)
                },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            if (devices.isNotEmpty()) {
                SectionHeader("Connected Devices")
                devices.forEach { device ->
                    ListItem(
                        leadingContent = {
                            val icon = when (device.deviceType.lowercase()) {
                                "desktop", "mac", "pc" -> MahoIcon.Monitor
                                "phone", "iphone", "android" -> MahoIcon.Smartphone
                                "tablet", "ipad" -> MahoIcon.TabletSmartphone
                                else -> MahoIcon.Monitor
                            }
                            Icon(painter = painterResource(id = icon.drawableRes), contentDescription = null)
                        },
                        headlineContent = { Text(device.name) },
                        supportingContent = {
                            if (device.lastSeen != null) Text("Last seen: ${device.lastSeen}")
                        },
                        trailingContent = {
                            Row {
                                IconButton(onClick = {
                                    val activeTabId = BridgeTabs.getActiveTabId()
                                    val activeTab = BridgeTabs.getTabViewModels().firstOrNull { tab -> tab.id == activeTabId }
                                    if (activeTab != null) {
                                        BridgeSync.sendTabToDevice(url = activeTab.url, title = activeTab.title, deviceId = device.id)
                                    } else {
                                        BridgeSync.sendTabToDevice(url = "", title = "", deviceId = device.id)
                                    }
                                }) {
                                    Icon(painter = painterResource(id = MahoIcon.Send.drawableRes), contentDescription = "Send Tab")
                                }
                                IconButton(onClick = {
                                    BridgeSync.removeSyncDevice(device.id)
                                    devices.remove(device)
                                }) {
                                    Icon(painter = painterResource(id = MahoIcon.Delete.drawableRes),
                                        contentDescription = "Remove Device",
                                        tint = MaterialTheme.colorScheme.error,
                                    )
                                }
                            }
                        },
                    )
                }
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            }

            SectionHeader("Recovery")
            Row(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Button(onClick = {
                    generatedSyncKey = BridgeSync.generateSyncKey()
                }) {
                    Text("Generate Recovery Phrase")
                }
            }
            if (generatedSyncKey != null) {
                ListItem(
                    headlineContent = { Text("Recovery Phrase") },
                    supportingContent = { Text(generatedSyncKey!!) },
                )
            }
            OutlinedTextField(
                value = recoveryPhraseInput,
                onValueChange = { recoveryPhraseInput = it },
                label = { Text("Recovery Phrase") },
                singleLine = true,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
            )
            Button(
                onClick = {
                    if (recoveryPhraseInput.isNotBlank()) {
                        joinResult = BridgeSync.joinSync(recoveryPhraseInput)
                        recoveryPhraseInput = ""
                        refresh()
                    }
                },
                enabled = recoveryPhraseInput.isNotBlank(),
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
            ) {
                Text("Join with Recovery Phrase")
            }
            if (joinResult != null) {
                ListItem(
                    headlineContent = { Text("Joined successfully") },
                    supportingContent = { Text(joinResult!!) },
                )
            }
        }
    }
}
