import Foundation

struct TabViewModel: Identifiable, Codable {
    let id: String
    let title: String
    let url: String
}

struct SpaceViewModel: Identifiable, Codable {
    let id: String
    let name: String
}

struct FolderViewModel: Identifiable, Codable {
    let id: String
    let name: String
}

struct DownloadViewModel: Identifiable, Codable {
    let id: String
    let filename: String
}

enum CoreUpdate: Codable {
    case tabsMigrated
    case openPeekTab(tabId: String)
    case favoriteLimitReached
}

enum AnyCodable: Codable {
    case bool(Bool)
    case string(String)
    case int(Int)
    case double(Double)

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let value = try? container.decode(Bool.self) {
            self = .bool(value)
        } else if let value = try? container.decode(Int.self) {
            self = .int(value)
        } else if let value = try? container.decode(Double.self) {
            self = .double(value)
        } else if let value = try? container.decode(String.self) {
            self = .string(value)
        } else {
            throw DecodingError.typeMismatch(AnyCodable.self, DecodingError.Context(codingPath: decoder.codingPath, debugDescription: "Unsupported AnyCodable value"))
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .bool(let value): try container.encode(value)
        case .string(let value): try container.encode(value)
        case .int(let value): try container.encode(value)
        case .double(let value): try container.encode(value)
        }
    }
}

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
    @Published var archivedTabs: [TabViewModel] = []
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

    private let bridge = MahoBridge()
    private var tickTimer: Timer?

    init() {
        refreshState()
        tickTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.tick()
            }
        }
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

    func tick() {
        guard let updatesJson = bridge.tick() else { return }
        guard let data = updatesJson.data(using: .utf8) else { return }
        guard let updates = try? JSONDecoder().decode([CoreUpdate].self, from: data) else { return }
        processCoreUpdates(updates)
    }

    func processCoreUpdates(_ updates: [CoreUpdate]) {
        for update in updates {
            switch update {
            case .tabsMigrated:
                refreshState()
            case .openPeekTab(let tabId):
                bridge.sendEvent(name: "open_peek_tab", payload: ["tabId": tabId])
            case .favoriteLimitReached:
                break
            }
        }
    }

    func loadArchivedTabs() {
        guard let spaceId = activeSpaceId() else { return }
        guard let json = bridge.getArchivedTabs(spaceId: spaceId) else { return }
        guard let data = json.data(using: .utf8) else { return }
        archivedTabs = (try? JSONDecoder().decode([TabViewModel].self, from: data)) ?? []
    }

    func restoreArchivedTab(tabId: String) {
        bridge.sendEvent(name: "restore_archived_tab", payload: ["tabId": tabId])
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

    private func activeSpaceId() -> String? {
        spaces.first { $0.id == spaces.first?.id }?.id ?? spaces.first?.id
    }

    private func encodeAnyCodable(_ value: AnyCodable) -> Any {
        switch value {
        case .bool(let v): return v
        case .string(let v): return v
        case .int(let v): return v
        case .double(let v): return v
        }
    }
}
