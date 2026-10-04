package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ReadingListItem
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

object BridgeReadingList {

    fun getReadingListItems(): List<ReadingListItem> {
        val json = MahoBridge.getReadingList() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<ReadingListItem>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun getReadingListUnreadCount(): Int {
        return getReadingListItems().count { !it.isRead }
    }

    fun toggleReadingListItemRead(itemId: String) {
        MahoBridge.toggleReadingListRead(itemId)
    }

    fun addToReadingList(url: String, title: String): List<CoreUpdate> {
        return MahoBridge.sendEvent(ShellEvent.AddToReadingList(url = url, title = title))
    }

    fun removeReadingListItem(itemId: String): List<CoreUpdate> {
        return MahoBridge.sendEvent(ShellEvent.RemoveFromReadingList(itemId = itemId))
    }
}
