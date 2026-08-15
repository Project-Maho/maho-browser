@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.library

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
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
import dev.maho.browser.bridge.BridgeNotes
import dev.maho.browser.models.NoteViewModel
import dev.maho.browser.ui.components.ConfirmDialog
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NotesScreen(
    onBack: () -> Unit = {},
) {
    var notes by remember { mutableStateOf<List<NoteViewModel>>(emptyList()) }
    var searchQuery by remember { mutableStateOf("") }
    var isLoading by remember { mutableStateOf(true) }
    var showCreateSheet by remember { mutableStateOf(false) }
    var editingNote by remember { mutableStateOf<NoteViewModel?>(null) }

    fun reload() {
        notes = if (searchQuery.isBlank()) {
            BridgeNotes.getAllNoteViewModels()
        } else {
            BridgeNotes.searchNotesFullText(searchQuery)
        }
    }

    LaunchedEffect(searchQuery) {
        isLoading = true
        reload()
        isLoading = false
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Notes") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(painter = painterResource(id = MahoIcon.NavBack.drawableRes), contentDescription = "Back")
                    }
                },
                actions = {
                    IconButton(
                        onClick = { BridgeNotes.exportAllNotes() },
                        enabled = notes.isNotEmpty(),
                    ) {
                        Icon(painter = painterResource(id = MahoIcon.Notes.drawableRes), contentDescription = "Export")
                    }
                },
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { showCreateSheet = true }) {
                Icon(painter = painterResource(id = MahoIcon.Notes.drawableRes), contentDescription = "New Note")
            }
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
                placeholder = { Text("Search notes") },
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
                notes.isEmpty() -> {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center,
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(painter = painterResource(id = MahoIcon.Notes.drawableRes),
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.size(48.dp),
                            )
                            Text(
                                text = if (searchQuery.isBlank()) "No Notes" else "No Results",
                                style = MaterialTheme.typography.titleMedium,
                                modifier = Modifier.padding(top = 8.dp),
                            )
                            Text(
                                text = if (searchQuery.isBlank())
                                    "Notes you create will appear here."
                                else
                                    "Try a different search term.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
                else -> {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(items = notes, key = { it.id }) { note ->
                            NoteItem(
                                note = note,
                                onTap = { editingNote = note },
                                onDelete = {
                                    BridgeNotes.deleteExistingNote(note.id)
                                    notes = notes.filter { it.id != note.id }
                                },
                            )
                        }
                    }
                }
            }
        }
    }

    if (showCreateSheet) {
        NoteEditorSheet(
            onDismiss = { showCreateSheet = false },
            onSave = { content ->
                BridgeNotes.createNewNote(content)
                showCreateSheet = false
                reload()
            },
        )
    }

    editingNote?.let { note ->
        NoteEditorSheet(
            initialContent = note.content,
            onDismiss = { editingNote = null },
            onSave = { content ->
                BridgeNotes.updateExistingNote(note.id, content)
                editingNote = null
                reload()
            },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun NoteItem(
    note: NoteViewModel,
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
            title = "Delete Note?",
            message = "This will permanently delete this note.",
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
                Icon(painter = painterResource(id = MahoIcon.Delete.drawableRes), contentDescription = "Delete", tint = MaterialTheme.colorScheme.onError)
            }
        },
        enableDismissFromStartToEnd = false,
    ) {
        ListItem(
            headlineContent = {
                Text(
                    text = note.content.replace("\n", " ").take(100),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            },
            supportingContent = if (note.linkedUrl != null) {
                {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(painter = painterResource(id = MahoIcon.Link.drawableRes),
                            contentDescription = null,
                            modifier = Modifier.size(14.dp),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Text(
                            text = note.linkedUrl,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            style = MaterialTheme.typography.bodySmall,
                            modifier = Modifier.padding(start = 4.dp),
                        )
                    }
                }
            } else {
                null
            },
            modifier = Modifier.clickable(onClick = onTap),
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun NoteEditorSheet(
    initialContent: String = "",
    onDismiss: () -> Unit,
    onSave: (content: String) -> Unit,
) {
    var content by remember { mutableStateOf(initialContent) }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 16.dp),
        ) {
            Text(
                text = if (initialContent.isEmpty()) "New Note" else "Edit Note",
                style = MaterialTheme.typography.titleLarge,
                modifier = Modifier.padding(bottom = 16.dp),
            )
            OutlinedTextField(
                value = content,
                onValueChange = { content = it },
                label = { Text("Content") },
                minLines = 5,
                maxLines = 10,
                modifier = Modifier.fillMaxWidth(),
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 16.dp, bottom = 24.dp),
            ) {
                Spacer(modifier = Modifier.weight(1f))
                TextButton(onClick = onDismiss) {
                    Text("Cancel")
                }
                TextButton(
                    onClick = { onSave(content) },
                    enabled = content.isNotBlank(),
                ) {
                    Text("Save")
                }
            }
        }
    }
}
