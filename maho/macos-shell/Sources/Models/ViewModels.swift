import Foundation

struct TabViewModel: Codable, Equatable {
    let id: TabId
    let spaceId: SpaceId
    let title: String
    let customTitle: String?
    let url: String
    let favicon: ImageData?
    let isLoading: Bool
    let isPinned: Bool
    let isFavorite: Bool
    let favoriteOrder: Int?
    let isMuted: Bool
    let isPlayingAudio: Bool
    let lifecycleState: String
    let children: [TabId]
    let createdAt: MahoDateTime
    let lastActiveAt: MahoDateTime

    private enum CodingKeys: String, CodingKey {
        case id, spaceId, title, customTitle, url, favicon, isLoading, isPinned
        case isFavorite, favoriteOrder, isMuted, isPlayingAudio, lifecycleState
        case children, createdAt, lastActiveAt
    }
}

struct SpaceViewModel: Codable, Equatable {
    let id: SpaceId
    let name: String
    let color: SpaceColor
    let theme: SpaceTheme?
    let tabCount: Int
    let isActive: Bool
    let icon: String?
    let profileId: ProfileId?
    let profileName: String?
    let orderIndex: Int?

    private enum CodingKeys: String, CodingKey {
        case id, name, color, theme, tabCount, isActive, icon, profileId, profileName, orderIndex
    }
}

extension Array where Element == SpaceViewModel {
    func sortedByCanonicalOrder() -> [SpaceViewModel] {
        sorted { lhs, rhs in
            switch (lhs.orderIndex, rhs.orderIndex) {
            case let (l?, r?) where l != r:
                return l < r
            case (_?, nil):
                return true
            case (nil, _?):
                return false
            default:
                return lhs.name.localizedCaseInsensitiveCompare(rhs.name) == .orderedAscending
            }
        }
    }
}

struct FolderViewModel: Codable, Equatable {
    let id: FolderId
    let name: String
    let tabCount: Int
    let isExpanded: Bool
    let tabIds: [TabId]
    let isPinned: Bool
    let parentFolderId: FolderId?

    private enum CodingKeys: String, CodingKey {
        case id, name, tabCount, isExpanded, tabIds, isPinned, parentFolderId
    }
}

struct TabTreeViewModel: Codable, Equatable {
    let rootTabs: [TabViewModel]

    private enum CodingKeys: String, CodingKey {
        case rootTabs
    }
}

struct ArchivedTabViewModel: Codable, Equatable {
    let id: TabId
    let title: String
    let url: String
    let favicon: ImageData?
    let archivedAt: String

    private enum CodingKeys: String, CodingKey {
        case id, title, url, favicon, archivedAt
    }
}

struct DownloadViewModel: Codable, Equatable {
    let id: String
    let filename: String
    let url: String
    let totalBytes: UInt64
    let receivedBytes: UInt64
    let state: DownloadState
    let filePath: String?
    let mimeType: String?
    let error: String?
    let startedAt: String
    let completedAt: String?
    let originalFilename: String?

    private enum CodingKeys: String, CodingKey {
        case id, filename, url, totalBytes, receivedBytes, state
        case filePath, mimeType, error, startedAt, completedAt, originalFilename
    }
}

enum DownloadState: String, Codable, Equatable {
    case downloading
    case paused
    case completed
    case failed
    case cancelled
}

enum SuggestionType: String, Codable, Equatable {
    case tab
    case bookmark
    case history
    case action
    case navigation
    case calculator
    case unitConversion = "unitconversion"
    case archivedTab = "archivedtab"
    case closedTab = "closedtab"
}

struct SuggestionViewModel: Codable, Equatable {
    let kind: SuggestionType
    let key: String
    let title: String
    let subtitle: String?
    let executionPayload: String?
    let icon: ImageData?
    let relevanceScore: Double
    let matchRanges: [[Int]]?

    private enum CodingKeys: String, CodingKey {
        case kind, key, title, subtitle, executionPayload, icon, relevanceScore, matchRanges
    }
}

struct SearchContext: Codable, Equatable {
    let tabs: [TabViewModel]
    let spaces: [SpaceViewModel]
    let archivedTabs: [TabViewModel]
    let bookmarks: [[String]]
    let closedTabs: [TabViewModel]
    let extensions: [[String]]

    private enum CodingKeys: String, CodingKey {
        case tabs, spaces, archivedTabs, bookmarks, closedTabs, extensions
    }
}

struct ActionViewModel: Codable, Equatable {
    let id: String
    let label: String
    let icon: ImageData?
    let shortcut: String?
}

struct TabStripViewModel: Codable, Equatable {
    let id: TabId
    let title: String
    let favicon: ImageData?
    let isActive: Bool
    let isLoading: Bool
    let isPlayingAudio: Bool

    private enum CodingKeys: String, CodingKey {
        case id, title, favicon, isActive, isLoading, isPlayingAudio
    }
}

struct TabStateUpdate: Codable, Equatable {
    let title: String?
    let customTitle: String?
    let url: String?
    let favicon: ImageData??
    let isLoading: Bool?
    let isPinned: Bool?
    let isMuted: Bool?
    let isPlayingAudio: Bool?
    let isFavorite: Bool?
    let favoriteOrder: Int??
    let lifecycleState: String?

    private enum CodingKeys: String, CodingKey {
        case title, customTitle, url, favicon, isLoading, isPinned
        case isMuted, isPlayingAudio, isFavorite, favoriteOrder, lifecycleState
    }
}

struct NotificationViewModel: Codable, Equatable {
    let id: String
    let title: String
    let message: String
    let icon: ImageData?
    let actions: [NotificationAction]
}

struct NotificationAction: Codable, Equatable {
    let id: String
    let label: String
}

struct PermissionRequest: Codable, Equatable {
    let id: String
    let origin: String
    let permission: String
    let message: String
}

struct FindBarState: Codable, Equatable {
    let query: String
    let matchCount: UInt32
    let activeIndex: UInt32
    let isVisible: Bool

    private enum CodingKeys: String, CodingKey {
        case query, matchCount, activeIndex, isVisible
    }
}

struct BoostViewModel: Codable, Equatable {
    let id: BoostId
    let domain: String
    let customCss: String?
    let enabled: Bool

    private enum CodingKeys: String, CodingKey {
        case id, domain, customCss, enabled
    }
}

struct SettingsViewModel: Codable, Equatable {
    let sections: [SettingsSection]
}

struct SettingsSection: Codable, Equatable {
    let title: String
    let items: [SettingsItem]
}

struct SettingsItem: Codable, Equatable {
    let key: String
    let label: String
    let itemType: SettingsItemType
    let value: JSONValue

    private enum CodingKeys: String, CodingKey {
        case key, label
        case itemType = "type"
        case value
    }
}

enum SettingsItemType: String, Codable, Equatable {
    case toggle
    case select
    case text
    case number
    case color
    case slider
    case keyCapture = "key_capture"
    case urlPattern = "url_pattern"
    case dragList = "drag_list"
    case pathSelector = "path_selector"
}

struct NoteViewModel: Codable, Equatable {
    let id: String
    let content: String
    let linkedUrl: String?

    private enum CodingKeys: String, CodingKey {
        case id, content, linkedUrl
    }
}

struct EaselViewModel: Codable, Equatable {
    let id: String
    let name: String
    let itemCount: Int

    private enum CodingKeys: String, CodingKey {
        case id, name, itemCount
    }
}

struct ExtensionViewModel: Codable, Equatable {
    let id: String
    let name: String
    let version: String
    let enabled: Bool
    let icon: ImageData?
}
