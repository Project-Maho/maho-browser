import Foundation

enum QuickAction: Codable {
    case newTab
    case newSpace
    case closeTab
    case closeOtherTabs
    case closeAllTabs
    case duplicateTab
    case pinTab
    case unpinTab
    case muteTab
    case unmuteTab
    case muteAllTabs
    case freezeTab
    case unfreezeTab
    case reloadTab
    case hardReload
    case copyUrl
    case clearHistory
    case clearCookies
    case openSettings
    case openDownloads
    case openBookmarks
    case openHistory
    case toggleBoost
    case toggleSidebar
    case toggleContentBlocker
    case newFolder
    case archiveTab
    case restoreLastClosed
    case toggleFullScreen
    case zoomIn
    case zoomOut
    case resetZoom
    case findInPage
    case printPage
    case viewSource
    case toggleDevTools
    case nextSpace
    case prevSpace
    case custom(actionId: String)

    private enum CodingKeys: String, CodingKey {
        case kind
        case actionId = "action_id"
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .custom(let actionId):
            try container.encode("custom", forKey: .kind)
            try container.encode(actionId, forKey: .actionId)
        default:
            try container.encode(kindString, forKey: .kind)
        }
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        let kind = try container.decode(String.self, forKey: .kind)
        switch kind {
        case "new_tab": self = .newTab
        case "new_space": self = .newSpace
        case "close_tab": self = .closeTab
        case "close_other_tabs": self = .closeOtherTabs
        case "close_all_tabs": self = .closeAllTabs
        case "duplicate_tab": self = .duplicateTab
        case "pin_tab": self = .pinTab
        case "unpin_tab": self = .unpinTab
        case "mute_tab": self = .muteTab
        case "unmute_tab": self = .unmuteTab
        case "mute_all_tabs": self = .muteAllTabs
        case "freeze_tab": self = .freezeTab
        case "unfreeze_tab": self = .unfreezeTab
        case "reload_tab": self = .reloadTab
        case "hard_reload": self = .hardReload
        case "copy_url": self = .copyUrl
        case "clear_history": self = .clearHistory
        case "clear_cookies": self = .clearCookies
        case "open_settings": self = .openSettings
        case "open_downloads": self = .openDownloads
        case "open_bookmarks": self = .openBookmarks
        case "open_history": self = .openHistory
        case "toggle_boost": self = .toggleBoost
        case "toggle_sidebar": self = .toggleSidebar
        case "toggle_content_blocker": self = .toggleContentBlocker
        case "new_folder": self = .newFolder
        case "archive_tab": self = .archiveTab
        case "restore_last_closed": self = .restoreLastClosed
        case "toggle_full_screen": self = .toggleFullScreen
        case "zoom_in": self = .zoomIn
        case "zoom_out": self = .zoomOut
        case "reset_zoom": self = .resetZoom
        case "find_in_page": self = .findInPage
        case "print_page": self = .printPage
        case "view_source": self = .viewSource
        case "toggle_dev_tools": self = .toggleDevTools
        case "next_space": self = .nextSpace
        case "prev_space": self = .prevSpace
        case "custom":
            let actionId = try container.decode(String.self, forKey: .actionId)
            self = .custom(actionId: actionId)
        default:
            throw DecodingError.dataCorruptedError(forKey: .kind, in: container, debugDescription: "Unknown QuickAction kind: \(kind)")
        }
    }

    private var kindString: String {
        switch self {
        case .newTab: return "new_tab"
        case .newSpace: return "new_space"
        case .closeTab: return "close_tab"
        case .closeOtherTabs: return "close_other_tabs"
        case .closeAllTabs: return "close_all_tabs"
        case .duplicateTab: return "duplicate_tab"
        case .pinTab: return "pin_tab"
        case .unpinTab: return "unpin_tab"
        case .muteTab: return "mute_tab"
        case .unmuteTab: return "unmute_tab"
        case .muteAllTabs: return "mute_all_tabs"
        case .freezeTab: return "freeze_tab"
        case .unfreezeTab: return "unfreeze_tab"
        case .reloadTab: return "reload_tab"
        case .hardReload: return "hard_reload"
        case .copyUrl: return "copy_url"
        case .clearHistory: return "clear_history"
        case .clearCookies: return "clear_cookies"
        case .openSettings: return "open_settings"
        case .openDownloads: return "open_downloads"
        case .openBookmarks: return "open_bookmarks"
        case .openHistory: return "open_history"
        case .toggleBoost: return "toggle_boost"
        case .toggleSidebar: return "toggle_sidebar"
        case .toggleContentBlocker: return "toggle_content_blocker"
        case .newFolder: return "new_folder"
        case .archiveTab: return "archive_tab"
        case .restoreLastClosed: return "restore_last_closed"
        case .toggleFullScreen: return "toggle_full_screen"
        case .zoomIn: return "zoom_in"
        case .zoomOut: return "zoom_out"
        case .resetZoom: return "reset_zoom"
        case .findInPage: return "find_in_page"
        case .printPage: return "print_page"
        case .viewSource: return "view_source"
        case .toggleDevTools: return "toggle_dev_tools"
        case .nextSpace: return "next_space"
        case .prevSpace: return "prev_space"
        case .custom: return "custom"
        }
    }
}

struct DynamicCodingKey: CodingKey {
    var stringValue: String
    var intValue: Int?

    init?(stringValue: String) { self.stringValue = stringValue }
    init?(intValue: Int) { self.intValue = intValue; self.stringValue = "\(intValue)" }
}
