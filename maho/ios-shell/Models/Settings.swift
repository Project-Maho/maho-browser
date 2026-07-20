import Foundation

enum Theme: String, Codable {
    case light
    case dark
    case system
}

enum Density: String, Codable {
    case compact
    case comfortable
}

enum RestorePolicy: String, Codable {
    case restoreAll = "restore_all"
    case restorePinned = "restore_pinned"
    case startFresh = "start_fresh"
}

enum ReaderTheme: String, Codable {
    case light
    case sepia
    case dark
}

enum AutoplayPolicy: String, Codable {
    case allow
    case blockAll = "block_all"
    case blockAudio = "block_audio"
}

enum PinnedCloseBehavior: String, Codable {
    case `switch` = "switch"
    case reset = "reset"
    case resetSwitch = "reset-switch"
    case unloadSwitch = "unload-switch"
    case resetUnloadSwitch = "reset-unload-switch"
    case close = "close"
}

struct ReaderSettings: Codable {
    let fontFamily: String
    let fontSize: Double
    let theme: ReaderTheme
}

struct SettingsSearchEngine: Codable {
    let name: String
    let urlTemplate: String
    let isDefault: Bool
}

struct AppearanceSettings: Codable {
    let theme: Theme
    let density: Density
    let sidebarWidth: Double
    let showTabBar: Bool
    let windowTransparency: Bool
    var sidebarCollapsed: Bool = false
    var customChromeCss: String? = nil
    var customIconPath: String? = nil
}

struct GeneralSettings: Codable {
    let defaultSearchEngine: SettingsSearchEngine
    let todayTabTimeoutHours: Double
    let restoreOnLaunch: RestorePolicy
    let downloadPath: String
    let autoplayPolicy: AutoplayPolicy
    var archiveTimeoutHours: Double = 24.0
    var siteSearchEntries: [SiteSearchEntry] = []
    var pinnedCloseBehavior: PinnedCloseBehavior = .switch

    private enum CodingKeys: String, CodingKey {
        case defaultSearchEngine, todayTabTimeoutHours, restoreOnLaunch
        case downloadPath, autoplayPolicy
        case archiveTimeoutHours, siteSearchEntries, pinnedCloseBehavior
    }

    init(
        defaultSearchEngine: SettingsSearchEngine,
        todayTabTimeoutHours: Double,
        restoreOnLaunch: RestorePolicy,
        downloadPath: String,
        autoplayPolicy: AutoplayPolicy,
        archiveTimeoutHours: Double = 24.0,
        siteSearchEntries: [SiteSearchEntry] = [],
        pinnedCloseBehavior: PinnedCloseBehavior = .switch
    ) {
        self.defaultSearchEngine = defaultSearchEngine
        self.todayTabTimeoutHours = todayTabTimeoutHours
        self.restoreOnLaunch = restoreOnLaunch
        self.downloadPath = downloadPath
        self.autoplayPolicy = autoplayPolicy
        self.archiveTimeoutHours = archiveTimeoutHours
        self.siteSearchEntries = siteSearchEntries
        self.pinnedCloseBehavior = pinnedCloseBehavior
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        defaultSearchEngine = try container.decode(SettingsSearchEngine.self, forKey: .defaultSearchEngine)
        todayTabTimeoutHours = try container.decode(Double.self, forKey: .todayTabTimeoutHours)
        restoreOnLaunch = try container.decode(RestorePolicy.self, forKey: .restoreOnLaunch)
        downloadPath = try container.decode(String.self, forKey: .downloadPath)
        autoplayPolicy = try container.decode(AutoplayPolicy.self, forKey: .autoplayPolicy)
        archiveTimeoutHours = try container.decodeIfPresent(Double.self, forKey: .archiveTimeoutHours) ?? 24.0
        siteSearchEntries = try container.decodeIfPresent([SiteSearchEntry].self, forKey: .siteSearchEntries) ?? []
        pinnedCloseBehavior = try container.decodeIfPresent(PinnedCloseBehavior.self, forKey: .pinnedCloseBehavior) ?? .switch
    }
}

struct SiteSearchEntry: Codable {
    let keyword: String
    let name: String
    let urlTemplate: String
    var colorName: String? = nil
}

struct PrivacySettings: Codable {
    let doNotTrack: Bool
    let blockThirdPartyCookies: Bool
    let contentBlockerEnabled: Bool
    let popupBlockerEnabled: Bool
    var searchSuggestionsEnabled: Bool = false
    var secureDnsEnabled: Bool = false
    var secureDnsProvider: String = ""
    var secureDnsCustomUrl: String? = nil
    var clearDataOnExit: Bool = false
    var safeBrowsingEnabled: Bool = true
}

enum KeyModifier: String, Codable {
    case ctrl
    case shift
    case alt
    case meta
}

struct KeyboardShortcut: Codable {
    let action: String
    let key: String
    let modifiers: [KeyModifier]
}

enum PermissionPolicy: String, Codable {
    case allow
    case deny
    case ask
}

struct PerSiteSettings: Codable {
    let urlPattern: String
    let zoomLevel: Double?
    let permissions: [String: PermissionPolicy]?
    let notifications: PermissionPolicy?
}

enum ToolbarItemKind: String, Codable {
    case backForward = "back_forward"
    case reload
    case addressBar = "address_bar"
    case share
    case downloads
    case extensions
    case splitView = "split_view"
    case spacer
    case flexibleSpacer = "flexible_spacer"
    case custom
}

struct ToolbarItem: Codable {
    let id: String
    let kind: ToolbarItemKind
    let visible: Bool
    let label: String
}

struct MaxSettings: Codable {
    var enabled: Bool = false
    var pagePreviews: Bool = false
    var tidyTabTitles: Bool = false
    var tidyDownloads: Bool = false
    var tidyTabs: Bool = false
    var aiCommandBar: Bool = false
    var instantLinks: Bool = false
}

enum PasswordAutofillProvider: String, Codable {
    case builtin
    case system
    case disabled
}

struct AutofillSettings: Codable {
    var addressesEnabled: Bool = true
    var paymentsEnabled: Bool = true
    var passwordProvider: PasswordAutofillProvider = .builtin
}

struct AdvancedSettings: Codable {
    var developerMode: Bool = false
    var hardwareAcceleration: Bool = true
    var experimentalFeatures: Bool = false
}

struct NotificationSettings: Codable {
    var enabled: Bool = true
    var calendarNotifications: Bool = true
    var updateNotifications: Bool = true
    var soundEnabled: Bool = true
}

struct Settings: Codable {
    let general: GeneralSettings
    let appearance: AppearanceSettings
    let privacy: PrivacySettings
    let reader: ReaderSettings
    let keyboardShortcuts: [KeyboardShortcut]
    let perSiteSettings: [PerSiteSettings]
    var toolbarItems: [ToolbarItem] = []
    var max: MaxSettings = MaxSettings()
    var autofill: AutofillSettings = AutofillSettings()
    var advanced: AdvancedSettings = AdvancedSettings()
    var notifications: NotificationSettings = NotificationSettings()
}

struct SettingsUpdate: Codable {
    var general: GeneralSettingsUpdate? = nil
    var appearance: AppearanceSettingsUpdate? = nil
    var privacy: PrivacySettingsUpdate? = nil
    var reader: ReaderSettingsUpdate? = nil
    var keyboardShortcuts: [KeyboardShortcut]? = nil
    var perSiteSettings: [PerSiteSettings]? = nil
    var toolbarItems: [ToolbarItem]? = nil
    var max: MaxSettingsUpdate? = nil
    var autofill: AutofillSettingsUpdate? = nil
    var advanced: AdvancedSettingsUpdate? = nil
    var notifications: NotificationSettingsUpdate? = nil
}

struct GeneralSettingsUpdate: Codable {
    var defaultSearchEngine: SettingsSearchEngine? = nil
    var todayTabTimeoutHours: Double? = nil
    var restoreOnLaunch: RestorePolicy? = nil
    var downloadPath: String? = nil
    var autoplayPolicy: AutoplayPolicy? = nil
    var archiveTimeoutHours: Double? = nil
    var siteSearchEntries: [SiteSearchEntry]? = nil
    var pinnedCloseBehavior: PinnedCloseBehavior? = nil
}

struct AppearanceSettingsUpdate: Codable {
    var theme: Theme? = nil
    var density: Density? = nil
    var sidebarWidth: Double? = nil
    var showTabBar: Bool? = nil
    var windowTransparency: Bool? = nil
    var sidebarCollapsed: Bool? = nil
    var customChromeCss: Patchable<String> = .absent
    var customIconPath: Patchable<String> = .absent

    private enum CodingKeys: String, CodingKey {
        case theme, density, sidebarWidth, showTabBar
        case windowTransparency, sidebarCollapsed, customChromeCss, customIconPath
    }
}

extension AppearanceSettingsUpdate {
    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        theme = try container.decodeIfPresent(Theme.self, forKey: .theme)
        density = try container.decodeIfPresent(Density.self, forKey: .density)
        sidebarWidth = try container.decodeIfPresent(Double.self, forKey: .sidebarWidth)
        showTabBar = try container.decodeIfPresent(Bool.self, forKey: .showTabBar)
        windowTransparency = try container.decodeIfPresent(Bool.self, forKey: .windowTransparency)
        sidebarCollapsed = try container.decodeIfPresent(Bool.self, forKey: .sidebarCollapsed)
        customChromeCss = try container.decodePatchable(Patchable<String>.self, forKey: .customChromeCss)
        customIconPath = try container.decodePatchable(Patchable<String>.self, forKey: .customIconPath)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encodeIfPresent(theme, forKey: .theme)
        try container.encodeIfPresent(density, forKey: .density)
        try container.encodeIfPresent(sidebarWidth, forKey: .sidebarWidth)
        try container.encodeIfPresent(showTabBar, forKey: .showTabBar)
        try container.encodeIfPresent(windowTransparency, forKey: .windowTransparency)
        try container.encodeIfPresent(sidebarCollapsed, forKey: .sidebarCollapsed)
        try container.encodePatchable(customChromeCss, forKey: .customChromeCss)
        try container.encodePatchable(customIconPath, forKey: .customIconPath)
    }
}

struct PrivacySettingsUpdate: Codable {
    var doNotTrack: Bool? = nil
    var blockThirdPartyCookies: Bool? = nil
    var contentBlockerEnabled: Bool? = nil
    var popupBlockerEnabled: Bool? = nil
    var searchSuggestionsEnabled: Bool? = nil
    var secureDnsEnabled: Bool? = nil
    var secureDnsProvider: String? = nil
    var secureDnsCustomUrl: Patchable<String> = .absent
    var clearDataOnExit: Bool? = nil
    var safeBrowsingEnabled: Bool? = nil

    private enum CodingKeys: String, CodingKey {
        case doNotTrack, blockThirdPartyCookies, contentBlockerEnabled
        case popupBlockerEnabled, searchSuggestionsEnabled, secureDnsEnabled
        case secureDnsProvider, secureDnsCustomUrl, clearDataOnExit, safeBrowsingEnabled
    }
}

extension PrivacySettingsUpdate {
    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        doNotTrack = try container.decodeIfPresent(Bool.self, forKey: .doNotTrack)
        blockThirdPartyCookies = try container.decodeIfPresent(Bool.self, forKey: .blockThirdPartyCookies)
        contentBlockerEnabled = try container.decodeIfPresent(Bool.self, forKey: .contentBlockerEnabled)
        popupBlockerEnabled = try container.decodeIfPresent(Bool.self, forKey: .popupBlockerEnabled)
        searchSuggestionsEnabled = try container.decodeIfPresent(Bool.self, forKey: .searchSuggestionsEnabled)
        secureDnsEnabled = try container.decodeIfPresent(Bool.self, forKey: .secureDnsEnabled)
        secureDnsProvider = try container.decodeIfPresent(String.self, forKey: .secureDnsProvider)
        secureDnsCustomUrl = try container.decodePatchable(Patchable<String>.self, forKey: .secureDnsCustomUrl)
        clearDataOnExit = try container.decodeIfPresent(Bool.self, forKey: .clearDataOnExit)
        safeBrowsingEnabled = try container.decodeIfPresent(Bool.self, forKey: .safeBrowsingEnabled)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encodeIfPresent(doNotTrack, forKey: .doNotTrack)
        try container.encodeIfPresent(blockThirdPartyCookies, forKey: .blockThirdPartyCookies)
        try container.encodeIfPresent(contentBlockerEnabled, forKey: .contentBlockerEnabled)
        try container.encodeIfPresent(popupBlockerEnabled, forKey: .popupBlockerEnabled)
        try container.encodeIfPresent(searchSuggestionsEnabled, forKey: .searchSuggestionsEnabled)
        try container.encodeIfPresent(secureDnsEnabled, forKey: .secureDnsEnabled)
        try container.encodeIfPresent(secureDnsProvider, forKey: .secureDnsProvider)
        try container.encodePatchable(secureDnsCustomUrl, forKey: .secureDnsCustomUrl)
        try container.encodeIfPresent(clearDataOnExit, forKey: .clearDataOnExit)
        try container.encodeIfPresent(safeBrowsingEnabled, forKey: .safeBrowsingEnabled)
    }
}

struct ReaderSettingsUpdate: Codable {
    var fontFamily: String? = nil
    var fontSize: Double? = nil
    var theme: ReaderTheme? = nil
}

struct MaxSettingsUpdate: Codable {
    var enabled: Bool? = nil
    var pagePreviews: Bool? = nil
    var tidyTabTitles: Bool? = nil
    var tidyDownloads: Bool? = nil
    var tidyTabs: Bool? = nil
    var aiCommandBar: Bool? = nil
    var instantLinks: Bool? = nil
}

struct AutofillSettingsUpdate: Codable {
    var addressesEnabled: Bool? = nil
    var paymentsEnabled: Bool? = nil
    var passwordProvider: PasswordAutofillProvider? = nil
}

struct AdvancedSettingsUpdate: Codable {
    var developerMode: Bool? = nil
    var hardwareAcceleration: Bool? = nil
    var experimentalFeatures: Bool? = nil
}

struct NotificationSettingsUpdate: Codable {
    var enabled: Bool? = nil
    var calendarNotifications: Bool? = nil
    var updateNotifications: Bool? = nil
    var soundEnabled: Bool? = nil
}
