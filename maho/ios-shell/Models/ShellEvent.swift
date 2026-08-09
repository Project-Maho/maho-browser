enum ShellEvent: Codable {
    case unknown(kind: String)
    // Navigation
    case navigateTo(tabId: TabId, url: Url)
    case goBack(tabId: TabId)
    case goForward(tabId: TabId)
    case reload(tabId: TabId)
    case stop(tabId: TabId)

    // Tab management
    case createTab(spaceId: SpaceId, url: Url?, parentId: TabId?, isPrivate: Bool)
    case closeTab(tabId: TabId)
    case activateTab(tabId: TabId)
    case duplicateTab(tabId: TabId)
    case pinTab(tabId: TabId)
    case unpinTab(tabId: TabId)
    case favoriteTab(tabId: TabId)
    case changeTabRole(tabId: TabId, newRole: TabRole)
    case reorderFavorite(tabId: TabId, newIndex: Int)
    case muteTab(tabId: TabId)
    case unmuteTab(tabId: TabId)
    case freezeTab(tabId: TabId)
    case moveTab(tabId: TabId, targetSpace: SpaceId, position: Int)
    case setTabParent(tabId: TabId, newParentId: TabId?)
    case reorderTab(tabId: TabId, beforeTabId: TabId?)
    case closeOtherTabs(spaceId: SpaceId, tabId: TabId)
    case closeTabsToRight(spaceId: SpaceId, tabId: TabId)
    case closeTabsToLeft(spaceId: SpaceId, tabId: TabId)
    case reopenLastClosed
    case archiveTabById(tabId: TabId)
    case restoreArchivedTab(tabId: TabId)
    case resetPinnedTab(tabId: TabId)

    // Space management
    case createSpace(name: String, color: SpaceColor, profileId: ProfileId)
    case deleteSpace(spaceId: SpaceId)
    case exportSpaceIntoFolder(spaceId: SpaceId, targetSpaceId: SpaceId)
    case activateSpace(spaceId: SpaceId)
    case renameSpace(spaceId: SpaceId, name: String)
    case recolorSpace(spaceId: SpaceId, color: SpaceColor)
    case reorderSpace(spaceId: SpaceId, from: Int, to: Int)

    // Folder management
    case createFolder(spaceId: SpaceId, name: String, isPinned: Bool)
    case renameFolder(spaceId: SpaceId, folderId: FolderId, name: String)
    case deleteFolder(spaceId: SpaceId, folderId: FolderId)
    case moveFolderIntoFolder(spaceId: SpaceId, folderId: FolderId, targetFolderId: FolderId)
    case reorderFolder(spaceId: SpaceId, folderId: FolderId, from: Int, to: Int)
    case moveTabToFolder(spaceId: SpaceId, folderId: FolderId, tabId: TabId)
    case removeTabFromFolder(spaceId: SpaceId, folderId: FolderId, tabId: TabId)
    case toggleFolderExpanded(spaceId: SpaceId, folderId: FolderId)
    case pinFolder(spaceId: SpaceId, folderId: FolderId)
    case unpinFolder(spaceId: SpaceId, folderId: FolderId)

    // Command bar
    case commandBarOpened
    case commandBarClosed
    case commandBarQuery(text: String, mode: String?, isIncognito: Bool)
    case commandBarSelect(index: Int, key: String)
    case commandBarAction(action: QuickAction)
    case saveSearch(query: String)
    case addSearchEngine(engine: SearchEngineModel)
    case removeSearchEngine(id: String)
    case setDefaultSearchEngine(id: String)

    // Notification management
    case dismissNotification(notificationId: String)
    case dismissAllNotifications
    case notificationAction(notificationId: String, actionId: String)
    case setNotificationFilter(origin: String, allowed: Bool)



    // Boosts
    case createBoost(domain: String)
    case updateBoost(boostId: BoostId, changes: BoostUpdate)
    case toggleBoost(boostId: BoostId, enabled: Bool)
    case deleteBoost(boostId: BoostId)

    // Content Blocker
    case toggleContentBlocker(enabled: Bool)
    case togglePopupBlocking(enabled: Bool)
    case addFilterList(id: String, name: String, url: String)
    case removeFilterList(id: String)
    case toggleFilterList(id: String, enabled: Bool)

    // Notes
    case createNote(linkedTab: TabId?, content: String)
    case updateNote(noteId: NoteId, content: String)
    case deleteNote(noteId: NoteId)

    // Settings
    case updateSettings(changes: SettingsUpdate)

    // Window/sidebar
    case sidebarToggled(visible: Bool)
    case sidebarResized(width: Double)
    case windowResized(size: Size)
    case windowFocusChanged(focused: Bool)

    // Web content state sync
    case tabTitleUpdated(tabId: TabId, title: String)
    case tabUrlUpdated(tabId: TabId, url: Url)
    case tabLoadingChanged(tabId: TabId, isLoading: Bool)
    case tabNavigationStateChanged(tabId: TabId, canGoBack: Bool, canGoForward: Bool)
    case tabFaviconUpdated(tabId: TabId, favicon: ImageData?)

    // History
    case searchHistory(query: String, limit: Int, isIncognito: Bool)
    case clearHistory
    case deleteHistoryEntry(entryId: String)

    // Bookmarks
    case addBookmark(url: Url, title: String, folderId: String?)
    case removeBookmark(bookmarkId: String)
    case moveBookmark(bookmarkId: String, folderId: String?)
    case searchBookmarks(query: String)

    // Permissions
    case grantPermission(origin: String, permission: String)
    case revokePermission(origin: String, permission: String)
    case queryPermission(origin: String, permission: String)

    // Zoom
    case setZoom(tabId: TabId, zoomLevel: Double)
    case resetZoom(tabId: TabId)

    // Downloads
    case pauseDownload(downloadId: String)
    case resumeDownload(downloadId: String)
    case cancelDownload(downloadId: String)
    case removeDownload(downloadId: String)

    // Browsing
    case togglePiP(tabId: TabId)
    case toggleDevTools(tabId: TabId)
    case printPage(tabId: TabId)
    case viewSource(tabId: TabId)

    // Lifecycle
    case appLaunched
    case appWillTerminate
    case memoryWarning(level: MemoryPressureLevel)



    // Extensions management
    case toggleExtension(extensionId: String)
    case removeExtension(extensionId: String)

    // Account & Sync
    case signIn(email: String, displayName: String?, password: String? = nil, accessToken: String? = nil, userId: String? = nil, deviceId: String? = nil)
    case signOut
    case toggleSync

    // Air traffic control
    case createTrafficRule(rule: TrafficRule)
    case deleteTrafficRule(ruleId: String)
    case updateTrafficRule(rule: TrafficRule)
    case setDefaultLinkBehavior(behavior: DefaultLinkBehavior)

    // Space settings
    case updateSpaceConfig(changes: SpaceConfigUpdate)

    // Profile management
    case createProfile(name: String)
    case deleteProfile(profileId: ProfileId)
    case updateProfile(profileId: ProfileId, name: String?, avatarColor: String?, downloadPath: String?, archiveTimeoutHours: Patchable<Double>)
    case switchProfile(profileId: ProfileId)

    // Passwords
    case searchPasswords(query: String)
    case deletePassword(passwordId: String)
    case addPassword(domain: String, username: String)

    // Autofill
    case addAutofillAddress(address: AutofillAddress)
    case deleteAutofillAddress(id: String)
    case addAutofillPayment(payment: AutofillPayment)
    case deleteAutofillPayment(id: String)

    case resetSettings

    // Reading List
    case addToReadingList(url: String, title: String)
    case removeFromReadingList(itemId: String)
    case markReadingListItemRead(itemId: String)
    case markReadingListItemUnread(itemId: String)

    private enum CodingKeys: String, CodingKey {
        case kind
    }

    private var kindValue: String {
        switch self {
        case .navigateTo: return "navigate_to"
        case .goBack: return "go_back"
        case .goForward: return "go_forward"
        case .reload: return "reload"
        case .stop: return "stop"
        case .createTab: return "create_tab"
        case .closeTab: return "close_tab"
        case .activateTab: return "activate_tab"
        case .duplicateTab: return "duplicate_tab"
        case .pinTab: return "pin_tab"
        case .unpinTab: return "unpin_tab"
        case .favoriteTab: return "favorite_tab"
        case .changeTabRole: return "change_tab_role"
        case .reorderFavorite: return "reorder_favorite"
        case .muteTab: return "mute_tab"
        case .unmuteTab: return "unmute_tab"
        case .freezeTab: return "freeze_tab"
        case .moveTab: return "move_tab"
        case .setTabParent: return "set_tab_parent"
        case .reorderTab: return "reorder_tab"
        case .closeOtherTabs: return "close_other_tabs"
        case .closeTabsToRight: return "close_tabs_to_right"
        case .closeTabsToLeft: return "close_tabs_to_left"
        case .reopenLastClosed: return "reopen_last_closed"
        case .archiveTabById: return "archive_tab_by_id"
        case .restoreArchivedTab: return "restore_archived_tab"
        case .resetPinnedTab: return "reset_pinned_tab"
        case .createSpace: return "create_space"
        case .deleteSpace: return "delete_space"
        case .exportSpaceIntoFolder: return "export_space_into_folder"
        case .activateSpace: return "activate_space"
        case .renameSpace: return "rename_space"
        case .recolorSpace: return "recolor_space"
        case .reorderSpace: return "reorder_space"
        case .createFolder: return "create_folder"
        case .renameFolder: return "rename_folder"
        case .deleteFolder: return "delete_folder"
        case .moveFolderIntoFolder: return "move_folder_into_folder"
        case .reorderFolder: return "reorder_folder"
        case .moveTabToFolder: return "move_tab_to_folder"
        case .removeTabFromFolder: return "remove_tab_from_folder"
        case .toggleFolderExpanded: return "toggle_folder_expanded"
        case .pinFolder: return "pin_folder"
        case .unpinFolder: return "unpin_folder"
        case .commandBarOpened: return "command_bar_opened"
        case .commandBarClosed: return "command_bar_closed"
        case .commandBarQuery: return "command_bar_query"
        case .commandBarSelect: return "command_bar_select"
        case .commandBarAction: return "command_bar_action"
        case .saveSearch: return "save_search"
        case .addSearchEngine: return "add_search_engine"
        case .removeSearchEngine: return "remove_search_engine"
        case .setDefaultSearchEngine: return "set_default_search_engine"
        case .dismissNotification: return "dismiss_notification"
        case .dismissAllNotifications: return "dismiss_all_notifications"
        case .notificationAction: return "notification_action"
        case .setNotificationFilter: return "set_notification_filter"

        case .createBoost: return "create_boost"
        case .updateBoost: return "update_boost"
        case .toggleBoost: return "toggle_boost"
        case .deleteBoost: return "delete_boost"
        case .toggleContentBlocker: return "toggle_content_blocker"
        case .togglePopupBlocking: return "toggle_popup_blocking"
        case .addFilterList: return "add_filter_list"
        case .removeFilterList: return "remove_filter_list"
        case .toggleFilterList: return "toggle_filter_list"
        case .createNote: return "create_note"
        case .updateNote: return "update_note"
        case .deleteNote: return "delete_note"
        case .updateSettings: return "update_settings"
        case .sidebarToggled: return "sidebar_toggled"
        case .sidebarResized: return "sidebar_resized"
        case .windowResized: return "window_resized"
        case .windowFocusChanged: return "window_focus_changed"
        case .tabTitleUpdated: return "tab_title_updated"
        case .tabUrlUpdated: return "tab_url_updated"
        case .tabLoadingChanged: return "tab_loading_changed"
        case .tabNavigationStateChanged: return "tab_navigation_state_changed"
        case .tabFaviconUpdated: return "tab_favicon_updated"
        case .searchHistory: return "search_history"
        case .clearHistory: return "clear_history"
        case .deleteHistoryEntry: return "delete_history_entry"
        case .addBookmark: return "add_bookmark"
        case .removeBookmark: return "remove_bookmark"
        case .moveBookmark: return "move_bookmark"
        case .searchBookmarks: return "search_bookmarks"
        case .grantPermission: return "grant_permission"
        case .revokePermission: return "revoke_permission"
        case .queryPermission: return "query_permission"
        case .setZoom: return "set_zoom"
        case .resetZoom: return "reset_zoom"
        case .pauseDownload: return "pause_download"
        case .resumeDownload: return "resume_download"
        case .cancelDownload: return "cancel_download"
        case .removeDownload: return "remove_download"
        case .togglePiP: return "toggle_pi_p"
        case .toggleDevTools: return "toggle_dev_tools"
        case .printPage: return "print_page"
        case .viewSource: return "view_source"
        case .appLaunched: return "app_launched"
        case .appWillTerminate: return "app_will_terminate"
        case .memoryWarning: return "memory_warning"
        case .toggleExtension: return "toggle_extension"
        case .removeExtension: return "remove_extension"
        case .signIn: return "sign_in"
        case .signOut: return "sign_out"
        case .toggleSync: return "toggle_sync"
        case .createTrafficRule: return "create_traffic_rule"
        case .deleteTrafficRule: return "delete_traffic_rule"
        case .updateTrafficRule: return "update_traffic_rule"
        case .setDefaultLinkBehavior: return "set_default_link_behavior"
        case .updateSpaceConfig: return "update_space_config"
        case .createProfile: return "create_profile"
        case .deleteProfile: return "delete_profile"
        case .updateProfile: return "update_profile"
        case .switchProfile: return "switch_profile"
        case .searchPasswords: return "search_passwords"
        case .deletePassword: return "delete_password"
        case .addPassword: return "add_password"
        case .addAutofillAddress: return "add_autofill_address"
        case .deleteAutofillAddress: return "delete_autofill_address"
        case .addAutofillPayment: return "add_autofill_payment"
        case .deleteAutofillPayment: return "delete_autofill_payment"
        case .resetSettings: return "reset_settings"
        case .addToReadingList: return "add_to_reading_list"
        case .removeFromReadingList: return "remove_from_reading_list"
        case .markReadingListItemRead: return "mark_reading_list_item_read"
        case .markReadingListItemUnread: return "mark_reading_list_item_unread"
        case .unknown(let kind): return kind
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: DynamicCodingKey.self)
        try container.encode(kindValue, forKey: DynamicCodingKey(stringValue: "kind")!)

        if try encodeTabsSpaces(to: &container) { return }
        if try encodeBrowser(to: &container) { return }
        if try encodeAccountLibrary(to: &container) { return }

        if case .unknown(let kind) = self {
            try container.encode(kind, forKey: DynamicCodingKey(stringValue: "kind")!)
        }
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: DynamicCodingKey.self)
        let kind = try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "kind")!)

        if let event = try Self.decodeTabsSpaces(kind: kind, from: container) {
            self = event
            return
        }
        if let event = try Self.decodeBrowser(kind: kind, from: container) {
            self = event
            return
        }
        if let event = try Self.decodeAccountLibrary(kind: kind, from: container) {
            self = event
            return
        }
        self = .unknown(kind: kind)
    }
}
