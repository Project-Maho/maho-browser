import Foundation

func roundTrip<T: Codable & Equatable>(_ value: T) throws -> T {
    let encoder = JSONEncoder()
    let decoder = JSONDecoder()
    let data = try encoder.encode(value)
    return try decoder.decode(T.self, from: data)
}

func jsonString<T: Codable>(_ value: T) throws -> String {
    let encoder = JSONEncoder()
    let data = try encoder.encode(value)
    return String(data: data, encoding: .utf8) ?? ""
}

// MARK: - Test Data Helpers

let tabId: TabId = "tab-1"
let spaceId: SpaceId = "space-1"
let folderId: FolderId = "folder-1"
let url = MahoUrl("https://example.com")
let dateTime = MahoDateTime("2024-01-01T00:00:00Z")
let spaceColor = SpaceColor(hue: 0.5, saturation: 0.8, brightness: 0.9)

let tab = TabViewModel(
    id: tabId, spaceId: spaceId, title: "Test", customTitle: nil, url: "https://example.com",
    favicon: nil, isLoading: false, isPinned: false, isFavorite: false, favoriteOrder: 3,
    isMuted: false, isPlayingAudio: false, lifecycleState: "active",
    children: [], createdAt: dateTime, lastActiveAt: dateTime
)

let tabStateUpdate = TabStateUpdate(
    title: "Updated Title",
    customTitle: "Custom Title",
    url: "https://example.com/updated",
    favicon: nil,
    isLoading: true,
    isPinned: true,
    isMuted: false,
    isPlayingAudio: true,
    isFavorite: true,
    lifecycleState: "active"
)

let space = SpaceViewModel(
    id: spaceId, name: "Test Space", color: spaceColor, theme: nil, tabCount: 1, isActive: true,
    icon: nil, profileId: "default", profileName: "Default", orderIndex: 0
)

let spaceDomain = Space(
    id: spaceId,
    profileId: "default",
    name: "Test Space",
    color: spaceColor,
    theme: nil,
    icon: "globe",
    tabOrder: [tabId],
    folders: [],
    atcRules: [],
    isActive: true,
    createdAt: dateTime
)

let spaceConfig = SpaceConfig(
    color: spaceColor,
    theme: nil,
    icon: "globe",
    name: "Test Space",
    profileId: "default"
)

let spaceConfigUpdate = SpaceConfigUpdate(
    spaceId: spaceId,
    name: "Config Space",
    color: spaceColor,
    theme: nil,
    icon: nil,
    profileId: "default"
)

let spaceConfigUpdateIconClear = SpaceConfigUpdate(
    spaceId: spaceId,
    name: nil,
    color: nil,
    theme: nil,
    icon: nil,
    profileId: nil
)

let folder = FolderViewModel(
    id: folderId, name: "Test Folder", tabCount: 0, isExpanded: true, tabIds: [], isPinned: false, parentFolderId: nil
)

let suggestion = SuggestionViewModel(
    kind: .tab, key: "test", title: "Test", subtitle: nil, executionPayload: nil, icon: nil, relevanceScore: 1.0, matchRanges: nil
)

let notification = NotificationViewModel(
    id: "notif-1", title: "Test", message: "Test message", icon: nil, actions: []
)

let permReq = PermissionRequest(
    id: "perm-1", origin: "https://example.com", permission: "camera", message: "Allow camera?"
)

let findBar = FindBarState(query: "test", matchCount: 5, activeIndex: 2, isVisible: true)

let settingsUpdate = SettingsUpdate(
    general: nil, appearance: nil, privacy: nil, reader: nil,
    keyboardShortcuts: nil, perSiteSettings: nil, toolbarItems: nil,
    max: nil, autofill: nil, advanced: nil, notifications: nil
)

let settings = Settings(
    general: GeneralSettings(
        defaultSearchEngine: SettingsSearchEngine(name: "DuckDuckGo", urlTemplate: "https://duckduckgo.com/?q={query}", isDefault: true),
        todayTabTimeoutHours: 24,
        restoreOnLaunch: .restoreAll,
        downloadPath: "/tmp",
        autoplayPolicy: .blockAudio
    ),
    appearance: AppearanceSettings(theme: .system, accentColor: nil, density: .comfortable, sidebarWidth: 220, showTabBar: true, windowTransparency: false),
    privacy: PrivacySettings(doNotTrack: true, blockThirdPartyCookies: true, contentBlockerEnabled: true, popupBlockerEnabled: true),
    reader: ReaderSettings(fontFamily: "System", fontSize: 16, theme: .light),
    keyboardShortcuts: [],
    perSiteSettings: []
)

let llmContext: JSONValue = .object([
    "tab_id": .string(tabId),
    "source": .string("macos-shell"),
    "details": .object([
        "favorite": .bool(true),
        "order": .number(3),
    ])
])

// MARK: - ShellEvent Tests

func testShellEventRoundTrip() {
    let events: [ShellEvent] = [
        .createTab(spaceId: spaceId, url: url, parentId: nil),
        .closeTab(tabId: tabId),
        .activateTab(tabId: tabId),
        .createSpace(name: "Work", color: spaceColor, profileId: "default"),
        .updateSpaceConfig(changes: spaceConfigUpdate),
        .renameSpace(spaceId: spaceId, name: "Renamed"),
        .createFolder(spaceId: spaceId, name: "New Folder", isPinned: false),
        .deleteFolder(spaceId: spaceId, folderId: folderId),
        .moveTabToFolder(spaceId: spaceId, folderId: folderId, tabId: tabId),
        .commandBarQuery(text: "hello", mode: "tab"),
        .updateSettings(changes: settingsUpdate),
    ]

    for event in events {
        do {
            let decoded = try roundTrip(event)
            assertEqual(event, decoded)
        } catch {
            print("FAIL: ShellEvent round-trip failed for \(event) with error: \(error)")
            exit(1)
        }
    }
}

func testCoreUpdateRoundTrip() {
    let updates: [CoreUpdate] = [
        .tabCreated(tab: tab),
        .tabClosed(tabId: tabId, animated: false),
        .activeSpaceChanged(spaceId: spaceId, activeTabId: nil),
        .folderCreated(folder: folder),
        .spaceCreated(space: space),
        .spaceUpdated(spaceId: spaceId, changes: SpaceUpdate(name: "Renamed", color: nil, tabCount: nil, isActive: nil, icon: nil, profileId: nil, profileName: nil, orderIndex: nil)),
        .tabUpdated(tabId: tabId, changes: tabStateUpdate),
        .spaceConfigUpdated(spaceId: spaceId, changes: spaceConfigUpdate),
        .commandBarResults(suggestions: [suggestion]),
        .showFindBar(tabId: tabId),
        .showNotification(notification: notification),
        .zoomChanged(tabId: tabId, zoomLevel: 1.5),
        .settingsChanged(settings: settings),
        .navigationStateChanged(tabId: tabId, url: url, title: "Title", canGoBack: true, canGoForward: false, isLoading: false, progress: 0.5),
        .requestLlmCompletion(requestId: "req-1", requestType: "tidy_tab_title", prompt: "Shorten this title", context: llmContext),
    ]

    for update in updates {
        do {
            let decoded = try roundTrip(update)
            assertEqual(update, decoded)
        } catch {
            print("FAIL: CoreUpdate round-trip failed for \(update) with error: \(error)")
            exit(1)
        }
    }
}

func testKindTags() {
    let shellEventTags: [(ShellEvent, String)] = [
        (.createTab(spaceId: spaceId, url: url, parentId: nil), "create_tab"),
        (.closeTab(tabId: tabId), "close_tab"),
        (.activateTab(tabId: tabId), "activate_tab"),
        (.createSpace(name: "Work", color: spaceColor, profileId: "default"), "create_space"),
        (.updateSpaceConfig(changes: spaceConfigUpdate), "update_space_config"),
        (.renameSpace(spaceId: spaceId, name: "Renamed"), "rename_space"),
        (.createFolder(spaceId: spaceId, name: "New Folder", isPinned: false), "create_folder"),
        (.deleteFolder(spaceId: spaceId, folderId: folderId), "delete_folder"),
        (.moveTabToFolder(spaceId: spaceId, folderId: folderId, tabId: tabId), "move_tab_to_folder"),
        (.commandBarQuery(text: "hello", mode: "tab"), "command_bar_query"),
        (.updateSettings(changes: settingsUpdate), "update_settings"),
    ]

    for (event, expectedTag) in shellEventTags {
        do {
            let json = try jsonString(event)
            assertTrue(json.contains("\"kind\":\"\(expectedTag)\""), "Expected kind tag '\(expectedTag)' in JSON: \(json)")
        } catch {
            print("FAIL: Kind tag check failed for ShellEvent with error: \(error)")
            exit(1)
        }
    }

    let coreUpdateTags: [(CoreUpdate, String)] = [
        (.tabCreated(tab: tab), "tab_created"),
        (.tabClosed(tabId: tabId, animated: false), "tab_closed"),
        (.activeSpaceChanged(spaceId: spaceId, activeTabId: nil), "active_space_changed"),
        (.spaceCreated(space: space), "space_created"),
        (.spaceUpdated(spaceId: spaceId, changes: SpaceUpdate(name: "Renamed", color: nil, tabCount: nil, isActive: nil, icon: nil, profileId: nil, profileName: nil, orderIndex: nil)), "space_updated"),
        (.tabUpdated(tabId: tabId, changes: tabStateUpdate), "tab_updated"),
        (.spaceConfigUpdated(spaceId: spaceId, changes: spaceConfigUpdate), "space_config_updated"),
        (.folderCreated(folder: folder), "folder_created"),
        (.commandBarResults(suggestions: [suggestion]), "command_bar_results"),
        (.showFindBar(tabId: tabId), "show_find_bar"),
        (.showNotification(notification: notification), "show_notification"),
        (.zoomChanged(tabId: tabId, zoomLevel: 1.5), "zoom_changed"),
        (.settingsChanged(settings: settings), "settings_changed"),
        (.navigationStateChanged(tabId: tabId, url: url, title: "Title", canGoBack: true, canGoForward: false, isLoading: false, progress: 0.5), "navigation_state_changed"),
        (.requestLlmCompletion(requestId: "req-1", requestType: "tidy_tab_title", prompt: "Shorten this title", context: llmContext), "request_llm_completion"),
    ]

    for (update, expectedTag) in coreUpdateTags {
        do {
            let json = try jsonString(update)
            assertTrue(json.contains("\"kind\":\"\(expectedTag)\""), "Expected kind tag '\(expectedTag)' in JSON: \(json)")
        } catch {
            print("FAIL: Kind tag check failed for CoreUpdate with error: \(error)")
            exit(1)
        }
    }
}

// MARK: - Space domain round-trips

func testSpaceRoundTrip() {
    let decoded = try! roundTrip(spaceDomain)
    assertEqual(decoded, spaceDomain)
}

func testSpaceConfigRoundTrip() {
    let decoded = try! roundTrip(spaceConfig)
    assertEqual(decoded, spaceConfig)
}

func testSpaceConfigUpdateRoundTrip() {
    let decoded = try! roundTrip(spaceConfigUpdate)
    assertEqual(decoded, spaceConfigUpdate)

    let cleared = try! roundTrip(spaceConfigUpdateIconClear)
    assertEqual(cleared, spaceConfigUpdateIconClear)
}

func testSpaceConfigUpdateSerializationShape() throws {
    let encoder = JSONEncoder()
    let data = try encoder.encode(spaceConfigUpdate)
    guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
        assertTrue(false, "SpaceConfigUpdate should serialize to a JSON object")
        return
    }

    assertEqual(json["spaceId"] as? String, "space-1")
    assertEqual(json["name"] as? String, "Config Space")
    assertTrue(json["color"] != nil, "SpaceConfigUpdate should include color")
    assertTrue(json["icon"] == nil, "SpaceConfigUpdate should omit icon when unchanged")
    assertEqual(json["profileId"] as? String, "default")
}

func testSpaceSwitcherDisplayItemAccessibilityLabel() {
    let activeSpace = SpaceSwitcherDisplayItem(
        id: "space-1",
        space: space,
        name: "Work",
        color: spaceColor,
        theme: nil,
        tabCount: 3,
        isActive: true,
        icon: nil,
        profileName: "Default",
        profileId: "default",
        orderIndex: 0
    )

    assertEqual(activeSpace.accessibilityLabel, "Space: Work, Active, Profile Default, 3 tabs")
}

// MARK: - Main

@main
struct TestRunner {
    static func main() {
        testShellEventRoundTrip()
        testCoreUpdateRoundTrip()
        testKindTags()
        testSpaceRoundTrip()
        testSpaceConfigRoundTrip()
        testSpaceConfigUpdateRoundTrip()
        try! testSpaceConfigUpdateSerializationShape()
        testSpaceSwitcherDisplayItemAccessibilityLabel()
        print("ALL TESTS PASSED")
    }
}
