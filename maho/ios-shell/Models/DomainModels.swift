import Foundation

struct SpaceColor: Codable {
    let hue: Double
    let saturation: Double
    let brightness: Double
}

struct Tab: Codable, Identifiable {
    let id: TabId
    let parentId: TabId?
    let spaceId: SpaceId
    let url: Url
    let title: String
    let favicon: ImageData?
    let state: TabLifecycleState
    let isPinned: Bool
    let isFavorite: Bool
    let isMuted: Bool
    let zoomLevel: Double
    let createdAt: String
    let lastActiveAt: String
    let scrollPosition: ScrollPosition
    let pinnedUrl: Url?
}

struct Space: Codable, Identifiable {
    let id: SpaceId
    var profileId: ProfileId = ""
    let name: String
    let color: SpaceColor
    let icon: String?
    let tabOrder: [TabId]
    let folders: [Folder]
    let atcRules: [ATCRule]
    let isActive: Bool
    let createdAt: String
}

struct ATCRule: Codable {
    let id: String
    let condition: ATCCondition
    let action: ATCAction
    let enabled: Bool
}

enum ATCAction: Codable {
    case close
    case archive
    case route(spaceId: SpaceId)

    init(from decoder: Decoder) throws {
        if let container = try? decoder.singleValueContainer(),
           let str = try? container.decode(String.self) {
            switch str {
            case "close": self = .close
            case "archive": self = .archive
            default:
                throw DecodingError.dataCorrupted(
                    DecodingError.Context(codingPath: decoder.codingPath, debugDescription: "Unknown ATCAction string: \(str)"))
            }
        } else {
            let container = try decoder.container(keyedBy: DynamicCodingKey.self)
            if let routeKey = DynamicCodingKey(stringValue: "route"),
               container.contains(routeKey) {
                let nested = try container.nestedContainer(keyedBy: DynamicCodingKey.self, forKey: routeKey)
                let spaceId = try nested.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!)
                self = .route(spaceId: spaceId)
            } else {
                throw DecodingError.dataCorrupted(
                    DecodingError.Context(codingPath: decoder.codingPath, debugDescription: "Unknown ATCAction object key"))
            }
        }
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
            var container = encoder.container(keyedBy: DynamicCodingKey.self)
            var nested = container.nestedContainer(keyedBy: DynamicCodingKey.self, forKey: DynamicCodingKey(stringValue: "route")!)
            try nested.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
        }
    }
}

enum ATCCondition: Codable {
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
        case "inactive_duration": self = .inactiveDuration(hours: try container.decode(Double.self, forKey: .hours))
        case "tab_count_exceeded": self = .tabCountExceeded(maxTabs: try container.decode(UInt32.self, forKey: .maxTabs))
        case "url_pattern": self = .urlPattern(pattern: try container.decode(String.self, forKey: .pattern))
        case "url_contains": self = .urlContains(text: try container.decode(String.self, forKey: .text))
        case "url_equals": self = .urlEquals(url: try container.decode(String.self, forKey: .url))
        default:
            throw DecodingError.dataCorruptedError(forKey: .kind, in: container, debugDescription: "Unknown ATCCondition: \(kind)")
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

struct Profile: Codable, Identifiable {
    let id: ProfileId
    let name: String
    let spaces: [Space]
    let boosts: [Boost]
    let notes: [Note]
    let searchEngines: [SettingsSearchEngine]
    let settings: Settings
}

struct ProfileConfig: Codable {
    let id: ProfileId
    let name: String
    let avatarColor: String
    let defaultSearchEngine: SettingsSearchEngine
    let downloadPath: String
    let archiveTimeoutHours: Double?
    let dataStoreId: String?
}

struct Folder: Codable, Identifiable {
    let id: FolderId
    let name: String
    let tabIds: [TabId]
    let isExpanded: Bool
    var isPinned: Bool = false
    var parentFolderId: FolderId? = nil
}

enum TextCase: String, Codable {
    case none
    case upper
    case lower
    case capitalize
}

struct ColorBoost: Codable {
    let enableColorBoost: Bool
    let dotAngleDeg: Double
    let secondaryDotAngleDegDelta: Double
    let brightness: Double
    let saturation: Double
    let contrast: Double
    let autoTheme: Bool
    let smartInvert: Bool
}

struct TypographyBoost: Codable {
    let fontFamily: String?
    let textCaseOverride: TextCase
    let sizeOverride: Float?
}

struct Boost: Codable {
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

struct ColorBoostUpdate: Codable {
    var enableColorBoost: Bool? = nil
    var dotAngleDeg: Double? = nil
    var secondaryDotAngleDegDelta: Double? = nil
    var brightness: Double? = nil
    var saturation: Double? = nil
    var contrast: Double? = nil
    var autoTheme: Bool? = nil
    var smartInvert: Bool? = nil
}

struct TypographyBoostUpdate: Codable {
    var fontFamily: Patchable<String> = .absent
    var textCaseOverride: TextCase? = nil
    var sizeOverride: Patchable<Float> = .absent

    private enum CodingKeys: String, CodingKey {
        case fontFamily, textCaseOverride, sizeOverride
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        fontFamily = try container.decodePatchable(Patchable<String>.self, forKey: .fontFamily)
        textCaseOverride = try container.decodeIfPresent(TextCase.self, forKey: .textCaseOverride)
        sizeOverride = try container.decodePatchable(Patchable<Float>.self, forKey: .sizeOverride)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encodePatchable(fontFamily, forKey: .fontFamily)
        try container.encodeIfPresent(textCaseOverride, forKey: .textCaseOverride)
        try container.encodePatchable(sizeOverride, forKey: .sizeOverride)
    }
}

struct BoostUpdate: Codable {
    var name: String? = nil
    var color: ColorBoostUpdate? = nil
    var typography: TypographyBoostUpdate? = nil
    var zapSelectors: [String]? = nil
    var customCss: String? = nil
}

struct Note: Codable, Identifiable {
    let id: NoteId
    let linkedTabId: TabId?
    let linkedUrl: Url?
    let content: String
    let createdAt: String
    let updatedAt: String
}

struct AccountInfo: Codable {
    let email: String
    let displayName: String?
    let avatarUrl: String?
    let syncEnabled: Bool
}

enum SyncState: Codable {
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
            throw DecodingError.dataCorruptedError(forKey: .kind, in: container, debugDescription: "Unknown SyncState: \(kind)")
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

struct TrafficRule: Codable {
    let id: String
    let urlPattern: String
    let matchType: MatchType
    let targetSpaceId: SpaceId
    let enabled: Bool
}

enum MatchType: String, Codable {
    case contains
    case equals
    case regex
}

enum DefaultLinkBehavior: Codable {
    case currentSpace
    case mostRecentSpace
    case specificSpace(spaceId: SpaceId)

    init(from decoder: Decoder) throws {
        if let container = try? decoder.singleValueContainer(),
           let str = try? container.decode(String.self) {
            switch str {
            case "current_space": self = .currentSpace
            case "most_recent_space": self = .mostRecentSpace
            default:
                throw DecodingError.dataCorrupted(
                    DecodingError.Context(codingPath: decoder.codingPath, debugDescription: "Unknown DefaultLinkBehavior string: \(str)"))
            }
        } else {
            let container = try decoder.container(keyedBy: DynamicCodingKey.self)
            if let key = DynamicCodingKey(stringValue: "specific_space"),
               container.contains(key) {
                let nested = try container.nestedContainer(keyedBy: DynamicCodingKey.self, forKey: key)
                let spaceId = try nested.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!)
                self = .specificSpace(spaceId: spaceId)
            } else {
                throw DecodingError.dataCorrupted(
                    DecodingError.Context(codingPath: decoder.codingPath, debugDescription: "Unknown DefaultLinkBehavior object key"))
            }
        }
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
            var container = encoder.container(keyedBy: DynamicCodingKey.self)
            var nested = container.nestedContainer(keyedBy: DynamicCodingKey.self, forKey: DynamicCodingKey(stringValue: "specific_space")!)
            try nested.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
        }
    }
}

struct SavedPassword: Codable {
    let id: String
    let domain: String
    let username: String
    let createdAt: String
    let lastUsed: String?
    let password: String?
}

struct AutofillAddress: Codable {
    let id: String
    let name: String
    let street: String
    let city: String
    let state: String
    let zip: String
    let country: String
    let phone: String?
    let email: String?
}

struct AutofillPayment: Codable {
    let id: String
    let cardName: String
    let lastFour: String
    let expiry: String
}

struct SearchEngineModel: Codable {
    let id: String
    let name: String
    let urlTemplate: String
    let shortcut: String?
    let iconUrl: String?
    let isDefault: Bool
}

struct FindSession: Codable {
    let query: String
    let matchCount: UInt32
    let activeIndex: UInt32
    let isVisible: Bool
}

struct ReadingListItem: Codable, Identifiable {
    let id: String
    let url: String
    let title: String
    let isRead: Bool
    let addedAt: String
}

struct ConnectedDevice: Codable, Identifiable {
    let id: String
    let name: String
    let deviceType: String
    let lastSeen: String?
    let isOnline: Bool?

    var appearsOffline: Bool {
        isOnline == false
    }
}
