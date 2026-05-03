import AppKit

extension Notification.Name {
    static let mahoSidebarRefreshRequested = Notification.Name("mahoSidebarRefreshRequested")
}

class SidebarController: NSViewController, CoreUpdateObserver {
    private enum BodyMode {
        case tabs
        case archive
        case createThemePlaceholder
    }

    private weak var containerView: NSView?
    let outlineView = SidebarOutlineView(frame: .zero)
    let dataSource = SidebarTreeDataSource()
    let scrollView = NSScrollView()
    private let bodyContainerView = NSView()
    private let archivePanelView = SidebarArchivePanelView()
    private let createThemePlaceholderView = SidebarCreateThemePlaceholderView()
    private let libraryContainerView = NSView()
    private let libraryRailView = LibraryRailView()
    private let libraryDividerView = NSView()
    private let spaceSwitcherView = SpaceSwitcherView(frame: .zero)
    private let footerContainerView = NSView()
    private let footerStackView = NSStackView()
    private let libraryButtonContainer = NSView()
    private let libraryButton = NSButton(frame: .zero)
    private let addSpaceButton = NSButton(frame: .zero)
    private let navControlRow = NSStackView()
    private let addressBar = SidebarAddressBarView()
    private let favoritesGrid = FavoritesGridView()
    private var bodyContainerTopToFavoritesConstraint: NSLayoutConstraint?
    private var bodyContainerTopToNavRowConstraint: NSLayoutConstraint?
    private var bodyContainerBottomToFooterConstraint: NSLayoutConstraint?
    private var bodyContainerBottomToContainerConstraint: NSLayoutConstraint?
    private var navControlRowTopConstraint: NSLayoutConstraint?
    private var navControlRowLeadingConstraint: NSLayoutConstraint?
    private var addressBarHeightConstraint: NSLayoutConstraint?

    private enum LayoutMetrics {
        static let defaultNavigationRowTopInset: CGFloat = 6
        static let defaultNavigationRowLeadingInset: CGFloat = 64
        static let favoritesTopSpacing: CGFloat = 8
        static let footerHorizontalInset: CGFloat = 12
        static let footerVerticalInset: CGFloat = 2
        static let footerHeight: CGFloat = 28
        static let footerControlSize: CGFloat = 24
        static let footerSpacing: CGFloat = 6
    }

    private enum NavigationOverflowMode {
        case full
        case compact
    }

    // newTabRow moved into outline view as .newTab section
    private var favoritesGridHeightConstraint: NSLayoutConstraint?
    private var leftControlGroup: NSStackView?
    private var rightControlGroup: NSStackView?
    private var sidebarToggleButton: NSButton?
    private var backButton: NSButton?
    private var forwardButton: NSButton?
    private var reloadStopButton: NSButton?
    private var aiButton: NSButton?
    private var moreButton: NSButton?
    private var navigationOverflowMode: NavigationOverflowMode?
    private var tidyTabsPopover: NSPopover?
    private weak var tidyTabsAnchorView: NSView?
    private let dragOverlayView = SidebarDropOverlayView()
    private var activeDropOverlayTarget: SidebarDropOverlayTarget = .none
    private let uiTestFavoritesRenderedOrderSeparator = "|"
    private var reloadRequestedAfterDrag = false
    private var localCustomTitles: [TabId: String] = [:]
    private var needsInitialReload = false
    private var isReloadingSidebar = false
    private var bodyMode: BodyMode = .tabs
    private var lastFocusedBodyView: NSView?
    var isDragging: Bool {
        return dragController.isDragging
    }
    var activeSpaceId: SpaceId?
    weak var browserWindowController: BrowserWindowController?
    private let dragController = SidebarDragController()

    override func loadView() {
        let container = NSView()
        container.wantsLayer = true
        container.layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
        container.setAccessibilityElement(true)
        container.setAccessibilityIdentifier("sidebar")
        containerView = container

        setupNavigationRow(in: container)
        setupAddressBar(in: container)
        favoritesGrid.translatesAutoresizingMaskIntoConstraints = false
        favoritesGrid.setAccessibilityIdentifier("sidebar.favoritesGrid")
        favoritesGrid.onTabSelected = { [weak self] tabId in
            self?.activateTab(tabId)
        }
        favoritesGrid.onFavoriteReorder = { tabId, newIndex in
            CoreUpdateDispatcher.shared.sendAndDispatch(.reorderFavorite(tabId: tabId, newIndex: newIndex))
        }
        favoritesGrid.onFavoriteAdd = { [weak self] tabId, newIndex in
            CoreUpdateDispatcher.shared.sendAndDispatch(.favoriteTab(tabId: tabId))
            guard let self else { return }
            let resolvedSpaceId = self.activeSpaceId ?? self.resolvedActiveSpaceId()
            guard let spaceId = resolvedSpaceId else { return }
            let favoriteTabs = MahoBridge.shared.getFavoriteTabs(spaceId: spaceId)
            guard let currentIndex = favoriteTabs.firstIndex(where: { $0.id == tabId }) else { return }
            if currentIndex != newIndex {
                CoreUpdateDispatcher.shared.sendAndDispatch(.reorderFavorite(tabId: tabId, newIndex: newIndex))
            }
        }
        favoritesGrid.onFavoriteRemove = { tabId in
            CoreUpdateDispatcher.shared.sendAndDispatch(.unfavoriteTab(tabId: tabId))
        }
        favoritesGrid.menuProvider = { [weak self] tab in
            self?.favoritesContextMenu(for: tab)
        }
        favoritesGrid.onDragSessionWillBegin = { [weak self] in
            self?.dragController.draggingSessionWillBegin()
        }
        favoritesGrid.onDragSessionEnded = { [weak self] in
            self?.dragController.draggingSessionEnded()
            self?.flushDeferredReloadIfNeeded()
        }
        favoritesGrid.onUITestDragStateChanged = { [weak self] isInsertionActive, insertionIndex, renderedOrder in
            self?.writeFavoritesDragUITestState(
                insertionActive: isInsertionActive,
                insertionIndex: insertionIndex,
                renderedOrder: renderedOrder
            )
        }
        container.addSubview(favoritesGrid)

        footerContainerView.translatesAutoresizingMaskIntoConstraints = false
        footerContainerView.wantsLayer = true
        footerContainerView.layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
        footerContainerView.setAccessibilityElement(true)
        footerContainerView.setAccessibilityRole(.group)
        footerContainerView.setAccessibilityIdentifier("sidebar.footer")
        container.addSubview(footerContainerView)

        footerStackView.translatesAutoresizingMaskIntoConstraints = false
        footerStackView.orientation = .horizontal
        footerStackView.alignment = .centerY
        footerStackView.distribution = .fill
        footerStackView.spacing = LayoutMetrics.footerSpacing
        footerContainerView.addSubview(footerStackView)

        configureFooterLibraryButton()
        configureFooterAddSpaceButton()

        spaceSwitcherView.translatesAutoresizingMaskIntoConstraints = false
        spaceSwitcherView.setContentHuggingPriority(.defaultLow, for: .horizontal)
        spaceSwitcherView.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        footerStackView.addArrangedSubview(libraryButtonContainer)
        footerStackView.addArrangedSubview(spaceSwitcherView)
        footerStackView.addArrangedSubview(addSpaceButton)

        bodyContainerView.translatesAutoresizingMaskIntoConstraints = false
        bodyContainerView.wantsLayer = true
        bodyContainerView.layer?.backgroundColor = NSColor.clear.cgColor
        bodyContainerView.setAccessibilityElement(true)
        bodyContainerView.setAccessibilityRole(.group)
        bodyContainerView.setAccessibilityIdentifier("sidebar.body")
        bodyContainerView.setAccessibilityLabel("Sidebar Body")
        container.addSubview(bodyContainerView)

        scrollView.documentView = outlineView
        scrollView.hasVerticalScroller = true
        scrollView.drawsBackground = false
        scrollView.autohidesScrollers = true
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.contentView.postsBoundsChangedNotifications = true
        bodyContainerView.addSubview(scrollView)

        archivePanelView.translatesAutoresizingMaskIntoConstraints = false
        archivePanelView.onRestoreArchivedTab = { [weak self] tabId in
            CoreUpdateDispatcher.shared.sendAndDispatch(.restoreArchivedTab(tabId: tabId))
            self?.setBodyMode(.tabs)
        }
        archivePanelView.onDeleteArchivedTab = { tabId in
            CoreUpdateDispatcher.shared.sendAndDispatch(.deleteArchivedTab(tabId: tabId))
        }

        createThemePlaceholderView.translatesAutoresizingMaskIntoConstraints = false
        createThemePlaceholderView.onBack = { [weak self] in
            self?.setBodyMode(.tabs)
        }
        createThemePlaceholderView.onOpenDetailedEditor = { [weak self] space in
            self?.browserWindowController?.openThemeColorEditorSheet(for: space)
        }

        libraryRailView.translatesAutoresizingMaskIntoConstraints = false
        libraryRailView.onBackPressed = { [weak self] in
            self?.setBodyMode(.tabs)
        }

        libraryDividerView.translatesAutoresizingMaskIntoConstraints = false
        libraryDividerView.wantsLayer = true
        libraryDividerView.layer?.backgroundColor = MahoAppColors.libraryRailDivider.cgColor

        libraryContainerView.translatesAutoresizingMaskIntoConstraints = false
        libraryContainerView.wantsLayer = true
        libraryContainerView.isHidden = true
        libraryContainerView.addSubview(libraryRailView)
        libraryContainerView.addSubview(libraryDividerView)
        libraryContainerView.addSubview(archivePanelView)
        libraryContainerView.addSubview(createThemePlaceholderView)
        bodyContainerView.addSubview(libraryContainerView)

        dragOverlayView.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(dragOverlayView)

        favoritesGridHeightConstraint = favoritesGrid.heightAnchor.constraint(equalToConstant: 0)
        favoritesGrid.heightConstraint = favoritesGridHeightConstraint

        navControlRowTopConstraint = navControlRow.topAnchor.constraint(equalTo: container.topAnchor, constant: LayoutMetrics.defaultNavigationRowTopInset)
        navControlRowLeadingConstraint = navControlRow.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: LayoutMetrics.defaultNavigationRowLeadingInset)
        addressBarHeightConstraint = addressBar.heightAnchor.constraint(equalToConstant: ChromeGeometry.addressBarHeight)
        bodyContainerTopToFavoritesConstraint = bodyContainerView.topAnchor.constraint(equalTo: favoritesGrid.bottomAnchor, constant: 2)
        bodyContainerTopToNavRowConstraint = bodyContainerView.topAnchor.constraint(equalTo: container.topAnchor, constant: 10)
        bodyContainerBottomToFooterConstraint = bodyContainerView.bottomAnchor.constraint(equalTo: footerContainerView.topAnchor)
        bodyContainerBottomToContainerConstraint = bodyContainerView.bottomAnchor.constraint(equalTo: container.bottomAnchor)

        NSLayoutConstraint.activate([
            navControlRowTopConstraint!,
            navControlRowLeadingConstraint!,
            navControlRow.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -ChromeGeometry.navRowTrailingInset),
            navControlRow.heightAnchor.constraint(equalToConstant: ChromeGeometry.navRowHeight),

            addressBar.topAnchor.constraint(equalTo: navControlRow.bottomAnchor, constant: ChromeGeometry.navToAddressBarSpacing),
            addressBar.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: ChromeGeometry.addressBarHorizontalInset),
            addressBar.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -ChromeGeometry.addressBarHorizontalInset),
            addressBarHeightConstraint!,

            favoritesGrid.topAnchor.constraint(equalTo: addressBar.bottomAnchor, constant: LayoutMetrics.favoritesTopSpacing),
            favoritesGrid.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            favoritesGrid.trailingAnchor.constraint(equalTo: container.trailingAnchor),

            bodyContainerTopToFavoritesConstraint!,
            bodyContainerBottomToFooterConstraint!,
            bodyContainerView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            bodyContainerView.trailingAnchor.constraint(equalTo: container.trailingAnchor),

            scrollView.topAnchor.constraint(equalTo: bodyContainerView.topAnchor),
            scrollView.leadingAnchor.constraint(equalTo: bodyContainerView.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: bodyContainerView.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: bodyContainerView.bottomAnchor),

            archivePanelView.topAnchor.constraint(equalTo: libraryContainerView.topAnchor),
            archivePanelView.leadingAnchor.constraint(equalTo: libraryDividerView.trailingAnchor),
            archivePanelView.trailingAnchor.constraint(equalTo: libraryContainerView.trailingAnchor),
            archivePanelView.bottomAnchor.constraint(equalTo: libraryContainerView.bottomAnchor),

            createThemePlaceholderView.topAnchor.constraint(equalTo: libraryContainerView.topAnchor),
            createThemePlaceholderView.leadingAnchor.constraint(equalTo: libraryContainerView.leadingAnchor),
            createThemePlaceholderView.trailingAnchor.constraint(equalTo: libraryContainerView.trailingAnchor),
            createThemePlaceholderView.bottomAnchor.constraint(equalTo: libraryContainerView.bottomAnchor),

            libraryContainerView.topAnchor.constraint(equalTo: bodyContainerView.topAnchor),
            libraryContainerView.leadingAnchor.constraint(equalTo: bodyContainerView.leadingAnchor),
            libraryContainerView.trailingAnchor.constraint(equalTo: bodyContainerView.trailingAnchor),
            libraryContainerView.bottomAnchor.constraint(equalTo: bodyContainerView.bottomAnchor),

            libraryRailView.topAnchor.constraint(equalTo: libraryContainerView.topAnchor),
            libraryRailView.leadingAnchor.constraint(equalTo: libraryContainerView.leadingAnchor),
            libraryRailView.bottomAnchor.constraint(equalTo: libraryContainerView.bottomAnchor),
            libraryRailView.widthAnchor.constraint(equalToConstant: 68),

            libraryDividerView.topAnchor.constraint(equalTo: libraryContainerView.topAnchor),
            libraryDividerView.leadingAnchor.constraint(equalTo: libraryRailView.trailingAnchor),
            libraryDividerView.bottomAnchor.constraint(equalTo: libraryContainerView.bottomAnchor),
            libraryDividerView.widthAnchor.constraint(equalToConstant: 1),

            dragOverlayView.topAnchor.constraint(equalTo: container.topAnchor),
            dragOverlayView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            dragOverlayView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            dragOverlayView.bottomAnchor.constraint(equalTo: container.bottomAnchor),

            footerContainerView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            footerContainerView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            footerContainerView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
            footerContainerView.heightAnchor.constraint(equalToConstant: LayoutMetrics.footerHeight),

            footerStackView.leadingAnchor.constraint(equalTo: footerContainerView.leadingAnchor, constant: LayoutMetrics.footerHorizontalInset),
            footerStackView.trailingAnchor.constraint(equalTo: footerContainerView.trailingAnchor, constant: -LayoutMetrics.footerHorizontalInset),
            footerStackView.topAnchor.constraint(equalTo: footerContainerView.topAnchor, constant: LayoutMetrics.footerVerticalInset),
            footerStackView.bottomAnchor.constraint(equalTo: footerContainerView.bottomAnchor, constant: -LayoutMetrics.footerVerticalInset),

            spaceSwitcherView.heightAnchor.constraint(equalToConstant: LayoutMetrics.footerControlSize),
            favoritesGridHeightConstraint!,
        ])

        self.view = container
    }

    func refreshAppearance() {
        let appearance = view.window?.effectiveAppearance ?? view.effectiveAppearance
        appearance.performAsCurrentDrawingAppearance {
            containerView?.layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
            bodyContainerView.layer?.backgroundColor = NSColor.clear.cgColor
            footerContainerView.layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
        }
        libraryButton.contentTintColor = MahoAppColors.spaceControlTint
        addressBar.refreshAppearance()
        favoritesGrid.refreshAppearance()
        outlineView.refreshAppearance()
        archivePanelView.refreshAppearance()
        libraryRailView.refreshAppearance()
        libraryDividerView.layer?.backgroundColor = MahoAppColors.libraryRailDivider.cgColor
        spaceSwitcherView.refreshAppearance()
        updateBodyModeUI()
        updateSidebarChromeTint()
    }

    override func viewDidLoad() {
        super.viewDidLoad()

        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("MainColumn"))
        column.width = 160
        outlineView.addTableColumn(column)
        outlineView.outlineTableColumn = column
        outlineView.setAccessibilityIdentifier("sidebar.outline")
        outlineView.setAccessibilityLabel("Tab List")
        outlineView.setAccessibilityRole(.outline)

        dataSource.controller = self
        dataSource.dragController = dragController
        dragController.controller = self
        outlineView.dataSource = dataSource
        outlineView.delegate = dataSource

        outlineView.registerForDraggedTypes([SidebarDragController.dragType])

        CoreUpdateDispatcher.shared.register(self)
        NotificationCenter.default.addObserver(self, selector: #selector(handleSidebarRefreshRequest), name: .mahoSidebarRefreshRequested, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(handleLiveFolderUpdated), name: .liveFolderUpdated, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(handleScrollBoundsChanged), name: NSView.boundsDidChangeNotification, object: scrollView.contentView)
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(activeSpaceThemeDidChange),
            name: .mahoActiveSpaceThemeChanged,
            object: nil
        )

        reloadSidebar()
        updateSidebarChromeTint()
    }

    override func viewDidLayout() {
        super.viewDidLayout()
        updateNavigationOverflowModeIfNeeded()
        updateFavoritesGridHeight()
        updateActiveDropOverlay()
    }

    @objc private func activeSpaceThemeDidChange() {
        updateSidebarChromeTint()
    }

    private func updateSidebarChromeTint() {
        guard let tint = SpaceThemeManager.shared.chromeTint(alpha: 0.06) else {
            containerView?.layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
            return
        }
        let baseColor = MahoAppColors.sidebarBackground
        let blended = baseColor.blended(withFraction: 0.15, of: tint) ?? baseColor
        containerView?.layer?.backgroundColor = blended.cgColor
    }

    override func viewDidAppear() {
        super.viewDidAppear()
        if needsInitialReload {
            needsInitialReload = false
            reloadSidebar()
        }
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
        NotificationCenter.default.removeObserver(self, name: .mahoSidebarRefreshRequested, object: nil)
        NotificationCenter.default.removeObserver(self, name: .liveFolderUpdated, object: nil)
        NotificationCenter.default.removeObserver(self, name: NSView.boundsDidChangeNotification, object: scrollView.contentView)
        NotificationCenter.default.removeObserver(self, name: .mahoActiveSpaceThemeChanged, object: nil)
    }

    // MARK: - Reload

    func focusAddressBar() {
        addressBar.enterEditingState()
    }

    func reloadSidebar() {
        // RULE 9: Skip reload during drag
        guard !isDragging else { return }
        guard !isReloadingSidebar else { return }
        isReloadingSidebar = true
        defer { isReloadingSidebar = false }

        let spaces = canonicalSpaces()
        let spaceId = resolvedActiveSpaceId(from: spaces)
        guard let spaceId = spaceId else { return }
        self.activeSpaceId = spaceId
        if let space = spaces.first(where: { $0.id == spaceId }) {
            SpaceThemeManager.shared.setActiveSpace(color: space.color, theme: space.theme)
        }

        let favoriteTabs = MahoBridge.shared.getFavoriteTabs(spaceId: spaceId)
        let favoriteTabIds = Set(favoriteTabs.map { $0.id })
        let pinnedTabs = MahoBridge.shared.getPinnedTabs(spaceId: spaceId).filter {
            !favoriteTabIds.contains($0.id)
        }
        let normalTabs = MahoBridge.shared.getSpaceTabs(spaceId: spaceId).filter {
            !$0.isPinned && !$0.isFavorite && !favoriteTabIds.contains($0.id)
        }
        let folders = MahoBridge.shared.getFolderViewModels(spaceId: spaceId)
        AppDelegate.appendUITestDebugLog("Sidebar.reloadSidebar spaceId=\(spaceId) favorites=\(favoriteTabs.count) pinned=\(pinnedTabs.count) normal=\(normalTabs.count) folders=\(folders.map { $0.tabIds.count })")
        if ProcessInfo.processInfo.arguments.contains("--ui-testing"), favoriteTabs.count >= 6 {
            AppDelegate.writeUITestState(["favoritesSeededReady": "true"])
        }
        let snapshot = SidebarSnapshot.build(
            pinnedTabs: pinnedTabs,
            regularTabs: normalTabs,
            folders: folders
        )

        let liveFolderSections = snapshot.isTablessSparseState ? [] : buildLiveFolderSections()
        let finalSnapshot = SidebarSnapshot(
            rootSections: snapshot.rootSections + liveFolderSections,
            isTablessSparseState: snapshot.isTablessSparseState
        )

        dataSource.snapshot = finalSnapshot
        let visibleFavoriteTabs = Array(favoriteTabs.prefix(FavoritesGridView.maxVisibleFavorites))
        favoritesGrid.configure(with: visibleFavoriteTabs, activeTabId: browserWindowController?.activeTabId)
        writeFavoritesDragUITestStateSnapshot()
        updateFavoritesGridHeight()
        archivePanelView.reloadArchivedTabs(spaceId: spaceId)
        updateBodyModeUI()

        // RULE 12: Always full reloadData, never reloadItem
        // RULE 13: Never beginUpdates/endUpdates
        guard outlineView.window != nil else {
            needsInitialReload = true
            return
        }

        outlineView.reloadData()
        updateSidebarAccessibilityCount(favoriteTabs: favoriteTabs, pinnedTabs: pinnedTabs, normalTabs: normalTabs, folders: folders)

        for section in finalSnapshot.rootSections {
            if section.sectionId != .separator && section.sectionId != .newTab {
                outlineView.expandItem(section)
            }
            for child in section.children {
                if let folder = child as? SectionNode, folder.isFolder, folder.isExpanded {
                    outlineView.expandItem(folder)
                }
            }
        }

        if let activeTabId = browserWindowController?.activeTabId {
            revealActiveTabIfNeeded(tabId: activeTabId)
        }

        if let activeTabId = browserWindowController?.activeTabId {
            let tabs = MahoBridge.shared.getTabViewModels()
            if let activeTab = tabs.first(where: { $0.id == activeTabId }) {
                var favicon: NSImage? = nil
                if let faviconData = activeTab.favicon {
                    favicon = NSImage(data: Data(faviconData.data))
                }
                addressBar.update(url: activeTab.url, favicon: favicon, tabId: activeTabId)
            }
        } else {
            addressBar.clearActiveTab()
        }

        refreshNavigationControls()
        fputs("DIAG: reloadSidebar: COMPLETE\n", stderr)
    }

    private func updateSidebarAccessibilityCount(
        favoriteTabs: [TabViewModel],
        pinnedTabs: [TabViewModel],
        normalTabs: [TabViewModel],
        folders: [FolderViewModel]
    ) {
        let folderTabIds = Set(folders.flatMap(\.tabIds))
        let visibleCount = favoriteTabs.count + pinnedTabs.count + normalTabs.filter { !folderTabIds.contains($0.id) }.count + folders.reduce(0) { $0 + $1.tabIds.count }
        AppDelegate.appendUITestDebugLog("Sidebar.updateSidebarAccessibilityCount visibleCount=\(visibleCount)")
        AppDelegate.writeUITestState([
            "sidebarTabCount": String(visibleCount)
        ])
    }

    private func revealActiveTabIfNeeded(tabId: TabId) {
        guard let row = rowForTab(tabId) else { return }
        outlineView.scrollRowToVisible(row)
        outlineView.layoutSubtreeIfNeeded()
    }

    private func rowForTab(_ tabId: TabId) -> Int? {
        let totalRows = outlineView.numberOfRows
        guard totalRows > 0 else { return nil }
        for row in 0..<totalRows {
            if let tab = outlineView.item(atRow: row) as? TabNode, tab.tabId == tabId {
                return row
            }
        }
        return nil
    }


    func activateTab(_ tabId: TabId) {
        CoreUpdateDispatcher.shared.sendAndDispatch(.activateTab(tabId: tabId))
        browserWindowController?.activateTabLocally(tabId)
    }

    func closeTab(_ tabId: TabId) {
        CoreUpdateDispatcher.shared.sendAndDispatch(.closeTab(tabId: tabId))
    }

    func handleTabTitleChanged(tabId: TabId, newTitle: String) {
        localCustomTitles[tabId] = newTitle
        MahoBridge.shared.setTabCustomTitle(tabId: tabId, title: newTitle)
    }

    func localCustomTitle(for tabId: TabId) -> String? {
        return localCustomTitles[tabId]
    }

    func toggleFolder(_ folder: SectionNode) {
        guard folder.isFolder,
              let spaceId = activeSpaceId,
              let folderId = folder.folderId
        else {
            return
        }

        CoreUpdateDispatcher.shared.sendAndDispatch(.toggleFolderExpanded(spaceId: spaceId, folderId: folderId))
        reloadSidebar()
    }

    @objc private func handleSidebarRefreshRequest() {
        reloadSidebar()
    }

    @objc private func handleLiveFolderUpdated() {
        reloadSidebar()
    }

    @objc private func handleScrollBoundsChanged() {
        updateActiveDropOverlay()
    }

    private func buildLiveFolderSections() -> [SectionNode] {
        let liveFolders = LiveFolderManager.shared.getFolders()
        guard !liveFolders.isEmpty else { return [] }

        var sections: [SectionNode] = []
        sections.append(SectionNode(sectionId: .separator, title: ""))

        for folder in liveFolders {
            let items = LiveFolderManager.shared.getItems(for: folder.id)
            let children: [AnyObject] = items.map { item in
                LiveFolderItemNode(item: item)
            }
            let section = SectionNode(
                sectionId: .regular,
                title: folder.name,
                children: children,
                isFolder: true,
                isExpanded: true
            )
            sections.append(section)
        }
        return sections
    }

    @objc func newTab() {
        let spaceId = activeSpaceId ?? MahoBridge.shared.getActiveSpaceId()
        guard let spaceId = spaceId else { return }
        activeSpaceId = spaceId
        (NSApp.delegate as? AppDelegate)?.showCommandBarForNewTab(spaceId: spaceId)
    }

    func showContextMenu(for item: Any, at point: NSPoint, in view: NSView) {
        if let menu = contextMenu(forSidebarItem: item) {
            NSMenu.popUpContextMenu(menu, with: NSApp.currentEvent!, for: view)
        }
    }

    func contextMenu(forSidebarItem item: Any) -> NSMenu? {
        let spaces = canonicalSpaces()
        guard let spaceId = resolvedActiveSpaceId(from: spaces) else { return nil }

        if let tab = item as? TabNode {
            let tabVM = TabViewModel(
                id: tab.tabId,
                spaceId: spaceId,
                title: tab.title,
                customTitle: nil,
                url: tab.url,
                favicon: tab.favicon,
                isLoading: tab.isLoading,
                isPinned: tab.isPinned,
                isFavorite: tab.isFavorite,
                favoriteOrder: nil,
                isMuted: tab.isMuted,
                isPlayingAudio: tab.isPlayingAudio,
                lifecycleState: tab.lifecycleState,
                children: [],
                createdAt: MahoDateTime(""),
                lastActiveAt: MahoDateTime("")
            )
            return TabContextMenu.menu(for: tabVM, spaceId: spaceId, spaces: spaces)
        }

        if let section = item as? SectionNode, section.isFolder {
            let folders = MahoBridge.shared.getFolderViewModels(spaceId: spaceId)
            guard let folder = folders.first(where: { $0.id == section.folderId }) else { return nil }
            return FolderContextMenu.menu(for: folder, spaceId: spaceId)
        }

        guard item is SectionNode,
              let space = spaces.first(where: { $0.id == spaceId }) else {
            return nil
        }
        return SpaceContextMenu.menu(for: space)
    }

    func backgroundContextMenu() -> NSMenu? {
        let spaces = canonicalSpaces()
        guard let spaceId = resolvedActiveSpaceId(from: spaces),
              let space = spaces.first(where: { $0.id == spaceId }) else {
            return nil
        }
        return SpaceContextMenu.menu(for: space)
    }

    // MARK: - CoreUpdateObserver

    func handleCoreUpdate(_ update: CoreUpdate) {
        DispatchQueue.main.async { [weak self] in
            self?.processCoreUpdate(update)
        }
    }

    private var pendingReloadWork: DispatchWorkItem?

    private func scheduleReload() {
        pendingReloadWork?.cancel()
        if isDragging {
            reloadRequestedAfterDrag = true
            return
        }

        let workItem = DispatchWorkItem { [weak self] in
            self?.reloadSidebar()
        }
        pendingReloadWork = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: workItem)
    }

    func requestReload() {
        if isDragging {
            reloadRequestedAfterDrag = true
            return
        }
        scheduleReload()
    }

    func flushDeferredReloadIfNeeded() {
        guard reloadRequestedAfterDrag else { return }
        reloadRequestedAfterDrag = false
        pendingReloadWork?.cancel()
        pendingReloadWork = nil
        reloadSidebar()
    }

    private func processCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .fullState,
             .tabCreated, .tabClosed, .tabUpdated, .tabOrderChanged,
             .folderCreated, .folderDeleted, .folderUpdated, .folderOrderChanged,
               .tabFaviconChanged, .tabAudioStateChanged,
               .spaceCreated, .spaceDeleted, .spaceUpdated,
               .spaceOrderChanged, .spaceConfigUpdated,
               .tabLifecycleChanged, .tabsMigrated:
            scheduleReload()
        case .navigationStateChanged(let tabId, let url, _, let canGoBack, let canGoForward, let isLoading, let progress, let isSecure):
            if tabId == browserWindowController?.activeTabId {
                refreshNavigationControls(canGoBack: canGoBack, canGoForward: canGoForward, isLoading: isLoading)
                updateAddressBar(tabId: tabId, url: url.value, isSecure: isSecure)
                addressBar.updateProgress(progress, isLoading: isLoading)
            }
            scheduleReload()
        case .activeSpaceChanged(let spaceId, _):
            activeSpaceId = spaceId
            setBodyMode(.tabs)
            // Bypass debounce — space switch must reflect immediately in sidebar
            pendingReloadWork?.cancel()
            pendingReloadWork = nil
            reloadSidebar()
        case .activeProfileChanged:
            activeSpaceId = nil
            setBodyMode(.tabs)
            scheduleReload()
        case .tidyTabsReady(let spaceId, let folders):
            showTidyTabsPopover(spaceId: spaceId, folders: folders, anchorView: resolvedTidyTabsAnchorView())
        case .commandBarResults(let suggestions):
            addressBar.updateSuggestions(suggestions)
        default:
            break
        }
    }

    private func canonicalSpaces() -> [SpaceViewModel] {
        MahoBridge.shared.getSpaceViewModels().sortedByCanonicalOrder()
    }

    private func resolvedActiveSpaceId(from spaces: [SpaceViewModel]? = nil) -> SpaceId? {
        let spaces = spaces ?? canonicalSpaces()

        if let globalActiveSpaceId = MahoBridge.shared.getActiveSpaceId(),
           spaces.contains(where: { $0.id == globalActiveSpaceId }) {
            activeSpaceId = globalActiveSpaceId
            return globalActiveSpaceId
        }

        if let flaggedActiveSpaceId = spaces.first(where: { $0.isActive })?.id {
            activeSpaceId = flaggedActiveSpaceId
            return flaggedActiveSpaceId
        }

        if let currentActiveSpaceId = activeSpaceId,
           spaces.contains(where: { $0.id == currentActiveSpaceId }) {
            return currentActiveSpaceId
        }

        let fallbackSpaceId = spaces.first?.id
        activeSpaceId = fallbackSpaceId
        return fallbackSpaceId
    }

    private func setupNavigationRow(in container: NSView) {
        navControlRow.translatesAutoresizingMaskIntoConstraints = false
        navControlRow.orientation = .horizontal
        navControlRow.spacing = 0
        navControlRow.alignment = .centerY
        navControlRow.distribution = .fill

        let leftControlGroup = makeNavigationControlGroup()
        let rightControlGroup = makeNavigationControlGroup()
        let spacerView = NSView()
        spacerView.translatesAutoresizingMaskIntoConstraints = false
        spacerView.setContentHuggingPriority(.defaultLow, for: .horizontal)
        spacerView.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let sidebarToggleButton = makeNavigationButton(symbolName: "sidebar.left", toolTip: "Toggle Sidebar", action: #selector(toggleSidebar(_:)), accessibilityIdentifier: "sidebar.nav.toggle")
        let backButton = makeNavigationButton(symbolName: "chevron.left", toolTip: "Back", action: #selector(goBack(_:)), accessibilityIdentifier: "sidebar.nav.back")
        let forwardButton = makeNavigationButton(symbolName: "chevron.right", toolTip: "Forward", action: #selector(goForward(_:)), accessibilityIdentifier: "sidebar.nav.forward")
        let reloadStopButton = makeNavigationButton(symbolName: "arrow.clockwise", toolTip: "Reload", action: #selector(reloadOrStop(_:)), accessibilityIdentifier: "sidebar.nav.reload")
        let aiButton = makeNavigationButton(symbolName: "sparkles", toolTip: "AI Panel", action: #selector(toggleAIPanel(_:)), accessibilityIdentifier: "sidebar.nav.ai")
        let moreButton = makeNavigationButton(symbolName: "ellipsis", toolTip: "More", action: #selector(showOverflowMenu(_:)), accessibilityIdentifier: "sidebar.nav.more")

        sidebarToggleButton.setAccessibilityLabel("Toggle Sidebar")
        backButton.setAccessibilityLabel("Go Back")
        forwardButton.setAccessibilityLabel("Go Forward")
        reloadStopButton.setAccessibilityLabel("Reload Page")
        aiButton.setAccessibilityLabel("Toggle AI Panel")
        moreButton.setAccessibilityLabel("More Navigation Actions")
        if aiButton.image == nil {
            aiButton.title = "AI"
            aiButton.font = NSFont.systemFont(ofSize: 10, weight: .medium)
        }

        self.leftControlGroup = leftControlGroup
        self.rightControlGroup = rightControlGroup
        self.sidebarToggleButton = sidebarToggleButton
        self.backButton = backButton
        self.forwardButton = forwardButton
        self.reloadStopButton = reloadStopButton
        self.aiButton = aiButton
        self.moreButton = moreButton

        leftControlGroup.addArrangedSubview(sidebarToggleButton)
        rightControlGroup.addArrangedSubview(backButton)
        rightControlGroup.addArrangedSubview(forwardButton)
        rightControlGroup.addArrangedSubview(reloadStopButton)
        rightControlGroup.addArrangedSubview(aiButton)
        rightControlGroup.addArrangedSubview(moreButton)

        [leftControlGroup, spacerView, rightControlGroup].forEach {
            navControlRow.addArrangedSubview($0)
        }
        navControlRow.setCustomSpacing(ChromeGeometry.navGroupGap, after: leftControlGroup)

        applyNavigationOverflowMode(.full)
        container.addSubview(navControlRow)
    }

    private func setupAddressBar(in container: NSView) {
        addressBar.translatesAutoresizingMaskIntoConstraints = false
        addressBar.onUrlSubmitted = { urlString, tabId in
            let url = MahoUrl(urlString)
            CoreUpdateDispatcher.shared.sendAndDispatch(.navigateTo(tabId: tabId, url: url))
        }
        addressBar.onCommandBarRequested = { [weak self] in
            self?.openCommandBar()
        }
        addressBar.onSiteControlRequested = { [weak self] anchorView in
            self?.browserWindowController?.showSiteControlPopover(relativeTo: anchorView)
        }
        container.addSubview(addressBar)
    }

    func alignNavigationRow(toTrafficLights trafficLightsFrame: NSRect) {
        let navRowLeading = floor(trafficLightsFrame.maxX + ChromeGeometry.navToTrafficLightGap)
        let navRowTop = floor(view.bounds.height - trafficLightsFrame.midY - (ChromeGeometry.navRowHeight / 2) - 1)
        navControlRowLeadingConstraint?.constant = navRowLeading
        navControlRowTopConstraint?.constant = navRowTop
        view.layoutSubtreeIfNeeded()
    }

    // setupNewTabRow removed — New Tab is now a section row inside the outline view

    private func makeNavigationControlGroup() -> NSStackView {
        let stackView = NSStackView()
        stackView.translatesAutoresizingMaskIntoConstraints = false
        stackView.orientation = .horizontal
        stackView.spacing = ChromeGeometry.navClusterSpacing
        stackView.alignment = .centerY
        stackView.distribution = .fill
        stackView.setContentHuggingPriority(.required, for: .horizontal)
        stackView.setContentCompressionResistancePriority(.required, for: .horizontal)
        return stackView
    }

    private func makeNavigationButton(symbolName: String, toolTip: String, action: Selector, accessibilityIdentifier: String? = nil) -> NSButton {
        let button = NSButton(frame: .zero)
        button.translatesAutoresizingMaskIntoConstraints = false
        button.isBordered = false
        button.bezelStyle = .regularSquare
        button.image = configuredSymbol(named: symbolName, pointSize: ChromeGeometry.navButtonSymbolPointSize, accessibilityDescription: toolTip)
        button.contentTintColor = MahoAppColors.primaryText.withAlphaComponent(0.68)
        button.imageScaling = .scaleProportionallyDown
        button.target = self
        button.action = action
        button.toolTip = toolTip
        button.focusRingType = .exterior
        button.setAccessibilityRole(.button)
        if let accessibilityIdentifier = accessibilityIdentifier {
            button.setAccessibilityIdentifier(accessibilityIdentifier)
        }
        NSLayoutConstraint.activate([
            button.widthAnchor.constraint(equalToConstant: ChromeGeometry.navButtonSize),
            button.heightAnchor.constraint(equalToConstant: ChromeGeometry.navButtonSize),
        ])
        return button
    }

    private func configureFooterLibraryButton() {
        libraryButtonContainer.translatesAutoresizingMaskIntoConstraints = false
        libraryButtonContainer.wantsLayer = true
        libraryButtonContainer.layer?.cornerRadius = 12
        libraryButtonContainer.layer?.borderWidth = 1
        libraryButtonContainer.setContentHuggingPriority(.required, for: .horizontal)
        libraryButtonContainer.setContentCompressionResistancePriority(.required, for: .horizontal)

        libraryButton.translatesAutoresizingMaskIntoConstraints = false
        libraryButton.isBordered = false
        libraryButton.bezelStyle = .regularSquare
        libraryButton.image = configuredSymbol(named: "archivebox", pointSize: 12, accessibilityDescription: "Archive")
        libraryButton.contentTintColor = MahoAppColors.spaceControlTint
        libraryButton.imageScaling = .scaleProportionallyDown
        libraryButton.target = self
        libraryButton.action = #selector(toggleArchiveModeFromFooter(_:))
        libraryButton.toolTip = "Archive"
        libraryButton.focusRingType = .exterior
        libraryButton.setAccessibilityRole(.button)
        libraryButton.setAccessibilityIdentifier("sidebar.footer.archive")
        libraryButton.setAccessibilityLabel("Toggle Archive Panel")
        libraryButton.setContentHuggingPriority(.required, for: .horizontal)
        libraryButton.setContentCompressionResistancePriority(.required, for: .horizontal)
        libraryButtonContainer.addSubview(libraryButton)

        NSLayoutConstraint.activate([
            libraryButtonContainer.widthAnchor.constraint(equalToConstant: LayoutMetrics.footerControlSize),
            libraryButtonContainer.heightAnchor.constraint(equalToConstant: LayoutMetrics.footerControlSize),
            libraryButton.centerXAnchor.constraint(equalTo: libraryButtonContainer.centerXAnchor),
            libraryButton.centerYAnchor.constraint(equalTo: libraryButtonContainer.centerYAnchor),
            libraryButton.widthAnchor.constraint(equalToConstant: LayoutMetrics.footerControlSize),
            libraryButton.heightAnchor.constraint(equalToConstant: LayoutMetrics.footerControlSize),
        ])
    }

    private func configureFooterAddSpaceButton() {
        addSpaceButton.translatesAutoresizingMaskIntoConstraints = false
        addSpaceButton.isBordered = false
        addSpaceButton.bezelStyle = .regularSquare
        addSpaceButton.image = configuredSymbol(named: "plus", pointSize: 12, accessibilityDescription: "New")
        addSpaceButton.contentTintColor = MahoAppColors.spaceControlTint
        addSpaceButton.imageScaling = .scaleProportionallyDown
        addSpaceButton.target = spaceSwitcherView
        addSpaceButton.action = #selector(SpaceSwitcherView.handleAddButtonClick(_:))
        addSpaceButton.toolTip = "New"
        addSpaceButton.focusRingType = .exterior
        addSpaceButton.setAccessibilityRole(.button)
        addSpaceButton.setAccessibilityIdentifier("sidebar.space.add")
        addSpaceButton.setAccessibilityLabel("New")
        addSpaceButton.setContentHuggingPriority(.required, for: .horizontal)
        addSpaceButton.setContentCompressionResistancePriority(.required, for: .horizontal)
        NSLayoutConstraint.activate([
            addSpaceButton.widthAnchor.constraint(equalToConstant: LayoutMetrics.footerControlSize),
            addSpaceButton.heightAnchor.constraint(equalToConstant: LayoutMetrics.footerControlSize),
        ])
    }

    private func refreshNavigationControls(canGoBack: Bool? = nil, canGoForward: Bool? = nil, isLoading: Bool? = nil) {
        let hasActiveTab = browserWindowController?.activeTabId != nil
        let canGoBack = canGoBack ?? browserWindowController?.canGoBackForActiveTab() ?? false
        let canGoForward = canGoForward ?? browserWindowController?.canGoForwardForActiveTab() ?? false
        let isLoading = isLoading ?? activeTabIsLoading()

        backButton?.isEnabled = hasActiveTab && canGoBack
        forwardButton?.isEnabled = hasActiveTab && canGoForward
        reloadStopButton?.isEnabled = hasActiveTab
        reloadStopButton?.image = configuredSymbol(
            named: isLoading ? "xmark" : "arrow.clockwise",
            pointSize: 12,
            accessibilityDescription: isLoading ? "Stop" : "Reload"
        )
        reloadStopButton?.toolTip = isLoading ? "Stop" : "Reload"
        reloadStopButton?.setAccessibilityLabel(isLoading ? "Stop Loading" : "Reload Page")
        moreButton?.isEnabled = true
        moreButton?.alphaValue = 0.94
        backButton?.alphaValue = (backButton?.isEnabled == true) ? 0.94 : 0.4
        forwardButton?.alphaValue = (forwardButton?.isEnabled == true) ? 0.94 : 0.4
        reloadStopButton?.alphaValue = (reloadStopButton?.isEnabled == true) ? 0.94 : 0.4
        sidebarToggleButton?.alphaValue = 0.84
        aiButton?.alphaValue = 0.9
    }

    private func updateAddressBar(tabId: TabId, url: String, isSecure: Bool = true) {
        var favicon: NSImage? = nil
        let tabs = MahoBridge.shared.getTabViewModels()
        if let tab = tabs.first(where: { $0.id == tabId }), let faviconData = tab.favicon {
            favicon = NSImage(data: Data(faviconData.data))
        }
        addressBar.update(url: url, favicon: favicon, tabId: tabId, isSecure: isSecure)
    }

    private func activeTabIsLoading() -> Bool {
        guard let tabId = browserWindowController?.activeTabId else { return false }
        return MahoBridge.shared.getTabViewModels().first(where: { $0.id == tabId })?.isLoading ?? false
    }

    private func updateNavigationOverflowModeIfNeeded() {
        let mode: NavigationOverflowMode = view.bounds.width <= ChromeGeometry.compactWidthThreshold ? .compact : .full
        guard navigationOverflowMode != mode else { return }
        applyNavigationOverflowMode(mode)
    }

    private func applyNavigationOverflowMode(_ mode: NavigationOverflowMode) {
        navigationOverflowMode = mode

        switch mode {
        case .full:
            reloadStopButton?.isHidden = false
            aiButton?.isHidden = false
            moreButton?.isHidden = true
            leftControlGroup?.isHidden = false
        case .compact:
            reloadStopButton?.isHidden = true
            aiButton?.isHidden = true
            moreButton?.isHidden = false
            leftControlGroup?.isHidden = true
        }
    }

    private func makeOverflowMenu() -> NSMenu {
        let menu = NSMenu()
        let isLoading = activeTabIsLoading()
        let reloadOrStopTitle = isLoading ? "Stop" : "Reload"
        let reloadOrStopItem = NSMenuItem(
            title: reloadOrStopTitle,
            action: #selector(reloadOrStop(_:)),
            keyEquivalent: ""
        )
        reloadOrStopItem.target = self
        reloadOrStopItem.image = configuredSymbol(
            named: isLoading ? "xmark" : "arrow.clockwise",
            pointSize: 12,
            accessibilityDescription: reloadOrStopTitle
        )
        reloadOrStopItem.isEnabled = browserWindowController?.activeTabId != nil
        menu.addItem(reloadOrStopItem)

        let toggleSidebarItem = NSMenuItem(
            title: "Toggle Sidebar",
            action: #selector(toggleSidebar(_:)),
            keyEquivalent: ""
        )
        toggleSidebarItem.target = self
        menu.addItem(toggleSidebarItem)

        let toggleAIItem = NSMenuItem(
            title: "Toggle AI Panel",
            action: #selector(toggleAIPanel(_:)),
            keyEquivalent: ""
        )
        toggleAIItem.target = self
        toggleAIItem.image = configuredSymbol(
            named: "sparkles",
            pointSize: 12,
            accessibilityDescription: "Toggle AI Panel"
        )
        menu.addItem(toggleAIItem)
        return menu
    }

    @objc private func showOverflowMenu(_ sender: Any?) {
        refreshNavigationControls()
        guard let button = moreButton else { return }
        let menu = makeOverflowMenu()
        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: button.bounds.maxY), in: button)
    }

    private func openCommandBar() {
        if let appDelegate = NSApp.delegate as? AppDelegate {
            appDelegate.focusURLBar()
        }
    }

    private func updateFavoritesGridHeight() {
        let availableWidth = favoritesGrid.bounds.width > 0 ? favoritesGrid.bounds.width : view.bounds.width
        favoritesGridHeightConstraint?.constant = FavoritesGridView.height(
            for: favoritesGrid.favoriteCount,
            availableWidth: availableWidth
        )
    }

    private func writeFavoritesDragUITestStateSnapshot() {
        writeFavoritesDragUITestState(
            insertionActive: favoritesGrid.uiTestDragInsertionStateIsActive,
            insertionIndex: favoritesGrid.uiTestDragInsertionIndex,
            renderedOrder: favoritesGrid.uiTestRenderedFavoriteTabIDs
        )
    }

    private func writeFavoritesDragUITestState(
        insertionActive: Bool,
        insertionIndex: Int?,
        renderedOrder: [TabId]
    ) {
        var state: [String: String] = [
            "favoritesDragInsertionActive": insertionActive ? "true" : "false",
            "favoritesDragInsertionIndex": insertionIndex.map(String.init) ?? "",
            "favoritesRenderedOrder": renderedOrder.joined(separator: uiTestFavoritesRenderedOrderSeparator),
            "favoritesOverlayActive": activeDropOverlayTarget == .favorites ? "true" : "false"
        ]
        if renderedOrder.count >= 6 {
            state["favoritesSeededReady"] = "true"
        }
        AppDelegate.writeUITestState(state)
    }

    func showFavoritesDropOverlay() {
        guard view.window != nil, favoritesGrid.window != nil else { return }
        activeDropOverlayTarget = .favorites
        updateActiveDropOverlay()
        writeFavoritesDragUITestStateSnapshot()
    }

    func showSectionDropOverlay(for sectionId: SectionID) {
        guard view.window != nil, outlineView.window != nil else { return }
        switch sectionId {
        case .pinned:
            activeDropOverlayTarget = .pinned
        case .regular:
            activeDropOverlayTarget = .regular
        default:
            return
        }
        updateActiveDropOverlay()
    }

    func clearFavoritesDropOverlay() {
        guard activeDropOverlayTarget == .favorites else { return }
        clearSidebarDropOverlay()
        writeFavoritesDragUITestStateSnapshot()
    }

    private func setFavoritesDropOverlayVisible(_ isVisible: Bool) {
        if isVisible {
            showFavoritesDropOverlay()
        } else {
            clearFavoritesDropOverlay()
        }
    }

    func clearSidebarDropOverlay() {
        activeDropOverlayTarget = .none
        dragOverlayView.highlightRect = nil
        writeFavoritesDragUITestStateSnapshot()
    }

    func handleOutlineDraggingExited() {
        dragController.draggingExited(outlineView: outlineView)
    }

    func beginFolderDragSession() {
        dragController.draggingSessionWillBegin()
    }

    func endFolderDragSession() {
        dragController.draggingSessionEnded()
    }

    private func updateActiveDropOverlay() {
        guard let rect = overlayRect(for: activeDropOverlayTarget) else {
            dragOverlayView.highlightRect = nil
            return
        }
        dragOverlayView.highlightRect = rect
    }

    private func overlayRect(for target: SidebarDropOverlayTarget) -> NSRect? {
        switch target {
        case .none:
            return nil
        case .favorites:
            return visibleFavoritesOverlayRect()
        case .pinned:
            return visibleSectionOverlayRect(for: .pinned)
        case .regular:
            return visibleSectionOverlayRect(for: .regular)
        }
    }

    private func visibleFavoritesOverlayRect() -> NSRect? {
        guard view.window != nil, favoritesGrid.window != nil else { return nil }
        let rect = favoritesGrid.convert(favoritesGrid.bounds, to: view).intersection(view.bounds)
        guard rect.width > 0, rect.height > 0 else { return nil }
        return rect.insetBy(dx: 2, dy: 1)
    }

    private func visibleSectionOverlayRect(for sectionId: SectionID) -> NSRect? {
        guard view.window != nil, outlineView.window != nil else { return nil }
        let rootSections = dataSource.snapshot.rootSections
        guard let snapshotIndex = rootSections.firstIndex(where: { $0.sectionId == sectionId }) else {
            return nil
        }

        let section = rootSections[snapshotIndex]
        let sectionRow = outlineView.row(forItem: section)
        guard sectionRow >= 0 else { return nil }

        let sectionHeaderRect = outlineView.rect(ofRow: sectionRow)
        var sectionBandMaxY = outlineView.bounds.maxY

        if snapshotIndex + 1 < rootSections.count {
            for nextIndex in (snapshotIndex + 1)..<rootSections.count {
                let nextSection = rootSections[nextIndex]
                let nextRow = outlineView.row(forItem: nextSection)
                if nextRow >= 0 {
                    sectionBandMaxY = outlineView.rect(ofRow: nextRow).minY
                    break
                }
            }
        }

        let bandRect = NSRect(
            x: outlineView.visibleRect.minX,
            y: sectionHeaderRect.minY,
            width: outlineView.visibleRect.width,
            height: max(sectionHeaderRect.height, sectionBandMaxY - sectionHeaderRect.minY)
        ).intersection(outlineView.visibleRect)

        guard bandRect.width > 0, bandRect.height > 0 else { return nil }

        let containerRect = outlineView.convert(bandRect, to: view).intersection(view.bounds)
        guard containerRect.width > 0, containerRect.height > 0 else { return nil }

        return containerRect.insetBy(dx: 2, dy: 1)
    }

    func favoritesContextMenu(for tab: TabViewModel) -> NSMenu? {
        let spaces = canonicalSpaces()
        guard !spaces.isEmpty else { return nil }

        let allTabs = MahoBridge.shared.getTabViewModels()
        let resolvedTab = allTabs.first(where: { $0.id == tab.id }) ?? tab
        let activeTab = browserWindowController?.activeTabId.flatMap { activeTabId in
            allTabs.first(where: { $0.id == activeTabId })
        }

        return FavoritesContextMenu.menu(for: .init(
            tab: resolvedTab,
            spaces: spaces,
            currentActiveTab: activeTab,
            window: view.window,
            onRename: { [weak self] newTitle in
                self?.handleTabTitleChanged(tabId: resolvedTab.id, newTitle: newTitle)
            },
            onUpdateCustomization: { customization in
                FavoriteTileCustomizationStore.shared.setCustomization(customization, for: resolvedTab.id)
            },
            onRefreshRequested: { [weak self] in
                self?.reloadSidebar()
            }
        ))
    }

    @objc func removeFavoriteFromMenu(_ sender: NSMenuItem) {
        guard let tabId = sender.representedObject as? TabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.unfavoriteTab(tabId: tabId))
    }

    @objc private func toggleSidebar(_ sender: Any?) {
        browserWindowController?.toggleSidebar()
    }

    @objc private func goBack(_ sender: Any?) {
        browserWindowController?.goBackInActiveTab()
    }

    @objc private func goForward(_ sender: Any?) {
        browserWindowController?.goForwardInActiveTab()
    }

    @objc private func reloadOrStop(_ sender: Any?) {
        browserWindowController?.reloadOrStopActiveTab()
    }

    @objc private func toggleAIPanel(_ sender: Any?) {
        (NSApp.delegate as? AppDelegate)?.toggleAIPanel()
    }

    @objc private func toggleArchiveModeFromFooter(_ sender: Any?) {
        setBodyMode(bodyMode == .tabs ? .archive : .tabs, resetArchivePanelState: bodyMode == .tabs)
    }

    func toggleArchivePanel() {
        setBodyMode(bodyMode == .tabs ? .archive : .tabs, resetArchivePanelState: bodyMode == .tabs)
    }

    func requestTidyTabs(from sourceView: NSView) {
        let resolvedSpaceId = activeSpaceId ?? MahoBridge.shared.getActiveSpaceId()
        guard let spaceId = resolvedSpaceId else { return }
        activeSpaceId = spaceId
        tidyTabsAnchorView = sourceView
        tidyTabsPopover?.close()
        tidyTabsPopover = nil
        showTidyTabsLoadingPopover(anchorView: sourceView)
        CoreUpdateDispatcher.shared.sendAndDispatch(.requestTidyTabs(spaceId: spaceId))
    }

    func canRequestTidyTabs() -> Bool {
        let resolvedSpaceId = activeSpaceId ?? MahoBridge.shared.getActiveSpaceId()
        guard let spaceId = resolvedSpaceId else { return false }
        let todayTabs = MahoBridge.shared.getTodayTabs(spaceId: spaceId)
        let unpinnedCount = todayTabs.filter { !$0.isPinned }.count
        return unpinnedCount >= 6
    }

    private func showTidyTabsLoadingPopover(anchorView: NSView) {
        let popover = NSPopover()
        popover.behavior = .semitransient
        popover.contentSize = NSSize(width: 300, height: 80)
        popover.contentViewController = TidyTabsLoadingViewController()
        popover.show(relativeTo: anchorView.bounds, of: anchorView, preferredEdge: .maxY)
        tidyTabsPopover = popover
    }

    private func showTidyTabsPopover(spaceId: SpaceId, folders: [TidyTabFolder], anchorView: NSView?) {
        tidyTabsPopover?.close()
        let popover = NSPopover()
        popover.behavior = .semitransient
        popover.contentSize = NSSize(width: 300, height: 400)
        let allTabs = MahoBridge.shared.getTabViewModels()
        let vc = TidyTabsConfirmViewController(spaceId: spaceId, folders: folders, allTabs: allTabs) { [weak self] in
            self?.tidyTabsPopover?.close()
            self?.tidyTabsPopover = nil
        } onApply: { [weak self] applySpaceId in
            self?.tidyTabsPopover?.close()
            self?.tidyTabsPopover = nil
            CoreUpdateDispatcher.shared.sendAndDispatch(.applyTidyTabs(spaceId: applySpaceId))
        }
        popover.contentViewController = vc
        if let anchorView {
            popover.show(relativeTo: anchorView.bounds, of: anchorView, preferredEdge: .maxY)
        }
        tidyTabsPopover = popover
    }

    private func resolvedTidyTabsAnchorView() -> NSView? {
        if let tidyTabsAnchorView, !tidyTabsAnchorView.isHidden {
            return tidyTabsAnchorView
        }
        if let contentView = browserWindowController?.window?.contentView {
            return contentView
        }
        return isViewLoaded ? view : nil
    }

    private func configuredSymbol(named symbolName: String, pointSize: CGFloat, accessibilityDescription: String?) -> NSImage? {
        let configuration = NSImage.SymbolConfiguration(pointSize: pointSize, weight: .medium)
        return NSImage(systemSymbolName: symbolName, accessibilityDescription: accessibilityDescription)?.withSymbolConfiguration(configuration)
    }

    func showCreateThemePlaceholder(for space: SpaceViewModel) {
        activeSpaceId = space.id
        createThemePlaceholderView.configure(space: space)
        setBodyMode(.createThemePlaceholder)
    }

    private func setBodyMode(_ mode: BodyMode, resetArchivePanelState: Bool = false) {
        guard bodyMode != mode else {
            if mode == .archive {
                if resetArchivePanelState {
                    archivePanelView.resetState()
                }
                archivePanelView.reloadArchivedTabs(spaceId: activeSpaceId ?? resolvedActiveSpaceId())
                DispatchQueue.main.async { [weak self] in
                    self?.archivePanelView.focusSearchField()
                }
            }
            return
        }

        let previousMode = bodyMode
        if previousMode == .tabs {
            lastFocusedBodyView = view.window?.firstResponder as? NSView
        }

        bodyMode = mode
        if mode == .archive {
            if resetArchivePanelState {
                archivePanelView.resetState()
            }
            archivePanelView.reloadArchivedTabs(spaceId: activeSpaceId ?? resolvedActiveSpaceId())
        }

        transitionBodyMode(from: previousMode, to: mode)
    }

    private func transitionBodyMode(from previousMode: BodyMode, to newMode: BodyMode) {
        let showingLibrarySurface = newMode != .tabs
        let incomingView = showingLibrarySurface ? libraryContainerView : scrollView
        let outgoingView = showingLibrarySurface ? scrollView : libraryContainerView

        incomingView.alphaValue = 0
        incomingView.isHidden = false
        updateBodyModeUI(visibleLibrarySurface: showingLibrarySurface, preserveBodyVisibility: true)
        view.layoutSubtreeIfNeeded()

        NSAnimationContext.runAnimationGroup { context in
            context.duration = 0.18
            context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            context.allowsImplicitAnimation = true

            outgoingView.animator().alphaValue = 0
            incomingView.animator().alphaValue = 1
            view.layoutSubtreeIfNeeded()
        } completionHandler: { [weak self] in
            guard let self else { return }
            outgoingView.alphaValue = 1
            outgoingView.isHidden = true
            self.updateBodyModeUI(visibleLibrarySurface: showingLibrarySurface, preserveBodyVisibility: false)

            if newMode == .archive {
                DispatchQueue.main.async { [weak self] in
                    self?.archivePanelView.focusSearchField()
                }
            } else if newMode == .createThemePlaceholder {
                DispatchQueue.main.async { [weak self] in
                    self?.createThemePlaceholderView.focusPrimaryAction()
                }
            } else if let lastFocusedBodyView, lastFocusedBodyView.window != nil {
                self.view.window?.makeFirstResponder(lastFocusedBodyView)
            } else {
                self.view.window?.makeFirstResponder(self.outlineView)
            }
        }
    }

    private func updateBodyModeUI(visibleLibrarySurface: Bool? = nil, preserveBodyVisibility: Bool = false) {
        let showingLibrarySurface = visibleLibrarySurface ?? (bodyMode != .tabs)
        let showingArchive = bodyMode == .archive
        let showingCreateThemePlaceholder = bodyMode == .createThemePlaceholder

        bodyContainerTopToFavoritesConstraint?.isActive = !showingLibrarySurface
        bodyContainerTopToNavRowConstraint?.isActive = showingLibrarySurface
        bodyContainerBottomToFooterConstraint?.isActive = !showingLibrarySurface
        bodyContainerBottomToContainerConstraint?.isActive = showingLibrarySurface

        addressBarHeightConstraint?.constant = showingLibrarySurface ? 0 : ChromeGeometry.addressBarHeight
        favoritesGridHeightConstraint?.constant = showingLibrarySurface
            ? 0
            : FavoritesGridView.height(for: favoritesGrid.favoriteCount, availableWidth: favoritesGrid.bounds.width > 0 ? favoritesGrid.bounds.width : view.bounds.width)

        navControlRow.isHidden = showingLibrarySurface
        footerContainerView.isHidden = showingLibrarySurface
        addressBar.isHidden = showingLibrarySurface
        favoritesGrid.isHidden = showingLibrarySurface
        if !preserveBodyVisibility {
            scrollView.isHidden = showingLibrarySurface
            libraryContainerView.isHidden = !showingLibrarySurface
        }

        archivePanelView.isHidden = !showingArchive
        createThemePlaceholderView.isHidden = !showingCreateThemePlaceholder
        libraryRailView.isHidden = !showingArchive
        libraryDividerView.isHidden = !showingArchive
        libraryRailView.alphaValue = showingArchive ? 1 : 0
        libraryDividerView.alphaValue = showingArchive ? 1 : 0
        libraryRailView.setAccessibilityElement(showingArchive)
        libraryDividerView.setAccessibilityElement(showingArchive)

        libraryButton.contentTintColor = showingArchive
            ? MahoAppColors.primaryText.withAlphaComponent(0.82)
            : MahoAppColors.spaceControlTint
        libraryButtonContainer.layer?.backgroundColor = (showingArchive
            ? MahoAppColors.archiveControlActiveBackground.withAlphaComponent(0.74)
            : MahoAppColors.archiveControlBackground.withAlphaComponent(0.18)).cgColor
        libraryButtonContainer.layer?.borderColor = (showingArchive
            ? MahoAppColors.archiveControlBorder.withAlphaComponent(0.50)
            : MahoAppColors.archiveControlBorder.withAlphaComponent(0.14)).cgColor
        libraryButton.toolTip = showingArchive ? "Show Tabs" : "Archive"
        libraryButton.setAccessibilityValue(showingArchive ? "Archive Visible" : "Archive Hidden")
        browserWindowController?.setArchivePresentationActive(showingArchive)

        switch bodyMode {
        case .tabs:
            bodyContainerView.setAccessibilityIdentifier("sidebar.body.tabs")
            bodyContainerView.setAccessibilityLabel("Sidebar Tabs Body")
        case .archive:
            bodyContainerView.setAccessibilityIdentifier("sidebar.body.archive")
            bodyContainerView.setAccessibilityLabel("Archived Tabs Body")
        case .createThemePlaceholder:
            bodyContainerView.setAccessibilityIdentifier("sidebar.body.createTheme")
            bodyContainerView.setAccessibilityLabel("Create Theme Placeholder Body")
        }
        view.layoutSubtreeIfNeeded()
    }
}

private final class SidebarCreateThemePlaceholderView: NSView {
    var onBack: (() -> Void)?
    var onOpenDetailedEditor: ((SpaceViewModel) -> Void)?

    private var currentSpace: SpaceViewModel?
    private let contentStackView = NSStackView()
    private let headerStackView = NSStackView()
    private let eyebrowLabel = NSTextField(labelWithString: "CREATE THEME")
    private let titleLabel = NSTextField(labelWithString: "Create Theme")
    private let helperLabel = NSTextField(labelWithString: "Start in the sidebar, then open the full theme color editor when you need the detailed controls.")
    private let spacerView = NSView()
    private let backButton = NSButton(title: "Back", target: nil, action: nil)
    private let ctaButton = NSButton(title: "Open Detailed Editor", target: nil, action: nil)

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

    func configure(space: SpaceViewModel) {
        currentSpace = space
        let trimmedName = space.name.trimmingCharacters(in: .whitespacesAndNewlines)
        titleLabel.stringValue = trimmedName.isEmpty ? "Create Theme" : "Create Theme for \(trimmedName)"
        setAccessibilityLabel(trimmedName.isEmpty ? "Create Theme Placeholder" : "Create Theme Placeholder for \(trimmedName)")
    }

    func focusPrimaryAction() {
        window?.makeFirstResponder(ctaButton)
    }

    func refreshAppearance() {
        layer?.backgroundColor = MahoAppColors.sidebarBackground.withAlphaComponent(0.56).cgColor
        eyebrowLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.82)
        titleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.94)
        helperLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.78)

        let secondaryFill = MahoAppColors.archiveControlBackground.withAlphaComponent(0.24)
        let secondaryBorder = MahoAppColors.archiveControlBorder.withAlphaComponent(0.18)
        backButton.contentTintColor = MahoAppColors.primaryText.withAlphaComponent(0.88)
        backButton.bezelColor = secondaryFill
        backButton.layer?.backgroundColor = secondaryFill.cgColor
        backButton.layer?.borderColor = secondaryBorder.cgColor

        let primaryFill = MahoAppColors.spaceChipSelected.withAlphaComponent(0.92)
        let primaryBorder = MahoAppColors.spaceChipSelected.withAlphaComponent(0.84)
        ctaButton.contentTintColor = NSColor.white.withAlphaComponent(0.98)
        ctaButton.bezelColor = primaryFill
        ctaButton.layer?.backgroundColor = primaryFill.cgColor
        ctaButton.layer?.borderColor = primaryBorder.cgColor
    }

    private func setupViews() {
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor

        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        setAccessibilityIdentifier("sidebar.createTheme.placeholder")
        setAccessibilityLabel("Create Theme Placeholder")

        eyebrowLabel.translatesAutoresizingMaskIntoConstraints = false
        eyebrowLabel.font = .systemFont(ofSize: 11, weight: .semibold)
        eyebrowLabel.setAccessibilityElement(false)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 24, weight: .semibold)
        titleLabel.maximumNumberOfLines = 2
        titleLabel.lineBreakMode = .byWordWrapping
        titleLabel.setAccessibilityElement(false)

        helperLabel.translatesAutoresizingMaskIntoConstraints = false
        helperLabel.font = .systemFont(ofSize: 13, weight: .regular)
        helperLabel.maximumNumberOfLines = 3
        helperLabel.lineBreakMode = .byWordWrapping
        helperLabel.setAccessibilityElement(false)

        contentStackView.translatesAutoresizingMaskIntoConstraints = false
        contentStackView.orientation = .vertical
        contentStackView.alignment = .leading
        contentStackView.spacing = 12
        addSubview(contentStackView)

        headerStackView.translatesAutoresizingMaskIntoConstraints = false
        headerStackView.orientation = .vertical
        headerStackView.alignment = .leading
        headerStackView.spacing = 8
        headerStackView.addArrangedSubview(eyebrowLabel)
        headerStackView.addArrangedSubview(titleLabel)
        headerStackView.addArrangedSubview(helperLabel)

        backButton.translatesAutoresizingMaskIntoConstraints = false
        backButton.isBordered = false
        backButton.bezelStyle = .rounded
        backButton.wantsLayer = true
        backButton.layer?.cornerRadius = 11
        backButton.layer?.borderWidth = 1
        backButton.layer?.masksToBounds = true
        backButton.target = self
        backButton.action = #selector(handleBack)
        backButton.keyEquivalent = "\u{1b}"
        backButton.setAccessibilityIdentifier("sidebar.createTheme.back")
        backButton.setAccessibilityLabel("Back to Tabs")

        ctaButton.translatesAutoresizingMaskIntoConstraints = false
        ctaButton.isBordered = false
        ctaButton.bezelStyle = .rounded
        ctaButton.wantsLayer = true
        ctaButton.layer?.cornerRadius = 11
        ctaButton.layer?.borderWidth = 1
        ctaButton.layer?.masksToBounds = true
        ctaButton.target = self
        ctaButton.action = #selector(handleOpenDetailedEditor)
        ctaButton.setAccessibilityIdentifier("sidebar.createTheme.openDetailedEditor")
        ctaButton.setAccessibilityLabel("Open Detailed Theme Editor")

        spacerView.translatesAutoresizingMaskIntoConstraints = false
        spacerView.setContentHuggingPriority(.defaultLow, for: .vertical)
        spacerView.setContentCompressionResistancePriority(.defaultLow, for: .vertical)

        contentStackView.addArrangedSubview(headerStackView)
        contentStackView.addArrangedSubview(spacerView)
        contentStackView.addArrangedSubview(backButton)
        contentStackView.addArrangedSubview(ctaButton)

        NSLayoutConstraint.activate([
            contentStackView.topAnchor.constraint(equalTo: topAnchor, constant: 28),
            contentStackView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 24),
            contentStackView.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -24),
            contentStackView.bottomAnchor.constraint(lessThanOrEqualTo: bottomAnchor, constant: -28),

            headerStackView.widthAnchor.constraint(lessThanOrEqualToConstant: 280),
            spacerView.heightAnchor.constraint(greaterThanOrEqualToConstant: 12),
            backButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 96),
            backButton.heightAnchor.constraint(equalToConstant: 34),
            ctaButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 196),
            ctaButton.heightAnchor.constraint(equalToConstant: 36),
        ])

        refreshAppearance()
    }

    @objc private func handleBack() {
        onBack?()
    }

    @objc private func handleOpenDetailedEditor() {
        guard let currentSpace else { return }
        onOpenDetailedEditor?(currentSpace)
    }
}

private enum SidebarDropOverlayTarget {
    case none
    case favorites
    case pinned
    case regular
}

private final class SidebarDropOverlayView: NSView {
    private enum AnimationMetrics {
        static let fadeDuration: CFTimeInterval = 0.18
        static let pulseDuration: CFTimeInterval = 1.4
        static let scaleKeyPath = "transform.scale"
    }

    var highlightRect: NSRect? {
        didSet {
            guard oldValue != highlightRect else { return }
            updateVisibility(animated: true)
            needsDisplay = true
        }
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupView()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupView()
    }

    override func hitTest(_ point: NSPoint) -> NSView? {
        nil
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        guard let highlightRect, highlightRect.width > 0, highlightRect.height > 0 else { return }

        let path = NSBezierPath(roundedRect: highlightRect, xRadius: 6, yRadius: 6)
        MahoAppColors.dropIndicator.withAlphaComponent(0.12).setFill()
        path.fill()

        MahoAppColors.dropIndicator.withAlphaComponent(0.55).setStroke()
        path.lineWidth = 1.5
        path.stroke()
    }

    private func setupView() {
        wantsLayer = true
        alphaValue = 0
        isHidden = true
        layer?.opacity = 0
        setAccessibilityElement(false)
    }

    private func updateVisibility(animated: Bool) {
        if highlightRect == nil {
            stopPulseAnimation()
            guard animated else {
                alphaValue = 0
                layer?.opacity = 0
                isHidden = true
                return
            }

            NSAnimationContext.runAnimationGroup { context in
                context.duration = AnimationMetrics.fadeDuration
                context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                animator().alphaValue = 0
            } completionHandler: { [weak self] in
                guard let self, self.highlightRect == nil else { return }
                self.layer?.opacity = 0
                self.isHidden = true
            }
            layer?.opacity = 0
            return
        }

        isHidden = false
        needsDisplay = true
        guard animated else {
            alphaValue = 1
            layer?.opacity = 1
            startPulseAnimationIfNeeded()
            return
        }

        if alphaValue < 0.001 {
            alphaValue = 0
            layer?.opacity = 0
            NSAnimationContext.runAnimationGroup { context in
                context.duration = AnimationMetrics.fadeDuration
                context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                animator().alphaValue = 1
            }
            let fadeIn = CABasicAnimation(keyPath: "opacity")
            fadeIn.fromValue = 0
            fadeIn.toValue = 1
            fadeIn.duration = AnimationMetrics.fadeDuration
            layer?.add(fadeIn, forKey: "sidebarDropOverlayFadeIn")
            layer?.opacity = 1
        } else {
            alphaValue = 1
            layer?.opacity = 1
        }

        startPulseAnimationIfNeeded()
    }

    private func startPulseAnimationIfNeeded() {
        guard let layer, layer.animation(forKey: "sidebarDropOverlayPulse") == nil else { return }
        let pulse = CABasicAnimation(keyPath: AnimationMetrics.scaleKeyPath)
        pulse.fromValue = 0.992
        pulse.toValue = 1.008
        pulse.duration = AnimationMetrics.pulseDuration
        pulse.autoreverses = true
        pulse.repeatCount = .infinity
        pulse.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        layer.add(pulse, forKey: "sidebarDropOverlayPulse")
    }

    private func stopPulseAnimation() {
        layer?.removeAnimation(forKey: "sidebarDropOverlayPulse")
    }
}

private final class SidebarClickableView: NSButton {
    var onClick: (() -> Void)?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupButton()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupButton()
    }

    private func setupButton() {
        title = ""
        isBordered = false
        bezelStyle = .regularSquare
        target = self
        action = #selector(handleClick)
        setButtonType(.momentaryLight)
        isTransparent = false
        setAccessibilityRole(.button)
        setAccessibilityElement(true)
    }

    override var intrinsicContentSize: NSSize {
        return frame.size.width > 0 ? frame.size : NSSize(width: 200, height: 34)
    }

    override func hitTest(_ point: NSPoint) -> NSView? {
        let local = convert(point, from: superview)
        return bounds.contains(local) ? self : nil
    }

    override func accessibilityChildren() -> [Any]? { return nil }

    override func accessibilityPerformPress() -> Bool {
        handleClick()
        return true
    }

    override func accessibilityFrame() -> NSRect {
        guard let window = window else { return super.accessibilityFrame() }
        let windowRect = convert(bounds, to: nil)
        return window.convertToScreen(windowRect)
    }

    @objc private func handleClick() {
        onClick?()
    }
}

private final class FavoritesGridView: NSView {
    fileprivate static let shadowColor = NSColor.black.withAlphaComponent(0.02)
    fileprivate static let emptyStateCardShadowColor = NSColor.black.withAlphaComponent(0.16)
    fileprivate static let emptyStateBadgeShadowColor = NSColor.black.withAlphaComponent(0.20)
    fileprivate static let defaultAvailableWidth: CGFloat = 160
    fileprivate static let maxVisibleFavorites = 10
    fileprivate static let tileIconSize: CGFloat = 20
    fileprivate static let horizontalPadding: CGFloat = SidebarMetrics.sidebarHorizontalInset
    fileprivate static let spacing: CGFloat = 7
    fileprivate static let topPadding: CGFloat = 5
    fileprivate static let bottomPadding: CGFloat = 7
    fileprivate static let emptyStateHeight: CGFloat = 108
    fileprivate static let singleRowCardHeight: CGFloat = 34
    fileprivate static let twoFavoriteRowHeight: CGFloat = 42
    fileprivate static let partialRowIndentPerMissingColumn: CGFloat = 2

    private struct LayoutSpec {
        let rowCounts: [Int]
        let maxColumns: Int
        let usesCompactRowCard: Bool
    }

    private struct LayoutMetrics {
        let spec: LayoutSpec
        let tileWidth: CGFloat
        let tileHeight: CGFloat
        let contentHeight: CGFloat
    }

    private struct DragInsertionState: Equatable {
        let hoveredIndex: Int
        let sourceTabId: TabId
        let sourceIsFavorite: Bool
    }

    private enum DragOrigin {
        case favorites
        case other
    }

    private struct DragInfo {
        let tabId: TabId
        let origin: DragOrigin
    }

    private var tileViews: [FavoriteTileView] = []
    private var displayedTabs: [TabViewModel] = []
    private var displayedActiveTabId: TabId?
    private var paletteCache: [FavoriteTilePaletteCacheKey: FavoriteTilePalette] = [:]
    private let emptyStateView = FavoritesEmptyStateCardView()
    private(set) var favoriteCount = 0
    var onTabSelected: ((TabId) -> Void)?
    var onFavoriteReorder: ((TabId, Int) -> Void)?
    var onFavoriteAdd: ((TabId, Int) -> Void)?
    var onFavoriteRemove: ((TabId) -> Void)?
    var menuProvider: ((TabViewModel) -> NSMenu?)?
    var onDragSessionWillBegin: (() -> Void)?
    var onDragSessionEnded: (() -> Void)?
    var onUITestDragStateChanged: ((Bool, Int?, [TabId]) -> Void)?
    private var shouldAnimateNextLayout = false
    private var dragInsertionState: DragInsertionState?
    weak var heightConstraint: NSLayoutConstraint?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        setAccessibilityRole(.group)
        registerForDraggedTypes([SidebarDragController.dragType])
        emptyStateView.isHidden = true
        addSubview(emptyStateView)
        refreshAppearance()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        setAccessibilityRole(.group)
        registerForDraggedTypes([SidebarDragController.dragType])
        emptyStateView.isHidden = true
        addSubview(emptyStateView)
        refreshAppearance()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance(reresolvePalettes: true)
    }

    func refreshAppearance(reresolvePalettes: Bool = false) {
        let appearance = window?.effectiveAppearance ?? effectiveAppearance
        appearance.performAsCurrentDrawingAppearance {
            layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
        }
        emptyStateView.refreshAppearance()
        if reresolvePalettes {
            paletteCache.removeAll()
        }
        let currentRenderedTabs = renderedTabs
        for (index, tileView) in tileViews.enumerated() {
            if reresolvePalettes, index < currentRenderedTabs.count {
                tileView.updatePalette(resolvedPalette(for: currentRenderedTabs[index], appearance: appearance), animated: false)
            }
            tileView.refreshAppearance()
        }
    }

    override func layout() {
        super.layout()

        syncTileViewsWithRenderedTabs(animated: shouldAnimateNextLayout)

        let visibleCount = visibleTileCount

        if bounds.width > 0 {
            let expectedHeight = Self.height(for: visibleCount, availableWidth: bounds.width)
            if let constraint = heightConstraint, abs(constraint.constant - expectedHeight) > 0.5 {
                constraint.constant = expectedHeight
            }
        }

        if visibleCount == 0 {
            let emptyWidth = max(0, bounds.width - (Self.horizontalPadding * 2))
            emptyStateView.frame = NSRect(
                x: Self.horizontalPadding,
                y: Self.bottomPadding,
                width: emptyWidth,
                height: Self.emptyStateHeight
            )
            return
        }

        let frames = Self.frames(for: visibleCount, availableWidth: bounds.width)
        let shouldAnimate = shouldAnimateNextLayout && window != nil
        shouldAnimateNextLayout = false

        if shouldAnimate {
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.18
                context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                for (tileView, frame) in zip(tileViews, frames) {
                    tileView.animator().frame = frame
                    tileView.animator().alphaValue = 1
                }
            }
        } else {
            for (tileView, frame) in zip(tileViews, frames) {
                tileView.frame = frame
                tileView.alphaValue = 1
            }
        }
    }

    func configure(with tabs: [TabViewModel], activeTabId: TabId? = nil) {
        var seenTabIds = Set<TabId>()
        let dedupedTabs = tabs.filter { seenTabIds.insert($0.id).inserted }
        let limitedTabs = Array(dedupedTabs.prefix(Self.maxVisibleFavorites))
        let previousTabIds = tileViews.compactMap(\.tabId)
        let previousDragState = dragInsertionState
        favoriteCount = limitedTabs.count
        displayedTabs = limitedTabs
        displayedActiveTabId = activeTabId
        let nextDragState = clampedDragInsertionState(for: dragInsertionState, favoriteCount: limitedTabs.count)
        dragInsertionState = nextDragState
        let nextTabIds = displayedTabsForRendering(with: limitedTabs, dragState: nextDragState).map(\.id)
        shouldAnimateNextLayout = previousTabIds != nextTabIds || previousDragState != nextDragState
        syncTileViewsWithRenderedTabs(animated: shouldAnimateNextLayout)
        needsLayout = true
    }

    private func syncTileViewsWithRenderedTabs(animated: Bool) {
        let currentRenderedTabs = renderedTabs
        emptyStateView.isHidden = !currentRenderedTabs.isEmpty
        let retainedTabIds = Set(currentRenderedTabs.map(\.id))
        paletteCache = paletteCache.filter { retainedTabIds.contains($0.key.tabId) }

        var existingViewsByTabId: [TabId: FavoriteTileView] = [:]
        existingViewsByTabId.reserveCapacity(tileViews.count)
        for tileView in tileViews {
            guard let tabId = tileView.tabId, existingViewsByTabId[tabId] == nil else { continue }
            existingViewsByTabId[tabId] = tileView
        }

        for tileView in tileViews where tileView.tabId.map({ !retainedTabIds.contains($0) }) ?? true {
            tileView.removeFromSuperview()
        }

        var nextTileViews: [FavoriteTileView] = []
        nextTileViews.reserveCapacity(currentRenderedTabs.count)

        for tab in currentRenderedTabs {
            if let existingTileView = existingViewsByTabId[tab.id] {
                nextTileViews.append(existingTileView)
            } else {
                let tileView = FavoriteTileView()
                tileView.alphaValue = animated ? 0 : 1
                nextTileViews.append(tileView)
                addSubview(tileView)
            }
        }

        tileViews = nextTileViews

        let appearance = window?.effectiveAppearance ?? effectiveAppearance
        for (index, (tileView, tab)) in zip(tileViews, currentRenderedTabs).enumerated() {
            let palette = resolvedPalette(for: tab, appearance: appearance)
            tileView.prefersCompactRowLayout = currentRenderedTabs.count == 1
            tileView.configure(with: tab, palette: palette)
            tileView.isSelectedTile = tab.id == displayedActiveTabId
            tileView.onDragSessionWillBegin = { [weak self] in
                self?.onDragSessionWillBegin?()
            }
            tileView.onDragSessionEnded = { [weak self] in
                self?.handleDragSessionEnded()
            }
            tileView.setAccessibilityElement(true)
            tileView.setAccessibilityRole(.cell)
            tileView.setAccessibilityIdentifier("sidebar.favorite.\(index)")
            tileView.setAccessibilityLabel(tab.title.isEmpty ? tab.url : tab.title)
            tileView.onClick = { [weak self] in
                self?.onTabSelected?(tab.id)
            }
            tileView.menuProvider = { [weak self] in
                self?.menuProvider?(tab)
            }
        }
    }

    private func resolvedPalette(for tab: TabViewModel, appearance: NSAppearance) -> FavoriteTilePalette {
        let key = FavoriteTilePaletteResolver.cacheKey(for: tab.id, favicon: tab.favicon, appearance: appearance)
        if let cached = paletteCache[key] {
            return cached
        }
        let palette = FavoriteTilePaletteResolver.resolve(for: tab.favicon, appearance: appearance)
        paletteCache[key] = palette
        return palette
    }

    static func height(for favoriteCount: Int, availableWidth: CGFloat = defaultAvailableWidth) -> CGFloat {
        guard favoriteCount > 0 else {
            return emptyStateHeight + topPadding + bottomPadding
        }
        return layoutMetrics(for: favoriteCount, availableWidth: availableWidth).contentHeight
    }

    var uiTestRenderedFavoriteTabIDs: [TabId] {
        renderedTabs.map(\.id)
    }

    var uiTestDragInsertionStateIsActive: Bool {
        dragInsertionState != nil
    }

    var uiTestDragInsertionIndex: Int? {
        dragInsertionState?.hoveredIndex
    }

    private var renderedTabs: [TabViewModel] {
        displayedTabsForRendering(with: displayedTabs, dragState: dragInsertionState)
    }

    private var visibleTileCount: Int {
        renderedTabs.count
    }

    private func displayedTabsForRendering(with tabs: [TabViewModel], dragState: DragInsertionState?) -> [TabViewModel] {
        guard let dragState else { return tabs }

        if dragState.sourceIsFavorite {
            guard let sourceIndex = tabs.firstIndex(where: { $0.id == dragState.sourceTabId }) else {
                return tabs
            }
            var reorderedTabs = tabs
            let draggedTab = reorderedTabs.remove(at: sourceIndex)
            let insertionIndex = min(max(0, dragState.hoveredIndex), reorderedTabs.count)
            reorderedTabs.insert(draggedTab, at: insertionIndex)
            return reorderedTabs
        }

        guard favoriteCount < Self.maxVisibleFavorites,
              let sourceTab = draggedTabViewModel(for: dragState.sourceTabId) else {
            return tabs
        }

        var augmentedTabs = tabs
        let insertionIndex = min(max(0, dragState.hoveredIndex), augmentedTabs.count)
        augmentedTabs.insert(sourceTab, at: insertionIndex)
        return Array(augmentedTabs.prefix(Self.maxVisibleFavorites))
    }

    private func draggedTabViewModel(for tabId: TabId) -> TabViewModel? {
        MahoBridge.shared.getTabViewModels().first(where: { $0.id == tabId })
    }

    private func clampedDragInsertionState(for state: DragInsertionState?, favoriteCount: Int) -> DragInsertionState? {
        guard let state else { return nil }
        let maxIndex = state.sourceIsFavorite ? max(0, favoriteCount - 1) : favoriteCount
        return DragInsertionState(
            hoveredIndex: min(max(0, state.hoveredIndex), maxIndex),
            sourceTabId: state.sourceTabId,
            sourceIsFavorite: state.sourceIsFavorite
        )
    }

    private func updateDragInsertionState(hoveredIndex: Int, sourceTabId: TabId, sourceIsFavorite: Bool) {
        let clampedIndex = sourceIsFavorite
            ? min(max(0, hoveredIndex), max(0, favoriteCount - 1))
            : min(max(0, hoveredIndex), favoriteCount)
        let nextState = DragInsertionState(
            hoveredIndex: clampedIndex,
            sourceTabId: sourceTabId,
            sourceIsFavorite: sourceIsFavorite
        )
        guard dragInsertionState != nextState else { return }
        dragInsertionState = nextState
        shouldAnimateNextLayout = true
        syncTileViewsWithRenderedTabs(animated: true)
        notifyUITestDragStateChanged()
        needsLayout = true
        needsDisplay = true
    }

    private func clearDragInsertionState(animated: Bool) {
        guard dragInsertionState != nil else { return }
        dragInsertionState = nil
        shouldAnimateNextLayout = animated
        syncTileViewsWithRenderedTabs(animated: animated)
        notifyUITestDragStateChanged()
        needsLayout = true
        needsDisplay = true
    }

    private func notifyUITestDragStateChanged() {
        onUITestDragStateChanged?(
            dragInsertionState != nil,
            dragInsertionState?.hoveredIndex,
            renderedTabs.map(\.id)
        )
    }

    private func handleDragSessionEnded() {
        clearDragInsertionState(animated: true)
        onDragSessionEnded?()
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        guard window != nil else { return [] }
        return updateDragDestination(from: sender)
    }

    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        guard window != nil else { return [] }
        return updateDragDestination(from: sender)
    }

    override func draggingExited(_ sender: NSDraggingInfo?) {
        guard window != nil else { return }
        clearDragInsertionState(animated: true)
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        defer {
            clearDragInsertionState(animated: true)
        }
        guard let dragInfo = parseDraggedInfo(from: sender) else { return false }
        guard dragInfo.origin == .favorites || favoriteCount < Self.maxVisibleFavorites else {
            return false
        }

        let location = convert(sender.draggingLocation, from: nil)
        let targetIndex = resolvedDropIndex(for: location, sourceTabId: dragInfo.tabId, sourceIsFavorite: dragInfo.origin == .favorites)

        if dragInfo.origin == .favorites {
            onFavoriteReorder?(dragInfo.tabId, targetIndex)
        } else {
            onFavoriteAdd?(dragInfo.tabId, targetIndex)
        }
        return true
    }

    override func prepareForDragOperation(_ sender: NSDraggingInfo) -> Bool {
        guard let dragInfo = parseDraggedInfo(from: sender) else { return false }
        return dragInfo.origin == .favorites || favoriteCount < Self.maxVisibleFavorites
    }

    private func updateDragDestination(from sender: NSDraggingInfo) -> NSDragOperation {
        guard let dragInfo = parseDraggedInfo(from: sender) else {
            clearDragInsertionState(animated: true)
            return []
        }

        guard dragInfo.origin == .favorites || favoriteCount < Self.maxVisibleFavorites else {
            clearDragInsertionState(animated: true)
            return []
        }

        let location = convert(sender.draggingLocation, from: nil)
        let targetIndex = resolvedDropIndex(for: location, sourceTabId: dragInfo.tabId, sourceIsFavorite: dragInfo.origin == .favorites)
        updateDragInsertionState(
            hoveredIndex: targetIndex,
            sourceTabId: dragInfo.tabId,
            sourceIsFavorite: dragInfo.origin == .favorites
        )
        return .move
    }

    private func parseDraggedInfo(from info: NSDraggingInfo) -> DragInfo? {
        guard let pasteboardItem = info.draggingPasteboard.pasteboardItems?.first,
              let string = pasteboardItem.string(forType: SidebarDragController.dragType),
              let data = string.data(using: .utf8),
              let dict = try? JSONSerialization.jsonObject(with: data) as? [String: String],
              dict["type"] == "tab",
              let id = dict["id"],
              (info.draggingPasteboard.pasteboardItems?.count ?? 0) == 1 else {
            return nil
        }
        let origin: DragOrigin = dict["origin"] == "favorites" ? .favorites : .other
        return DragInfo(tabId: id, origin: origin)
    }

    private func tileIndex(for point: NSPoint, itemCount: Int) -> Int {
        guard itemCount > 0 else { return 0 }

        let frames = Self.frames(for: itemCount, availableWidth: bounds.width)
        if let matchedIndex = frames.firstIndex(where: { $0.insetBy(dx: -Self.spacing * 0.5, dy: -Self.spacing * 0.5).contains(point) }) {
            return matchedIndex
        }

        guard let nearest = frames.enumerated().min(by: {
            Self.distanceSquared(from: point, to: Self.midpoint(of: $0.element)) < Self.distanceSquared(from: point, to: Self.midpoint(of: $1.element))
        }) else {
            return 0
        }

        if nearest.offset == frames.count - 1 {
            let lastFrame = nearest.element
            let isBeyondTrailingEdge = point.x >= lastFrame.midX || point.y <= lastFrame.midY
            if isBeyondTrailingEdge {
                return itemCount
            }
        }

        return nearest.offset
    }

    private func resolvedDropIndex(for point: NSPoint, sourceTabId: TabId, sourceIsFavorite: Bool) -> Int {
        let itemCount = sourceIsFavorite ? max(0, favoriteCount - 1) : favoriteCount
        let targetIndex = tileIndex(for: point, itemCount: itemCount)

        guard sourceIsFavorite,
              displayedTabs.contains(where: { $0.id == sourceTabId }) else {
            return min(max(0, targetIndex), itemCount)
        }

        return min(max(0, targetIndex), max(0, favoriteCount - 1))
    }

    override func concludeDragOperation(_ sender: NSDraggingInfo?) {
        clearDragInsertionState(animated: true)
    }

    private static func layoutMetrics(for favoriteCount: Int, availableWidth: CGFloat) -> LayoutMetrics {
        let spec = layoutSpec(for: favoriteCount)
        if spec.usesCompactRowCard {
            return LayoutMetrics(
                spec: spec,
                tileWidth: singleRowCardHeight,
                tileHeight: singleRowCardHeight,
                contentHeight: singleRowCardHeight + topPadding + bottomPadding
            )
        }

        let usableWidth = availableWidth > 0 ? availableWidth : defaultAvailableWidth
        let innerWidth = max(0, usableWidth - (horizontalPadding * 2))
        let totalSpacing = CGFloat(max(spec.maxColumns - 1, 0)) * spacing
        let tileWidth = max(0, (innerWidth - totalSpacing) / CGFloat(max(spec.maxColumns, 1)))
        let tileHeight = favoriteCount == 2 ? min(tileWidth, twoFavoriteRowHeight) : tileWidth
        let rowCount = spec.rowCounts.count
        let contentHeight = CGFloat(rowCount) * tileHeight
            + CGFloat(max(rowCount - 1, 0)) * spacing
            + topPadding
            + bottomPadding

        return LayoutMetrics(spec: spec, tileWidth: tileWidth, tileHeight: tileHeight, contentHeight: contentHeight)
    }

    private static func frames(for favoriteCount: Int, availableWidth: CGFloat, contentHeight: CGFloat? = nil) -> [NSRect] {
        guard favoriteCount > 0 else { return [] }

        let metrics = layoutMetrics(for: favoriteCount, availableWidth: availableWidth)
        let resolvedHeight = contentHeight ?? metrics.contentHeight
        let usableWidth = availableWidth > 0 ? availableWidth : defaultAvailableWidth
        let innerWidth = max(0, usableWidth - (horizontalPadding * 2))

        if metrics.spec.usesCompactRowCard {
            let originY = resolvedHeight - topPadding - metrics.tileHeight
            return [
                NSRect(
                    x: horizontalPadding,
                    y: max(bottomPadding, originY),
                    width: innerWidth,
                    height: metrics.tileHeight
                )
            ]
        }

        var frames: [NSRect] = []
        frames.reserveCapacity(favoriteCount)

        let fullRowWidth = CGFloat(metrics.spec.maxColumns) * metrics.tileWidth
            + CGFloat(max(metrics.spec.maxColumns - 1, 0)) * spacing
        let centeredTrackOffset = max(0, (innerWidth - fullRowWidth) * 0.5)

        for (rowIndex, rowCount) in metrics.spec.rowCounts.enumerated() {
            let missingColumnCount = max(metrics.spec.maxColumns - rowCount, 0)
            let originX = horizontalPadding
                + centeredTrackOffset
                + (CGFloat(missingColumnCount) * partialRowIndentPerMissingColumn)
            let originY = resolvedHeight - topPadding - metrics.tileHeight - CGFloat(rowIndex) * (metrics.tileHeight + spacing)

            for column in 0..<rowCount {
                frames.append(
                    NSRect(
                        x: originX + CGFloat(column) * (metrics.tileWidth + spacing),
                        y: max(bottomPadding, originY),
                        width: metrics.tileWidth,
                        height: metrics.tileHeight
                    )
                )
            }
        }

        return frames
    }

    private static func layoutSpec(for favoriteCount: Int) -> LayoutSpec {
        switch max(0, min(favoriteCount, maxVisibleFavorites)) {
        case 1:
            return LayoutSpec(rowCounts: [1], maxColumns: 1, usesCompactRowCard: true)
        case 2:
            return LayoutSpec(rowCounts: [2], maxColumns: 2, usesCompactRowCard: false)
        case 3:
            return LayoutSpec(rowCounts: [3], maxColumns: 3, usesCompactRowCard: false)
        case 4:
            return LayoutSpec(rowCounts: [4], maxColumns: 4, usesCompactRowCard: false)
        case 5:
            return LayoutSpec(rowCounts: [3, 2], maxColumns: 3, usesCompactRowCard: false)
        case 6:
            return LayoutSpec(rowCounts: [3, 3], maxColumns: 3, usesCompactRowCard: false)
        case 7:
            return LayoutSpec(rowCounts: [4, 3], maxColumns: 4, usesCompactRowCard: false)
        case 8:
            return LayoutSpec(rowCounts: [4, 4], maxColumns: 4, usesCompactRowCard: false)
        case 9:
            return LayoutSpec(rowCounts: [3, 3, 3], maxColumns: 3, usesCompactRowCard: false)
        default:
            return LayoutSpec(rowCounts: [4, 4, 2], maxColumns: 4, usesCompactRowCard: false)
        }
    }

    private static func distanceSquared(from point: NSPoint, to other: NSPoint) -> CGFloat {
        let dx = point.x - other.x
        let dy = point.y - other.y
        return dx * dx + dy * dy
    }

    private static func midpoint(of rect: NSRect) -> NSPoint {
        NSPoint(x: rect.midX, y: rect.midY)
    }
}

private final class FavoritesEmptyStateCardView: NSView {
    private let cardView = NSView()
    private let cardShadowColor = FavoritesGridView.emptyStateCardShadowColor
    private let badgeShadowColor = FavoritesGridView.emptyStateBadgeShadowColor
    private let badgeView = NSView()
    private let badgeIconView = NSImageView()
    private let closeIconView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "Drag to add Favorites")
    private let subtitleLabel = NSTextField(wrappingLabelWithString: "Favorites keep your most used sites and apps close")
    private let dashLayer = CAShapeLayer()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    override func layout() {
        super.layout()
        dashLayer.frame = cardView.bounds
        dashLayer.path = CGPath(
            roundedRect: cardView.bounds.insetBy(dx: 1, dy: 1),
            cornerWidth: 16,
            cornerHeight: 16,
            transform: nil
        )
    }

    override func hitTest(_ point: NSPoint) -> NSView? {
        nil
    }

    private func setupViews() {
        wantsLayer = true
        layer?.masksToBounds = false

        cardView.translatesAutoresizingMaskIntoConstraints = false
        cardView.wantsLayer = true
        cardView.layer?.cornerRadius = 16
        cardView.layer?.borderWidth = 1
        cardView.layer?.shadowOpacity = 1
        cardView.layer?.shadowRadius = 6
        cardView.layer?.shadowOffset = NSSize(width: 0, height: -2)
        addSubview(cardView)

        dashLayer.fillColor = NSColor.clear.cgColor
        dashLayer.lineWidth = 1
        dashLayer.lineDashPattern = [6, 4]
        dashLayer.lineCap = .round
        cardView.layer?.addSublayer(dashLayer)

        badgeView.translatesAutoresizingMaskIntoConstraints = false
        badgeView.wantsLayer = true
        badgeView.layer?.cornerRadius = 15
        badgeView.layer?.borderWidth = 1
        badgeView.layer?.shadowOpacity = 1
        badgeView.layer?.shadowRadius = 4
        badgeView.layer?.shadowOffset = NSSize(width: 0, height: -1)
        addSubview(badgeView)

        badgeIconView.translatesAutoresizingMaskIntoConstraints = false
        badgeIconView.image = configuredSymbol(named: "star.fill", pointSize: 11, accessibilityDescription: nil)
        badgeView.addSubview(badgeIconView)

        closeIconView.translatesAutoresizingMaskIntoConstraints = false
        closeIconView.image = configuredSymbol(named: "xmark", pointSize: 8, accessibilityDescription: nil)
        cardView.addSubview(closeIconView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 13, weight: .semibold)
        titleLabel.textColor = MahoAppColors.primaryText
        titleLabel.alignment = .center
        titleLabel.maximumNumberOfLines = 2
        cardView.addSubview(titleLabel)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = .systemFont(ofSize: 11, weight: .regular)
        subtitleLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.82)
        subtitleLabel.alignment = .center
        subtitleLabel.maximumNumberOfLines = 2
        cardView.addSubview(subtitleLabel)

        NSLayoutConstraint.activate([
            cardView.leadingAnchor.constraint(equalTo: leadingAnchor),
            cardView.trailingAnchor.constraint(equalTo: trailingAnchor),
            cardView.topAnchor.constraint(equalTo: topAnchor, constant: 12),
            cardView.bottomAnchor.constraint(equalTo: bottomAnchor),

            badgeView.centerXAnchor.constraint(equalTo: cardView.centerXAnchor),
            badgeView.centerYAnchor.constraint(equalTo: cardView.topAnchor, constant: 4),
            badgeView.widthAnchor.constraint(equalToConstant: 30),
            badgeView.heightAnchor.constraint(equalToConstant: 30),

            badgeIconView.centerXAnchor.constraint(equalTo: badgeView.centerXAnchor),
            badgeIconView.centerYAnchor.constraint(equalTo: badgeView.centerYAnchor),
            badgeIconView.widthAnchor.constraint(equalToConstant: 12),
            badgeIconView.heightAnchor.constraint(equalToConstant: 12),

            closeIconView.topAnchor.constraint(equalTo: cardView.topAnchor, constant: 10),
            closeIconView.trailingAnchor.constraint(equalTo: cardView.trailingAnchor, constant: -10),
            closeIconView.widthAnchor.constraint(equalToConstant: 10),
            closeIconView.heightAnchor.constraint(equalToConstant: 10),

            titleLabel.topAnchor.constraint(equalTo: cardView.topAnchor, constant: 24),
            titleLabel.leadingAnchor.constraint(equalTo: cardView.leadingAnchor, constant: 16),
            titleLabel.trailingAnchor.constraint(equalTo: cardView.trailingAnchor, constant: -16),

            subtitleLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 5),
            subtitleLabel.leadingAnchor.constraint(equalTo: cardView.leadingAnchor, constant: 20),
            subtitleLabel.trailingAnchor.constraint(equalTo: cardView.trailingAnchor, constant: -20),
            subtitleLabel.bottomAnchor.constraint(lessThanOrEqualTo: cardView.bottomAnchor, constant: -14),
        ])

        refreshAppearance()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    func refreshAppearance() {
        let appearance = window?.effectiveAppearance ?? effectiveAppearance
        appearance.performAsCurrentDrawingAppearance {
            cardView.layer?.backgroundColor = MahoAppColors.favoritesEmptyCardBackground.cgColor
            cardView.layer?.borderColor = MahoAppColors.favoritesEmptyCardBorder.cgColor
            cardView.layer?.shadowColor = cardShadowColor.cgColor
            dashLayer.strokeColor = MahoAppColors.favoritesEmptyCardDash.cgColor
            badgeView.layer?.backgroundColor = MahoAppColors.favoritesEmptyBadgeBackground.cgColor
            badgeView.layer?.borderColor = MahoAppColors.favoritesEmptyBadgeBorder.cgColor
            badgeView.layer?.shadowColor = badgeShadowColor.cgColor
        }
        badgeIconView.contentTintColor = MahoAppColors.favoritesEmptyBadgeIcon
        closeIconView.contentTintColor = MahoAppColors.favoritesEmptyCloseGlyph
        titleLabel.textColor = MahoAppColors.primaryText
        subtitleLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.82)
    }

    private func configuredSymbol(named symbolName: String, pointSize: CGFloat, accessibilityDescription: String?) -> NSImage? {
        let configuration = NSImage.SymbolConfiguration(pointSize: pointSize, weight: .medium)
        return NSImage(systemSymbolName: symbolName, accessibilityDescription: accessibilityDescription)?.withSymbolConfiguration(configuration)
    }
}

private final class FavoriteTileView: NSView {
    private enum AppearanceState {
        case idle
        case hover
        case selected
    }

    private struct SurfaceAppearance {
        let backgroundColor: NSColor
        let borderColor: NSColor
        let shadowColor: NSColor
        let shadowRadius: CGFloat
        let shadowOffset: NSSize
    }

    private enum AnimationMetrics {
        static let paletteTransitionDuration = MahoAppColors.favoritesPaletteTransitionDuration
    }

    private static let shadowColor = FavoritesGridView.shadowColor

    private let imageView = NSImageView()
    private var currentTab: TabViewModel?
    private let fallbackBadge = NSTextField(labelWithString: "")
    private let customEmojiLabel = NSTextField(labelWithString: "")
    private let titleLabel = NSTextField(labelWithString: "")
    private var trackingArea: NSTrackingArea?
    private var currentPalette = FavoriteTilePalette.fallback(isDarkAppearance: false)
    private var isHovered = false
    var isSelectedTile = false {
        didSet {
            guard oldValue != isSelectedTile else { return }
            refreshAppearance()
        }
    }
    private var imageCenterXConstraint: NSLayoutConstraint?
    private var imageLeadingConstraint: NSLayoutConstraint?
    private var imageCenterYConstraint: NSLayoutConstraint?
    private var fallbackBadgeCenterXConstraint: NSLayoutConstraint?
    private var fallbackBadgeLeadingConstraint: NSLayoutConstraint?
    private var fallbackBadgeCenterYConstraint: NSLayoutConstraint?
    private var imageWidthConstraint: NSLayoutConstraint?
    private var imageHeightConstraint: NSLayoutConstraint?
    private var titleDefaultLeadingConstraint: NSLayoutConstraint?
    private var titleCompactLeadingConstraint: NSLayoutConstraint?
    private var titleTrailingConstraint: NSLayoutConstraint?
    private var titleBottomConstraint: NSLayoutConstraint?
    private var titleCenterYConstraint: NSLayoutConstraint?
    var onClick: (() -> Void)?
    var menuProvider: (() -> NSMenu?)?
    var tabId: TabId?
    var prefersCompactRowLayout = false

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    private func setupViews() {
        wantsLayer = true
        layer?.cornerRadius = 10
        layer?.borderWidth = 1
        layer?.shadowOpacity = 1
        layer?.shadowRadius = 1.2
        layer?.shadowOffset = NSSize(width: 0, height: -1)

        imageView.translatesAutoresizingMaskIntoConstraints = false
        imageView.imageScaling = .scaleProportionallyUpOrDown
        addSubview(imageView)

        fallbackBadge.translatesAutoresizingMaskIntoConstraints = false
        fallbackBadge.font = .systemFont(ofSize: 14, weight: .medium)
        fallbackBadge.textColor = currentPalette.fallbackTint.withAlphaComponent(0.58)
        fallbackBadge.alignment = .center
        fallbackBadge.isHidden = true
        addSubview(fallbackBadge)

        customEmojiLabel.translatesAutoresizingMaskIntoConstraints = false
        customEmojiLabel.font = .systemFont(ofSize: 24)
        customEmojiLabel.alignment = .center
        customEmojiLabel.isHidden = true
        addSubview(customEmojiLabel)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 11, weight: .medium)
        titleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.82)
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 1
        titleLabel.isHidden = true
        addSubview(titleLabel)

        imageCenterXConstraint = imageView.centerXAnchor.constraint(equalTo: centerXAnchor)
        imageLeadingConstraint = imageView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 14)
        imageCenterYConstraint = imageView.centerYAnchor.constraint(equalTo: centerYAnchor)
        fallbackBadgeCenterXConstraint = fallbackBadge.centerXAnchor.constraint(equalTo: centerXAnchor)
        fallbackBadgeLeadingConstraint = fallbackBadge.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 14)
        fallbackBadgeCenterYConstraint = fallbackBadge.centerYAnchor.constraint(equalTo: centerYAnchor)
        imageWidthConstraint = imageView.widthAnchor.constraint(equalToConstant: FavoritesGridView.tileIconSize)
        imageHeightConstraint = imageView.heightAnchor.constraint(equalToConstant: FavoritesGridView.tileIconSize)
        titleDefaultLeadingConstraint = titleLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12)
        titleCompactLeadingConstraint = titleLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 46)
        titleTrailingConstraint = titleLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12)
        titleBottomConstraint = titleLabel.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -10)
        titleCenterYConstraint = titleLabel.centerYAnchor.constraint(equalTo: centerYAnchor)

        NSLayoutConstraint.activate([
            imageCenterXConstraint!,
            imageCenterYConstraint!,
            imageWidthConstraint!,
            imageHeightConstraint!,

            fallbackBadgeCenterXConstraint!,
            fallbackBadgeCenterYConstraint!,
            customEmojiLabel.centerXAnchor.constraint(equalTo: centerXAnchor),
            customEmojiLabel.centerYAnchor.constraint(equalTo: centerYAnchor),

            titleDefaultLeadingConstraint!,
            titleTrailingConstraint!,
            titleBottomConstraint!,
        ])

        refreshAppearance()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea {
            removeTrackingArea(trackingArea)
        }
        let trackingArea = NSTrackingArea(
            rect: bounds,
            options: [.mouseEnteredAndExited, .activeInActiveApp, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(trackingArea)
        self.trackingArea = trackingArea
    }

    override func mouseEntered(with event: NSEvent) {
        guard !isHovered else { return }
        isHovered = true
        refreshAppearance()
    }

    override func mouseExited(with event: NSEvent) {
        guard isHovered else { return }
        isHovered = false
        refreshAppearance()
    }

    func refreshAppearance() {
        let appearanceState = resolvedAppearanceState()
        applyAppearance(surfaceAppearance(for: appearanceState), animated: false)
        let fallbackTint = currentPalette.fallbackTint.withAlphaComponent(0.62)
        fallbackBadge.textColor = fallbackTint
        titleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.82)
        if imageView.contentTintColor != nil {
            imageView.contentTintColor = currentPalette.iconTint
        }
    }

    override func layout() {
        super.layout()

        let usesCompactRowLayout = prefersCompactRowLayout
        imageLeadingConstraint?.constant = usesCompactRowLayout ? 12 : 14
        fallbackBadgeLeadingConstraint?.constant = usesCompactRowLayout ? 12 : 14
        titleCompactLeadingConstraint?.constant = 38
        titleTrailingConstraint?.constant = usesCompactRowLayout ? -10 : -12
        layer?.cornerRadius = usesCompactRowLayout ? 9 : 12
        titleLabel.isHidden = !usesCompactRowLayout
        imageCenterXConstraint?.isActive = !usesCompactRowLayout
        imageLeadingConstraint?.isActive = usesCompactRowLayout
        fallbackBadgeCenterXConstraint?.isActive = !usesCompactRowLayout
        fallbackBadgeLeadingConstraint?.isActive = usesCompactRowLayout
        titleDefaultLeadingConstraint?.isActive = !usesCompactRowLayout
        titleCompactLeadingConstraint?.isActive = usesCompactRowLayout
        titleBottomConstraint?.isActive = !usesCompactRowLayout
        titleCenterYConstraint?.isActive = usesCompactRowLayout
        imageCenterYConstraint?.constant = 0
        fallbackBadgeCenterYConstraint?.constant = 0

        let iconSize: CGFloat = usesCompactRowLayout ? 16 : FavoritesGridView.tileIconSize
        imageWidthConstraint?.constant = iconSize
        imageHeightConstraint?.constant = iconSize
    }

    func configure(with tab: TabViewModel) {
        configure(with: tab, palette: currentPalette)
    }

    func configure(with tab: TabViewModel, palette: FavoriteTilePalette) {
        currentTab = tab
        currentPalette = palette
        tabId = tab.id
        titleLabel.stringValue = titleText(for: tab)

        let customizationStore = FavoriteTileCustomizationStore.shared
        if let customImage = customizationStore.image(for: tab.id) {
            imageView.image = customImage
            imageView.contentTintColor = nil
            imageView.isHidden = false
            fallbackBadge.isHidden = true
            customEmojiLabel.isHidden = true
        } else if let customEmoji = customizationStore.emoji(for: tab.id) {
            customEmojiLabel.stringValue = customEmoji
            customEmojiLabel.isHidden = false
            imageView.isHidden = true
            fallbackBadge.isHidden = true
        } else if let faviconData = tab.favicon, let image = NSImage(data: Data(faviconData.data)) {
            imageView.image = image
            imageView.contentTintColor = nil
            imageView.isHidden = false
            fallbackBadge.isHidden = true
            customEmojiLabel.isHidden = true
        } else {
            imageView.image = configuredSymbol(named: fallbackSymbol(for: tab), pointSize: 16, accessibilityDescription: nil)
            imageView.contentTintColor = currentPalette.iconTint
            imageView.isHidden = false
            fallbackBadge.isHidden = true
            customEmojiLabel.isHidden = true
        }

        refreshAppearance()
        needsLayout = true
    }

    func updatePalette(_ palette: FavoriteTilePalette, animated: Bool) {
        let previousAppearance = surfaceAppearance(for: resolvedAppearanceState())
        currentPalette = palette
        let nextAppearance = surfaceAppearance(for: resolvedAppearanceState())

        if imageView.contentTintColor != nil {
            imageView.contentTintColor = palette.iconTint
        }
        fallbackBadge.textColor = palette.fallbackTint.withAlphaComponent(0.62)
        applyAppearanceTransition(from: previousAppearance, to: nextAppearance, animated: animated)
    }

    private func resolvedAppearanceState() -> AppearanceState {
        if isSelectedTile {
            return .selected
        }
        if isHovered {
            return .hover
        }
        return .idle
    }

    private func backgroundColor(for state: AppearanceState) -> NSColor {
        switch state {
        case .idle:
            return currentPalette.idleBackground
        case .hover:
            return currentPalette.hoverBackground
        case .selected:
            return currentPalette.selectedBackground
        }
    }

    private func borderColor(for state: AppearanceState) -> NSColor {
        switch state {
        case .idle:
            return currentPalette.idleBorder
        case .hover:
            return currentPalette.hoverBorder
        case .selected:
            return currentPalette.selectedBorder
        }
    }

    private func shadowColor(for state: AppearanceState) -> NSColor {
        switch state {
        case .selected:
            return currentPalette.selectedGlow
        case .idle, .hover:
            return Self.shadowColor
        }
    }

    private func shadowRadius(for state: AppearanceState) -> CGFloat {
        switch state {
        case .selected:
            return prefersCompactRowLayout ? 5 : 7
        case .idle, .hover:
            return 1.2
        }
    }

    private func shadowOffset(for state: AppearanceState) -> NSSize {
        switch state {
        case .selected:
            return NSSize(width: 0, height: 0)
        case .idle, .hover:
            return NSSize(width: 0, height: -1)
        }
    }

    private func titleText(for tab: TabViewModel) -> String {
        let trimmedCustomTitle = tab.customTitle?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if !trimmedCustomTitle.isEmpty {
            return trimmedCustomTitle
        }

        if !tab.title.isEmpty {
            return tab.title
        }

        if let host = URL(string: tab.url)?.host {
            return host.replacingOccurrences(of: "www.", with: "")
        }

        return tab.url
    }

    private func fallbackSymbol(for tab: TabViewModel) -> String {
        if let url = URL(string: tab.url), let host = url.host {
            if host.contains("github") { return "chevron.left.forwardslash.chevron.right" }
            if host.contains("figma") { return "square.grid.2x2" }
            if host.contains("notion") { return "doc.text" }
            if host.contains("slack") { return "bubble.left.and.bubble.right" }
            if host.contains("linear") { return "line.3.horizontal.decrease" }
            if host.contains("vercel") { return "triangle" }
            if host.contains("docs") { return "text.alignleft" }
            if host.contains("mail") || host.contains("inbox") { return "envelope" }
        }
        return "globe"
    }

    private func configuredSymbol(named symbolName: String, pointSize: CGFloat, accessibilityDescription: String?) -> NSImage? {
        let configuration = NSImage.SymbolConfiguration(pointSize: pointSize, weight: .medium)
        return NSImage(systemSymbolName: symbolName, accessibilityDescription: accessibilityDescription)?.withSymbolConfiguration(configuration)
    }

    private func surfaceAppearance(for state: AppearanceState) -> SurfaceAppearance {
        SurfaceAppearance(
            backgroundColor: backgroundColor(for: state),
            borderColor: borderColor(for: state),
            shadowColor: shadowColor(for: state),
            shadowRadius: shadowRadius(for: state),
            shadowOffset: shadowOffset(for: state)
        )
    }

    private func applyAppearanceTransition(from previousAppearance: SurfaceAppearance, to nextAppearance: SurfaceAppearance, animated: Bool) {
        guard animated, window != nil else {
            applyAppearance(nextAppearance, animated: false)
            return
        }

        applyAppearance(nextAppearance, animated: false)
        animateLayerColor(keyPath: "backgroundColor", from: previousAppearance.backgroundColor.cgColor, to: nextAppearance.backgroundColor.cgColor, key: "favoriteTile.backgroundColor")
        animateLayerColor(keyPath: "borderColor", from: previousAppearance.borderColor.cgColor, to: nextAppearance.borderColor.cgColor, key: "favoriteTile.borderColor")
        animateLayerColor(keyPath: "shadowColor", from: previousAppearance.shadowColor.cgColor, to: nextAppearance.shadowColor.cgColor, key: "favoriteTile.shadowColor")
        animateLayerScalar(keyPath: "shadowRadius", from: previousAppearance.shadowRadius, to: nextAppearance.shadowRadius, key: "favoriteTile.shadowRadius")
        animateLayerSize(keyPath: "shadowOffset", from: previousAppearance.shadowOffset, to: nextAppearance.shadowOffset, key: "favoriteTile.shadowOffset")
    }

    private func applyAppearance(_ appearance: SurfaceAppearance, animated: Bool) {
        let updates = {
            self.layer?.backgroundColor = appearance.backgroundColor.cgColor
            self.layer?.borderColor = appearance.borderColor.cgColor
            self.layer?.shadowColor = appearance.shadowColor.cgColor
            self.layer?.shadowRadius = appearance.shadowRadius
            self.layer?.shadowOffset = appearance.shadowOffset
        }

        if animated {
            NSAnimationContext.runAnimationGroup { context in
                context.duration = AnimationMetrics.paletteTransitionDuration
                context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                context.allowsImplicitAnimation = true
                updates()
            }
        } else {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            updates()
            CATransaction.commit()
        }
    }

    private func animateLayerColor(keyPath: String, from: CGColor, to: CGColor, key: String) {
        guard let layer else { return }
        let animation = CABasicAnimation(keyPath: keyPath)
        animation.fromValue = from
        animation.toValue = to
        animation.duration = AnimationMetrics.paletteTransitionDuration
        animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        layer.add(animation, forKey: key)
    }

    private func animateLayerScalar(keyPath: String, from: CGFloat, to: CGFloat, key: String) {
        guard let layer else { return }
        let animation = CABasicAnimation(keyPath: keyPath)
        animation.fromValue = from
        animation.toValue = to
        animation.duration = AnimationMetrics.paletteTransitionDuration
        animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        layer.add(animation, forKey: key)
    }

    private func animateLayerSize(keyPath: String, from: NSSize, to: NSSize, key: String) {
        guard let layer else { return }
        let animation = CABasicAnimation(keyPath: keyPath)
        animation.fromValue = NSValue(size: from)
        animation.toValue = NSValue(size: to)
        animation.duration = AnimationMetrics.paletteTransitionDuration
        animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        layer.add(animation, forKey: key)
    }

    private var dragStartEvent: NSEvent?
    private var dragStartLocation: NSPoint?
    private var didStartDrag = false
    var onDragSessionWillBegin: (() -> Void)?
    var onDragSessionEnded: (() -> Void)?

    override func mouseDown(with event: NSEvent) {
        dragStartEvent = event
        dragStartLocation = convert(event.locationInWindow, from: nil)
        didStartDrag = false
    }

    override func mouseDragged(with event: NSEvent) {
        guard !didStartDrag, let tabId = tabId, let startLocation = dragStartLocation, let startEvent = dragStartEvent else { return }
        let currentLocation = convert(event.locationInWindow, from: nil)
        let dx = currentLocation.x - startLocation.x
        let dy = currentLocation.y - startLocation.y
        guard (dx * dx + dy * dy) > 9 else { return }

        didStartDrag = true
        onDragSessionWillBegin?()
        let pasteboardItem = NSPasteboardItem()
        let json = "{\"type\":\"tab\",\"id\":\"\(tabId)\",\"origin\":\"favorites\"}"
        pasteboardItem.setString(json, forType: SidebarDragController.dragType)
        let draggingItem = NSDraggingItem(pasteboardWriter: pasteboardItem)
        draggingItem.setDraggingFrame(bounds, contents: snapshot())
        beginDraggingSession(with: [draggingItem], event: startEvent, source: self)
    }

    override func mouseUp(with event: NSEvent) {
        if !didStartDrag {
            onClick?()
        }
        dragStartEvent = nil
        dragStartLocation = nil
        didStartDrag = false
    }

    private func snapshot() -> NSImage {
        let image = NSImage(size: bounds.size)
        image.lockFocus()
        if let ctx = NSGraphicsContext.current?.cgContext {
            layer?.render(in: ctx)
        }
        image.unlockFocus()
        return image
    }

    override func rightMouseDown(with event: NSEvent) {
        if let menu = menuProvider?() ?? self.menu {
            NSMenu.popUpContextMenu(menu, with: event, for: self)
        } else {
            super.rightMouseDown(with: event)
        }
    }

    override func hitTest(_ point: NSPoint) -> NSView? {
        let local = convert(point, from: superview)
        return bounds.contains(local) ? self : nil
    }
}

extension FavoriteTileView: NSDraggingSource {
    func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
        return .move
    }

    func draggingSession(_ session: NSDraggingSession, endedAt screenPoint: NSPoint, operation: NSDragOperation) {
        defer { onDragSessionEnded?() }
        if operation == [] {
            guard let tabId = tabId,
                  let gridView = superview as? FavoritesGridView,
                  let window = gridView.window else { return }
            let windowPoint = window.convertPoint(fromScreen: screenPoint)
            let gridPoint = gridView.convert(windowPoint, from: nil)
            if !gridView.bounds.contains(gridPoint) {
                gridView.onFavoriteRemove?(tabId)
            }
        }
    }
}
