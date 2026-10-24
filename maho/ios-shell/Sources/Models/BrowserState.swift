import Foundation
import os.log

struct SettingsBundle: Codable {
    var searchSuggestions: Bool = true
    var popupBlocker: Bool = true
    var contentBlocker: Bool = false
}

/// State for the archive / legacy settings sheets. It does NOT drain the core
/// update queue: `MainBrowserView` is the single consumer of `tickDrain()`, and a
/// second drainer here would silently steal updates from it. Callers refresh this
/// object explicitly (`refreshState()`, `loadArchivedTabs()`).
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
    private let log = Logger(subsystem: "dev.maho.browser", category: "BrowserState")

    init() {
        refreshState()
    }

    func goBack() {
        guard let tabId = bridge.getActiveTabId() else { return }
        bridge.sendEvent(.goBack(tabId: tabId))
        refreshState()
    }

    func goForward() {
        guard let tabId = bridge.getActiveTabId() else { return }
        bridge.sendEvent(.goForward(tabId: tabId))
        refreshState()
    }

    func refreshState() {
        tabs = bridge.getTabViewModels()
        spaces = bridge.getSpaceViewModels()
        downloads = bridge.getDownloads()

        let activeSpaceId = bridge.getActiveSpaceId()
        folders = activeSpaceId.map { bridge.getFolderViewModels(spaceId: $0) } ?? []

        let activeTabId = bridge.getActiveTabId()
        currentUrl = tabs.first(where: { $0.id == activeTabId })?.url ?? ""

        if let viewModel = bridge.getSettings() {
            for section in viewModel.sections {
                for item in section.items {
                    guard let value = item.value.value as? Bool else { continue }
                    switch item.key {
                    // Keys emitted by maho-core settings_manager.rs get_settings_view_model.
                    case "search_suggestions": settings.searchSuggestions = value
                    case "popup_blocker": settings.popupBlocker = value
                    case "content_blocker":
                        settings.contentBlocker = value
                        contentBlockerEnabled = value
                    default: break
                    }
                }
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
        bridge.setContentBlocker(contentBlockerEnabled)
        for rule in contentRules {
            bridge.sendEvent(.toggleFilterList(id: rule.id, enabled: enabledRuleIds.contains(rule.id)))
        }
    }

    func updateSetting(key: String, value: AnyCodable) {
        guard let enabled = value.value as? Bool else {
            log.warning("Ignoring non-boolean setting update for \(key, privacy: .public)")
            return
        }
        switch key {
        case "general.search_suggestions":
            bridge.updatePrivacySettings(PrivacySettingsUpdate(searchSuggestionsEnabled: enabled))
        case "general.popup_blocker":
            bridge.updatePrivacySettings(PrivacySettingsUpdate(popupBlockerEnabled: enabled))
        case "privacy.content_blocker":
            bridge.setContentBlocker(enabled)
        default:
            log.warning("Ignoring unknown setting key \(key, privacy: .public)")
            return
        }
        refreshState()
    }
}
