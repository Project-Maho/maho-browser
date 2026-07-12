import SwiftUI
import UIKit
import LucideIcons

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

    public var image: UIImage? {
        return lucideUIImage?.withRenderingMode(.alwaysTemplate)
    }

    public var imageVector: Image {
        return Image(uiImage: image ?? UIImage())
    }

    private var lucideUIImage: UIImage? {
        switch self {
        case .navBack: return Lucide.chevronLeft
        case .navForward: return Lucide.chevronRight
        case .close: return Lucide.x
        case .reloadSync: return Lucide.rotateCw
        case .search: return Lucide.search
        case .globe: return Lucide.globe
        case .downloads: return Lucide.circleArrowDown
        case .archive: return Lucide.archive
        case .spaces: return Lucide.copy
        case .boosts: return Lucide.sparkles
        case .media: return Lucide.image
        case .easels: return Lucide.palette
        case .notes: return Lucide.notebookText
        case .bookmarks: return Lucide.bookmark
        case .history: return Lucide.clock
        case .readingList: return Lucide.glasses
        case .settings: return Lucide.settings
        case .profiles: return Lucide.circleUser
        case .notifications: return Lucide.bell
        case .autofillKey: return Lucide.keyRound
        case .privacy: return Lucide.shield
        case .helpFeedback: return Lucide.mail
        case .clearSweep: return Lucide.rotateCcw
        case .delete: return Lucide.trash2
        case .share: return Lucide.share
        case .moreActions: return Lucide.circleEllipsis
        case .infoLock: return Lucide.lock
        case .audioIndicator: return Lucide.volume2
        case .sparklesAi: return Lucide.sparkles
        case .add: return Lucide.plus
        case .checkCircle: return Lucide.circleCheck
        case .expandMore: return Lucide.chevronDown
        case .filter: return Lucide.listFilter
        case .folder: return Lucide.folder
        case .launch: return Lucide.arrowUpRight
        case .searchAlt: return Lucide.search
        case .sidebarLeft: return Lucide.panelLeft
        case .audioMuted: return Lucide.volumeOff
        }
    }
}

// MARK: - Lucide SwiftUI helper

extension Image {
    init(lucide icon: UIImage) {
        self.init(uiImage: icon.withRenderingMode(.alwaysTemplate))
    }
}

// MARK: - SF Symbol → Lucide compatibility lookup
//
// Migration helper for legacy SF Symbol name strings used in
// `Label(_, systemImage: "<sf-name>")` and similar APIs. Maps the SF Symbol
// kebab/dot identifier to a Lucide UIImage via lucide-icons-swift.
//
// Use `Image(lucide: MahoIcon.imageForSFSymbol("<sf-name>"))` or pass to
// `Label { Text(...) } icon: { ... }`. Returns an empty UIImage as fallback
// for unmapped names (renders blank rather than crashing).
extension MahoIcon {
    public static func imageForSFSymbol(_ name: String) -> UIImage {
        let img = sfSymbolToLucide(name) ?? UIImage()
        return img.withRenderingMode(.alwaysTemplate)
    }

    private static func sfSymbolToLucide(_ name: String) -> UIImage? {
        switch name {
        case "chevron.left", "chevron.backward", "arrow.backward": return Lucide.chevronLeft
        case "chevron.right", "chevron.forward", "arrow.forward", "arrow.right": return Lucide.chevronRight
        case "chevron.up": return Lucide.chevronUp
        case "chevron.down": return Lucide.chevronDown
        case "xmark", "xmark.circle.fill": return Lucide.x
        case "xmark.circle": return Lucide.circleX
        case "plus": return Lucide.plus
        case "minus": return Lucide.minus
        case "ellipsis": return Lucide.ellipsis
        case "ellipsis.circle": return Lucide.circleEllipsis

        case "arrow.clockwise": return Lucide.rotateCw
        case "arrow.counterclockwise": return Lucide.rotateCcw
        case "arrow.triangle.2.circlepath": return Lucide.refreshCw
        case "arrow.up.right", "arrow.up.forward": return Lucide.arrowUpRight
        case "arrow.down.circle", "arrow.down.circle.fill": return Lucide.circleArrowDown
        case "arrow.down.to.line": return Lucide.arrowDownToLine
        case "square.and.arrow.up": return Lucide.share
        case "square.and.pencil": return Lucide.squarePen
        case "doc.on.doc", "rectangle.on.rectangle", "square.on.square": return Lucide.copy
        case "plus.square.on.square": return Lucide.copyPlus

        case "sidebar.left": return Lucide.panelLeft
        case "sidebar.right": return Lucide.panelRight
        case "rectangle.split.2x1": return Lucide.panelLeftClose
        case "archivebox", "archivebox.fill": return Lucide.archive
        case "folder", "folder.fill": return Lucide.folder
        case "folder.badge.minus": return Lucide.folderMinus
        case "doc.text": return Lucide.fileText
        case "doc.text.magnifyingglass": return Lucide.fileSearch
        case "note.text": return Lucide.notebookText
        case "bookmark": return Lucide.bookmark
        case "book": return Lucide.bookOpen
        case "clock": return Lucide.clock
        case "clock.arrow.circlepath": return Lucide.history
        case "tray.and.arrow.down": return Lucide.inbox
        case "photo", "photo.on.rectangle": return Lucide.image
        case "photo.on.rectangle.angled": return Lucide.images
        case "square.grid.2x2": return Lucide.layoutGrid
        case "paintbrush": return Lucide.paintbrush
        case "paintbrush.pointed": return Lucide.palette
        case "pencil": return Lucide.pencil
        case "pencil.and.scribble", "pencil.tip.crop.circle": return Lucide.pencilLine

        case "checkmark": return Lucide.check
        case "checkmark.circle", "checkmark.circle.fill": return Lucide.circleCheck
        case "exclamationmark.triangle.fill": return Lucide.triangleAlert
        case "circle", "circle.fill": return Lucide.circle
        case "minus.circle": return Lucide.circleMinus
        case "circle.lefthalf.filled": return Lucide.contrast
        case "hourglass": return Lucide.hourglass

        case "pin", "pin.fill": return Lucide.pin
        case "pin.slash": return Lucide.pinOff
        case "star", "star.fill": return Lucide.star

        case "speaker.slash", "speaker.slash.fill": return Lucide.volumeOff
        case "speaker.wave.2", "speaker.wave.2.fill": return Lucide.volume2
        case "play": return Lucide.play
        case "pause": return Lucide.pause
        case "stop.fill": return Lucide.circleStop
        case "video": return Lucide.video
        case "music.note": return Lucide.music
        case "mic": return Lucide.mic

        case "gearshape": return Lucide.settings
        case "gearshape.2": return Lucide.settings2
        case "shield": return Lucide.shield
        case "shield.lefthalf.filled": return Lucide.shieldHalf
        case "checkmark.shield": return Lucide.shieldCheck
        case "exclamationmark.shield": return Lucide.shieldAlert
        case "xmark.shield": return Lucide.shieldX
        case "questionmark.shield": return Lucide.shieldQuestionMark
        case "lock.fill": return Lucide.lock
        case "lock.open": return Lucide.lockOpen
        case "key": return Lucide.keyRound
        case "bell": return Lucide.bell
        case "envelope": return Lucide.mail
        case "globe": return Lucide.globe
        case "cpu": return Lucide.cpu
        case "robot", "bot": return Lucide.bot
        case "safari": return Lucide.compass
        case "magnifyingglass": return Lucide.search
        case "plus.magnifyingglass": return Lucide.zoomIn
        case "minus.magnifyingglass": return Lucide.zoomOut

        case "sparkles", "sparkles.rectangle.stack": return Lucide.sparkles
        case "trash": return Lucide.trash2
        case "person.crop.circle": return Lucide.circleUser
        case "puzzlepiece.extension": return Lucide.puzzle
        case "creditcard": return Lucide.creditCard
        case "moon.fill": return Lucide.moon
        case "sun.max.fill": return Lucide.sun
        case "bolt": return Lucide.zap
        case "wifi.slash": return Lucide.wifiOff
        case "eye.slash", "eye.slash.fill": return Lucide.eyeOff
        case "eyeglasses": return Lucide.glasses
        case "function": return Lucide.squareFunction
        case "app.badge.checkmark": return Lucide.appWindow
        case "link": return Lucide.link
        case "mappin.and.ellipse": return Lucide.mapPin
        case "hand.thumbsup": return Lucide.thumbsUp
        case "hand.thumbsdown": return Lucide.thumbsDown
        case "doc.badge.plus": return Lucide.filePlus
        case "bubble.left.and.bubble.right": return Lucide.messagesSquare
        case "wrench": return Lucide.wrench
        case "triangle": return Lucide.triangle

        default: return nil
        }
    }
}
