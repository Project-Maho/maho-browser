import Foundation

extension ShellEvent {
    func encodeBrowser(to container: inout KeyedEncodingContainer<DynamicCodingKey>) throws -> Bool {
        switch self {
        case .commandBarOpened: break
        case .commandBarClosed: break
        case .commandBarQuery(let text, let mode, let isIncognito):
            try container.encode(text, forKey: DynamicCodingKey(stringValue: "text")!)
            try container.encodeIfPresent(mode, forKey: DynamicCodingKey(stringValue: "mode")!)
            try container.encode(isIncognito, forKey: DynamicCodingKey(stringValue: "is_incognito")!)
        case .commandBarSelect(let index, let key):
            try container.encode(index, forKey: DynamicCodingKey(stringValue: "index")!)
            try container.encode(key, forKey: DynamicCodingKey(stringValue: "key")!)
        case .commandBarAction(let action):
            try container.encode(action, forKey: DynamicCodingKey(stringValue: "action")!)
        case .saveSearch(let query):
            try container.encode(query, forKey: DynamicCodingKey(stringValue: "query")!)
        case .addSearchEngine(let engine):
            try container.encode(engine, forKey: DynamicCodingKey(stringValue: "engine")!)
        case .removeSearchEngine(let id):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
        case .setDefaultSearchEngine(let id):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
        case .dismissNotification(let notificationId):
            try container.encode(notificationId, forKey: DynamicCodingKey(stringValue: "notification_id")!)
        case .dismissAllNotifications: break
        case .notificationAction(let notificationId, let actionId):
            try container.encode(notificationId, forKey: DynamicCodingKey(stringValue: "notification_id")!)
            try container.encode(actionId, forKey: DynamicCodingKey(stringValue: "action_id")!)
        case .setNotificationFilter(let origin, let allowed):
            try container.encode(origin, forKey: DynamicCodingKey(stringValue: "origin")!)
            try container.encode(allowed, forKey: DynamicCodingKey(stringValue: "allowed")!)

        case .createBoost(let domain):
            try container.encode(domain, forKey: DynamicCodingKey(stringValue: "domain")!)
        case .updateBoost(let boostId, let changes):
            try container.encode(boostId, forKey: DynamicCodingKey(stringValue: "boost_id")!)
            try container.encode(changes, forKey: DynamicCodingKey(stringValue: "changes")!)
        case .toggleBoost(let boostId, let enabled):
            try container.encode(boostId, forKey: DynamicCodingKey(stringValue: "boost_id")!)
            try container.encode(enabled, forKey: DynamicCodingKey(stringValue: "enabled")!)
        case .deleteBoost(let boostId):
            try container.encode(boostId, forKey: DynamicCodingKey(stringValue: "boost_id")!)
        case .toggleContentBlocker(let enabled):
            try container.encode(enabled, forKey: DynamicCodingKey(stringValue: "enabled")!)
        case .togglePopupBlocking(let enabled):
            try container.encode(enabled, forKey: DynamicCodingKey(stringValue: "enabled")!)
        case .addFilterList(let id, let name, let url):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
            try container.encode(url, forKey: DynamicCodingKey(stringValue: "url")!)
        case .removeFilterList(let id):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
        case .toggleFilterList(let id, let enabled):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
            try container.encode(enabled, forKey: DynamicCodingKey(stringValue: "enabled")!)
        case .createNote(let linkedTab, let content):
            try container.encodeIfPresent(linkedTab, forKey: DynamicCodingKey(stringValue: "linked_tab")!)
            try container.encode(content, forKey: DynamicCodingKey(stringValue: "content")!)
        case .updateNote(let noteId, let content):
            try container.encode(noteId, forKey: DynamicCodingKey(stringValue: "note_id")!)
            try container.encode(content, forKey: DynamicCodingKey(stringValue: "content")!)
        case .deleteNote(let noteId):
            try container.encode(noteId, forKey: DynamicCodingKey(stringValue: "note_id")!)
        case .updateSettings(let changes):
            try container.encode(changes, forKey: DynamicCodingKey(stringValue: "changes")!)
        case .sidebarToggled(let visible):
            try container.encode(visible, forKey: DynamicCodingKey(stringValue: "visible")!)
        case .sidebarResized(let width):
            try container.encode(width, forKey: DynamicCodingKey(stringValue: "width")!)
        case .windowResized(let size):
            try container.encode(size, forKey: DynamicCodingKey(stringValue: "size")!)
        case .windowFocusChanged(let focused):
            try container.encode(focused, forKey: DynamicCodingKey(stringValue: "focused")!)
        case .tabTitleUpdated(let tabId, let title):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(title, forKey: DynamicCodingKey(stringValue: "title")!)
        case .tabUrlUpdated(let tabId, let url):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(url, forKey: DynamicCodingKey(stringValue: "url")!)
        case .tabLoadingChanged(let tabId, let isLoading):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(isLoading, forKey: DynamicCodingKey(stringValue: "is_loading")!)
        case .tabNavigationStateChanged(let tabId, let canGoBack, let canGoForward):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(canGoBack, forKey: DynamicCodingKey(stringValue: "can_go_back")!)
            try container.encode(canGoForward, forKey: DynamicCodingKey(stringValue: "can_go_forward")!)
        case .tabFaviconUpdated(let tabId, let favicon):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encodeIfPresent(favicon, forKey: DynamicCodingKey(stringValue: "favicon")!)
        case .searchHistory(let query, let limit, let isIncognito):
            try container.encode(query, forKey: DynamicCodingKey(stringValue: "query")!)
            try container.encode(limit, forKey: DynamicCodingKey(stringValue: "limit")!)
            try container.encode(isIncognito, forKey: DynamicCodingKey(stringValue: "is_incognito")!)
        case .clearHistory: break
        case .deleteHistoryEntry(let entryId):
            try container.encode(entryId, forKey: DynamicCodingKey(stringValue: "entry_id")!)
        case .addBookmark(let url, let title, let folderId):
            try container.encode(url, forKey: DynamicCodingKey(stringValue: "url")!)
            try container.encode(title, forKey: DynamicCodingKey(stringValue: "title")!)
            try container.encodeIfPresent(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .removeBookmark(let bookmarkId):
            try container.encode(bookmarkId, forKey: DynamicCodingKey(stringValue: "bookmark_id")!)
        case .moveBookmark(let bookmarkId, let folderId):
            try container.encode(bookmarkId, forKey: DynamicCodingKey(stringValue: "bookmark_id")!)
            try container.encodeIfPresent(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .searchBookmarks(let query):
            try container.encode(query, forKey: DynamicCodingKey(stringValue: "query")!)
        case .grantPermission(let origin, let permission):
            try container.encode(origin, forKey: DynamicCodingKey(stringValue: "origin")!)
            try container.encode(permission, forKey: DynamicCodingKey(stringValue: "permission")!)
        case .revokePermission(let origin, let permission):
            try container.encode(origin, forKey: DynamicCodingKey(stringValue: "origin")!)
            try container.encode(permission, forKey: DynamicCodingKey(stringValue: "permission")!)
        case .queryPermission(let origin, let permission):
            try container.encode(origin, forKey: DynamicCodingKey(stringValue: "origin")!)
            try container.encode(permission, forKey: DynamicCodingKey(stringValue: "permission")!)
        case .setZoom(let tabId, let zoomLevel):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(zoomLevel, forKey: DynamicCodingKey(stringValue: "zoom_level")!)
        case .resetZoom(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .pauseDownload(let downloadId):
            try container.encode(downloadId, forKey: DynamicCodingKey(stringValue: "download_id")!)
        case .resumeDownload(let downloadId):
            try container.encode(downloadId, forKey: DynamicCodingKey(stringValue: "download_id")!)
        case .cancelDownload(let downloadId):
            try container.encode(downloadId, forKey: DynamicCodingKey(stringValue: "download_id")!)
        case .removeDownload(let downloadId):
            try container.encode(downloadId, forKey: DynamicCodingKey(stringValue: "download_id")!)
        case .togglePiP(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .toggleDevTools(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .printPage(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .viewSource(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .appLaunched: break
        case .appWillTerminate: break
        case .memoryWarning(let level):
            try container.encode(level, forKey: DynamicCodingKey(stringValue: "level")!)

        default:
            return false
        }
        return true
    }

    static func decodeBrowser(kind: String, from container: KeyedDecodingContainer<DynamicCodingKey>) throws -> ShellEvent? {
        switch kind {
        // Command bar
        case "command_bar_opened":
            return .commandBarOpened
        case "command_bar_closed":
            return .commandBarClosed
        case "command_bar_query":
            return .commandBarQuery(
                text: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "text")!),
                mode: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "mode")!),
                isIncognito: (try? container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "is_incognito")!)) ?? false)
        case "command_bar_select":
            return .commandBarSelect(
                index: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "index")!),
                key: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "key")!))
        case "command_bar_action":
            return .commandBarAction(action: try container.decode(QuickAction.self, forKey: DynamicCodingKey(stringValue: "action")!))
        case "save_search":
            return .saveSearch(query: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "query")!))
        case "add_search_engine":
            return .addSearchEngine(engine: try container.decode(SearchEngineModel.self, forKey: DynamicCodingKey(stringValue: "engine")!))
        case "remove_search_engine":
            return .removeSearchEngine(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))
        case "set_default_search_engine":
            return .setDefaultSearchEngine(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))

        // Notification management
        case "dismiss_notification":
            return .dismissNotification(notificationId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "notification_id")!))
        case "dismiss_all_notifications":
            return .dismissAllNotifications
        case "notification_action":
            return .notificationAction(
                notificationId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "notification_id")!),
                actionId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "action_id")!))
        case "set_notification_filter":
            return .setNotificationFilter(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                allowed: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "allowed")!))



        // Boosts
        case "create_boost":
            return .createBoost(domain: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "domain")!))
        case "update_boost":
            return .updateBoost(
                boostId: try container.decode(BoostId.self, forKey: DynamicCodingKey(stringValue: "boost_id")!),
                changes: try container.decode(BoostUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))
        case "toggle_boost":
            return .toggleBoost(
                boostId: try container.decode(BoostId.self, forKey: DynamicCodingKey(stringValue: "boost_id")!),
                enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))
        case "delete_boost":
            return .deleteBoost(boostId: try container.decode(BoostId.self, forKey: DynamicCodingKey(stringValue: "boost_id")!))

        // Content Blocker
        case "toggle_content_blocker":
            return .toggleContentBlocker(enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))
        case "toggle_popup_blocking":
            return .togglePopupBlocking(enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))
        case "add_filter_list":
            return .addFilterList(
                id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!),
                url: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "url")!))
        case "remove_filter_list":
            return .removeFilterList(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))
        case "toggle_filter_list":
            return .toggleFilterList(
                id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!),
                enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))

        // Notes
        case "create_note":
            return .createNote(
                linkedTab: try container.decodeIfPresent(TabId.self, forKey: DynamicCodingKey(stringValue: "linked_tab")!),
                content: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "content")!))
        case "update_note":
            return .updateNote(
                noteId: try container.decode(NoteId.self, forKey: DynamicCodingKey(stringValue: "note_id")!),
                content: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "content")!))
        case "delete_note":
            return .deleteNote(noteId: try container.decode(NoteId.self, forKey: DynamicCodingKey(stringValue: "note_id")!))

        // Settings
        case "update_settings":
            return .updateSettings(changes: try container.decode(SettingsUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))

        // Window/sidebar
        case "sidebar_toggled":
            return .sidebarToggled(visible: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "visible")!))
        case "sidebar_resized":
            return .sidebarResized(width: try container.decode(Double.self, forKey: DynamicCodingKey(stringValue: "width")!))
        case "window_resized":
            return .windowResized(size: try container.decode(Size.self, forKey: DynamicCodingKey(stringValue: "size")!))
        case "window_focus_changed":
            return .windowFocusChanged(focused: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "focused")!))

        // Web content state sync
        case "tab_title_updated":
            return .tabTitleUpdated(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                title: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "title")!))
        case "tab_url_updated":
            return .tabUrlUpdated(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!))
        case "tab_loading_changed":
            return .tabLoadingChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                isLoading: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "is_loading")!))
        case "tab_navigation_state_changed":
            return .tabNavigationStateChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                canGoBack: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "can_go_back")!),
                canGoForward: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "can_go_forward")!))
        case "tab_favicon_updated":
            return .tabFaviconUpdated(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                favicon: try container.decodeIfPresent(ImageData.self, forKey: DynamicCodingKey(stringValue: "favicon")!))

        // History
        case "search_history":
            return .searchHistory(
                query: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "query")!),
                limit: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "limit")!),
                isIncognito: (try? container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "is_incognito")!)) ?? false)
        case "clear_history":
            return .clearHistory
        case "delete_history_entry":
            return .deleteHistoryEntry(entryId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "entry_id")!))

        // Bookmarks
        case "add_bookmark":
            return .addBookmark(
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!),
                title: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "title")!),
                folderId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "remove_bookmark":
            return .removeBookmark(bookmarkId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "bookmark_id")!))
        case "move_bookmark":
            return .moveBookmark(
                bookmarkId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "bookmark_id")!),
                folderId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "search_bookmarks":
            return .searchBookmarks(query: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "query")!))

        // Permissions
        case "grant_permission":
            return .grantPermission(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                permission: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "permission")!))
        case "revoke_permission":
            return .revokePermission(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                permission: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "permission")!))
        case "query_permission":
            return .queryPermission(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                permission: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "permission")!))

        // Zoom
        case "set_zoom":
            return .setZoom(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                zoomLevel: try container.decode(Double.self, forKey: DynamicCodingKey(stringValue: "zoom_level")!))
        case "reset_zoom":
            return .resetZoom(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))

        // Downloads
        case "pause_download":
            return .pauseDownload(downloadId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "download_id")!))
        case "resume_download":
            return .resumeDownload(downloadId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "download_id")!))
        case "cancel_download":
            return .cancelDownload(downloadId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "download_id")!))
        case "remove_download":
            return .removeDownload(downloadId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "download_id")!))

        // Browsing
        case "toggle_pi_p":
            return .togglePiP(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "toggle_dev_tools":
            return .toggleDevTools(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "print_page":
            return .printPage(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "view_source":
            return .viewSource(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))

        // Lifecycle
        case "app_launched":
            return .appLaunched
        case "app_will_terminate":
            return .appWillTerminate
        case "memory_warning":
            return .memoryWarning(level: try container.decode(MemoryPressureLevel.self, forKey: DynamicCodingKey(stringValue: "level")!))



        default:
            return nil
        }
    }
}
