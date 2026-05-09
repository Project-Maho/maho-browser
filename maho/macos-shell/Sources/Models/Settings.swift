import Foundation

// MARK: - Enums

enum Theme: String, Codable, Equatable {
    case light
    case dark
    case system
}

enum Density: String, Codable, Equatable {
    case compact
    case comfortable
}

enum RestorePolicy: String, Codable, Equatable {
    case restoreAll = "restore_all"
    case restorePinned = "restore_pinned"
    case startFresh = "start_fresh"
}

enum ReaderTheme: String, Codable, Equatable {
    case light
    case sepia
    case dark
}

enum AutoplayPolicy: String, Codable, Equatable {
    case allow
    case blockAll = "block_all"
    case blockAudio = "block_audio"
}

enum PermissionPolicy: String, Codable, Equatable {
    case allow
    case deny
    case ask
}

enum ToolbarItemKind: String, Codable, Equatable {
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

// MARK: - Reader Settings

struct ReaderSettings: Codable, Equatable {
    let fontFamily: String
    let fontSize: Double
    let theme: ReaderTheme

    private enum CodingKeys: String, CodingKey {
        case fontFamily, fontSize, theme
    }
}

// MARK: - Search Engine (settings.rs version — used inside GeneralSettings)

struct SettingsSearchEngine: Codable, Equatable {
    let name: String
    let urlTemplate: String
    let isDefault: Bool

    private enum CodingKeys: String, CodingKey {
        case name, urlTemplate, isDefault
    }
}

// MARK: - Appearance Settings

struct AppearanceSettings: Codable, Equatable {
    let theme: Theme
    let accentColor: Color?
    let density: Density
    let sidebarWidth: Double
    let showTabBar: Bool
    let windowTransparency: Bool
    var sidebarCollapsed: Bool = false
    var customChromeCss: String? = nil
    var customIconPath: String? = nil

    private enum CodingKeys: String, CodingKey {
        case theme, accentColor, density, sidebarWidth, showTabBar
        case windowTransparency, sidebarCollapsed, customChromeCss, customIconPath
    }
}

// MARK: - General Settings

struct GeneralSettings: Codable, Equatable {
    let defaultSearchEngine: SettingsSearchEngine
    let todayTabTimeoutHours: Double
    let restoreOnLaunch: RestorePolicy
    let downloadPath: String
    let autoplayPolicy: AutoplayPolicy
    var archiveTimeoutHours: Double = 24.0
    var siteSearchEntries: [SiteSearchEntry] = []

    private enum CodingKeys: String, CodingKey {
        case defaultSearchEngine, todayTabTimeoutHours, restoreOnLaunch
        case downloadPath, autoplayPolicy, archiveTimeoutHours, siteSearchEntries
    }
}

struct SiteSearchEntry: Codable, Equatable {
    let keyword: String
    let name: String
    let urlTemplate: String
    var colorName: String? = nil

    private enum CodingKeys: String, CodingKey {
        case keyword, name, urlTemplate, colorName
    }
}

// MARK: - Privacy Settings

struct PrivacySettings: Codable, Equatable {
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

    private enum CodingKeys: String, CodingKey {
        case doNotTrack, blockThirdPartyCookies, contentBlockerEnabled
        case popupBlockerEnabled, searchSuggestionsEnabled, secureDnsEnabled
        case secureDnsProvider, secureDnsCustomUrl, clearDataOnExit, safeBrowsingEnabled
    }
}

// MARK: - Per-Site Settings

struct PerSiteSettings: Codable, Equatable {
    let urlPattern: String
    let zoomLevel: Double?
    let permissions: [String: PermissionPolicy]?
    let notifications: PermissionPolicy?

    private enum CodingKeys: String, CodingKey {
        case urlPattern, zoomLevel, permissions, notifications
    }
}

// MARK: - Toolbar

struct ToolbarItem: Codable, Equatable {
    let id: String
    let kind: ToolbarItemKind
    let visible: Bool
    let label: String

    private enum CodingKeys: String, CodingKey {
        case id, kind, visible, label
    }
}

// MARK: - Max Settings

struct MaxSettings: Codable, Equatable {
    var enabled: Bool = false
    var pagePreviews: Bool = false
    var tidyTabTitles: Bool = false
    var tidyDownloads: Bool = false
    var tidyTabs: Bool = false
    var aiCommandBar: Bool = false
    var instantLinks: Bool = false

    private enum CodingKeys: String, CodingKey {
        case enabled, pagePreviews, tidyTabTitles, tidyDownloads
        case tidyTabs, aiCommandBar, instantLinks
    }
}

struct MaxSettingsUpdate: Codable, Equatable {
    var enabled: Bool? = nil
    var pagePreviews: Bool? = nil
    var tidyTabTitles: Bool? = nil
    var tidyDownloads: Bool? = nil
    var tidyTabs: Bool? = nil
    var aiCommandBar: Bool? = nil
    var instantLinks: Bool? = nil

    private enum CodingKeys: String, CodingKey {
        case enabled, pagePreviews, tidyTabTitles, tidyDownloads
        case tidyTabs, aiCommandBar, instantLinks
    }
}

// MARK: - Autofill Settings

enum PasswordAutofillProvider: String, Codable, Equatable {
    case chromium
    case onepassword
    case bitwarden
}

struct AutofillSettings: Codable, Equatable {
    var addressesEnabled: Bool = true
    var paymentsEnabled: Bool = true
    var passwordsEnabled: Bool = true
    var passwordProvider: PasswordAutofillProvider = .chromium

    private enum CodingKeys: String, CodingKey {
        case addressesEnabled, paymentsEnabled, passwordsEnabled, passwordProvider
    }
}

struct AutofillSettingsUpdate: Codable, Equatable {
    var addressesEnabled: Bool? = nil
    var paymentsEnabled: Bool? = nil
    var passwordsEnabled: Bool? = nil
    var passwordProvider: PasswordAutofillProvider? = nil

    private enum CodingKeys: String, CodingKey {
        case addressesEnabled, paymentsEnabled, passwordsEnabled, passwordProvider
    }
}

// MARK: - Advanced Settings

struct AdvancedSettings: Codable, Equatable {
    var developerMode: Bool = false
    var hardwareAcceleration: Bool = true
    var experimentalFeatures: Bool = false

    private enum CodingKeys: String, CodingKey {
        case developerMode, hardwareAcceleration, experimentalFeatures
    }
}

struct AdvancedSettingsUpdate: Codable, Equatable {
    var developerMode: Bool? = nil
    var hardwareAcceleration: Bool? = nil
    var experimentalFeatures: Bool? = nil

    private enum CodingKeys: String, CodingKey {
        case developerMode, hardwareAcceleration, experimentalFeatures
    }
}

// MARK: - Notification Settings

struct NotificationSettings: Codable, Equatable {
    var enabled: Bool = true
    var calendarNotifications: Bool = true
    var updateNotifications: Bool = true
    var soundEnabled: Bool = true

    private enum CodingKeys: String, CodingKey {
        case enabled, calendarNotifications, updateNotifications, soundEnabled
    }
}

struct NotificationSettingsUpdate: Codable, Equatable {
    var enabled: Bool? = nil
    var calendarNotifications: Bool? = nil
    var updateNotifications: Bool? = nil
    var soundEnabled: Bool? = nil

    private enum CodingKeys: String, CodingKey {
        case enabled, calendarNotifications, updateNotifications, soundEnabled
    }
}

// MARK: - Main Settings

struct Settings: Codable, Equatable {
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

    private enum CodingKeys: String, CodingKey {
        case general, appearance, privacy, reader, keyboardShortcuts
        case perSiteSettings, toolbarItems, max, autofill, advanced, notifications
    }
}

// MARK: - Settings Update

struct SettingsUpdate: Codable, Equatable {
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

    private enum CodingKeys: String, CodingKey {
        case general, appearance, privacy, reader, keyboardShortcuts
        case perSiteSettings, toolbarItems, max, autofill, advanced, notifications
    }
}

struct GeneralSettingsUpdate: Codable, Equatable {
    var defaultSearchEngine: SettingsSearchEngine? = nil
    var todayTabTimeoutHours: Double? = nil
    var restoreOnLaunch: RestorePolicy? = nil
    var downloadPath: String? = nil
    var autoplayPolicy: AutoplayPolicy? = nil
    var archiveTimeoutHours: Double? = nil
    var siteSearchEntries: [SiteSearchEntry]? = nil

    private enum CodingKeys: String, CodingKey {
        case defaultSearchEngine, todayTabTimeoutHours, restoreOnLaunch
        case downloadPath, autoplayPolicy, archiveTimeoutHours, siteSearchEntries
    }
}

struct AppearanceSettingsUpdate: Codable, Equatable {
    var theme: Theme? = nil
    var accentColor: Color?? = nil
    var density: Density? = nil
    var sidebarWidth: Double? = nil
    var showTabBar: Bool? = nil
    var windowTransparency: Bool? = nil
    var sidebarCollapsed: Bool? = nil
    var customChromeCss: String?? = nil
    var customIconPath: String?? = nil

    private enum CodingKeys: String, CodingKey {
        case theme, accentColor, density, sidebarWidth, showTabBar
        case windowTransparency, sidebarCollapsed, customChromeCss, customIconPath
    }
}

struct PrivacySettingsUpdate: Codable, Equatable {
    var doNotTrack: Bool? = nil
    var blockThirdPartyCookies: Bool? = nil
    var contentBlockerEnabled: Bool? = nil
    var popupBlockerEnabled: Bool? = nil
    var searchSuggestionsEnabled: Bool? = nil
    var secureDnsEnabled: Bool? = nil
    var secureDnsProvider: String? = nil
    var secureDnsCustomUrl: String?? = nil
    var clearDataOnExit: Bool? = nil
    var safeBrowsingEnabled: Bool? = nil

    private enum CodingKeys: String, CodingKey {
        case doNotTrack, blockThirdPartyCookies, contentBlockerEnabled
        case popupBlockerEnabled, searchSuggestionsEnabled, secureDnsEnabled
        case secureDnsProvider, secureDnsCustomUrl, clearDataOnExit, safeBrowsingEnabled
    }
}

struct ReaderSettingsUpdate: Codable, Equatable {
    var fontFamily: String? = nil
    var fontSize: Double? = nil
    var theme: ReaderTheme? = nil

    private enum CodingKeys: String, CodingKey {
        case fontFamily, fontSize, theme
    }
}

// MARK: - App Icon

struct AppIconOption: Codable, Equatable {
    let id: String
    let name: String
    let previewPath: String
    let isDefault: Bool

    private enum CodingKeys: String, CodingKey {
        case id, name, previewPath, isDefault
    }
}
