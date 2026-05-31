import AppKit

final class SpaceSwitcherView: NSView, CoreUpdateObserver {
    typealias DisplayItem = SpaceSwitcherDisplayItem

    static let dragType = SpaceSwitcherDrag.pasteboardType

    private let topBorder = NSView()
    private let scrollView = NSScrollView()
    private let stripContainerView = NSView()
    private let itemsStackView = NSStackView()
    private let dropIndicatorView = NSView()
    private let fadeMask = CAGradientLayer()

    private var actionPopover: NSPopover?
    private var pendingActionAfterPopoverClose: (() -> Void)?
    private var actionPopoverEventMonitor: Any?

    private var spaces: [SpaceViewModel] = []
    private var itemViews: [SpaceSwitcherItemView] = []
    private var selectedSpaceId: SpaceId?
    private var activeSpaceId: SpaceId?
    private var pendingDropInsertionIndex: Int?
    private var isInternalReload = false
    private var lastDisplayItemIdentifiers: [String] = []
    private var profileBadgeColorsById: [ProfileId: NSColor] = [:]
    private var shouldShowProfileBadges = false

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
        reloadSpaces()
        CoreUpdateDispatcher.shared.register(self)
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
        reloadSpaces()
        CoreUpdateDispatcher.shared.register(self)
    }

    deinit {
        actionPopover?.close()
        removeActionPopoverEventMonitor()
        NotificationCenter.default.removeObserver(self)
        CoreUpdateDispatcher.shared.unregister(self)
    }

    override var acceptsFirstResponder: Bool { true }

    private func setupUI() {
        wantsLayer = true
        registerForDraggedTypes([SpaceSwitcherDrag.pasteboardType])

        topBorder.translatesAutoresizingMaskIntoConstraints = false
        topBorder.wantsLayer = true
        topBorder.layer?.backgroundColor = NSColor.clear.cgColor
        addSubview(topBorder)

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.drawsBackground = false
        scrollView.hasVerticalScroller = false
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true
        scrollView.borderType = .noBorder
        scrollView.contentView.wantsLayer = true
        scrollView.contentView.postsBoundsChangedNotifications = true
        fadeMask.startPoint = CGPoint(x: 0, y: 0.5)
        fadeMask.endPoint = CGPoint(x: 1, y: 0.5)
        fadeMask.colors = [NSColor.white.cgColor, NSColor.white.cgColor]
        fadeMask.locations = [0, 1]
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(scrollViewDidScroll),
            name: NSView.boundsDidChangeNotification,
            object: scrollView.contentView
        )

        stripContainerView.translatesAutoresizingMaskIntoConstraints = false
        stripContainerView.wantsLayer = true
        stripContainerView.layer?.backgroundColor = NSColor.clear.cgColor
        stripContainerView.layer?.cornerRadius = 12
        scrollView.documentView = stripContainerView

        itemsStackView.translatesAutoresizingMaskIntoConstraints = false
        itemsStackView.orientation = .horizontal
        itemsStackView.alignment = .centerY
        itemsStackView.distribution = .fill
        itemsStackView.spacing = 2
        itemsStackView.setHuggingPriority(.required, for: .horizontal)
        itemsStackView.setContentCompressionResistancePriority(.required, for: .horizontal)
        stripContainerView.addSubview(itemsStackView)

        dropIndicatorView.translatesAutoresizingMaskIntoConstraints = true
        dropIndicatorView.wantsLayer = true
        dropIndicatorView.layer?.backgroundColor = MahoAppColors.dropIndicator.cgColor
        dropIndicatorView.layer?.cornerRadius = 1
        dropIndicatorView.alphaValue = 0
        dropIndicatorView.isHidden = true
        stripContainerView.addSubview(dropIndicatorView)

        addSubview(scrollView)

        NSLayoutConstraint.activate([
            topBorder.topAnchor.constraint(equalTo: topAnchor),
            topBorder.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            topBorder.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -8),
            topBorder.heightAnchor.constraint(equalToConstant: 1),

            scrollView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            scrollView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -8),
            scrollView.centerYAnchor.constraint(equalTo: centerYAnchor),
            scrollView.topAnchor.constraint(equalTo: topAnchor, constant: 2),
            scrollView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -2),
            scrollView.heightAnchor.constraint(equalToConstant: 20),
            stripContainerView.heightAnchor.constraint(equalTo: scrollView.heightAnchor),
            stripContainerView.widthAnchor.constraint(greaterThanOrEqualTo: scrollView.widthAnchor),

            itemsStackView.centerXAnchor.constraint(equalTo: stripContainerView.centerXAnchor),
            itemsStackView.leadingAnchor.constraint(greaterThanOrEqualTo: stripContainerView.leadingAnchor),
            itemsStackView.topAnchor.constraint(equalTo: stripContainerView.topAnchor),
            itemsStackView.bottomAnchor.constraint(equalTo: stripContainerView.bottomAnchor),
            stripContainerView.trailingAnchor.constraint(greaterThanOrEqualTo: itemsStackView.trailingAnchor),

            heightAnchor.constraint(equalToConstant: 28),
        ])

        updateFadeMask()
        refreshAppearance()
    }

    func refreshAppearance() {
        let appearance = window?.effectiveAppearance ?? effectiveAppearance
        appearance.performAsCurrentDrawingAppearance {
            layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
            topBorder.layer?.backgroundColor = MahoAppColors.spaceStripBorder.cgColor
        }
        for itemView in itemViews {
            itemView.refreshAppearance()
        }
    }

    private func reloadSpaces() {
        let previousSelection = selectedSpaceId

        spaces = sortedSpaces(from: MahoBridge.shared.getSpaceViewModels())
        activeSpaceId = spaces.first(where: { $0.isActive })?.id ?? MahoBridge.shared.getActiveSpaceId()

        if let previousSelection,
           spaces.contains(where: { $0.id == previousSelection }) {
            selectedSpaceId = previousSelection
        } else {
            selectedSpaceId = activeSpaceId ?? spaces.first?.id
        }

        rebuildItems()
    }

    private func sortedSpaces(from rawSpaces: [SpaceViewModel]) -> [SpaceViewModel] {
        rawSpaces.sortedByCanonicalOrder()
    }

    private func rebuildItems() {
        isInternalReload = true
        defer { isInternalReload = false }

        refreshProfileBadgeConfiguration()

        let displayItems = displayItems()
        let nextIdentifiers = displayItems.map(\.id)
        let shouldAnimateRefresh = window != nil && lastDisplayItemIdentifiers != nextIdentifiers
        lastDisplayItemIdentifiers = nextIdentifiers

        for itemView in itemViews {
            itemsStackView.removeArrangedSubview(itemView)
            itemView.removeFromSuperview()
        }
        itemViews.removeAll()

        for (index, item) in displayItems.enumerated() {
            let itemView = SpaceSwitcherItemView(item: item)
            itemView.delegate = self
            itemView.index = index
            itemView.setAccessibilityIdentifier("sidebar.space.\(index)")
            itemView.setAccessibilityLabel(item.accessibilityLabel)
            itemView.setAccessibilityRole(NSAccessibility.Role.button)
            itemView.isSelected = item.space?.id == selectedSpaceId
            itemView.configureProfileBadge(
                color: item.profileId.flatMap { profileBadgeColorsById[$0] },
                visible: shouldShowProfileBadges
            )
            itemView.alphaValue = shouldAnimateRefresh ? 0 : 1
            itemViews.append(itemView)
            itemsStackView.addArrangedSubview(itemView)
        }

        updateButtons()
        layoutSubtreeIfNeeded()
        ensureDocumentLayout()
        layoutSubtreeIfNeeded()
        updateDropIndicatorFrame()
        updateFadeMask()

        guard shouldAnimateRefresh else { return }
        NSAnimationContext.runAnimationGroup { context in
            context.duration = 0.16
            context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            itemViews.forEach { $0.animator().alphaValue = 1 }
        }
    }

    private func displayItems() -> [DisplayItem] {
        spaces.map { space in
            DisplayItem(
                id: stringIdentifier(for: space.id),
                space: space,
                name: space.name,
                color: space.color,
                theme: space.theme,
                tabCount: space.tabCount,
                isActive: space.id == activeSpaceId,
                icon: space.icon,
                profileName: space.profileName,
                profileId: space.profileId,
                orderIndex: space.orderIndex
            )
        }
    }

    private func refreshProfileBadgeConfiguration() {
        guard let jsonString = MahoBridge.shared.listProfiles(),
              let data = jsonString.data(using: .utf8),
              let profiles = try? JSONDecoder().decode([ProfileConfig].self, from: data) else {
            shouldShowProfileBadges = false
            profileBadgeColorsById = [:]
            return
        }

        shouldShowProfileBadges = profiles.count >= 2
        profileBadgeColorsById = Dictionary(
            uniqueKeysWithValues: profiles.map { profile in
                (profile.id, NSColor.fromProfileAvatarString(profile.avatarColor))
            }
        )
    }


    private func updateButtons() {}

    private func refreshSelectionState() {
        for itemView in itemViews {
            itemView.isSelected = itemView.spaceId == selectedSpaceId
        }
        updateButtons()
    }

    private func moveSelection(offset: Int) {
        guard !spaces.isEmpty else { return }
        let currentIndex = spaces.firstIndex(where: { $0.id == (selectedSpaceId ?? activeSpaceId ?? spaces[0].id) }) ?? 0
        let targetIndex = min(max(currentIndex + offset, 0), spaces.count - 1)
        guard targetIndex != currentIndex else { return }
        let targetSpace = spaces[targetIndex]
        selectedSpaceId = targetSpace.id
        refreshSelectionState()
        activate(spaceId: targetSpace.id)
    }

    private func activate(spaceId: SpaceId) {
        selectedSpaceId = spaceId
        CoreUpdateDispatcher.shared.sendAndDispatch(.activateSpace(spaceId: spaceId))
    }

    private func stringIdentifier(for spaceId: SpaceId) -> String {
        "\(spaceId)"
    }

    private func defaultColor(for index: Int) -> SpaceColor {
        let palette: [SpaceColor] = [
            SpaceColor(hue: 0.58, saturation: 0.82, brightness: 0.94),
            SpaceColor(hue: 0.75, saturation: 0.80, brightness: 0.92),
            SpaceColor(hue: 0.35, saturation: 0.76, brightness: 0.86),
            SpaceColor(hue: 0.04, saturation: 0.80, brightness: 0.92),
            SpaceColor(hue: 0.15, saturation: 0.76, brightness: 0.94),
            SpaceColor(hue: 0.92, saturation: 0.74, brightness: 0.92),
        ]
        return palette[index % palette.count]
    }

    @objc func handleAddButtonClick(_ sender: NSButton) {
        addSpaceClicked(sender)
    }

    @objc private func addSpaceClicked(_ sender: NSView?) {
        if let actionPopover, actionPopover.isShown {
            pendingActionAfterPopoverClose = nil
            actionPopover.performClose(sender)
            return
        }

        let popover = NSPopover()
        popover.behavior = .transient
        popover.animates = true
        popover.delegate = self

        let controller = SpaceSwitcherActionPopoverController()
        controller.onSelect = { [weak self] item in
            self?.handleActionMenuSelection(item)
        }
        controller.onCancel = { [weak self] in
            self?.closeActionPopover()
        }
        popover.contentViewController = controller

        let anchorView = sender ?? self
        let anchorBounds = sender?.bounds ?? bounds
        popover.show(relativeTo: anchorBounds, of: anchorView, preferredEdge: .minY)
        actionPopover = popover
        installActionPopoverEventMonitor()
    }

    private func handleActionMenuSelection(_ item: SpaceSwitcherActionMenuItem) {
        switch item {
        case .newSpace:
            pendingActionAfterPopoverClose = { [weak self] in
                self?.openCreateSpaceSheet()
            }
            closeActionPopover(animated: false)
        case .newFolder:
            guard let spaceId = targetSpaceIdForActionMenu() else {
                queueActionUnavailableAlert(for: item)
                return
            }
            pendingActionAfterPopoverClose = {
                SpaceContextMenu.createFolder(in: spaceId)
            }
            closeActionPopover(animated: false)
        case .newTab:
            guard let spaceId = targetSpaceIdForActionMenu() else {
                queueActionUnavailableAlert(for: item)
                return
            }
            pendingActionAfterPopoverClose = {
                SpaceContextMenu.createTab(in: spaceId)
            }
            closeActionPopover(animated: false)
        }
    }

    private func closeActionPopover(animated: Bool = true) {
        guard let actionPopover else {
            removeActionPopoverEventMonitor()
            runPendingActionAfterPopoverClose()
            return
        }

        if !actionPopover.isShown {
            self.actionPopover = nil
            removeActionPopoverEventMonitor()
            runPendingActionAfterPopoverClose()
            return
        }

        if !animated {
            actionPopover.animates = false
        }
        actionPopover.performClose(nil)
    }

    private func installActionPopoverEventMonitor() {
        removeActionPopoverEventMonitor()
        actionPopoverEventMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]) { [weak self] event in
            guard let self, let actionPopover, actionPopover.isShown else { return event }
            guard let popoverWindow = actionPopover.contentViewController?.view.window else { return event }

            if event.window !== popoverWindow {
                self.pendingActionAfterPopoverClose = nil
                self.closeActionPopover(animated: false)
            }

            return event
        }
    }

    private func removeActionPopoverEventMonitor() {
        if let actionPopoverEventMonitor {
            NSEvent.removeMonitor(actionPopoverEventMonitor)
            self.actionPopoverEventMonitor = nil
        }
    }

    private func runPendingActionAfterPopoverClose() {
        let action = pendingActionAfterPopoverClose
        pendingActionAfterPopoverClose = nil
        action?()
    }

    private func openCreateSpaceSheet() {
        let profileId = MahoBridge.shared.getActiveProfileId() ?? ""
        SpaceContextMenu.presentCreateSpaceSheet(
            defaultColor: defaultColor(for: spaces.count),
            profileId: profileId,
            from: self
        )
    }

    private func queueActionUnavailableAlert(for item: SpaceSwitcherActionMenuItem) {
        pendingActionAfterPopoverClose = { [weak self] in
            self?.presentActionUnavailableAlert(for: item)
        }
        closeActionPopover(animated: false)
    }

    private func presentActionUnavailableAlert(for item: SpaceSwitcherActionMenuItem) {
        let alert = NSAlert()
        alert.alertStyle = .informational
        alert.messageText = "Can't \(item.title) Right Now"
        alert.informativeText = "Select or open a space first, then try again."
        alert.addButton(withTitle: "OK")

        if let window = window ?? NSApp.mainWindow ?? NSApp.keyWindow {
            alert.beginSheetModal(for: window)
        } else {
            alert.runModal()
        }
    }

    private func targetSpaceIdForActionMenu() -> SpaceId? {
        selectedSpaceId ?? activeSpaceId ?? spaces.first?.id ?? MahoBridge.shared.getActiveSpaceId()
    }

    private func configureIconButton(
        _ button: NSButton,
        symbolName: String,
        toolTip: String,
        action: Selector,
        accessibilityIdentifier: String? = nil
    ) {
        button.translatesAutoresizingMaskIntoConstraints = false
        button.isBordered = false
        button.bezelStyle = .regularSquare
        button.image = configuredSymbol(named: symbolName, pointSize: 12, accessibilityDescription: toolTip)
        button.contentTintColor = MahoAppColors.spaceControlTint
        button.imageScaling = .scaleProportionallyDown
        button.target = self
        button.action = action
        button.toolTip = toolTip
        button.focusRingType = .exterior
        button.setAccessibilityRole(.button)
        if let accessibilityIdentifier {
            button.setAccessibilityIdentifier(accessibilityIdentifier)
        }
        NSLayoutConstraint.activate([
            button.widthAnchor.constraint(equalToConstant: 24),
            button.heightAnchor.constraint(equalToConstant: 24),
        ])
    }

    private func configuredSymbol(named symbolName: String, pointSize: CGFloat, accessibilityDescription: String?) -> NSImage? {
        return MahoIcon.imageForSFSymbol(symbolName, pointSize: pointSize, accessibilityDescription: accessibilityDescription)
    }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 123:
            moveSelection(offset: -1)
        case 124:
            moveSelection(offset: 1)
        case 36, 49:
            if let selectedSpaceId {
                activate(spaceId: selectedSpaceId)
            }
        case 45:
            if event.modifierFlags.intersection(.deviceIndependentFlagsMask).contains(.command) {
                addSpaceClicked(nil)
            } else {
                super.keyDown(with: event)
            }
        default:
            super.keyDown(with: event)
        }
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            switch update {
            case .fullState,
                 .spaceCreated,
                 .spaceDeleted,
                 .spaceUpdated,
                 .spaceOrderChanged,
                 .spaceConfigUpdated,
                 .activeSpaceChanged(_, _),
                 .activeProfileChanged:
                self.reloadSpaces()
            default:
                break
            }
        }
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        updateDropTarget(with: sender)
    }

    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        updateDropTarget(with: sender)
    }

    override func draggingExited(_ sender: NSDraggingInfo?) {
        pendingDropInsertionIndex = nil
        updateDropIndicatorFrame()
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        guard let draggingSpaceId = draggingSpaceId(from: sender.draggingPasteboard),
              let fromIndex = spaces.firstIndex(where: { $0.id == draggingSpaceId }),
              let proposedInsertionIndex = insertionIndex(for: sender),
              spaces.count > 1 else {
            pendingDropInsertionIndex = nil
            updateDropIndicatorFrame()
            return false
        }

        let finalIndex = proposedInsertionIndex - (fromIndex < proposedInsertionIndex ? 1 : 0)
        pendingDropInsertionIndex = nil
        updateDropIndicatorFrame()

        guard finalIndex >= 0,
              finalIndex < spaces.count,
              finalIndex != fromIndex else {
            return false
        }

        CoreUpdateDispatcher.shared.sendAndDispatch(.reorderSpace(spaceId: draggingSpaceId, from: fromIndex, to: finalIndex))
        return true
    }

    override func prepareForDragOperation(_ sender: NSDraggingInfo) -> Bool {
        draggingSpaceId(from: sender.draggingPasteboard) != nil
    }

    private func updateDropTarget(with draggingInfo: NSDraggingInfo) -> NSDragOperation {
        guard let draggingSpaceId = draggingSpaceId(from: draggingInfo.draggingPasteboard),
              spaces.contains(where: { $0.id == draggingSpaceId }),
              spaces.count > 1,
              let insertionIndex = insertionIndex(for: draggingInfo) else {
            pendingDropInsertionIndex = nil
            updateDropIndicatorFrame()
            return []
        }

        pendingDropInsertionIndex = insertionIndex
        updateDropIndicatorFrame()
        return .move
    }

    private func draggingSpaceId(from pasteboard: NSPasteboard) -> SpaceId? {
        guard let rawValue = pasteboard.string(forType: Self.dragType), !rawValue.isEmpty else {
            return nil
        }
        return SpaceId(rawValue)
    }

    private func insertionIndex(for draggingInfo: NSDraggingInfo) -> Int? {
        guard !itemViews.isEmpty else { return 0 }
        let location = stripContainerView.convert(draggingInfo.draggingLocation, from: nil)
        let sortedViews = itemViews.sorted { $0.frame.minX < $1.frame.minX }
        for (index, itemView) in sortedViews.enumerated() {
            if location.x < itemView.frame.midX {
                return index
            }
        }
        return sortedViews.count
    }

    private func ensureDocumentLayout() {
        let visibleWidth = max(scrollView.contentView.bounds.width, 0)
        let contentWidth = max(itemsStackView.fittingSize.width, 0)
        let targetWidth = max(visibleWidth, contentWidth)
        let targetHeight = max(scrollView.bounds.height, 20)

        guard targetWidth > 0, targetHeight > 0 else { return }

        var frame = stripContainerView.frame
        if abs(frame.size.width - targetWidth) > 0.5 ||
            abs(frame.size.height - targetHeight) > 0.5 ||
            frame.origin != .zero {
            frame.origin = .zero
            frame.size = CGSize(width: targetWidth, height: targetHeight)
            stripContainerView.frame = frame
        }
    }

    private func updateDropIndicatorFrame() {
        guard let pendingDropInsertionIndex else {
            dropIndicatorView.isHidden = true
            dropIndicatorView.alphaValue = 0
            return
        }

        let xPosition: CGFloat
        if itemViews.isEmpty {
            xPosition = 0
        } else if pendingDropInsertionIndex <= 0 {
            xPosition = itemViews[0].frame.minX - 3
        } else if pendingDropInsertionIndex >= itemViews.count {
            xPosition = itemViews[itemViews.count - 1].frame.maxX + 3
        } else {
            xPosition = itemViews[pendingDropInsertionIndex].frame.minX - 3
        }

        dropIndicatorView.frame = NSRect(x: xPosition, y: 3, width: 2, height: max(18, stripContainerView.bounds.height - 6))
        dropIndicatorView.isHidden = false
        dropIndicatorView.alphaValue = 1.0
    }

    @objc private func scrollViewDidScroll() {
        updateFadeMask()
    }

    private func updateFadeMask() {
        let contentView = scrollView.contentView
        let bounds = contentView.bounds

        guard bounds.width > 0, bounds.height > 0 else {
            contentView.layer?.mask = nil
            return
        }

        fadeMask.frame = bounds
        contentView.layer?.mask = fadeMask

        guard let documentView = scrollView.documentView else {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            fadeMask.colors = [NSColor.white.cgColor, NSColor.white.cgColor]
            fadeMask.locations = [0, 1]
            CATransaction.commit()
            return
        }

        let width = max(bounds.width, 1)
        let fadeDistance = min(12.0 / width, 0.5)
        let scrollOffset = bounds.origin.x
        let maxScroll = max(documentView.frame.width - bounds.width, 0)
        let needsLeftFade = scrollOffset > 2
        let needsRightFade = scrollOffset < maxScroll - 2

        let colors: [CGColor]
        let locations: [NSNumber]

        switch (needsLeftFade, needsRightFade) {
        case (false, false):
            colors = [NSColor.white.cgColor, NSColor.white.cgColor]
            locations = [0, 1]
        case (true, false):
            colors = [
                NSColor.clear.cgColor,
                NSColor.white.cgColor,
                NSColor.white.cgColor,
            ]
            locations = [0, NSNumber(value: Double(fadeDistance)), 1]
        case (false, true):
            colors = [
                NSColor.white.cgColor,
                NSColor.white.cgColor,
                NSColor.clear.cgColor,
            ]
            locations = [0, NSNumber(value: Double(1 - fadeDistance)), 1]
        case (true, true):
            colors = [
                NSColor.clear.cgColor,
                NSColor.white.cgColor,
                NSColor.white.cgColor,
                NSColor.clear.cgColor,
            ]
            locations = [0, NSNumber(value: Double(fadeDistance)), NSNumber(value: Double(1 - fadeDistance)), 1]
        }

        CATransaction.begin()
        CATransaction.setDisableActions(true)
        fadeMask.colors = colors
        fadeMask.locations = locations
        CATransaction.commit()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    override func layout() {
        super.layout()
        ensureDocumentLayout()
        fadeMask.frame = scrollView.contentView.bounds
        updateDropIndicatorFrame()
        updateFadeMask()
    }
}

extension SpaceSwitcherView: NSPopoverDelegate {
    func popoverDidClose(_ notification: Notification) {
        guard notification.object as? NSPopover === actionPopover else { return }
        removeActionPopoverEventMonitor()
        actionPopover = nil
        runPendingActionAfterPopoverClose()
    }
}

extension SpaceSwitcherView: SpaceSwitcherItemViewDelegate {
    func spaceSwitcherItemViewDidActivate(_ itemView: SpaceSwitcherItemView) {
        guard let spaceId = itemView.spaceId else { return }
        activate(spaceId: spaceId)
    }

    func spaceSwitcherItemViewDidRequestContextMenu(_ itemView: SpaceSwitcherItemView, event: NSEvent) {
        guard let spaceId = itemView.spaceId,
              let space = spaces.first(where: { $0.id == spaceId }) else {
            return
        }

        let menu = SpaceContextMenu.menu(for: space)
        NSMenu.popUpContextMenu(menu, with: event, for: itemView)
    }

    func spaceSwitcherItemViewDidSelect(_ itemView: SpaceSwitcherItemView) {
        if let spaceId = itemView.spaceId {
            selectedSpaceId = spaceId
        }
        if !isInternalReload {
            refreshSelectionState()
        }
        window?.makeFirstResponder(self)
    }
}


private enum SpaceSwitcherActionMenuItem: CaseIterable {
    case newSpace
    case newFolder
    case newTab

    var title: String {
        switch self {
        case .newSpace:
            return "New Space"
        case .newFolder:
            return "New Folder"
        case .newTab:
            return "New Tab"
        }
    }

    var subtitle: String {
        switch self {
        case .newSpace:
            return "Create a fresh workspace"
        case .newFolder:
            return "Organize tabs in the current space"
        case .newTab:
            return "Open a page in the current space"
        }
    }

    var symbolName: String {
        switch self {
        case .newSpace:
            return "square.grid.2x2"
        case .newFolder:
            return "folder"
        case .newTab:
            return "plus.square.on.square"
        }
    }

    var accessibilityLabel: String {
        "\(title), \(subtitle)"
    }
}

private final class SpaceSwitcherActionPopoverController: NSViewController {
    var onSelect: ((SpaceSwitcherActionMenuItem) -> Void)?
    var onCancel: (() -> Void)?

    private let items = SpaceSwitcherActionMenuItem.allCases
    private var rowViews: [SpaceSwitcherActionRowView] = []
    private var highlightedIndex = 0 {
        didSet {
            updateHighlightState()
        }
    }

    override var acceptsFirstResponder: Bool { true }

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 272, height: 170))
        container.wantsLayer = true
        container.layer?.backgroundColor = MahoAppColors.cardBackground.cgColor
        container.layer?.cornerRadius = 16

        let stackView = NSStackView()
        stackView.translatesAutoresizingMaskIntoConstraints = false
        stackView.orientation = .vertical
        stackView.alignment = .leading
        stackView.spacing = 6
        container.addSubview(stackView)

        for (index, item) in items.enumerated() {
            let rowView = SpaceSwitcherActionRowView(item: item)
            rowView.onHover = { [weak self] in
                self?.highlightedIndex = index
            }
            rowView.onSelect = { [weak self] in
                self?.activateItem(at: index)
            }
            rowViews.append(rowView)
            stackView.addArrangedSubview(rowView)
        }

        NSLayoutConstraint.activate([
            stackView.topAnchor.constraint(equalTo: container.topAnchor, constant: 8),
            stackView.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 8),
            stackView.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -8),
            stackView.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -8),
        ])

        view = container
        preferredContentSize = container.frame.size
        updateHighlightState()
    }

    override func viewDidAppear() {
        super.viewDidAppear()
        view.window?.makeFirstResponder(self)
    }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 48:
            let isReverse = event.modifierFlags.intersection(.deviceIndependentFlagsMask).contains(.shift)
            moveHighlight(offset: isReverse ? -1 : 1, wraps: true)
        case 125:
            moveHighlight(offset: 1, wraps: false)
        case 126:
            moveHighlight(offset: -1, wraps: false)
        case 36, 49:
            activateItem(at: highlightedIndex)
        case 53:
            onCancel?()
        default:
            super.keyDown(with: event)
        }
    }

    private func moveHighlight(offset: Int, wraps: Bool) {
        guard !items.isEmpty else { return }

        let proposedIndex = highlightedIndex + offset
        let nextIndex: Int
        if wraps {
            nextIndex = (proposedIndex % items.count + items.count) % items.count
        } else {
            nextIndex = max(0, min(proposedIndex, items.count - 1))
        }

        guard nextIndex != highlightedIndex else { return }
        highlightedIndex = nextIndex
    }

    private func activateItem(at index: Int) {
        guard items.indices.contains(index) else { return }
        onSelect?(items[index])
    }

    private func updateHighlightState() {
        for (index, rowView) in rowViews.enumerated() {
            rowView.isHighlighted = index == highlightedIndex
        }
    }
}

private final class SpaceSwitcherActionRowView: NSView {
    var onHover: (() -> Void)?
    var onSelect: (() -> Void)?

    var isHighlighted: Bool = false {
        didSet {
            updateAppearance()
        }
    }

    private var isPressed = false {
        didSet {
            updateAppearance()
        }
    }

    private let item: SpaceSwitcherActionMenuItem
    private let selectionBackgroundView = NSView()
    private let iconBackgroundView = NSView()
    private let iconImageView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let subtitleLabel = NSTextField(labelWithString: "")
    private var trackingArea: NSTrackingArea?

    init(item: SpaceSwitcherActionMenuItem) {
        self.item = item
        super.init(frame: .zero)
        setupViews()
        updateAppearance()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()

        if let trackingArea {
            removeTrackingArea(trackingArea)
        }

        let trackingArea = NSTrackingArea(
            rect: bounds,
            options: [.activeInKeyWindow, .inVisibleRect, .mouseEnteredAndExited],
            owner: self,
            userInfo: nil
        )
        self.trackingArea = trackingArea
        addTrackingArea(trackingArea)
    }

    override func mouseEntered(with event: NSEvent) {
        super.mouseEntered(with: event)
        onHover?()
    }

    override func mouseExited(with event: NSEvent) {
        super.mouseExited(with: event)
        isPressed = false
    }

    override func mouseDown(with event: NSEvent) {
        isPressed = true
        onHover?()
    }

    override func mouseDragged(with event: NSEvent) {
        super.mouseDragged(with: event)
        let point = convert(event.locationInWindow, from: nil)
        isPressed = bounds.contains(point)
        if isPressed {
            onHover?()
        }
    }

    override func mouseUp(with event: NSEvent) {
        defer { isPressed = false }

        let point = convert(event.locationInWindow, from: nil)
        guard bounds.contains(point) else { return }
        onSelect?()
    }

    private func setupViews() {
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        setAccessibilityElement(true)
        setAccessibilityRole(.button)
        setAccessibilityLabel(item.accessibilityLabel)

        selectionBackgroundView.translatesAutoresizingMaskIntoConstraints = false
        selectionBackgroundView.wantsLayer = true
        selectionBackgroundView.layer?.cornerRadius = 10
        addSubview(selectionBackgroundView)

        iconBackgroundView.translatesAutoresizingMaskIntoConstraints = false
        iconBackgroundView.wantsLayer = true
        iconBackgroundView.layer?.cornerRadius = 8
        addSubview(iconBackgroundView)

        iconImageView.translatesAutoresizingMaskIntoConstraints = false
        iconImageView.imageScaling = .scaleProportionallyDown
        iconImageView.image = MahoIcon.imageForSFSymbol(item.symbolName, pointSize: 13, accessibilityDescription: item.title)
        addSubview(iconImageView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 13, weight: .semibold)
        titleLabel.stringValue = item.title
        titleLabel.lineBreakMode = .byTruncatingTail
        addSubview(titleLabel)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = .systemFont(ofSize: 11, weight: .medium)
        subtitleLabel.stringValue = item.subtitle
        subtitleLabel.lineBreakMode = .byTruncatingTail
        addSubview(subtitleLabel)

        NSLayoutConstraint.activate([
            heightAnchor.constraint(equalToConstant: 48),
            widthAnchor.constraint(equalToConstant: 256),

            selectionBackgroundView.topAnchor.constraint(equalTo: topAnchor),
            selectionBackgroundView.leadingAnchor.constraint(equalTo: leadingAnchor),
            selectionBackgroundView.trailingAnchor.constraint(equalTo: trailingAnchor),
            selectionBackgroundView.bottomAnchor.constraint(equalTo: bottomAnchor),

            iconBackgroundView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            iconBackgroundView.centerYAnchor.constraint(equalTo: centerYAnchor),
            iconBackgroundView.widthAnchor.constraint(equalToConstant: 28),
            iconBackgroundView.heightAnchor.constraint(equalToConstant: 28),

            iconImageView.centerXAnchor.constraint(equalTo: iconBackgroundView.centerXAnchor),
            iconImageView.centerYAnchor.constraint(equalTo: iconBackgroundView.centerYAnchor),
            iconImageView.widthAnchor.constraint(equalToConstant: 16),
            iconImageView.heightAnchor.constraint(equalToConstant: 16),

            titleLabel.topAnchor.constraint(equalTo: topAnchor, constant: 9),
            titleLabel.leadingAnchor.constraint(equalTo: iconBackgroundView.trailingAnchor, constant: 10),
            titleLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),

            subtitleLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 1),
            subtitleLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            subtitleLabel.trailingAnchor.constraint(equalTo: titleLabel.trailingAnchor),
        ])
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        updateAppearance()
    }

    private func updateAppearance() {
        let isEmphasized = isHighlighted || isPressed
        selectionBackgroundView.layer?.backgroundColor = isEmphasized
            ? MahoAppColors.spaceChipSelected.cgColor
            : NSColor.clear.cgColor
        selectionBackgroundView.layer?.borderWidth = 0
        selectionBackgroundView.layer?.borderColor = NSColor.clear.cgColor

        iconBackgroundView.layer?.backgroundColor = isEmphasized
            ? MahoAppColors.spaceChipHover.withAlphaComponent(isPressed ? 1.0 : 0.82).cgColor
            : NSColor.clear.cgColor
        iconImageView.contentTintColor = isEmphasized
            ? MahoAppColors.primaryText
            : MahoAppColors.primaryText
        titleLabel.textColor = MahoAppColors.primaryText
        subtitleLabel.textColor = isEmphasized
            ? MahoAppColors.secondaryText
            : MahoAppColors.secondaryText.withAlphaComponent(0.78)
    }
}
