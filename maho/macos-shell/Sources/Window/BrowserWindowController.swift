import AppKit
import QuartzCore
import WebKit

final class BrowserWindowController: NSWindowController, CoreUpdateObserver {
    private lazy var webViewManager = WebViewManager()
    private lazy var toolbarController = ToolbarController()
    lazy var sidebarController = SidebarController()

    private lazy var splitView = ArcSplitView()
    private var sidebarView: NSView { sidebarController.view }
    private lazy var contentView = CanvasBackgroundView()
    private lazy var contentColumnView = NSView()
    private lazy var contentSurfaceContainerView = NSView()
    private lazy var webContentContainerView = NSView()
    private lazy var archiveDetailView = ArchiveDetailSurfaceView()
    private var expandedSidebarWidth: CGFloat = BrowserWindowController.defaultSidebarWidth
    private var isArchivePresentationActive = false
    private lazy var findBarView = FindBarView(frame: .zero)
    private var notificationBanner: NotificationBannerView?
    private var littleArcController: LittleArcWindowController?
    private var peekController: PeekWindowController?
    private lazy var titlebarChromeView = NSVisualEffectView(frame: .zero)
    private var memoryMonitorTimer: Timer?
    private var browsingActivity: NSUserActivity?
    private lazy var aiSidePanel = AISidePanel(frame: .zero)
    private var aiPanelContainer: NSView?
    private var aiPanelWidthConstraint: NSLayoutConstraint?
    private var aiPanelConstraints: [NSLayoutConstraint] = []
    private var floatingAIPanel: NSPanel?
    private var isAIPanelVisible = false
    private var miniPlayerWindowController: MiniPlayerWindowController?
    private var previousActiveSpaceId: String?
    private var didLogInitialWindowDidResize = false
    private var sidebarWidthPersistTimer: Timer?

    var activeTabId: TabId? { return webViewManager.activeTabId }

    var currentURL: URL? {
        guard let tabId = webViewManager.activeTabId,
              let webView = webViewManager.webViews[tabId] else { return nil }
        return webView.url
    }

    func evaluateJavaScriptInActiveTab(_ script: String) {
        guard let tabId = webViewManager.activeTabId,
              let webView = webViewManager.webViews[tabId] else { return }
        webView.evaluateJavaScript(script, completionHandler: nil)
    }

    static let compactSidebarWidthThreshold: CGFloat = ChromeGeometry.compactWidthThreshold
    private static let defaultSidebarWidth: CGFloat = compactSidebarWidthThreshold + 20
    private static let minSidebarWidth: CGFloat = 180
    private static let maxSidebarWidth: CGFloat = 400
    private static let minContentWidth: CGFloat = 400

    private static let mainWindowDefaultFrame = NSRect(x: 0, y: 0, width: 1200, height: 800)
    private static let mainWindowMinimumSize = NSSize(width: 600, height: 400)

    static func create() -> BrowserWindowController {
        let window = NSWindow(
            contentRect: mainWindowDefaultFrame,
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        appendFrameSnapshotLog("create: after NSWindow init", window: window, splitView: nil)
        window.title = "Maho"
        window.identifier = NSUserInterfaceItemIdentifier("mainWindow")
        window.setAccessibilityIdentifier("mainWindow")
        window.isRestorable = false
        window.center()
        appendFrameSnapshotLog("create: after center", window: window, splitView: nil)
        window.titlebarAppearsTransparent = true
        window.titleVisibility = .hidden
        if #available(macOS 11.0, *) {
            window.toolbarStyle = .unifiedCompact
        }
        window.backgroundColor = MahoAppColors.windowBackground
        window.minSize = Self.mainWindowMinimumSize
        window.contentMinSize = Self.mainWindowMinimumSize

        let wc = BrowserWindowController(window: window)
        window.delegate = wc
        wc.webViewManager.owner = wc

        wc.setupToolbar()
        wc.setupSplitView()
        if let settings = MahoBridge.shared.getSettings() {
            wc.applyAppearanceSettings(settings.appearance)
        }
        wc.appendFrameSnapshot("create: after setupSplitView completes")

        CoreUpdateDispatcher.shared.register(wc)
        wc.startMemoryMonitoring()

        NotificationCenter.default.addObserver(wc, selector: #selector(handleSkipBack(_:)), name: .miniPlayerSkipBack, object: nil)
        NotificationCenter.default.addObserver(wc, selector: #selector(handleSkipForward(_:)), name: .miniPlayerSkipForward, object: nil)
        NotificationCenter.default.addObserver(wc, selector: #selector(handlePlayPause(_:)), name: .miniPlayerPlayPause, object: nil)
        NotificationCenter.default.addObserver(wc, selector: #selector(handleATCLinkRule(_:)), name: Notification.Name("ATCLinkRuleTriggered"), object: nil)
        NotificationCenter.default.addObserver(wc, selector: #selector(activeSpaceThemeDidChange), name: .mahoActiveSpaceThemeChanged, object: nil)
        return wc
    }

    deinit {
        memoryMonitorTimer?.invalidate()
        sidebarWidthPersistTimer?.invalidate()
        browsingActivity?.invalidate()
        NotificationCenter.default.removeObserver(self)
        CoreUpdateDispatcher.shared.unregister(self)
    }

    private func startMemoryMonitoring() {
        memoryMonitorTimer = Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { [weak self] _ in
            self?.checkMemoryUsage()
        }
    }

    private func checkMemoryUsage() {
        var info = mach_task_basic_info()
        var count = mach_msg_type_number_t(MemoryLayout<mach_task_basic_info>.size) / 4
        let result = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(MACH_TASK_BASIC_INFO), $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return }

        let usedBytes = info.resident_size
        let totalBytes = ProcessInfo.processInfo.physicalMemory
        let ratio = Double(usedBytes) / Double(totalBytes)

        if ratio > 0.80 {
            let webViewCount = webViewManager.webViews.count
            let level: MemoryPressureLevel = ratio > 0.90 ? .critical : .warning
            CoreUpdateDispatcher.shared.sendAndDispatch(.memoryWarning(level: level))

            let pct = Int(ratio * 100)
            let notification = NotificationViewModel(
                id: "memory-monitor",
                title: "Memory",
                message: "Using \(pct)% of memory (\(webViewCount) tabs open)",
                icon: nil,
                actions: []
            )
            DispatchQueue.main.async { [weak self] in
                self?.showNotificationBanner(notification)
            }
        }
    }

    // MARK: - Setup

    private func setupToolbar() {
        guard let window = window else { return }
        toolbarController.window = window
        window.toolbar = toolbarController.makeToolbar()
    }

    private func setupSplitView() {
        guard let window = window else { return }

        splitView.isVertical = true
        splitView.dividerStyle = .thin
        splitView.delegate = self
        let splitViewMinimumWidthConstraint = splitView.widthAnchor.constraint(greaterThanOrEqualToConstant: BrowserWindowController.mainWindowMinimumSize.width)
        splitViewMinimumWidthConstraint.isActive = true

        sidebarController.browserWindowController = self
        _ = sidebarController.view

        let sidebarContainerView = NSView()
        sidebarContainerView.translatesAutoresizingMaskIntoConstraints = false
        sidebarContainerView.addSubview(sidebarView)
        sidebarView.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            sidebarView.topAnchor.constraint(equalTo: sidebarContainerView.topAnchor),
            sidebarView.leadingAnchor.constraint(equalTo: sidebarContainerView.leadingAnchor),
            sidebarView.trailingAnchor.constraint(equalTo: sidebarContainerView.trailingAnchor),
            sidebarView.bottomAnchor.constraint(equalTo: sidebarContainerView.bottomAnchor),
        ])

        sidebarContainerView.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let sidebarContainerMinimumWidthConstraint = sidebarContainerView.widthAnchor.constraint(greaterThanOrEqualToConstant: BrowserWindowController.minSidebarWidth)
        sidebarContainerMinimumWidthConstraint.isActive = true

        contentColumnView.translatesAutoresizingMaskIntoConstraints = false
        let contentColumnMinimumWidthConstraint = contentColumnView.widthAnchor.constraint(greaterThanOrEqualToConstant: BrowserWindowController.minContentWidth)
        contentColumnMinimumWidthConstraint.isActive = true
        contentSurfaceContainerView.translatesAutoresizingMaskIntoConstraints = false
        webContentContainerView.translatesAutoresizingMaskIntoConstraints = false
        webContentContainerView.wantsLayer = true
        archiveDetailView.translatesAutoresizingMaskIntoConstraints = false
        archiveDetailView.isHidden = true

        contentColumnView.addSubview(contentView)
        contentView.translatesAutoresizingMaskIntoConstraints = false
        contentView.addSubview(contentSurfaceContainerView)
        contentSurfaceContainerView.addSubview(webContentContainerView)
        contentSurfaceContainerView.addSubview(archiveDetailView)
        NSLayoutConstraint.activate([
            contentView.topAnchor.constraint(equalTo: contentColumnView.topAnchor),
            contentView.leadingAnchor.constraint(equalTo: contentColumnView.leadingAnchor),
            contentView.trailingAnchor.constraint(equalTo: contentColumnView.trailingAnchor),
            contentView.bottomAnchor.constraint(equalTo: contentColumnView.bottomAnchor),

            contentSurfaceContainerView.topAnchor.constraint(equalTo: contentView.topAnchor),
            contentSurfaceContainerView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            contentSurfaceContainerView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            contentSurfaceContainerView.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),

            webContentContainerView.topAnchor.constraint(equalTo: contentSurfaceContainerView.topAnchor),
            webContentContainerView.leadingAnchor.constraint(equalTo: contentSurfaceContainerView.leadingAnchor),
            webContentContainerView.trailingAnchor.constraint(equalTo: contentSurfaceContainerView.trailingAnchor),
            webContentContainerView.bottomAnchor.constraint(equalTo: contentSurfaceContainerView.bottomAnchor),

            archiveDetailView.topAnchor.constraint(equalTo: contentSurfaceContainerView.topAnchor),
            archiveDetailView.leadingAnchor.constraint(equalTo: contentSurfaceContainerView.leadingAnchor),
            archiveDetailView.trailingAnchor.constraint(equalTo: contentSurfaceContainerView.trailingAnchor),
            archiveDetailView.bottomAnchor.constraint(equalTo: contentSurfaceContainerView.bottomAnchor),
        ])

        findBarView.translatesAutoresizingMaskIntoConstraints = false
        webContentContainerView.addSubview(findBarView)
        NSLayoutConstraint.activate([
            findBarView.leadingAnchor.constraint(equalTo: webContentContainerView.leadingAnchor),
            findBarView.trailingAnchor.constraint(equalTo: webContentContainerView.trailingAnchor),
            findBarView.bottomAnchor.constraint(equalTo: webContentContainerView.bottomAnchor),
        ])
        findBarView.isHidden = true


        splitView.addArrangedSubview(sidebarContainerView)
        splitView.addArrangedSubview(contentColumnView)

        let aiContainer = NSView()
        aiContainer.translatesAutoresizingMaskIntoConstraints = false
        aiContainer.wantsLayer = true
        aiContainer.isHidden = true
        aiContainer.setAccessibilityIdentifier("ai.panel")

        aiSidePanel.translatesAutoresizingMaskIntoConstraints = false
        aiSidePanel.delegate = self
        aiSidePanel.setDisplayMode(.sidebar)
        aiContainer.addSubview(aiSidePanel)
        aiPanelConstraints = [
            aiSidePanel.topAnchor.constraint(equalTo: aiContainer.topAnchor),
            aiSidePanel.leadingAnchor.constraint(equalTo: aiContainer.leadingAnchor),
            aiSidePanel.trailingAnchor.constraint(equalTo: aiContainer.trailingAnchor),
            aiSidePanel.bottomAnchor.constraint(equalTo: aiContainer.bottomAnchor),
        ]
        NSLayoutConstraint.activate(aiPanelConstraints)

        let widthConstraint = aiContainer.widthAnchor.constraint(equalToConstant: BrowserWindowController.aiPanelMinimumVisibleWidth)
        widthConstraint.isActive = false  // starts inactive; aiContainer is hidden
        aiPanelWidthConstraint = widthConstraint
        aiPanelContainer = aiContainer
        splitView.addArrangedSubview(aiContainer)

        self.sidebarContainer = sidebarContainerView

        splitView.setHoldingPriority(.defaultHigh, forSubviewAt: 0)
        splitView.setHoldingPriority(.defaultLow, forSubviewAt: 1)
        splitView.setHoldingPriority(.defaultHigh, forSubviewAt: 2)

        let contentViewWrapper = AppearanceAwareContentViewWrapper()
        contentViewWrapper.onEffectiveAppearanceChanged = { [weak self] in
            self?.refreshWindowAppearance()
        }
        contentViewWrapper.translatesAutoresizingMaskIntoConstraints = false
        contentViewWrapper.addSubview(splitView)
        splitView.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            splitView.topAnchor.constraint(equalTo: contentViewWrapper.topAnchor),
            splitView.leadingAnchor.constraint(equalTo: contentViewWrapper.leadingAnchor),
            splitView.trailingAnchor.constraint(equalTo: contentViewWrapper.trailingAnchor),
            splitView.bottomAnchor.constraint(equalTo: contentViewWrapper.bottomAnchor),
        ])

        window.contentView = contentViewWrapper
        configureWindowChrome(for: window)
        refreshWindowAppearance()
        updateArchivePresentation(active: false)
        splitView.setPosition(BrowserWindowController.defaultSidebarWidth, ofDividerAt: 0)
        window.contentView?.layoutSubtreeIfNeeded()
    }

    func setArchivePresentationActive(_ active: Bool) {
        updateArchivePresentation(active: active)
    }

    private func updateArchivePresentation(active: Bool) {
        isArchivePresentationActive = active
        archiveDetailView.configure(
            spaceName: activeSpaceName(),
            tabTitle: currentArchiveSurfaceTitle(),
            url: currentArchiveSurfaceURL(),
            archivedCount: currentArchivedTabCount()
        )
        archiveDetailView.isHidden = true
        webContentContainerView.isHidden = false
        contentColumnView.isHidden = active
        if active {
            findBarView.isHidden = true
            splitView.adjustSubviews()
            splitView.layoutSubtreeIfNeeded()
        } else {
            splitView.adjustSubviews()
            splitView.setPosition(expandedSidebarWidth, ofDividerAt: 0)
            splitView.layoutSubtreeIfNeeded()
        }
    }

    private func activeSpaceName() -> String {
        guard let activeSpaceId = sidebarController.activeSpaceId ?? MahoBridge.shared.getCanonicalActiveSpaceId() else {
            return "Library"
        }
        let spaces = MahoBridge.shared.getCanonicalSpaceViewModels()
        return spaces.first(where: { $0.id == activeSpaceId })?.name ?? "Library"
    }

    private func currentArchiveSurfaceTitle() -> String {
        let title = currentTitleForActiveTab().trimmingCharacters(in: .whitespacesAndNewlines)
        if !title.isEmpty {
            return title
        }
        let urlString = currentUrlForActiveTab().trimmingCharacters(in: .whitespacesAndNewlines)
        if !urlString.isEmpty {
            return urlString
        }
        return "Archived tabs"
    }

    private func currentArchiveSurfaceURL() -> String {
        currentUrlForActiveTab().trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func currentArchivedTabCount() -> Int {
        guard let activeSpaceId = sidebarController.activeSpaceId ?? MahoBridge.shared.getCanonicalActiveSpaceId() else {
            return 0
        }
        return MahoBridge.shared.getArchivedTabs(spaceId: activeSpaceId).count
    }

    func goBackInActiveTab() {
        guard let tabId = webViewManager.activeTabId else { return }
        webViewManager.goBack(tabId: tabId)
    }

    func goForwardInActiveTab() {
        guard let tabId = webViewManager.activeTabId else { return }
        webViewManager.goForward(tabId: tabId)
    }

    func reloadOrStopActiveTab() {
        guard let tabId = webViewManager.activeTabId else { return }
        let webView = webViewManager.webViews[tabId]
        if webView?.isLoading == true {
            webViewManager.stop(tabId: tabId)
        } else {
            webViewManager.reload(tabId: tabId)
        }
    }

    private func configureWindowChrome(for window: NSWindow) {
        if #available(macOS 11.0, *) {
            window.titlebarSeparatorStyle = .none
        }

        titlebarChromeView.material = .hudWindow
        titlebarChromeView.blendingMode = .behindWindow
        titlebarChromeView.state = .active
        titlebarChromeView.translatesAutoresizingMaskIntoConstraints = false
        titlebarChromeView.wantsLayer = true
        titlebarChromeView.layer?.backgroundColor = MahoAppColors.titlebarGlass.cgColor
        titlebarChromeView.layer?.borderColor = MahoAppColors.titlebarEdge.cgColor
        titlebarChromeView.layer?.borderWidth = 1
        updateTitlebarChromeTint()

        if let contentView = window.contentView {
            contentView.addSubview(titlebarChromeView, positioned: .below, relativeTo: nil)
            NSLayoutConstraint.activate([
                titlebarChromeView.topAnchor.constraint(equalTo: contentView.topAnchor),
                titlebarChromeView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
                titlebarChromeView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
                titlebarChromeView.heightAnchor.constraint(equalToConstant: ChromeGeometry.chromeHeight),
            ])
        }
    }

    override func showWindow(_ sender: Any?) {
        appendFrameSnapshot("showWindow: start")
        super.showWindow(sender)
        ensureMainWindowFrameIsValid()
        appendFrameSnapshot("showWindow: after ensureMainWindowFrameIsValid")
        if let sidebarContainer = sidebarContainer {
            sidebarContainer.isHidden = false
            sidebarController.view.isHidden = false
            sidebarController.outlineView.setAccessibilityElement(true)
            expandedSidebarWidth = max(expandedSidebarWidth, BrowserWindowController.defaultSidebarWidth)
            splitView.adjustSubviews()
            splitView.setPosition(expandedSidebarWidth, ofDividerAt: 0)
            splitView.layoutSubtreeIfNeeded()
        }
        let setup = { [weak self] in
            guard let self = self else { return }
            self.layoutTrafficLights()
            if let activeProfileId = MahoBridge.shared.getActiveProfileId() {
                self.webViewManager.setProfile(activeProfileId)
            }
            self.restoreInitialActiveTabIfNeeded()
        }
        setup()
        appendFrameSnapshot("showWindow: end")
    }

    func ensureMainWindowFrameIsValid() -> Bool {
        guard let window = window else { return false }
        let frame = window.frame
        let isValid = frame.width >= Self.mainWindowMinimumSize.width && frame.height >= Self.mainWindowMinimumSize.height
        guard !isValid else { return true }
        window.setFrame(Self.mainWindowDefaultFrame, display: true)
        window.center()
        return false
    }

    private func restoreInitialActiveTabIfNeeded() {
        guard webViewManager.activeTabId == nil else { return }
        guard let activeSpaceId = MahoBridge.shared.getCanonicalActiveSpaceId() else {
            setEmptyStateVisible(true)
            toolbarController.updateActiveTab(nil)
            toolbarController.updateNavigationState(
                url: "",
                title: "",
                canGoBack: false,
                canGoForward: false,
                isLoading: false
            )
            return
        }
        switchToSpaceTab(spaceId: activeSpaceId, preferredTabId: nil)
        previousActiveSpaceId = activeSpaceId
        if let spaces = Optional(MahoBridge.shared.getCanonicalSpaceViewModels()),
           let space = spaces.first(where: { $0.id == activeSpaceId }) {
            SpaceThemeManager.shared.setActiveSpace(color: space.color, theme: space.theme)
        }
    }

    private func switchToSpaceTab(spaceId: String, preferredTabId: TabId?) {
        if let previousSpaceId = previousActiveSpaceId,
           previousSpaceId != spaceId {
            let spaces = MahoBridge.shared.getCanonicalSpaceViewModels()
            let oldIndex = spaces.firstIndex(where: { $0.id == previousSpaceId })
            let newIndex = spaces.firstIndex(where: { $0.id == spaceId })

            if let oldIndex, let newIndex {
                webContentContainerView.wantsLayer = true

                let transition = CATransition()
                transition.type = .push
                transition.subtype = newIndex > oldIndex ? .fromRight : .fromLeft
                transition.duration = 0.25
                transition.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                webContentContainerView.layer?.add(transition, forKey: "spaceSwitchSlide")
            }
        }

        let tabs = MahoBridge.shared.getSpaceTabs(spaceId: spaceId)
        let fallbackTabs = MahoBridge.shared.getTabViewModels().filter { $0.spaceId == spaceId }
        let allTabs = tabs.isEmpty ? fallbackTabs : tabs
        let displayableStates: Set<String> = ["active", "frozen"]
        let candidateTabs = allTabs.filter { displayableStates.contains($0.lifecycleState) }

        let candidateTab: TabViewModel?
        if let preferred = preferredTabId,
           let match = candidateTabs.first(where: { $0.id == preferred }) {
            candidateTab = match
        } else {
            candidateTab = candidateTabs.max { lhs, rhs in
                lhs.lastActiveAt.value < rhs.lastActiveAt.value
            }
        }

        if let preferred = preferredTabId, candidateTab?.id != preferred {
            let reason = allTabs.contains(where: { $0.id == preferred }) ? "excluded by lifecycle filter" : "missing from space"
            NSLog("[Maho] switchToSpaceTab: preferredTabId %@ %@ (%@ displayable candidates for space %@), falling back to %@",
                  preferred, reason, "\(candidateTabs.count)" as NSString, spaceId, candidateTab?.id ?? "nil")
        }

        guard let tab = candidateTab else {
            webViewManager.clearActiveWebView()
            setEmptyStateVisible(true)
            toolbarController.updateActiveTab(nil)
            toolbarController.updateNavigationState(
                url: "",
                title: "",
                canGoBack: false,
                canGoForward: false,
                isLoading: false
            )
            aiSidePanel.refreshContextSummary()
            return
        }

        if webViewManager.activeTabId == tab.id {
            return
        }

        setEmptyStateVisible(false)
        let webView = webViewManager.createWebView(for: tab.id, url: tab.url)
        _ = webView
        webViewManager.activateWebView(for: tab.id, in: webContentContainerView)
        toolbarController.updateActiveTab(tab.id)
        toolbarController.updateNavigationState(
            url: tab.url,
            title: tab.title,
            canGoBack: false,
            canGoForward: false,
            isLoading: tab.isLoading
        )
        aiSidePanel.refreshContextSummary()
    }

    private func layoutTrafficLights() {
        guard let window = window,
              let closeButton = window.standardWindowButton(.closeButton),
              let miniButton = window.standardWindowButton(.miniaturizeButton),
              let zoomButton = window.standardWindowButton(.zoomButton),
              let container = closeButton.superview,
              sidebarController.view.window != nil
        else {
            return
        }

        let buttonSize = closeButton.frame.size
        let y = floor((container.bounds.height - buttonSize.height) / 2) + ChromeGeometry.trafficLightsVerticalOffset
        let trafficLightLeadingInset = ChromeGeometry.trafficLightLeadingInset
        let trafficLightGap = ChromeGeometry.trafficLightGap
        closeButton.setFrameOrigin(NSPoint(x: trafficLightLeadingInset, y: y))
        miniButton.setFrameOrigin(NSPoint(x: trafficLightLeadingInset + buttonSize.width + trafficLightGap, y: y))
        zoomButton.setFrameOrigin(NSPoint(x: trafficLightLeadingInset + (buttonSize.width + trafficLightGap) * 2, y: y))

        [closeButton, miniButton, zoomButton].forEach {
            $0.alphaValue = 0.98
        }

        let trafficLightsFrameInContainer = closeButton.frame
            .union(miniButton.frame)
            .union(zoomButton.frame)
        let trafficLightsFrameInWindow = container.convert(trafficLightsFrameInContainer, to: nil)
        let trafficLightsFrameInSidebar = sidebarController.view.convert(trafficLightsFrameInWindow, from: nil)
        sidebarController.alignNavigationRow(toTrafficLights: trafficLightsFrameInSidebar)
    }

    private func configuredSymbol(named symbolName: String, pointSize: CGFloat, accessibilityDescription: String?) -> NSImage? {
        let configuration = NSImage.SymbolConfiguration(pointSize: pointSize, weight: .medium)
        return NSImage(systemSymbolName: symbolName, accessibilityDescription: accessibilityDescription)?.withSymbolConfiguration(configuration)
    }

    private func setEmptyStateVisible(_ visible: Bool) {
        if visible {
            findBarView.isHidden = true
        }
    }

    // MARK: - CoreUpdateObserver

    func handleCoreUpdate(_ update: CoreUpdate) {
        dispatchPrecondition(condition: .onQueue(.main))
        let wasFocused = self === WindowManager.shared.focusedWindow
        DispatchQueue.main.async { [weak self] in
            self?.processCoreUpdate(update, wasFocusedAtDispatch: wasFocused)
        }
    }

    private func processCoreUpdate(_ update: CoreUpdate, wasFocusedAtDispatch: Bool) {
        switch update {
        case .tabCreated(let tab):
            // Only focused window claims and displays the new tab
            if wasFocusedAtDispatch {
                setEmptyStateVisible(false)
                let webView = webViewManager.createWebView(for: tab.id, url: tab.url)
                _ = webView
                webViewManager.activateWebView(for: tab.id, in: webContentContainerView)
                toolbarController.updateActiveTab(tab.id)
                toolbarController.updateNavigationState(
                    url: tab.url,
                    title: tab.title,
                    canGoBack: false,
                    canGoForward: false,
                    isLoading: tab.isLoading
                )
                aiSidePanel.refreshContextSummary()
            }
        case .tabClosed(let tabId, _):
            // Only displayer handles WebView cleanup
            guard WebViewRegistry.shared.currentDisplayer(of: tabId) === self || webViewManager.activeTabId == tabId else { break }
            webViewManager.removeWebView(for: tabId)
            if webViewManager.activeTabId == nil {
                for subview in webContentContainerView.subviews where subview !== findBarView {
                    subview.removeFromSuperview()
                }
                setEmptyStateVisible(true)
                toolbarController.updateActiveTab(nil)
                toolbarController.updateNavigationState(
                    url: "",
                    title: "",
                    canGoBack: false,
                    canGoForward: false,
                    isLoading: false
                )
                aiSidePanel.refreshContextSummary()
            }

        case .navigateTab(let tabId, let url):
            // Only the displayer should trigger navigation
            guard WebViewRegistry.shared.currentDisplayer(of: tabId) === self else { break }
            webViewManager.loadURL(url.value, in: tabId)

        case .zoomChanged(let tabId, let zoomLevel):
            guard WebViewRegistry.shared.currentDisplayer(of: tabId) === self else { break }
            webViewManager.setZoom(zoomLevel, for: tabId)

        case .printRequested(let tabId):
            guard WebViewRegistry.shared.currentDisplayer(of: tabId) === self else { break }
            if let win = window {
                webViewManager.printWebView(tabId: tabId, in: win)
            }

        case .pipToggled(let tabId, let active):
            guard WebViewRegistry.shared.currentDisplayer(of: tabId) === self else { break }
            if active {
                let title = webViewManager.webViews[tabId]?.title ?? "Media"
                miniPlayerWindowController = MiniPlayerWindowController()
                miniPlayerWindowController?.configure(tabId: tabId, title: title, isPlaying: true)
            } else {
                miniPlayerWindowController?.dismiss()
                miniPlayerWindowController = nil
            }

        case .activeSpaceChanged(let spaceId, let activeTabId):
            guard wasFocusedAtDispatch else { break }
            switchToSpaceTab(spaceId: spaceId, preferredTabId: activeTabId)
            previousActiveSpaceId = spaceId
            let spaces = MahoBridge.shared.getCanonicalSpaceViewModels()
            if let space = spaces.first(where: { $0.id == spaceId }) {
                SpaceThemeManager.shared.setActiveSpace(color: space.color, theme: space.theme)
            }

        case .contentRulesCompiled:
            break

        case .settingsChanged(let settings):
            applyAppearanceSettings(settings.appearance)

        case .navigationStateChanged(let tabId, let url, let title, let canGoBack, let canGoForward, let isLoading, _, _):
            if tabId == webViewManager.activeTabId {
                toolbarController.updateNavigationState(
                    url: url.value,
                    title: title,
                    canGoBack: canGoBack,
                    canGoForward: canGoForward,
                    isLoading: isLoading
                )
                aiSidePanel.refreshContextSummary()
                // After updating toolbar, update Handoff activity
                updateBrowsingActivity(url: url.value, title: title)
            }

        case .tabUpdated(let tabId, let changes):
            if tabId == webViewManager.activeTabId {
                toolbarController.updateNavigationState(
                    url: changes.url ?? currentUrlForActiveTab(),
                    title: changes.title ?? currentTitleForActiveTab(),
                    canGoBack: self.canGoBackForActiveTab(),
                    canGoForward: self.canGoForwardForActiveTab(),
                    isLoading: changes.isLoading ?? false
                )
                aiSidePanel.refreshContextSummary()
            }

        case .tabFaviconChanged:
            aiSidePanel.refreshContextSummary()

        case .showFindBar(let tabId):
            guard WebViewRegistry.shared.currentDisplayer(of: tabId) === self || webViewManager.activeTabId == tabId else { break }
            findBarView.isHidden = false

        case .showNotification(let notification):
            guard wasFocusedAtDispatch else { break }
            showNotificationBanner(notification)

        case .showPermissionRequest(let request):
            showPermissionSheet(request)

        case .notificationDismissed:
            notificationBanner?.removeFromSuperview()
            notificationBanner = nil

        case .allNotificationsDismissed:
            notificationBanner?.removeFromSuperview()
            notificationBanner = nil

        case .openPeekTab(let url, let sourceTabId):
            peekController = PeekWindowController(url: url.value, sourceTabId: sourceTabId)
            peekController?.showWindow(nil)

        case .downloadRenamed:
            break

        case .tabsMigrated(let tabIds, _, _):
            if let activeTabId = webViewManager.activeTabId, tabIds.contains(activeTabId) {
                toolbarController.updateActiveTab(activeTabId)
            }

        case .fullState:
            if webViewManager.activeTabId == nil {
                restoreInitialActiveTabIfNeeded()
            }

        case .activeProfileChanged(let profileId):
            guard wasFocusedAtDispatch else { break }
            webViewManager.setProfile(profileId)
            for tabId in Array(webViewManager.webViews.keys) {
                webViewManager.removeWebView(for: tabId)
            }
            restoreInitialActiveTabIfNeeded()

        case .profileDeleted(_, let dataStoreId):
            if let dsId = dataStoreId {
                ProfileDataStoreManager.shared.removeDataStore(for: dsId)
            }

        case .tabLifecycleChanged(let tabId, _):
            if tabId == webViewManager.activeTabId {
                toolbarController.updateActiveTab(tabId)
            }

        case .error(let context, let error):
            guard wasFocusedAtDispatch else { break }
            NSLog("[Maho] Error in \(context): \(error.code) – \(error.message)")
            let alert = NSAlert()
            alert.messageText = error.message
            alert.informativeText = "Error code: \(error.code)"
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            if let window = self.window {
                alert.beginSheetModal(for: window, completionHandler: nil)
            }

        default:
            break
        }
    }

    private func showNotificationBanner(_ notification: NotificationViewModel) {
        notificationBanner?.removeFromSuperview()
        let banner = NotificationBannerView(frame: .zero)
        banner.translatesAutoresizingMaskIntoConstraints = false
        banner.configure(notification: notification)
        contentView.addSubview(banner)
        NSLayoutConstraint.activate([
            banner.topAnchor.constraint(equalTo: contentView.topAnchor, constant: 12),
            banner.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -12),
        ])
        notificationBanner = banner
    }

    private func showPermissionSheet(_ request: PermissionRequest) {
        guard let window = window else { return }
        let permController = PermissionSheetController()
        _ = permController.view
        permController.configure(request: request)
        let sheetWindow = NSPanel(contentViewController: permController)
        sheetWindow.styleMask = [.titled, .closable]
        sheetWindow.title = "Permission Request"
        window.beginSheet(sheetWindow) { _ in }
    }

    private func applyAppearanceSettings(_ appearance: AppearanceSettings) {
        switch appearance.theme {
        case .dark:
            window?.appearance = NSAppearance(named: .darkAqua)
        case .light:
            window?.appearance = NSAppearance(named: .aqua)
        case .system:
            window?.appearance = nil
        }

        // Apply sidebar width from prefs
        let prefWidth = CGFloat(appearance.sidebarWidth)
        if prefWidth >= BrowserWindowController.minSidebarWidth &&
           prefWidth <= BrowserWindowController.maxSidebarWidth {
            expandedSidebarWidth = prefWidth
            if let container = sidebarContainer, !container.isHidden {
                splitView.setPosition(prefWidth, ofDividerAt: 0)
            }
        }

        // Apply sidebar collapsed state from prefs
        if let container = sidebarContainer {
            let shouldCollapse = appearance.sidebarCollapsed
            let isCurrentlyCollapsed = container.isHidden
            if shouldCollapse != isCurrentlyCollapsed {
                toggleSidebar()
            }
        }

        refreshWindowAppearance()
    }

    @objc private func activeSpaceThemeDidChange() {
        updateTitlebarChromeTint()
    }

    private func updateTitlebarChromeTint() {
        guard let tint = SpaceThemeManager.shared.chromeTint(alpha: 0.06) else {
            titlebarChromeView.layer?.backgroundColor = MahoAppColors.titlebarGlass.cgColor
            return
        }
        let blended = MahoAppColors.titlebarGlass.blended(withFraction: 0.15, of: tint) ?? MahoAppColors.titlebarGlass
        titlebarChromeView.layer?.backgroundColor = blended.cgColor
    }

    private func refreshWindowAppearance() {
        guard let window else { return }
        let effectiveAppearance = window.effectiveAppearance
        window.backgroundColor = MahoAppColors.windowBackground
        titlebarChromeView.material = isDarkAppearance(window.effectiveAppearance) ? .hudWindow : .headerView
        updateTitlebarChromeTint()
        titlebarChromeView.layer?.borderColor = cgColor(MahoAppColors.titlebarEdge, using: effectiveAppearance)
        contentView.refreshAppearance()
        archiveDetailView.refreshAppearance()
        sidebarController.refreshAppearance()
        toolbarController.refreshAppearance()
    }

    private func isDarkAppearance(_ appearance: NSAppearance) -> Bool {
        appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
    }

    private func cgColor(_ color: NSColor, using appearance: NSAppearance) -> CGColor {
        var resolved = color.cgColor
        appearance.performAsCurrentDrawingAppearance {
            resolved = color.cgColor
        }
        return resolved
    }

    private func currentUrlForActiveTab() -> String {
        guard let tabId = webViewManager.activeTabId,
              let webView = webViewManager.webViews[tabId] else { return "" }
        return webView.url?.absoluteString ?? ""
    }

    private func currentTitleForActiveTab() -> String {
        guard let tabId = webViewManager.activeTabId,
              let webView = webViewManager.webViews[tabId] else { return "" }
        return webView.title ?? ""
    }

    private func updateBrowsingActivity(url: String?, title: String?) {
        guard let urlString = url,
              let actualUrl = URL(string: urlString),
              let scheme = actualUrl.scheme?.lowercased(),
              scheme == "http" || scheme == "https" else {
            browsingActivity?.invalidate()
            browsingActivity = nil
            return
        }
        let activity = NSUserActivity(activityType: "dev.maho.browsing")
        activity.webpageURL = actualUrl
        activity.title = title ?? urlString
        activity.isEligibleForHandoff = true
        activity.becomeCurrent()
        browsingActivity = activity
    }

    private var siteControlPopover: SiteControlPopover?
    private var sidebarContainer: NSView?

    func toggleSidebar() {
        guard let container = sidebarContainer else { return }
        let hiding = !container.isHidden
        if hiding {
            expandedSidebarWidth = max(container.frame.width, BrowserWindowController.minSidebarWidth)
            splitView.setPosition(0, ofDividerAt: 0)
            container.isHidden = true
            sidebarController.view.isHidden = true
            sidebarController.outlineView.setAccessibilityElement(false)
            splitView.layoutSubtreeIfNeeded()
            layoutTrafficLights()
            CoreUpdateDispatcher.shared.sendAndDispatch(.sidebarToggled(visible: false))
        } else {
            sidebarController.outlineView.setAccessibilityElement(true)
            container.isHidden = false
            sidebarController.view.isHidden = false
            splitView.adjustSubviews()
            if !isArchivePresentationActive {
                splitView.setPosition(expandedSidebarWidth, ofDividerAt: 0)
            }
            splitView.layoutSubtreeIfNeeded()
            layoutTrafficLights()
            CoreUpdateDispatcher.shared.sendAndDispatch(.sidebarToggled(visible: true))
        }
        persistSidebarState(collapsed: hiding)
    }

    private func persistSidebarState(collapsed: Bool) {
        let update = SettingsUpdate(
            appearance: AppearanceSettingsUpdate(
                sidebarWidth: Double(expandedSidebarWidth),
                sidebarCollapsed: collapsed
            )
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    func focusURLBar() {
        toolbarController.focusURLField()
    }

    func webViewWasReclaimed(tabId: TabId) {
        if activeTabId == tabId {
            webViewManager.clearActiveWebView()
            setEmptyStateVisible(true)
            toolbarController.updateActiveTab(nil)
        }
    }

    func toggleToolbarVisible() {
        guard let toolbar = window?.toolbar else { return }
        toolbar.isVisible.toggle()
    }

    func canGoBackForActiveTab() -> Bool {
        guard let tabId = webViewManager.activeTabId,
              let webView = webViewManager.webViews[tabId] else { return false }
        return webView.canGoBack
    }

    func canGoForwardForActiveTab() -> Bool {
        guard let tabId = webViewManager.activeTabId,
              let webView = webViewManager.webViews[tabId] else { return false }
        return webView.canGoForward
    }

    

    func showSiteControlPopover(relativeTo view: NSView) {
        let popover = SiteControlPopover()
        let domain: String
        if let tabId = webViewManager.activeTabId,
           let webView = webViewManager.webViews[tabId],
           let host = webView.url?.host {
            domain = host
        } else {
            domain = ""
        }
        popover.configure(domain: domain, tabId: webViewManager.activeTabId)
        popover.show(relativeTo: view.bounds, of: view, preferredEdge: .minY)
        siteControlPopover = popover
    }

    private static let aiPanelPreferredWidth: CGFloat = 428
    private static let aiPanelMaximumWidth: CGFloat = 640
    private static let aiPanelMinimumVisibleWidth: CGFloat = 220

    @objc func toggleAIPanel() {
        setAIPanelVisible(!isAIPanelVisible)
    }

    func showAIPanel() {
        setAIPanelVisible(true)
    }

    func showAIPanelWithQuery(_ query: String) {
        showAIPanel()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
            self?.aiSidePanel.submitQuery(query)
        }
    }

    func captureVisibleAIPanelPNG(to path: String) -> Bool {
        guard isAIPanelVisible,
              floatingAIPanel == nil,
              !aiSidePanel.bounds.isEmpty,
              let bitmap = aiSidePanel.bitmapImageRepForCachingDisplay(in: aiSidePanel.bounds) else {
            return false
        }

        aiSidePanel.layoutSubtreeIfNeeded()
        aiSidePanel.cacheDisplay(in: aiSidePanel.bounds, to: bitmap)
        guard let data = bitmap.representation(using: .png, properties: [:]) else {
            return false
        }

        let url = URL(fileURLWithPath: path)
        do {
            try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
            try data.write(to: url, options: .atomic)
            return true
        } catch {
            AppDelegate.appendUITestDebugLog("captureVisibleAIPanelPNG failed path=\(path) error=\(error.localizedDescription)")
            return false
        }
    }

    private func setAIPanelVisible(_ visible: Bool) {
        guard let container = aiPanelContainer,
              let window = window else { return }
        isAIPanelVisible = visible
        if ProcessInfo.processInfo.arguments.contains("--ui-testing") {
            AppDelegate.writeUITestState(["aiPanelVisible": visible ? "1" : "0"])
        }
        if visible {
            aiSidePanel.setDisplayMode(.sidebar)
        }

        if visible {
            if floatingAIPanel != nil {
                reattachAIPanelToSidebar()
            }

            let availableWidth = currentAIPanelLayoutWidth(in: window)
            let panelWidth = resolvedAIPanelWidth(for: availableWidth)
            let dividerPosition = availableWidth - panelWidth

            aiPanelWidthConstraint?.constant = panelWidth
            aiPanelWidthConstraint?.isActive = true
            container.isHidden = false
            container.alphaValue = 0
            splitView.adjustSubviews()
            splitView.layoutSubtreeIfNeeded()
            window.contentView?.layoutSubtreeIfNeeded()

            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.25
                context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                container.animator().alphaValue = 1
                splitView.animator().setPosition(dividerPosition, ofDividerAt: 1)
            }
        } else {
            let availableWidth = currentAIPanelLayoutWidth(in: window)

            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.2
                context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                container.animator().alphaValue = 0
                splitView.animator().setPosition(availableWidth, ofDividerAt: 1)
            } completionHandler: { [weak self] in
                container.isHidden = true
                container.alphaValue = 1
                self?.aiPanelWidthConstraint?.isActive = false
            }
        }
    }

    private func updateAIPanelLayout(for windowWidth: CGFloat) {
        guard isAIPanelVisible else { return }

        let panelWidth = resolvedAIPanelWidth(for: windowWidth)
        aiPanelWidthConstraint?.constant = panelWidth
        splitView.adjustSubviews()
        splitView.setPosition(windowWidth - panelWidth, ofDividerAt: 1)
        splitView.layoutSubtreeIfNeeded()
    }

    private func reattachAIPanelToSidebar() {
        guard let container = aiPanelContainer else { return }
        guard aiSidePanel.superview !== container else { return }

        floatingAIPanel?.orderOut(nil)
        floatingAIPanel?.contentView = nil
        floatingAIPanel = nil

        aiSidePanel.removeFromSuperview()
        aiSidePanel.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(aiSidePanel)
        NSLayoutConstraint.activate(aiPanelConstraints)
    }

    private func closeFloatingAIPanel() {
        reattachAIPanelToSidebar()
        floatingAIPanel?.orderOut(nil)
        floatingAIPanel?.close()
        floatingAIPanel = nil
        aiSidePanel.setDisplayMode(.sidebar)
        setAIPanelVisible(false)
    }

    private func currentAIPanelLayoutWidth(in window: NSWindow) -> CGFloat {
        let splitWidth = splitView.bounds.width
        if splitWidth > 0 {
            return splitWidth
        }

        let contentLayoutWidth = window.contentLayoutRect.width
        if contentLayoutWidth > 0 {
            return contentLayoutWidth
        }

        if let contentWidth = window.contentView?.bounds.width, contentWidth > 0 {
            return contentWidth
        }

        return window.frame.width
    }

    private func resolvedAIPanelWidth(for windowWidth: CGFloat) -> CGFloat {
        let dividerAllowance = splitView.dividerThickness * 2
        let preferredPanelWidth = min(
            BrowserWindowController.aiPanelMaximumWidth,
            max(BrowserWindowController.aiPanelMinimumVisibleWidth, BrowserWindowController.aiPanelPreferredWidth)
        )
        let maxPanelWidth = max(
            BrowserWindowController.aiPanelMinimumVisibleWidth,
            windowWidth - BrowserWindowController.minContentWidth - dividerAllowance
        )

        return min(preferredPanelWidth, maxPanelWidth)
    }

    @objc private func handleSkipBack(_ notification: Notification) {
        guard let tabId = notification.userInfo?["tabId"] as? TabId,
              let webView = webViewManager.webViews[tabId] else { return }
        webView.evaluateJavaScript(MediaSessionBridge.skipBackScript, completionHandler: nil)
    }

    @objc private func handleSkipForward(_ notification: Notification) {
        guard let tabId = notification.userInfo?["tabId"] as? TabId,
              let webView = webViewManager.webViews[tabId] else { return }
        webView.evaluateJavaScript(MediaSessionBridge.skipForwardScript, completionHandler: nil)
    }

    @objc private func handlePlayPause(_ notification: Notification) {
        guard let tabId = notification.userInfo?["tabId"] as? TabId,
              let webView = webViewManager.webViews[tabId] else { return }
        webView.evaluateJavaScript(MediaSessionBridge.playPauseScript, completionHandler: nil)
    }

    @objc private func handleATCLinkRule(_ notification: Notification) {
        guard let userInfo = notification.userInfo,
              let url = userInfo["url"] as? URL,
              let rule = userInfo["rule"] as? ATCLinkRule else { return }

        if let spaceId = rule.targetSpaceId, !spaceId.isEmpty {
            MahoBridge.shared.activateSpace(spaceId: spaceId)
        }
        if let profileId = rule.targetProfileId, !profileId.isEmpty {
            _ = MahoBridge.shared.switchProfile(profileId: profileId)
        }
        if rule.openInNewTab {
            let activeSpaceId = rule.targetSpaceId ?? MahoBridge.shared.getCanonicalActiveSpaceId() ?? ""
            CoreUpdateDispatcher.shared.sendAndDispatch(
                .createTab(spaceId: activeSpaceId, url: MahoUrl(url.absoluteString), parentId: nil)
            )
        }
    }

    func activeWebView() -> WKWebView? {
        guard let tabId = webViewManager.activeTabId else { return nil }
        return webViewManager.webViews[tabId]
    }

    func showCreateThemeSidebarPlaceholder(for space: SpaceViewModel) {
        sidebarController.showCreateThemePlaceholder(for: space)
    }

    func openThemeColorEditorSheet(for space: SpaceViewModel) {
        SpaceContextMenu.presentThemeColorEditor(for: space, from: window)
    }

    func activateTabLocally(_ tabId: TabId) {
        let tabs = MahoBridge.shared.getTabViewModels()
        guard let tab = tabs.first(where: { $0.id == tabId }) else { return }

        let webView = webViewManager.createWebView(for: tabId, url: tab.url)
        _ = webView

        setEmptyStateVisible(false)
        webViewManager.activateWebView(for: tabId, in: webContentContainerView)
        toolbarController.updateActiveTab(tabId)

        let wv = WebViewRegistry.shared.webView(for: tabId)
        toolbarController.updateNavigationState(
            url: wv?.url?.absoluteString ?? tab.url,
            title: wv?.title ?? tab.title,
            canGoBack: wv?.canGoBack ?? false,
            canGoForward: wv?.canGoForward ?? false,
            isLoading: wv?.isLoading ?? false
        )
        aiSidePanel.refreshContextSummary()
    }
}

extension BrowserWindowController: AISidePanelDelegate {
    func aiSidePanelDidRequestClose() {
        if floatingAIPanel != nil {
            closeFloatingAIPanel()
        } else {
            toggleAIPanel()
        }
    }

    func aiSidePanelDidRequestTidyTabs(from sourceView: NSView) {
        sidebarController.requestTidyTabs(from: sourceView)
    }

    func aiSidePanelCanRequestTidyTabs() -> Bool {
        return sidebarController.canRequestTidyTabs()
    }

    func aiSidePanelAllTabs() -> [TabReference] {
        let tabViewModels = MahoBridge.shared.getTabViewModels()
        var refs: [TabReference] = []
        var activeRef: TabReference?

        for tab in tabViewModels {
            guard let webView = webViewManager.webViews[tab.id] else { continue }
            let reference = TabReference(
                id: tab.id,
                title: tab.title,
                url: tab.url,
                favicon: tab.favicon,
                webView: webView
            )

            if tab.id == webViewManager.activeTabId {
                activeRef = reference
            } else {
                refs.append(reference)
            }
        }

        if let activeRef {
            refs.insert(activeRef, at: 0)
        }

        return refs
    }

    func aiSidePanelToolRegistry() -> MCPToolRegistry? {
        return MCPServer.shared.toolRegistry
    }

    func aiSidePanelCaptureActiveTab(completion: @escaping (NSImage?) -> Void) {
        guard let tabId = webViewManager.activeTabId,
              let webView = webViewManager.webViews[tabId] else {
            completion(nil)
            return
        }
        TabPreviewManager.shared.capturePreview(tabId: tabId, webView: webView, completion: completion)
    }

    func aiSidePanelDidRequestDetach() {
        guard window != nil else { return }

        if let floatingAIPanel {
            floatingAIPanel.makeKeyAndOrderFront(nil)
            return
        }

        guard let container = aiPanelContainer else { return }
        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: BrowserWindowController.aiPanelPreferredWidth, height: 600),
            styleMask: [.titled, .closable, .resizable, .utilityWindow],
            backing: .buffered,
            defer: false
        )
        panel.title = "Maho AI"
        panel.isFloatingPanel = true
        panel.isReleasedWhenClosed = false
        panel.center()

        let hostingView = NSView(frame: panel.contentView?.bounds ?? panel.frame)
        hostingView.autoresizingMask = [.width, .height]
        hostingView.wantsLayer = true
        panel.contentView = hostingView

        aiSidePanel.removeFromSuperview()
        aiSidePanel.translatesAutoresizingMaskIntoConstraints = false
        hostingView.addSubview(aiSidePanel)
        NSLayoutConstraint.activate([
            aiSidePanel.topAnchor.constraint(equalTo: hostingView.topAnchor),
            aiSidePanel.leadingAnchor.constraint(equalTo: hostingView.leadingAnchor),
            aiSidePanel.trailingAnchor.constraint(equalTo: hostingView.trailingAnchor),
            aiSidePanel.bottomAnchor.constraint(equalTo: hostingView.bottomAnchor),
        ])

        container.isHidden = true
        aiPanelWidthConstraint?.isActive = false
        aiSidePanel.setDisplayMode(.floating)
        floatingAIPanel = panel
        panel.delegate = self
        panel.orderFront(nil)
    }

    func aiSidePanelDidRequestSidebarMode() {
        setAIPanelVisible(true)
    }

    func aiSidePanelDidRequestOpenInNewTab() {
        guard let spaceId = MahoBridge.shared.getCanonicalActiveSpaceId() else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .createTab(spaceId: spaceId, url: MahoUrl("maho://ai"), parentId: nil)
        )
        toggleAIPanel()
    }
}

// MARK: - MCPToolRegistryDelegate

extension BrowserWindowController: MCPToolRegistryDelegate {
    func mcpActiveWebView() -> WKWebView? {
        return activeWebView()
    }

    func mcpWebView(forTabId tabId: String) -> WKWebView? {
        return webViewManager.webViews[tabId]
    }

    func mcpNavigate(to url: String) {
        guard let tabId = activeTabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.navigateTo(tabId: tabId, url: MahoUrl(url)))
    }

    func mcpCloseTab(id: String) {
        CoreUpdateDispatcher.shared.sendAndDispatch(.closeTab(tabId: id))
    }

    func mcpCreateTab(url: String) {
        guard let spaceId = MahoBridge.shared.getCanonicalActiveSpaceId() else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.createTab(spaceId: spaceId, url: MahoUrl(url), parentId: nil))
    }
}

// MARK: - NSWindowDelegate

extension BrowserWindowController: NSWindowDelegate {
    func windowWillResize(_ sender: NSWindow, to frameSize: NSSize) -> NSSize {
        NSSize(
            width: max(frameSize.width, Self.mainWindowMinimumSize.width),
            height: max(frameSize.height, Self.mainWindowMinimumSize.height)
        )
    }

    func windowDidBecomeKey(_ notification: Notification) {
        WindowManager.shared.setFocused(self)
    }

    func windowDidResize(_ notification: Notification) {
        guard let window = window else { return }
        if !didLogInitialWindowDidResize {
            didLogInitialWindowDidResize = true
            appendFrameSnapshot("windowDidResize: first callback")
            NSLog("MAHO_FRAME_DEBUG: windowDidResize callstack\n%@", Thread.callStackSymbols.joined(separator: "\n"))
        }
        layoutTrafficLights()
        updateAIPanelLayout(for: currentAIPanelLayoutWidth(in: window))
    }

    func windowWillClose(_ notification: Notification) {
        if let panel = floatingAIPanel, notification.object as? NSWindow === panel {
            reattachAIPanelToSidebar()
            floatingAIPanel = nil
            aiSidePanel.setDisplayMode(.sidebar)
            isAIPanelVisible = false
            aiPanelWidthConstraint?.isActive = false
            aiPanelContainer?.isHidden = true
            aiPanelContainer?.alphaValue = 1
            return
        }

        for tabId in WebViewRegistry.shared.webViews.keys {
            if WebViewRegistry.shared.currentDisplayer(of: tabId) === self {
                WebViewRegistry.shared.relinquish(tabId: tabId)
            }
        }
        WindowManager.shared.removeWindow(self)
    }
}

// MARK: - NSSplitViewDelegate

extension BrowserWindowController: NSSplitViewDelegate {

    func splitView(_ splitView: NSSplitView, constrainMinCoordinate proposedMinimumPosition: CGFloat, ofSubviewAt dividerIndex: Int) -> CGFloat {
        if dividerIndex == 0 {
            return BrowserWindowController.minSidebarWidth
        }
        return proposedMinimumPosition
    }

    func splitView(_ splitView: NSSplitView, constrainMaxCoordinate proposedMaximumPosition: CGFloat, ofSubviewAt dividerIndex: Int) -> CGFloat {
        if dividerIndex == 0 {
            return BrowserWindowController.maxSidebarWidth
        }
        return proposedMaximumPosition
    }

    func splitView(_ splitView: NSSplitView, canCollapseSubview subview: NSView) -> Bool {
        return subview === sidebarContainer || subview === aiPanelContainer
    }

    func splitView(_ splitView: NSSplitView, shouldAdjustSizeOfSubview view: NSView) -> Bool {
        return view === contentColumnView
    }

    func splitViewDidResizeSubviews(_ notification: Notification) {
        guard let container = sidebarContainer, !container.isHidden else { return }
        let width = container.frame.width
        if width >= BrowserWindowController.minSidebarWidth && width != expandedSidebarWidth {
            expandedSidebarWidth = width
            sidebarWidthPersistTimer?.invalidate()
            sidebarWidthPersistTimer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: false) { [weak self] _ in
                guard let self = self else { return }
                let update = SettingsUpdate(
                    appearance: AppearanceSettingsUpdate(sidebarWidth: Double(self.expandedSidebarWidth))
                )
                CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
            }
        }
    }

    private func appendDebugLog(_ message: String) {
        NSLog("MAHO_FRAME_DEBUG: %@", message)
    }

    private func appendFrameSnapshot(_ label: String) {
        Self.appendFrameSnapshotLog(label, window: window, splitView: splitView)
    }

    private static func appendFrameSnapshotLog(_ label: String, window: NSWindow?, splitView: NSSplitView?) {
        let windowFrame = window.map { NSStringFromRect($0.frame) } ?? "nil"
        let contentViewFrame = window.flatMap { $0.contentView?.frame }.map { NSStringFromRect($0) } ?? "nil"
        let splitViewFrame = splitView.map { NSStringFromRect($0.frame) } ?? "nil"
        NSLog(
            "MAHO_FRAME_DEBUG: %@ window.frame=%@ contentView.frame=%@ splitView.frame=%@",
            label,
            windowFrame,
            contentViewFrame,
            splitViewFrame
        )
    }

    private static func appendToFile(_ path: String, _ text: String) {
        if let handle = FileHandle(forWritingAtPath: path) {
            handle.seekToEndOfFile()
            handle.write(text.data(using: .utf8)!)
            handle.synchronizeFile()
            handle.closeFile()
        } else {
            try? text.write(toFile: path, atomically: false, encoding: .utf8)
        }
    }
}

private final class ArcSplitView: NSSplitView {
    override var dividerThickness: CGFloat { 1 }

    override func drawDivider(in rect: NSRect) {
        MahoAppColors.divider.setFill()
        rect.fill()
    }
}

private final class AppearanceAwareContentViewWrapper: NSView {
    var onEffectiveAppearanceChanged: (() -> Void)?

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        onEffectiveAppearanceChanged?()
    }
}

private final class ArchiveDetailSurfaceView: NSView {
    private enum Metrics {
        static let outerInset: CGFloat = 28
        static let stackSpacing: CGFloat = 18
        static let previewCornerRadius: CGFloat = 24
        static let previewPadding: CGFloat = 28
        static let previewSpacing: CGFloat = 18
        static let glyphSize: CGFloat = 52
        static let chipCornerRadius: CGFloat = 12
        static let chipHorizontalInset: CGFloat = 12
        static let chipVerticalInset: CGFloat = 8
    }

    private let contentStackView = NSStackView()
    private let eyebrowLabel = NSTextField(labelWithString: "Library")
    private let titleLabel = NSTextField(labelWithString: "Archived tabs")
    private let subtitleLabel = NSTextField(labelWithString: "Reference view")
    private let previewCardView = NSView()
    private let previewStackView = NSStackView()
    private let glyphContainerView = NSView()
    private let glyphImageView = NSImageView()
    private let detailTitleLabel = NSTextField(labelWithString: "Archived tabs")
    private let detailURLLabel = NSTextField(labelWithString: "")
    private let countChipView = NSView()
    private let countChipLabel = NSTextField(labelWithString: "0 archived")
    private let helperLabel = NSTextField(labelWithString: "Browsing is paused while library mode is open.")

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    func configure(spaceName: String, tabTitle: String, url: String, archivedCount: Int) {
        eyebrowLabel.stringValue = spaceName.uppercased()
        titleLabel.stringValue = "Library"
        subtitleLabel.stringValue = archivedCount == 1 ? "1 archived tab in \(spaceName)" : "\(archivedCount) archived tabs in \(spaceName)"
        detailTitleLabel.stringValue = tabTitle
        detailURLLabel.stringValue = url.isEmpty ? "This window is showing your archive surface instead of the live page." : url
        countChipLabel.stringValue = archivedCount == 1 ? "1 archived" : "\(archivedCount) archived"
    }

    func refreshAppearance() {
        layer?.backgroundColor = MahoAppColors.archiveDetailPanelBackground.cgColor
        previewCardView.layer?.backgroundColor = MahoAppColors.archiveDetailPanelInnerBackground.cgColor
        previewCardView.layer?.borderColor = MahoAppColors.archiveDetailPanelBorder.cgColor
        glyphContainerView.layer?.backgroundColor = MahoAppColors.archiveDetailPanelAccent.cgColor
        countChipView.layer?.backgroundColor = MahoAppColors.archiveDetailPanelMutedFill.cgColor
        countChipView.layer?.borderColor = MahoAppColors.archiveDetailPanelBorder.withAlphaComponent(0.74).cgColor
        eyebrowLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.72)
        titleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.96)
        subtitleLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.80)
        detailTitleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.92)
        detailURLLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
        helperLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.74)
        countChipLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.86)
        glyphImageView.contentTintColor = MahoAppColors.primaryText.withAlphaComponent(0.88)
    }

    private func setupViews() {
        wantsLayer = true
        translatesAutoresizingMaskIntoConstraints = false
        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        setAccessibilityIdentifier("window.archive.detail")
        setAccessibilityLabel("Archive Detail Surface")

        contentStackView.translatesAutoresizingMaskIntoConstraints = false
        contentStackView.orientation = .vertical
        contentStackView.alignment = .leading
        contentStackView.distribution = .fill
        contentStackView.spacing = Metrics.stackSpacing
        addSubview(contentStackView)

        eyebrowLabel.translatesAutoresizingMaskIntoConstraints = false
        eyebrowLabel.font = NSFont.systemFont(ofSize: 11, weight: .semibold)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = NSFont.systemFont(ofSize: 40, weight: .semibold)
        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = NSFont.systemFont(ofSize: 15, weight: .medium)

        previewCardView.translatesAutoresizingMaskIntoConstraints = false
        previewCardView.wantsLayer = true
        previewCardView.layer?.cornerRadius = Metrics.previewCornerRadius
        previewCardView.layer?.borderWidth = 1
        previewCardView.addSubview(previewStackView)

        previewStackView.translatesAutoresizingMaskIntoConstraints = false
        previewStackView.orientation = .vertical
        previewStackView.alignment = .leading
        previewStackView.distribution = .fill
        previewStackView.spacing = Metrics.previewSpacing

        glyphContainerView.translatesAutoresizingMaskIntoConstraints = false
        glyphContainerView.wantsLayer = true
        glyphContainerView.layer?.cornerRadius = 20
        glyphContainerView.addSubview(glyphImageView)

        glyphImageView.translatesAutoresizingMaskIntoConstraints = false
        glyphImageView.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: Metrics.glyphSize, weight: .regular)
        glyphImageView.image = NSImage(systemSymbolName: "archivebox.fill", accessibilityDescription: "Archive")
        glyphImageView.imageScaling = .scaleProportionallyDown

        detailTitleLabel.translatesAutoresizingMaskIntoConstraints = false
        detailTitleLabel.font = NSFont.systemFont(ofSize: 24, weight: .semibold)
        detailURLLabel.translatesAutoresizingMaskIntoConstraints = false
        detailURLLabel.font = NSFont.systemFont(ofSize: 13, weight: .medium)
        detailURLLabel.maximumNumberOfLines = 2
        detailURLLabel.lineBreakMode = .byTruncatingMiddle

        countChipView.translatesAutoresizingMaskIntoConstraints = false
        countChipView.wantsLayer = true
        countChipView.layer?.cornerRadius = Metrics.chipCornerRadius
        countChipView.layer?.borderWidth = 1
        countChipView.addSubview(countChipLabel)

        countChipLabel.translatesAutoresizingMaskIntoConstraints = false
        countChipLabel.font = NSFont.systemFont(ofSize: 12, weight: .semibold)

        helperLabel.translatesAutoresizingMaskIntoConstraints = false
        helperLabel.font = NSFont.systemFont(ofSize: 14, weight: .medium)

        contentStackView.addArrangedSubview(eyebrowLabel)
        contentStackView.addArrangedSubview(titleLabel)
        contentStackView.addArrangedSubview(subtitleLabel)
        contentStackView.addArrangedSubview(previewCardView)

        previewStackView.addArrangedSubview(glyphContainerView)
        previewStackView.addArrangedSubview(detailTitleLabel)
        previewStackView.addArrangedSubview(detailURLLabel)
        previewStackView.addArrangedSubview(countChipView)
        previewStackView.addArrangedSubview(helperLabel)

        let previewWidthConstraint = previewCardView.widthAnchor.constraint(equalTo: contentStackView.widthAnchor)
        previewWidthConstraint.priority = .defaultHigh

        NSLayoutConstraint.activate([
            contentStackView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: Metrics.outerInset),
            contentStackView.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -Metrics.outerInset),
            contentStackView.topAnchor.constraint(equalTo: topAnchor, constant: Metrics.outerInset),
            contentStackView.bottomAnchor.constraint(lessThanOrEqualTo: bottomAnchor, constant: -Metrics.outerInset),

            previewWidthConstraint,
            previewCardView.widthAnchor.constraint(lessThanOrEqualToConstant: 560),

            previewStackView.leadingAnchor.constraint(equalTo: previewCardView.leadingAnchor, constant: Metrics.previewPadding),
            previewStackView.trailingAnchor.constraint(equalTo: previewCardView.trailingAnchor, constant: -Metrics.previewPadding),
            previewStackView.topAnchor.constraint(equalTo: previewCardView.topAnchor, constant: Metrics.previewPadding),
            previewStackView.bottomAnchor.constraint(equalTo: previewCardView.bottomAnchor, constant: -Metrics.previewPadding),

            glyphContainerView.widthAnchor.constraint(equalToConstant: 116),
            glyphContainerView.heightAnchor.constraint(equalToConstant: 116),
            glyphImageView.centerXAnchor.constraint(equalTo: glyphContainerView.centerXAnchor),
            glyphImageView.centerYAnchor.constraint(equalTo: glyphContainerView.centerYAnchor),
            glyphImageView.widthAnchor.constraint(equalToConstant: Metrics.glyphSize),
            glyphImageView.heightAnchor.constraint(equalToConstant: Metrics.glyphSize),

            countChipLabel.leadingAnchor.constraint(equalTo: countChipView.leadingAnchor, constant: Metrics.chipHorizontalInset),
            countChipLabel.trailingAnchor.constraint(equalTo: countChipView.trailingAnchor, constant: -Metrics.chipHorizontalInset),
            countChipLabel.topAnchor.constraint(equalTo: countChipView.topAnchor, constant: Metrics.chipVerticalInset),
            countChipLabel.bottomAnchor.constraint(equalTo: countChipView.bottomAnchor, constant: -Metrics.chipVerticalInset),
        ])

        configure(spaceName: "Library", tabTitle: "Archived tabs", url: "", archivedCount: 0)
        refreshAppearance()
    }
}

private final class CanvasBackgroundView: NSView {
    private let gradientLayer = CAGradientLayer()
    private let glowLayer = CAGradientLayer()
    private let spotlightLayer = CAGradientLayer()
    private let innerGlowLayer = CAGradientLayer()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupLayers()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupLayers()
    }

    override func layout() {
        super.layout()
        gradientLayer.frame = bounds
        glowLayer.frame = bounds
        spotlightLayer.frame = bounds.insetBy(dx: bounds.width * 0.16, dy: bounds.height * 0.18)
        innerGlowLayer.frame = bounds.insetBy(dx: bounds.width * 0.28, dy: bounds.height * 0.28)
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    func refreshAppearance() {
        let appearance = effectiveAppearance
        layer?.backgroundColor = cgColor(MahoAppColors.contentBackground, using: appearance)
        layer?.borderColor = cgColor(MahoAppColors.contentBorder, using: appearance)
        gradientLayer.colors = [
            cgColor(MahoAppColors.contentGradientTop, using: appearance),
            cgColor(MahoAppColors.contentBackground, using: appearance),
            cgColor(MahoAppColors.contentGradientBottom, using: appearance),
        ]
        glowLayer.colors = [
            cgColor(MahoAppColors.primaryText.withAlphaComponent(0.012), using: appearance),
            NSColor.clear.cgColor,
        ]
        spotlightLayer.colors = [
            cgColor(MahoAppColors.canvasAccent, using: appearance),
            NSColor.clear.cgColor,
        ]
        innerGlowLayer.colors = [
            cgColor(MahoAppColors.canvasInnerGlow, using: appearance),
            NSColor.clear.cgColor,
        ]
    }

    private func setupLayers() {
        wantsLayer = true
        layer?.borderWidth = 1

        gradientLayer.locations = [0, 0.5, 1]
        gradientLayer.startPoint = CGPoint(x: 0.24, y: 1)
        gradientLayer.endPoint = CGPoint(x: 0.76, y: 0)

        glowLayer.locations = [0, 1]
        glowLayer.startPoint = CGPoint(x: 0.5, y: 1)
        glowLayer.endPoint = CGPoint(x: 0.5, y: 0)

        spotlightLayer.type = .radial
        spotlightLayer.locations = [0, 1]
        spotlightLayer.startPoint = CGPoint(x: 0.5, y: 0.56)
        spotlightLayer.endPoint = CGPoint(x: 1.0, y: 1.0)

        innerGlowLayer.type = .radial
        innerGlowLayer.locations = [0, 1]
        innerGlowLayer.startPoint = CGPoint(x: 0.5, y: 0.52)
        innerGlowLayer.endPoint = CGPoint(x: 1.0, y: 1.0)

        layer?.addSublayer(gradientLayer)
        layer?.addSublayer(spotlightLayer)
        layer?.addSublayer(innerGlowLayer)
        layer?.addSublayer(glowLayer)
        refreshAppearance()
    }

    private func cgColor(_ color: NSColor, using appearance: NSAppearance) -> CGColor {
        var resolved = color.cgColor
        appearance.performAsCurrentDrawingAppearance {
            resolved = color.cgColor
        }
        return resolved
    }
}
