import SwiftUI
import WebKit
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct MainBrowserView: View {
    enum ShellRoute {
        case home
        case browsing(TabId)

        var isBrowsing: Bool {
            if case .browsing = self { return true }
            return false
        }

        var browsingTabId: TabId? {
            guard case .browsing(let tabId) = self else { return nil }
            return tabId
        }
    }

    @StateObject private var browserVM = BrowserViewModel()
    @StateObject private var browserState = BrowserState()
    @State private var showTabGrid = false
    @State private var showSettings = false
    @State private var showHistory = false
    @State private var showBookmarks = false
    @State private var showArchive = false
    @State private var showReadingList = false
    @State private var showDownloads = false
    @State private var showNotes = false
    @State private var showFindInPage = false
    @State private var isIncognito = false
    @State private var activeTabId: TabId?
    @State private var activeSpaceId: SpaceId?
    @State private var tabs: [TabViewModel] = []
    @State private var spaces: [SpaceViewModel] = []
    @State private var tintColor: Color = ShellTheme.Palette.accent
    @State private var isBottomBarVisible = true
    @State private var chromeVisibilityProgress: CGFloat = 1
    @State private var bottomBarTabSwipeOffset: CGFloat = 0
    @State private var pendingNavigationUrl: String?
    @ObservedObject private var agenticJourney = E2EAgenticJourney.shared
    @State private var isReaderMode = false
    @State private var zoomLevel = 1.0
    @State private var isDesktopMode = false
    @State private var defaultUserAgent: String?
    @StateObject private var pageSummarizer = PageSummarizer()
    @State private var showSummaryOverlay = false
    @State private var showPinchSummary = false
    @State private var pinchSummaryTitle = ""
    @State private var pinchSummarySentences: [String] = []
    @State private var shellRoute: ShellRoute = .home
    @State private var homeQuery = ""
    @StateObject private var pinchSummarizer = PageSummarizer()
    @State private var browseForMeSeed = UUID()
    @State private var isSearchFocused = false
    @State private var searchOrigin: SearchPresentationOrigin?
    @StateObject private var commandBarVM = CommandBarViewModel()
    @State private var showVoiceAssistant = false
    @State private var voiceSearchManager = VoiceSearchManager()
    @State private var showAiChat = false
    @State private var showConversations = false
    @State private var autoOpenAiChatOnAppear = false
    @State private var askAIQuery: String?

    private let bridge = MahoBridge.shared
    private let tickTimer = Timer.publish(every: 0.35, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            shellBackgroundLayer

            e2eVerifyResultOverlay

            if let activeTabId {
                BrowserView(
                    viewModel: browserVM,
                    tabId: activeTabId,
                    isIncognito: effectiveIsPrivate,
                    isBarVisible: $isBottomBarVisible,
                    tintColor: $tintColor,
                    chromeVisibilityProgress: $chromeVisibilityProgress,
                    onPinchSummarize: triggerPinchSummarize,
                    onNewTabRequested: { url in
                        createTabAndNavigate(url.absoluteString)
                    }
                )
            }

            if !isBrowsing {
                HomeSearchView(
                    isIncognito: effectiveIsPrivate,
                    onOpenTabs: {
                        refreshState()
                        showTabGrid = true
                    },
                    onOpenSettings: {
                        showSettings = true
                    },
                    onResumeTab: { tabId in
                        resumeTabFromHome(tabId)
                    },
                    onSelectSite: { url in
                        if let activeSpace = bridge.getActiveSpaceId() {
                            bridge.createTab(url: Url(url), inSpace: activeSpace)
                            refreshState()
                        }
                    }
                )
                .transition(.opacity)
                .zIndex(0)
            }
            Color.clear
                .safeAreaInset(edge: .bottom) {
                    Color.clear.frame(height: bottomChromeInset)
                }
            .animation(.spring(response: 0.3, dampingFraction: 0.86), value: chromeVisibilityProgress)

            if isBrowsing {
                VStack(spacing: 0) {
                    if effectiveIsPrivate {
                        PrivateBanner()
                        .transition(.move(edge: .top).combined(with: .opacity))
                    }

                    Spacer()
                }
                .animation(.spring(response: 0.32, dampingFraction: 0.86), value: effectiveIsPrivate)
            }

            VStack(spacing: 0) {
                if showFindInPage, let activeTabId = browsingTabId {
                    FindInPageView(tabId: activeTabId, isVisible: $showFindInPage, isIncognito: effectiveIsPrivate)
                        .padding(.top, ShellTheme.Spacing.small)
                }

                Spacer()

                ArcBottomBar(
                    tabCount: currentSpaceTabs.count,
                    tintColor: effectiveTintColor,
                    isIncognito: isIncognito,
                    isHomeMode: !isBrowsing,
                    isLoading: browserVM.isLoading,
                    canReload: activeTabId != nil,
                    canShare: shareURL != nil,
                    isReaderMode: isReaderMode,
                    isDesktopMode: isDesktopMode,
                    zoomLevel: zoomLevel,
                    pageTitle: browserVM.currentTitle,
                    pageHost: MainBrowserPresentation.displayHost(for: browserVM.currentUrl) ?? "",
                    canGoBack: browserVM.canGoBack,
                    canGoForward: browserVM.canGoForward,
                    tabSwipeOffset: bottomBarTabSwipeOffset,
                    spaces: spacesMenuItems,
                    homeQuery: $homeQuery,
                    onTabsTapped: {
                        refreshState()
                        showTabGrid = true
                    },
                    onAddressTapped: {
                        if let tabId = browsingTabId {
                            presentSearchOverlay(origin: .browsing(tabId))
                        } else {
                            presentSearchOverlay(origin: .home)
                        }
                    },
                    onNewTabTapped: {
                        presentSearchOverlay(origin: .home)
                    },
                    onGoBack: goBack,
                    onGoForward: goForward,
                    onTabSwipeLeft: {
                        switchTab(direction: .next)
                    },
                    onTabSwipeRight: {
                        switchTab(direction: .previous)
                    },
                    onSelectSpace: selectSpace,
                    onFindInPage: {
                        guard browsingTabId != nil else { return }
                        showFindInPage = true
                    },
                    onSettings: {
                        showSettings = true
                    },
                    onOpenAiChat: {
                        presentAiChat(query: "")
                    },
                    onOpenConversations: {
                        showConversations = true
                    },
                    onShare: shareCurrentPage,
                    onReload: reloadCurrentPage,
                    onArchive: archiveCurrentTab,
                    onOpenArchive: {
                        browserState.loadArchivedTabs()
                        showArchive = true
                    },
                    onToggleReader: toggleReaderMode,
                    onZoomIn: {
                        adjustZoom(by: 0.1)
                    },
                    onZoomOut: {
                        adjustZoom(by: -0.1)
                    },
                    onResetZoom: resetZoom,
                    onToggleDesktop: toggleDesktopMode,
                    onPiP: {
                        if let webView = browserVM.webView {
                            PiPManager.shared.enablePiP(for: webView)
                        }
                    },
                    onBrowseForMe: {
                        guard browsingTabId != nil else { return }
                        showPinchSummary = false
                        browseForMeSeed = UUID()
                        showSummaryOverlay = true
                        pageSummarizer.summarize(webView: browserVM.webView)
                    }
                )
                .padding(.horizontal, ShellTheme.Spacing.medium)
                .padding(.bottom, ShellTheme.Spacing.xSmall)
                .offset(y: bottomBarVerticalOffset)
                .scaleEffect(bottomBarScale, anchor: .bottom)
                .opacity(bottomBarOpacity)
                .allowsHitTesting(isBrowsing ? isBottomBarVisible : true)
                .accessibilityHidden(isBrowsing ? !isBottomBarVisible : false)
                .accessibilityIdentifier("mainBrowserBottomBar")
                .accessibilityValue((isBrowsing ? isBottomBarVisible : true) ? "visible" : "hidden")
            }
            .zIndex(2)
            .animation(.spring(response: 0.28, dampingFraction: 0.88), value: isBottomBarVisible)
            .animation(.spring(response: 0.3, dampingFraction: 0.86), value: chromeVisibilityProgress)
            .animation(.spring(response: 0.28, dampingFraction: 0.88), value: showFindInPage)

            if showSummaryOverlay {
                ZStack(alignment: .top) {
                    Rectangle()
                        .fill(.ultraThinMaterial)
                        .overlay {
                            LinearGradient(
                                colors: [
                                    Color.white.opacity(0.02),
                                    ShellTheme.Palette.overlayDim.opacity(0.18),
                                    ShellTheme.Palette.overlayDim.opacity(0.3)
                                ],
                                startPoint: .top,
                                endPoint: .bottom
                            )
                        }
                        .ignoresSafeArea()
                        .allowsHitTesting(false)
                        .transition(.opacity)

                    VStack(spacing: ShellTheme.Spacing.mediumLarge) {
                        SummaryOverlayView(
                            state: pageSummarizer.state,
                            onDismiss: {
                                showSummaryOverlay = false
                                pageSummarizer.reset()
                            }
                        )
                        .id(browseForMeSeed)
                        .transition(.move(edge: .top).combined(with: .opacity))
                        Spacer()
                    }
                    .padding(.top, ShellTheme.Spacing.page)
                }
                .animation(.spring(response: 0.35, dampingFraction: 0.85), value: showSummaryOverlay)
            }

            if showPinchSummary {
                PinchSummaryView(
                    title: pinchSummaryTitle,
                    sentences: pinchSummarySentences,
                    isIncognito: effectiveIsPrivate,
                    onDismiss: {
                        withAnimation(.spring(response: 0.3, dampingFraction: 0.85)) {
                            showPinchSummary = false
                        }
                    }
                )
                .transition(.scale(scale: 0.85).combined(with: .opacity))
                .zIndex(10)
            }

            if showVoiceAssistant {
                VoiceAssistantOverlayView(
                    voiceManager: voiceSearchManager,
                    onResult: handleVoiceAssistantResult,
                    onDismiss: dismissVoiceAssistantOverlay
                )
                .transition(.opacity.combined(with: .move(edge: .bottom)))
                .zIndex(90)
            }

            if isSearchFocused {
                SearchSheet(
                    query: $homeQuery,
                    viewModel: commandBarVM,
                    isIncognito: effectiveIsPrivate,
                    onSubmit: { query in
                        handleSearchSubmission(query)
                    },
                    onSelectSuggestion: { suggestion in
                        handleSearchSuggestionSelection(suggestion)
                    },
                    onBrowseForMe: { query in
                        dismissSearchOverlay()
                        presentAiChat(query: query)
                    },
                    onTogglePrivate: {
                        toggleSearchOverlayPrivateMode()
                    },
                    onCancel: {
                        dismissSearchOverlay()
                    },
                    voiceManager: voiceSearchManager,
                    onVoiceAssistantRequested: {
                        presentVoiceAssistantOverlay()
                    }
                )
                .transition(.opacity.combined(with: .move(edge: .bottom)))
                .zIndex(100)
            }
        }
        .mainBrowserSheets(
            showTabGrid: $showTabGrid,
            showSettings: $showSettings,
            showHistory: $showHistory,
            showBookmarks: $showBookmarks,
            showArchive: $showArchive,
            showReadingList: $showReadingList,
            isReaderMode: $isReaderMode,
            showDownloads: $showDownloads,
            showNotes: $showNotes,
            showAiChat: $showAiChat,
            showConversations: $showConversations,
            isIncognito: $isIncognito,
            browserVM: browserVM,
            browserState: browserState,
            askAIQuery: askAIQuery,
            onTabGridDismiss: handleTabGridDismiss,
            onNavigate: createTabAndNavigate
        )
        .onAppear {
            loadBrowserState()
            navigateAddressBarFixtureIfNeeded()
            setupDeepLinkHandler()
        }
        .onReceive(tickTimer) { _ in
            processUpdates(bridge.tickDrain())
            if let url = pendingNavigationUrl, browserVM.webView != nil {
                pendingNavigationUrl = nil
                browserVM.navigate(to: url)
            }
            updateHandoffIfNeeded()
        }
        .userActivity(HandoffManager.activityType) { activity in
            updateHandoffActivity(activity)
        }
        .onReceive(pinchSummarizer.$state) { newState in
            if case .result(let sentences) = newState {
                pinchSummarySentences = sentences
                showSummaryOverlay = false
                pageSummarizer.reset()
                withAnimation(.spring(response: 0.35, dampingFraction: 0.85)) {
                    showPinchSummary = true
                }
                pinchSummarizer.reset()
            } else if case .error = newState {
                pinchSummarizer.reset()
            }
        }
        .onChange(of: homeQuery) { _, newValue in
            Task {
                try? await Task.sleep(nanoseconds: 150 * 1_000_000)
                guard newValue == homeQuery, isSearchFocused else { return }
                commandBarVM.query(newValue, isIncognito: effectiveIsPrivate)
            }
        }
        .onChange(of: isSearchFocused) { _, newValue in
            if newValue {
                commandBarVM.query(homeQuery, isIncognito: effectiveIsPrivate)
            }
        }
        .onChange(of: effectiveIsPrivate) { _, newValue in
            guard isSearchFocused else { return }
            commandBarVM.query(homeQuery, isIncognito: newValue)
        }
    }

    private func setupDeepLinkHandler() {
        DeepLinkHandler.shared.onOpenURL = { [self] urlString in
            createTabAndNavigate(urlString)
        }
        DeepLinkHandler.shared.onSearch = { [self] query in
            let encoded = query.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? query
            let searchURL = "https://www.google.com/search?q=\(encoded)"
            createTabAndNavigate(searchURL)
        }
        DeepLinkHandler.shared.onNewTab = { [self] in
            activeTabId = nil
            transitionToHome()
            presentSearchOverlay(origin: .home)
        }
        HandoffManager.shared.onReceiveURL = { [self] url in
            createTabAndNavigate(url.absoluteString)
        }

        if DeepLinkHandler.shared.pendingNewTabSearch {
            DeepLinkHandler.shared.pendingNewTabSearch = false
            activeTabId = nil
            transitionToHome()
            presentSearchOverlay(origin: .home)
        }
    }

    private func updateHandoffIfNeeded() {
        guard activeTabId != nil, let url = URL(string: browserVM.currentUrl) else { return }
        _ = HandoffManager.shared.makeActivity(with: url)
    }

    private func updateHandoffActivity(_ activity: NSUserActivity) {
        guard activeTabId != nil, let url = URL(string: browserVM.currentUrl) else { return }
        _ = HandoffManager.shared.makeActivity(with: url)
    }

    private var currentSpaceTabs: [TabViewModel] {
        guard let activeSpaceId else { return tabs }
        return tabs.filter { $0.spaceId == activeSpaceId }
    }

    private var recentTabs: [TabViewModel] {
        currentSpaceTabs
            .sorted { $0.lastActiveAt > $1.lastActiveAt }
            .prefix(3)
            .map { $0 }
    }

    private var topSites: [TabViewModel] {
        var seen = Set<String>()
        return currentSpaceTabs
            .sorted { $0.lastActiveAt > $1.lastActiveAt }
            .filter { tab in
                guard !tab.url.isEmpty else { return false }
                return seen.insert(tab.url).inserted
            }
            .prefix(8)
            .map { $0 }
    }

    /// On-screen privacy: active tab's privacy when browsing, else the "new tab
    /// mode" (`isIncognito`). Drives webview storage + private chrome. Conflating
    /// this with `isIncognito` (which only picks the mode for NEW tabs) reintroduces
    /// the privacy leak this feature fixes.
    private var effectiveIsPrivate: Bool {
        currentActiveTab?.isPrivate ?? isIncognito
    }

    private var effectiveTintColor: Color {
        effectiveIsPrivate ? ShellTheme.Palette.incognitoTint : tintColor
    }

    private var browsingTabId: TabId? {
        guard case .browsing(let tabId) = shellRoute else { return nil }
        return tabId
    }

    private var isBrowsing: Bool {
        browsingTabId != nil
    }

    private var activeSpaceName: String {
        spaces.first(where: { $0.id == activeSpaceId })?.name ?? "Space"
    }

    private var backgroundColor: Color {
        if effectiveIsPrivate {
            return ShellTheme.Palette.incognitoBackground
        }

        return activeTabId != nil ? ShellTheme.Palette.browsingBackground : ShellTheme.Palette.homeBackground
    }

    @ViewBuilder
    private var e2eVerifyResultOverlay: some View {
        Group {
            if E2ETestHooks.verifyResult != .notRun {
                Text(E2ETestHooks.verifyResult.rawValue)
                    .frame(width: 1, height: 1)
                    .opacity(0.001)
                    .allowsHitTesting(false)
                    .accessibilityIdentifier("e2eVerifyResult")
                    .accessibilityLabel(E2ETestHooks.verifyResult.rawValue)
            }
            if let status = E2ETestHooks.archiveFixtureStatus {
                Text(status.rawValue)
                    .frame(width: 1, height: 1)
                    .opacity(0.001)
                    .allowsHitTesting(false)
                    .accessibilityIdentifier("archiveFixtureStatus")
                    .accessibilityLabel(status.rawValue)
            }
            if E2ETestHooks.archiveFixtureReady {
                Text("Archive fixture ready")
                    .frame(width: 1, height: 1)
                    .opacity(0.001)
                    .allowsHitTesting(false)
                    .accessibilityIdentifier("archiveFixtureReady")
                    .accessibilityLabel("Archive fixture ready")
            }
            if let status = E2ETestHooks.addressBarFixtureStatus {
                Text(status.rawValue)
                    .frame(width: 1, height: 1)
                    .opacity(0.001)
                    .allowsHitTesting(false)
                    .accessibilityIdentifier("addressBarFixtureStatus")
                    .accessibilityLabel(status.rawValue)
            }
            if E2EAgenticJourney.isArmed {
                Text(agenticJourney.status)
                    .frame(width: 1, height: 1)
                    .opacity(0.001)
                    .allowsHitTesting(false)
                    .accessibilityIdentifier("agenticJourneyStatus")
                    .accessibilityLabel(agenticJourney.status)
                Text(agenticJourney.steps.joined(separator: "|"))
                    .frame(width: 1, height: 1)
                    .opacity(0.001)
                    .allowsHitTesting(false)
                    .lineLimit(1)
                    .accessibilityIdentifier("agenticJourneySteps")
                    .accessibilityLabel(agenticJourney.steps.joined(separator: "|"))
            }
        }
    }

    @ViewBuilder
    private var shellBackgroundLayer: some View {
        if effectiveIsPrivate {
            LinearGradient(
                colors: [
                    ShellTheme.Palette.incognitoBackground,
                    ShellTheme.Palette.incognitoTint,
                    ShellTheme.Palette.incognitoBackground
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .overlay {
                VStack {
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                        .fill(ShellTheme.Palette.incognitoSurface)
                        .frame(maxWidth: ShellTheme.Size.incognitoBannerMaxWidth)
                        .frame(height: 160)
                        .blur(radius: 0.5)
                        .offset(y: -96)

                    Spacer()
                }
            }
            .ignoresSafeArea()
        } else {
            backgroundColor.ignoresSafeArea()
        }
    }

    private var incognitoStateBanner: some View {
        HStack(alignment: .center, spacing: ShellTheme.Spacing.medium) {
            ZStack {
                RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                    .fill(ShellTheme.Palette.incognitoSurfaceStrong)
                    .frame(width: ShellTheme.Size.summaryStateIcon, height: ShellTheme.Size.summaryStateIcon)

                Image(lucide: Lucide.eyeOff)
                    .font(.headline.weight(.semibold))
                    .foregroundStyle(ShellTheme.Palette.incognitoForeground)
            }

            Text("Private")
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(ShellTheme.Palette.incognitoForeground)

            Spacer(minLength: 0)
        }
        .padding(ShellTheme.Spacing.medium)
        .frame(maxWidth: ShellTheme.Size.incognitoBannerMaxWidth)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .fill(ShellTheme.Palette.incognitoSurface)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .strokeBorder(ShellTheme.Palette.incognitoBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
        .shadow(
            color: ShellTheme.Elevation.bottomBar(isIncognito: true).color,
            radius: ShellTheme.Elevation.bottomBar(isIncognito: true).radius,
            x: ShellTheme.Elevation.bottomBar(isIncognito: true).x,
            y: ShellTheme.Elevation.bottomBar(isIncognito: true).y
        )
        .padding(.horizontal, ShellTheme.Spacing.large)
        .frame(maxWidth: .infinity)
        .accessibilityIdentifier("incognitoStateBanner")
        .accessibilityValue("active")
    }

    private var shareURL: URL? {
        URL(string: browserVM.currentUrl)
    }

    private var canReloadCurrentPage: Bool {
        activeTabId != nil
    }

    private var canShareCurrentPage: Bool {
        shareURL != nil
    }

    private var bottomChromeInset: CGFloat {
        let collapsedInset = ShellTheme.Size.bottomInsetWithoutBar
        let expandedInset = ShellTheme.Size.bottomInsetWithBar
        let visibilityProgress = activeTabId == nil ? CGFloat(1) : chromeVisibilityProgress
        return collapsedInset + ((expandedInset - collapsedInset) * visibilityProgress)
    }

    private var bottomBarOpacity: Double {
        let visibleProgress: CGFloat = max(chromeVisibilityProgress, isBottomBarVisible ? 1 : 0)
        let overlayWeight = showSummaryOverlay || showPinchSummary ? CGFloat(0.78) : CGFloat(1)
        let opacity = (CGFloat(0.18) + (visibleProgress * CGFloat(0.82))) * overlayWeight
        return Double(opacity)
    }

    private var bottomBarVerticalOffset: CGFloat {
        let hiddenOffset = ShellTheme.Size.bottomInsetWithBar + ShellTheme.Spacing.field
        let progress: CGFloat = max(chromeVisibilityProgress, isBottomBarVisible ? 1 : 0)
        let overlayLift = showSummaryOverlay || showPinchSummary ? ShellTheme.Spacing.small : CGFloat.zero
        return ((CGFloat(1) - progress) * hiddenOffset) + overlayLift
    }

    private var bottomBarScale: CGFloat {
        let progress: CGFloat = max(chromeVisibilityProgress, isBottomBarVisible ? 1 : 0)
        let overlayScale = showSummaryOverlay || showPinchSummary ? CGFloat(0.97) : CGFloat(1)
        return (CGFloat(0.92) + (progress * CGFloat(0.08))) * overlayScale
    }

    private var spacesMenuItems: [ArcBottomBar.SpaceMenuItem] {
        spaces.map { space in
            ArcBottomBar.SpaceMenuItem(id: space.id, name: space.name, isActive: space.isActive)
        }
    }

    private var orderedBrowsingTabs: [TabViewModel] {
        currentSpaceTabs.sorted { $0.lastActiveAt > $1.lastActiveAt }
    }

    private func loadBrowserState() {
        refreshState()
        if isBrowsing {
            isBottomBarVisible = true
            chromeVisibilityProgress = 1
        }
    }

    private func navigateAddressBarFixtureIfNeeded() {
        guard case .ready? = E2ETestHooks.addressBarFixtureStatus,
              let tab = currentActiveTab else {
            return
        }
        pendingNavigationUrl = tab.url
    }

    private func refreshState() {
        tabs = bridge.getTabViewModels()
        spaces = bridge.getSpaceViewModels()
        activeSpaceId = bridge.getActiveSpaceId() ?? spaces.first?.id
        activeTabId = bridge.getActiveTabId()

        if let tab = currentActiveTab {
            if browsingTabId != nil || pendingNavigationUrl != nil {
                shellRoute = .browsing(tab.id)
            }
            browserVM.tabId = tab.id
            browserVM.currentTitle = tab.title
            browserVM.currentUrl = tab.url
            browserVM.isLoading = tab.isLoading
            browserVM.isSecure = tab.url.hasPrefix("https://")
            tintColor = MainBrowserPresentation.tintColor(for: tab)
            reapplyPageOptionsIfNeeded()
        } else {
            browserVM.tabId = ""
            browserVM.currentTitle = ""
            browserVM.currentUrl = ""
            browserVM.isLoading = false
            tintColor = ShellTheme.Palette.accent
            isBottomBarVisible = true
            showFindInPage = false
            isReaderMode = false
            zoomLevel = 1.0
            isDesktopMode = false
            defaultUserAgent = nil
            shellRoute = .home
            chromeVisibilityProgress = 1
            bottomBarTabSwipeOffset = 0
        }
    }

    private var currentActiveTab: TabViewModel? {
        guard let activeTabId else { return nil }
        return tabs.first(where: { $0.id == activeTabId })
    }

    private func processUpdates(_ updates: [CoreUpdate]) {
        guard !updates.isEmpty else { return }

        var requiresRefresh = false

        for update in updates {
            browserVM.handleCoreUpdate(update)
            bridge.handleProfileUpdate(update)

            switch update {
            case .fullState,
                 .tabCreated,
                 .tabUpdated,
                 .tabClosed,
                 .tabOrderChanged,
                 .spaceCreated,
                 .spaceUpdated,
                 .spaceDeleted,
                 .spaceRenamed,
                 .spaceRecolored,
                 .spaceReordered,
                 .spaceOrderChanged,
                 .activeSpaceChanged,
                 .tabsMigrated,
                 .tabLifecycleChanged,
                 .navigateTab:
                requiresRefresh = true
            case .showFindBar(let tabId):
                activeTabId = tabId
                shellRoute = .browsing(tabId)
                showFindInPage = true
            default:
                break
            }
        }

        if requiresRefresh {
            refreshState()
        }
    }

    private func handleTabGridDismiss() {
        refreshState()
    }

    private func createTabAndNavigate(_ query: String) {
        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        guard let targetSpaceId = activeSpaceId ?? spaces.first?.id else { return }

        homeQuery = ""
        let destination = MainBrowserPresentation.normalizedURL(from: trimmed)
        pendingNavigationUrl = destination
        browserVM.currentUrl = destination
        browserVM.currentTitle = ""
        bridge.createTab(url: Url(destination), inSpace: targetSpaceId, isPrivate: effectiveIsPrivate)
        refreshState()

        let fallbackTabId = currentSpaceTabs.sorted(by: { $0.lastActiveAt > $1.lastActiveAt }).first?.id

        if let activatedTab = bridge.getActiveTabId() ?? fallbackTabId {
            activeTabId = activatedTab
            shellRoute = .browsing(activatedTab)
            browserVM.tabId = activatedTab
            isBottomBarVisible = true
            chromeVisibilityProgress = 1
        }
    }

    private func reloadCurrentPage() {
        guard browsingTabId != nil else { return }
        browserVM.reload()
    }

    private func archiveCurrentTab() {
        guard let tabId = browsingTabId else { return }
        bridge.archiveTab(id: tabId)
        refreshState()
    }

    private func activateTab(_ id: TabId) {
        activeTabId = id
        bridge.activateTab(id: id)
        refreshState()
        if let tab = tabs.first(where: { $0.id == id }), !tab.url.isEmpty && tab.url != "about:blank" {
            if browserVM.currentUrl != tab.url {
                browserVM.currentUrl = tab.url
                browserVM.navigate(to: tab.url)
            }
        }
        shellRoute = .browsing(id)
        isBottomBarVisible = true
        chromeVisibilityProgress = 1
    }

    private func resumeTabFromHome(_ id: TabId) {
        activateTab(id)
    }

    private func presentSearchOverlay(origin: SearchPresentationOrigin) {
        searchOrigin = origin
        homeQuery = MainBrowserPresentation.searchPrefillQuery(for: origin, currentUrl: browserVM.currentUrl)
        withAnimation(.spring(response: 0.32, dampingFraction: 0.88)) {
            isSearchFocused = true
        }
    }

    private func presentSearchOverlay(prefillCurrentUrl: Bool = true) {
        if prefillCurrentUrl, let tabId = browsingTabId {
            presentSearchOverlay(origin: .browsing(tabId))
        } else {
            presentSearchOverlay(origin: .home)
        }
    }

    private func dismissSearchOverlay(clearQuery: Bool = true) {
        withAnimation(.spring(response: 0.24, dampingFraction: 0.9)) {
            isSearchFocused = false
        }

        if clearQuery {
            homeQuery = ""
            commandBarVM.suggestions = []
        }
        searchOrigin = nil
    }

    private func handleSearchSubmission(_ rawQuery: String) {
        guard let plan = MainBrowserPresentation.searchSubmissionPlan(
            for: searchOrigin,
            query: rawQuery,
            activeTabId: activeTabId
        ) else {
            dismissSearchOverlay()
            return
        }

        dismissSearchOverlay()

        switch plan {
        case .reuseActiveTab(let tabId, let destination):
            pendingNavigationUrl = nil
            browserVM.currentUrl = destination
            browserVM.currentTitle = ""
            if browserVM.tabId != tabId {
                browserVM.tabId = tabId
            }
            browserVM.navigate(to: destination)
            shellRoute = .browsing(tabId)
            isBottomBarVisible = true
            chromeVisibilityProgress = 1
        case .createNewTab(let destination):
            createTabAndNavigate(destination)
        }
    }

    private func presentVoiceAssistantOverlay() {
        voiceSearchManager.reset()
        withAnimation(.spring(response: 0.32, dampingFraction: 0.88)) {
            isSearchFocused = false
            showVoiceAssistant = true
        }
    }

    private func dismissVoiceAssistantOverlay() {
        voiceSearchManager.reset()
        withAnimation(.spring(response: 0.24, dampingFraction: 0.9)) {
            showVoiceAssistant = false
        }
    }

    private func handleVoiceAssistantResult(_ text: String) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        voiceSearchManager.reset()

        withAnimation(.spring(response: 0.24, dampingFraction: 0.9)) {
            showVoiceAssistant = false
            isSearchFocused = false
        }

        guard !trimmed.isEmpty else { return }
        presentAiChat(query: trimmed)
    }

    private func toggleSearchOverlayPrivateMode() {
        let targetPrivateState = !effectiveIsPrivate
        setPrivate(targetPrivateState)
        if let activeTabId {
            searchOrigin = .browsing(activeTabId)
        } else {
            searchOrigin = .home
        }
        commandBarVM.query(homeQuery, isIncognito: targetPrivateState)
    }

    private func handleSearchSuggestionSelection(_ suggestion: SuggestionViewModel) {
        if suggestion.kind == .aiAnswer {
            dismissSearchOverlay()
            presentAiChat(query: aiSearchQueryText(for: suggestion))
            return
        }

        let targetUrl = suggestion.executionPayload ?? suggestion.key
        handleSearchSubmission(targetUrl)
    }

    private func aiSearchQueryText(for suggestion: SuggestionViewModel) -> String {
        if suggestion.key.hasPrefix("ai_search:") {
            return String(suggestion.key.dropFirst("ai_search:".count))
        }

        return suggestion.title
    }

    private func transitionToHome() {
        shellRoute = .home
        homeQuery = ""
        showFindInPage = false
        isBottomBarVisible = true
        chromeVisibilityProgress = 1
        bottomBarTabSwipeOffset = 0
    }

    private func presentAiChat(query: String) {
        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        askAIQuery = trimmed.isEmpty ? nil : trimmed
        autoOpenAiChatOnAppear = !trimmed.isEmpty
        showAiChat = true
    }

    private func goBack() {
        guard browsingTabId != nil else { return }
        browserVM.goBack()
    }

    private func goForward() {
        guard browsingTabId != nil else { return }
        browserVM.goForward()
    }

    private enum TabSwitchDirection {
        case previous
        case next
    }

    private func switchTab(direction: TabSwitchDirection) {
        guard let activeTabId else { return }
        let orderedTabs = orderedBrowsingTabs
        guard orderedTabs.count > 1,
              let currentIndex = orderedTabs.firstIndex(where: { $0.id == activeTabId }) else {
            resetTabSwipeOffset()
            return
        }

        let targetIndex: Int
        let offsetDirection: CGFloat

        switch direction {
        case .previous:
            guard currentIndex > 0 else {
                bounceTabSwipeOffset(direction: 1)
                return
            }
            targetIndex = currentIndex - 1
            offsetDirection = 1
        case .next:
            guard currentIndex < orderedTabs.count - 1 else {
                bounceTabSwipeOffset(direction: -1)
                return
            }
            targetIndex = currentIndex + 1
            offsetDirection = -1
        }

        let targetTab = orderedTabs[targetIndex]
        withAnimation(.easeOut(duration: 0.16)) {
            bottomBarTabSwipeOffset = 18 * offsetDirection
        }
        activateTab(targetTab.id)
        withAnimation(.spring(response: 0.3, dampingFraction: 0.76)) {
            bottomBarTabSwipeOffset = 0
        }

#if canImport(UIKit)
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
#endif
    }

    private func bounceTabSwipeOffset(direction: CGFloat) {
        withAnimation(.easeOut(duration: 0.12)) {
            bottomBarTabSwipeOffset = 10 * direction
        }
        resetTabSwipeOffset(delay: 0.12)
    }

    private func resetTabSwipeOffset(delay: Double = 0) {
        let reset = {
            withAnimation(.spring(response: 0.28, dampingFraction: 0.8)) {
                bottomBarTabSwipeOffset = 0
            }
        }

        if delay == 0 {
            reset()
        } else {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: reset)
        }
    }

    private func selectSpace(_ id: SpaceId) {
        bridge.activateSpace(id: id)
        refreshState()

        if let firstTab = currentSpaceTabs.sorted(by: { $0.lastActiveAt > $1.lastActiveAt }).first {
            activateTab(firstTab.id)
        } else {
            transitionToHome()
            activeTabId = nil
            browserVM.tabId = ""
        }
    }

    private func toggleReaderMode() {
        guard browsingTabId != nil else { return }
        isReaderMode.toggle()
        if isReaderMode {
            runReadabilityExtraction()
        } else {
            browserVM.extractedArticle = nil
        }
    }

    private func runReadabilityExtraction() {
        guard let webView = browserVM.webView else { return }
        webView.evaluateJavaScript("window.__mahoRunReadability && window.__mahoRunReadability();") { _, error in
            if let error = error {
                print("Readability execution failed: \(error.localizedDescription)")
            }
        }
    }

    private func triggerPinchSummarize() {
        guard browsingTabId != nil, !showPinchSummary, !showSummaryOverlay else { return }
        pinchSummaryTitle = browserVM.currentTitle
        pinchSummarizer.summarize(webView: browserVM.webView)
    }

    private func translatePage() {
        guard browsingTabId != nil else { return }
        browserVM.translatePage()
    }

    private func adjustZoom(by delta: Double) {
        guard browsingTabId != nil else { return }
        let updatedZoom = min(max(zoomLevel + delta, 0.5), 2.0)
        zoomLevel = (updatedZoom * 10).rounded() / 10
        browserVM.setZoom(zoomLevel)

        let percentage = Int((zoomLevel * 100).rounded())
        browserVM.webView?.evaluateJavaScript("document.body.style.zoom = '\(percentage)%';")
    }

    private func resetZoom() {
        guard browsingTabId != nil else { return }
        zoomLevel = 1.0
        browserVM.setZoom(zoomLevel)
        browserVM.webView?.evaluateJavaScript("document.body.style.zoom = '100%';")
    }

    private func toggleDesktopMode() {
        guard browsingTabId != nil else { return }
        isDesktopMode.toggle()
        applyDesktopMode(reload: true)
    }

    private func applyDesktopMode(reload: Bool) {
        guard let webView = browserVM.webView else { return }

        if defaultUserAgent == nil {
            if let currentCustomUserAgent = webView.customUserAgent, !currentCustomUserAgent.isEmpty {
                defaultUserAgent = currentCustomUserAgent
            } else {
                webView.evaluateJavaScript("navigator.userAgent") { result, _ in
                    guard let userAgent = result as? String else { return }
                    if defaultUserAgent == nil {
                        defaultUserAgent = userAgent
                    }
                    if isDesktopMode {
                        webView.customUserAgent = MainBrowserPresentation.desktopUserAgent(from: userAgent)
                        if reload {
                            browserVM.reload()
                        }
                    }
                }
                return
            }
        }

        if isDesktopMode {
            webView.customUserAgent = MainBrowserPresentation.desktopUserAgent(from: defaultUserAgent ?? "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15")
        } else {
            webView.customUserAgent = defaultUserAgent
        }

        if reload {
            browserVM.reload()
        }
    }

    private func reapplyPageOptionsIfNeeded() {
        guard browserVM.webView != nil else { return }

        if isReaderMode {
            runReadabilityExtraction()
        }

        if abs(zoomLevel - 1.0) > 0.001 {
            let percentage = Int((zoomLevel * 100).rounded())
            browserVM.webView?.evaluateJavaScript("document.body.style.zoom = '\(percentage)%';")
        }

        if isDesktopMode {
            applyDesktopMode(reload: false)
        }
    }

    private func setPrivate(_ on: Bool) {
        let targetSpaceId = activeSpaceId ?? spaces.first?.id ?? ""
        guard !targetSpaceId.isEmpty else { return }
        if on {
            isIncognito = true
            tintColor = ShellTheme.Palette.incognitoTint
            showSummaryOverlay = false
            pageSummarizer.reset()
            bridge.createTab(url: nil, inSpace: targetSpaceId, isPrivate: true)
            refreshState()
        } else {
            let privateTabs = bridge.getTabViewModels().filter { $0.spaceId == targetSpaceId && $0.isPrivate }
            for tab in privateTabs {
                bridge.closeTab(id: tab.id)
            }
            isIncognito = false
            refreshState()

            let remainingTabs = bridge.getTabViewModels().filter { $0.spaceId == targetSpaceId && !$0.isPrivate }
            if let firstNormal = remainingTabs.sorted(by: { $0.lastActiveAt > $1.lastActiveAt }).first {
                bridge.activateTab(id: firstNormal.id)
                tintColor = MainBrowserPresentation.tintColor(for: firstNormal)
            } else {
                transitionToHome()
                tintColor = ShellTheme.Palette.accent
            }
            refreshState()
        }
    }

    private func shareCurrentPage() {
#if canImport(UIKit)
        guard let url = shareURL else { return }

        let activityVC = UIActivityViewController(activityItems: [url], applicationActivities: nil)
        guard let scene = UIApplication.shared.connectedScenes.first as? UIWindowScene,
              let rootVC = scene.windows.first?.rootViewController else {
            return
        }

        var presenter = rootVC
        while let presented = presenter.presentedViewController {
            presenter = presented
        }

        if let popover = activityVC.popoverPresentationController {
            popover.sourceView = presenter.view
            popover.sourceRect = CGRect(
                x: presenter.view.bounds.midX,
                y: presenter.view.bounds.maxY - ShellTheme.Size.bottomBarMinHeight,
                width: 1,
                height: 1
            )
            popover.permittedArrowDirections = []
        }

        presenter.present(activityVC, animated: true)
#endif
    }

}
