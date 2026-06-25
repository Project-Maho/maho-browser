import Foundation

enum CoreUpdate: Codable {
    case unknown(kind: String)
    case fullState(state: AppStateSnapshot)
    case tabCreated(tab: TabViewModel)
    case tabUpdated(tabId: TabId, changes: TabStateUpdate)
    case tabClosed(tabId: TabId, animated: Bool)
    case tabOrderChanged(spaceId: SpaceId, order: [TabId])
    case spaceCreated(space: SpaceViewModel)
    case spaceUpdated(spaceId: SpaceId, changes: SpaceUpdate)
    case spaceDeleted(spaceId: SpaceId)
    case spaceRenamed(spaceId: SpaceId, name: String)
    case spaceRecolored(spaceId: SpaceId, color: SpaceColor)
    case spaceReordered(spaceId: SpaceId, from: Int, to: Int)
    case spaceOrderChanged(order: [SpaceId])
    case folderCreated(folder: FolderViewModel)
    case folderUpdated(folderId: FolderId, changes: FolderUpdate)
    case folderDeleted(folderId: FolderId)
    case folderOrderChanged(spaceId: SpaceId)
    case activeSpaceChanged(spaceId: SpaceId)
    case commandBarResults(suggestions: [SuggestionViewModel])
    case recentSearchesUpdated(searches: [String])
    case searchEnginesUpdated(engines: [SearchEngineViewModel])
    case navigationStateChanged(tabId: TabId, url: Url, title: String, canGoBack: Bool, canGoForward: Bool, isLoading: Bool, progress: Double)
    case tabAudioStateChanged(tabId: TabId, isPlaying: Bool)
    case tabFaviconChanged(tabId: TabId, favicon: ImageData?)
    case downloadStarted(download: DownloadViewModel)
    case downloadProgress(downloadId: DownloadId, progress: Double)
    case downloadCompleted(downloadId: DownloadId)
    case showNotification(notification: NotificationViewModel)
    case notificationDismissed(notificationId: String)
    case allNotificationsDismissed
    case notificationList(notifications: [NotificationViewModel])
    case showPermissionRequest(request: PermissionRequest)
    case showFindBar(tabId: TabId)
    case historyResults(entries: [HistoryEntry])
    case bookmarkResults(bookmarks: [BookmarkEntry])
    case zoomChanged(tabId: TabId, zoomLevel: Double)
    case tabPreviewUpdated(tabId: TabId, previewData: String)
    case tabPreviewCaptureRequested(tabId: TabId)
    case permissionResponse(origin: String, permission: String, granted: Bool)
    case printRequested(tabId: TabId)
    case pipToggled(tabId: TabId, active: Bool)
    case contentRulesCompiled(ruleCount: Int)
    case contentBlockerStateChanged(enabled: Bool, popupBlocking: Bool)
    case filterListUpdated(id: String, ruleCount: Int)
    case tabLifecycleChanged(tabId: TabId, state: TabLifecycleState)
    case memoryPressureResponse(action: MemoryAction)
    case syncStateChanged(status: SyncStatus)
    case settingsChanged(settings: Settings)
    case spaceConfigUpdated(spaceId: SpaceId, changes: SpaceConfigUpdate)
    case extensionToggled(extensionId: String, enabled: Bool)
    case extensionRemoved(extensionId: String)
    case accountChanged(account: AccountInfo?)
    case accountSyncStateChanged(state: SyncState)
    case trafficRuleCreated(rule: TrafficRule)
    case trafficRuleDeleted(ruleId: String)
    case profileCreated(profile: ProfileConfig)
    case profileDeleted(profileId: ProfileId, dataStoreId: String?)
    case profileUpdated(profile: ProfileConfig)
    case activeProfileChanged(profileId: ProfileId)
    case error(context: String, error: MahoError)
    case passwordDeleted(passwordId: String)
    case passwordsSearchResult(passwords: [SavedPassword])
    case autofillAddressAdded(address: AutofillAddress)
    case autofillAddressDeleted(id: String)
    case autofillPaymentAdded(payment: AutofillPayment)
    case autofillPaymentDeleted(id: String)
    case openPeekTab(url: Url, sourceTabId: TabId)
    case favoriteLimitReached(max: Int)
    case navigateTab(tabId: TabId, url: Url)
    case tabsMigrated(tabIds: [TabId], fromSpaceId: SpaceId, toSpaceId: SpaceId)

    private enum CodingKeys: String, CodingKey {
        case kind
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: DynamicCodingKey.self)
        let kind = try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "kind")!)

        switch kind {
        case "full_state":
            self = .fullState(state: try container.decode(AppStateSnapshot.self, forKey: DynamicCodingKey(stringValue: "state")!))
        case "tab_created":
            self = .tabCreated(tab: try container.decode(TabViewModel.self, forKey: DynamicCodingKey(stringValue: "tab")!))
        case "tab_updated":
            self = .tabUpdated(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                changes: try container.decode(TabStateUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))
        case "tab_closed":
            self = .tabClosed(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                animated: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "animated")!))
        case "tab_order_changed":
            self = .tabOrderChanged(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                order: try container.decode([TabId].self, forKey: DynamicCodingKey(stringValue: "order")!))
        case "space_created":
            self = .spaceCreated(space: try container.decode(SpaceViewModel.self, forKey: DynamicCodingKey(stringValue: "space")!))
        case "space_updated":
            self = .spaceUpdated(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                changes: try container.decode(SpaceUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))
        case "space_deleted":
            self = .spaceDeleted(spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!))
        case "space_renamed":
            self = .spaceRenamed(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!))
        case "space_recolored":
            self = .spaceRecolored(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                color: try container.decode(SpaceColor.self, forKey: DynamicCodingKey(stringValue: "color")!))
        case "space_reordered":
            self = .spaceReordered(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                from: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "from")!),
                to: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "to")!))
        case "space_order_changed":
            self = .spaceOrderChanged(order: try container.decode([SpaceId].self, forKey: DynamicCodingKey(stringValue: "order")!))
        case "folder_created":
            self = .folderCreated(folder: try container.decode(FolderViewModel.self, forKey: DynamicCodingKey(stringValue: "folder")!))
        case "folder_updated":
            self = .folderUpdated(
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                changes: try container.decode(FolderUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))
        case "folder_deleted":
            self = .folderDeleted(folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "folder_order_changed":
            self = .folderOrderChanged(spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!))
        case "active_space_changed":
            self = .activeSpaceChanged(spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!))
        case "command_bar_results":
            self = .commandBarResults(suggestions: try container.decode([SuggestionViewModel].self, forKey: DynamicCodingKey(stringValue: "suggestions")!))
        case "recent_searches_updated":
            self = .recentSearchesUpdated(searches: try container.decode([String].self, forKey: DynamicCodingKey(stringValue: "searches")!))
        case "search_engines_updated":
            self = .searchEnginesUpdated(engines: try container.decode([SearchEngineViewModel].self, forKey: DynamicCodingKey(stringValue: "engines")!))
        case "navigation_state_changed":
            self = .navigationStateChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!),
                title: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "title")!),
                canGoBack: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "can_go_back")!),
                canGoForward: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "can_go_forward")!),
                isLoading: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "is_loading")!),
                progress: try container.decode(Double.self, forKey: DynamicCodingKey(stringValue: "progress")!))
        case "all_notifications_dismissed":
            self = .allNotificationsDismissed
        case "settings_changed":
            self = .settingsChanged(settings: try container.decode(Settings.self, forKey: DynamicCodingKey(stringValue: "settings")!))
        case "error":
            self = .error(
                context: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "context")!),
                error: try container.decode(MahoError.self, forKey: DynamicCodingKey(stringValue: "error")!))
        case "download_started":
            self = .downloadStarted(download: try container.decode(DownloadViewModel.self, forKey: DynamicCodingKey(stringValue: "download")!))
        case "download_progress":
            self = .downloadProgress(
                downloadId: try container.decode(DownloadId.self, forKey: DynamicCodingKey(stringValue: "download_id")!),
                progress: try container.decode(Double.self, forKey: DynamicCodingKey(stringValue: "progress")!))
        case "download_completed":
            self = .downloadCompleted(downloadId: try container.decode(DownloadId.self, forKey: DynamicCodingKey(stringValue: "download_id")!))
        case "show_notification":
            self = .showNotification(notification: try container.decode(NotificationViewModel.self, forKey: DynamicCodingKey(stringValue: "notification")!))
        case "notification_dismissed":
            self = .notificationDismissed(notificationId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "notification_id")!))
        case "notification_list":
            self = .notificationList(notifications: try container.decode([NotificationViewModel].self, forKey: DynamicCodingKey(stringValue: "notifications")!))
        case "show_permission_request":
            self = .showPermissionRequest(request: try container.decode(PermissionRequest.self, forKey: DynamicCodingKey(stringValue: "request")!))
        case "show_find_bar":
            self = .showFindBar(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "history_results":
            self = .historyResults(entries: try container.decode([HistoryEntry].self, forKey: DynamicCodingKey(stringValue: "entries")!))
        case "bookmark_results":
            self = .bookmarkResults(bookmarks: try container.decode([BookmarkEntry].self, forKey: DynamicCodingKey(stringValue: "bookmarks")!))
        case "zoom_changed":
            self = .zoomChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                zoomLevel: try container.decode(Double.self, forKey: DynamicCodingKey(stringValue: "zoom_level")!))
        case "tab_preview_updated":
            self = .tabPreviewUpdated(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                previewData: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "preview_data")!))
        case "tab_preview_capture_requested":
            self = .tabPreviewCaptureRequested(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "permission_response":
            self = .permissionResponse(
                origin: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "origin")!),
                permission: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "permission")!),
                granted: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "granted")!))
        case "print_requested":
            self = .printRequested(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "pip_toggled":
            self = .pipToggled(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                active: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "active")!))
        case "content_rules_compiled":
            self = .contentRulesCompiled(ruleCount: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "rule_count")!))
        case "content_blocker_state_changed":
            self = .contentBlockerStateChanged(
                enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!),
                popupBlocking: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "popup_blocking")!))
        case "filter_list_updated":
            self = .filterListUpdated(
                id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!),
                ruleCount: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "rule_count")!))
        case "tab_lifecycle_changed":
            self = .tabLifecycleChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                state: try container.decode(TabLifecycleState.self, forKey: DynamicCodingKey(stringValue: "state")!))
        case "memory_pressure_response":
            self = .memoryPressureResponse(action: try container.decode(MemoryAction.self, forKey: DynamicCodingKey(stringValue: "action")!))
        case "sync_state_changed":
            self = .syncStateChanged(status: try container.decode(SyncStatus.self, forKey: DynamicCodingKey(stringValue: "status")!))
        case "space_config_updated":
            self = .spaceConfigUpdated(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                changes: try container.decode(SpaceConfigUpdate.self, forKey: DynamicCodingKey(stringValue: "changes")!))

        case "extension_toggled":
            self = .extensionToggled(
                extensionId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "extension_id")!),
                enabled: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "enabled")!))
        case "extension_removed":
            self = .extensionRemoved(extensionId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "extension_id")!))
        case "account_changed":
            self = .accountChanged(account: try container.decodeIfPresent(AccountInfo.self, forKey: DynamicCodingKey(stringValue: "account")!))
        case "account_sync_state_changed":
            self = .accountSyncStateChanged(state: try container.decode(SyncState.self, forKey: DynamicCodingKey(stringValue: "state")!))
        case "traffic_rule_created":
            self = .trafficRuleCreated(rule: try container.decode(TrafficRule.self, forKey: DynamicCodingKey(stringValue: "rule")!))
        case "traffic_rule_deleted":
            self = .trafficRuleDeleted(ruleId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "rule_id")!))
        case "profile_created":
            self = .profileCreated(profile: try container.decode(ProfileConfig.self, forKey: DynamicCodingKey(stringValue: "profile")!))
        case "profile_deleted":
            self = .profileDeleted(
                profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!),
                dataStoreId: try container.decodeIfPresent(String.self, forKey: DynamicCodingKey(stringValue: "data_store_id")!))
        case "profile_updated":
            self = .profileUpdated(profile: try container.decode(ProfileConfig.self, forKey: DynamicCodingKey(stringValue: "profile")!))
        case "active_profile_changed":
            self = .activeProfileChanged(profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!))
        case "password_deleted":
            self = .passwordDeleted(passwordId: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "password_id")!))
        case "passwords_search_result":
            self = .passwordsSearchResult(passwords: try container.decode([SavedPassword].self, forKey: DynamicCodingKey(stringValue: "passwords")!))
        case "autofill_address_added":
            self = .autofillAddressAdded(address: try container.decode(AutofillAddress.self, forKey: DynamicCodingKey(stringValue: "address")!))
        case "autofill_address_deleted":
            self = .autofillAddressDeleted(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))
        case "autofill_payment_added":
            self = .autofillPaymentAdded(payment: try container.decode(AutofillPayment.self, forKey: DynamicCodingKey(stringValue: "payment")!))
        case "autofill_payment_deleted":
            self = .autofillPaymentDeleted(id: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "id")!))
        case "tab_audio_state_changed":
            self = .tabAudioStateChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                isPlaying: try container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "is_playing")!))
        case "tab_favicon_changed":
            self = .tabFaviconChanged(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                favicon: try container.decodeIfPresent(ImageData.self, forKey: DynamicCodingKey(stringValue: "favicon")!))
        case "open_peek_tab":
            self = .openPeekTab(
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!),
                sourceTabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "source_tab_id")!))
        case "favorite_limit_reached":
            self = .favoriteLimitReached(max: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "max")!))
        case "navigate_tab":
            self = .navigateTab(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!))
        case "tabs_migrated":
            self = .tabsMigrated(
                tabIds: try container.decode([TabId].self, forKey: DynamicCodingKey(stringValue: "tab_ids")!),
                fromSpaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "from_space_id")!),
                toSpaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "to_space_id")!))
        default:
            self = .unknown(kind: kind)
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: DynamicCodingKey.self)
        let _ = container // CoreUpdate is primarily decoded from Rust, encoding is rarely needed
    }
}

struct SpaceConfigUpdate: Codable {
    let spaceId: SpaceId
    let name: String?
    let color: SpaceColor?
    let icon: Patchable<String>
    let profileId: ProfileId?

    private enum CodingKeys: String, CodingKey {
        case spaceId, name, color, icon, profileId
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        spaceId = try container.decode(SpaceId.self, forKey: .spaceId)
        name = try container.decodeIfPresent(String.self, forKey: .name)
        color = try container.decodeIfPresent(SpaceColor.self, forKey: .color)
        icon = try container.decodePatchable(Patchable<String>.self, forKey: .icon)
        profileId = try container.decodeIfPresent(ProfileId.self, forKey: .profileId)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(spaceId, forKey: .spaceId)
        try container.encodeIfPresent(name, forKey: .name)
        try container.encodeIfPresent(color, forKey: .color)
        try container.encodePatchable(icon, forKey: .icon)
        try container.encodeIfPresent(profileId, forKey: .profileId)
    }
}

struct SpaceUpdate: Codable {
    let name: String?
    let color: SpaceColor?
    let tabCount: Int?
    let isActive: Bool?
    let icon: Patchable<String>
    let profileId: ProfileId?
    let profileName: String?
    let orderIndex: Int?

    private enum CodingKeys: String, CodingKey {
        case name, color, tabCount, isActive, icon, profileId, profileName, orderIndex
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        name = try container.decodeIfPresent(String.self, forKey: .name)
        color = try container.decodeIfPresent(SpaceColor.self, forKey: .color)
        tabCount = try container.decodeIfPresent(Int.self, forKey: .tabCount)
        isActive = try container.decodeIfPresent(Bool.self, forKey: .isActive)
        icon = try container.decodePatchable(Patchable<String>.self, forKey: .icon)
        profileId = try container.decodeIfPresent(ProfileId.self, forKey: .profileId)
        profileName = try container.decodeIfPresent(String.self, forKey: .profileName)
        orderIndex = try container.decodeIfPresent(Int.self, forKey: .orderIndex)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encodeIfPresent(name, forKey: .name)
        try container.encodeIfPresent(color, forKey: .color)
        try container.encodeIfPresent(tabCount, forKey: .tabCount)
        try container.encodeIfPresent(isActive, forKey: .isActive)
        try container.encodePatchable(icon, forKey: .icon)
        try container.encodeIfPresent(profileId, forKey: .profileId)
        try container.encodeIfPresent(profileName, forKey: .profileName)
        try container.encodeIfPresent(orderIndex, forKey: .orderIndex)
    }
}

struct FolderUpdate: Codable {
    let name: String?
    let isExpanded: Bool?
    let isPinned: Bool?
}

enum SyncStatus: Codable {
    case idle
    case syncing(progress: Double)
    case synced(lastSyncAt: String)
    case error(message: String)
    case offline

    private enum CodingKeys: String, CodingKey {
        case kind
        case progress
        case lastSyncAt = "last_sync_at"
        case message
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)
        switch kind {
        case "idle": self = .idle
        case "syncing": self = .syncing(progress: try container.decode(Double.self, forKey: .progress))
        case "synced": self = .synced(lastSyncAt: try container.decode(String.self, forKey: .lastSyncAt))
        case "error": self = .error(message: try container.decode(String.self, forKey: .message))
        case "offline": self = .offline
        default:
            throw DecodingError.dataCorruptedError(forKey: .kind, in: container, debugDescription: "Unknown SyncStatus: \(kind)")
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .idle: try container.encode("idle", forKey: .kind)
        case .syncing(let progress):
            try container.encode("syncing", forKey: .kind)
            try container.encode(progress, forKey: .progress)
        case .synced(let lastSyncAt):
            try container.encode("synced", forKey: .kind)
            try container.encode(lastSyncAt, forKey: .lastSyncAt)
        case .error(let message):
            try container.encode("error", forKey: .kind)
            try container.encode(message, forKey: .message)
        case .offline: try container.encode("offline", forKey: .kind)
        }
    }
}

enum MemoryAction: Codable {
    case frozeTabs(count: Int)
    case suspendedTabs(count: Int)
    case killedTabs(count: Int)
    case releasedWebviews(count: Int)

    private enum CodingKeys: String, CodingKey {
        case kind
        case count
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)
        let count = try container.decode(Int.self, forKey: .count)
        switch kind {
        case "froze_tabs": self = .frozeTabs(count: count)
        case "suspended_tabs": self = .suspendedTabs(count: count)
        case "killed_tabs": self = .killedTabs(count: count)
        case "released_webviews": self = .releasedWebviews(count: count)
        default:
            throw DecodingError.dataCorruptedError(forKey: .kind, in: container, debugDescription: "Unknown MemoryAction: \(kind)")
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .frozeTabs(let count):
            try container.encode("froze_tabs", forKey: .kind)
            try container.encode(count, forKey: .count)
        case .suspendedTabs(let count):
            try container.encode("suspended_tabs", forKey: .kind)
            try container.encode(count, forKey: .count)
        case .killedTabs(let count):
            try container.encode("killed_tabs", forKey: .kind)
            try container.encode(count, forKey: .count)
        case .releasedWebviews(let count):
            try container.encode("released_webviews", forKey: .kind)
            try container.encode(count, forKey: .count)
        }
    }
}

struct MahoError: Codable {
    let code: String
    let message: String
    let details: AnyCodable?
}

struct AppStateSnapshot: Codable {
    let spaces: [SpaceViewModel]
    let activeSpaceId: SpaceId
    let tabs: [String: [TabViewModel]]
    let syncStatus: SyncStatus
}

struct HistoryEntry: Codable {
    let id: String
    let url: String
    let title: String
    let visitedAt: String
}

struct BookmarkEntry: Codable {
    let id: String
    let url: String
    let title: String
    let folderId: String?
    let createdAt: String
}

enum TabLifecycleState: Codable {
    case active
    case frozen
    case suspended(snapshot: TabSnapshot)
    case archived(metadataOnly: Bool)

    private enum CodingKeys: String, CodingKey {
        case kind
        case snapshot
        case metadataOnly = "metadata_only"
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)
        switch kind {
        case "active": self = .active
        case "frozen": self = .frozen
        case "suspended": self = .suspended(snapshot: try container.decode(TabSnapshot.self, forKey: .snapshot))
        case "archived": self = .archived(metadataOnly: try container.decode(Bool.self, forKey: .metadataOnly))
        default:
            throw DecodingError.dataCorruptedError(forKey: .kind, in: container, debugDescription: "Unknown TabLifecycleState: \(kind)")
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .active: try container.encode("active", forKey: .kind)
        case .frozen: try container.encode("frozen", forKey: .kind)
        case .suspended(let snapshot):
            try container.encode("suspended", forKey: .kind)
            try container.encode(snapshot, forKey: .snapshot)
        case .archived(let metadataOnly):
            try container.encode("archived", forKey: .kind)
            try container.encode(metadataOnly, forKey: .metadataOnly)
        }
    }
}

struct AnyCodable: Codable {
    let value: Any?

    init(_ value: Any?) {
        self.value = value
    }

    static func bool(_ value: Bool) -> AnyCodable {
        AnyCodable(value)
    }

    static func string(_ value: String) -> AnyCodable {
        AnyCodable(value)
    }

    static func int(_ value: Int) -> AnyCodable {
        AnyCodable(value)
    }

    static func double(_ value: Double) -> AnyCodable {
        AnyCodable(value)
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if container.decodeNil() {
            value = nil
        } else if let str = try? container.decode(String.self) {
            value = str
        } else if let num = try? container.decode(Double.self) {
            value = num
        } else if let bool = try? container.decode(Bool.self) {
            value = bool
        } else {
            value = nil
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        if let str = value as? String { try container.encode(str) }
        else if let num = value as? Double { try container.encode(num) }
        else if let bool = value as? Bool { try container.encode(bool) }
        else { try container.encodeNil() }
    }
}
