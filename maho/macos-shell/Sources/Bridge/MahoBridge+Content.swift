import Foundation

// MARK: - Content Operations (Boosts, Notes, Downloads, Content Rules, Filter Lists,
//         Bookmarks, History, Find, Permissions, Zoom, Reader, Notifications, Split View,
//         Reading List)

extension MahoBridge {

    // MARK: - Boosts

    func getBoostViewModels() -> [BoostViewModel] {
        switch getBoostViewModelsResult() {
        case .success(let boosts):
            return boosts
        case .failure:
            return []
        }
    }

    func getBoostsForUrl(_ url: String) -> [BoostViewModel] {
        return withFFIString(url) { cStr -> [BoostViewModel] in
            guard let ptr = callCore({ maho_core_get_boosts_for_url($0, cStr) }) else { return [] }
            defer { maho_string_free(ptr) }
            return decode(String(cString: ptr)) ?? []
        }
    }




    // MARK: - Notes

    func getNoteViewModels() -> [NoteViewModel] {
        guard let json = callCoreString({ maho_core_get_note_view_models($0) }) else { return [] }
        return decode(json) ?? []
    }

    func linkNoteToTab(noteId: String, tabId: String) {
        withFFIString(noteId) { noteCStr in
            withFFIString(tabId) { tabCStr in
                callCoreVoid { maho_core_link_note_to_tab($0, noteCStr, tabCStr) }
            }
        }
    }

    func linkNoteToUrl(noteId: String, url: String) {
        withFFIString(noteId) { noteCStr in
            withFFIString(url) { urlCStr in
                callCoreVoid { maho_core_link_note_to_url($0, noteCStr, urlCStr) }
            }
        }
    }

    func unlinkNoteFromTab(noteId: String) {
        withFFIString(noteId) { cStr in
            callCoreVoid { maho_core_unlink_note_from_tab($0, cStr) }
        }
    }

    func getLinkedTabId(noteId: String) -> String? {
        return withFFIString(noteId) { cStr -> String? in
            return callCoreString { maho_core_get_linked_tab_id($0, cStr) }
        }
    }

    func exportNotes(format: String) -> String? {
        return withFFIString(format) { cStr -> String? in
            return callCoreString { maho_core_export_notes($0, cStr) }
        }
    }

    func exportSingleNote(noteId: String, format: String) -> String? {
        return withFFIString(noteId) { noteCStr -> String? in
            return withFFIString(format) { formatCStr -> String? in
                return callCoreString { maho_core_export_single_note($0, noteCStr, formatCStr) }
            }
        }
    }

    func searchNotesFts(query: String) -> String? {
        return withFFIString(query) { cStr -> String? in
            return callCoreString { maho_core_search_notes_fts($0, cStr) }
        }
    }

    // MARK: - Downloads

    func getDownloadViewModels() -> [DownloadViewModel] {
        guard let json = callCoreString({ maho_core_get_download_view_models($0) }) else { return [] }
        return decode(json) ?? []
    }

    func startDownload(json: String) -> String? {
        return withFFIString(json) { cStr -> String? in
            return callCoreString { maho_core_start_download($0, cStr) }
        }
    }

    func updateDownloadProgress(downloadId: String, receivedBytes: UInt64) {
        withFFIString(downloadId) { cStr in
            callCoreVoid { maho_core_update_download_progress($0, cStr, receivedBytes) }
        }
    }

    func pauseDownload(downloadId: String) {
        withFFIString(downloadId) { cStr in
            callCoreVoid { maho_core_pause_download($0, cStr) }
        }
    }

    func resumeDownload(downloadId: String) {
        withFFIString(downloadId) { cStr in
            callCoreVoid { maho_core_resume_download($0, cStr) }
        }
    }

    func cancelDownload(downloadId: String) {
        withFFIString(downloadId) { cStr in
            callCoreVoid { maho_core_cancel_download($0, cStr) }
        }
    }

    func removeDownload(downloadId: String) {
        withFFIString(downloadId) { cStr in
            callCoreVoid { maho_core_remove_download($0, cStr) }
        }
    }

    func completeDownload(downloadId: String) -> String? {
        return withFFIString(downloadId) { cStr -> String? in
            return callCoreString { maho_core_complete_download($0, cStr) }
        }
    }

    func restoreDownloadName(downloadId: String) -> String? {
        return withFFIString(downloadId) { cStr -> String? in
            return callCoreString { maho_core_restore_download_name($0, cStr) }
        }
    }

    // MARK: - Content Rules & Filter Lists

    func shouldBlockRequest(url: String, sourceUrl: String, requestType: String) -> Bool {
        return withFFIString(url) { urlCStr -> Bool in
            return withFFIString(sourceUrl) { sourceCStr -> Bool in
                return withFFIString(requestType) { typeCStr -> Bool in
                    guard let result = callCoreNonOptional({ maho_core_should_block_request($0, urlCStr, sourceCStr, typeCStr) }) else { return false }
                    return result
                }
            }
        }
    }

    func getFilterLists() -> String? {
        return callCoreString { maho_core_get_filter_lists($0) }
    }

    func addFilterList(id: String, name: String, url: String) {
        withFFIString(id) { idCStr in
            withFFIString(name) { nameCStr in
                withFFIString(url) { urlCStr in
                    callCoreVoid { maho_core_add_filter_list($0, idCStr, nameCStr, urlCStr) }
                }
            }
        }
    }

    func updateFilterListContent(id: String, content: String) {
        withFFIString(id) { idCStr in
            withFFIString(content) { contentCStr in
                callCoreVoid { maho_core_update_filter_list_content($0, idCStr, contentCStr) }
            }
        }
    }

    func rebuildContentRules() {
        callCoreVoid { maho_core_rebuild_content_rules($0) }
    }

    func getContentRuleCount() -> UInt {
        guard let count = callCoreNonOptional({ maho_core_get_content_rule_count($0) }) else { return 0 }
        return count
    }

    // MARK: - Bookmarks

    func getAllBookmarks() -> String? {
        return callCoreString { maho_core_get_all_bookmarks($0) }
    }

    func addBookmark(url: String, title: String, folderId: String?) {
        withFFIString(url) { urlCStr in
            withFFIString(title) { titleCStr in
                if let folderId = folderId {
                    withFFIString(folderId) { folderCStr in
                        callCoreVoid { maho_core_add_bookmark($0, urlCStr, titleCStr, folderCStr) }
                    }
                } else {
                    callCoreVoid { maho_core_add_bookmark($0, urlCStr, titleCStr, nil) }
                }
            }
        }
    }

    func removeBookmark(bookmarkId: String) {
        withFFIString(bookmarkId) { cStr in
            callCoreVoid { maho_core_remove_bookmark($0, cStr) }
        }
    }

    func searchBookmarks(query: String) -> String? {
        return withFFIString(query) { cStr -> String? in
            return callCoreString { maho_core_search_bookmarks($0, cStr) }
        }
    }

    func moveBookmark(bookmarkId: String, folderId: String?) {
        withFFIString(bookmarkId) { bookmarkCStr in
            if let folderId = folderId {
                withFFIString(folderId) { folderCStr in
                    callCoreVoid { maho_core_move_bookmark($0, bookmarkCStr, folderCStr) }
                }
            } else {
                callCoreVoid { maho_core_move_bookmark($0, bookmarkCStr, nil) }
            }
        }
    }

    func createBookmarkFolder(name: String, parentId: String?) -> String? {
        return withFFIString(name) { nameCStr -> String? in
            if let parentId = parentId {
                return withFFIString(parentId) { parentCStr -> String? in
                    return callCoreString { maho_core_create_bookmark_folder($0, nameCStr, parentCStr) }
                }
            } else {
                return callCoreString { maho_core_create_bookmark_folder($0, nameCStr, nil) }
            }
        }
    }

    func deleteBookmarkFolder(folderId: String) -> String? {
        return withFFIString(folderId) { cStr -> String? in
            return callCoreString { maho_core_delete_bookmark_folder($0, cStr) }
        }
    }

    // MARK: - History

    func addHistoryEntry(url: String, title: String) {
        withFFIString(url) { urlCStr in
            withFFIString(title) { titleCStr in
                callCoreVoid { maho_core_add_history_entry($0, urlCStr, titleCStr) }
            }
        }
    }

    func searchHistory(query: String, limit: UInt) -> String? {
        return withFFIString(query) { cStr -> String? in
            return callCoreString { maho_core_search_history($0, cStr, limit) }
        }
    }

    func searchHistoryPaginated(query: String, limit: UInt, offset: UInt) -> String? {
        return withFFIString(query) { cStr -> String? in
            return callCoreString { maho_core_search_history_paginated($0, cStr, limit, offset) }
        }
    }

    func getHistoryGroupedByDate() -> String? {
        return callCoreString { maho_core_get_history_grouped_by_date($0) }
    }

    func clearHistory() {
        callCoreVoid { maho_core_clear_history($0) }
    }

    func deleteHistoryEntry(entryId: String) {
        withFFIString(entryId) { cStr in
            callCoreVoid { maho_core_delete_history_entry($0, cStr) }
        }
    }

    // MARK: - Find in Page

    func getFindBarState() -> FindBarState? {
        guard let json = callCoreString({ maho_core_get_find_bar_state($0) }) else { return nil }
        return decode(json)
    }

    func startFind(tabId: String, query: String, caseSensitive: Bool, wholeWord: Bool) -> String? {
        return withFFIString(tabId) { tabCStr -> String? in
            return withFFIString(query) { queryCStr -> String? in
                return callCoreString { maho_core_start_find($0, tabCStr, queryCStr, caseSensitive, wholeWord) }
            }
        }
    }

    func findNext() -> String? {
        return callCoreString { maho_core_find_next($0) }
    }

    func findPrevious() -> String? {
        return callCoreString { maho_core_find_previous($0) }
    }

    func dismissFind() {
        callCoreVoid { maho_core_dismiss_find($0) }
    }

    // MARK: - Permissions

    func grantPermission(origin: String, permission: String) {
        withFFIString(origin) { originCStr in
            withFFIString(permission) { permCStr in
                callCoreVoid { maho_core_grant_permission($0, originCStr, permCStr) }
            }
        }
    }

    func revokePermission(origin: String, permission: String) {
        withFFIString(origin) { originCStr in
            withFFIString(permission) { permCStr in
                callCoreVoid { maho_core_revoke_permission($0, originCStr, permCStr) }
            }
        }
    }

    func queryPermission(origin: String, permission: String) -> String? {
        return withFFIString(origin) { originCStr -> String? in
            return withFFIString(permission) { permCStr -> String? in
                return callCoreString { maho_core_query_permission($0, originCStr, permCStr) }
            }
        }
    }

    // MARK: - Zoom

    func getZoom(site: String) -> Double {
        return withFFIString(site) { cStr -> Double in
            guard let result = callCoreNonOptional({ maho_core_get_zoom($0, cStr) }) else { return 1.0 }
            return result
        }
    }

    func setZoom(site: String, zoom: Double) {
        withFFIString(site) { cStr in
            callCoreVoid { maho_core_set_zoom($0, cStr, zoom) }
        }
    }

    // MARK: - Reader Mode

    func toggleReaderMode(tabId: String) -> Bool {
        return withFFIString(tabId) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_toggle_reader_mode($0, cStr) }) else { return false }
            return result
        }
    }

    func isReaderMode(tabId: String) -> Bool {
        return withFFIString(tabId) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_is_reader_mode($0, cStr) }) else { return false }
            return result
        }
    }

    func getReaderSettings() -> String? {
        return callCoreString { maho_core_get_reader_settings($0) }
    }

    // MARK: - Notifications

    func getNotifications() -> String? {
        return callCoreString { maho_core_get_notifications($0) }
    }

    func dismissNotification(notificationId: String) {
        withFFIString(notificationId) { cStr in
            callCoreVoid { maho_core_dismiss_notification($0, cStr) }
        }
    }

    func dismissAllNotifications() {
        callCoreVoid { maho_core_dismiss_all_notifications($0) }
    }

    func setNotificationFilter(origin: String, allowed: Bool) {
        withFFIString(origin) { cStr in
            callCoreVoid { maho_core_set_notification_filter($0, cStr, allowed) }
        }
    }

    // MARK: - Split View

    func getSplitViewConfig() -> SplitViewConfig? {
        guard let json = callCoreString({ maho_core_get_split_view_config($0) }) else { return nil }
        return decode(json)
    }

    // MARK: - Reading List

    func addToReadingList(url: String, title: String) -> String? {
        return withFFIString(url) { urlCStr -> String? in
            return withFFIString(title) { titleCStr -> String? in
                return callCoreString { maho_core_add_to_reading_list($0, urlCStr, titleCStr) }
            }
        }
    }

    func removeFromReadingList(itemId: String) -> Bool {
        return withFFIString(itemId) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_remove_from_reading_list($0, cStr) }) else { return false }
            return result
        }
    }

    func toggleReadingListRead(itemId: String) -> Int8 {
        return withFFIString(itemId) { cStr -> Int8 in
            guard let result = callCoreNonOptional({ maho_core_toggle_reading_list_read($0, cStr) }) else { return -1 }
            return result
        }
    }

    func getReadingList() -> String? {
        return callCoreString { maho_core_get_reading_list($0) }
    }

    func getUnreadCount() -> UInt {
        guard let count = callCoreNonOptional({ maho_core_get_unread_count($0) }) else { return 0 }
        return count
    }

    // MARK: - Browser Import

    func detectBrowserProfiles() -> String? {
        // This is a static function (no core pointer needed)
        let ptr = maho_core_detect_browser_profiles()
        guard let ptr = ptr else { return nil }
        defer { maho_string_free(ptr) }
        return String(cString: ptr)
    }

    func importChromeBookmarks(profilePath: String) -> String? {
        return withFFIString(profilePath) { cStr -> String? in
            return callCoreString { maho_core_import_chrome_bookmarks($0, cStr) }
        }
    }

    func importFirefoxBookmarks(profilePath: String) -> String? {
        return withFFIString(profilePath) { cStr -> String? in
            return callCoreString { maho_core_import_firefox_bookmarks($0, cStr) }
        }
    }
}
