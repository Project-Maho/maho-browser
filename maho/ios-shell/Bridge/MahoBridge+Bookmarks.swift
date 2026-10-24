import Foundation

extension MahoBridge {

    func getBookmarks() -> [BookmarkEntry] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_all_bookmarks(ptr))
        } ?? nil) ?? []
    }

    func addBookmark(url: String, title: String, folderId: String?) {
        _ = withCore { ptr in
            FFIString.withCString(url) { cUrl in
                FFIString.withCString(title) { cTitle in
                    if let folderId {
                        FFIString.withCString(folderId) { cFolderId in
                            maho_core_add_bookmark(ptr, cUrl, cTitle, cFolderId)
                        }
                    } else {
                        maho_core_add_bookmark(ptr, cUrl, cTitle, nil)
                    }
                }
            }
        }
    }

    func removeBookmark(id: String) {
        _ = withCore { ptr in
            FFIString.withCString(id) { cBookmarkId in
                maho_core_remove_bookmark(ptr, cBookmarkId)
            }
        }
    }

    func moveBookmark(id: String, toFolder folderId: String?) {
        _ = withCore { ptr in
            FFIString.withCString(id) { cBookmarkId in
                if let folderId {
                    FFIString.withCString(folderId) { cFolderId in
                        maho_core_move_bookmark(ptr, cBookmarkId, cFolderId)
                    }
                } else {
                    maho_core_move_bookmark(ptr, cBookmarkId, nil)
                }
            }
        }
    }

    /// `maho_core_search_bookmarks` serializes storage rows as
    /// `[id, url, title, folder_id|null, created_at]` tuples, not objects.
    func searchBookmarks(query: String) -> [BookmarkEntry] {
        let rows: [[String?]] = (withCore { ptr in
            FFIString.withCString(query) { cQuery in
                FFIString.consumeJSON(maho_core_search_bookmarks(ptr, cQuery), as: [[String?]].self)
            }
        } ?? nil) ?? []
        return rows.compactMap { row in
            guard row.count == 5,
                  let id = row[0], let url = row[1], let title = row[2], let createdAt = row[4] else {
                return nil
            }
            return BookmarkEntry(id: id, url: url, title: title, folderId: row[3], createdAt: createdAt)
        }
    }
}
