import Foundation

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

    func deleteHistoryEntry(id: String) {
        _ = withCore { ptr in
            FFIString.withCString(id) { cEntryId in
                maho_core_delete_history_entry(ptr, cEntryId)
            }
        }
    }
}
