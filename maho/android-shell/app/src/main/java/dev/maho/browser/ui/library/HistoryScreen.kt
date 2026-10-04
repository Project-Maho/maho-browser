@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.library

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeHistory
import dev.maho.browser.models.HistoryEntry
import dev.maho.browser.ui.components.ConfirmDialog
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HistoryScreen(
    onOpenUrl: (String) -> Unit = {},
    onBack: () -> Unit = {},
) {
    var entries by remember { mutableStateOf<List<HistoryEntry>>(emptyList()) }
    var searchQuery by remember { mutableStateOf("") }
    var isLoading by remember { mutableStateOf(true) }
    var showClearDialog by remember { mutableStateOf(false) }

    LaunchedEffect(searchQuery) {
        isLoading = true
        entries = BridgeHistory.searchHistoryEntries(query = searchQuery, limit = 500)
        isLoading = false
    }

    if (showClearDialog) {
        AlertDialog(
            onDismissRequest = { showClearDialog = false },
            title = { Text("Clear All History?") },
            text = { Text("This will permanently remove all browsing history.") },
            confirmButton = {
                TextButton(onClick = {
                    BridgeHistory.clearAllHistory()
                    entries = emptyList()
                    showClearDialog = false
                }) {
                    Text("Clear All")
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearDialog = false }) {
                    Text("Cancel")
                }
            },
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("History") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(painter = painterResource(id = MahoIcon.NavBack.drawableRes), contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(
                        onClick = { showClearDialog = true },
                        enabled = entries.isNotEmpty(),
                    ) {
                        Icon(painter = painterResource(id = MahoIcon.ClearSweep.drawableRes), contentDescription = "Clear all")
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
            TextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                placeholder = { Text("Search history") },
                singleLine = true,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 8.dp),
            )

            when {
                isLoading -> {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center,
                    ) {
                        CircularProgressIndicator()
                    }
                }
                entries.isEmpty() -> {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center,
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(painter = painterResource(id = MahoIcon.History.drawableRes),
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                text = if (searchQuery.isBlank()) "No History" else "No Results",
                                style = MaterialTheme.typography.titleMedium,
                                modifier = Modifier.padding(top = 8.dp),
                            )
                        }
                    }
                }
                else -> {
                    val grouped = groupByDate(entries)
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        grouped.forEach { (label, sectionEntries) ->
                            item(key = "header_$label") {
                                Text(
                                    text = label,
                                    style = MaterialTheme.typography.titleSmall,
                                    color = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.padding(
                                        horizontal = 16.dp,
                                        vertical = 8.dp,
                                    ),
                                )
                            }
                            items(
                                items = sectionEntries,
                                key = { it.id },
                            ) { entry ->
                                HistoryItem(
                                    entry = entry,
                                    onTap = { onOpenUrl(entry.url) },
                                    onDelete = {
                                        BridgeHistory.deleteHistoryEntryById(entry.id)
                                        entries = entries.filter { it.id != entry.id }
                                    },
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun HistoryItem(
    entry: HistoryEntry,
    onTap: () -> Unit,
    onDelete: () -> Unit,
) {
    var showConfirm by remember { mutableStateOf(false) }
    val dismissState = rememberSwipeToDismissBoxState(
        confirmValueChange = { value ->
            if (value == SwipeToDismissBoxValue.EndToStart) {
                showConfirm = true
            }
            false
        },
    )

    if (showConfirm) {
        ConfirmDialog(
            title = "Delete History Entry?",
            message = "This will remove this page from your browsing history.",
            confirmLabel = "Delete",
            onConfirm = {
                showConfirm = false
                onDelete()
            },
            onDismiss = { showConfirm = false },
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
                    text = entry.title.ifBlank { entry.url },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            },
            supportingContent = {
                Text(
                    text = entry.url,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
            },
            modifier = Modifier.clickable(onClick = onTap),
        )
    }
}

private fun groupByDate(entries: List<HistoryEntry>): List<Pair<String, List<HistoryEntry>>> {
    val today = LocalDate.now()
    val yesterday = today.minusDays(1)
    val weekAgo = today.minusDays(7)
    val monthAgo = today.minusDays(30)

    val groups = linkedMapOf<String, MutableList<HistoryEntry>>(
        "Today" to mutableListOf(),
        "Yesterday" to mutableListOf(),
        "Last 7 Days" to mutableListOf(),
        "Last 30 Days" to mutableListOf(),
        "Older" to mutableListOf(),
    )

    for (entry in entries) {
        val date = parseDate(entry.visitedAt)
        val label = when {
            date == today -> "Today"
            date == yesterday -> "Yesterday"
            date >= weekAgo -> "Last 7 Days"
            date >= monthAgo -> "Last 30 Days"
            else -> "Older"
        }
        groups[label]?.add(entry)
    }

    return groups.filter { it.value.isNotEmpty() }.map { it.key to it.value.toList() }
}

private fun parseDate(isoString: String): LocalDate {
    return try {
        Instant.parse(isoString).atZone(ZoneId.systemDefault()).toLocalDate()
    } catch (_: Exception) {
        LocalDate.MIN
    }
}
