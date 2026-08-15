package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.ArchivedTabViewModel
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.FolderId
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.builtins.serializer

object BridgeTabs {

    fun getTabViewModels(): List<TabViewModel> {
        val json = MahoBridge.getTabViewModels() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<TabViewModel>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun getArchivedTabs(spaceId: SpaceId): List<ArchivedTabViewModel> {
        val json = MahoBridge.getArchivedTabs(MahoJson.instance.encodeToString(String.serializer(), spaceId))
            ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<ArchivedTabViewModel>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun restoreArchivedTab(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.RestoreArchivedTab(tabId = tabId))

    fun getActiveTabId(): TabId? {
        val json = MahoBridge.getSpaceViewModels() ?: return null
        val spaces = try {
            MahoJson.instance.decodeFromString<List<dev.maho.browser.models.SpaceViewModel>>(json)
        } catch (_: Exception) {
            return null
        }
        val activeSpaceId = spaces.firstOrNull { it.isActive }?.id ?: return null
        val tabs = getTabViewModels().filter { it.spaceId == activeSpaceId }
        return tabs.maxByOrNull { it.lastActiveAt }?.id
    }

    fun createTab(spaceId: SpaceId, url: String? = null): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CreateTab(spaceId = spaceId, url = url))

    fun closeTab(id: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CloseTab(tabId = id))

    fun archiveTab(id: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ArchiveTabById(tabId = id))

    fun activateTab(id: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ActivateTab(tabId = id))

    fun reorderTab(id: TabId, beforeTabId: TabId? = null): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ReorderTab(tabId = id, beforeTabId = beforeTabId))

    fun moveTab(tabId: TabId, targetSpace: SpaceId, position: Int): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.MoveTab(tabId = tabId, targetSpace = targetSpace, position = position))

    fun moveTabToFolder(spaceId: SpaceId, folderId: FolderId, tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.MoveTabToFolder(spaceId = spaceId, folderId = folderId, tabId = tabId))

    fun removeTabFromFolder(spaceId: SpaceId, folderId: FolderId, tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.RemoveTabFromFolder(spaceId = spaceId, folderId = folderId, tabId = tabId))

    fun closeAllTabs(spaceId: SpaceId, keepTabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CloseOtherTabs(spaceId = spaceId, tabId = keepTabId))

    fun duplicateTab(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.DuplicateTab(tabId = tabId))

    fun pinTab(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.PinTab(tabId = tabId))

    fun unpinTab(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.UnpinTab(tabId = tabId))

    fun muteTab(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.MuteTab(tabId = tabId))

    fun unmuteTab(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.UnmuteTab(tabId = tabId))

    fun favoriteTab(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.FavoriteTab(tabId = tabId))

    fun getFavoriteTabs(spaceId: SpaceId): List<TabViewModel> {
        val json = MahoBridge.getFavoriteTabs(MahoJson.instance.encodeToString(String.serializer(), spaceId))
            ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<TabViewModel>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun getPinnedTabs(spaceId: SpaceId): List<TabViewModel> {
        return getTabViewModels().filter { it.spaceId == spaceId && it.isPinned && !it.isFavorite }
    }

    fun getTodayTabs(spaceId: SpaceId): List<TabViewModel> {
        return getTabViewModels().filter { it.spaceId == spaceId && !it.isPinned && !it.isFavorite }
    }
}
