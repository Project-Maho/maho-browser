import AppKit
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
    case stopLoading = "stop_loading"
    case notSecure = "not_secure"
    case aiOnboarding = "ai_onboarding"
    case aiSettings = "ai_settings"
    case documentText = "document_text"
    case compareTabs = "compare_tabs"
    case uploadFile = "upload_file"
    case tools = "tools"
    case connectApp = "connect_app"
    case responseFormat = "response_format"
    case stopGeneration = "stop_generation"
    case send = "send"
    case removeContext = "remove_context"
    case restore = "restore"
    case gradientToggle = "gradient_toggle"
    case themeLight = "theme_light"
    case themeDark = "theme_dark"
    case brightnessDown = "brightness_down"
    case splitView = "split_view"
    case extensions = "extensions"
    case autofillLocation = "autofill_location"
    case autofillCard = "autofill_card"
    case navBackForward = "nav_back_forward"
    case flexibleSpacer = "flexible_spacer"
    case customToolbarItem = "custom_toolbar_item"
    case navigation = "navigation"
    case calculator = "calculator"
    case unitConversion = "unit_conversion"
    case closedTab = "closed_tab"
    case historySync = "history_sync"
    case chevronLeft = "chevron_left"
    case chevronRight = "chevron_right"
    case moreOptions = "more_options"
    case favoriteFilled = "favorite_filled"
    case siteFigma = "site_figma"
    case siteSlack = "site_slack"
    case siteVercel = "site_vercel"
    case newSpace = "new_space"
    case newTab = "new_tab"
    case archiveFilled = "archive_filled"
    case audioPlaying = "audio_playing"
    case folderFilled = "folder_filled"
    case editContext = "edit_context"
    case assistantStyle = "assistant_style"
    case spacer = "spacer"
    case camera = "camera"
    case microphone = "microphone"
    case siteGithub = "site_github"

    public var image: NSImage? {
        guard let copy = lucideImage.copy() as? NSImage else { return nil }
        copy.isTemplate = true
        return copy
    }

    public func image(
        pointSize: CGFloat,
        weight: NSFont.Weight = .regular,
        accessibilityDescription: String? = nil
    ) -> NSImage? {
        guard let copy = lucideImage.copy() as? NSImage else { return nil }
        copy.isTemplate = true
        copy.size = NSSize(width: pointSize, height: pointSize)
        copy.accessibilityDescription = accessibilityDescription
        return copy
    }

    private var lucideImage: NSImage {
        switch self {
        case .navBack: return Lucide.chevronLeft
        case .navForward: return Lucide.chevronRight
        case .close: return Lucide.x
        case .reloadSync: return Lucide.rotateCw
        case .search: return Lucide.search
        case .globe: return Lucide.globe
        case .downloads: return Lucide.arrowDownToLine
        case .archive: return Lucide.archive
        case .spaces: return Lucide.copy
        case .boosts: return Lucide.zap
        case .media: return Lucide.images
        case .easels: return Lucide.pencilLine
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
        case .stopLoading: return Lucide.x
        case .notSecure: return Lucide.triangleAlert
        case .aiOnboarding: return Lucide.sparkles
        case .aiSettings: return Lucide.slidersHorizontal
        case .documentText: return Lucide.fileText
        case .compareTabs: return Lucide.copy
        case .uploadFile: return Lucide.filePlus
        case .tools: return Lucide.wrench
        case .connectApp: return Lucide.appWindowMac
        case .responseFormat: return Lucide.alignLeft
        case .stopGeneration: return Lucide.circleStop
        case .send: return Lucide.arrowRight
        case .removeContext: return Lucide.x
        case .restore: return Lucide.arrowUpLeft
        case .gradientToggle: return Lucide.contrast
        case .themeLight: return Lucide.sun
        case .themeDark: return Lucide.moon
        case .brightnessDown: return Lucide.minus
        case .splitView: return Lucide.panelLeftClose
        case .extensions: return Lucide.puzzle
        case .autofillLocation: return Lucide.mapPin
        case .autofillCard: return Lucide.creditCard
        case .navBackForward: return Lucide.chevronsLeftRight
        case .flexibleSpacer: return Lucide.moveHorizontal
        case .customToolbarItem: return Lucide.star
        case .navigation: return Lucide.compass
        case .calculator: return Lucide.functionSquare
        case .unitConversion: return Lucide.arrowLeftRight
        case .closedTab: return Lucide.squareX
        case .historySync: return Lucide.history
        case .chevronLeft: return Lucide.chevronLeft
        case .chevronRight: return Lucide.chevronRight
        case .moreOptions: return Lucide.ellipsis
        case .favoriteFilled: return Lucide.star
        case .siteFigma: return Lucide.layoutGrid
        case .siteSlack: return Lucide.messagesSquare
        case .siteVercel: return Lucide.triangle
        case .newSpace: return Lucide.layoutGrid
        case .newTab: return Lucide.copyPlus
        case .archiveFilled: return Lucide.archive
        case .audioPlaying: return Lucide.volume2
        case .folderFilled: return Lucide.folder
        case .editContext: return Lucide.squarePen
        case .assistantStyle: return Lucide.ruler
        case .spacer: return Lucide.minus
        case .camera: return Lucide.video
        case .microphone: return Lucide.mic
        case .siteGithub: return Lucide.code
        }
    }

    // MARK: - SF Symbol Migration Helper

    /// Looks up a Lucide icon by its former SF Symbol name.
    /// Used by call sites that pass SF Symbol names as dynamic strings.
    public static func imageForSFSymbol(
        _ sfSymbolName: String,
        pointSize: CGFloat? = nil,
        accessibilityDescription: String? = nil
    ) -> NSImage? {
        guard let baseImage = sfSymbolMapping[sfSymbolName] else { return nil }
        guard let copy = baseImage.copy() as? NSImage else { return nil }
        copy.isTemplate = true
        if let pointSize = pointSize {
            copy.size = NSSize(width: pointSize, height: pointSize)
        }
        copy.accessibilityDescription = accessibilityDescription
        return copy
    }

    // swiftlint:disable function_body_length
    private static let sfSymbolMapping: [String: NSImage] = [
        "app.badge.checkmark": Lucide.appWindow,
        "app.connected.to.app.below.fill": Lucide.appWindowMac,
        "archivebox": Lucide.archive,
        "archivebox.fill": Lucide.archive,
        "arrow.backward": Lucide.arrowLeft,
        "arrow.triangle.branch": Lucide.gitBranch,
        "arrow.clockwise": Lucide.rotateCw,
        "arrow.counterclockwise": Lucide.rotateCcw,
        "arrow.down.circle": Lucide.arrowDownCircle,
        "arrow.down.circle.fill": Lucide.arrowDownCircle,
        "arrow.down.to.line": Lucide.arrowDownToLine,
        "arrow.forward": Lucide.arrowRight,
        "arrow.left.and.right": Lucide.moveHorizontal,
        "arrow.left.arrow.right": Lucide.arrowLeftRight,
        "arrow.right": Lucide.arrowRight,
        "arrow.triangle.2.circlepath": Lucide.refreshCw,
        "arrow.up.forward": Lucide.arrowUpRight,
        "arrow.up.left": Lucide.arrowUpLeft,
        "arrow.up.right": Lucide.arrowUpRight,
        "arrow.uturn.backward": Lucide.undo2,
        "bell": Lucide.bell,
        "bolt": Lucide.zap,
        "book": Lucide.bookOpen,
        "bookmark": Lucide.bookmark,
        "briefcase": Lucide.briefcase,
        "bubble.left.and.bubble.right": Lucide.messagesSquare,
        "camera": Lucide.camera,
        "checkmark": Lucide.check,
        "checkmark.circle": Lucide.circleCheck,
        "checkmark.circle.fill": Lucide.circleCheck,
        "checkmark.shield": Lucide.shieldCheck,
        "chevron.backward": Lucide.chevronLeft,
        "chevron.down": Lucide.chevronDown,
        "chevron.forward": Lucide.chevronRight,
        "chevron.left": Lucide.chevronLeft,
        "chevron.left.chevron.right": Lucide.chevronsLeftRight,
        "chevron.left.forwardslash.chevron.right": Lucide.code,
        "chevron.right": Lucide.chevronRight,
        "chevron.up": Lucide.chevronUp,
        "circle": Lucide.circle,
        "circle.fill": Lucide.circle,
        "circle.lefthalf.filled": Lucide.contrast,
        "clock": Lucide.clock,
        "clock.arrow.circlepath": Lucide.history,
        "creditcard": Lucide.creditCard,
        "desktopcomputer": Lucide.monitor,
        "doc.badge.plus": Lucide.filePlus,
        "doc.on.doc": Lucide.copy,
        "doc.text": Lucide.fileText,
        "doc.text.magnifyingglass": Lucide.fileSearch,
        "ellipsis": Lucide.ellipsis,
        "ellipsis.circle": Lucide.circleEllipsis,
        "envelope": Lucide.mail,
        "exclamationmark.shield": Lucide.shieldAlert,
        "exclamationmark.triangle.fill": Lucide.triangleAlert,
        "eye.slash": Lucide.eyeOff,
        "eye.slash.fill": Lucide.eyeOff,
        "eyeglasses": Lucide.glasses,
        "folder": Lucide.folder,
        "folder.badge.minus": Lucide.folderMinus,
        "folder.fill": Lucide.folder,
        "function": Lucide.functionSquare,
        "gearshape": Lucide.settings,
        "gearshape.2": Lucide.settings2,
        "globe": Lucide.globe,
        "gamecontroller": Lucide.gamepad2,
        "hand.thumbsdown": Lucide.thumbsDown,
        "hand.thumbsup": Lucide.thumbsUp,
        "hourglass": Lucide.hourglass,
        "house": Lucide.home,
        "key": Lucide.keyRound,
        "keyboard": Lucide.keyboard,
        "leaf": Lucide.leaf,
        "lightbulb": Lucide.lightbulb,
        "line.3.horizontal": Lucide.menu,
        "line.3.horizontal.decrease": Lucide.listFilter,
        "line.3.horizontal.decrease.circle": Lucide.filter,
        "lines.measurement.horizontal": Lucide.ruler,
        "link": Lucide.link,
        "lock.fill": Lucide.lock,
        "lock": Lucide.lock,
        "lock.open": Lucide.lockOpen,
        "magnifyingglass": Lucide.search,
        "mappin.and.ellipse": Lucide.mapPin,
        "message": Lucide.messageSquare,
        "mic": Lucide.mic,
        "minus": Lucide.minus,
        "minus.circle": Lucide.circleMinus,
        "minus.magnifyingglass": Lucide.zoomOut,
        "moon.fill": Lucide.moon,
        "music.note": Lucide.music,
        "note.text": Lucide.notebookText,
        "paintbrush": Lucide.paintbrush,
        "paintbrush.pointed": Lucide.palette,
        "paperplane": Lucide.send,
        "pause": Lucide.pause,
        "pencil": Lucide.pencil,
        "pencil.and.scribble": Lucide.pencilLine,
        "pencil.line": Lucide.pencilLine,
        "pencil.tip.crop.circle": Lucide.pencilLine,
        "person.2": Lucide.users,
        "person.crop.circle": Lucide.circleUser,
        "photo": Lucide.image,
        "photo.on.rectangle": Lucide.image,
        "photo.on.rectangle.angled": Lucide.images,
        "pin": Lucide.pin,
        "pin.fill": Lucide.pin,
        "pin.slash": Lucide.pinOff,
        "pip.enter": Lucide.pictureInPicture2,
        "play": Lucide.play,
        "plus": Lucide.plus,
        "plus.magnifyingglass": Lucide.zoomIn,
        "plus.square.on.square": Lucide.copyPlus,
        "puzzlepiece.extension": Lucide.puzzle,
        "questionmark.shield": Lucide.shieldQuestion,
        "rectangle.on.rectangle": Lucide.copy,
        "rectangle.split.2x1": Lucide.panelLeftClose,
        "safari": Lucide.compass,
        "shield": Lucide.shield,
        "shield.lefthalf.filled": Lucide.shieldHalf,
        "sidebar.left": Lucide.panelLeft,
        "sidebar.right": Lucide.panelRight,
        "slider.horizontal.3": Lucide.slidersHorizontal,
        "sparkles": Lucide.sparkles,
        "sparkles.rectangle.stack": Lucide.sparkles,
        "speaker.slash": Lucide.volumeOff,
        "speaker.slash.fill": Lucide.volumeOff,
        "speaker.wave.2": Lucide.volume2,
        "speaker.wave.2.fill": Lucide.volume2,
        "square.and.arrow.up": Lucide.share,
        "square.and.pencil": Lucide.squarePen,
        "square.grid.2x2": Lucide.layoutGrid,
        "square.on.square": Lucide.copy,
        "star": Lucide.star,
        "star.fill": Lucide.star,
        "stop.fill": Lucide.circleStop,
        "storefront": Lucide.store,
        "sun.max.fill": Lucide.sun,
        "text.alignleft": Lucide.alignLeft,
        "trash": Lucide.trash2,
        "tray.and.arrow.down": Lucide.inbox,
        "triangle": Lucide.triangle,
        "video": Lucide.video,
        "wifi.slash": Lucide.wifiOff,
        "wrench": Lucide.wrench,
        "xmark": Lucide.x,
        "xmark.circle": Lucide.circleX,
        "xmark.circle.fill": Lucide.circleX,
        "xmark.shield": Lucide.shieldX,
        "xmark.square": Lucide.squareX,
    ]
    // swiftlint:enable function_body_length
}
