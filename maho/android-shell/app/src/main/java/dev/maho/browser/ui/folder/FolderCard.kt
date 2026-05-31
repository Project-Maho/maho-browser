package dev.maho.browser.ui.folder

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import dev.maho.browser.models.FolderViewModel
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun FolderCard(
    folder: FolderViewModel,
    onTap: () -> Unit,
    onRename: (String) -> Unit,
    onDelete: () -> Unit,
    onDragHandleStart: () -> Unit,
    onDragHandleDrag: (Offset) -> Unit,
    onDragHandleEnd: () -> Unit,
    onDragHandleCancel: () -> Unit,
    isDragging: Boolean,
    modifier: Modifier = Modifier,
) {
    var showMenu by remember { mutableStateOf(false) }

    ListItem(
        headlineContent = {
            Text(
                text = folder.name,
                style = MaterialTheme.typography.bodyLarge,
            )
        },
        supportingContent = {
            Text(
                text = "${folder.tabCount} tab${if (folder.tabCount != 1) "s" else ""}",
                style = MaterialTheme.typography.bodySmall,
            )
        },
        leadingContent = {
            Icon(painter = painterResource(id = MahoIcon.Folder.drawableRes),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
            )
        },
        trailingContent = {
            Box(
                modifier = Modifier
                    .padding(end = 8.dp)
                    .size(32.dp)
                    .pointerInput(folder.id) {
                        detectDragGesturesAfterLongPress(
                            onDragStart = { onDragHandleStart() },
                            onDragEnd = { onDragHandleEnd() },
                            onDragCancel = { onDragHandleCancel() },
                            onDrag = { _, dragAmount -> onDragHandleDrag(dragAmount) },
                        )
                    },
                contentAlignment = Alignment.Center,
            ) {
                Icon(painter = painterResource(id = MahoIcon.DragHandle.drawableRes),
                    contentDescription = "Drag folder to reorder",
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        },
        modifier = modifier
            .graphicsLayer {
                scaleX = if (isDragging) 1.02f else 1f
                scaleY = if (isDragging) 1.02f else 1f
                shadowElevation = if (isDragging) 12f else 0f
                alpha = if (isDragging) 0.96f else 1f
            }
            .combinedClickable(
                onClick = onTap,
                onLongClick = { showMenu = true },
            ),
    )

    DropdownMenu(
        expanded = showMenu,
        onDismissRequest = { showMenu = false },
    ) {
        DropdownMenuItem(
            text = { Text("Rename") },
            onClick = {
                showMenu = false
                onRename("${folder.name} (renamed)")
            },
        )
        DropdownMenuItem(
            text = { Text("Delete") },
            onClick = {
                showMenu = false
                onDelete()
            },
        )
    }
}
