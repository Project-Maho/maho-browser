import Foundation

enum TabLifecycleState: Codable, Equatable {
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
        case "active":
            self = .active
        case "frozen":
            self = .frozen
        case "suspended":
            let snapshot = try container.decode(TabSnapshot.self, forKey: .snapshot)
            self = .suspended(snapshot: snapshot)
        case "archived":
            let metadataOnly = try container.decode(Bool.self, forKey: .metadataOnly)
            self = .archived(metadataOnly: metadataOnly)
        default:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown TabLifecycleState kind: \(kind)"
            )
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .active:
            try container.encode("active", forKey: .kind)
        case .frozen:
            try container.encode("frozen", forKey: .kind)
        case .suspended(let snapshot):
            try container.encode("suspended", forKey: .kind)
            try container.encode(snapshot, forKey: .snapshot)
        case .archived(let metadataOnly):
            try container.encode("archived", forKey: .kind)
            try container.encode(metadataOnly, forKey: .metadataOnly)
        }
    }
}

struct Tab: Codable, Equatable {
    let id: TabId
    let parentId: TabId?
    let spaceId: SpaceId
    let url: MahoUrl
    let title: String
    let favicon: ImageData?
    let state: TabLifecycleState
    let isPinned: Bool
    let isFavorite: Bool
    let isMuted: Bool
    let zoomLevel: Double
    let createdAt: MahoDateTime
    let lastActiveAt: MahoDateTime
    let scrollPosition: ScrollPosition
    let pinnedUrl: MahoUrl?

    private enum CodingKeys: String, CodingKey {
        case id, parentId, spaceId, url, title, favicon, state
        case isPinned, isFavorite, isMuted, zoomLevel
        case createdAt, lastActiveAt, scrollPosition, pinnedUrl
    }
}

struct ThemeColor: Codable, Equatable {
    let hue: Double
    let saturation: Double
    let brightness: Double
    var isCustom: Bool = false
    var isPrimary: Bool = false

    private enum CodingKeys: String, CodingKey {
        case hue, saturation, brightness, isCustom, isPrimary
    }

    init(hue: Double, saturation: Double, brightness: Double, isCustom: Bool = false, isPrimary: Bool = false) {
        self.hue = hue
        self.saturation = saturation
        self.brightness = brightness
        self.isCustom = isCustom
        self.isPrimary = isPrimary
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        hue = try container.decode(Double.self, forKey: .hue)
        saturation = try container.decode(Double.self, forKey: .saturation)
        brightness = try container.decode(Double.self, forKey: .brightness)
        isCustom = try container.decodeIfPresent(Bool.self, forKey: .isCustom) ?? false
        isPrimary = try container.decodeIfPresent(Bool.self, forKey: .isPrimary) ?? false
    }
}

enum ColorHarmony: String, Codable, Equatable {
    case complementary
    case singleAnalogous = "single_analogous"
    case splitComplementary = "split_complementary"
    case analogous
    case triadic
    case custom
}

enum SpaceTheme: Codable, Equatable {
    case solid(color: SpaceColor)
    case gradient(gradientColors: [ThemeColor], harmony: ColorHarmony, opacity: Double, texture: Double)

    private enum CodingKeys: String, CodingKey {
        case type, color, gradientColors, harmony, opacity, texture
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let type = try container.decode(String.self, forKey: .type)
        switch type {
        case "solid":
            let color = try container.decode(SpaceColor.self, forKey: .color)
            self = .solid(color: color)
        case "gradient":
            let colors = try container.decode([ThemeColor].self, forKey: .gradientColors)
            let harmony = try container.decode(ColorHarmony.self, forKey: .harmony)
            let opacity = try container.decode(Double.self, forKey: .opacity)
            let texture = try container.decode(Double.self, forKey: .texture)
            self = .gradient(gradientColors: colors, harmony: harmony, opacity: opacity, texture: texture)
        default:
            throw DecodingError.dataCorruptedError(forKey: .type, in: container, debugDescription: "Unknown SpaceTheme type: \(type)")
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .solid(let color):
            try container.encode("solid", forKey: .type)
            try container.encode(color, forKey: .color)
        case .gradient(let colors, let harmony, let opacity, let texture):
            try container.encode("gradient", forKey: .type)
            try container.encode(colors, forKey: .gradientColors)
            try container.encode(harmony, forKey: .harmony)
            try container.encode(opacity, forKey: .opacity)
            try container.encode(texture, forKey: .texture)
        }
    }

    /// Extract the primary/base color for backward-compatible rendering.
    var baseColor: SpaceColor {
        switch self {
        case .solid(let color):
            return color
        case .gradient(let colors, _, _, _):
            if let primary = colors.first(where: { $0.isPrimary }) ?? colors.first {
                return SpaceColor(hue: primary.hue, saturation: primary.saturation, brightness: primary.brightness)
            }
            return SpaceColor(hue: 0.58, saturation: 0.8, brightness: 0.9)
        }
    }
}

struct SpaceColor: Codable, Equatable {
    let hue: Double
    let saturation: Double
    let brightness: Double
    let grain: Double

    init(hue: Double, saturation: Double, brightness: Double, grain: Double = 0.0) {
        self.hue = hue
        self.saturation = saturation
        self.brightness = brightness
        self.grain = grain
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        hue = try container.decode(Double.self, forKey: .hue)
        saturation = try container.decode(Double.self, forKey: .saturation)
        brightness = try container.decode(Double.self, forKey: .brightness)
        grain = try container.decodeIfPresent(Double.self, forKey: .grain) ?? 0.0
    }

    private enum CodingKeys: String, CodingKey {
        case hue, saturation, brightness, grain
    }
}

struct SpaceConfigUpdate: Codable, Equatable {
    let spaceId: SpaceId
    let name: String?
    let color: SpaceColor?
    let theme: SpaceTheme?
    let icon: String??
    let profileId: ProfileId?

    private enum CodingKeys: String, CodingKey {
        case spaceId, name, color, theme, icon, profileId
    }
}

struct ThemeStoreEntry: Codable, Identifiable {
    let id: String
    let name: String
    let description: String
    let author: String
    let version: String
    /// Raw SpaceTheme JSON — decode separately
    let theme: SpaceTheme
    let previewUrl: String?
    let downloadCount: Int
    let createdAt: String
    let updatedAt: String
    let tags: [String]
}

struct ThemeStoreManifest: Codable {
    let version: Int
    let themes: [ThemeStoreEntry]
}

struct CssMod: Codable, Identifiable {
    let id: String
    let name: String
    let description: String?
    let author: String?
    let version: String?
    let css: String
    var enabled: Bool
    let homepage: String?
    let sourceUrl: String?
    let createdAt: String
    let updatedAt: String
}

struct ATCRule: Codable, Equatable {
    let id: String
    let condition: ATCCondition
    let action: ATCAction
    let enabled: Bool

    private enum CodingKeys: String, CodingKey {
        case id, condition, action, enabled
    }
}

enum ATCAction: Codable, Equatable {
    case close
    case archive
    case route(spaceId: SpaceId)

    private enum CodingKeys: String, CodingKey {
        case kind
        case spaceId = "space_id"
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let str = try? container.decode(String.self) {
            switch str {
            case "close": self = .close; return
            case "archive": self = .archive; return
            default: break
            }
        }
        let keyed = try decoder.container(keyedBy: CodingKeys.self)
        let spaceId = try keyed.decode(SpaceId.self, forKey: .spaceId)
        self = .route(spaceId: spaceId)
    }

    func encode(to encoder: Encoder) throws {
        switch self {
        case .close:
            var container = encoder.singleValueContainer()
            try container.encode("close")
        case .archive:
            var container = encoder.singleValueContainer()
            try container.encode("archive")
        case .route(let spaceId):
            var container = encoder.container(keyedBy: CodingKeys.self)
            try container.encode(spaceId, forKey: .spaceId)
        }
    }
}

enum ATCCondition: Codable, Equatable {
    case inactiveDuration(hours: Double)
    case tabCountExceeded(maxTabs: UInt32)
    case urlPattern(pattern: String)
    case urlContains(text: String)
    case urlEquals(url: String)

    private enum CodingKeys: String, CodingKey {
        case kind
        case hours
        case maxTabs = "max_tabs"
        case pattern
        case text
        case url
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)
        switch kind {
        case "inactive_duration":
            let hours = try container.decode(Double.self, forKey: .hours)
            self = .inactiveDuration(hours: hours)
        case "tab_count_exceeded":
            let maxTabs = try container.decode(UInt32.self, forKey: .maxTabs)
            self = .tabCountExceeded(maxTabs: maxTabs)
        case "url_pattern":
            let pattern = try container.decode(String.self, forKey: .pattern)
            self = .urlPattern(pattern: pattern)
        case "url_contains":
            let text = try container.decode(String.self, forKey: .text)
            self = .urlContains(text: text)
        case "url_equals":
            let url = try container.decode(String.self, forKey: .url)
            self = .urlEquals(url: url)
        default:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown ATCCondition kind: \(kind)"
            )
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .inactiveDuration(let hours):
            try container.encode("inactive_duration", forKey: .kind)
            try container.encode(hours, forKey: .hours)
        case .tabCountExceeded(let maxTabs):
            try container.encode("tab_count_exceeded", forKey: .kind)
            try container.encode(maxTabs, forKey: .maxTabs)
        case .urlPattern(let pattern):
            try container.encode("url_pattern", forKey: .kind)
            try container.encode(pattern, forKey: .pattern)
        case .urlContains(let text):
            try container.encode("url_contains", forKey: .kind)
            try container.encode(text, forKey: .text)
        case .urlEquals(let url):
            try container.encode("url_equals", forKey: .kind)
            try container.encode(url, forKey: .url)
        }
    }
}

struct Space: Codable, Equatable {
    let id: SpaceId
    let profileId: ProfileId
    let name: String
    let color: SpaceColor
    let theme: SpaceTheme?
    let icon: String?
    let tabOrder: [TabId]
    let folders: [Folder]
    let atcRules: [ATCRule]
    let isActive: Bool
    let createdAt: MahoDateTime

    private enum CodingKeys: String, CodingKey {
        case id, profileId, name, color, theme, icon, tabOrder
        case folders, atcRules, isActive, createdAt
    }
}

struct SpaceConfig: Codable, Equatable {
    let color: SpaceColor
    let theme: SpaceTheme?
    let icon: String?
    let name: String
    let profileId: ProfileId?

    private enum CodingKeys: String, CodingKey {
        case color, theme, icon, name, profileId
    }
}

struct Folder: Codable, Equatable {
    let id: FolderId
    let name: String
    let tabIds: [TabId]
    let isExpanded: Bool
    var isPinned: Bool = false
    let parentFolderId: FolderId?

    private enum CodingKeys: String, CodingKey {
        case id, name, tabIds, isExpanded, isPinned, parentFolderId
    }
}

struct ProfileConfig: Codable, Equatable {
    let id: ProfileId
    let name: String
    let avatarColor: String
    let defaultSearchEngine: SettingsSearchEngine
    let downloadPath: String
    let archiveTimeoutHours: Double?
    let dataStoreId: String?

    private enum CodingKeys: String, CodingKey {
        case id, name, avatarColor, defaultSearchEngine
        case downloadPath, archiveTimeoutHours, dataStoreId
    }
}

enum TextCase: String, Codable, Equatable {
    case none
    case upper
    case lower
    case capitalize
}

struct ColorBoost: Codable, Equatable {
    let enableColorBoost: Bool
    let dotAngleDeg: Double
    let secondaryDotAngleDegDelta: Double
    let brightness: Double
    let saturation: Double
    let contrast: Double
    let autoTheme: Bool
    let smartInvert: Bool
}

struct TypographyBoost: Codable, Equatable {
    let fontFamily: String?
    let textCaseOverride: TextCase
    let sizeOverride: Float?
}

struct Boost: Codable, Equatable {
    let id: BoostId
    let domain: String
    let name: String
    let color: ColorBoost
    let typography: TypographyBoost
    let zapSelectors: [String]
    let customCss: String

    private enum CodingKeys: String, CodingKey {
        case id, domain, name, color, typography, zapSelectors, customCss
    }
}

struct ColorBoostUpdate: Codable, Equatable {
    let enableColorBoost: Bool?
    let dotAngleDeg: Double?
    let secondaryDotAngleDegDelta: Double?
    let brightness: Double?
    let saturation: Double?
    let contrast: Double?
    let autoTheme: Bool?
    let smartInvert: Bool?
}

struct TypographyBoostUpdate: Codable, Equatable {
    let fontFamily: String??
    let textCaseOverride: TextCase?
    let sizeOverride: Float??
}

struct BoostUpdate: Codable, Equatable {
    let name: String?
    let color: ColorBoostUpdate?
    let typography: TypographyBoostUpdate?
    let zapSelectors: [String]?
    let customCss: String?
}

struct TrafficRule: Codable, Equatable {
    let id: String
    let urlPattern: String
    let matchType: MatchType
    let targetSpaceId: SpaceId
    let enabled: Bool

    private enum CodingKeys: String, CodingKey {
        case id, urlPattern, matchType, targetSpaceId, enabled
    }
}

enum MatchType: String, Codable, Equatable {
    case contains
    case equals
    case regex
}

enum DefaultLinkBehavior: Codable, Equatable {
    case currentSpace
    case mostRecentSpace
    case specificSpace(spaceId: SpaceId)

    private enum CodingKeys: String, CodingKey {
        case kind
        case spaceId = "space_id"
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let str = try? container.decode(String.self) {
            switch str {
            case "current_space": self = .currentSpace; return
            case "most_recent_space": self = .mostRecentSpace; return
            default: break
            }
        }
        let keyed = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try keyed.decode(String.self, forKey: .kind)
        if kind == "specific_space" {
            let spaceId = try keyed.decode(SpaceId.self, forKey: .spaceId)
            self = .specificSpace(spaceId: spaceId)
            return
        }
        throw DecodingError.dataCorruptedError(
            forKey: .kind, in: keyed,
            debugDescription: "Unknown DefaultLinkBehavior kind: \(kind)"
        )
    }

    func encode(to encoder: Encoder) throws {
        switch self {
        case .currentSpace:
            var container = encoder.singleValueContainer()
            try container.encode("current_space")
        case .mostRecentSpace:
            var container = encoder.singleValueContainer()
            try container.encode("most_recent_space")
        case .specificSpace(let spaceId):
            var container = encoder.container(keyedBy: CodingKeys.self)
            try container.encode("specific_space", forKey: .kind)
            try container.encode(spaceId, forKey: .spaceId)
        }
    }
}

struct AccountInfo: Codable, Equatable {
    let email: String
    let displayName: String?
    let avatarUrl: String?
    let syncEnabled: Bool

    private enum CodingKeys: String, CodingKey {
        case email, displayName, avatarUrl, syncEnabled
    }
}

enum SyncState: Codable, Equatable {
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
        case "idle":
            self = .idle
        case "syncing":
            let progress = try container.decode(Double.self, forKey: .progress)
            self = .syncing(progress: progress)
        case "synced":
            let lastSyncAt = try container.decode(String.self, forKey: .lastSyncAt)
            self = .synced(lastSyncAt: lastSyncAt)
        case "error":
            let message = try container.decode(String.self, forKey: .message)
            self = .error(message: message)
        case "offline":
            self = .offline
        default:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown SyncState kind: \(kind)"
            )
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

struct SavedPassword: Codable, Equatable {
    let id: String
    let domain: String
    let username: String
    let createdAt: String
    let lastUsed: String?
    let password: String?

    private enum CodingKeys: String, CodingKey {
        case id, domain, username, createdAt, lastUsed, password
    }
}

struct AutofillAddress: Codable, Equatable {
    let id: String
    let name: String
    let street: String
    let city: String
    let state: String
    let zip: String
    let country: String
    let phone: String?
    let email: String?

    private enum CodingKeys: String, CodingKey {
        case id, name, street, city, state, zip, country, phone, email
    }
}

struct AutofillPayment: Codable, Equatable {
    let id: String
    let cardName: String
    let lastFour: String
    let expiry: String

    private enum CodingKeys: String, CodingKey {
        case id, cardName, lastFour, expiry
    }
}

struct SearchEngineModel: Codable, Equatable {
    let id: String
    let name: String
    let urlTemplate: String
    let shortcut: String?
    let iconUrl: String?
    let isDefault: Bool

    private enum CodingKeys: String, CodingKey {
        case id, name, urlTemplate, shortcut, iconUrl, isDefault
    }
}

struct SearchEngineViewModel: Codable, Equatable {
    let id: String
    let name: String
    let shortcut: String?
    let iconUrl: String?
    let isDefault: Bool

    private enum CodingKeys: String, CodingKey {
        case id, name, shortcut, iconUrl, isDefault
    }
}

struct SplitPane: Codable, Equatable {
    let tabId: TabId

    private enum CodingKeys: String, CodingKey {
        case tabId
    }
}

struct SplitViewConfig: Codable, Equatable {
    let panes: [SplitPane]
    let orientation: Orientation
    let ratios: [Double]
}

struct SplitConfig: Codable, Equatable {
    let tabIds: [TabId]
    let orientation: Orientation

    private enum CodingKeys: String, CodingKey {
        case tabIds, orientation
    }
}

enum ShortcutCategory: String, Codable, Equatable, Hashable {
    case navigation
    case tabs
    case spaces
    case window
    case edit
    case view
    case developer
    case custom
}

struct ShortcutBinding: Codable, Equatable {
    let action: String
    let label: String
    let category: ShortcutCategory
    let keyCombo: KeyCombo
    let isCustom: Bool
    let enabled: Bool

    private enum CodingKeys: String, CodingKey {
        case action, label, category, keyCombo, isCustom, enabled
    }
}

struct ShortcutConflict: Codable, Equatable {
    let keyCombo: KeyCombo
    let existingAction: String
    let newAction: String

    private enum CodingKeys: String, CodingKey {
        case keyCombo, existingAction, newAction
    }
}

enum Permission: String, Codable, Equatable {
    case tabs
    case activeTab
    case storage
    case notifications
    case contextMenus
    case webRequest
    case cookies
    case history
    case bookmarks
    case downloads
    case management
}

struct ContentScriptConfig: Codable, Equatable {
    let matches: [String]
    let js: [String]?
    let css: [String]?
    let runAt: ScriptRunAt?

    private enum CodingKeys: String, CodingKey {
        case matches, js, css, runAt
    }
}

enum ScriptRunAt: String, Codable, Equatable {
    case documentStart = "document_start"
    case documentEnd = "document_end"
    case documentIdle = "document_idle"
}

struct BackgroundConfig: Codable, Equatable {
    let serviceWorker: String

    private enum CodingKeys: String, CodingKey {
        case serviceWorker
    }
}

struct ExtensionManifest: Codable, Equatable {
    let manifestVersion: UInt32
    let name: String
    let version: String
    let permissions: [Permission]
    let contentScripts: [ContentScriptConfig]?
    let background: BackgroundConfig?

    private enum CodingKeys: String, CodingKey {
        case manifestVersion, name, version, permissions, contentScripts, background
    }
}

struct Extension: Codable, Equatable {
    let id: ExtensionId
    let name: String
    let version: String
    let manifest: ExtensionManifest
    let enabled: Bool
    let permissions: [Permission]
    let storageQuotaBytes: UInt64

    private enum CodingKeys: String, CodingKey {
        case id, name, version, manifest, enabled, permissions, storageQuotaBytes
    }
}

struct Note: Codable, Equatable {
    let id: NoteId
    let linkedTabId: TabId?
    let linkedUrl: MahoUrl?
    let content: String
    let createdAt: MahoDateTime
    let updatedAt: MahoDateTime

    private enum CodingKeys: String, CodingKey {
        case id, linkedTabId, linkedUrl, content, createdAt, updatedAt
    }
}

enum CanvasItem: Codable, Equatable {
    case webEmbed(url: MahoUrl, rect: Rect)
    case note(content: String, rect: Rect)
    case drawing(paths: [DrawPath], rect: Rect)
    case image(data: ImageData, rect: Rect)

    private enum CodingKeys: String, CodingKey {
        case kind
        case url, rect, content, paths, data
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)
        switch kind {
        case "web_embed":
            let url = try container.decode(MahoUrl.self, forKey: .url)
            let rect = try container.decode(Rect.self, forKey: .rect)
            self = .webEmbed(url: url, rect: rect)
        case "note":
            let content = try container.decode(String.self, forKey: .content)
            let rect = try container.decode(Rect.self, forKey: .rect)
            self = .note(content: content, rect: rect)
        case "drawing":
            let paths = try container.decode([DrawPath].self, forKey: .paths)
            let rect = try container.decode(Rect.self, forKey: .rect)
            self = .drawing(paths: paths, rect: rect)
        case "image":
            let data = try container.decode(ImageData.self, forKey: .data)
            let rect = try container.decode(Rect.self, forKey: .rect)
            self = .image(data: data, rect: rect)
        default:
            throw DecodingError.dataCorruptedError(
                forKey: .kind, in: container,
                debugDescription: "Unknown CanvasItem kind: \(kind)"
            )
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .webEmbed(let url, let rect):
            try container.encode("web_embed", forKey: .kind)
            try container.encode(url, forKey: .url)
            try container.encode(rect, forKey: .rect)
        case .note(let content, let rect):
            try container.encode("note", forKey: .kind)
            try container.encode(content, forKey: .content)
            try container.encode(rect, forKey: .rect)
        case .drawing(let paths, let rect):
            try container.encode("drawing", forKey: .kind)
            try container.encode(paths, forKey: .paths)
            try container.encode(rect, forKey: .rect)
        case .image(let data, let rect):
            try container.encode("image", forKey: .kind)
            try container.encode(data, forKey: .data)
            try container.encode(rect, forKey: .rect)
        }
    }
}

struct Easel: Codable, Equatable {
    let id: EaselId
    let name: String
    let canvasItems: [CanvasItem]
    let viewport: Viewport

    private enum CodingKeys: String, CodingKey {
        case id, name, canvasItems, viewport
    }
}
