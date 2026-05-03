package dev.maho.browser.ui.chrome

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

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
                imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = "Back",
            )
        }
        IconButton(onClick = onForward, enabled = canGoForward) {
            Icon(
                imageVector = Icons.AutoMirrored.Filled.ArrowForward,
                contentDescription = "Forward",
            )
        }
        Spacer(modifier = Modifier.width(4.dp))
        if (isLoading) {
            IconButton(onClick = onStop) {
                Icon(
                    imageVector = Icons.Default.Close,
                    contentDescription = "Stop",
                )
            }
        } else {
            IconButton(onClick = onReload) {
                Icon(
                    imageVector = Icons.Default.Refresh,
                    contentDescription = "Reload",
                )
            }
        }
    }
}
