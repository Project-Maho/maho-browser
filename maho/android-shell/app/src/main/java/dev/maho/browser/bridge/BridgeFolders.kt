package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.FolderId
import dev.maho.browser.models.FolderViewModel
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.models.SpaceId
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.builtins.serializer

object BridgeFolders {

    fun getFolderViewModels(spaceId: SpaceId): List<FolderViewModel> {
        val spaceIdJson = MahoJson.instance.encodeToString(String.serializer(), spaceId)
        val json = MahoBridge.getFolderViewModels(spaceIdJson) ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<FolderViewModel>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun createFolder(name: String, spaceId: SpaceId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CreateFolder(spaceId = spaceId, name = name))

    fun deleteFolder(id: FolderId, spaceId: SpaceId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.DeleteFolder(spaceId = spaceId, folderId = id))

    fun renameFolder(id: FolderId, spaceId: SpaceId, name: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.RenameFolder(spaceId = spaceId, folderId = id, name = name))

    fun reorderFolder(id: FolderId, spaceId: SpaceId, from: Int, to: Int): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ReorderFolder(spaceId = spaceId, folderId = id, from = from, to = to))

    fun createFolderAndReturnModel(name: String, spaceId: SpaceId): FolderViewModel? {
        val updates = createFolder(name, spaceId)
        for (update in updates) {
            if (update is CoreUpdate.FolderCreated) {
                return update.folder
            }
        }
        return null
    }
}
