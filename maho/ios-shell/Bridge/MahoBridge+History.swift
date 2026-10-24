import Foundation
import WebKit

extension MahoBridge {

    func searchHistory(query: String, limit: Int, isIncognito: Bool = false) -> [HistoryEntry] {
        let updates = handleEvent(.searchHistory(query: query, limit: limit, isIncognito: isIncognito))
        for update in updates {
            if case .historyResults(let entries) = update {
                return entries
            }
        }
        return []
    }

    func clearHistory() {
        _ = withCore { ptr in
            maho_core_clear_history(ptr)
        }
    }

    /// Clears everything "Clear Browsing Data" promises: core history plus all
    /// WebKit website data (cookies, cache, local storage, logins).
    /// Removal is by type since `.distantPast`, so there is no fetch-then-remove
    /// window in which a newly written record escapes. `completion` runs once
    /// WebKit has finished removing the data.
    func clearAllBrowsingData(completion: (() -> Void)? = nil) {
        clearHistory()
        WKWebsiteDataStore.default().removeData(
            ofTypes: WKWebsiteDataStore.allWebsiteDataTypes(),
            modifiedSince: .distantPast
        ) {
            completion?()
        }
    }

    func deleteHistoryEntry(id: String) {
        _ = withCore { ptr in
            FFIString.withCString(id) { cEntryId in
                maho_core_delete_history_entry(ptr, cEntryId)
            }
        }
    }
}
