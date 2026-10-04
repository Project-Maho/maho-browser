package dev.maho.browser.ui.permissions

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Button
import androidx.compose.material3.BottomSheetDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PermissionSheet(
    permissionType: String,
    origin: String,
    onGrant: () -> Unit,
    onDeny: () -> Unit,
    onDismiss: () -> Unit,
    isIncognito: Boolean = false,
) {
    val shellColors = BrowserShellTheme.colors
    val sheetState = rememberModalBottomSheetState()
    val icon = when (permissionType) {
        "camera" -> MahoIcon.Camera
        "microphone" -> MahoIcon.Mic
        "location" -> MahoIcon.MapPin
        "notifications" -> MahoIcon.Notifications
        else -> MahoIcon.Privacy
    }
    val description = when (permissionType) {
        "camera" -> "This site wants to use your camera. You can change this later in Privacy settings."
        "microphone" -> "This site wants to use your microphone. You can change this later in Privacy settings."
        "location" -> "This site wants to know your location. You can change this later in Privacy settings."
        "notifications" -> "This site wants to send you notifications. You can change this later in Notification settings."
        else -> "This site is requesting a permission. You can change this later in settings."
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = if (isIncognito) shellColors.incognitoSurface else BottomSheetDefaults.ContainerColor,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(
                painter = painterResource(id = icon.drawableRes),
                contentDescription = permissionType,
                tint = if (isIncognito) shellColors.incognitoAccent else MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(48.dp),
            )
            Spacer(modifier = Modifier.height(16.dp))
            Text(
                text = "\"$origin\" wants to access your $permissionType",
                style = MaterialTheme.typography.titleMedium,
                textAlign = TextAlign.Center,
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = description,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
            Spacer(modifier = Modifier.height(24.dp))
            Row(
                horizontalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                OutlinedButton(onClick = {
                    onDeny()
                    onDismiss()
                }) { Text("Deny") }
                Button(onClick = {
                    onGrant()
                    onDismiss()
                }) { Text("Allow") }
            }
            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}
