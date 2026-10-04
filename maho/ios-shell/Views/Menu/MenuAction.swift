import Foundation
import SwiftUI
import LucideIcons

enum MenuAction: String, CaseIterable, Identifiable {
    case newTab
    case newPrivateTab
    case bookmarks
    case history
    case downloads
    case readingList
    case notes
    case sharePage
    case findInPage
    case reload
    case desktopSite
    case readerMode
    case settings
    case signIn
    case profileSwitcher

    var id: String { rawValue }

    var title: String {
        switch self {
        case .newTab: return "New Tab"
        case .newPrivateTab: return "New Private Tab"
        case .bookmarks: return "Bookmarks"
        case .history: return "History"
        case .downloads: return "Downloads"
        case .readingList: return "Reading List"
        case .notes: return "Notes"
        case .sharePage: return "Share Page"
        case .findInPage: return "Find in Page"
        case .reload: return "Reload Page"
        case .desktopSite: return "Desktop Site"
        case .readerMode: return "Reader Mode"
        case .settings: return "Settings"
        case .signIn: return "Sign In / Sync"
        case .profileSwitcher: return "Profiles"
        }
    }

    var lucideIcon: UIImage {
        switch self {
        case .newTab: return Lucide.plus
        case .newPrivateTab: return Lucide.eyeOff
        case .bookmarks: return Lucide.bookmark
        case .history: return Lucide.clock
        case .downloads: return Lucide.circleArrowDown
        case .readingList: return Lucide.glasses
        case .notes: return Lucide.notebookText
        case .sharePage: return Lucide.share
        case .findInPage: return Lucide.search
        case .reload: return Lucide.rotateCw
        case .desktopSite: return Lucide.monitor
        case .readerMode: return Lucide.bookOpen
        case .settings: return Lucide.settings
        case .signIn: return Lucide.user
        case .profileSwitcher: return Lucide.circleUser
        }
    }
}
