import Foundation

extension MahoBridge {

    func searchHistory(query: String, limit: Int) -> [HistoryEntry] {
        let updates = handleEvent(.searchHistory(query: query, limit: limit))
        for update in updates {
            if case .historyResults(let entries) = update {
                return entries
            }
        }
        return []
    }

    func clearHistory() {
        sendEvent(.clearHistory)
    }

    func deleteHistoryEntry(id: String) {
        sendEvent(.deleteHistoryEntry(entryId: id))
    }
}
