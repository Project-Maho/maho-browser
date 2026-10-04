package dev.maho.browser.ui.chrome

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun NavigationButtons(
    canGoBack: Boolean,
    canGoForward: Boolean,
    isLoading: Boolean,
    onBack: () -> Unit,
    onForward: () -> Unit,
    onReload: () -> Unit,
    onStop: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = onBack, enabled = canGoBack) {
            Icon(
                painter = painterResource(id = MahoIcon.NavBack.drawableRes),
                contentDescription = "Back",
            )
        }
        IconButton(onClick = onForward, enabled = canGoForward) {
            Icon(
                painter = painterResource(id = MahoIcon.NavForward.drawableRes),
                contentDescription = "Forward",
            )
        }
        Spacer(modifier = Modifier.width(4.dp))
        if (isLoading) {
            IconButton(onClick = onStop) {
                Icon(
                    painter = painterResource(id = MahoIcon.Close.drawableRes),
                    contentDescription = "Stop",
                )
            }
        } else {
            IconButton(onClick = onReload) {
                Icon(
                    painter = painterResource(id = MahoIcon.ReloadSync.drawableRes),
                    contentDescription = "Reload",
                )
            }
        }
    }
}
