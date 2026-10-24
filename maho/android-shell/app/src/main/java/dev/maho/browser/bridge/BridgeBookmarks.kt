package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.BookmarkEntry
import dev.maho.browser.models.BookmarkFolder
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

    // Display names for bookmark folders, keyed by folder id.
    fun getFolderNames(): Map<String, String> {
        val json = try {
            MahoBridge.getBookmarkFolders()
        } catch (_: UnsatisfiedLinkError) {
            // A native library built before nativeGetBookmarkFolders existed.
            null
        } ?: return emptyMap()
        return try {
            MahoJson.instance.decodeFromString<List<BookmarkFolder>>(json).associate { it.id to it.name }
        } catch (_: Exception) {
            emptyMap()
        }
    }

    // The JNI layer maps an empty folder id to "no folder" (root).
    fun addBookmarkEntry(url: String, title: String, folderId: String? = null) {
        MahoBridge.addBookmark(url, title, folderId ?: "")
    }

    fun removeBookmarkEntry(bookmarkId: String) {
        MahoBridge.removeBookmark(bookmarkId)
    }

    fun moveBookmarkEntry(bookmarkId: String, folderId: String? = null) {
        MahoBridge.moveBookmark(bookmarkId, folderId ?: "")
    }

    // nativeSearchBookmarks serializes storage rows as
    // [id, url, title, folder_id|null, created_at] tuples, not objects.
    fun searchBookmarkEntries(query: String): List<BookmarkEntry> {
        val json = MahoBridge.searchBookmarks(query) ?: return emptyList()
        val rows = try {
            MahoJson.instance.decodeFromString<List<List<String?>>>(json)
        } catch (_: Exception) {
            return emptyList()
        }
        return rows.mapNotNull { row ->
            if (row.size != 5) return@mapNotNull null
            val id = row[0] ?: return@mapNotNull null
            val url = row[1] ?: return@mapNotNull null
            val title = row[2] ?: return@mapNotNull null
            val createdAt = row[4] ?: return@mapNotNull null
            BookmarkEntry(id = id, url = url, title = title, folderId = row[3], createdAt = createdAt)
        }
    }
}
