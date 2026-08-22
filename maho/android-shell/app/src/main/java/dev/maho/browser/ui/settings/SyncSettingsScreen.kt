package dev.maho.browser.ui.settings

import android.app.Activity
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.maho.browser.bridge.BridgeProfiles
import dev.maho.browser.bridge.BridgeSync
import dev.maho.browser.models.ConnectedDevice
import dev.maho.browser.sync.CredentialManagerGoogleSource
import dev.maho.browser.sync.RelaySessionStore
import dev.maho.browser.sync.SyncManager
import dev.maho.browser.sync.SyncStateKind
import dev.maho.browser.sync.SyncStateResult
import dev.maho.browser.ui.components.ConfirmDialog
import dev.maho.browser.ui.components.MahoGroupedRow
import dev.maho.browser.ui.components.MahoGroupedSection
import dev.maho.browser.ui.components.MahoGroupedToggle
import dev.maho.browser.ui.components.MahoRowDivider
import dev.maho.browser.ui.components.MahoTextField
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private enum class SyncUiStatus {
    Synced,
    Syncing,
    Idle,
    Error,
    Offline
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SyncSettingsScreen(
    onBack: () -> Unit = {},
) {
    val shellColors = BrowserShellTheme.colors
    val clipboardManager = LocalClipboardManager.current
    val scope = rememberCoroutineScope()
    val activity = LocalContext.current as Activity
    remember(activity) { CredentialManagerGoogleSource(activity) }
    var relaySessionEmail by remember { mutableStateOf(RelaySessionStore.load()?.email) }
    var serverUrlInput by remember { mutableStateOf(SyncManager.getServerUrl()) }
    var emailInput by remember { mutableStateOf(relaySessionEmail ?: "") }
    var passwordInput by remember { mutableStateOf("") }
    var syncEnabled by remember { mutableStateOf(SyncManager.isSyncEnabled) }
    var syncStatus by remember { mutableStateOf(if (relaySessionEmail == null) SyncUiStatus.Offline else SyncUiStatus.Idle) }
    var syncStatusDetail by remember { mutableStateOf<String?>(null) }
    var activeProfileName by remember { mutableStateOf<String?>(null) }
    val devices = remember { mutableStateListOf<ConnectedDevice>() }
    var recoveryPhraseInput by remember { mutableStateOf("") }
    var generatedSyncKey by remember { mutableStateOf<String?>(null) }
    var joinResult by remember { mutableStateOf<String?>(null) }
    var syncError by remember { mutableStateOf<String?>(null) }
    var isSubmittingAuth by remember { mutableStateOf(false) }
    var deviceToRemove by remember { mutableStateOf<ConnectedDevice?>(null) }

    deviceToRemove?.let { device ->
        ConfirmDialog(
            title = "Remove Device?",
            message = "This will disconnect \"${device.name}\" from sync on this account.",
            confirmLabel = "Remove",
            onConfirm = {
                BridgeSync.removeSyncDevice(device.id)
                devices.remove(device)
                deviceToRemove = null
            },
            onDismiss = { deviceToRemove = null },
        )
    }

    fun refresh() {
        val session = RelaySessionStore.load()
        relaySessionEmail = session?.email
        if (session != null) {
            serverUrlInput = session.serverUrl ?: serverUrlInput
        }
        syncEnabled = SyncManager.isSyncEnabled

        val activeProfileId = BridgeProfiles.getActiveProfile()
        activeProfileName = activeProfileId?.let { id ->
            BridgeProfiles.getAllProfiles().firstOrNull { it.id == id }?.name ?: id
        }

        when (val stateResult = BridgeSync.getSyncState()) {
            is SyncStateResult.Available -> {
                val state = stateResult.state
                syncStatus = when (state.kind) {
                    SyncStateKind.CONNECTING, SyncStateKind.SYNCING -> SyncUiStatus.Syncing
                    SyncStateKind.SYNCED -> SyncUiStatus.Synced
                    SyncStateKind.ERROR -> SyncUiStatus.Error
                    SyncStateKind.OFFLINE -> SyncUiStatus.Offline
                    SyncStateKind.IDLE -> SyncUiStatus.Idle
                }
                syncStatusDetail = when {
                    !state.lastError.isNullOrBlank() -> state.lastError
                    state.pendingOutboxCount > 0 -> "${state.pendingOutboxCount} pending change${if (state.pendingOutboxCount == 1) "" else "s"} in this profile"
                    state.lastSuccessAt != null -> "Core sync state is up to date for the active profile"
                    else -> null
                }
            }
            is SyncStateResult.Invalid -> {
                syncStatus = SyncUiStatus.Error
                syncStatusDetail = stateResult.reason
            }
            SyncStateResult.Unavailable -> {
                syncStatus = when {
                    relaySessionEmail == null -> SyncUiStatus.Offline
                    syncEnabled -> SyncUiStatus.Syncing
                    else -> SyncUiStatus.Idle
                }
                syncStatusDetail = null
            }
        }

        val currentDevices = BridgeSync.getConnectedDevices()
        devices.clear()
        devices.addAll(currentDevices)
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
        containerColor = shellColors.overlayBackground,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "Sync",
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
            if (relaySessionEmail != null) {
                MahoGroupedSection(title = "Account") {
                    MahoGroupedRow(
                        title = relaySessionEmail ?: "",
                        subtitle = activeProfileName?.let { "Relay account connected · Active profile: $it" }
                            ?: "Relay account connected",
                        icon = painterResource(id = MahoIcon.Profiles.drawableRes),
                        iconTint = Color(0xFF0A84FF),
                    )
                    MahoRowDivider()
                    MahoGroupedRow(
                        title = "Sign Out",
                        icon = painterResource(id = MahoIcon.Close.drawableRes),
                        iconTint = shellColors.error,
                        onClick = {
                            scope.launch {
                                withContext(Dispatchers.IO) {
                                    SyncManager.logout()
                                }
                                relaySessionEmail = null
                                emailInput = ""
                                passwordInput = ""
                                syncError = null
                                refresh()
                            }
                        },
                    )
                }

                MahoGroupedSection(title = "Synchronization") {
                    MahoGroupedToggle(
                        title = "Sync Data",
                        subtitle = "Automatically sync tabs, spaces, and bookmarks for the active profile",
                        icon = painterResource(id = MahoIcon.SyncIcon.drawableRes),
                        iconTint = Color(0xFF30D158),
                        checked = syncEnabled,
                        onCheckedChange = { checked ->
                            scope.launch {
                                withContext(Dispatchers.IO) {
                                    if (checked) {
                                        SyncManager.startSync()
                                    } else {
                                        SyncManager.stopSync()
                                    }
                                }
                                refresh()
                            }
                        },
                    )
                    MahoRowDivider()
                    MahoGroupedRow(
                        title = activeProfileName?.let { "Status · $it" } ?: "Status",
                        subtitle = syncStatusDetail,
                        trailingContent = {
                            val (statusText, statusColor) = when (syncStatus) {
                                SyncUiStatus.Synced -> "Synced" to shellColors.success
                                SyncUiStatus.Syncing -> "Syncing…" to Color(0xFF0A84FF)
                                SyncUiStatus.Idle -> "Idle" to shellColors.textSecondary
                                SyncUiStatus.Error -> "Error" to shellColors.error
                                SyncUiStatus.Offline -> "Offline" to shellColors.textTertiary
                            }
                            Surface(
                                shape = RoundedCornerShape(6.dp),
                                color = statusColor.copy(alpha = 0.14f),
                                modifier = Modifier.padding(vertical = 2.dp),
                            ) {
                                Text(
                                    text = statusText,
                                    color = statusColor,
                                    fontSize = 13.sp,
                                    fontWeight = FontWeight.Medium,
                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp),
                                )
                            }
                        },
                    )
                    MahoRowDivider()
                    MahoGroupedRow(
                        title = "Sync Now",
                        subtitle = if (syncEnabled) "Push and pull this profile immediately" else "Enable Sync Data first",
                        icon = painterResource(id = MahoIcon.SyncIcon.drawableRes),
                        iconTint = Color(0xFF0A84FF),
                        onClick = {
                            if (syncEnabled) {
                                syncStatus = SyncUiStatus.Syncing
                                SyncManager.syncNow()
                            }
                        },
                    )
                }

                if (devices.isNotEmpty()) {
                    MahoGroupedSection(
                        title = "Connected Devices",
                        footer = "Devices sharing data through this Relay account.",
                    ) {
                        devices.forEachIndexed { index, device ->
                            if (index > 0) MahoRowDivider()
                            MahoGroupedRow(
                                title = device.name,
                                subtitle = if (device.isOnline == true) "Active now" else "Offline",
                                icon = painterResource(id = MahoIcon.Globe.drawableRes),
                                iconTint = if (device.isOnline == true) Color(0xFF30D158) else Color(0xFF8E8E93),
                                trailingContent = {
                                    IconButton(
                                        onClick = { deviceToRemove = device },
                                        modifier = Modifier.size(28.dp),
                                    ) {
                                        Icon(
                                            painter = painterResource(id = MahoIcon.Delete.drawableRes),
                                            contentDescription = "Remove Device",
                                            tint = shellColors.error.copy(alpha = 0.7f),
                                            modifier = Modifier.size(16.dp),
                                        )
                                    }
                                },
                            )
                        }
                    }
                }
            } else {
                MahoGroupedSection(
                    title = "Sign In",
                    footer = "Sign in to automatically sync your tabs, spaces, and bookmarks across all your devices without needing a Recovery Phrase.",
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        MahoTextField(
                            value = serverUrlInput,
                            onValueChange = {
                                serverUrlInput = it
                                SyncManager.updateServerUrl(it)
                            },
                            label = "Relay Server",
                            placeholder = "https://relay.mahobrowser.com",
                        )

                        MahoTextField(
                            value = emailInput,
                            onValueChange = { emailInput = it },
                            label = "Email Address",
                            placeholder = "name@example.com",
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email),
                        )

                        MahoTextField(
                            value = passwordInput,
                            onValueChange = { passwordInput = it },
                            label = "Password",
                            placeholder = "••••••••",
                            visualTransformation = PasswordVisualTransformation(),
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                        )

                        Spacer(modifier = Modifier.height(4.dp))

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                        ) {
                            Button(
                                onClick = {
                                    scope.launch {
                                        isSubmittingAuth = true
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
                                        } finally {
                                            isSubmittingAuth = false
                                        }
                                    }
                                },
                                enabled = !isSubmittingAuth && emailInput.isNotBlank() && passwordInput.isNotBlank(),
                                shape = RoundedCornerShape(12.dp),
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = Color(0xFF0A84FF),
                                    contentColor = Color.White,
                                ),
                                modifier = Modifier
                                    .weight(1f)
                                    .height(46.dp),
                            ) {
                                if (isSubmittingAuth) {
                                    CircularProgressIndicator(
                                        color = Color.White,
                                        strokeWidth = 2.dp,
                                        modifier = Modifier.size(18.dp),
                                    )
                                } else {
                                    Text("Log In", fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
                                }
                            }

                            OutlinedButton(
                                onClick = {
                                    scope.launch {
                                        isSubmittingAuth = true
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
                                        } finally {
                                            isSubmittingAuth = false
                                        }
                                    }
                                },
                                enabled = !isSubmittingAuth && emailInput.isNotBlank() && passwordInput.isNotBlank(),
                                shape = RoundedCornerShape(12.dp),
                                border = BorderStroke(1.dp, Color(0xFF0A84FF).copy(alpha = 0.5f)),
                                colors = ButtonDefaults.outlinedButtonColors(
                                    contentColor = Color(0xFF0A84FF),
                                ),
                                modifier = Modifier
                                    .weight(1f)
                                    .height(46.dp),
                            ) {
                                Text("Sign Up", fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
                            }
                        }
                    }
                }
            }

            if (syncError != null) {
                MahoGroupedSection {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(14.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        Icon(
                            painter = painterResource(id = MahoIcon.Close.drawableRes),
                            contentDescription = null,
                            tint = shellColors.error,
                            modifier = Modifier.size(18.dp),
                        )
                        Text(
                            text = syncError?.replace("\"", "") ?: "",
                            style = MaterialTheme.typography.bodyMedium.copy(fontSize = 13.5.sp),
                            color = shellColors.error,
                        )
                    }
                }
            }

            MahoGroupedSection(
                title = "Advanced Recovery",
                footer = "Recovery phrases are preserved for emergency manual backup and key export.",
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    OutlinedButton(
                        onClick = {
                            generatedSyncKey = BridgeSync.generateSyncKey()
                            if (generatedSyncKey != null) {
                                clipboardManager.setText(AnnotatedString(generatedSyncKey ?: ""))
                            }
                        },
                        shape = RoundedCornerShape(10.dp),
                        border = BorderStroke(0.5.dp, shellColors.cardBorder),
                        modifier = Modifier.fillMaxWidth().height(42.dp),
                    ) {
                        Text("Generate Recovery Phrase", color = shellColors.textPrimary, fontSize = 14.sp)
                    }

                    if (generatedSyncKey != null) {
                        Surface(
                            shape = RoundedCornerShape(8.dp),
                            color = shellColors.fieldBackground,
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text(
                                text = generatedSyncKey ?: "",
                                color = shellColors.accent,
                                fontSize = 13.sp,
                                modifier = Modifier.padding(12.dp),
                            )
                        }
                    }

                    MahoTextField(
                        value = recoveryPhraseInput,
                        onValueChange = { recoveryPhraseInput = it },
                        placeholder = "Enter phrase to restore…",
                        label = "Manual Recovery Phrase",
                    )

                    Button(
                        onClick = {
                            val res = BridgeSync.joinSync(recoveryPhraseInput)
                            joinResult = if (res != null) "Joined successfully" else "Failed to join"
                            refresh()
                        },
                        enabled = recoveryPhraseInput.isNotBlank(),
                        shape = RoundedCornerShape(10.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = shellColors.fieldBackground,
                            contentColor = shellColors.textPrimary,
                        ),
                        modifier = Modifier.fillMaxWidth().height(42.dp),
                    ) {
                        Text("Join with Phrase", fontSize = 14.sp)
                    }
                }
            }

            if (joinResult != null) {
                Text(
                    text = joinResult ?: "",
                    color = shellColors.textSecondary,
                    fontSize = 13.sp,
                    modifier = Modifier.padding(horizontal = 24.dp),
                )
            }

            Spacer(modifier = Modifier.height(32.dp))
        }
    }
}
