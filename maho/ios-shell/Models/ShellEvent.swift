import Foundation

enum ShellEvent: Codable {
    case unknown(kind: String)
    // Navigation
    case navigateTo(tabId: TabId, url: Url)
    case goBack(tabId: TabId)
    case goForward(tabId: TabId)
    case reload(tabId: TabId)
    case stop(tabId: TabId)

    // Tab management
    case createTab(spaceId: SpaceId, url: Url?, parentId: TabId?)
    case closeTab(tabId: TabId)
    case activateTab(tabId: TabId)
    case duplicateTab(tabId: TabId)
    case pinTab(tabId: TabId)
    case unpinTab(tabId: TabId)
    case favoriteTab(tabId: TabId)
    case unfavoriteTab(tabId: TabId)
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
    case commandBarQuery(text: String, mode: String?)
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

    // Split view
    case createSplit(tabIds: [TabId], orientation: Orientation)
    case removeSplit(paneId: PaneId)
    case resizeSplit(paneId: PaneId, ratio: Double)

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

    // History
    case searchHistory(query: String, limit: Int)
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
        case .unfavoriteTab: return "unfavorite_tab"
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
        case .createSplit: return "create_split"
        case .removeSplit: return "remove_split"
        case .resizeSplit: return "resize_split"
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

        switch self {
        case .navigateTo(let tabId, let url):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(url, forKey: DynamicCodingKey(stringValue: "url")!)
        case .goBack(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .goForward(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .reload(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .stop(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .createTab(let spaceId, let url, let parentId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encodeIfPresent(url, forKey: DynamicCodingKey(stringValue: "url")!)
            try container.encodeIfPresent(parentId, forKey: DynamicCodingKey(stringValue: "parent_id")!)
        case .closeTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .activateTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .duplicateTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .pinTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .unpinTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .favoriteTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .unfavoriteTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .muteTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .unmuteTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .freezeTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .moveTab(let tabId, let targetSpace, let position):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(targetSpace, forKey: DynamicCodingKey(stringValue: "target_space")!)
            try container.encode(position, forKey: DynamicCodingKey(stringValue: "position")!)
        case .setTabParent(let tabId, let newParentId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encodeIfPresent(newParentId, forKey: DynamicCodingKey(stringValue: "new_parent_id")!)
        case .reorderTab(let tabId, let beforeTabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encodeIfPresent(beforeTabId, forKey: DynamicCodingKey(stringValue: "before_tab_id")!)
        case .closeOtherTabs(let spaceId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .closeTabsToRight(let spaceId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .closeTabsToLeft(let spaceId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .reopenLastClosed: break
        case .restoreArchivedTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .resetPinnedTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .createSpace(let name, let color, let profileId):
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
            try container.encode(color, forKey: DynamicCodingKey(stringValue: "color")!)
            try container.encode(profileId, forKey: DynamicCodingKey(stringValue: "profile_id")!)
        case .deleteSpace(let spaceId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
        case .exportSpaceIntoFolder(let spaceId, let targetSpaceId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(targetSpaceId, forKey: DynamicCodingKey(stringValue: "target_space_id")!)
        case .activateSpace(let spaceId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
        case .renameSpace(let spaceId, let name):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
        case .recolorSpace(let spaceId, let color):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(color, forKey: DynamicCodingKey(stringValue: "color")!)
        case .reorderSpace(let spaceId, let from, let to):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(from, forKey: DynamicCodingKey(stringValue: "from")!)
            try container.encode(to, forKey: DynamicCodingKey(stringValue: "to")!)
        case .createFolder(let spaceId, let name, let isPinned):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
            try container.encode(isPinned, forKey: DynamicCodingKey(stringValue: "is_pinned")!)
        case .renameFolder(let spaceId, let folderId, let name):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
        case .deleteFolder(let spaceId, let folderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .moveFolderIntoFolder(let spaceId, let folderId, let targetFolderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(targetFolderId, forKey: DynamicCodingKey(stringValue: "target_folder_id")!)
        case .reorderFolder(let spaceId, let folderId, let from, let to):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(from, forKey: DynamicCodingKey(stringValue: "from")!)
            try container.encode(to, forKey: DynamicCodingKey(stringValue: "to")!)
        case .moveTabToFolder(let spaceId, let folderId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .removeTabFromFolder(let spaceId, let folderId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .toggleFolderExpanded(let spaceId, let folderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .pinFolder(let spaceId, let folderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .unpinFolder(let spaceId, let folderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .commandBarOpened: break
        case .commandBarClosed: break
        case .commandBarQuery(let text, let mode):
            try container.encode(text, forKey: DynamicCodingKey(stringValue: "text")!)
            try container.encodeIfPresent(mode, forKey: DynamicCodingKey(stringValue: "mode")!)
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
        case .createSplit(let tabIds, let orientation):
            try container.encode(tabIds, forKey: DynamicCodingKey(stringValue: "tab_ids")!)
            try container.encode(orientation, forKey: DynamicCodingKey(stringValue: "orientation")!)
        case .removeSplit(let paneId):
            try container.encode(paneId, forKey: DynamicCodingKey(stringValue: "pane_id")!)
        case .resizeSplit(let paneId, let ratio):
            try container.encode(paneId, forKey: DynamicCodingKey(stringValue: "pane_id")!)
            try container.encode(ratio, forKey: DynamicCodingKey(stringValue: "ratio")!)
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
        case .searchHistory(let query, let limit):
            try container.encode(query, forKey: DynamicCodingKey(stringValue: "query")!)
            try container.encode(limit, forKey: DynamicCodingKey(stringValue: "limit")!)
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

        case .toggleExtension(let extensionId):
            try container.encode(extensionId, forKey: DynamicCodingKey(stringValue: "extension_id")!)
        case .removeExtension(let extensionId):
            try container.encode(extensionId, forKey: DynamicCodingKey(stringValue: "extension_id")!)
        case .signIn(let email, let displayName, let password, let accessToken, let userId, let deviceId):
            try container.encode(email, forKey: DynamicCodingKey(stringValue: "email")!)
            try container.encodeIfPresent(displayName, forKey: DynamicCodingKey(stringValue: "display_name")!)
            try container.encodeIfPresent(password, forKey: DynamicCodingKey(stringValue: "password")!)
            try container.encodeIfPresent(accessToken, forKey: DynamicCodingKey(stringValue: "access_token")!)
            try container.encodeIfPresent(userId, forKey: DynamicCodingKey(stringValue: "user_id")!)
            try container.encodeIfPresent(deviceId, forKey: DynamicCodingKey(stringValue: "device_id")!)
        case .signOut: break
        case .toggleSync: break
        case .createTrafficRule(let rule):
            try container.encode(rule, forKey: DynamicCodingKey(stringValue: "rule")!)
        case .deleteTrafficRule(let ruleId):
            try container.encode(ruleId, forKey: DynamicCodingKey(stringValue: "rule_id")!)
        case .updateTrafficRule(let rule):
            try container.encode(rule, forKey: DynamicCodingKey(stringValue: "rule")!)
        case .setDefaultLinkBehavior(let behavior):
            try container.encode(behavior, forKey: DynamicCodingKey(stringValue: "behavior")!)
        case .updateSpaceConfig(let changes):
            try container.encode(changes, forKey: DynamicCodingKey(stringValue: "changes")!)
        case .createProfile(let name):
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
        case .deleteProfile(let profileId):
            try container.encode(profileId, forKey: DynamicCodingKey(stringValue: "profile_id")!)
        case .updateProfile(let profileId, let name, let avatarColor, let downloadPath, let archiveTimeoutHours):
            try container.encode(profileId, forKey: DynamicCodingKey(stringValue: "profile_id")!)
            try container.encodeIfPresent(name, forKey: DynamicCodingKey(stringValue: "name")!)
            try container.encodeIfPresent(avatarColor, forKey: DynamicCodingKey(stringValue: "avatar_color")!)
            try container.encodeIfPresent(downloadPath, forKey: DynamicCodingKey(stringValue: "download_path")!)
            try container.encodePatchable(archiveTimeoutHours, forKey: DynamicCodingKey(stringValue: "archive_timeout_hours")!)
        case .switchProfile(let profileId):
            try container.encode(profileId, forKey: DynamicCodingKey(stringValue: "profile_id")!)
        case .searchPasswords(let query):
            try container.encode(query, forKey: DynamicCodingKey(stringValue: "query")!)
        case .deletePassword(let passwordId):
            try container.encode(passwordId, forKey: DynamicCodingKey(stringValue: "password_id")!)
        case .addPassword(let domain, let username):
            try container.encode(domain, forKey: DynamicCodingKey(stringValue: "domain")!)
            try container.encode(username, forKey: DynamicCodingKey(stringValue: "username")!)
        case .addAutofillAddress(let address):
            try container.encode(address, forKey: DynamicCodingKey(stringValue: "address")!)
        case .deleteAutofillAddress(let id):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
        case .addAutofillPayment(let payment):
            try container.encode(payment, forKey: DynamicCodingKey(stringValue: "payment")!)
        case .deleteAutofillPayment(let id):
            try container.encode(id, forKey: DynamicCodingKey(stringValue: "id")!)
        case .resetSettings: break
        case .addToReadingList(let url, let title):
            try container.encode(url, forKey: DynamicCodingKey(stringValue: "url")!)
            try container.encode(title, forKey: DynamicCodingKey(stringValue: "title")!)
        case .removeFromReadingList(let itemId):
            try container.encode(itemId, forKey: DynamicCodingKey(stringValue: "item_id")!)
        case .markReadingListItemRead(let itemId):
            try container.encode(itemId, forKey: DynamicCodingKey(stringValue: "item_id")!)
        case .markReadingListItemUnread(let itemId):
            try container.encode(itemId, forKey: DynamicCodingKey(stringValue: "item_id")!)
        case .unknown(let kind):
            try container.encode(kind, forKey: DynamicCodingKey(stringValue: "kind")!)
        }
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: DynamicCodingKey.self)
        let kind = try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "kind")!)

        switch kind {
        // Navigation
        case "navigate_to":
            self = .navigateTo(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!))
        case "go_back":
            self = .goBack(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "go_forward":
            self = .goForward(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "reload":
            self = .reload(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "stop":
            self = .stop(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))

        // Tab management
        case "create_tab":
            self = .createTab(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                url: try container.decodeIfPresent(Url.self, forKey: DynamicCodingKey(stringValue: "url")!),
                parentId: try container.decodeIfPresent(TabId.self, forKey: DynamicCodingKey(stringValue: "parent_id")!))
        case "close_tab":
            self = .closeTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "activate_tab":
            self = .activateTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "duplicate_tab":
            self = .duplicateTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "pin_tab":
            self = .pinTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "unpin_tab":
            self = .unpinTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "favorite_tab":
            self = .favoriteTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "unfavorite_tab":
            self = .unfavoriteTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "mute_tab":
            self = .muteTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "unmute_tab":
            self = .unmuteTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "freeze_tab":
            self = .freezeTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "move_tab":
            self = .moveTab(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                targetSpace: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "target_space")!),
                position: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "position")!))
        case "set_tab_parent":
            self = .setTabParent(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                newParentId: try container.decodeIfPresent(TabId.self, forKey: DynamicCodingKey(stringValue: "new_parent_id")!))
        case "reorder_tab":
            self = .reorderTab(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                beforeTabId: try container.decodeIfPresent(TabId.self, forKey: DynamicCodingKey(stringValue: "before_tab_id")!))
        case "close_other_tabs":
            self = .closeOtherTabs(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "close_tabs_to_right":
            self = .closeTabsToRight(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "close_tabs_to_left":
            self = .closeTabsToLeft(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "reopen_last_closed":
            self = .reopenLastClosed
        case "restore_archived_tab":
            self = .restoreArchivedTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "reset_pinned_tab":
            self = .resetPinnedTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))

        // Space management
        case "create_space":
            self = .createSpace(
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!),
                color: try container.decode(SpaceColor.self, forKey: DynamicCodingKey(stringValue: "color")!),
                profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!))
        case "delete_space":
            self = .deleteSpace(spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!))
        case "export_space_into_folder":
            self = .exportSpaceIntoFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                targetSpaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "target_space_id")!))
        case "activate_space":
            self = .activateSpace(spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!))
        case "rename_space":
            self = .renameSpace(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!))
        case "recolor_space":
            self = .recolorSpace(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                color: try container.decode(SpaceColor.self, forKey: DynamicCodingKey(stringValue: "color")!))
        case "reorder_space":
            self = .reorderSpace(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                from: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "from")!),
                to: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "to")!))

        // Folder management
        case "create_folder":
            self = .createFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!),
                isPinned: try container.decodeIfPresent(Bool.self, forKey: DynamicCodingKey(stringValue: "is_pinned")!) ?? false)
        case "rename_folder":
            self = .renameFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!))
        case "delete_folder":
            self = .deleteFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "move_folder_into_folder":
            self = .moveFolderIntoFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                targetFolderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "target_folder_id")!))
        case "reorder_folder":
            self = .reorderFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                from: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "from")!),
                to: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "to")!))
        case "move_tab_to_folder":
            self = .moveTabToFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "remove_tab_from_folder":
            self = .removeTabFromFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "toggle_folder_expanded":
            self = .toggleFolderExpanded(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "pin_folder":
            self = .pinFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "unpin_folder":
            self = .unpinFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))

        // Command bar
        case "command_bar_opened":
            self = .commandBarOpened
        case "command_bar_closed":
            self = .commandBarClosed
        case "command_bar_query":
            self = .commandBarQuery(
                text: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "text")!),
                mode: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "mode")!))
        case "command_bar_select":
            self = .commandBarSelect(
                index: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "index")!),
                key: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "key")!))
        case "command_bar_action":
            self = .commandBarAction(action: try container.decode(QuickAction.self, forKey: DynamicCodingKey(stringValue: "action")!))
        case "save_search":
            self = .saveSearch(query: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "query")!))
        case "add_search_engine":
            self = .addSearchEngine(engine: try container.decode(SearchEngineModel.self, forKey: DynamicCodingKey(stringValue: "engine")!))
        case "remove_search_engine":
            self = .removeSearchEngine(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))
        case "set_default_search_engine":
            self = .setDefaultSearchEngine(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))

        // Notification management
        case "dismiss_notification":
            self = .dismissNotification(notificationId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "notification_id")!))
        case "dismiss_all_notifications":
            self = .dismissAllNotifications
        case "notification_action":
            self = .notificationAction(
                notificationId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "notification_id")!),
                actionId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "action_id")!))
        case "set_notification_filter":
            self = .setNotificationFilter(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                allowed: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "allowed")!))

        // Split view
        case "create_split":
            self = .createSplit(
                tabIds: try container.decode([TabId].self, forKey: DynamicCodingKey(stringValue: "tab_ids")!),
                orientation: try container.decode(Orientation.self, forKey: DynamicCodingKey(stringValue: "orientation")!))
        case "remove_split":
            self = .removeSplit(paneId: try container.decode(PaneId.self, forKey: DynamicCodingKey(stringValue: "pane_id")!))
        case "resize_split":
            self = .resizeSplit(
                paneId: try container.decode(PaneId.self, forKey: DynamicCodingKey(stringValue: "pane_id")!),
                ratio: try container.decode(Double.self, forKey: DynamicCodingKey(stringValue: "ratio")!))

        // Boosts
        case "create_boost":
            self = .createBoost(domain: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "domain")!))
        case "update_boost":
            self = .updateBoost(
                boostId: try container.decode(BoostId.self, forKey: DynamicCodingKey(stringValue: "boost_id")!),
                changes: try container.decode(BoostUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))
        case "toggle_boost":
            self = .toggleBoost(
                boostId: try container.decode(BoostId.self, forKey: DynamicCodingKey(stringValue: "boost_id")!),
                enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))
        case "delete_boost":
            self = .deleteBoost(boostId: try container.decode(BoostId.self, forKey: DynamicCodingKey(stringValue: "boost_id")!))

        // Content Blocker
        case "toggle_content_blocker":
            self = .toggleContentBlocker(enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))
        case "toggle_popup_blocking":
            self = .togglePopupBlocking(enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))
        case "add_filter_list":
            self = .addFilterList(
                id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!),
                url: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "url")!))
        case "remove_filter_list":
            self = .removeFilterList(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))
        case "toggle_filter_list":
            self = .toggleFilterList(
                id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!),
                enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))

        // Notes
        case "create_note":
            self = .createNote(
                linkedTab: try container.decodeIfPresent(TabId.self, forKey: DynamicCodingKey(stringValue: "linked_tab")!),
                content: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "content")!))
        case "update_note":
            self = .updateNote(
                noteId: try container.decode(NoteId.self, forKey: DynamicCodingKey(stringValue: "note_id")!),
                content: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "content")!))
        case "delete_note":
            self = .deleteNote(noteId: try container.decode(NoteId.self, forKey: DynamicCodingKey(stringValue: "note_id")!))

        // Settings
        case "update_settings":
            self = .updateSettings(changes: try container.decode(SettingsUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))

        // Window/sidebar
        case "sidebar_toggled":
            self = .sidebarToggled(visible: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "visible")!))
        case "sidebar_resized":
            self = .sidebarResized(width: try container.decode(Double.self, forKey: DynamicCodingKey(stringValue: "width")!))
        case "window_resized":
            self = .windowResized(size: try container.decode(Size.self, forKey: DynamicCodingKey(stringValue: "size")!))
        case "window_focus_changed":
            self = .windowFocusChanged(focused: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "focused")!))

        // Web content state sync
        case "tab_title_updated":
            self = .tabTitleUpdated(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                title: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "title")!))
        case "tab_url_updated":
            self = .tabUrlUpdated(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!))
        case "tab_loading_changed":
            self = .tabLoadingChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                isLoading: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "is_loading")!))
        case "tab_navigation_state_changed":
            self = .tabNavigationStateChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                canGoBack: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "can_go_back")!),
                canGoForward: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "can_go_forward")!))

        // History
        case "search_history":
            self = .searchHistory(
                query: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "query")!),
                limit: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "limit")!))
        case "clear_history":
            self = .clearHistory
        case "delete_history_entry":
            self = .deleteHistoryEntry(entryId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "entry_id")!))

        // Bookmarks
        case "add_bookmark":
            self = .addBookmark(
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!),
                title: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "title")!),
                folderId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "remove_bookmark":
            self = .removeBookmark(bookmarkId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "bookmark_id")!))
        case "move_bookmark":
            self = .moveBookmark(
                bookmarkId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "bookmark_id")!),
                folderId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "search_bookmarks":
            self = .searchBookmarks(query: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "query")!))

        // Permissions
        case "grant_permission":
            self = .grantPermission(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                permission: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "permission")!))
        case "revoke_permission":
            self = .revokePermission(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                permission: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "permission")!))
        case "query_permission":
            self = .queryPermission(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                permission: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "permission")!))

        // Zoom
        case "set_zoom":
            self = .setZoom(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                zoomLevel: try container.decode(Double.self, forKey: DynamicCodingKey(stringValue: "zoom_level")!))
        case "reset_zoom":
            self = .resetZoom(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))

        // Downloads
        case "pause_download":
            self = .pauseDownload(downloadId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "download_id")!))
        case "resume_download":
            self = .resumeDownload(downloadId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "download_id")!))
        case "cancel_download":
            self = .cancelDownload(downloadId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "download_id")!))
        case "remove_download":
            self = .removeDownload(downloadId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "download_id")!))

        // Browsing
        case "toggle_pi_p":
            self = .togglePiP(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "toggle_dev_tools":
            self = .toggleDevTools(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "print_page":
            self = .printPage(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "view_source":
            self = .viewSource(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))

        // Lifecycle
        case "app_launched":
            self = .appLaunched
        case "app_will_terminate":
            self = .appWillTerminate
        case "memory_warning":
            self = .memoryWarning(level: try container.decode(MemoryPressureLevel.self, forKey: DynamicCodingKey(stringValue: "level")!))



        // Extensions management
        case "toggle_extension":
            self = .toggleExtension(extensionId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "extension_id")!))
        case "remove_extension":
            self = .removeExtension(extensionId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "extension_id")!))

        // Account & Sync
        case "sign_in":
            self = .signIn(
                email: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "email")!),
                displayName: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "display_name")!),
                password: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "password")!),
                accessToken: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "access_token")!),
                userId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "user_id")!),
                deviceId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "device_id")!))
        case "sign_out":
            self = .signOut
        case "toggle_sync":
            self = .toggleSync

        // Air traffic control
        case "create_traffic_rule":
            self = .createTrafficRule(rule: try container.decode(TrafficRule.self, forKey: DynamicCodingKey(stringValue: "rule")!))
        case "delete_traffic_rule":
            self = .deleteTrafficRule(ruleId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "rule_id")!))
        case "update_traffic_rule":
            self = .updateTrafficRule(rule: try container.decode(TrafficRule.self, forKey: DynamicCodingKey(stringValue: "rule")!))
        case "set_default_link_behavior":
            self = .setDefaultLinkBehavior(behavior: try container.decode(DefaultLinkBehavior.self, forKey: DynamicCodingKey(stringValue: "behavior")!))

        // Space settings
        case "update_space_config":
            self = .updateSpaceConfig(
                changes: try container.decode(SpaceConfigUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))

        // Profile management
        case "create_profile":
            self = .createProfile(name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!))
        case "delete_profile":
            self = .deleteProfile(profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!))
        case "update_profile":
            self = .updateProfile(
                profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!),
                name: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "name")!),
                avatarColor: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "avatar_color")!),
                downloadPath: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "download_path")!),
                archiveTimeoutHours: try container.decodePatchable(Patchable<Double>.self, forKey: DynamicCodingKey(stringValue: "archive_timeout_hours")!))
        case "switch_profile":
            self = .switchProfile(profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!))

        // Passwords
        case "search_passwords":
            self = .searchPasswords(query: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "query")!))
        case "delete_password":
            self = .deletePassword(passwordId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "password_id")!))
        case "add_password":
            self = .addPassword(
                domain: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "domain")!),
                username: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "username")!))

        // Autofill
        case "add_autofill_address":
            self = .addAutofillAddress(address: try container.decode(AutofillAddress.self, forKey: DynamicCodingKey(stringValue: "address")!))
        case "delete_autofill_address":
            self = .deleteAutofillAddress(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))
        case "add_autofill_payment":
            self = .addAutofillPayment(payment: try container.decode(AutofillPayment.self, forKey: DynamicCodingKey(stringValue: "payment")!))
        case "delete_autofill_payment":
            self = .deleteAutofillPayment(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))

        case "reset_settings":
            self = .resetSettings

        // Reading List
        case "add_to_reading_list":
            self = .addToReadingList(
                url: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "url")!),
                title: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "title")!))
        case "remove_from_reading_list":
            self = .removeFromReadingList(itemId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "item_id")!))
        case "mark_reading_list_item_read":
            self = .markReadingListItemRead(itemId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "item_id")!))
        case "mark_reading_list_item_unread":
            self = .markReadingListItemUnread(itemId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "item_id")!))

        default:
            self = .unknown(kind: kind)
        }
    }
}

enum QuickAction: Codable {
    case newTab
    case newSpace
    case closeTab
    case closeOtherTabs
    case closeAllTabs
    case duplicateTab
    case pinTab
    case unpinTab
    case muteTab
    case unmuteTab
    case muteAllTabs
    case freezeTab
    case unfreezeTab
    case reloadTab
    case hardReload
    case copyUrl
    case clearHistory
    case clearCookies
    case openSettings
    case openDownloads
    case openBookmarks
    case openHistory
    case toggleBoost
    case toggleSidebar
    case toggleContentBlocker
    case newFolder
    case archiveTab
    case restoreLastClosed
    case toggleFullScreen
    case zoomIn
    case zoomOut
    case resetZoom
    case findInPage
    case printPage
    case viewSource
    case toggleDevTools
    case nextSpace
    case prevSpace
    case custom(actionId: String)

    private enum CodingKeys: String, CodingKey {
        case kind
        case actionId = "action_id"
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .custom(let actionId):
            try container.encode("custom", forKey: .kind)
            try container.encode(actionId, forKey: .actionId)
        default:
            try container.encode(kindString, forKey: .kind)
        }
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)
        switch kind {
        case "new_tab": self = .newTab
        case "new_space": self = .newSpace
        case "close_tab": self = .closeTab
        case "close_other_tabs": self = .closeOtherTabs
        case "close_all_tabs": self = .closeAllTabs
        case "duplicate_tab": self = .duplicateTab
        case "pin_tab": self = .pinTab
        case "unpin_tab": self = .unpinTab
        case "mute_tab": self = .muteTab
        case "unmute_tab": self = .unmuteTab
        case "mute_all_tabs": self = .muteAllTabs
        case "freeze_tab": self = .freezeTab
        case "unfreeze_tab": self = .unfreezeTab
        case "reload_tab": self = .reloadTab
        case "hard_reload": self = .hardReload
        case "copy_url": self = .copyUrl
        case "clear_history": self = .clearHistory
        case "clear_cookies": self = .clearCookies
        case "open_settings": self = .openSettings
        case "open_downloads": self = .openDownloads
        case "open_bookmarks": self = .openBookmarks
        case "open_history": self = .openHistory
        case "toggle_boost": self = .toggleBoost
        case "toggle_sidebar": self = .toggleSidebar
        case "toggle_content_blocker": self = .toggleContentBlocker
        case "new_folder": self = .newFolder
        case "archive_tab": self = .archiveTab
        case "restore_last_closed": self = .restoreLastClosed
        case "toggle_full_screen": self = .toggleFullScreen
        case "zoom_in": self = .zoomIn
        case "zoom_out": self = .zoomOut
        case "reset_zoom": self = .resetZoom
        case "find_in_page": self = .findInPage
        case "print_page": self = .printPage
        case "view_source": self = .viewSource
        case "toggle_dev_tools": self = .toggleDevTools
        case "next_space": self = .nextSpace
        case "prev_space": self = .prevSpace
        case "custom":
            let actionId = try container.decode(String.self, forKey: .actionId)
            self = .custom(actionId: actionId)
        default:
            throw DecodingError.dataCorruptedError(forKey: .kind, in: container, debugDescription: "Unknown QuickAction kind: \(kind)")
        }
    }

    private var kindString: String {
        switch self {
        case .newTab: return "new_tab"
        case .newSpace: return "new_space"
        case .closeTab: return "close_tab"
        case .closeOtherTabs: return "close_other_tabs"
        case .closeAllTabs: return "close_all_tabs"
        case .duplicateTab: return "duplicate_tab"
        case .pinTab: return "pin_tab"
        case .unpinTab: return "unpin_tab"
        case .muteTab: return "mute_tab"
        case .unmuteTab: return "unmute_tab"
        case .muteAllTabs: return "mute_all_tabs"
        case .freezeTab: return "freeze_tab"
        case .unfreezeTab: return "unfreeze_tab"
        case .reloadTab: return "reload_tab"
        case .hardReload: return "hard_reload"
        case .copyUrl: return "copy_url"
        case .clearHistory: return "clear_history"
        case .clearCookies: return "clear_cookies"
        case .openSettings: return "open_settings"
        case .openDownloads: return "open_downloads"
        case .openBookmarks: return "open_bookmarks"
        case .openHistory: return "open_history"
        case .toggleBoost: return "toggle_boost"
        case .toggleSidebar: return "toggle_sidebar"
        case .toggleContentBlocker: return "toggle_content_blocker"
        case .newFolder: return "new_folder"
        case .archiveTab: return "archive_tab"
        case .restoreLastClosed: return "restore_last_closed"
        case .toggleFullScreen: return "toggle_full_screen"
        case .zoomIn: return "zoom_in"
        case .zoomOut: return "zoom_out"
        case .resetZoom: return "reset_zoom"
        case .findInPage: return "find_in_page"
        case .printPage: return "print_page"
        case .viewSource: return "view_source"
        case .toggleDevTools: return "toggle_dev_tools"
        case .nextSpace: return "next_space"
        case .prevSpace: return "prev_space"
        case .custom: return "custom"
        }
    }
}

struct DynamicCodingKey: CodingKey {
    var stringValue: String
    var intValue: Int?

    init?(stringValue: String) { self.stringValue = stringValue }
    init?(intValue: Int) { self.intValue = intValue; self.stringValue = "\(intValue)" }
}
