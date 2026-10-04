@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.folder

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.layout.boundsInParent
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeFolders
import dev.maho.browser.models.FolderId
import dev.maho.browser.models.FolderViewModel
import dev.maho.browser.models.SpaceId
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

private data class FolderItemBounds(
    val top: Float,
    val height: Float,
)

@Composable
fun FolderListSheet(
    spaceId: SpaceId,
    onDismiss: () -> Unit,
    onFolderSelected: (FolderId) -> Unit,
    modifier: Modifier = Modifier,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    val folders = remember { mutableStateListOf<FolderViewModel>() }
    val itemBounds = remember { mutableStateMapOf<FolderId, FolderItemBounds>() }
    var draggedFolderId by remember { mutableStateOf<FolderId?>(null) }
    var dragStartIndex by remember { mutableIntStateOf(-1) }
    var draggedIndex by remember { mutableIntStateOf(-1) }
    var targetIndex by remember { mutableIntStateOf(-1) }
    var dragOffsetY by remember { mutableFloatStateOf(0f) }

    fun reloadFolders() {
        folders.clear()
        folders.addAll(BridgeFolders.getFolderViewModels(spaceId))
        itemBounds.clear()
    }

    fun resetDragState() {
        draggedFolderId = null
        dragStartIndex = -1
        draggedIndex = -1
        targetIndex = -1
        dragOffsetY = 0f
    }

    fun moveFolder(fromIndex: Int, toIndex: Int) {
        if (fromIndex == toIndex) return
        val folder = folders.removeAt(fromIndex)
        folders.add(toIndex, folder)
    }

    fun finishDrag(commit: Boolean) {
        val folderId = draggedFolderId
        val moved = folderId != null && dragStartIndex >= 0 && draggedIndex >= 0 && dragStartIndex != draggedIndex

        if (commit && moved && folderId != null) {
            BridgeFolders.reorderFolder(folderId, spaceId, dragStartIndex, draggedIndex)
        }

        reloadFolders()
        resetDragState()
    }

    fun handleDrag(deltaY: Float) {
        val activeFolderId = draggedFolderId ?: return
        if (draggedIndex !in folders.indices) return

        dragOffsetY += deltaY
        val currentBounds = itemBounds[activeFolderId] ?: return
        val currentCenterY = currentBounds.top + currentBounds.height / 2f + dragOffsetY

        while (draggedIndex < folders.lastIndex) {
            val nextFolder = folders[draggedIndex + 1]
            val nextBounds = itemBounds[nextFolder.id] ?: break
            val nextCenterY = nextBounds.top + nextBounds.height / 2f
            if (currentCenterY <= nextCenterY) break
            targetIndex = draggedIndex + 1
            moveFolder(draggedIndex, draggedIndex + 1)
            draggedIndex += 1
            dragOffsetY = 0f
            break
        }

        while (draggedIndex > 0) {
            val previousFolder = folders[draggedIndex - 1]
            val previousBounds = itemBounds[previousFolder.id] ?: break
            val previousCenterY = previousBounds.top + previousBounds.height / 2f
            if (currentCenterY >= previousCenterY) break
            targetIndex = draggedIndex - 1
            moveFolder(draggedIndex, draggedIndex - 1)
            draggedIndex -= 1
            dragOffsetY = 0f
            break
        }
    }

    LaunchedEffect(spaceId) {
        reloadFolders()
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        modifier = modifier,
    ) {
        Scaffold(
            topBar = {
                TopAppBar(title = { Text("Folders") })
            },
            floatingActionButton = {
                FloatingActionButton(onClick = {
                    BridgeFolders.createFolder(name = "New Folder", spaceId = spaceId)
                    reloadFolders()
                }) {
                    Icon(painter = painterResource(id = MahoIcon.Add.drawableRes), contentDescription = "New Folder")
                }
            },
        ) { padding ->
            if (folders.isEmpty()) {
                Box(
                    modifier = Modifier
                        .padding(padding)
                        .fillMaxWidth()
                        .padding(vertical = 64.dp),
                    contentAlignment = androidx.compose.ui.Alignment.Center,
                ) {
                    Column(
                        horizontalAlignment = androidx.compose.ui.Alignment.CenterHorizontally,
                    ) {
                        Icon(painter = painterResource(id = MahoIcon.Folder.drawableRes),
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Text(
                            text = "No Folders",
                            style = MaterialTheme.typography.titleMedium,
                            modifier = Modifier.padding(top = 8.dp),
                        )
                    }
                }
            } else {
                LazyColumn(
                    modifier = Modifier
                        .padding(padding)
                        .fillMaxWidth(),
                ) {
                    items(
                        items = folders,
                        key = { it.id },
                    ) { folder ->
                        val index = folders.indexOfFirst { it.id == folder.id }
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .onGloballyPositioned { coordinates ->
                                    val bounds = coordinates.boundsInParent()
                                    itemBounds[folder.id] = FolderItemBounds(
                                        top = bounds.top,
                                        height = bounds.height,
                                    )
                                },
                        ) {
                            if (draggedFolderId != null && index == targetIndex) {
                                HorizontalDivider(
                                    thickness = 2.dp,
                                    color = MaterialTheme.colorScheme.primary,
                                )
                            } else {
                                Spacer(modifier = Modifier.height(2.dp))
                            }
                            FolderCard(
                                folder = folder,
                                onTap = { onFolderSelected(folder.id) },
                                onRename = { newName ->
                                    BridgeFolders.renameFolder(folder.id, spaceId, newName)
                                    reloadFolders()
                                },
                                onDelete = {
                                    BridgeFolders.deleteFolder(folder.id, spaceId)
                                    reloadFolders()
                                },
                                onDragHandleStart = {
                                    draggedFolderId = folder.id
                                    dragStartIndex = index
                                    draggedIndex = index
                                    targetIndex = index
                                    dragOffsetY = 0f
                                },
                                onDragHandleDrag = { dragAmount: Offset ->
                                    handleDrag(dragAmount.y)
                                },
                                onDragHandleEnd = {
                                    finishDrag(commit = true)
                                },
                                onDragHandleCancel = {
                                    finishDrag(commit = false)
                                },
                                isDragging = draggedFolderId == folder.id,
                            )
                        }
                    }
                }
            }
        }
    }
}
