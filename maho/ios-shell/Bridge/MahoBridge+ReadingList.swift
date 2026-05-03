import Foundation

extension MahoBridge {

    func getReadingList() -> [ReadingListItem] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_reading_list(ptr))
        } ?? nil) ?? []
    }

    func getReadingListUnreadCount() -> Int {
        let items = getReadingList()
        return items.filter { !$0.isRead }.count
    }

    func toggleRead(id: String) {
        withCore { ptr in
            id.withCString { cId in
                maho_core_toggle_reading_list_read(ptr, cId)
            }
        }
    }

    func addToReadingList(url: String, title: String) {
        sendEvent(.addToReadingList(url: url, title: title))
    }

    func removeFromReadingList(id: String) {
        sendEvent(.removeFromReadingList(itemId: id))
    }
}
