package dev.maho.browser.ui.settings

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import dev.maho.browser.ConversationSession
import dev.maho.browser.bridge.BridgeConversations
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private const val PrivacyDashboardPrefs = "maho_ai_privacy_dashboard"
private const val SendConversationHistoryKey = "send_conversation_history"
private const val AllowTelemetryKey = "allow_telemetry"

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PrivacyDashboardScreen(
    onBack: () -> Unit = {},
) {
    val shellColors = BrowserShellTheme.colors
    val context = LocalContext.current
    val prefs = remember(context) {
        context.getSharedPreferences(PrivacyDashboardPrefs, Context.MODE_PRIVATE)
    }
    val scope = rememberCoroutineScope()

    var sendConversationHistory by rememberSaveable {
        mutableStateOf(prefs.getBoolean(SendConversationHistoryKey, true))
    }
    var allowTelemetry by rememberSaveable {
        mutableStateOf(prefs.getBoolean(AllowTelemetryKey, false))
    }
    var isClearingConversations by remember { mutableStateOf(false) }
    var statusMessage by rememberSaveable { mutableStateOf<String?>(null) }

    val cameraAccessGranted = ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.CAMERA,
    ) == PackageManager.PERMISSION_GRANTED
    val microphoneAccessGranted = ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.RECORD_AUDIO,
    ) == PackageManager.PERMISSION_GRANTED

    fun persistBoolean(key: String, value: Boolean) {
        prefs.edit().putBoolean(key, value).apply()
    }

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .testTag("aiScreenPrivacyDashboard"),
        containerColor = shellColors.overlayBackground,
        topBar = {
            TopAppBar(
                title = { Text("Privacy Dashboard") },
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
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(vertical = 8.dp),
        ) {
            item {
                SectionHeader("AI Privacy")
            }
            item {
                PrivacySwitchRow(
                    title = "Send conversation history to providers",
                    supportingText = "Keep provider prompts context-aware across the current chat session.",
                    checked = sendConversationHistory,
                    onCheckedChange = { enabled ->
                        sendConversationHistory = enabled
                        persistBoolean(SendConversationHistoryKey, enabled)
                    },
                )
            }
            item {
                PrivacySwitchRow(
                    title = "Allow telemetry",
                    supportingText = "Share aggregate AI feature health signals from this device.",
                    checked = allowTelemetry,
                    onCheckedChange = { enabled ->
                        allowTelemetry = enabled
                        persistBoolean(AllowTelemetryKey, enabled)
                    },
                )
            }
            item {
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            }
            item {
                SectionHeader("Data")
            }
            item {
                Column(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Button(
                        onClick = {
                            scope.launch {
                                isClearingConversations = true
                                statusMessage = null
                                val conversations = withContext(Dispatchers.IO) {
                                    BridgeConversations.listConversations(limit = 1_000L)
                                }
                                val cleared = withContext(Dispatchers.IO) {
                                    conversations.all { session: ConversationSession ->
                                        BridgeConversations.deleteConversation(session.id)
                                    }
                                }
                                statusMessage = when {
                                    !cleared -> "Unable to clear every saved conversation right now."
                                    conversations.isEmpty() -> "No saved conversations to clear."
                                    else -> "Cleared ${conversations.size} saved conversation(s)."
                                }
                                isClearingConversations = false
                            }
                        },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = !isClearingConversations,
                    ) {
                        if (isClearingConversations) {
                            CircularProgressIndicator(
                                modifier = Modifier.padding(end = 10.dp),
                                strokeWidth = 2.dp,
                            )
                        }
                        Text("Clear all conversations")
                    }

                    OutlinedButton(
                        onClick = {
                            statusMessage = "AI response cache clearing is not available in this build yet."
                        },
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text("Clear cached AI responses")
                    }
                }
            }
            statusMessage?.let { message ->
                item {
                    ListItem(
                        headlineContent = { Text("Status") },
                        supportingContent = { Text(message) },
                    )
                }
            }
            item {
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            }
            item {
                SectionHeader("Permissions")
            }
            item {
                PermissionStatusRow(
                    icon = MahoIcon.Camera,
                    title = "Camera access",
                    granted = cameraAccessGranted,
                )
            }
            item {
                PermissionStatusRow(
                    icon = MahoIcon.Mic,
                    title = "Microphone access",
                    granted = microphoneAccessGranted,
                )
            }
        }
    }
}

@Composable
private fun PrivacySwitchRow(
    title: String,
    supportingText: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = { Text(supportingText) },
        trailingContent = {
            Switch(
                checked = checked,
                onCheckedChange = onCheckedChange,
            )
        },
    )
}

@Composable
private fun PermissionStatusRow(
    icon: MahoIcon,
    title: String,
    granted: Boolean,
) {
    val shellColors = BrowserShellTheme.colors

    ListItem(
        headlineContent = { Text(title) },
        supportingContent = {
            Text(
                if (granted) {
                    "Granted on this device"
                } else {
                    "Not granted"
                },
            )
        },
        leadingContent = {
            Icon(
                painter = painterResource(id = icon.drawableRes),
                contentDescription = null,
                tint = if (granted) shellColors.accent else shellColors.textSecondary,
            )
        },
    )
}
