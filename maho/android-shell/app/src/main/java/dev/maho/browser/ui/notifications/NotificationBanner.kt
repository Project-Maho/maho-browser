package dev.maho.browser.ui.notifications

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import androidx.compose.ui.res.painterResource

data class BannerAction(
    val id: String,
    val label: String,
    val onClick: () -> Unit,
)

@Composable
fun NotificationBanner(
    title: String,
    message: String,
    icon: MahoIcon,
    actions: List<BannerAction> = emptyList(),
    onDismiss: () -> Unit,
    isIncognito: Boolean = false,
) {
    val shellColors = BrowserShellTheme.colors
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
        colors = if (isIncognito) {
            CardDefaults.cardColors(containerColor = shellColors.incognitoSurface)
        } else {
            CardDefaults.cardColors()
        },
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                painter = painterResource(id = icon.drawableRes),
                contentDescription = null,
                tint = if (isIncognito) shellColors.incognitoAccent else MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(32.dp),
            )
            Spacer(modifier = Modifier.width(12.dp))
            Column(
                modifier = Modifier.weight(1f),
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleSmall,
                    maxLines = 1,
                )
                Text(
                    text = message,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2,
                )
            }
            if (actions.isEmpty()) {
                IconButton(onClick = onDismiss) {
                    Icon(painter = painterResource(id = MahoIcon.Close.drawableRes),
                        contentDescription = "Dismiss",
                        modifier = Modifier.size(18.dp),
                    )
                }
            } else {
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    actions.forEach { action ->
                        Button(onClick = action.onClick) {
                            Text(action.label)
                        }
                    }
                }
            }
        }
    }
}
