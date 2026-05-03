import AppKit

enum CommandBarSparkleArt {
    private static let outerPoints: [CGPoint] = [
        CGPoint(x: 12, y: 2.6),
        CGPoint(x: 14.3, y: 8.2),
        CGPoint(x: 20, y: 10.5),
        CGPoint(x: 14.3, y: 12.8),
        CGPoint(x: 12, y: 18.4),
        CGPoint(x: 9.7, y: 12.8),
        CGPoint(x: 4, y: 10.5),
        CGPoint(x: 9.7, y: 8.2)
    ]

    private static let innerPoints: [CGPoint] = [
        CGPoint(x: 12, y: 6.5),
        CGPoint(x: 13.5, y: 10.2),
        CGPoint(x: 17.2, y: 11.7),
        CGPoint(x: 13.5, y: 13.2),
        CGPoint(x: 12, y: 16.9),
        CGPoint(x: 10.5, y: 13.2),
        CGPoint(x: 6.8, y: 11.7),
        CGPoint(x: 10.5, y: 10.2)
    ]

    private static let outerGradientColors = [
        NSColor(red: 0.31, green: 0.73, blue: 1.00, alpha: 1),
        NSColor(red: 0.48, green: 0.39, blue: 1.00, alpha: 1)
    ]

    private static let innerGradientColors = [
        NSColor(red: 1.00, green: 0.45, blue: 0.31, alpha: 1),
        NSColor(red: 1.00, green: 0.84, blue: 0.32, alpha: 1)
    ]

    static func draw(in rect: NSRect, shadow: NSShadow? = nil) {
        let outerPath = sparklePath(in: rect, points: outerPoints)
        let innerPath = sparklePath(in: rect, points: innerPoints)

        if let shadow {
            NSGraphicsContext.saveGraphicsState()
            shadow.set()
            NSGradient(colors: outerGradientColors)?.draw(in: outerPath, angle: -35)
            NSGraphicsContext.restoreGraphicsState()
        } else {
            NSGradient(colors: outerGradientColors)?.draw(in: outerPath, angle: -35)
        }

        NSGradient(colors: innerGradientColors)?.draw(in: innerPath, angle: 35)
    }

    static func image(size: NSSize) -> NSImage? {
        let image = NSImage(size: size)
        image.lockFocus()
        draw(in: NSRect(origin: .zero, size: size))
        image.unlockFocus()
        return image
    }

    private static func sparklePath(in rect: NSRect, points: [CGPoint]) -> NSBezierPath {
        let path = NSBezierPath()
        for (index, point) in points.enumerated() {
            let mappedPoint = CGPoint(
                x: rect.minX + rect.width * (point.x / 24),
                y: rect.minY + rect.height * (point.y / 24)
            )
            if index == 0 {
                path.move(to: mappedPoint)
            } else {
                path.line(to: mappedPoint)
            }
        }
        path.close()
        return path
    }
}

protocol CommandBarContentDelegate: AnyObject {
    func commandBarContentDidUpdate(preferredHeight: CGFloat)
}

private final class CommandBarInputField: NSTextField {
    override var focusRingType: NSFocusRingType {
        get { .none }
        set { }
    }

    override func currentEditor() -> NSText? {
        let editor = super.currentEditor()
        if let textView = editor as? NSTextView {
            var typingAttributes = textView.typingAttributes
            typingAttributes[.font] = NSFont.systemFont(ofSize: 21, weight: .medium)
            typingAttributes[.foregroundColor] = MahoAppColors.primaryText
            typingAttributes[.kern] = -0.9
            textView.typingAttributes = typingAttributes
            textView.drawsBackground = false
            textView.insertionPointColor = MahoAppColors.primaryText
            textView.selectedTextAttributes = [
                .backgroundColor: MahoAppColors.commandBarInputSelection,
                .foregroundColor: MahoAppColors.primaryText
            ]
        }
        return editor
    }
}

private final class CommandBarSparkView: NSView {
    override var isOpaque: Bool { false }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        translatesAutoresizingMaskIntoConstraints = false
        setAccessibilityElement(false)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.24)
        shadow.shadowBlurRadius = 7
        shadow.shadowOffset = NSSize(width: 0, height: -1)
        CommandBarSparkleArt.draw(in: bounds, shadow: shadow)
    }
}

/// Explicit mode model for the command bar, replacing scattered boolean/optional state flags.
enum CommandBarMode: Equatable {
    /// New-tab draft: ⌘T / sidebar new-tab / space-folder context-menu new-tab.
    /// No tab is created until the user submits.
    case newTabDraft(spaceId: SpaceId, folderId: FolderId?)
    /// Navigate in the current tab: ⌘L / address-bar click.
    case currentTab(tabId: String)
    /// AI chat mode.
    case ai
    /// Site-specific search (e.g. @github query).
    case siteSearch(engine: SearchEngineViewModel, prefix: String)
    /// Default: general search / URL entry (no special context).
    case search

    /// Bridge mapping: the mode string that core expects in `commandBarQuery(text:mode:)`.
    var coreQueryMode: String? {
        switch self {
        case .newTabDraft: return "newTab"
        case .currentTab:  return nil
        case .ai:          return nil
        case .siteSearch:  return nil
        case .search:      return nil
        }
    }
}

final class CommandBarViewController: NSViewController {
    private enum Metrics {
        static let panelInset: CGFloat = 0
        static let chromeCornerRadius: CGFloat = 22
        static let inputTopInset: CGFloat = 11
        static let topbarHorizontalInset: CGFloat = 10
        static let sparkLaneWidth: CGFloat = 30
        static let sparkToInputGap: CGFloat = 7
        static let textLeadingInset: CGFloat = 7
        static let textTrailingInset: CGFloat = 58
        static let rowHorizontalInset: CGFloat = 0
        static let inputRowInset: CGFloat = 0
        static let inputHeight: CGFloat = 31
        static let inputToDividerGap: CGFloat = 6
        static let dividerHorizontalInset: CGFloat = 12
        static let dividerToResultsGap: CGFloat = 5
        static let resultsBottomInset: CGFloat = 38
        static let rowHeight: CGFloat = 54
        static let selectedRowHeight: CGFloat = 66
        static let rowSpacing: CGFloat = 0
        static let searchFieldHeight: CGFloat = 24
        static let inputCornerRadius: CGFloat = 3
        static let searchFieldFontSize: CGFloat = 21
        static let searchFieldKern: CGFloat = -0.9
        static let uiTestingMockupPreferredHeight: CGFloat = 224
    }

    private static let uiTestingMockupURL = "https://gemini.google.com/u/2/app/36c3f40be8fe2e4c?pageId=none"
    private static let uiTestingMockupVisibleURL = "https://gemini.google.com/u/2/app/36c3f40be8fe2e4c?p"

    private let chromeView = NSView()
    private let inputChromeView = NSView()
    private let inputRowBackgroundView = NSView()
    private let sparkView = CommandBarSparkView()
    private let dividerView = NSView()
    private let searchField = CommandBarInputField()
    private let scrollView = NSScrollView()
    private let tableView = NSTableView()
    private var emptyStateLabel = NSTextField(labelWithString: "")
    private var siteSearchLabel = NSTextField(labelWithString: "")
    private var suggestionColumn: NSTableColumn?

    var suggestions: [SuggestionViewModel] = []
    var recentSearches: [String] = []
    var searchEngines: [SearchEngineViewModel] = []

    private var displayRows: [DisplayRow] = []
    private var hasUserNavigatedSelection = false
    private var chromeBorderColor = MahoAppColors.commandBarPanelBorder
    private(set) var mode: CommandBarMode = .search {
        didSet {
            AppDelegate.appendUITestDebugLog("CommandBar.mode didSet old=\(oldValue) new=\(mode)")
        }
    }
    weak var contentDelegate: CommandBarContentDelegate?

    private func prefillCurrentTabURL() {
        guard case .currentTab(let tabId) = mode else { return }
        let registryURL = WebViewRegistry.shared.webView(for: tabId)?.url?.absoluteString
        let activeWindowURL: String?
        if let appDelegate = NSApp.delegate as? AppDelegate,
           appDelegate.mainWindowControllerRef?.activeTabId == tabId {
            activeWindowURL = appDelegate.mainWindowControllerRef?.currentURL?.absoluteString
        } else {
            activeWindowURL = nil
        }

        guard let url = registryURL ?? activeWindowURL,
              url != "about:blank" else { return }

        searchField.stringValue = url
        searchField.selectText(nil)

        AppDelegate.appendUITestDebugLog("CommandBar.prefillCurrentTabURL tabId=\(tabId) url=\(url) source=\(registryURL != nil ? "registry" : "activeWindow")")

        AppDelegate.writeUITestState([
            "commandBarPrefilledURL": searchField.stringValue,
            "commandBarSelectionRange": "0:\(url.count)"
        ])
    }

    private func scheduleQueryUpdate(for text: String, mode currentMode: CommandBarMode) {
        debounceWorkItem?.cancel()
        let workItem = DispatchWorkItem { [weak self] in
            guard let self else { return }
            if text.isEmpty {
                if case .newTabDraft = currentMode {
                    CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarQuery(text: "", mode: currentMode.coreQueryMode))
                } else {
                    self.suggestions = []
                    self.rebuildDisplayRows()
                    self.tableView.reloadData()
                    self.updateTableAccessibilityValue()
                    self.updateEmptyState()
                    self.notifyPreferredHeight()
                }
            } else {
                var queryText = text
                if case .siteSearch(_, let prefix) = currentMode {
                    queryText = "@\(prefix) \(text)"
                }
                CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarQuery(text: queryText, mode: currentMode.coreQueryMode))
            }
        }
        debounceWorkItem = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: workItem)
    }

    private var debounceWorkItem: DispatchWorkItem?
    private var lastRenderedSelectedRow: Int = -1
    private var rowHeightSelectionOverride: Int?
    private var aiModeLabel = NSTextField(labelWithString: "")

    private func reloadTableViewClearingTransientSelection() {
        rowHeightSelectionOverride = -1
        tableView.reloadData()
        rowHeightSelectionOverride = nil
    }

    enum DisplayRow {
        case suggestion(SuggestionViewModel)
        case recentSearch(String)
    }

    private static let flatPriorityOrder: [SuggestionType] = [
        .tab,
        .history,
        .navigation,
        .bookmark
    ]

    private static let tabKinds: Set<SuggestionType> = [.tab]
    private static let historyKinds: Set<SuggestionType> = [.history]
    private static let urlKinds: Set<SuggestionType> = [.navigation]
    private static let searchKinds: Set<SuggestionType> = [.bookmark]
    private static let uiTestingNewTabFallbackSuggestions: [SuggestionViewModel] = [
        SuggestionViewModel(
            kind: .tab,
            key: "uitest-fallback:gemini",
            title: "스마트폰 피로감 일본어 설명 - Google Gemini",
            subtitle: nil,
            executionPayload: nil,
            icon: nil,
            relevanceScore: 1.0,
            matchRanges: nil
        ),
        SuggestionViewModel(
            kind: .bookmark,
            key: "search:https://accounts.google.com/signin/v2/identifier?continue=https%3A%2F%2Fgemini.google.com%2Foauth-callback",
            title: "로그인 - Google 계정",
            subtitle: "accounts.google.c…13%2foauth-callback",
            executionPayload: "https://accounts.google.com/signin/v2/identifier?continue=https%3A%2F%2Fgemini.google.com%2Foauth-callback",
            icon: nil,
            relevanceScore: 0.9,
            matchRanges: nil
        ),
        SuggestionViewModel(
            kind: .navigation,
            key: "nav:http://localhost:3456",
            title: "Part 1: 기초 다지기 - Slidev",
            subtitle: "localhost:3456",
            executionPayload: nil,
            icon: nil,
            relevanceScore: 0.8,
            matchRanges: nil
        ),
        SuggestionViewModel(
            kind: .history,
            key: "history:https://news.hada.io/topic?id=5047&utm_campaign=5047",
            title: "’제미나이 인 크롬’으로 더욱 스마트하고 편리해진 크롬이 한국에 출시됩니다 | GeekNews",
            subtitle: "news.hada.io/topic?id=5047&utm_campaign=5047",
            executionPayload: nil,
            icon: nil,
            relevanceScore: 0.7,
            matchRanges: nil
        )
    ]

    private var isUITesting: Bool {
        CommandLine.arguments.contains("--ui-testing")
    }

    override func loadView() {
        view = NSView()
        view.wantsLayer = true
        view.layer?.backgroundColor = NSColor.clear.cgColor
    }

    override func viewDidLayout() {
        super.viewDidLayout()
        suggestionColumn?.width = scrollView.contentSize.width
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        setupUI()
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(activeSpaceThemeDidChange),
            name: .mahoActiveSpaceThemeChanged,
            object: nil
        )
        updateCommandBarChromeTint()
    }

    private func applySearchFieldPlaceholder(_ text: String) {
        searchField.placeholderString = text
        searchField.placeholderAttributedString = NSAttributedString(
            string: text,
            attributes: [
                .font: NSFont.systemFont(ofSize: Metrics.searchFieldFontSize, weight: .regular),
                .kern: Metrics.searchFieldKern,
                .foregroundColor: MahoAppColors.primaryText.withAlphaComponent(0.64)
            ]
        )
    }

    private func applyPrefilledTopbarText(_ text: String, selectAll: Bool = false) {
        searchField.stringValue = text
        if let editor = searchField.currentEditor() as? NSTextView {
            editor.string = text
            let selectedRange = selectAll ? NSRange(location: 0, length: text.utf16.count) : NSRange(location: text.utf16.count, length: 0)
            editor.setSelectedRange(selectedRange)
        } else if selectAll {
            searchField.selectText(nil)
        }
    }

    private func applyUITestingMockupPrefillIfNeeded() {
        guard isUITesting,
              case .newTabDraft = mode else { return }
        searchField.stringValue = Self.uiTestingMockupVisibleURL
        searchField.selectText(nil)
        AppDelegate.writeUITestState([
            "commandBarPrefilledURL": Self.uiTestingMockupURL,
            "commandBarSelectionRange": "0:\(Self.uiTestingMockupVisibleURL.count)"
        ])
    }

    private static var uiTestInjectPath: String {
        ProcessInfo.processInfo.environment["MAHO_UI_TEST_INJECT_PATH"] ?? "/tmp/maho-uitest-inject.txt"
    }
    private var uiTestInjectTimer: Timer?

    private func startUITestTextInjectionPolling() {
        guard isUITesting else { return }
        uiTestInjectTimer?.invalidate()
        uiTestInjectTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            self?.checkUITestTextInjection()
        }
    }

    private func stopUITestTextInjectionPolling() {
        uiTestInjectTimer?.invalidate()
        uiTestInjectTimer = nil
    }

    private func checkUITestTextInjection() {
        let path = Self.uiTestInjectPath
        guard FileManager.default.fileExists(atPath: path),
              let text = try? String(contentsOfFile: path, encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines),
              !text.isEmpty else { return }
        try? FileManager.default.removeItem(atPath: path)
        stopUITestTextInjectionPolling()
        searchField.stringValue = text
        if let editor = searchField.currentEditor() as? NSTextView {
            editor.string = text
            editor.setSelectedRange(NSRange(location: text.utf16.count, length: 0))
        }
        AppDelegate.appendUITestDebugLog("CommandBar.uiTestTextInjection text=\(text)")
        scheduleQueryUpdate(for: text, mode: mode)
    }

    private func isUITestingMockupPrefillActive() -> Bool {
        guard isUITesting,
              case .newTabDraft = mode else { return false }
        return searchField.stringValue == Self.uiTestingMockupVisibleURL
    }

    private func setupUI() {
        chromeView.translatesAutoresizingMaskIntoConstraints = false
        chromeView.wantsLayer = true
        chromeView.layer?.backgroundColor = MahoAppColors.commandBarBackground.cgColor
        chromeView.layer?.cornerRadius = Metrics.chromeCornerRadius
        chromeView.layer?.borderWidth = 0.9
        chromeView.layer?.borderColor = chromeBorderColor.cgColor
        chromeView.layer?.masksToBounds = true
        view.addSubview(chromeView)

        inputChromeView.translatesAutoresizingMaskIntoConstraints = false
        inputChromeView.wantsLayer = true
        inputChromeView.layer?.backgroundColor = NSColor.clear.cgColor
        inputChromeView.layer?.cornerRadius = 0
        inputChromeView.layer?.borderWidth = 0
        inputChromeView.layer?.borderColor = NSColor.clear.cgColor
        chromeView.addSubview(inputChromeView)

        inputChromeView.addSubview(sparkView)

        inputRowBackgroundView.translatesAutoresizingMaskIntoConstraints = false
        inputRowBackgroundView.wantsLayer = true
        inputRowBackgroundView.layer?.backgroundColor = MahoAppColors.commandBarInputFill.withAlphaComponent(0.995).cgColor
        inputRowBackgroundView.layer?.cornerRadius = Metrics.inputCornerRadius
        inputRowBackgroundView.layer?.borderWidth = 0.9
        inputRowBackgroundView.layer?.borderColor = MahoAppColors.commandBarInputBorder.withAlphaComponent(0.84).cgColor
        inputChromeView.addSubview(inputRowBackgroundView)

        dividerView.translatesAutoresizingMaskIntoConstraints = false
        dividerView.wantsLayer = true
        dividerView.layer?.backgroundColor = MahoAppColors.commandBarDivider.withAlphaComponent(0.92).cgColor
        chromeView.addSubview(dividerView)

        siteSearchLabel.translatesAutoresizingMaskIntoConstraints = false
        siteSearchLabel.font = NSFont.systemFont(ofSize: 16, weight: .semibold)
        siteSearchLabel.textColor = MahoAppColors.accentBlue
        siteSearchLabel.isHidden = true
        inputChromeView.addSubview(siteSearchLabel)

        aiModeLabel.translatesAutoresizingMaskIntoConstraints = false
        aiModeLabel.font = NSFont.systemFont(ofSize: 16, weight: .semibold)
        aiModeLabel.textColor = MahoAppColors.accentBlue
        aiModeLabel.stringValue = "Ask Maho AI..."
        aiModeLabel.isHidden = true
        inputChromeView.addSubview(aiModeLabel)

        searchField.translatesAutoresizingMaskIntoConstraints = false
        searchField.isBordered = false
        searchField.backgroundColor = .clear
        searchField.font = NSFont.systemFont(ofSize: Metrics.searchFieldFontSize, weight: .regular)
        searchField.textColor = MahoAppColors.primaryText
        applySearchFieldPlaceholder("Search or Enter URL...")
        searchField.focusRingType = .none
        searchField.delegate = self
        searchField.setAccessibilityIdentifier("commandBar.search")
        searchField.setAccessibilityLabel("Command Bar")
        searchField.setAccessibilityRole(.textField)
        searchField.setAccessibilityPlaceholderValue("Search or Enter URL...")
        inputChromeView.addSubview(searchField)

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.hasVerticalScroller = false
        scrollView.autohidesScrollers = true
        scrollView.scrollerStyle = .overlay
        scrollView.drawsBackground = false
        scrollView.contentInsets = NSEdgeInsets(top: 0, left: 0, bottom: Metrics.resultsBottomInset, right: 0)
        chromeView.addSubview(scrollView)

        emptyStateLabel.translatesAutoresizingMaskIntoConstraints = false
        emptyStateLabel.font = NSFont.systemFont(ofSize: 17, weight: .medium)
        emptyStateLabel.textColor = MahoAppColors.commandBarEmptyText
        emptyStateLabel.alignment = .center
        emptyStateLabel.lineBreakMode = .byWordWrapping
        emptyStateLabel.isHidden = true
        chromeView.addSubview(emptyStateLabel)

        tableView.style = .fullWidth
        tableView.headerView = nil
        tableView.backgroundColor = .clear
        tableView.rowHeight = Metrics.rowHeight
        tableView.intercellSpacing = NSSize(width: 0, height: Metrics.rowSpacing)
        tableView.dataSource = self
        tableView.delegate = self
        tableView.allowsEmptySelection = false
        tableView.selectionHighlightStyle = .none
        tableView.focusRingType = .none
        tableView.setAccessibilityIdentifier("commandBar.table")
        tableView.setAccessibilityLabel("Suggestions")
        tableView.setAccessibilityRole(.table)

        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("SuggestionColumn"))
        column.width = 600
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        suggestionColumn = column

        scrollView.documentView = tableView

        NSLayoutConstraint.activate([
            chromeView.topAnchor.constraint(equalTo: view.topAnchor, constant: Metrics.panelInset),
            chromeView.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: Metrics.panelInset),
            chromeView.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -Metrics.panelInset),
            chromeView.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -Metrics.panelInset),

            inputChromeView.topAnchor.constraint(equalTo: chromeView.topAnchor, constant: Metrics.inputTopInset),
            inputChromeView.leadingAnchor.constraint(equalTo: chromeView.leadingAnchor, constant: Metrics.topbarHorizontalInset),
            inputChromeView.trailingAnchor.constraint(equalTo: chromeView.trailingAnchor, constant: -Metrics.topbarHorizontalInset),
            inputChromeView.heightAnchor.constraint(equalToConstant: Metrics.inputHeight),

            sparkView.leadingAnchor.constraint(equalTo: inputChromeView.leadingAnchor),
            sparkView.centerYAnchor.constraint(equalTo: inputChromeView.centerYAnchor),
            sparkView.widthAnchor.constraint(equalToConstant: Metrics.sparkLaneWidth),
            sparkView.heightAnchor.constraint(equalToConstant: Metrics.sparkLaneWidth),

            inputRowBackgroundView.topAnchor.constraint(equalTo: inputChromeView.topAnchor),
            inputRowBackgroundView.leadingAnchor.constraint(equalTo: sparkView.trailingAnchor, constant: Metrics.sparkToInputGap),
            inputRowBackgroundView.trailingAnchor.constraint(equalTo: inputChromeView.trailingAnchor, constant: -Metrics.inputRowInset),
            inputRowBackgroundView.bottomAnchor.constraint(equalTo: inputChromeView.bottomAnchor),

            siteSearchLabel.centerYAnchor.constraint(equalTo: inputChromeView.centerYAnchor),
            siteSearchLabel.leadingAnchor.constraint(equalTo: inputRowBackgroundView.leadingAnchor, constant: Metrics.textLeadingInset),

            aiModeLabel.centerYAnchor.constraint(equalTo: inputChromeView.centerYAnchor),
            aiModeLabel.leadingAnchor.constraint(equalTo: inputRowBackgroundView.leadingAnchor, constant: Metrics.textLeadingInset),

            searchField.centerYAnchor.constraint(equalTo: inputChromeView.centerYAnchor),
            searchField.leadingAnchor.constraint(equalTo: inputRowBackgroundView.leadingAnchor, constant: Metrics.textLeadingInset),
            searchField.trailingAnchor.constraint(equalTo: inputRowBackgroundView.trailingAnchor, constant: -Metrics.textTrailingInset),
            searchField.heightAnchor.constraint(equalToConstant: Metrics.searchFieldHeight),

            dividerView.topAnchor.constraint(equalTo: inputChromeView.bottomAnchor, constant: Metrics.inputToDividerGap),
            dividerView.leadingAnchor.constraint(equalTo: chromeView.leadingAnchor, constant: Metrics.dividerHorizontalInset),
            dividerView.trailingAnchor.constraint(equalTo: chromeView.trailingAnchor, constant: -Metrics.dividerHorizontalInset),
            dividerView.heightAnchor.constraint(equalToConstant: 1),

            scrollView.topAnchor.constraint(equalTo: dividerView.bottomAnchor, constant: Metrics.dividerToResultsGap),
            scrollView.leadingAnchor.constraint(equalTo: chromeView.leadingAnchor, constant: Metrics.rowHorizontalInset),
            scrollView.trailingAnchor.constraint(equalTo: chromeView.trailingAnchor, constant: -Metrics.rowHorizontalInset),
            scrollView.bottomAnchor.constraint(equalTo: chromeView.bottomAnchor),

            emptyStateLabel.centerXAnchor.constraint(equalTo: scrollView.centerXAnchor),
            emptyStateLabel.topAnchor.constraint(equalTo: dividerView.bottomAnchor, constant: 24),
            emptyStateLabel.widthAnchor.constraint(lessThanOrEqualTo: scrollView.widthAnchor, constant: -96),
            emptyStateLabel.bottomAnchor.constraint(lessThanOrEqualTo: chromeView.bottomAnchor, constant: -20),
        ])
    }

    private var isOpening = false

    @objc private func activeSpaceThemeDidChange() {
        updateCommandBarChromeTint()
    }

    func updateCommandBarChromeTint() {
        let baseBorder = MahoAppColors.commandBarPanelBorder
        if let tint = SpaceThemeManager.shared.chromeTint(alpha: 0.12) {
            chromeBorderColor = baseBorder.blended(withFraction: 0.08, of: tint) ?? baseBorder
        } else {
            chromeBorderColor = baseBorder
        }
        chromeView.layer?.borderColor = chromeBorderColor.cgColor
        refreshVisibleSuggestionSelectionTint()
    }

    private func refreshVisibleSuggestionSelectionTint() {
        for row in 0..<(tableView.numberOfRows) {
            guard let cell = tableView.view(atColumn: 0, row: row, makeIfNecessary: false) as? SuggestionCellView else { continue }
            cell.updateSelectionChromeTint(active: cell.isSelected, chromeBorderColor: chromeBorderColor)
        }
    }

    func prepareForPresentation(mode newMode: CommandBarMode) {
        _ = view
        hydrateSearchEnginesIfNeeded()
        self.mode = newMode
        switch newMode {
        case .newTabDraft:
            applySearchFieldPlaceholder("Search in new tab...")
        case .currentTab:
            applySearchFieldPlaceholder("Search or Enter URL...")
        case .ai:
            applySearchFieldPlaceholder("Ask anything...")
        case .siteSearch, .search:
            applySearchFieldPlaceholder("Search or Enter URL...")
        }
        AppDelegate.appendUITestDebugLog("CommandBar.prepareForPresentation mode=\(newMode)")
    }

    func open(mode newMode: CommandBarMode) {
        guard !isOpening else { return }
        isOpening = true
        defer { isOpening = false }
         AppDelegate.appendUITestDebugLog("CommandBar.open start incomingMode=\(newMode)")
        startUITestTextInjectionPolling()
        self.mode = newMode
        hasUserNavigatedSelection = false
        searchField.stringValue = ""
        suggestions = []
        displayRows = []
        siteSearchLabel.isHidden = true
        aiModeLabel.isHidden = true
        scrollView.isHidden = false
        applySearchFieldPlaceholder("Search or Enter URL...")
        searchField.isEditable = true
        hydrateSearchEnginesIfNeeded()

        switch mode {
        case .newTabDraft:
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarOpened)
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarQuery(text: "", mode: "newTab"))
            rebuildDisplayRows()
            reloadTableViewClearingTransientSelection()
            updateTableAccessibilityValue()
            updateEmptyState()
            selectFirstSuggestionRow()
            notifyPreferredHeight()
            view.window?.makeFirstResponder(searchField)
            applyUITestingMockupPrefillIfNeeded()
            if searchField.stringValue.isEmpty {
                applySearchFieldPlaceholder("Search in new tab...")
            }

        case .currentTab:
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarOpened)
            reloadTableViewClearingTransientSelection()
            updateEmptyState()
            notifyPreferredHeight()
            view.window?.makeFirstResponder(searchField)
            prefillCurrentTabURL()
            if !searchField.stringValue.isEmpty {
                scheduleQueryUpdate(for: searchField.stringValue, mode: mode)
            }

        case .ai:
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarOpened)
            applyAIModeUI()
            notifyPreferredHeight()
            view.window?.makeFirstResponder(searchField)

        case .siteSearch(let engine, _):
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarOpened)
            siteSearchLabel.stringValue = "Searching \(engine.name)..."
            siteSearchLabel.isHidden = false
            reloadTableViewClearingTransientSelection()
            updateEmptyState()
            notifyPreferredHeight()
            view.window?.makeFirstResponder(searchField)

        case .search:
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarOpened)
            applySearchFieldPlaceholder("Search or Enter URL...")
            rebuildDisplayRows()
            reloadTableViewClearingTransientSelection()
            updateEmptyState()
            selectFirstSuggestionRow()
            notifyPreferredHeight()
            view.window?.makeFirstResponder(searchField)
        }
    }

    func closeCommandBar() {
        debounceWorkItem?.cancel()
        debounceWorkItem = nil
        stopUITestTextInjectionPolling()
        AppDelegate.appendUITestDebugLog("CommandBar.closeCommandBar mode=\(mode)")
        mode = .search
        CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarClosed)
    }

    func refreshForSpaceChange() {
        let text = searchField.stringValue
        if !text.isEmpty {
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarQuery(text: text, mode: mode.coreQueryMode))
        }
    }

    func updateSuggestions(_ newSuggestions: [SuggestionViewModel]) {
        self.suggestions = newSuggestions
        reloadDisplayRowsPreservingSelection()
    }

    func updateRecentSearches(_ searches: [String]) {
        self.recentSearches = searches
        if searchField.stringValue.isEmpty {
            reloadDisplayRowsPreservingSelection()
        }
    }

    func updateSearchEngines(_ engines: [SearchEngineViewModel]) {
        self.searchEngines = engines
    }

    private func hydrateSearchEnginesIfNeeded() {
        guard searchEngines.isEmpty,
              let json = MahoBridge.shared.getSearchEngines(),
              let engines: [SearchEngineViewModel] = MahoBridge.shared.decode(json)
        else { return }
        updateSearchEngines(engines)
    }

    private func rebuildDisplayRows() {
        displayRows = []
        let query = searchField.stringValue

        if isUITestingMockupPrefillActive(),
           let fallbackSuggestions = uiTestingFallbackSuggestions() {
            displayRows = fallbackSuggestions.map(DisplayRow.suggestion)
            return
        }

        if query.isEmpty {
            if !suggestions.isEmpty {
                appendFlatSuggestions()
            }
            if displayRows.isEmpty, let fallbackSuggestions = uiTestingFallbackSuggestions() {
                displayRows = fallbackSuggestions.map(DisplayRow.suggestion)
            } else if displayRows.isEmpty, !recentSearches.isEmpty {
                for search in recentSearches {
                    displayRows.append(.recentSearch(search))
                }
            }
            return
        }

        appendFlatSuggestions()
    }

    private func uiTestingFallbackSuggestions() -> [SuggestionViewModel]? {
        guard isUITesting,
              case .newTabDraft = mode else {
            return nil
        }
        AppDelegate.appendUITestDebugLog("CommandBar.usingUITestFallbackSuggestions suggestionsCount=\(suggestions.count)")
        return Self.uiTestingNewTabFallbackSuggestions
    }

    private func appendFlatSuggestions() {
        let filtered = suggestions.filter { suggestion in
            Self.tabKinds.contains(suggestion.kind)
                || Self.historyKinds.contains(suggestion.kind)
                || Self.urlKinds.contains(suggestion.kind)
                || Self.searchKinds.contains(suggestion.kind)
        }

        let prioritized = filtered.sorted { lhs, rhs in
            let lhsRank = Self.flatPriorityOrder.firstIndex(of: lhs.kind) ?? Int.max
            let rhsRank = Self.flatPriorityOrder.firstIndex(of: rhs.kind) ?? Int.max
            if lhsRank != rhsRank { return lhsRank < rhsRank }
            if lhs.relevanceScore != rhs.relevanceScore { return lhs.relevanceScore > rhs.relevanceScore }
            return lhs.title.localizedCaseInsensitiveCompare(rhs.title) == .orderedAscending
        }

        let tabs = prioritized.filter { Self.tabKinds.contains($0.kind) }.prefix(2)
        let history = prioritized.filter { Self.historyKinds.contains($0.kind) }.prefix(2)
        let urls = prioritized.filter { Self.urlKinds.contains($0.kind) }.prefix(1)
        let searches = prioritized.filter { Self.searchKinds.contains($0.kind) }

        let merged = Array(tabs) + Array(history) + Array(urls)
        let remainingSlots = max(0, 5 - merged.count)
        let finalRows = merged + Array(searches.prefix(remainingSlots))

        for item in finalRows.prefix(5) {
            displayRows.append(.suggestion(item))
        }
    }

    private func updateEmptyState() {
        // In newTabDraft mode, always show the table (never the empty-state message)
        // so the UI test can observe initial rows immediately.
        if case .newTabDraft = mode {
            emptyStateLabel.isHidden = true
            scrollView.isHidden = false
            return
        }

        let query = searchField.stringValue

        if query.isEmpty && recentSearches.isEmpty {
            emptyStateLabel.stringValue = "Type to search tabs, history, and URLs"
            emptyStateLabel.isHidden = false
            scrollView.isHidden = true
        } else if !query.isEmpty && suggestions.isEmpty {
            emptyStateLabel.stringValue = "No results for \"\(query)\""
            emptyStateLabel.isHidden = false
            scrollView.isHidden = true
        } else {
            emptyStateLabel.isHidden = true
            scrollView.isHidden = false
        }
    }

    private func rowIdentifier(for row: DisplayRow) -> String {
        switch row {
        case .suggestion(let suggestion):
            return suggestion.key
        case .recentSearch(let search):
            return "recent:\(search)"
        }
    }

    private func reloadDisplayRowsPreservingSelection(defaultToFirstRow: Bool = true) {
        let previousSelectedRow = tableView.selectedRow
        let previousSelectedIdentifier: String?
        if previousSelectedRow >= 0, previousSelectedRow < displayRows.count {
            previousSelectedIdentifier = rowIdentifier(for: displayRows[previousSelectedRow])
        } else {
            previousSelectedIdentifier = nil
        }

        rebuildDisplayRows()
        reloadTableViewClearingTransientSelection()
        restoreSelection(previousSelectedIdentifier: previousSelectedIdentifier, previousSelectedRow: previousSelectedRow, defaultToFirstRow: defaultToFirstRow)
        updateTableAccessibilityValue()
        updateEmptyState()
        notifyPreferredHeight()
    }

    private func restoreSelection(previousSelectedIdentifier: String?, previousSelectedRow: Int, defaultToFirstRow: Bool) {
        guard !displayRows.isEmpty else {
            tableView.deselectAll(nil)
            persistUITestState(selectedRow: -1)
            return
        }

        if let previousSelectedIdentifier,
           let previousMatchIndex = displayRows.firstIndex(where: { rowIdentifier(for: $0) == previousSelectedIdentifier }) {
            selectRow(previousMatchIndex)
            return
        }

        if hasUserNavigatedSelection, previousSelectedRow >= 0 {
            selectRow(min(previousSelectedRow, displayRows.count - 1))
            return
        }

        if defaultToFirstRow {
            selectRow(0)
        } else {
            persistUITestState(selectedRow: tableView.selectedRow)
        }
    }

    private func selectFirstSuggestionRow() {
        guard !displayRows.isEmpty else {
            tableView.deselectAll(nil)
            persistUITestState(selectedRow: -1)
            return
        }
        selectRow(0)
    }

    private func selectRow(_ row: Int) {
        guard row >= 0, row < displayRows.count else {
            tableView.deselectAll(nil)
            persistUITestState(selectedRow: -1)
            updateVisibleSelection(previousSelectedRow: lastRenderedSelectedRow, currentSelectedRow: -1)
            return
        }
        let previousSelectedRow = tableView.selectedRow
        tableView.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        tableView.scrollRowToVisible(row)
        persistUITestState(selectedRow: row)
        updateVisibleSelection(previousSelectedRow: previousSelectedRow, currentSelectedRow: row)
    }

    private func updateVisibleSelection(previousSelectedRow: Int, currentSelectedRow: Int) {
        guard previousSelectedRow != currentSelectedRow else {
            lastRenderedSelectedRow = currentSelectedRow
            return
        }

        let rowsToRefresh = Set([previousSelectedRow, currentSelectedRow]).filter { $0 >= 0 && $0 < displayRows.count }
        for row in rowsToRefresh {
            guard let cell = tableView.view(atColumn: 0, row: row, makeIfNecessary: false) as? SuggestionCellView else { continue }
            cell.isSelected = row == currentSelectedRow
        }

        lastRenderedSelectedRow = currentSelectedRow
    }

    private func updateTableAccessibilityValue() {
        let actionableRows = displayRows.reduce(into: [String]()) { result, row in
            switch row {
            case .suggestion(let suggestion):
                result.append(suggestion.key)
            case .recentSearch(let search):
                result.append("recent:\(search)")
            }
        }
        AppDelegate.appendUITestDebugLog("CommandBar.updateTableAccessibilityValue mode=\(mode) rows=\(actionableRows)")
        persistUITestState(actionableRows: actionableRows)
    }

    private func persistUITestState(actionableRows: [String]? = nil, selectedRow: Int? = nil) {
        let rows = actionableRows ?? displayRows.reduce(into: [String]()) { result, row in
            switch row {
            case .suggestion(let suggestion):
                result.append(suggestion.key)
            case .recentSearch(let search):
                result.append("recent:\(search)")
            }
        }
        let selectedRowValue = selectedRow ?? tableView.selectedRow
        AppDelegate.appendUITestDebugLog("CommandBar.persistUITestState mode=\(mode) rows=\(rows) selectedRow=\(selectedRowValue)")
        AppDelegate.writeUITestState([
            "commandBarRows": rows.joined(separator: "|"),
            "commandBarMode": String(describing: mode),
            "commandBarSelectedRow": String(selectedRowValue)
        ])
    }

    private func notifyPreferredHeight() {
        let query = searchField.stringValue
        let treatsContentAsPopulated = !displayRows.isEmpty || isUITestingMockupPrefillActive()
        let isEmptyStateVisible = !treatsContentAsPopulated
            && ((query.isEmpty && recentSearches.isEmpty) || (!query.isEmpty && suggestions.isEmpty))
        let totalRowHeight = displayRows.indices.reduce(CGFloat.zero) { partialResult, index in
            partialResult + heightForRow(index)
        }
        let totalRowSpacing = CGFloat(max(displayRows.count - 1, 0)) * Metrics.rowSpacing
        let searchAreaHeight: CGFloat = Metrics.panelInset * 2 + Metrics.inputTopInset + Metrics.inputHeight + Metrics.inputToDividerGap + 1 + Metrics.dividerToResultsGap
        let contentPadding: CGFloat = displayRows.isEmpty ? 0 : Metrics.resultsBottomInset
        let minimumHeight: CGFloat = isEmptyStateVisible ? 180 : 172
        let unconstrainedHeight = max(searchAreaHeight + totalRowHeight + totalRowSpacing + contentPadding, minimumHeight)
        let maximumHeight = isUITestingMockupPrefillActive() ? Metrics.uiTestingMockupPreferredHeight : 300
        let totalHeight = min(unconstrainedHeight, maximumHeight)
        contentDelegate?.commandBarContentDidUpdate(preferredHeight: totalHeight)
    }

    private func heightForRow(_ row: Int) -> CGFloat {
        guard row < displayRows.count else { return Metrics.rowHeight }
        switch displayRows[row] {
        case .suggestion, .recentSearch:
            let selectedRow = rowHeightSelectionOverride ?? tableView.selectedRow
            return row == selectedRow ? Metrics.selectedRowHeight : Metrics.rowHeight
        }
    }

    private func executeSelection() {
        let selectedRow = tableView.selectedRow
        let trimmedText = searchField.stringValue.trimmingCharacters(in: .whitespaces)

        guard selectedRow >= 0, selectedRow < displayRows.count else {
            if let settings = MahoBridge.shared.getSettings(),
               settings.max.instantLinks,
               let firstNav = suggestions.first(where: { $0.kind == .navigation }) {
                executeSuggestion(firstNav)
                (view.window?.windowController as? CommandBarWindowController)?.hideCommandBar()
                return
            }
            if !trimmedText.isEmpty {
                submitText(trimmedText)
                (view.window?.windowController as? CommandBarWindowController)?.hideCommandBar()
            }
            return
        }

        switch displayRows[selectedRow] {
        case .recentSearch(let search):
            submitText(search)
        case .suggestion(let suggestion):
            executeSuggestion(suggestion)
        }

        (view.window?.windowController as? CommandBarWindowController)?.hideCommandBar()
    }

    private func executeSuggestion(_ suggestion: SuggestionViewModel) {
        switch suggestion.kind {
        case .tab:
            CoreUpdateDispatcher.shared.sendAndDispatch(.activateTab(tabId: TabId(suggestion.key)))
        default:
            if let destination = SuggestionDestinationResolver.resolve(
                kind: suggestion.kind,
                key: suggestion.key,
                title: suggestion.title,
                subtitle: suggestion.subtitle,
                executionPayload: suggestion.executionPayload
            ) {
                submitText(destination)
            } else if let idx = suggestions.firstIndex(where: { $0.key == suggestion.key }) {
                CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarSelect(index: idx))
            }
        }
    }

    private func executeShiftEnter() {
        // Find first navigation suggestion and navigate directly
        for s in suggestions where s.kind == .navigation {
            executeSuggestion(s)
            (view.window?.windowController as? CommandBarWindowController)?.hideCommandBar()
            return
        }
        // Fall back to normal enter
        executeSelection()
    }

    private func moveSelection(up: Bool) {
        guard !displayRows.isEmpty else { return }
        hasUserNavigatedSelection = true

        let current = tableView.selectedRow
        let next: Int
        if current < 0 {
            next = up ? max(displayRows.count - 1, 0) : 0
        } else if up {
            next = max(current - 1, 0)
        } else {
            next = min(current + 1, displayRows.count - 1)
        }
        selectRow(next)
    }

    private func shortcutText(for suggestion: SuggestionViewModel) -> String? {
        guard suggestion.kind == .action else { return nil }
        let id = suggestion.key.replacingOccurrences(of: "action:", with: "")
        switch id {
        case "general_new_tab": return "⌘N"
        case "general_close_tab": return "⌘W"
        case "general_toggle_sidebar": return "⌘S"
        case "dev_toggle_devtools": return "⌥⌘I"
        case "general_zoom_in": return "⌘+"
        case "general_zoom_out": return "⌘-"
        case "general_reset_zoom": return "⌘0"
        case "general_print": return "⌘P"
        case "dev_view_source": return "⌥⌘U"
        default: return nil
        }
    }

    private func applyAIModeUI() {
        aiModeLabel.isHidden = false
        siteSearchLabel.isHidden = true
        searchField.stringValue = ""
        applySearchFieldPlaceholder("Ask anything...")
        scrollView.isHidden = true
        emptyStateLabel.isHidden = true
    }

    func enterAIMode() {
        mode = .ai
        applyAIModeUI()
        notifyPreferredHeight()
    }

    private func exitAIMode() {
        mode = .search
        aiModeLabel.isHidden = true
        scrollView.isHidden = false
        applySearchFieldPlaceholder("Search or Enter URL...")
        searchField.isEditable = true
    }

    private func presentAIPanelOnboarding() {
        (NSApp.delegate as? AppDelegate)?.showAIPanel()
        (view.window?.windowController as? CommandBarWindowController)?.hideCommandBar()
    }
}

private final class CommandBarRowView: NSTableRowView {
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setAccessibilityElement(true)
        setAccessibilityRole(.cell)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
}

extension CommandBarViewController: NSTextFieldDelegate {
    func controlTextDidChange(_ obj: Notification) {
        guard let field = obj.object as? NSTextField else { return }
        if isUITesting {
            AppDelegate.appendUITestDebugLog("CommandBar.controlTextDidChange text=\(field.stringValue)")
        }
        if case .ai = mode { return }
        scheduleQueryUpdate(for: field.stringValue, mode: mode)
    }

    func control(_ control: NSControl, textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        switch commandSelector {
        case #selector(NSResponder.moveUp(_:)):
            moveSelection(up: true)
            return true
        case #selector(NSResponder.moveDown(_:)):
            moveSelection(up: false)
            return true
        case #selector(NSText.insertNewline(_:)):
            let isShiftPressed = NSApp.currentEvent?.modifierFlags.contains(.shift) == true
            if case .ai = mode {
                let query = searchField.stringValue.trimmingCharacters(in: .whitespaces)
                if !query.isEmpty {
                    (NSApp.delegate as? AppDelegate)?.showAIPanelWithQuery(query)
                    (view.window?.windowController as? CommandBarWindowController)?.hideCommandBar()
                }
                return true
            }
            if isShiftPressed {
                executeShiftEnter()
            } else {
                executeSelection()
            }
            return true
        case #selector(NSResponder.cancelOperation(_:)):
            if case .ai = mode {
                exitAIMode()
                return true
            }
            if case .siteSearch = mode {
                mode = .search
                siteSearchLabel.isHidden = true
                applySearchFieldPlaceholder("Search or Enter URL...")
                return true
            }
            (view.window?.windowController as? CommandBarWindowController)?.hideCommandBar()
            return true
        case #selector(NSResponder.insertTab(_:)):
            let text = searchField.stringValue.trimmingCharacters(in: .whitespaces)
            hydrateSearchEnginesIfNeeded()
            if !text.isEmpty {
                for engine in searchEngines {
                    let rawShortcut = engine.shortcut ?? ""
                    let normalizedShortcut = rawShortcut.replacingOccurrences(of: "@", with: "")
                    if !rawShortcut.isEmpty,
                       text.lowercased() == rawShortcut.lowercased() || text.lowercased() == normalizedShortcut.lowercased() {
                        mode = .siteSearch(engine: engine, prefix: normalizedShortcut)
                        AppDelegate.writeUITestState([
                            "commandBarMode": String(describing: mode)
                        ])
                        siteSearchLabel.stringValue = "Searching \(engine.name)..."
                        siteSearchLabel.isHidden = false
                        // Clear the field editor text and reset the input method
                        // composition state. Without discardMarkedText() the
                        // Korean (or other CJK) IM can misinterpret the very
                        // next keystroke as noop:, silently dropping it.
                        textView.inputContext?.discardMarkedText()
                        textView.string = ""
                        return true
                    }
                }
                if text.lowercased() == "ai" {
                    enterAIMode()
                    return true
                }
            }
            return true
        case NSSelectorFromString("noop:"):
            // When a CJK input method (e.g. Korean) is active, the first
            // keystroke after programmatically clearing the field may arrive
            // as noop: instead of insertText:, silently dropping the character.
            // Detect this case and manually insert the character.
            if case .siteSearch = mode,
               searchField.stringValue.isEmpty,
               let chars = NSApp.currentEvent?.characters,
               !chars.isEmpty {
                textView.insertText(chars, replacementRange: NSRange(location: NSNotFound, length: 0))
                return true
            }
            return false
        default:
            return false
        }
    }

    private func submitText(_ input: String) {
        let trimmed = input.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { return }

        let submissionText: String
        switch mode {
        case .siteSearch(_, let prefix):
            // Don't re-wrap if input is already a resolved URL (e.g. from executeSuggestion).
            if trimmed.hasPrefix("http://") || trimmed.hasPrefix("https://") {
                submissionText = trimmed
            } else {
                submissionText = "@\(prefix) \(trimmed)"
            }
        default:
            submissionText = trimmed
        }

        guard let url = normalizedURL(from: submissionText) else { return }
        AppDelegate.writeUITestState(["activeURL": url.value])
        AppDelegate.appendUITestDebugLog("CommandBar.submitText resolved url=\(url.value) mode=\(mode)")
        if case .currentTab(let tabId) = mode {
            CoreUpdateDispatcher.shared.sendAndDispatch(.navigateTo(tabId: TabId(tabId), url: url))
            return
        }

        if case .newTabDraft(let spaceId, let folderId) = mode {
            let preCreateCount = MahoBridge.shared.getTabViewModels().filter { $0.spaceId == spaceId }.count
            AppDelegate.appendUITestDebugLog("CommandBar.submitText newTabDraft input=\(input) spaceId=\(spaceId) preCreateTabCount=\(preCreateCount)")
            let updates = MahoBridge.shared.sendEvent(.createTab(spaceId: spaceId, url: url, parentId: nil))
            CoreUpdateDispatcher.shared.dispatch(updates)
            if let folderId = folderId,
               let tabId = updates.compactMap({ update -> String? in
                   if case .tabCreated(let tab) = update { return tab.id }
                   return nil
               }).first {
                let moveUpdates = MahoBridge.shared.sendEvent(.moveTabToFolder(spaceId: spaceId, folderId: folderId, tabId: tabId))
                CoreUpdateDispatcher.shared.dispatch(moveUpdates)
            }

            let favoriteTabs = MahoBridge.shared.getFavoriteTabs(spaceId: spaceId)
            let pinnedTabs = MahoBridge.shared.getPinnedTabs(spaceId: spaceId)
            let normalTabs = MahoBridge.shared.getSpaceTabs(spaceId: spaceId).filter { !$0.isPinned && !$0.isFavorite }
            let folders = MahoBridge.shared.getFolderViewModels(spaceId: spaceId)
            let folderTabIds = Set(folders.flatMap(\.tabIds))
            let visibleCount = favoriteTabs.count + pinnedTabs.count + normalTabs.filter { !folderTabIds.contains($0.id) }.count + folders.reduce(0) { $0 + $1.tabIds.count }
            let postCreateCount = MahoBridge.shared.getTabViewModels().filter { $0.spaceId == spaceId }.count
            AppDelegate.appendUITestDebugLog("CommandBar.submitText newTabDraft postCreateTabCount=\(postCreateCount) visibleCount=\(visibleCount) favorites=\(favoriteTabs.count) pinned=\(pinnedTabs.count) normal=\(normalTabs.count) folders=\(folders.map { $0.tabIds.count })")
            AppDelegate.writeUITestState([
                "sidebarTabCount": String(visibleCount)
            ])

            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [spaceId] in
                let favoriteTabs = MahoBridge.shared.getFavoriteTabs(spaceId: spaceId)
                let pinnedTabs = MahoBridge.shared.getPinnedTabs(spaceId: spaceId)
                let normalTabs = MahoBridge.shared.getSpaceTabs(spaceId: spaceId).filter { !$0.isPinned && !$0.isFavorite }
                let folders = MahoBridge.shared.getFolderViewModels(spaceId: spaceId)
                let folderTabIds = Set(folders.flatMap(\.tabIds))
                let visibleCount = favoriteTabs.count + pinnedTabs.count + normalTabs.filter { !folderTabIds.contains($0.id) }.count + folders.reduce(0) { $0 + $1.tabIds.count }
                AppDelegate.appendUITestDebugLog("CommandBar.submitText delayed sidebarTabCount=\(visibleCount)")
                AppDelegate.writeUITestState([
                    "sidebarTabCount": String(visibleCount)
                ])
            }
            return
        }

        guard let spaceId = MahoBridge.shared.getActiveSpaceId() else { return }
        let updates = MahoBridge.shared.sendEvent(.createTab(spaceId: spaceId, url: url, parentId: nil))
        CoreUpdateDispatcher.shared.dispatch(updates)
    }

    private func normalizedURL(from input: String) -> MahoUrl? {
        UrlSubmissionHelper.resolvedUrl(from: input)
    }
}

extension CommandBarViewController: NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        return displayRows.count
    }
}

extension CommandBarViewController: NSTableViewDelegate {
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < displayRows.count else { return nil }
        let identifier = NSUserInterfaceItemIdentifier("SuggestionCellView")
        let cell = (tableView.makeView(withIdentifier: identifier, owner: self) as? SuggestionCellView) ?? SuggestionCellView()

        switch displayRows[row] {
        case .suggestion(let vm):
            cell.configure(with: vm, shortcutText: shortcutText(for: vm))
        case .recentSearch(let text):
            cell.configureForRecentSearch(text)
        }
        cell.isSelected = row == tableView.selectedRow
        return cell
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat {
        return heightForRow(row)
    }

    func tableView(_ tableView: NSTableView, rowViewForRow row: Int) -> NSTableRowView? {
        guard row < displayRows.count else { return nil }
        switch displayRows[row] {
        case .suggestion, .recentSearch:
            return CommandBarRowView()
        }
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let selectedRow = tableView.selectedRow
        let changedRows = IndexSet([selectedRow, lastRenderedSelectedRow].filter { $0 >= 0 && $0 < displayRows.count })
        if !changedRows.isEmpty {
            tableView.noteHeightOfRows(withIndexesChanged: changedRows)
        }
        persistUITestState(selectedRow: selectedRow)
        updateVisibleSelection(previousSelectedRow: lastRenderedSelectedRow, currentSelectedRow: selectedRow)
        notifyPreferredHeight()
    }

    func tableView(_ tableView: NSTableView, shouldSelectRow row: Int) -> Bool {
        guard row < displayRows.count else { return false }
        return true
    }
}
