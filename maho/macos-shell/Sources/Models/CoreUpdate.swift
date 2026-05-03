import Foundation

struct SpaceUpdate: Codable, Equatable {
    let name: String?
    let color: SpaceColor?
    let tabCount: Int?
    let isActive: Bool?
    let icon: String??
    let profileId: ProfileId?
    let profileName: String?
    let orderIndex: Int?

    private enum CodingKeys: String, CodingKey {
        case name, color, tabCount, isActive, icon, profileId, profileName, orderIndex
    }
}

struct FolderUpdate: Codable, Equatable {
    let name: String?
    let isExpanded: Bool?
    let isPinned: Bool?

    private enum CodingKeys: String, CodingKey {
        case name, isExpanded, isPinned
    }
}

struct TidyTabFolder: Codable, Equatable {
    let name: String
    let tabIds: [TabId]

    private enum CodingKeys: String, CodingKey {
        case name
        case tabIds = "tab_ids"
    }
}

enum SyncStatus: Codable, Equatable {
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
        case "syncing":
            self = .syncing(progress: try container.decode(Double.self, forKey: .progress))
        case "synced":
            self = .synced(lastSyncAt: try container.decode(String.self, forKey: .lastSyncAt))
        case "error":
            self = .error(message: try container.decode(String.self, forKey: .message))
        case "offline": self = .offline
        default:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown SyncStatus kind: \(kind)")
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .idle:
            try container.encode("idle", forKey: .kind)
        case .syncing(let progress):
            try container.encode("syncing", forKey: .kind)
            try container.encode(progress, forKey: .progress)
        case .synced(let lastSyncAt):
            try container.encode("synced", forKey: .kind)
            try container.encode(lastSyncAt, forKey: .lastSyncAt)
        case .error(let message):
            try container.encode("error", forKey: .kind)
            try container.encode(message, forKey: .message)
        case .offline:
            try container.encode("offline", forKey: .kind)
        }
    }
}

enum MemoryAction: Codable, Equatable {
    case frozeTabs(count: Int)
    case suspendedTabs(count: Int)
    case killedTabs(count: Int)
    case releasedWebviews(count: Int)

    private enum CodingKeys: String, CodingKey {
        case kind, count
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
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown MemoryAction kind: \(kind)")
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

struct MahoError: Codable, Equatable {
    let code: String
    let message: String
    let details: JSONValue?
}

struct AppStateSnapshot: Codable, Equatable {
    let spaces: [SpaceViewModel]
    let activeSpaceId: SpaceId
    let tabs: [String: [TabViewModel]]
    let syncStatus: SyncStatus

    private enum CodingKeys: String, CodingKey {
        case spaces, activeSpaceId, tabs, syncStatus
    }
}

struct HistoryEntry: Codable, Equatable {
    let id: String
    let url: String
    let title: String
    let visitedAt: String

    private enum CodingKeys: String, CodingKey {
        case id, url, title, visitedAt
    }
}

struct BookmarkEntry: Codable, Equatable {
    let id: String
    let url: String
    let title: String
    let folderId: String?
    let createdAt: String

    private enum CodingKeys: String, CodingKey {
        case id, url, title, folderId, createdAt
    }
}

enum CoreUpdate: Codable, Equatable {
    case fullState(state: AppStateSnapshot)
    case tabCreated(tab: TabViewModel)
    case tabUpdated(tabId: TabId, changes: TabStateUpdate)
    case tabClosed(tabId: TabId, animated: Bool)
    case tabOrderChanged(spaceId: SpaceId, order: [TabId])
    case spaceCreated(space: SpaceViewModel)
    case spaceUpdated(spaceId: SpaceId, changes: SpaceUpdate)
    case spaceDeleted(spaceId: SpaceId)
    case spaceOrderChanged(order: [SpaceId])
    case folderCreated(folder: FolderViewModel)
    case folderUpdated(folderId: FolderId, changes: FolderUpdate)
    case folderDeleted(folderId: FolderId)
    case folderOrderChanged(spaceId: SpaceId)
    case activeSpaceChanged(spaceId: SpaceId, activeTabId: TabId?)
    case commandBarResults(suggestions: [SuggestionViewModel])
    case recentSearchesUpdated(searches: [String])
    case searchEnginesUpdated(engines: [SearchEngineViewModel])
    case navigationStateChanged(tabId: TabId, url: MahoUrl, title: String, canGoBack: Bool, canGoForward: Bool, isLoading: Bool, progress: Double, isSecure: Bool)
    case tabAudioStateChanged(tabId: TabId, isPlaying: Bool)
    case tabFaviconChanged(tabId: TabId, favicon: ImageData?)
    case downloadStarted(download: DownloadViewModel)
    case downloadProgress(downloadId: DownloadId, progress: Double)
    case downloadCompleted(downloadId: DownloadId)
    case downloadRenamed(downloadId: DownloadId, oldName: String, newName: String)
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
    case globalBoostsToggled(enabled: Bool)
    case jsBoostsToggled(enabled: Bool)
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
    case openPeekTab(url: MahoUrl, sourceTabId: TabId)
    case favoriteLimitReached(max: Int)
    case navigateTab(tabId: TabId, url: MahoUrl)
    case tabsMigrated(tabIds: [TabId], fromSpaceId: SpaceId, toSpaceId: SpaceId)
    case requestLlmCompletion(requestId: String, requestType: String, prompt: String, context: JSONValue)
    case pagePreviewReady(url: MahoUrl, title: String, summary: String)
    case tidyTabsReady(spaceId: SpaceId, folders: [TidyTabFolder])
    case skillsListReady(skills: [SkillInfo])
    case skillPromptReady(skillId: String, skillName: String, systemPrompt: String)

    private enum CodingKeys: String, CodingKey {
        case kind
        case state, tab, tabId = "tab_id", changes, animated
        case spaceId = "space_id", order, space, folder
        case folderId = "folder_id"
        case suggestions, searches, engines
        case url, title, canGoBack = "can_go_back", canGoForward = "can_go_forward"
        case isLoading = "is_loading", progress, isPlaying = "is_playing"
        case isSecure = "is_secure"
        case favicon, download, downloadId = "download_id"
        case notification, notificationId = "notification_id"
        case notifications, request
        case entries, bookmarks
        case zoomLevel = "zoom_level", previewData = "preview_data"
        case origin, permission, granted, active
        case ruleCount = "rule_count", enabled
        case popupBlocking = "popup_blocking"
        case id, status, settings
        case extensionId = "extension_id"
        case account
        case rule, ruleId = "rule_id"
        case profile, profileId = "profile_id"
        case dataStoreId = "data_store_id"
        case context, error
        case passwordId = "password_id", passwords
        case address, payment
        case sourceTabId = "source_tab_id"
        case max
        case tabIds = "tab_ids"
        case fromSpaceId = "from_space_id"
        case toSpaceId = "to_space_id"
        case action
        case requestId = "request_id"
        case requestType = "request_type"
        case prompt
        case summary
        case oldName = "old_name"
        case newName = "new_name"
        case folders
        case skills
        case skillId = "skill_id"
        case skillName = "skill_name"
        case systemPrompt = "system_prompt"
        case activeTabId = "active_tab_id"
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)

        switch kind {
        case "full_state":
            self = .fullState(state: try container.decode(AppStateSnapshot.self, forKey: .state))
        case "tab_created":
            self = .tabCreated(tab: try container.decode(TabViewModel.self, forKey: .tab))
        case "tab_updated":
            self = .tabUpdated(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                changes: try container.decode(TabStateUpdate.self, forKey: .changes))
        case "tab_closed":
            self = .tabClosed(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                animated: try container.decode(Bool.self, forKey: .animated))
        case "tab_order_changed":
            self = .tabOrderChanged(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                order: try container.decode([TabId].self, forKey: .order))
        case "space_created":
            self = .spaceCreated(space: try container.decode(SpaceViewModel.self, forKey: .space))
        case "space_updated":
            self = .spaceUpdated(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                changes: try container.decode(SpaceUpdate.self, forKey: .changes))
        case "space_deleted":
            self = .spaceDeleted(spaceId: try container.decode(SpaceId.self, forKey: .spaceId))
        case "space_order_changed":
            self = .spaceOrderChanged(order: try container.decode([SpaceId].self, forKey: .order))
        case "folder_created":
            self = .folderCreated(folder: try container.decode(FolderViewModel.self, forKey: .folder))
        case "folder_updated":
            self = .folderUpdated(
                folderId: try container.decode(FolderId.self, forKey: .folderId),
                changes: try container.decode(FolderUpdate.self, forKey: .changes))
        case "folder_deleted":
            self = .folderDeleted(folderId: try container.decode(FolderId.self, forKey: .folderId))
        case "folder_order_changed":
            self = .folderOrderChanged(spaceId: try container.decode(SpaceId.self, forKey: .spaceId))
        case "active_space_changed":
            self = .activeSpaceChanged(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                activeTabId: try container.decodeIfPresent(TabId.self, forKey: .activeTabId)
            )
        case "command_bar_results":
            self = .commandBarResults(suggestions: try container.decode([SuggestionViewModel].self, forKey: .suggestions))
        case "recent_searches_updated":
            self = .recentSearchesUpdated(searches: try container.decode([String].self, forKey: .searches))
        case "search_engines_updated":
            self = .searchEnginesUpdated(engines: try container.decode([SearchEngineViewModel].self, forKey: .engines))
        case "navigation_state_changed":
            self = .navigationStateChanged(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                url: try container.decode(MahoUrl.self, forKey: .url),
                title: try container.decode(String.self, forKey: .title),
                canGoBack: try container.decode(Bool.self, forKey: .canGoBack),
                canGoForward: try container.decode(Bool.self, forKey: .canGoForward),
                isLoading: try container.decode(Bool.self, forKey: .isLoading),
                progress: try container.decode(Double.self, forKey: .progress),
                isSecure: try container.decodeIfPresent(Bool.self, forKey: .isSecure) ?? true)
        case "tab_audio_state_changed":
            self = .tabAudioStateChanged(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                isPlaying: try container.decode(Bool.self, forKey: .isPlaying))
        case "tab_favicon_changed":
            self = .tabFaviconChanged(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                favicon: try container.decodeIfPresent(ImageData.self, forKey: .favicon))
        case "download_started":
            self = .downloadStarted(download: try container.decode(DownloadViewModel.self, forKey: .download))
        case "download_progress":
            self = .downloadProgress(
                downloadId: try container.decode(DownloadId.self, forKey: .downloadId),
                progress: try container.decode(Double.self, forKey: .progress))
        case "download_completed":
            self = .downloadCompleted(downloadId: try container.decode(DownloadId.self, forKey: .downloadId))
        case "download_renamed":
            self = .downloadRenamed(
                downloadId: try container.decode(DownloadId.self, forKey: .downloadId),
                oldName: try container.decode(String.self, forKey: .oldName),
                newName: try container.decode(String.self, forKey: .newName))
        case "show_notification":
            self = .showNotification(notification: try container.decode(NotificationViewModel.self, forKey: .notification))
        case "notification_dismissed":
            self = .notificationDismissed(notificationId: try container.decode(String.self, forKey: .notificationId))
        case "all_notifications_dismissed":
            self = .allNotificationsDismissed
        case "notification_list":
            self = .notificationList(notifications: try container.decode([NotificationViewModel].self, forKey: .notifications))
        case "show_permission_request":
            self = .showPermissionRequest(request: try container.decode(PermissionRequest.self, forKey: .request))
        case "show_find_bar":
            self = .showFindBar(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "history_results":
            self = .historyResults(entries: try container.decode([HistoryEntry].self, forKey: .entries))
        case "bookmark_results":
            self = .bookmarkResults(bookmarks: try container.decode([BookmarkEntry].self, forKey: .bookmarks))
        case "zoom_changed":
            self = .zoomChanged(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                zoomLevel: try container.decode(Double.self, forKey: .zoomLevel))
        case "tab_preview_updated":
            self = .tabPreviewUpdated(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                previewData: try container.decode(String.self, forKey: .previewData))
        case "tab_preview_capture_requested":
            self = .tabPreviewCaptureRequested(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "permission_response":
            self = .permissionResponse(
                origin: try container.decode(String.self, forKey: .origin),
                permission: try container.decode(String.self, forKey: .permission),
                granted: try container.decode(Bool.self, forKey: .granted))
        case "print_requested":
            self = .printRequested(tabId: try container.decode(TabId.self, forKey: .tabId))
        case "pip_toggled":
            self = .pipToggled(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                active: try container.decode(Bool.self, forKey: .active))
        case "content_rules_compiled":
            self = .contentRulesCompiled(ruleCount: try container.decode(Int.self, forKey: .ruleCount))
        case "content_blocker_state_changed":
            self = .contentBlockerStateChanged(
                enabled: try container.decode(Bool.self, forKey: .enabled),
                popupBlocking: try container.decode(Bool.self, forKey: .popupBlocking))
        case "filter_list_updated":
            self = .filterListUpdated(
                id: try container.decode(String.self, forKey: .id),
                ruleCount: try container.decode(Int.self, forKey: .ruleCount))
        case "tab_lifecycle_changed":
            self = .tabLifecycleChanged(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                state: try container.decode(TabLifecycleState.self, forKey: .state))
        case "memory_pressure_response":
            self = .memoryPressureResponse(action: try container.decode(MemoryAction.self, forKey: .action))
        case "sync_state_changed":
            self = .syncStateChanged(status: try container.decode(SyncStatus.self, forKey: .status))
        case "settings_changed":
            self = .settingsChanged(settings: try container.decode(Settings.self, forKey: .settings))
        case "space_config_updated":
            self = .spaceConfigUpdated(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                changes: try container.decode(SpaceConfigUpdate.self, forKey: .changes))
        case "global_boosts_toggled":
            self = .globalBoostsToggled(enabled: try container.decode(Bool.self, forKey: .enabled))
        case "js_boosts_toggled":
            self = .jsBoostsToggled(enabled: try container.decode(Bool.self, forKey: .enabled))
        case "extension_toggled":
            self = .extensionToggled(
                extensionId: try container.decode(String.self, forKey: .extensionId),
                enabled: try container.decode(Bool.self, forKey: .enabled))
        case "extension_removed":
            self = .extensionRemoved(extensionId: try container.decode(String.self, forKey: .extensionId))
        case "account_changed":
            self = .accountChanged(account: try container.decodeIfPresent(AccountInfo.self, forKey: .account))
        case "account_sync_state_changed":
            self = .accountSyncStateChanged(state: try container.decode(SyncState.self, forKey: .state))
        case "traffic_rule_created":
            self = .trafficRuleCreated(rule: try container.decode(TrafficRule.self, forKey: .rule))
        case "traffic_rule_deleted":
            self = .trafficRuleDeleted(ruleId: try container.decode(String.self, forKey: .ruleId))
        case "profile_created":
            self = .profileCreated(profile: try container.decode(ProfileConfig.self, forKey: .profile))
        case "profile_deleted":
            self = .profileDeleted(
                profileId: try container.decode(ProfileId.self, forKey: .profileId),
                dataStoreId: try container.decodeIfPresent(String.self, forKey: .dataStoreId))
        case "profile_updated":
            self = .profileUpdated(profile: try container.decode(ProfileConfig.self, forKey: .profile))
        case "active_profile_changed":
            self = .activeProfileChanged(profileId: try container.decode(ProfileId.self, forKey: .profileId))
        case "error":
            self = .error(
                context: try container.decode(String.self, forKey: .context),
                error: try container.decode(MahoError.self, forKey: .error))
        case "password_deleted":
            self = .passwordDeleted(passwordId: try container.decode(String.self, forKey: .passwordId))
        case "passwords_search_result":
            self = .passwordsSearchResult(passwords: try container.decode([SavedPassword].self, forKey: .passwords))
        case "autofill_address_added":
            self = .autofillAddressAdded(address: try container.decode(AutofillAddress.self, forKey: .address))
        case "autofill_address_deleted":
            self = .autofillAddressDeleted(id: try container.decode(String.self, forKey: .id))
        case "autofill_payment_added":
            self = .autofillPaymentAdded(payment: try container.decode(AutofillPayment.self, forKey: .payment))
        case "autofill_payment_deleted":
            self = .autofillPaymentDeleted(id: try container.decode(String.self, forKey: .id))
        case "open_peek_tab":
            self = .openPeekTab(
                url: try container.decode(MahoUrl.self, forKey: .url),
                sourceTabId: try container.decode(TabId.self, forKey: .sourceTabId))
        case "favorite_limit_reached":
            self = .favoriteLimitReached(max: try container.decode(Int.self, forKey: .max))
        case "navigate_tab":
            self = .navigateTab(
                tabId: try container.decode(TabId.self, forKey: .tabId),
                url: try container.decode(MahoUrl.self, forKey: .url))
        case "tabs_migrated":
            self = .tabsMigrated(
                tabIds: try container.decode([TabId].self, forKey: .tabIds),
                fromSpaceId: try container.decode(SpaceId.self, forKey: .fromSpaceId),
                toSpaceId: try container.decode(SpaceId.self, forKey: .toSpaceId))
        case "request_llm_completion":
            self = .requestLlmCompletion(
                requestId: try container.decode(String.self, forKey: .requestId),
                requestType: try container.decode(String.self, forKey: .requestType),
                prompt: try container.decode(String.self, forKey: .prompt),
                context: try container.decode(JSONValue.self, forKey: .context))
        case "page_preview_ready":
            self = .pagePreviewReady(
                url: try container.decode(MahoUrl.self, forKey: .url),
                title: try container.decode(String.self, forKey: .title),
                summary: try container.decode(String.self, forKey: .summary))
        case "tidy_tabs_ready":
            self = .tidyTabsReady(
                spaceId: try container.decode(SpaceId.self, forKey: .spaceId),
                folders: try container.decode([TidyTabFolder].self, forKey: .folders))
        case "skills_list_ready":
            self = .skillsListReady(skills: try container.decode([SkillInfo].self, forKey: .skills))
        case "skill_prompt_ready":
            self = .skillPromptReady(
                skillId: try container.decode(String.self, forKey: .skillId),
                skillName: try container.decode(String.self, forKey: .skillName),
                systemPrompt: try container.decode(String.self, forKey: .systemPrompt))
        default:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown CoreUpdate kind: \(kind)")
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)

        switch self {
        case .fullState(let state):
            try container.encode("full_state", forKey: .kind)
            try container.encode(state, forKey: .state)
        case .tabCreated(let tab):
            try container.encode("tab_created", forKey: .kind)
            try container.encode(tab, forKey: .tab)
        case .tabUpdated(let tabId, let changes):
            try container.encode("tab_updated", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(changes, forKey: .changes)
        case .tabClosed(let tabId, let animated):
            try container.encode("tab_closed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(animated, forKey: .animated)
        case .tabOrderChanged(let spaceId, let order):
            try container.encode("tab_order_changed", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(order, forKey: .order)
        case .spaceCreated(let space):
            try container.encode("space_created", forKey: .kind)
            try container.encode(space, forKey: .space)
        case .spaceUpdated(let spaceId, let changes):
            try container.encode("space_updated", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(changes, forKey: .changes)
        case .spaceDeleted(let spaceId):
            try container.encode("space_deleted", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
        case .spaceOrderChanged(let order):
            try container.encode("space_order_changed", forKey: .kind)
            try container.encode(order, forKey: .order)
        case .folderCreated(let folder):
            try container.encode("folder_created", forKey: .kind)
            try container.encode(folder, forKey: .folder)
        case .folderUpdated(let folderId, let changes):
            try container.encode("folder_updated", forKey: .kind)
            try container.encode(folderId, forKey: .folderId)
            try container.encode(changes, forKey: .changes)
        case .folderDeleted(let folderId):
            try container.encode("folder_deleted", forKey: .kind)
            try container.encode(folderId, forKey: .folderId)
        case .folderOrderChanged(let spaceId):
            try container.encode("folder_order_changed", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
        case .activeSpaceChanged(let spaceId, let activeTabId):
            try container.encode("active_space_changed", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encodeIfPresent(activeTabId, forKey: .activeTabId)
        case .commandBarResults(let suggestions):
            try container.encode("command_bar_results", forKey: .kind)
            try container.encode(suggestions, forKey: .suggestions)
        case .recentSearchesUpdated(let searches):
            try container.encode("recent_searches_updated", forKey: .kind)
            try container.encode(searches, forKey: .searches)
        case .searchEnginesUpdated(let engines):
            try container.encode("search_engines_updated", forKey: .kind)
            try container.encode(engines, forKey: .engines)
        case .navigationStateChanged(let tabId, let url, let title, let canGoBack, let canGoForward, let isLoading, let progress, let isSecure):
            try container.encode("navigation_state_changed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(url, forKey: .url)
            try container.encode(title, forKey: .title)
            try container.encode(canGoBack, forKey: .canGoBack)
            try container.encode(canGoForward, forKey: .canGoForward)
            try container.encode(isLoading, forKey: .isLoading)
            try container.encode(progress, forKey: .progress)
            try container.encode(isSecure, forKey: .isSecure)
        case .tabAudioStateChanged(let tabId, let isPlaying):
            try container.encode("tab_audio_state_changed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(isPlaying, forKey: .isPlaying)
        case .tabFaviconChanged(let tabId, let favicon):
            try container.encode("tab_favicon_changed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encodeIfPresent(favicon, forKey: .favicon)
        case .downloadStarted(let download):
            try container.encode("download_started", forKey: .kind)
            try container.encode(download, forKey: .download)
        case .downloadProgress(let downloadId, let progress):
            try container.encode("download_progress", forKey: .kind)
            try container.encode(downloadId, forKey: .downloadId)
            try container.encode(progress, forKey: .progress)
        case .downloadCompleted(let downloadId):
            try container.encode("download_completed", forKey: .kind)
            try container.encode(downloadId, forKey: .downloadId)
        case .downloadRenamed(let downloadId, let oldName, let newName):
            try container.encode("download_renamed", forKey: .kind)
            try container.encode(downloadId, forKey: .downloadId)
            try container.encode(oldName, forKey: .oldName)
            try container.encode(newName, forKey: .newName)
        case .showNotification(let notification):
            try container.encode("show_notification", forKey: .kind)
            try container.encode(notification, forKey: .notification)
        case .notificationDismissed(let notificationId):
            try container.encode("notification_dismissed", forKey: .kind)
            try container.encode(notificationId, forKey: .notificationId)
        case .allNotificationsDismissed:
            try container.encode("all_notifications_dismissed", forKey: .kind)
        case .notificationList(let notifications):
            try container.encode("notification_list", forKey: .kind)
            try container.encode(notifications, forKey: .notifications)
        case .showPermissionRequest(let request):
            try container.encode("show_permission_request", forKey: .kind)
            try container.encode(request, forKey: .request)
        case .showFindBar(let tabId):
            try container.encode("show_find_bar", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .historyResults(let entries):
            try container.encode("history_results", forKey: .kind)
            try container.encode(entries, forKey: .entries)
        case .bookmarkResults(let bookmarks):
            try container.encode("bookmark_results", forKey: .kind)
            try container.encode(bookmarks, forKey: .bookmarks)
        case .zoomChanged(let tabId, let zoomLevel):
            try container.encode("zoom_changed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(zoomLevel, forKey: .zoomLevel)
        case .tabPreviewUpdated(let tabId, let previewData):
            try container.encode("tab_preview_updated", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(previewData, forKey: .previewData)
        case .tabPreviewCaptureRequested(let tabId):
            try container.encode("tab_preview_capture_requested", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .permissionResponse(let origin, let permission, let granted):
            try container.encode("permission_response", forKey: .kind)
            try container.encode(origin, forKey: .origin)
            try container.encode(permission, forKey: .permission)
            try container.encode(granted, forKey: .granted)
        case .printRequested(let tabId):
            try container.encode("print_requested", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
        case .pipToggled(let tabId, let active):
            try container.encode("pip_toggled", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(active, forKey: .active)
        case .contentRulesCompiled(let ruleCount):
            try container.encode("content_rules_compiled", forKey: .kind)
            try container.encode(ruleCount, forKey: .ruleCount)
        case .contentBlockerStateChanged(let enabled, let popupBlocking):
            try container.encode("content_blocker_state_changed", forKey: .kind)
            try container.encode(enabled, forKey: .enabled)
            try container.encode(popupBlocking, forKey: .popupBlocking)
        case .filterListUpdated(let id, let ruleCount):
            try container.encode("filter_list_updated", forKey: .kind)
            try container.encode(id, forKey: .id)
            try container.encode(ruleCount, forKey: .ruleCount)
        case .tabLifecycleChanged(let tabId, let state):
            try container.encode("tab_lifecycle_changed", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(state, forKey: .state)
        case .memoryPressureResponse(let action):
            try container.encode("memory_pressure_response", forKey: .kind)
            try container.encode(action, forKey: .action)
        case .syncStateChanged(let status):
            try container.encode("sync_state_changed", forKey: .kind)
            try container.encode(status, forKey: .status)
        case .settingsChanged(let settings):
            try container.encode("settings_changed", forKey: .kind)
            try container.encode(settings, forKey: .settings)
        case .spaceConfigUpdated(let spaceId, let changes):
            try container.encode("space_config_updated", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(changes, forKey: .changes)
        case .globalBoostsToggled(let enabled):
            try container.encode("global_boosts_toggled", forKey: .kind)
            try container.encode(enabled, forKey: .enabled)
        case .jsBoostsToggled(let enabled):
            try container.encode("js_boosts_toggled", forKey: .kind)
            try container.encode(enabled, forKey: .enabled)
        case .extensionToggled(let extensionId, let enabled):
            try container.encode("extension_toggled", forKey: .kind)
            try container.encode(extensionId, forKey: .extensionId)
            try container.encode(enabled, forKey: .enabled)
        case .extensionRemoved(let extensionId):
            try container.encode("extension_removed", forKey: .kind)
            try container.encode(extensionId, forKey: .extensionId)
        case .accountChanged(let account):
            try container.encode("account_changed", forKey: .kind)
            try container.encodeIfPresent(account, forKey: .account)
        case .accountSyncStateChanged(let state):
            try container.encode("account_sync_state_changed", forKey: .kind)
            try container.encode(state, forKey: .state)
        case .trafficRuleCreated(let rule):
            try container.encode("traffic_rule_created", forKey: .kind)
            try container.encode(rule, forKey: .rule)
        case .trafficRuleDeleted(let ruleId):
            try container.encode("traffic_rule_deleted", forKey: .kind)
            try container.encode(ruleId, forKey: .ruleId)
        case .profileCreated(let profile):
            try container.encode("profile_created", forKey: .kind)
            try container.encode(profile, forKey: .profile)
        case .profileDeleted(let profileId, let dataStoreId):
            try container.encode("profile_deleted", forKey: .kind)
            try container.encode(profileId, forKey: .profileId)
            try container.encodeIfPresent(dataStoreId, forKey: .dataStoreId)
        case .profileUpdated(let profile):
            try container.encode("profile_updated", forKey: .kind)
            try container.encode(profile, forKey: .profile)
        case .activeProfileChanged(let profileId):
            try container.encode("active_profile_changed", forKey: .kind)
            try container.encode(profileId, forKey: .profileId)
        case .error(let context, let errorVal):
            try container.encode("error", forKey: .kind)
            try container.encode(context, forKey: .context)
            try container.encode(errorVal, forKey: .error)
        case .passwordDeleted(let passwordId):
            try container.encode("password_deleted", forKey: .kind)
            try container.encode(passwordId, forKey: .passwordId)
        case .passwordsSearchResult(let passwords):
            try container.encode("passwords_search_result", forKey: .kind)
            try container.encode(passwords, forKey: .passwords)
        case .autofillAddressAdded(let address):
            try container.encode("autofill_address_added", forKey: .kind)
            try container.encode(address, forKey: .address)
        case .autofillAddressDeleted(let id):
            try container.encode("autofill_address_deleted", forKey: .kind)
            try container.encode(id, forKey: .id)
        case .autofillPaymentAdded(let payment):
            try container.encode("autofill_payment_added", forKey: .kind)
            try container.encode(payment, forKey: .payment)
        case .autofillPaymentDeleted(let id):
            try container.encode("autofill_payment_deleted", forKey: .kind)
            try container.encode(id, forKey: .id)
        case .openPeekTab(let url, let sourceTabId):
            try container.encode("open_peek_tab", forKey: .kind)
            try container.encode(url, forKey: .url)
            try container.encode(sourceTabId, forKey: .sourceTabId)
        case .favoriteLimitReached(let max):
            try container.encode("favorite_limit_reached", forKey: .kind)
            try container.encode(max, forKey: .max)
        case .navigateTab(let tabId, let url):
            try container.encode("navigate_tab", forKey: .kind)
            try container.encode(tabId, forKey: .tabId)
            try container.encode(url, forKey: .url)
        case .tabsMigrated(let tabIds, let fromSpaceId, let toSpaceId):
            try container.encode("tabs_migrated", forKey: .kind)
            try container.encode(tabIds, forKey: .tabIds)
            try container.encode(fromSpaceId, forKey: .fromSpaceId)
            try container.encode(toSpaceId, forKey: .toSpaceId)
        case .requestLlmCompletion(let requestId, let requestType, let prompt, let context):
            try container.encode("request_llm_completion", forKey: .kind)
            try container.encode(requestId, forKey: .requestId)
            try container.encode(requestType, forKey: .requestType)
            try container.encode(prompt, forKey: .prompt)
            try container.encode(context, forKey: .context)
        case .pagePreviewReady(let url, let title, let summary):
            try container.encode("page_preview_ready", forKey: .kind)
            try container.encode(url, forKey: .url)
            try container.encode(title, forKey: .title)
            try container.encode(summary, forKey: .summary)
        case .tidyTabsReady(let spaceId, let folders):
            try container.encode("tidy_tabs_ready", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
            try container.encode(folders, forKey: .folders)
        case .skillsListReady(let skills):
            try container.encode("skills_list_ready", forKey: .kind)
            try container.encode(skills, forKey: .skills)
        case .skillPromptReady(let skillId, let skillName, let systemPrompt):
            try container.encode("skill_prompt_ready", forKey: .kind)
            try container.encode(skillId, forKey: .skillId)
            try container.encode(skillName, forKey: .skillName)
            try container.encode(systemPrompt, forKey: .systemPrompt)
        }
    }
}
