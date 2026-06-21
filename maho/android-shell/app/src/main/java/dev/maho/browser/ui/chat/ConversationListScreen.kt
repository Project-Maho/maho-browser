@file:OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.chat

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.ConversationSession
import dev.maho.browser.MahoBridge
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
fun ConversationListScreen(
    onConversationSelected: (String) -> Unit,
    onNewConversation: () -> Unit,
    onBack: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val shellMetrics = BrowserShellTheme.metrics
    val scope = rememberCoroutineScope()
    var conversations by remember { mutableStateOf<List<ConversationSession>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var renameTarget by remember { mutableStateOf<ConversationSession?>(null) }
    var renameInput by remember { mutableStateOf("") }
    var deleteTarget by remember { mutableStateOf<ConversationSession?>(null) }
    var refreshNonce by remember { mutableStateOf(0) }

    suspend fun refreshConversations() {
        isLoading = true
        errorMessage = null
        conversations = withContext(Dispatchers.IO) {
            MahoBridge.listConversations()
        }
        isLoading = false
    }

    LaunchedEffect(refreshNonce) {
        refreshConversations()
    }

    if (renameTarget != null) {
        AlertDialog(
            onDismissRequest = { renameTarget = null },
            title = { Text("Rename chat") },
            text = {
                OutlinedTextField(
                    value = renameInput,
                    onValueChange = { renameInput = it },
                    label = { Text("Chat title") },
                    placeholder = { Text("Untitled chat") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        val target = renameTarget ?: return@TextButton
                        val nextTitle = renameInput.trim()
                        scope.launch {
                            val renamed = withContext(Dispatchers.IO) {
                                MahoBridge.renameConversation(target.id, nextTitle)
                            }
                            if (renamed) {
                                renameTarget = null
                                refreshNonce += 1
                            } else {
                                errorMessage = "Unable to rename this chat right now."
                            }
                        }
                    },
                ) {
                    Text("Save")
                }
            },
            dismissButton = {
                TextButton(onClick = { renameTarget = null }) {
                    Text("Cancel")
                }
            },
        )
    }

    if (deleteTarget != null) {
        val target = deleteTarget!!
        AlertDialog(
            onDismissRequest = { deleteTarget = null },
            title = { Text("Delete chat?") },
            text = {
                Text(
                    "${target.title?.takeIf { it.isNotBlank() } ?: "Untitled chat"} and its saved messages will be removed.",
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        scope.launch {
                            val deleted = withContext(Dispatchers.IO) {
                                MahoBridge.deleteConversation(target.id)
                            }
                            if (deleted) {
                                deleteTarget = null
                                refreshNonce += 1
                            } else {
                                errorMessage = "Unable to delete this chat right now."
                            }
                        }
                    },
                ) {
                    Text("Delete", color = MaterialTheme.colorScheme.error)
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteTarget = null }) {
                    Text("Cancel")
                }
            },
        )
    }

    Surface(
        modifier = Modifier
            .testTag("aiScreenConversationList")
            .fillMaxWidth()
            .fillMaxHeight(0.84f)
            .padding(horizontal = 12.dp)
            .navigationBarsPadding(),
        shape = RoundedCornerShape(shellMetrics.panelCorner),
        color = shellColors.overlaySurfaceHigh.copy(alpha = 0.98f),
        tonalElevation = 0.dp,
        shadowElevation = shellMetrics.floatingShadow,
    ) {
        Scaffold(
            containerColor = shellColors.overlaySurfaceHigh.copy(alpha = 0.98f),
            topBar = {
                TopAppBar(
                    title = { Text("Saved chats") },
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = shellColors.overlaySurfaceHigh.copy(alpha = 0.98f),
                        titleContentColor = shellColors.textPrimary,
                        navigationIconContentColor = shellColors.textPrimary,
                        actionIconContentColor = shellColors.textPrimary,
                    ),
                    navigationIcon = {
                        IconButton(onClick = onBack) {
                            Icon(
                                painter = painterResource(id = MahoIcon.NavBack.drawableRes),
                                contentDescription = "Back",
                            )
                        }
                    },
                    actions = {
                        TextButton(onClick = onNewConversation) {
                            Icon(
                                painter = painterResource(id = MahoIcon.Add.drawableRes),
                                contentDescription = null,
                                modifier = Modifier.size(18.dp),
                            )
                            Text(
                                text = "New",
                                modifier = Modifier.padding(start = 6.dp),
                            )
                        }
                    },
                )
            },
        ) { padding ->
            when {
                isLoading -> {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(padding),
                        contentAlignment = Alignment.Center,
                    ) {
                        CircularProgressIndicator()
                    }
                }

                conversations.isEmpty() -> {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(padding),
                        contentAlignment = Alignment.Center,
                    ) {
                        Column(
                            modifier = Modifier.padding(horizontal = 28.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(12.dp),
                        ) {
                            Surface(
                                shape = CircleShape,
                                color = shellColors.accent.copy(alpha = 0.14f),
                            ) {
                                Icon(
                                    painter = painterResource(id = MahoIcon.HelpFeedback.drawableRes),
                                    contentDescription = null,
                                    tint = shellColors.accent,
                                    modifier = Modifier
                                        .padding(16.dp)
                                        .size(24.dp),
                                )
                            }
                            Text(
                                text = "No saved chats yet",
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold,
                                color = shellColors.textPrimary,
                            )
                            Text(
                                text = "Start a new chat and it will appear here for quick resume, rename, or deletion.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = shellColors.textSecondary,
                            )
                        }
                    }
                }

                else -> {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(padding),
                    ) {
                        errorMessage?.let { message ->
                            ListItem(
                                leadingContent = {
                                    Icon(
                                        painter = painterResource(id = MahoIcon.Warning.drawableRes),
                                        contentDescription = null,
                                        tint = MaterialTheme.colorScheme.error,
                                    )
                                },
                                headlineContent = { Text("Chat history error") },
                                supportingContent = { Text(message) },
                            )
                            HorizontalDivider(color = shellColors.divider.copy(alpha = 0.42f))
                        }

                        LazyColumn(
                            modifier = Modifier.fillMaxSize(),
                            contentPadding = PaddingValues(vertical = 8.dp),
                        ) {
                            items(
                                items = conversations,
                                key = { session -> session.id },
                            ) { session ->
                                ConversationRow(
                                    session = session,
                                    onTap = { onConversationSelected(session.id) },
                                    onRename = {
                                        renameTarget = session
                                        renameInput = session.title.orEmpty()
                                    },
                                    onDelete = { deleteTarget = session },
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ConversationRow(
    session: ConversationSession,
    onTap: () -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val shellMetrics = BrowserShellTheme.metrics
    var menuExpanded by remember(session.id) { mutableStateOf(false) }

    Box(modifier = Modifier.fillMaxWidth()) {
        ListItem(
            overlineContent = {
                Text(
                    text = formatConversationTimestamp(session.last_message_at),
                    color = shellColors.textSecondary,
                )
            },
            headlineContent = {
                Text(
                    text = session.title?.takeIf { it.isNotBlank() } ?: "Untitled chat",
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            },
            supportingContent = {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = messageCountLabel(session.message_count),
                        style = MaterialTheme.typography.bodySmall,
                        color = shellColors.textSecondary,
                    )
                    session.model?.takeIf { it.isNotBlank() }?.let { model ->
                        Text(
                            text = model,
                            style = MaterialTheme.typography.bodySmall,
                            color = shellColors.textSecondary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
            },
            leadingContent = {
                Surface(
                    shape = CircleShape,
                    color = shellColors.accent.copy(alpha = 0.14f),
                ) {
                    Icon(
                        painter = painterResource(id = MahoIcon.HelpFeedback.drawableRes),
                        contentDescription = null,
                        tint = shellColors.accent,
                        modifier = Modifier
                            .padding(10.dp)
                            .size(18.dp),
                    )
                }
            },
            trailingContent = {
                IconButton(onClick = { menuExpanded = true }) {
                    Icon(
                        painter = painterResource(id = MahoIcon.MoreActions.drawableRes),
                        contentDescription = "Chat actions",
                        tint = shellColors.textSecondary,
                    )
                }
            },
            modifier = Modifier
                .fillMaxWidth()
                .combinedClickable(
                    onClick = onTap,
                    onLongClick = { menuExpanded = true },
                )
                .padding(horizontal = shellMetrics.compactSpacing),
        )

        DropdownMenu(
            expanded = menuExpanded,
            onDismissRequest = { menuExpanded = false },
            modifier = Modifier.align(Alignment.TopEnd),
        ) {
            DropdownMenuItem(
                text = { Text("Rename") },
                onClick = {
                    menuExpanded = false
                    onRename()
                },
            )
            DropdownMenuItem(
                text = { Text("Delete") },
                onClick = {
                    menuExpanded = false
                    onDelete()
                },
            )
        }
    }
}

private fun messageCountLabel(count: Int): String {
    return "$count message${if (count == 1) "" else "s"}"
}

private fun formatConversationTimestamp(isoString: String): String {
    return runCatching {
        val zonedDateTime = Instant.parse(isoString).atZone(ZoneId.systemDefault())
        val localDate = zonedDateTime.toLocalDate()
        val today = LocalDate.now()
        when {
            localDate == today -> zonedDateTime.format(DateTimeFormatter.ofPattern("h:mm a"))
            localDate == today.minusDays(1) -> "Yesterday"
            localDate.year == today.year -> zonedDateTime.format(DateTimeFormatter.ofPattern("MMM d"))
            else -> zonedDateTime.format(DateTimeFormatter.ofPattern("MMM d, yyyy"))
        }
    }.getOrDefault(isoString)
}
