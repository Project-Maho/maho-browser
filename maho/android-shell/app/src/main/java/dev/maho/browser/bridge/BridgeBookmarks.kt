package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.BookmarkEntry
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

object BridgeBookmarks {

    fun getBookmarkEntries(): List<BookmarkEntry> {
        val json = MahoBridge.getBookmarks() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<BookmarkEntry>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun addBookmarkEntry(url: String, title: String, folderId: String? = null) {
        MahoBridge.sendEvent(ShellEvent.AddBookmark(url = url, title = title, folderId = folderId))
    }

    fun removeBookmarkEntry(bookmarkId: String) {
        MahoBridge.sendEvent(ShellEvent.RemoveBookmark(bookmarkId = bookmarkId))
    }

    fun moveBookmarkEntry(bookmarkId: String, folderId: String? = null) {
        MahoBridge.sendEvent(ShellEvent.MoveBookmark(bookmarkId = bookmarkId, folderId = folderId))
    }

    fun searchBookmarkEntries(query: String): List<BookmarkEntry> {
        val updates = MahoBridge.sendEvent(ShellEvent.SearchBookmarks(query = query))
        for (update in updates) {
            if (update is CoreUpdate.BookmarkResults) {
                return update.bookmarks
            }
        }
        return emptyList()
    }
}
