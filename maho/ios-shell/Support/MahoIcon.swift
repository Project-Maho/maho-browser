import SwiftUI
import UIKit

public enum MahoIcon: String, CaseIterable {
    case navBack = "nav_back"
    case navForward = "nav_forward"
    case close = "close"
    case reloadSync = "reload_sync"
    case search = "search"
    case globe = "globe"
    case downloads = "downloads"
    case archive = "archive"
    case spaces = "spaces"
    case boosts = "boosts"
    case media = "media"
    case easels = "easels"
    case notes = "notes"
    case bookmarks = "bookmarks"
    case history = "history"
    case readingList = "reading_list"
    case settings = "settings"
    case profiles = "profiles"
    case notifications = "notifications"
    case autofillKey = "autofill_key"
    case privacy = "privacy"
    case helpFeedback = "help_feedback"
    case clearSweep = "clear_sweep"
    case delete = "delete"
    case share = "share"
    case moreActions = "more_actions"
    case infoLock = "info_lock"
    case audioIndicator = "audio_indicator"
    case sparklesAi = "sparkles_ai"
    case add = "add"
    case checkCircle = "check_circle"
    case expandMore = "expand_more"
    case filter = "filter"
    case folder = "folder"
    case launch = "launch"
    case searchAlt = "search_alt"
    case sidebarLeft = "sidebar_left"
    case audioMuted = "audio_muted"

    public var symbolName: String {
        switch self {
        case .navBack: return "chevron.backward"
        case .navForward: return "chevron.forward"
        case .close: return "xmark"
        case .reloadSync: return "arrow.clockwise"
        case .search: return "magnifyingglass"
        case .globe: return "globe"
        case .downloads: return "arrow.down.circle"
        case .archive: return "archivebox"
        case .spaces: return "square.on.square"
        case .boosts: return "sparkles"
        case .media: return "photo.on.rectangle"
        case .easels: return "paintbrush.pointed"
        case .notes: return "note.text"
        case .bookmarks: return "bookmark"
        case .history: return "clock"
        case .readingList: return "eyeglasses"
        case .settings: return "gearshape"
        case .profiles: return "person.crop.circle"
        case .notifications: return "bell"
        case .autofillKey: return "key"
        case .privacy: return "shield"
        case .helpFeedback: return "envelope"
        case .clearSweep: return "arrow.counterclockwise"
        case .delete: return "trash"
        case .share: return "square.and.arrow.up"
        case .moreActions: return "ellipsis.circle"
        case .infoLock: return "lock.fill"
        case .audioIndicator: return "speaker.wave.2"
        case .sparklesAi: return "sparkles"
        case .add: return "plus"
        case .checkCircle: return "checkmark.circle.fill"
        case .expandMore: return "chevron.down"
        case .filter: return "line.3.horizontal.decrease"
        case .folder: return "folder"
        case .launch: return "arrow.up.forward"
        case .searchAlt: return "magnifyingglass"
        case .sidebarLeft: return "sidebar.left"
        case .audioMuted: return "speaker.slash.fill"
        }
    }

    public var image: UIImage? {
        return UIImage(systemName: symbolName)
    }

    public var imageVector: Image {
        return Image(systemName: symbolName)
    }
}
