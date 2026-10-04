import Foundation





struct SettingsBundle: Codable {
    var searchSuggestions: Bool = true
    var popupBlocker: Bool = true
    var contentBlocker: Bool = false
}

@MainActor
class BrowserState: ObservableObject {
    @Published var currentUrl: String = ""
    @Published var canGoBack: Bool = false
    @Published var canGoForward: Bool = false
    @Published var tabs: [TabViewModel] = []
    @Published var spaces: [SpaceViewModel] = []
    @Published var folders: [FolderViewModel] = []
    @Published var downloads: [DownloadViewModel] = []
    @Published var archivedTabs: [ArchivedTabViewModel] = []
    @Published var contentRules: [ContentRule] = []
    @Published var enabledRuleIds: Set<String> = []
    @Published var contentBlockerEnabled: Bool = false
    @Published var settings: SettingsBundle = SettingsBundle()
    @Published var showCommandBar: Bool = false
    @Published var showBookmarks: Bool = false
    @Published var showHistory: Bool = false
    @Published var showDownloads: Bool = false
    @Published var showArchive: Bool = false
    @Published var showSettings: Bool = false

    private let bridge = MahoBridge.shared
    private var tickTimer: Timer?

    init() {
        refreshState()
        tickTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.drainCoreUpdates()
            }
        }
    }

    deinit {
        tickTimer?.invalidate()
    }

    func goBack() {
        bridge.sendEvent(name: "navigate_back")
        refreshState()
    }

    func goForward() {
        bridge.sendEvent(name: "navigate_forward")
        refreshState()
    }

    func refreshState() {
        guard let json = bridge.getStateJson() else { return }
        guard let data = json.data(using: .utf8) else { return }
        guard let dict = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return }

        if let url = dict["currentUrl"] as? String {
            currentUrl = url
        }
        if let back = dict["canGoBack"] as? Bool {
            canGoBack = back
        }
        if let forward = dict["canGoForward"] as? Bool {
            canGoForward = forward
        }
        if let tabData = try? JSONSerialization.data(withJSONObject: dict["tabs"] ?? []),
           let decoded = try? JSONDecoder().decode([TabViewModel].self, from: tabData) {
            tabs = decoded
        }
        if let spaceData = try? JSONSerialization.data(withJSONObject: dict["spaces"] ?? []),
           let decoded = try? JSONDecoder().decode([SpaceViewModel].self, from: spaceData) {
            spaces = decoded
        }
        if let folderData = try? JSONSerialization.data(withJSONObject: dict["folders"] ?? []),
           let decoded = try? JSONDecoder().decode([FolderViewModel].self, from: folderData) {
            folders = decoded
        }
        if let downloadData = try? JSONSerialization.data(withJSONObject: dict["downloads"] ?? []),
           let decoded = try? JSONDecoder().decode([DownloadViewModel].self, from: downloadData) {
            downloads = decoded
        }
        if let settingsData = try? JSONSerialization.data(withJSONObject: dict["settings"] ?? [:]),
           let decoded = try? JSONDecoder().decode(SettingsBundle.self, from: settingsData) {
            settings = decoded
        }
    }

    func drainCoreUpdates() {
        guard let updatesJson = bridge.tickJson() else { return }
        guard let data = updatesJson.data(using: .utf8) else { return }
        guard let updates = try? JSONDecoder().decode([CoreUpdate].self, from: data) else { return }
        processCoreUpdates(updates)
    }

    func processCoreUpdates(_ updates: [CoreUpdate]) {
        for update in updates {
            switch update {
            case .tabsMigrated:
                refreshState()
            case .openPeekTab(let url, let sourceTabId):
                bridge.sendEvent(name: "open_peek_tab", payload: ["url": url.value, "sourceTabId": sourceTabId])
            case .favoriteLimitReached:
                break
            default:
                break
            }
        }
    }

    func loadArchivedTabs() {
        guard let spaceId = bridge.getActiveSpaceId() else { return }
        guard let json = bridge.getArchivedTabs(spaceId: spaceId) else { return }
        guard let data = json.data(using: .utf8) else { return }
        archivedTabs = (try? JSONDecoder().decode([ArchivedTabViewModel].self, from: data)) ?? []
    }

    func restoreArchivedTab(tabId: String) {
        bridge.sendEvent(.restoreArchivedTab(tabId: tabId))
        refreshState()
        loadArchivedTabs()
    }

    func updateContentRules() {
        let payload: [String: Any] = [
            "enabled": contentBlockerEnabled,
            "ruleIds": Array(enabledRuleIds)
        ]
        bridge.sendEvent(name: "update_content_rules", payload: payload)
    }

    func updateSetting(key: String, value: AnyCodable) {
        let payload: [String: Any] = ["key": key, "value": encodeAnyCodable(value)]
        bridge.sendEvent(name: "update_setting", payload: payload)
        refreshState()
    }

    private func encodeAnyCodable(_ value: AnyCodable) -> Any {
        return value.value ?? ""
    }
}
