import Foundation

enum QuickAction: Codable, Equatable {
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
    case copyUrlMarkdown
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
    case sharePage
    case newIncognito
    case scrollToTop
    case scrollToBottom
    case clearBrowsingData
    case custom(actionId: String)

    private enum CodingKeys: String, CodingKey {
        case kind
        case actionId = "action_id"
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
        case "copy_url_markdown": self = .copyUrlMarkdown
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
        case "share_page": self = .sharePage
        case "new_incognito": self = .newIncognito
        case "scroll_to_top": self = .scrollToTop
        case "scroll_to_bottom": self = .scrollToBottom
        case "clear_browsing_data": self = .clearBrowsingData
        case "custom":
            let actionId = try container.decode(String.self, forKey: .actionId)
            self = .custom(actionId: actionId)
        default:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown QuickAction kind: \(kind)"
            )
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .newTab: try container.encode("new_tab", forKey: .kind)
        case .newSpace: try container.encode("new_space", forKey: .kind)
        case .closeTab: try container.encode("close_tab", forKey: .kind)
        case .closeOtherTabs: try container.encode("close_other_tabs", forKey: .kind)
        case .closeAllTabs: try container.encode("close_all_tabs", forKey: .kind)
        case .duplicateTab: try container.encode("duplicate_tab", forKey: .kind)
        case .pinTab: try container.encode("pin_tab", forKey: .kind)
        case .unpinTab: try container.encode("unpin_tab", forKey: .kind)
        case .muteTab: try container.encode("mute_tab", forKey: .kind)
        case .unmuteTab: try container.encode("unmute_tab", forKey: .kind)
        case .muteAllTabs: try container.encode("mute_all_tabs", forKey: .kind)
        case .freezeTab: try container.encode("freeze_tab", forKey: .kind)
        case .unfreezeTab: try container.encode("unfreeze_tab", forKey: .kind)
        case .reloadTab: try container.encode("reload_tab", forKey: .kind)
        case .hardReload: try container.encode("hard_reload", forKey: .kind)
        case .copyUrl: try container.encode("copy_url", forKey: .kind)
        case .copyUrlMarkdown: try container.encode("copy_url_markdown", forKey: .kind)
        case .clearHistory: try container.encode("clear_history", forKey: .kind)
        case .clearCookies: try container.encode("clear_cookies", forKey: .kind)
        case .openSettings: try container.encode("open_settings", forKey: .kind)
        case .openDownloads: try container.encode("open_downloads", forKey: .kind)
        case .openBookmarks: try container.encode("open_bookmarks", forKey: .kind)
        case .openHistory: try container.encode("open_history", forKey: .kind)
        case .toggleBoost: try container.encode("toggle_boost", forKey: .kind)
        case .toggleSidebar: try container.encode("toggle_sidebar", forKey: .kind)
        case .toggleContentBlocker: try container.encode("toggle_content_blocker", forKey: .kind)
        case .newFolder: try container.encode("new_folder", forKey: .kind)
        case .archiveTab: try container.encode("archive_tab", forKey: .kind)
        case .restoreLastClosed: try container.encode("restore_last_closed", forKey: .kind)
        case .toggleFullScreen: try container.encode("toggle_full_screen", forKey: .kind)
        case .zoomIn: try container.encode("zoom_in", forKey: .kind)
        case .zoomOut: try container.encode("zoom_out", forKey: .kind)
        case .resetZoom: try container.encode("reset_zoom", forKey: .kind)
        case .findInPage: try container.encode("find_in_page", forKey: .kind)
        case .printPage: try container.encode("print_page", forKey: .kind)
        case .viewSource: try container.encode("view_source", forKey: .kind)
        case .toggleDevTools: try container.encode("toggle_dev_tools", forKey: .kind)
        case .nextSpace: try container.encode("next_space", forKey: .kind)
        case .prevSpace: try container.encode("prev_space", forKey: .kind)
        case .sharePage: try container.encode("share_page", forKey: .kind)
        case .newIncognito: try container.encode("new_incognito", forKey: .kind)
        case .scrollToTop: try container.encode("scroll_to_top", forKey: .kind)
        case .scrollToBottom: try container.encode("scroll_to_bottom", forKey: .kind)
        case .clearBrowsingData: try container.encode("clear_browsing_data", forKey: .kind)
        case .custom(let actionId):
            try container.encode("custom", forKey: .kind)
            try container.encode(actionId, forKey: .actionId)
        }
    }
}

enum ShellEvent: Codable, Equatable {
    // Navigation
    case navigateTo(tabId: TabId, url: MahoUrl)
    case goBack(tabId: TabId)
    case goForward(tabId: TabId)
    case reload(tabId: TabId)
    case stop(tabId: TabId)

    // Tab management
    case createTab(spaceId: SpaceId, url: MahoUrl?, parentId: TabId?)
    case closeTab(tabId: TabId)
    case activateTab(tabId: TabId)
    case duplicateTab(tabId: TabId)
    case pinTab(tabId: TabId)
    case unpinTab(tabId: TabId)
    case favoriteTab(tabId: TabId)
    case unfavoriteTab(tabId: TabId)
    case reorderFavorite(tabId: TabId, newIndex: Int)
    case muteTab(tabId: TabId)
    case unmuteTab(tabId: TabId)
    case freezeTab(tabId: TabId)
    case unfreezeTab(tabId: TabId)
    case moveTab(tabId: TabId, targetSpace: SpaceId, position: Int)
    case setTabParent(tabId: TabId, newParentId: TabId?)
    case tabFaviconUpdated(tabId: TabId, favicon: ImageData?)
    case reorderTab(tabId: TabId, beforeTabId: TabId?)
    case closeOtherTabs(spaceId: SpaceId, tabId: TabId)
    case closeTabsToRight(spaceId: SpaceId, tabId: TabId)
    case closeTabsToLeft(spaceId: SpaceId, tabId: TabId)
    case reopenLastClosed
    case restoreArchivedTab(tabId: TabId)
    case deleteArchivedTab(tabId: TabId)
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
    case moveFolderToRoot(spaceId: SpaceId, folderId: FolderId, beforeFolderId: FolderId?)
    case reorderFolder(spaceId: SpaceId, folderId: FolderId, parentFolderId: FolderId?, beforeFolderId: FolderId?)
    case moveTabToFolder(spaceId: SpaceId, folderId: FolderId, tabId: TabId)
    case reorderTabInFolder(spaceId: SpaceId, folderId: FolderId, tabId: TabId, to: Int)
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
    case createBoost(urlPattern: UrlPattern)
    case updateBoost(boostId: BoostId, changes: BoostUpdate)
    case toggleBoost(boostId: BoostId, enabled: Bool)
    case deleteBoost(boostId: BoostId)

    // CSS Mods
    case installCssMod(name: String, css: String, description: String?, author: String?, version: String?, homepage: String?, sourceUrl: String?)
    case toggleCssMod(modId: String)
    case uninstallCssMod(modId: String)
    case updateCssMod(modId: String, css: String)

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
    case tabUrlUpdated(tabId: TabId, url: MahoUrl)
    case tabLoadingChanged(tabId: TabId, isLoading: Bool)
    case tabNavigationStateChanged(tabId: TabId, canGoBack: Bool, canGoForward: Bool)
    case tabSecurityChanged(tabId: TabId, isSecure: Bool)

    // History
    case searchHistory(query: String, limit: Int)
    case clearHistory
    case deleteHistoryEntry(entryId: String)

    // Bookmarks
    case addBookmark(url: MahoUrl, title: String, folderId: String?)
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
    case restoreDownloadName(downloadId: String)

    // Browsing
    case togglePiP(tabId: TabId)
    case toggleDevTools(tabId: TabId)
    case printPage(tabId: TabId)
    case viewSource(tabId: TabId)

    // Lifecycle
    case appLaunched
    case appWillTerminate
    case memoryWarning(level: MemoryPressureLevel)

    // Boosts settings
    case toggleGlobalBoosts
    case toggleJsBoosts

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
    case updateProfile(profileId: ProfileId, name: String?, avatarColor: String?, downloadPath: String?, archiveTimeoutHours: Double??)
    case switchProfile(profileId: ProfileId)

    // Passwords
    case searchPasswords(query: String)
    case deletePassword(passwordId: String)
    case addPassword(domain: String, username: String, password: String? = nil)

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

    // LLM Bridge
    case llmResult(requestId: String, result: String)
    case llmError(requestId: String, error: String)
    case requestPagePreview(url: String)
    case requestTidyTabs(spaceId: SpaceId)
    case applyTidyTabs(spaceId: SpaceId)

    // Skills
    case requestSkillsList
    case useSkill(skillId: String, userInput: String, pageContext: String)

    private enum CodingKeys: String, CodingKey {
        case kind
        case tabId = "tab_id"
        case url
        case spaceId = "space_id"
        case parentId = "parent_id"
        case targetSpace = "target_space"
        case position
        case newParentId = "new_parent_id"
        case favicon
        case from, to
        case name
        case color
        case profileId = "profile_id"
        case folderId = "folder_id"
        case targetFolderId = "target_folder_id"
        case beforeFolderId = "before_folder_id"
        case beforeTabId = "before_tab_id"
        case parentFolderId = "parent_folder_id"
        case targetSpaceId = "target_space_id"
        case newIndex = "new_index"
        case text, mode, index, key, action, query, engine, id
        case notificationId = "notification_id"
        case actionId = "action_id"
        case origin, allowed
        case tabIds = "tab_ids"
        case orientation
        case paneId = "pane_id"
        case ratio
        case urlPattern = "url_pattern"
        case boostId = "boost_id"
        case changes
        case enabled
        case modId = "mod_id"
        case css
        case description
        case author
        case version
        case homepage
        case sourceUrl = "source_url"
        case linkedTab = "linked_tab"
        case content
        case noteId = "note_id"
        case visible, width, size, focused
        case title
        case isLoading = "is_loading"
        case canGoBack = "can_go_back"
        case canGoForward = "can_go_forward"
        case isSecure = "is_secure"
        case limit
        case entryId = "entry_id"
        case bookmarkId = "bookmark_id"
        case permission
        case zoomLevel = "zoom_level"
        case downloadId = "download_id"
        case level
        case extensionId = "extension_id"
        case email
        case displayName = "display_name"
        case accessToken = "access_token"
        case userId = "user_id"
        case deviceId = "device_id"
        case rule
        case ruleId = "rule_id"
        case behavior
        case icon
        case avatarColor = "avatar_color"
        case downloadPath = "download_path"
        case archiveTimeoutHours = "archive_timeout_hours"
        case passwordId = "password_id"
        case domain, username, password
        case address, payment
        case itemId = "item_id"
        case isPinned = "is_pinned"
        case requestId = "request_id"
        case result
        case error
        case skillId = "skill_id"
        case userInput = "user_input"
        case pageContext = "page_context"
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)

        switch kind {
        // Navigation
        case "navigate_to":
            self = .navigateTo(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                url: try container.decode(MahoUrl.self, forKey: .url))
        case "go_back":
            self = .goBack(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "go_forward":
            self = .goForward(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "reload":
            self = .reload(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "stop":
            self = .stop(tabId: try container.decode(TabId.self, forKey: .tabId))

        // Tab management
        case "create_tab":
            self = .createTab(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                url: try container.decodeIfPresent(MahoUrl.self, forKey: .url),
                parentId: try container.decodeIfPresent(TabId.self, forKey: .parentId))
        case "close_tab":
            self = .closeTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "activate_tab":
            self = .activateTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "duplicate_tab":
            self = .duplicateTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "pin_tab":
            self = .pinTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "unpin_tab":
            self = .unpinTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "favorite_tab":
            self = .favoriteTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "unfavorite_tab":
            self = .unfavoriteTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "reorder_favorite":
            self = .reorderFavorite(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                newIndex: try container.decode(Int.self, forKey: .newIndex))
        case "mute_tab":
            self = .muteTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "unmute_tab":
            self = .unmuteTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "freeze_tab":
            self = .freezeTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "unfreeze_tab":
            self = .unfreezeTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "move_tab":
            self = .moveTab(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                targetSpace: try container.decode(SpaceId.self, forKey: .targetSpace),
                position: try container.decode(Int.self, forKey: .position))
        case "set_tab_parent":
            self = .setTabParent(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                newParentId: try container.decodeIfPresent(TabId.self, forKey: .newParentId))
        case "tab_favicon_updated":
            self = .tabFaviconUpdated(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                favicon: try container.decodeIfPresent(ImageData.self, forKey: .favicon))
        case "reorder_tab":
            self = .reorderTab(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                beforeTabId: try container.decodeIfPresent(TabId.self, forKey: .beforeTabId))
        case "close_other_tabs":
            self = .closeOtherTabs(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                tabId: try container.decode(TabId.self, forKey: .tabId))
        case "close_tabs_to_right":
            self = .closeTabsToRight(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                tabId: try container.decode(TabId.self, forKey: .tabId))
        case "close_tabs_to_left":
            self = .closeTabsToLeft(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                tabId: try container.decode(TabId.self, forKey: .tabId))
        case "reopen_last_closed":
            self = .reopenLastClosed
        case "restore_archived_tab":
            self = .restoreArchivedTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "delete_archived_tab":
            self = .deleteArchivedTab(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "reset_pinned_tab":
            self = .resetPinnedTab(tabId: try container.decode(TabId.self, forKey: .tabId))

        // Space management
        case "create_space":
            self = .createSpace(
                name: try container.decode(String.self, forKey: .name),
                color: try container.decode(SpaceColor.self, forKey: .color),
                profileId: try container.decode(ProfileId.self, forKey: .profileId))
        case "delete_space":
            self = .deleteSpace(spaceId: try container.decode(SpaceId.self, forKey: .spaceId))
        case "export_space_into_folder":
            self = .exportSpaceIntoFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                targetSpaceId: try container.decode(SpaceId.self, forKey: .targetSpaceId))
        case "activate_space":
            self = .activateSpace(spaceId: try container.decode(SpaceId.self, forKey: .spaceId))
        case "rename_space":
            self = .renameSpace(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                name: try container.decode(String.self, forKey: .name))
        case "recolor_space":
            self = .recolorSpace(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                color: try container.decode(SpaceColor.self, forKey: .color))
        case "reorder_space":
            self = .reorderSpace(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                from: try container.decode(Int.self, forKey: .from),
                to: try container.decode(Int.self, forKey: .to))

        // Folder management
        case "create_folder":
            self = .createFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                name: try container.decode(String.self, forKey: .name),
                isPinned: try container.decodeIfPresent(Bool.self, forKey: .isPinned) ?? false)
        case "rename_folder":
            self = .renameFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId),
                name: try container.decode(String.self, forKey: .name))
        case "delete_folder":
            self = .deleteFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId))
        case "move_folder_into_folder":
            self = .moveFolderIntoFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId),
                targetFolderId: try container.decode(FolderId.self, forKey: .targetFolderId))
        case "move_folder_to_root":
            self = .moveFolderToRoot(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId),
                beforeFolderId: try container.decodeIfPresent(FolderId.self, forKey: .beforeFolderId))
        case "reorder_folder":
            self = .reorderFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId),
                parentFolderId: try container.decodeIfPresent(FolderId.self, forKey: .parentFolderId),
                beforeFolderId: try container.decodeIfPresent(FolderId.self, forKey: .beforeFolderId))
        case "move_tab_to_folder":
            self = .moveTabToFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId),
                tabId: try container.decode(TabId.self, forKey: .tabId))
        case "reorder_tab_in_folder":
            self = .reorderTabInFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId),
                tabId: try container.decode(TabId.self, forKey: .tabId),
                to: try container.decode(Int.self, forKey: .to))
        case "remove_tab_from_folder":
            self = .removeTabFromFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId),
                tabId: try container.decode(TabId.self, forKey: .tabId))
        case "toggle_folder_expanded":
            self = .toggleFolderExpanded(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId))
        case "pin_folder":
            self = .pinFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId))
        case "unpin_folder":
            self = .unpinFolder(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folderId: try container.decode(FolderId.self, forKey: .folderId))

        // Command bar
        case "command_bar_opened":
            self = .commandBarOpened
        case "command_bar_closed":
            self = .commandBarClosed
        case "command_bar_query":
            self = .commandBarQuery(
                text: try container.decode(String.self, forKey: .text),
                mode: try container.decodeIfPresent(String.self, forKey: .mode))
        case "command_bar_select":
            self = .commandBarSelect(
                index: try container.decode(Int.self, forKey: .index),
                key: try container.decode(String.self, forKey: .key))
        case "command_bar_action":
            self = .commandBarAction(action: try container.decode(QuickAction.self, forKey: .action))
        case "save_search":
            self = .saveSearch(query: try container.decode(String.self, forKey: .query))
        case "add_search_engine":
            self = .addSearchEngine(engine: try container.decode(SearchEngineModel.self, forKey: .engine))
        case "remove_search_engine":
            self = .removeSearchEngine(id: try container.decode(String.self, forKey: .id))
        case "set_default_search_engine":
            self = .setDefaultSearchEngine(id: try container.decode(String.self, forKey: .id))

        // Notification management
        case "dismiss_notification":
            self = .dismissNotification(notificationId: try container.decode(String.self, forKey: .notificationId))
        case "dismiss_all_notifications":
            self = .dismissAllNotifications
        case "notification_action":
            self = .notificationAction(
                notificationId: try container.decode(String.self, forKey: .notificationId),
                actionId: try container.decode(String.self, forKey: .actionId))
        case "set_notification_filter":
            self = .setNotificationFilter(
                origin: try container.decode(String.self, forKey: .origin),
                allowed: try container.decode(Bool.self, forKey: .allowed))

        // Split view
        case "create_split":
            self = .createSplit(
                tabIds: try container.decode([TabId].self, forKey: .tabIds),
                orientation: try container.decode(Orientation.self, forKey: .orientation))
        case "remove_split":
            self = .removeSplit(paneId: try container.decode(PaneId.self, forKey: .paneId))
        case "resize_split":
            self = .resizeSplit(
                paneId: try container.decode(PaneId.self, forKey: .paneId),
                ratio: try container.decode(Double.self, forKey: .ratio))

        // Boosts
        case "create_boost":
            self = .createBoost(urlPattern: try container.decode(UrlPattern.self, forKey: .urlPattern))
        case "update_boost":
            self = .updateBoost(
                boostId: try container.decode(BoostId.self, forKey: .boostId),
                changes: try container.decode(BoostUpdate.self, forKey: .changes))
        case "toggle_boost":
            self = .toggleBoost(
                boostId: try container.decode(BoostId.self, forKey: .boostId),
                enabled: try container.decode(Bool.self, forKey: .enabled))
        case "delete_boost":
            self = .deleteBoost(boostId: try container.decode(BoostId.self, forKey: .boostId))

        // CSS Mods
        case "install_css_mod":
            self = .installCssMod(
                name: try container.decode(String.self, forKey: .name),
                css: try container.decode(String.self, forKey: .css),
                description: try container.decodeIfPresent(String.self, forKey: .description),
                author: try container.decodeIfPresent(String.self, forKey: .author),
                version: try container.decodeIfPresent(String.self, forKey: .version),
                homepage: try container.decodeIfPresent(String.self, forKey: .homepage),
                sourceUrl: try container.decodeIfPresent(String.self, forKey: .sourceUrl))
        case "toggle_css_mod":
            self = .toggleCssMod(modId: try container.decode(String.self, forKey: .modId))
        case "uninstall_css_mod":
            self = .uninstallCssMod(modId: try container.decode(String.self, forKey: .modId))
        case "update_css_mod":
            self = .updateCssMod(
                modId: try container.decode(String.self, forKey: .modId),
                css: try container.decode(String.self, forKey: .css))

        // Content Blocker
        case "toggle_content_blocker":
            self = .toggleContentBlocker(enabled: try container.decode(Bool.self, forKey: .enabled))
        case "toggle_popup_blocking":
            self = .togglePopupBlocking(enabled: try container.decode(Bool.self, forKey: .enabled))
        case "add_filter_list":
            self = .addFilterList(
                id: try container.decode(String.self, forKey: .id),
                name: try container.decode(String.self, forKey: .name),
                url: try container.decode(String.self, forKey: .url))
        case "remove_filter_list":
            self = .removeFilterList(id: try container.decode(String.self, forKey: .id))
        case "toggle_filter_list":
            self = .toggleFilterList(
                id: try container.decode(String.self, forKey: .id),
                enabled: try container.decode(Bool.self, forKey: .enabled))

        // Notes
        case "create_note":
            self = .createNote(
                linkedTab: try container.decodeIfPresent(TabId.self, forKey: .linkedTab),
                content: try container.decode(String.self, forKey: .content))
        case "update_note":
            self = .updateNote(
                noteId: try container.decode(NoteId.self, forKey: .noteId),
                content: try container.decode(String.self, forKey: .content))
        case "delete_note":
            self = .deleteNote(noteId: try container.decode(NoteId.self, forKey: .noteId))

        // Settings
        case "update_settings":
            self = .updateSettings(changes: try container.decode(SettingsUpdate.self, forKey: .changes))

        // Window/sidebar
        case "sidebar_toggled":
            self = .sidebarToggled(visible: try container.decode(Bool.self, forKey: .visible))
        case "sidebar_resized":
            self = .sidebarResized(width: try container.decode(Double.self, forKey: .width))
        case "window_resized":
            self = .windowResized(size: try container.decode(Size.self, forKey: .size))
        case "window_focus_changed":
            self = .windowFocusChanged(focused: try container.decode(Bool.self, forKey: .focused))

        // Web content state sync
        case "tab_title_updated":
            self = .tabTitleUpdated(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                title: try container.decode(String.self, forKey: .title))
        case "tab_url_updated":
            self = .tabUrlUpdated(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                url: try container.decode(MahoUrl.self, forKey: .url))
        case "tab_loading_changed":
            self = .tabLoadingChanged(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                isLoading: try container.decode(Bool.self, forKey: .isLoading))
        case "tab_navigation_state_changed":
            self = .tabNavigationStateChanged(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                canGoBack: try container.decode(Bool.self, forKey: .canGoBack),
                canGoForward: try container.decode(Bool.self, forKey: .canGoForward))
        case "tab_security_changed":
            self = .tabSecurityChanged(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                isSecure: try container.decode(Bool.self, forKey: .isSecure))

        // History
        case "search_history":
            self = .searchHistory(
                query: try container.decode(String.self, forKey: .query),
                limit: try container.decode(Int.self, forKey: .limit))
        case "clear_history":
            self = .clearHistory
        case "delete_history_entry":
            self = .deleteHistoryEntry(entryId: try container.decode(String.self, forKey: .entryId))

        // Bookmarks
        case "add_bookmark":
            self = .addBookmark(
                url: try container.decode(MahoUrl.self, forKey: .url),
                title: try container.decode(String.self, forKey: .title),
                folderId: try container.decodeIfPresent(String.self, forKey: .folderId))
        case "remove_bookmark":
            self = .removeBookmark(bookmarkId: try container.decode(String.self, forKey: .bookmarkId))
        case "move_bookmark":
            self = .moveBookmark(
                bookmarkId: try container.decode(String.self, forKey: .bookmarkId),
                folderId: try container.decodeIfPresent(String.self, forKey: .folderId))
        case "search_bookmarks":
            self = .searchBookmarks(query: try container.decode(String.self, forKey: .query))

        // Permissions
        case "grant_permission":
            self = .grantPermission(
                origin: try container.decode(String.self, forKey: .origin),
                permission: try container.decode(String.self, forKey: .permission))
        case "revoke_permission":
            self = .revokePermission(
                origin: try container.decode(String.self, forKey: .origin),
                permission: try container.decode(String.self, forKey: .permission))
        case "query_permission":
            self = .queryPermission(
                origin: try container.decode(String.self, forKey: .origin),
                permission: try container.decode(String.self, forKey: .permission))

        // Zoom
        case "set_zoom":
            self = .setZoom(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                zoomLevel: try container.decode(Double.self, forKey: .zoomLevel))
        case "reset_zoom":
            self = .resetZoom(tabId: try container.decode(TabId.self, forKey: .tabId))

        // Downloads
        case "pause_download":
            self = .pauseDownload(downloadId: try container.decode(String.self, forKey: .downloadId))
        case "resume_download":
            self = .resumeDownload(downloadId: try container.decode(String.self, forKey: .downloadId))
        case "cancel_download":
            self = .cancelDownload(downloadId: try container.decode(String.self, forKey: .downloadId))
        case "remove_download":
            self = .removeDownload(downloadId: try container.decode(String.self, forKey: .downloadId))
        case "restore_download_name":
            self = .restoreDownloadName(downloadId: try container.decode(String.self, forKey: .downloadId))

        // Browsing
        case "toggle_pi_p":
            self = .togglePiP(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "toggle_dev_tools":
            self = .toggleDevTools(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "print_page":
            self = .printPage(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "view_source":
            self = .viewSource(tabId: try container.decode(TabId.self, forKey: .tabId))

        // Lifecycle
        case "app_launched":
            self = .appLaunched
        case "app_will_terminate":
            self = .appWillTerminate
        case "memory_warning":
            self = .memoryWarning(level: try container.decode(MemoryPressureLevel.self, forKey: .level))

        // Boosts settings
        case "toggle_global_boosts":
            self = .toggleGlobalBoosts
        case "toggle_js_boosts":
            self = .toggleJsBoosts

        // Extensions management
        case "toggle_extension":
            self = .toggleExtension(extensionId: try container.decode(String.self, forKey: .extensionId))
        case "remove_extension":
            self = .removeExtension(extensionId: try container.decode(String.self, forKey: .extensionId))

        // Account & Sync
        case "sign_in":
            self = .signIn(
                email: try container.decode(String.self, forKey: .email),
                displayName: try container.decodeIfPresent(String.self, forKey: .displayName),
                password: try container.decodeIfPresent(String.self, forKey: .password),
                accessToken: try container.decodeIfPresent(String.self, forKey: .accessToken),
                userId: try container.decodeIfPresent(String.self, forKey: .userId),
                deviceId: try container.decodeIfPresent(String.self, forKey: .deviceId))
        case "sign_out":
            self = .signOut
        case "toggle_sync":
            self = .toggleSync

        // Air traffic control
        case "create_traffic_rule":
            self = .createTrafficRule(rule: try container.decode(TrafficRule.self, forKey: .rule))
        case "delete_traffic_rule":
            self = .deleteTrafficRule(ruleId: try container.decode(String.self, forKey: .ruleId))
        case "update_traffic_rule":
            self = .updateTrafficRule(rule: try container.decode(TrafficRule.self, forKey: .rule))
        case "set_default_link_behavior":
            self = .setDefaultLinkBehavior(behavior: try container.decode(DefaultLinkBehavior.self, forKey: .behavior))

        // Space settings
        case "update_space_config":
            self = .updateSpaceConfig(changes: try container.decode(SpaceConfigUpdate.self, forKey: .changes))

        // Profile management
        case "create_profile":
            self = .createProfile(name: try container.decode(String.self, forKey: .name))
        case "delete_profile":
            self = .deleteProfile(profileId: try container.decode(ProfileId.self, forKey: .profileId))
        case "update_profile":
            self = .updateProfile(
                profileId: try container.decode(ProfileId.self, forKey: .profileId),
                name: try container.decodeIfPresent(String.self, forKey: .name),
                avatarColor: try container.decodeIfPresent(String.self, forKey: .avatarColor),
                downloadPath: try container.decodeIfPresent(String.self, forKey: .downloadPath),
                archiveTimeoutHours: try container.decodeIfPresent(Double?.self, forKey: .archiveTimeoutHours))
        case "switch_profile":
            self = .switchProfile(profileId: try container.decode(ProfileId.self, forKey: .profileId))

        // Passwords
        case "search_passwords":
            self = .searchPasswords(query: try container.decode(String.self, forKey: .query))
        case "delete_password":
            self = .deletePassword(passwordId: try container.decode(String.self, forKey: .passwordId))
        case "add_password":
            self = .addPassword(
                domain: try container.decode(String.self, forKey: .domain),
                username: try container.decode(String.self, forKey: .username),
                password: try container.decodeIfPresent(String.self, forKey: .password))

        // Autofill
        case "add_autofill_address":
            self = .addAutofillAddress(address: try container.decode(AutofillAddress.self, forKey: .address))
        case "delete_autofill_address":
            self = .deleteAutofillAddress(id: try container.decode(String.self, forKey: .id))
        case "add_autofill_payment":
            self = .addAutofillPayment(payment: try container.decode(AutofillPayment.self, forKey: .payment))
        case "delete_autofill_payment":
            self = .deleteAutofillPayment(id: try container.decode(String.self, forKey: .id))

        case "reset_settings":
            self = .resetSettings

        // Reading List
        case "add_to_reading_list":
            self = .addToReadingList(
                url: try container.decode(String.self, forKey: .url),
                title: try container.decode(String.self, forKey: .title))
        case "remove_from_reading_list":
            self = .removeFromReadingList(itemId: try container.decode(String.self, forKey: .itemId))
        case "mark_reading_list_item_read":
            self = .markReadingListItemRead(itemId: try container.decode(String.self, forKey: .itemId))
        case "mark_reading_list_item_unread":
            self = .markReadingListItemUnread(itemId: try container.decode(String.self, forKey: .itemId))

        case "llm_result":
            self = .llmResult(
                requestId: try container.decode(String.self, forKey: .requestId),
                result: try container.decode(String.self, forKey: .result))
        case "llm_error":
            self = .llmError(
                requestId: try container.decode(String.self, forKey: .requestId),
                error: try container.decode(String.self, forKey: .error))
        case "request_page_preview":
            self = .requestPagePreview(url: try container.decode(String.self, forKey: .url))
        case "request_tidy_tabs":
            self = .requestTidyTabs(spaceId: try container.decode(SpaceId.self, forKey: .spaceId))
        case "apply_tidy_tabs":
            self = .applyTidyTabs(spaceId: try container.decode(SpaceId.self, forKey: .spaceId))

        case "request_skills_list":
            self = .requestSkillsList
        case "use_skill":
            self = .useSkill(
                skillId: try container.decode(String.self, forKey: .skillId),
                userInput: try container.decode(String.self, forKey: .userInput),
                pageContext: try container.decode(String.self, forKey: .pageContext))

        default:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown ShellEvent kind: \(kind)"
            )
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)

        switch self {
        case .navigateTo(let tabId, let url):
            try container.encode("navigate_to", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(url, forKey: .url)
        case .goBack(let tabId):
            try container.encode("go_back", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .goForward(let tabId):
            try container.encode("go_forward", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .reload(let tabId):
            try container.encode("reload", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .stop(let tabId):
            try container.encode("stop", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)

        case .createTab(let spaceId, let url, let parentId):
            try container.encode("create_tab", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encodeIfPresent(url, forKey: .url)
            try container.encodeIfPresent(parentId, forKey: .parentId)
        case .closeTab(let tabId):
            try container.encode("close_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .activateTab(let tabId):
            try container.encode("activate_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .duplicateTab(let tabId):
            try container.encode("duplicate_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .pinTab(let tabId):
            try container.encode("pin_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .unpinTab(let tabId):
            try container.encode("unpin_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .favoriteTab(let tabId):
            try container.encode("favorite_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .unfavoriteTab(let tabId):
            try container.encode("unfavorite_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .reorderFavorite(let tabId, let newIndex):
            try container.encode("reorder_favorite", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(newIndex, forKey: .newIndex)
        case .muteTab(let tabId):
            try container.encode("mute_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .unmuteTab(let tabId):
            try container.encode("unmute_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .freezeTab(let tabId):
            try container.encode("freeze_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .unfreezeTab(let tabId):
            try container.encode("unfreeze_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .moveTab(let tabId, let targetSpace, let position):
            try container.encode("move_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(targetSpace, forKey: .targetSpace)
            try container.encode(position, forKey: .position)
        case .setTabParent(let tabId, let newParentId):
            try container.encode("set_tab_parent", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encodeIfPresent(newParentId, forKey: .newParentId)
        case .tabFaviconUpdated(let tabId, let favicon):
            try container.encode("tab_favicon_updated", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encodeIfPresent(favicon, forKey: .favicon)
        case .reorderTab(let tabId, let beforeTabId):
            try container.encode("reorder_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encodeIfPresent(beforeTabId, forKey: .beforeTabId)
        case .closeOtherTabs(let spaceId, let tabId):
            try container.encode("close_other_tabs", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(tabId, forKey: .tabId)
        case .closeTabsToRight(let spaceId, let tabId):
            try container.encode("close_tabs_to_right", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(tabId, forKey: .tabId)
        case .closeTabsToLeft(let spaceId, let tabId):
            try container.encode("close_tabs_to_left", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(tabId, forKey: .tabId)
        case .reopenLastClosed:
            try container.encode("reopen_last_closed", forKey: .kind)
        case .restoreArchivedTab(let tabId):
            try container.encode("restore_archived_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .deleteArchivedTab(let tabId):
            try container.encode("delete_archived_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .resetPinnedTab(let tabId):
            try container.encode("reset_pinned_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)

        case .createSpace(let name, let color, let profileId):
            try container.encode("create_space", forKey: .kind)
            try container.encode(name, forKey: .name)
            try container.encode(color, forKey: .color)
            try container.encode(profileId, forKey: .profileId)
        case .deleteSpace(let spaceId):
            try container.encode("delete_space", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
        case .exportSpaceIntoFolder(let spaceId, let targetSpaceId):
            try container.encode("export_space_into_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(targetSpaceId, forKey: .targetSpaceId)
        case .activateSpace(let spaceId):
            try container.encode("activate_space", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
        case .renameSpace(let spaceId, let name):
            try container.encode("rename_space", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(name, forKey: .name)
        case .recolorSpace(let spaceId, let color):
            try container.encode("recolor_space", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(color, forKey: .color)
        case .reorderSpace(let spaceId, let from, let to):
            try container.encode("reorder_space", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(from, forKey: .from)
            try container.encode(to, forKey: .to)

        case .createFolder(let spaceId, let name, let isPinned):
            try container.encode("create_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(name, forKey: .name)
            try container.encode(isPinned, forKey: .isPinned)
        case .renameFolder(let spaceId, let folderId, let name):
            try container.encode("rename_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
            try container.encode(name, forKey: .name)
        case .deleteFolder(let spaceId, let folderId):
            try container.encode("delete_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
        case .moveFolderIntoFolder(let spaceId, let folderId, let targetFolderId):
            try container.encode("move_folder_into_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
            try container.encode(targetFolderId, forKey: .targetFolderId)
        case .moveFolderToRoot(let spaceId, let folderId, let beforeFolderId):
            try container.encode("move_folder_to_root", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
            try container.encodeIfPresent(beforeFolderId, forKey: .beforeFolderId)
        case .reorderFolder(let spaceId, let folderId, let parentFolderId, let beforeFolderId):
            try container.encode("reorder_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
            try container.encodeIfPresent(parentFolderId, forKey: .parentFolderId)
            try container.encodeIfPresent(beforeFolderId, forKey: .beforeFolderId)
        case .moveTabToFolder(let spaceId, let folderId, let tabId):
            try container.encode("move_tab_to_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
            try container.encode(tabId, forKey: .tabId)
        case .reorderTabInFolder(let spaceId, let folderId, let tabId, let to):
            try container.encode("reorder_tab_in_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(to, forKey: .to)
        case .removeTabFromFolder(let spaceId, let folderId, let tabId):
            try container.encode("remove_tab_from_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
            try container.encode(tabId, forKey: .tabId)
        case .toggleFolderExpanded(let spaceId, let folderId):
            try container.encode("toggle_folder_expanded", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
        case .pinFolder(let spaceId, let folderId):
            try container.encode("pin_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)
        case .unpinFolder(let spaceId, let folderId):
            try container.encode("unpin_folder", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folderId, forKey: .folderId)

        case .commandBarOpened:
            try container.encode("command_bar_opened", forKey: .kind)
        case .commandBarClosed:
            try container.encode("command_bar_closed", forKey: .kind)
        case .commandBarQuery(let text, let mode):
            try container.encode("command_bar_query", forKey: .kind)
            try container.encode(text, forKey: .text)
            try container.encodeIfPresent(mode, forKey: .mode)
        case .commandBarSelect(let index, let key):
            try container.encode("command_bar_select", forKey: .kind)
            try container.encode(index, forKey: .index)
            try container.encode(key, forKey: .key)
        case .commandBarAction(let action):
            try container.encode("command_bar_action", forKey: .kind)
            try container.encode(action, forKey: .action)
        case .saveSearch(let query):
            try container.encode("save_search", forKey: .kind)
            try container.encode(query, forKey: .query)
        case .addSearchEngine(let engine):
            try container.encode("add_search_engine", forKey: .kind)
            try container.encode(engine, forKey: .engine)
        case .removeSearchEngine(let id):
            try container.encode("remove_search_engine", forKey: .kind)
            try container.encode(id, forKey: .id)
        case .setDefaultSearchEngine(let id):
            try container.encode("set_default_search_engine", forKey: .kind)
            try container.encode(id, forKey: .id)

        case .dismissNotification(let notificationId):
            try container.encode("dismiss_notification", forKey: .kind)
            try container.encode(notificationId, forKey: .notificationId)
        case .dismissAllNotifications:
            try container.encode("dismiss_all_notifications", forKey: .kind)
        case .notificationAction(let notificationId, let actionId):
            try container.encode("notification_action", forKey: .kind)
            try container.encode(notificationId, forKey: .notificationId)
            try container.encode(actionId, forKey: .actionId)
        case .setNotificationFilter(let origin, let allowed):
            try container.encode("set_notification_filter", forKey: .kind)
            try container.encode(origin, forKey: .origin)
            try container.encode(allowed, forKey: .allowed)

        case .createSplit(let tabIds, let orientation):
            try container.encode("create_split", forKey: .kind)
            try container.encode(tabIds, forKey: .tabIds)
            try container.encode(orientation, forKey: .orientation)
        case .removeSplit(let paneId):
            try container.encode("remove_split", forKey: .kind)
            try container.encode(paneId, forKey: .paneId)
        case .resizeSplit(let paneId, let ratio):
            try container.encode("resize_split", forKey: .kind)
            try container.encode(paneId, forKey: .paneId)
            try container.encode(ratio, forKey: .ratio)

        case .createBoost(let urlPattern):
            try container.encode("create_boost", forKey: .kind)
            try container.encode(urlPattern, forKey: .urlPattern)
        case .updateBoost(let boostId, let changes):
            try container.encode("update_boost", forKey: .kind)
            try container.encode(boostId, forKey: .boostId)
            try container.encode(changes, forKey: .changes)
        case .toggleBoost(let boostId, let enabled):
            try container.encode("toggle_boost", forKey: .kind)
            try container.encode(boostId, forKey: .boostId)
            try container.encode(enabled, forKey: .enabled)
        case .deleteBoost(let boostId):
            try container.encode("delete_boost", forKey: .kind)
            try container.encode(boostId, forKey: .boostId)

        case .installCssMod(let name, let css, let description, let author, let version, let homepage, let sourceUrl):
            try container.encode("install_css_mod", forKey: .kind)
            try container.encode(name, forKey: .name)
            try container.encode(css, forKey: .css)
            try container.encodeIfPresent(description, forKey: .description)
            try container.encodeIfPresent(author, forKey: .author)
            try container.encodeIfPresent(version, forKey: .version)
            try container.encodeIfPresent(homepage, forKey: .homepage)
            try container.encodeIfPresent(sourceUrl, forKey: .sourceUrl)
        case .toggleCssMod(let modId):
            try container.encode("toggle_css_mod", forKey: .kind)
            try container.encode(modId, forKey: .modId)
        case .uninstallCssMod(let modId):
            try container.encode("uninstall_css_mod", forKey: .kind)
            try container.encode(modId, forKey: .modId)
        case .updateCssMod(let modId, let css):
            try container.encode("update_css_mod", forKey: .kind)
            try container.encode(modId, forKey: .modId)
            try container.encode(css, forKey: .css)

        case .toggleContentBlocker(let enabled):
            try container.encode("toggle_content_blocker", forKey: .kind)
            try container.encode(enabled, forKey: .enabled)
        case .togglePopupBlocking(let enabled):
            try container.encode("toggle_popup_blocking", forKey: .kind)
            try container.encode(enabled, forKey: .enabled)
        case .addFilterList(let id, let name, let url):
            try container.encode("add_filter_list", forKey: .kind)
            try container.encode(id, forKey: .id)
            try container.encode(name, forKey: .name)
            try container.encode(url, forKey: .url)
        case .removeFilterList(let id):
            try container.encode("remove_filter_list", forKey: .kind)
            try container.encode(id, forKey: .id)
        case .toggleFilterList(let id, let enabled):
            try container.encode("toggle_filter_list", forKey: .kind)
            try container.encode(id, forKey: .id)
            try container.encode(enabled, forKey: .enabled)

        case .createNote(let linkedTab, let content):
            try container.encode("create_note", forKey: .kind)
            try container.encodeIfPresent(linkedTab, forKey: .linkedTab)
            try container.encode(content, forKey: .content)
        case .updateNote(let noteId, let content):
            try container.encode("update_note", forKey: .kind)
            try container.encode(noteId, forKey: .noteId)
            try container.encode(content, forKey: .content)
        case .deleteNote(let noteId):
            try container.encode("delete_note", forKey: .kind)
            try container.encode(noteId, forKey: .noteId)

        case .updateSettings(let changes):
            try container.encode("update_settings", forKey: .kind)
            try container.encode(changes, forKey: .changes)

        case .sidebarToggled(let visible):
            try container.encode("sidebar_toggled", forKey: .kind)
            try container.encode(visible, forKey: .visible)
        case .sidebarResized(let width):
            try container.encode("sidebar_resized", forKey: .kind)
            try container.encode(width, forKey: .width)
        case .windowResized(let size):
            try container.encode("window_resized", forKey: .kind)
            try container.encode(size, forKey: .size)
        case .windowFocusChanged(let focused):
            try container.encode("window_focus_changed", forKey: .kind)
            try container.encode(focused, forKey: .focused)

        case .tabTitleUpdated(let tabId, let title):
            try container.encode("tab_title_updated", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(title, forKey: .title)
        case .tabUrlUpdated(let tabId, let url):
            try container.encode("tab_url_updated", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(url, forKey: .url)
        case .tabLoadingChanged(let tabId, let isLoading):
            try container.encode("tab_loading_changed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(isLoading, forKey: .isLoading)
        case .tabNavigationStateChanged(let tabId, let canGoBack, let canGoForward):
            try container.encode("tab_navigation_state_changed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(canGoBack, forKey: .canGoBack)
            try container.encode(canGoForward, forKey: .canGoForward)
        case .tabSecurityChanged(let tabId, let isSecure):
            try container.encode("tab_security_changed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(isSecure, forKey: .isSecure)

        case .searchHistory(let query, let limit):
            try container.encode("search_history", forKey: .kind)
            try container.encode(query, forKey: .query)
            try container.encode(limit, forKey: .limit)
        case .clearHistory:
            try container.encode("clear_history", forKey: .kind)
        case .deleteHistoryEntry(let entryId):
            try container.encode("delete_history_entry", forKey: .kind)
            try container.encode(entryId, forKey: .entryId)

        case .addBookmark(let url, let title, let folderId):
            try container.encode("add_bookmark", forKey: .kind)
            try container.encode(url, forKey: .url)
            try container.encode(title, forKey: .title)
            try container.encodeIfPresent(folderId, forKey: .folderId)
        case .removeBookmark(let bookmarkId):
            try container.encode("remove_bookmark", forKey: .kind)
            try container.encode(bookmarkId, forKey: .bookmarkId)
        case .moveBookmark(let bookmarkId, let folderId):
            try container.encode("move_bookmark", forKey: .kind)
            try container.encode(bookmarkId, forKey: .bookmarkId)
            try container.encodeIfPresent(folderId, forKey: .folderId)
        case .searchBookmarks(let query):
            try container.encode("search_bookmarks", forKey: .kind)
            try container.encode(query, forKey: .query)

        case .grantPermission(let origin, let permission):
            try container.encode("grant_permission", forKey: .kind)
            try container.encode(origin, forKey: .origin)
            try container.encode(permission, forKey: .permission)
        case .revokePermission(let origin, let permission):
            try container.encode("revoke_permission", forKey: .kind)
            try container.encode(origin, forKey: .origin)
            try container.encode(permission, forKey: .permission)
        case .queryPermission(let origin, let permission):
            try container.encode("query_permission", forKey: .kind)
            try container.encode(origin, forKey: .origin)
            try container.encode(permission, forKey: .permission)

        case .setZoom(let tabId, let zoomLevel):
            try container.encode("set_zoom", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(zoomLevel, forKey: .zoomLevel)
        case .resetZoom(let tabId):
            try container.encode("reset_zoom", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)

        case .pauseDownload(let downloadId):
            try container.encode("pause_download", forKey: .kind)
            try container.encode(downloadId, forKey: .downloadId)
        case .resumeDownload(let downloadId):
            try container.encode("resume_download", forKey: .kind)
            try container.encode(downloadId, forKey: .downloadId)
        case .cancelDownload(let downloadId):
            try container.encode("cancel_download", forKey: .kind)
            try container.encode(downloadId, forKey: .downloadId)
        case .removeDownload(let downloadId):
            try container.encode("remove_download", forKey: .kind)
            try container.encode(downloadId, forKey: .downloadId)
        case .restoreDownloadName(let downloadId):
            try container.encode("restore_download_name", forKey: .kind)
            try container.encode(downloadId, forKey: .downloadId)

        case .togglePiP(let tabId):
            try container.encode("toggle_pi_p", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .toggleDevTools(let tabId):
            try container.encode("toggle_dev_tools", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .printPage(let tabId):
            try container.encode("print_page", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .viewSource(let tabId):
            try container.encode("view_source", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)

        case .appLaunched:
            try container.encode("app_launched", forKey: .kind)
        case .appWillTerminate:
            try container.encode("app_will_terminate", forKey: .kind)
        case .memoryWarning(let level):
            try container.encode("memory_warning", forKey: .kind)
            try container.encode(level, forKey: .level)

        case .toggleGlobalBoosts:
            try container.encode("toggle_global_boosts", forKey: .kind)
        case .toggleJsBoosts:
            try container.encode("toggle_js_boosts", forKey: .kind)

        case .toggleExtension(let extensionId):
            try container.encode("toggle_extension", forKey: .kind)
            try container.encode(extensionId, forKey: .extensionId)
        case .removeExtension(let extensionId):
            try container.encode("remove_extension", forKey: .kind)
            try container.encode(extensionId, forKey: .extensionId)

        case .signIn(let email, let displayName, let password, let accessToken, let userId, let deviceId):
            try container.encode("sign_in", forKey: .kind)
            try container.encode(email, forKey: .email)
            try container.encodeIfPresent(displayName, forKey: .displayName)
            try container.encodeIfPresent(password, forKey: .password)
            try container.encodeIfPresent(accessToken, forKey: .accessToken)
            try container.encodeIfPresent(userId, forKey: .userId)
            try container.encodeIfPresent(deviceId, forKey: .deviceId)
        case .signOut:
            try container.encode("sign_out", forKey: .kind)
        case .toggleSync:
            try container.encode("toggle_sync", forKey: .kind)

        case .createTrafficRule(let rule):
            try container.encode("create_traffic_rule", forKey: .kind)
            try container.encode(rule, forKey: .rule)
        case .deleteTrafficRule(let ruleId):
            try container.encode("delete_traffic_rule", forKey: .kind)
            try container.encode(ruleId, forKey: .ruleId)
        case .updateTrafficRule(let rule):
            try container.encode("update_traffic_rule", forKey: .kind)
            try container.encode(rule, forKey: .rule)
        case .setDefaultLinkBehavior(let behavior):
            try container.encode("set_default_link_behavior", forKey: .kind)
            try container.encode(behavior, forKey: .behavior)

        case .updateSpaceConfig(let changes):
            try container.encode("update_space_config", forKey: .kind)
            try container.encode(changes, forKey: .changes)

        case .createProfile(let name):
            try container.encode("create_profile", forKey: .kind)
            try container.encode(name, forKey: .name)
        case .deleteProfile(let profileId):
            try container.encode("delete_profile", forKey: .kind)
            try container.encode(profileId, forKey: .profileId)
        case .updateProfile(let profileId, let name, let avatarColor, let downloadPath, let archiveTimeoutHours):
            try container.encode("update_profile", forKey: .kind)
            try container.encode(profileId, forKey: .profileId)
            try container.encodeIfPresent(name, forKey: .name)
            try container.encodeIfPresent(avatarColor, forKey: .avatarColor)
            try container.encodeIfPresent(downloadPath, forKey: .downloadPath)
            try container.encode(archiveTimeoutHours, forKey: .archiveTimeoutHours)
        case .switchProfile(let profileId):
            try container.encode("switch_profile", forKey: .kind)
            try container.encode(profileId, forKey: .profileId)

        case .searchPasswords(let query):
            try container.encode("search_passwords", forKey: .kind)
            try container.encode(query, forKey: .query)
        case .deletePassword(let passwordId):
            try container.encode("delete_password", forKey: .kind)
            try container.encode(passwordId, forKey: .passwordId)
        case .addPassword(let domain, let username, let password):
            try container.encode("add_password", forKey: .kind)
            try container.encode(domain, forKey: .domain)
            try container.encode(username, forKey: .username)
            try container.encodeIfPresent(password, forKey: .password)

        case .addAutofillAddress(let address):
            try container.encode("add_autofill_address", forKey: .kind)
            try container.encode(address, forKey: .address)
        case .deleteAutofillAddress(let id):
            try container.encode("delete_autofill_address", forKey: .kind)
            try container.encode(id, forKey: .id)
        case .addAutofillPayment(let payment):
            try container.encode("add_autofill_payment", forKey: .kind)
            try container.encode(payment, forKey: .payment)
        case .deleteAutofillPayment(let id):
            try container.encode("delete_autofill_payment", forKey: .kind)
            try container.encode(id, forKey: .id)

        case .resetSettings:
            try container.encode("reset_settings", forKey: .kind)

        case .addToReadingList(let url, let title):
            try container.encode("add_to_reading_list", forKey: .kind)
            try container.encode(url, forKey: .url)
            try container.encode(title, forKey: .title)
        case .removeFromReadingList(let itemId):
            try container.encode("remove_from_reading_list", forKey: .kind)
            try container.encode(itemId, forKey: .itemId)
        case .markReadingListItemRead(let itemId):
            try container.encode("mark_reading_list_item_read", forKey: .kind)
            try container.encode(itemId, forKey: .itemId)
        case .markReadingListItemUnread(let itemId):
            try container.encode("mark_reading_list_item_unread", forKey: .kind)
            try container.encode(itemId, forKey: .itemId)

        case .llmResult(let requestId, let result):
            try container.encode("llm_result", forKey: .kind)
            try container.encode(requestId, forKey: .requestId)
            try container.encode(result, forKey: .result)
        case .llmError(let requestId, let error):
            try container.encode("llm_error", forKey: .kind)
            try container.encode(requestId, forKey: .requestId)
            try container.encode(error, forKey: .error)
        case .requestPagePreview(let url):
            try container.encode("request_page_preview", forKey: .kind)
            try container.encode(url, forKey: .url)
        case .requestTidyTabs(let spaceId):
            try container.encode("request_tidy_tabs", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
        case .applyTidyTabs(let spaceId):
            try container.encode("apply_tidy_tabs", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
        case .requestSkillsList:
            try container.encode("request_skills_list", forKey: .kind)
        case .useSkill(let skillId, let userInput, let pageContext):
            try container.encode("use_skill", forKey: .kind)
            try container.encode(skillId, forKey: .skillId)
            try container.encode(userInput, forKey: .userInput)
            try container.encode(pageContext, forKey: .pageContext)
        }
    }
}
