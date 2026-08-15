@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.library

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.models.ReadingListItem
import dev.maho.browser.bridge.BridgeReadingList
import dev.maho.browser.ui.components.ConfirmDialog
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

private enum class FilterMode { All, Unread, Read }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ReadingListScreen(
    onOpenUrl: (String) -> Unit = {},
    onBack: () -> Unit = {},
) {
    var items by remember { mutableStateOf<List<ReadingListItem>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }
    var filterMode by remember { mutableStateOf(FilterMode.All) }

    val filteredItems = when (filterMode) {
        FilterMode.All -> items
        FilterMode.Unread -> items.filter { !it.isRead }
        FilterMode.Read -> items.filter { it.isRead }
    }

    val unreadCount = items.count { !it.isRead }

    fun reload() {
        items = BridgeReadingList.getReadingListItems()
    }

    LaunchedEffect(Unit) {
        isLoading = true
        reload()
        isLoading = false
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Reading List") },
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
            Row(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
            ) {
                FilterChip(
                    selected = filterMode == FilterMode.All,
                    onClick = { filterMode = FilterMode.All },
                    label = { Text("All") },
                )
                Spacer(modifier = Modifier.width(8.dp))
                FilterChip(
                    selected = filterMode == FilterMode.Unread,
                    onClick = { filterMode = FilterMode.Unread },
                    label = { Text("Unread ($unreadCount)") },
                )
                Spacer(modifier = Modifier.width(8.dp))
                FilterChip(
                    selected = filterMode == FilterMode.Read,
                    onClick = { filterMode = FilterMode.Read },
                    label = { Text("Read") },
                )
            }

            when {
                isLoading -> {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center,
                    ) {
                        CircularProgressIndicator()
                    }
                }
                filteredItems.isEmpty() -> {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center,
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(painter = painterResource(id = MahoIcon.Eye.drawableRes),
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.size(48.dp),
                            )
                            Text(
                                text = when (filterMode) {
                                    FilterMode.All -> "Reading List Empty"
                                    FilterMode.Unread -> "No Unread Items"
                                    FilterMode.Read -> "No Read Items"
                                },
                                style = MaterialTheme.typography.titleMedium,
                                modifier = Modifier.padding(top = 8.dp),
                            )
                            Text(
                                text = if (filterMode == FilterMode.All)
                                    "Add pages to read later."
                                else
                                    "No items match this filter.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
                else -> {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(items = filteredItems, key = { it.id }) { item ->
                            ReadingListItemRow(
                                item = item,
                                onTap = { onOpenUrl(item.url) },
                                onToggleRead = {
                                    BridgeReadingList.toggleReadingListItemRead(item.id)
                                    reload()
                                },
                                onDelete = {
                                    BridgeReadingList.removeReadingListItem(item.id)
                                    items = items.filter { it.id != item.id }
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
private fun ReadingListItemRow(
    item: ReadingListItem,
    onTap: () -> Unit,
    onToggleRead: () -> Unit,
    onDelete: () -> Unit,
) {
    var showConfirm by remember { mutableStateOf(false) }
    val dismissState = rememberSwipeToDismissBoxState(
        confirmValueChange = { value ->
            if (value == SwipeToDismissBoxValue.EndToStart) {
                showConfirm = true
                false
            } else if (value == SwipeToDismissBoxValue.StartToEnd) {
                onToggleRead()
                true
            } else {
                false
            }
        },
    )

    if (showConfirm) {
        ConfirmDialog(
            title = "Remove from Reading List?",
            message = "This will remove \"${item.title.ifBlank { item.url }}\" from your reading list.",
            confirmLabel = "Remove",
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
            val isDeleting = dismissState.targetValue == SwipeToDismissBoxValue.EndToStart
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(
                        if (isDeleting) MaterialTheme.colorScheme.error else Color.Transparent,
                    )
                    .padding(horizontal = 16.dp),
                contentAlignment = Alignment.CenterEnd,
            ) {
                if (isDeleting) {
                    Icon(painter = painterResource(id = MahoIcon.Delete.drawableRes), contentDescription = "Delete", tint = MaterialTheme.colorScheme.onError)
                }
            }
        },
    ) {
        ListItem(
            headlineContent = {
                Text(
                    text = item.title.ifBlank { item.url },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    color = if (item.isRead)
                        MaterialTheme.colorScheme.onSurfaceVariant
                    else
                        MaterialTheme.colorScheme.onSurface,
                )
            },
            supportingContent = {
                Text(
                    text = item.url,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
            },
            leadingContent = {
                Box(
                    modifier = Modifier
                        .size(8.dp)
                        .clip(CircleShape)
                        .background(
                            if (item.isRead) Color.Transparent
                            else MaterialTheme.colorScheme.primary,
                        ),
                )
            },
            modifier = Modifier.clickable(onClick = onTap),
        )
    }
}
