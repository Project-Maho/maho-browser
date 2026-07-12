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
        sendEvent(.clearHistory)
    }

    func deleteHistoryEntry(id: String) {
        sendEvent(.deleteHistoryEntry(entryId: id))
    }
}
