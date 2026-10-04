@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.library

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeDownloads
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.DownloadState
import dev.maho.browser.models.DownloadViewModel
import dev.maho.browser.ui.components.ConfirmDialog
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DownloadsScreen(
    onBack: () -> Unit = {},
) {
    var downloads by remember { mutableStateOf<List<DownloadViewModel>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        isLoading = true
        downloads = BridgeDownloads.getDownloads()
        isLoading = false
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Downloads") },
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
                .fillMaxSize(),
        ) {
            when {
                isLoading -> {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center,
                    ) {
                        CircularProgressIndicator()
                    }
                }
                downloads.isEmpty() -> {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center,
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(painter = painterResource(id = MahoIcon.Downloads.drawableRes),
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                text = "No Downloads",
                                style = MaterialTheme.typography.titleMedium,
                                modifier = Modifier.padding(top = 8.dp),
                            )
                        }
                    }
                }
                else -> {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(
                            items = downloads,
                            key = { it.id },
                        ) { download ->
                            DownloadItem(
                                download = download,
                                onPause = {
                                    BridgeDownloads.pauseDownload(download.id)
                                },
                                onResume = {
                                    BridgeDownloads.resumeDownload(download.id)
                                },
                                onCancel = {
                                    BridgeDownloads.cancelDownload(download.id)
                                },
                                onRemove = {
                                    BridgeDownloads.removeDownload(download.id)
                                    downloads = downloads.filter { it.id != download.id }
                                },
                                onProgressUpdate = { updated ->
                                    downloads = downloads.map {
                                        if (it.id == updated.id) updated else it
                                    }
                                },
                            )
                        }
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DownloadItem(
    download: DownloadViewModel,
    onPause: () -> Unit,
    onResume: () -> Unit,
    onCancel: () -> Unit,
    onRemove: () -> Unit,
    onProgressUpdate: (DownloadViewModel) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    var showRemoveConfirm by remember { mutableStateOf(false) }
    var showCancelConfirm by remember { mutableStateOf(false) }
    val dismissState = rememberSwipeToDismissBoxState(
        confirmValueChange = { value ->
            if (value == SwipeToDismissBoxValue.EndToStart) {
                showRemoveConfirm = true
            }
            false
        },
    )

    val progress = if (download.totalBytes > 0) {
        download.receivedBytes.toFloat() / download.totalBytes.toFloat()
    } else {
        0f
    }

    if (showRemoveConfirm) {
        ConfirmDialog(
            title = "Remove Download?",
            message = "This removes \"${download.filename}\" from your downloads list.",
            confirmLabel = "Remove",
            onConfirm = {
                showRemoveConfirm = false
                onRemove()
            },
            onDismiss = { showRemoveConfirm = false },
        )
    }

    if (showCancelConfirm) {
        ConfirmDialog(
            title = "Cancel Download?",
            message = "This stops and cancels \"${download.filename}\".",
            confirmLabel = "Cancel Download",
            onConfirm = {
                showCancelConfirm = false
                onCancel()
            },
            onDismiss = { showCancelConfirm = false },
            dismissLabel = "Keep",
        )
    }

    SwipeToDismissBox(
        state = dismissState,
        backgroundContent = {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(MaterialTheme.colorScheme.error)
                    .padding(horizontal = 16.dp),
                contentAlignment = Alignment.CenterEnd,
            ) {
                Icon(painter = painterResource(id = MahoIcon.Delete.drawableRes),
                    contentDescription = "Delete",
                    tint = MaterialTheme.colorScheme.onError,
                )
            }
        },
        enableDismissFromStartToEnd = false,
    ) {
        ListItem(
            headlineContent = {
                Text(
                    text = download.filename,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            },
            supportingContent = {
                Column {
                    if (download.state == DownloadState.Downloading || download.state == DownloadState.Paused) {
                        LinearProgressIndicator(
                            progress = { progress.coerceIn(0f, 1f) },
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 4.dp),
                        )
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = formatBytes(download.receivedBytes) + " / " + formatBytes(download.totalBytes),
                            style = MaterialTheme.typography.bodySmall,
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = stateLabel(download.state),
                            style = MaterialTheme.typography.labelSmall,
                            color = stateColor(download.state),
                        )
                    }
                }
            },
            trailingContent = {
                Box {
                    IconButton(onClick = { expanded = true }) {
                        Icon(painter = painterResource(id = MahoIcon.MoreActions.drawableRes), contentDescription = "Actions")
                    }
                    DropdownMenu(
                        expanded = expanded,
                        onDismissRequest = { expanded = false },
                    ) {
                        when (download.state) {
                            DownloadState.Downloading -> {
                                DropdownMenuItem(
                                    text = { Text("Pause") },
                                    leadingIcon = {
                                        Icon(painter = painterResource(id = MahoIcon.Pause.drawableRes), contentDescription = null)
                                    },
                                    onClick = {
                                        expanded = false
                                        onPause()
                                    },
                                )
                                DropdownMenuItem(
                                    text = { Text("Cancel") },
                                    leadingIcon = {
                                        Icon(painter = painterResource(id = MahoIcon.Delete.drawableRes), contentDescription = null)
                                    },
                                    onClick = {
                                        expanded = false
                                        showCancelConfirm = true
                                    },
                                )
                            }
                            DownloadState.Paused -> {
                                DropdownMenuItem(
                                    text = { Text("Resume") },
                                    leadingIcon = {
                                        Icon(painter = painterResource(id = MahoIcon.Play.drawableRes), contentDescription = null)
                                    },
                                    onClick = {
                                        expanded = false
                                        onResume()
                                    },
                                )
                                DropdownMenuItem(
                                    text = { Text("Cancel") },
                                    leadingIcon = {
                                        Icon(painter = painterResource(id = MahoIcon.Delete.drawableRes), contentDescription = null)
                                    },
                                    onClick = {
                                        expanded = false
                                        showCancelConfirm = true
                                    },
                                )
                            }
                            else -> {
                                DropdownMenuItem(
                                    text = { Text("Remove") },
                                    leadingIcon = {
                                        Icon(painter = painterResource(id = MahoIcon.Delete.drawableRes), contentDescription = null)
                                    },
                                    onClick = {
                                        expanded = false
                                        showRemoveConfirm = true
                                    },
                                )
                            }
                        }
                    }
                }
            },
        )
    }
}

private fun stateLabel(state: DownloadState): String = when (state) {
    DownloadState.Downloading -> "Downloading"
    DownloadState.Paused -> "Paused"
    DownloadState.Completed -> "Completed"
    DownloadState.Failed -> "Failed"
    DownloadState.Cancelled -> "Cancelled"
}

@Composable
private fun stateColor(state: DownloadState): Color {
    val colors = BrowserShellTheme.colors
    return when (state) {
        DownloadState.Downloading -> colors.accent
        DownloadState.Paused -> colors.warning
        DownloadState.Completed -> colors.success
        DownloadState.Failed -> colors.error
        DownloadState.Cancelled -> colors.textSecondary
    }
}

private fun formatBytes(bytes: Long): String {
    if (bytes < 1024) return "$bytes B"
    val kb = bytes / 1024.0
    if (kb < 1024) return "%.1f KB".format(kb)
    val mb = kb / 1024.0
    if (mb < 1024) return "%.1f MB".format(mb)
    val gb = mb / 1024.0
    return "%.1f GB".format(gb)
}

fun isDownloadUpdate(update: CoreUpdate): Boolean = when (update) {
    is CoreUpdate.DownloadStarted -> true
    is CoreUpdate.DownloadProgress -> true
    is CoreUpdate.DownloadCompleted -> true
    else -> false
}
