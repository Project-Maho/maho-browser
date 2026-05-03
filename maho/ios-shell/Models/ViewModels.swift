import Foundation

struct TabViewModel: Codable, Identifiable {
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
    let createdAt: String
    let lastActiveAt: String
}

struct SpaceViewModel: Codable, Identifiable {
    let id: SpaceId
    let name: String
    let color: SpaceColor
    let tabCount: Int
    let isActive: Bool
    let icon: String?
    let profileId: ProfileId?
    let profileName: String?
    let orderIndex: Int?
}

struct FolderViewModel: Codable, Identifiable {
    let id: FolderId
    let name: String
    let tabCount: Int
    let isExpanded: Bool
    let tabIds: [TabId]
    let isPinned: Bool
    let parentFolderId: FolderId?
}

struct TabStateUpdate: Codable {
    let title: String?
    let customTitle: String?
    let url: String?
    let favicon: ImageData??
    let isLoading: Bool?
    let isPinned: Bool?
    let isMuted: Bool?
    let isPlayingAudio: Bool?
    let isFavorite: Bool?
    let favoriteOrder: Int?
    let lifecycleState: String?
}

enum SuggestionType: String, Codable {
    case tab
    case bookmark
    case history
    case action
    case navigation
    case calculator
    case unitConversion
    case archivedTab
    case closedTab
}

struct SuggestionViewModel: Codable, Identifiable {
    var id: String { key }
    let kind: SuggestionType
    let key: String
    let title: String
    let subtitle: String?
    let executionPayload: String?
    let icon: ImageData?
    let relevanceScore: Double
    let matchRanges: [[Int]]?
}

struct NotificationViewModel: Codable, Identifiable {
    let id: String
    let title: String
    let message: String
    let icon: ImageData?
    let actions: [NotificationAction]
}

struct NotificationAction: Codable {
    let id: String
    let label: String
}

struct PermissionRequest: Codable {
    let id: String
    let origin: String
    let permission: String
    let message: String
}

struct DownloadViewModel: Codable, Identifiable {
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
}

enum DownloadState: String, Codable {
    case downloading
    case paused
    case completed
    case failed
    case cancelled
}

struct NoteViewModel: Codable, Identifiable {
    let id: String
    let content: String
    let linkedUrl: String?
}

struct SearchEngineViewModel: Codable {
    let id: String
    let name: String
    let shortcut: String?
    let iconUrl: String?
    let isDefault: Bool
}

struct SettingsViewModel: Codable {
    let sections: [SettingsSection]
}

struct SettingsSection: Codable {
    let title: String
    let items: [SettingsItem]
}

struct SettingsItem: Codable {
    let key: String
    let label: String
    let type: SettingsItemType
    let value: AnyCodable

    private enum CodingKeys: String, CodingKey {
        case key
        case label
        case type
        case value
    }
}

enum SettingsItemType: String, Codable {
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

struct BoostViewModel: Codable {
    let id: BoostId
    let urlPattern: String
    let customCss: String?
    let customJs: String?
    let enabled: Bool
}

struct FindBarState: Codable {
    let query: String
    let matchCount: UInt32
    let activeIndex: UInt32
    let isVisible: Bool
}
