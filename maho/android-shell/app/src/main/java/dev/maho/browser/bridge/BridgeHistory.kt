package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.HistoryEntry
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

object BridgeHistory {

    fun searchHistoryEntries(query: String, limit: Int = 100): List<HistoryEntry> {
        val updates = MahoBridge.sendEvent(ShellEvent.SearchHistory(query = query, limit = limit))
        for (update in updates) {
            if (update is CoreUpdate.HistoryResults) {
                return update.entries
            }
        }
        return emptyList()
    }

    fun clearAllHistory() {
        MahoBridge.clearHistory()
    }

    fun deleteHistoryEntryById(entryId: String) {
        MahoBridge.deleteHistoryEntry(entryId)
    }
}
