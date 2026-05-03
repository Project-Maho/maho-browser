@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.library

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Bookmark
import androidx.compose.material.icons.filled.Delete
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeBookmarks
import dev.maho.browser.models.BookmarkEntry

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BookmarksScreen(
    onOpenUrl: (String) -> Unit = {},
    onBack: () -> Unit = {},
) {
    var bookmarks by remember { mutableStateOf<List<BookmarkEntry>>(emptyList()) }
    var searchQuery by remember { mutableStateOf("") }
    var isLoading by remember { mutableStateOf(true) }
    var currentFolderId by remember { mutableStateOf<String?>(null) }
    var folderStack by remember { mutableStateOf<List<Pair<String, String?>>>(emptyList()) }

    val displayedBookmarks = bookmarks.filter { it.folderId == currentFolderId }

    LaunchedEffect(searchQuery) {
        isLoading = true
        bookmarks = if (searchQuery.isBlank()) {
            BridgeBookmarks.getBookmarkEntries()
        } else {
            BridgeBookmarks.searchBookmarkEntries(searchQuery)
        }
        isLoading = false
    }

    val title = folderStack.lastOrNull()?.first ?: "Bookmarks"

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(title) },
                navigationIcon = {
                    IconButton(onClick = {
                        if (folderStack.isNotEmpty()) {
                            val prev = folderStack.dropLast(1)
                            folderStack = prev
                            currentFolderId = prev.lastOrNull()?.second
                        } else {
                            onBack()
                        }
                    }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
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
                placeholder = { Text("Search bookmarks") },
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
                displayedBookmarks.isEmpty() -> {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center,
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(
                                Icons.Filled.Bookmark,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                text = if (searchQuery.isBlank()) "No Bookmarks" else "No Results",
                                style = MaterialTheme.typography.titleMedium,
                                modifier = Modifier.padding(top = 8.dp),
                            )
                        }
                    }
                }
                else -> {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(
                            items = displayedBookmarks,
                            key = { it.id },
                        ) { bookmark ->
                            BookmarkItem(
                                bookmark = bookmark,
                                onTap = { onOpenUrl(bookmark.url) },
                                onDelete = {
                                    BridgeBookmarks.removeBookmarkEntry(bookmark.id)
                                    bookmarks = bookmarks.filter { it.id != bookmark.id }
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
private fun BookmarkItem(
    bookmark: BookmarkEntry,
    onTap: () -> Unit,
    onDelete: () -> Unit,
) {
    val dismissState = rememberSwipeToDismissBoxState(
        confirmValueChange = { value ->
            if (value == SwipeToDismissBoxValue.EndToStart) {
                onDelete()
                true
            } else {
                false
            }
        },
    )

    SwipeToDismissBox(
        state = dismissState,
        backgroundContent = {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 16.dp),
                contentAlignment = Alignment.CenterEnd,
            ) {
                Icon(
                    Icons.Filled.Delete,
                    contentDescription = "Delete",
                    tint = Color.White,
                )
            }
        },
        enableDismissFromStartToEnd = false,
    ) {
        ListItem(
            headlineContent = {
                Text(
                    text = bookmark.title,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            },
            supportingContent = {
                Text(
                    text = bookmark.url,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
            },
            modifier = Modifier.clickable(onClick = onTap),
        )
    }
}
