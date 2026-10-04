package dev.maho.browser.ui.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import dev.maho.browser.models.NotificationSettingsUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NotificationSettingsScreen(
    onBack: () -> Unit = {},
) {
    var enabled by remember { mutableStateOf(true) }
    var calendarNotifications by remember { mutableStateOf(true) }
    var updateNotifications by remember { mutableStateOf(true) }
    var soundEnabled by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        val n = settings.notifications
        enabled = n.enabled
        calendarNotifications = n.calendarNotifications
        updateNotifications = n.updateNotifications
        soundEnabled = n.soundEnabled
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Notifications") },
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
            SectionHeader("Notifications")
            SwitchPreference("Enable Notifications", enabled) {
                enabled = it
                BridgeSettings.updateNotificationSettings(NotificationSettingsUpdate(enabled = it))
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            if (enabled) {
                SectionHeader("Categories")
                SwitchPreference("Calendar Reminders", calendarNotifications) {
                    calendarNotifications = it
                    BridgeSettings.updateNotificationSettings(
                        NotificationSettingsUpdate(calendarNotifications = it)
                    )
                }
                SwitchPreference("Update Notifications", updateNotifications) {
                    updateNotifications = it
                    BridgeSettings.updateNotificationSettings(
                        NotificationSettingsUpdate(updateNotifications = it)
                    )
                }
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

                SectionHeader("Sound")
                SwitchPreference("Notification Sound", soundEnabled) {
                    soundEnabled = it
                    BridgeSettings.updateNotificationSettings(
                        NotificationSettingsUpdate(soundEnabled = it)
                    )
                }
                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
            }

            TextButton(
                onClick = { MahoBridge.sendEvent(ShellEvent.DismissAllNotifications) },
                modifier = Modifier.padding(horizontal = 16.dp),
            ) { Text("Dismiss All Notifications") }
        }
    }
}
