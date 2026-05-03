import Foundation

extension MahoBridge {

    func getBookmarks() -> [BookmarkEntry] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_all_bookmarks(ptr))
        } ?? nil) ?? []
    }

    func addBookmark(url: String, title: String, folderId: String?) {
        sendEvent(.addBookmark(url: Url(url), title: title, folderId: folderId))
    }

    func removeBookmark(id: String) {
        sendEvent(.removeBookmark(bookmarkId: id))
    }

    func moveBookmark(id: String, toFolder folderId: String?) {
        sendEvent(.moveBookmark(bookmarkId: id, folderId: folderId))
    }

    func searchBookmarks(query: String) -> [BookmarkEntry] {
        let updates = handleEvent(.searchBookmarks(query: query))
        for update in updates {
            if case .bookmarkResults(let bookmarks) = update {
                return bookmarks
            }
        }
        return []
    }
}
