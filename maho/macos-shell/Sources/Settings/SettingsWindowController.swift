import AppKit

final class SettingsWindowController: NSWindowController {
    private static let defaultContentSize = NSSize(width: 940, height: 980)
    static let mahoAIPaneIdentifier = "maho_ai"

    private let settingsContainerViewController = SettingsContainerViewController()

    init() {
        let window = NSWindow(
            contentRect: NSRect(origin: .zero, size: Self.defaultContentSize),
            styleMask: [.titled, .closable, .miniaturizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.title = "Settings"
        window.center()
        window.titlebarAppearsTransparent = true
        window.titleVisibility = .hidden
        window.isMovableByWindowBackground = true
        if #available(macOS 11.0, *) {
            window.toolbarStyle = .unifiedCompact
        }
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = MahoAppColors.windowBackground

        super.init(window: window)
        window.setAccessibilityIdentifier("settingsWindow")
        window.contentViewController = settingsContainerViewController
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func showWindow(_ sender: Any?) {
        applyDefaultFrameForOpening()
        super.showWindow(sender)
    }

    func selectPane(index: Int) {
        settingsContainerViewController.selectPane(index: index)
    }

    func selectPane(identifier: String) -> String? {
        settingsContainerViewController.selectPane(identifier: identifier)
    }

    func selectedPaneAccessibilityLabel() -> String? {
        settingsContainerViewController.selectedPaneAccessibilityLabel()
    }

    func paneIdentifiers() -> [String] {
        settingsContainerViewController.paneIdentifiers()
    }

    func activePaneLayoutDiagnostics() -> [String: Any] {
        settingsContainerViewController.activePaneLayoutDiagnostics()
    }

    private func applyDefaultFrameForOpening() {
        guard let window else { return }
        guard !window.isVisible else { return }

        let targetFrame = window.frameRect(forContentRect: NSRect(origin: .zero, size: Self.defaultContentSize))
        let targetScreen = window.screen
            ?? NSScreen.screens.first(where: { $0.visibleFrame.intersects(window.frame) })
            ?? NSScreen.main

        var frame = targetFrame

        if let visibleFrame = targetScreen?.visibleFrame {
            frame.origin = NSPoint(
                x: visibleFrame.midX - frame.width / 2,
                y: visibleFrame.midY - frame.height / 2
            )

            if frame.width > visibleFrame.width {
                frame.size.width = visibleFrame.width
                frame.origin.x = visibleFrame.minX
            } else {
                frame.origin.x = min(max(frame.origin.x, visibleFrame.minX), visibleFrame.maxX - frame.width)
            }

            if frame.height > visibleFrame.height {
                frame.size.height = visibleFrame.height
                frame.origin.y = visibleFrame.minY
            } else {
                frame.origin.y = min(max(frame.origin.y, visibleFrame.minY), visibleFrame.maxY - frame.height)
            }
        }

        guard !window.frame.equalTo(frame) else { return }
        window.setFrame(frame, display: false)
    }
}

protocol SettingsPaneRefreshable {
    func refreshSettingsContent()
}

private struct SettingsPane {
    let identifier: String
    let title: String
    let imageName: String
    let makeViewController: () -> NSViewController
}

private final class SettingsContainerViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate {
    private let panes: [SettingsPane] = [
        SettingsPane(identifier: "general", title: "General", imageName: "gearshape", makeViewController: { GeneralSettingsViewController() }),
        SettingsPane(identifier: "appearance", title: "Appearance", imageName: "paintbrush", makeViewController: { AppearanceSettingsViewController() }),
        SettingsPane(identifier: "privacy", title: "Privacy", imageName: "shield", makeViewController: { PrivacySettingsViewController() }),
        SettingsPane(identifier: "shortcuts", title: "Shortcuts", imageName: "keyboard", makeViewController: { ShortcutsSettingsViewController() }),
        SettingsPane(identifier: "profiles", title: "Profiles", imageName: "person.2", makeViewController: { ProfilesSettingsViewController() }),
        SettingsPane(identifier: "advanced", title: "Advanced", imageName: "gearshape.2", makeViewController: { AdvancedSettingsViewController() }),
        SettingsPane(identifier: "notifications", title: "Notifications", imageName: "bell", makeViewController: { NotificationSettingsViewController() }),
        SettingsPane(identifier: "autofill", title: "Autofill", imageName: "key", makeViewController: { AutofillSettingsViewController() }),
        SettingsPane(identifier: "content_blocker", title: "Content Blocker", imageName: "shield.lefthalf.filled", makeViewController: { ContentBlockerSettingsViewController() }),
        SettingsPane(identifier: "atc", title: "Air Traffic Control", imageName: "arrow.triangle.branch", makeViewController: { ATCSettingsViewController() }),
        SettingsPane(identifier: "passwords", title: "Passwords", imageName: "lock", makeViewController: { PasswordsSettingsViewController() }),
        SettingsPane(identifier: "account_sync", title: "Account & Sync", imageName: "person.crop.circle", makeViewController: { AccountSyncSettingsViewController() }),
        SettingsPane(identifier: "css_mods", title: "CSS Mods", imageName: "paintbrush.pointed", makeViewController: { CssModsSettingsViewController() }),
        SettingsPane(identifier: "extensions", title: "Extensions", imageName: "puzzlepiece.extension", makeViewController: { ExtensionsSettingsViewController() }),
        SettingsPane(identifier: SettingsWindowController.mahoAIPaneIdentifier, title: "Maho AI", imageName: "sparkles", makeViewController: { AISettingsViewController() }),
    ]

    private let splitView = NSSplitView()
    private let sidebarContainer = NSVisualEffectView()
    private let sidebarScrollView = NSScrollView()
    private let sidebarTableView = NSTableView()
    private let contentContainer = SettingsContentContainerView()

    private var selectedPaneIndex = 0
    private var activePaneViewController: NSViewController?
    private var paneViewControllers: [String: NSViewController] = [:]

    override func loadView() {
        view = NSView()
        view.wantsLayer = true
        view.layer?.backgroundColor = MahoAppColors.contentBackground.cgColor
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        setupSplitView()
        setupSidebar()
        configurePanesAccessibility()
        selectPane(index: selectedPaneIndex)
    }

    func selectPane(index: Int) {
        guard panes.indices.contains(index) else { return }
        let previousSelectedPaneIndex = selectedPaneIndex
        selectedPaneIndex = index

        if sidebarTableView.selectedRow != index {
            sidebarTableView.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
        }
        sidebarTableView.scrollRowToVisible(index)

        if previousSelectedPaneIndex != index {
            sidebarTableView.reloadData(
                forRowIndexes: IndexSet([previousSelectedPaneIndex, index]),
                columnIndexes: IndexSet(integer: 0)
            )
        }

        let pane = panes[index]
        transition(to: paneViewController(for: pane))
        view.window?.title = "Settings"
    }

    func selectPane(identifier: String) -> String? {
        guard let index = panes.firstIndex(where: { $0.identifier == identifier }) else { return nil }
        selectPane(index: index)
        return panes[index].identifier
    }

    func paneIdentifiers() -> [String] {
        panes.map(\.identifier)
    }

    func selectedPaneAccessibilityLabel() -> String? {
        guard panes.indices.contains(selectedPaneIndex) else { return nil }
        return paneViewController(for: panes[selectedPaneIndex]).view.accessibilityLabel()
    }

    func activePaneLayoutDiagnostics() -> [String: Any] {
        guard panes.indices.contains(selectedPaneIndex) else {
            return ["error": "No selected settings pane"]
        }

        let pane = panes[selectedPaneIndex]
        let paneViewController = self.paneViewController(for: pane)
        let paneView = paneViewController.view

        view.layoutSubtreeIfNeeded()
        paneView.layoutSubtreeIfNeeded()

        return [
            "paneIdentifier": pane.identifier,
            "paneTitle": pane.title,
            "window": diagnosticsForView(view.window?.contentView),
            "settingsContainer": diagnosticsForView(view),
            "contentContainer": diagnosticsForView(contentContainer),
            "paneRoot": diagnosticsForView(paneView),
            "paneSubviewTree": diagnosticsTree(for: paneView, depth: 0, maxDepth: 5)
        ]
    }

    private func setupSplitView() {
        splitView.translatesAutoresizingMaskIntoConstraints = false
        splitView.isVertical = true
        splitView.dividerStyle = .thin

        sidebarContainer.translatesAutoresizingMaskIntoConstraints = false
        sidebarContainer.material = .sidebar
        sidebarContainer.blendingMode = .behindWindow
        sidebarContainer.state = .active
        sidebarContainer.wantsLayer = true
        sidebarContainer.layer?.backgroundColor = MahoAppColors.sidebarBackground.withAlphaComponent(0.92).cgColor
        sidebarContainer.layer?.borderColor = MahoAppColors.contentBorder.withAlphaComponent(0.42).cgColor
        sidebarContainer.layer?.borderWidth = 1

        contentContainer.translatesAutoresizingMaskIntoConstraints = false
        contentContainer.wantsLayer = true
        contentContainer.layer?.backgroundColor = MahoAppColors.contentBackground.cgColor

        splitView.addArrangedSubview(sidebarContainer)
        splitView.addArrangedSubview(contentContainer)
        view.addSubview(splitView)

        NSLayoutConstraint.activate([
            splitView.topAnchor.constraint(equalTo: view.topAnchor),
            splitView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            splitView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            splitView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            sidebarContainer.widthAnchor.constraint(equalToConstant: 222),
        ])

        splitView.setHoldingPriority(.defaultHigh, forSubviewAt: 0)
    }

    private func setupSidebar() {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("SettingsSidebarColumn"))
        column.resizingMask = .autoresizingMask
        sidebarTableView.addTableColumn(column)
        sidebarTableView.headerView = nil
        sidebarTableView.backgroundColor = .clear
        sidebarTableView.focusRingType = .none
        sidebarTableView.intercellSpacing = NSSize(width: 0, height: 4)
        sidebarTableView.rowHeight = 40
        sidebarTableView.rowSizeStyle = .custom
        sidebarTableView.selectionHighlightStyle = .none
        sidebarTableView.delegate = self
        sidebarTableView.dataSource = self
        sidebarTableView.allowsTypeSelect = false
        sidebarTableView.allowsColumnSelection = false
        sidebarTableView.translatesAutoresizingMaskIntoConstraints = false
        sidebarTableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle

        sidebarScrollView.translatesAutoresizingMaskIntoConstraints = false
        sidebarScrollView.drawsBackground = false
        sidebarScrollView.borderType = .noBorder
        sidebarScrollView.hasVerticalScroller = true
        sidebarScrollView.autohidesScrollers = true
        sidebarScrollView.documentView = sidebarTableView

        sidebarContainer.addSubview(sidebarScrollView)

        let sidebarTopInset: CGFloat = 36

        NSLayoutConstraint.activate([
            sidebarScrollView.topAnchor.constraint(equalTo: sidebarContainer.topAnchor, constant: sidebarTopInset),
            sidebarScrollView.leadingAnchor.constraint(equalTo: sidebarContainer.leadingAnchor, constant: 10),
            sidebarScrollView.trailingAnchor.constraint(equalTo: sidebarContainer.trailingAnchor, constant: -10),
            sidebarScrollView.bottomAnchor.constraint(equalTo: sidebarContainer.bottomAnchor, constant: -18),
        ])
    }

    private func configurePanesAccessibility() {
    }

    private func paneViewController(for pane: SettingsPane) -> NSViewController {
        if let existingViewController = paneViewControllers[pane.identifier] {
            return existingViewController
        }

        let viewController = pane.makeViewController()
        viewController.view.setAccessibilityLabel("\(pane.title) Settings")
        paneViewControllers[pane.identifier] = viewController
        return viewController
    }

    private func transition(to viewController: NSViewController) {
        guard activePaneViewController !== viewController else { return }

        if let current = activePaneViewController {
            current.view.removeFromSuperview()
            current.removeFromParent()
        }

        addChild(viewController)
        let paneView = viewController.view
        contentContainer.setContentView(paneView)

        (viewController as? SettingsPaneRefreshable)?.refreshSettingsContent()
        activePaneViewController = viewController
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        panes.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let identifier = NSUserInterfaceItemIdentifier("SettingsSidebarCell")
        let cell = (tableView.makeView(withIdentifier: identifier, owner: self) as? SettingsSidebarCellView)
            ?? SettingsSidebarCellView(frame: .zero)
        cell.identifier = identifier
        cell.configure(with: panes[row], selected: row == selectedPaneIndex)
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = sidebarTableView.selectedRow
        guard panes.indices.contains(row), row != selectedPaneIndex else { return }
        selectPane(index: row)
    }

    func tableView(_ tableView: NSTableView, rowViewForRow row: Int) -> NSTableRowView? {
        SettingsSidebarRowView()
    }
}

private func diagnosticsTree(for view: NSView?, depth: Int, maxDepth: Int) -> [String: Any] {
    guard let view else {
        return ["missing": true]
    }

    var result = diagnosticsForView(view)
    if depth >= maxDepth {
        result["childrenTruncated"] = true
        return result
    }

    result["children"] = view.subviews.map { diagnosticsTree(for: $0, depth: depth + 1, maxDepth: maxDepth) }
    return result
}

private func diagnosticsForView(_ view: NSView?) -> [String: Any] {
    guard let view else {
        return ["missing": true]
    }

    view.layoutSubtreeIfNeeded()

    let frame = view.frame.integral
    let fittingSize = view.fittingSize
    let intrinsicSize = view.intrinsicContentSize
    let horizontalConstraints = view.constraintsAffectingLayout(for: .horizontal)
    let verticalConstraints = view.constraintsAffectingLayout(for: .vertical)

    return [
        "className": String(describing: type(of: view)),
        "accessibilityIdentifier": view.accessibilityIdentifier(),
        "accessibilityLabel": view.accessibilityLabel() ?? "",
        "frame": rectDictionary(frame),
        "isHidden": view.isHidden,
        "subviewCount": view.subviews.count,
        "fittingSize": sizeDictionary(fittingSize),
        "intrinsicContentSize": sizeDictionary(intrinsicSize),
        "hasAmbiguousLayout": view.hasAmbiguousLayout,
        "horizontalHuggingPriority": view.contentHuggingPriority(for: .horizontal).rawValue,
        "horizontalCompressionResistance": view.contentCompressionResistancePriority(for: .horizontal).rawValue,
        "verticalHuggingPriority": view.contentHuggingPriority(for: .vertical).rawValue,
        "verticalCompressionResistance": view.contentCompressionResistancePriority(for: .vertical).rawValue,
        "horizontalConstraintCount": horizontalConstraints.count,
        "verticalConstraintCount": verticalConstraints.count,
        "horizontalConstraints": horizontalConstraints.map(constraintSummary),
        "verticalConstraints": verticalConstraints.map(constraintSummary)
    ]
}

private func rectDictionary(_ rect: NSRect) -> [String: CGFloat] {
    [
        "x": rect.origin.x,
        "y": rect.origin.y,
        "width": rect.size.width,
        "height": rect.size.height
    ]
}

private func sizeDictionary(_ size: NSSize) -> [String: CGFloat] {
    [
        "width": size.width,
        "height": size.height
    ]
}

private func constraintSummary(_ constraint: NSLayoutConstraint) -> [String: Any] {
    [
        "firstItem": String(describing: constraint.firstItem.map { type(of: $0) }),
        "firstAttribute": String(describing: constraint.firstAttribute),
        "relation": String(describing: constraint.relation),
        "secondItem": String(describing: constraint.secondItem.map { type(of: $0) }),
        "secondAttribute": String(describing: constraint.secondAttribute),
        "constant": constraint.constant,
        "priority": constraint.priority.rawValue,
        "isActive": constraint.isActive
    ]
}

private final class SettingsSidebarRowView: NSTableRowView {
    private let highlightView = NSView()
    private var trackingAreaRef: NSTrackingArea?
    private var isHovered = false

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override var isSelected: Bool {
        didSet {
            updateAppearance(animated: true)
        }
    }

    override var isEmphasized: Bool {
        get { false }
        set { }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()

        if let trackingAreaRef {
            removeTrackingArea(trackingAreaRef)
        }

        let trackingAreaRef = NSTrackingArea(
            rect: bounds,
            options: [.activeInActiveApp, .inVisibleRect, .mouseEnteredAndExited],
            owner: self,
            userInfo: nil
        )
        self.trackingAreaRef = trackingAreaRef
        addTrackingArea(trackingAreaRef)
    }

    override func mouseEntered(with event: NSEvent) {
        isHovered = true
        updateAppearance(animated: true)
    }

    override func mouseExited(with event: NSEvent) {
        isHovered = false
        updateAppearance(animated: true)
    }

    override func layout() {
        super.layout()
        highlightView.layer?.cornerRadius = 11
        highlightView.layer?.shadowPath = CGPath(
            roundedRect: highlightView.bounds,
            cornerWidth: 11,
            cornerHeight: 11,
            transform: nil
        )
    }

    override func prepareForReuse() {
        super.prepareForReuse()
        isHovered = false
        updateAppearance(animated: false)
    }

    override func drawSelection(in dirtyRect: NSRect) {
    }

    private func setupViews() {
        wantsLayer = true
        setAccessibilityElement(false)

        highlightView.translatesAutoresizingMaskIntoConstraints = false
        highlightView.wantsLayer = true
        addSubview(highlightView)

        NSLayoutConstraint.activate([
            highlightView.topAnchor.constraint(equalTo: topAnchor, constant: 1),
            highlightView.leadingAnchor.constraint(equalTo: leadingAnchor),
            highlightView.trailingAnchor.constraint(equalTo: trailingAnchor),
            highlightView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -1),
        ])

        updateAppearance(animated: false)
    }

    private func updateAppearance(animated: Bool) {
        let fillColor: NSColor
        let borderColor: NSColor

        if isSelected {
            fillColor = MahoAppColors.searchFieldBackground.withAlphaComponent(0.96)
            borderColor = MahoAppColors.cardBorder.withAlphaComponent(0.9)
        } else if isHovered {
            fillColor = MahoAppColors.sidebarHover.withAlphaComponent(0.9)
            borderColor = MahoAppColors.contentBorder.withAlphaComponent(0.32)
        } else {
            fillColor = .clear
            borderColor = .clear
        }

        let targetFill = fillColor.cgColor
        let targetBorder = borderColor.cgColor

        if animated, let layer = highlightView.layer {
            let fillAnimation = CABasicAnimation(keyPath: "backgroundColor")
            fillAnimation.fromValue = layer.backgroundColor
            fillAnimation.toValue = targetFill
            fillAnimation.duration = 0.16
            layer.add(fillAnimation, forKey: "settingsSidebarFill")

            let borderAnimation = CABasicAnimation(keyPath: "borderColor")
            borderAnimation.fromValue = layer.borderColor
            borderAnimation.toValue = targetBorder
            borderAnimation.duration = 0.16
            layer.add(borderAnimation, forKey: "settingsSidebarBorder")
        }

        highlightView.layer?.backgroundColor = targetFill
        highlightView.layer?.borderColor = targetBorder
        highlightView.layer?.borderWidth = isSelected || isHovered ? 1 : 0
        highlightView.layer?.shadowColor = MahoAppColors.canvasInnerGlow.cgColor
        highlightView.layer?.shadowOpacity = isSelected ? 0.32 : 0
        highlightView.layer?.shadowRadius = isSelected ? 18 : 0
        highlightView.layer?.shadowOffset = CGSize(width: 0, height: -6)
    }
}

private final class SettingsSidebarCellView: NSTableCellView {
    private let iconView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func configure(with pane: SettingsPane, selected: Bool) {
        titleLabel.stringValue = pane.title
        titleLabel.textColor = selected ? MahoAppColors.primaryText : MahoAppColors.secondaryText
        titleLabel.font = .systemFont(ofSize: 12.5, weight: selected ? .semibold : .medium)

        iconView.image = MahoIcon.imageForSFSymbol(pane.imageName, pointSize: 14, accessibilityDescription: pane.title)
        iconView.contentTintColor = selected
            ? MahoAppColors.accentBlueBright.withAlphaComponent(0.96)
            : MahoAppColors.secondaryText.withAlphaComponent(0.9)

        setAccessibilityLabel(pane.title)
    }

    private func setupViews() {
        translatesAutoresizingMaskIntoConstraints = false

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.imageScaling = .scaleProportionallyDown
        iconView.setContentHuggingPriority(.required, for: .horizontal)
        addSubview(iconView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 1
        addSubview(titleLabel)

        NSLayoutConstraint.activate([
            iconView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 14),
            iconView.centerYAnchor.constraint(equalTo: centerYAnchor),
            iconView.widthAnchor.constraint(equalToConstant: 17),
            iconView.heightAnchor.constraint(equalToConstant: 17),

            titleLabel.leadingAnchor.constraint(equalTo: iconView.trailingAnchor, constant: 11),
            titleLabel.centerYAnchor.constraint(equalTo: centerYAnchor),
            titleLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),
        ])
    }
}

private final class SettingsFullWidthStackView: NSStackView {
    override func addArrangedSubview(_ view: NSView) {
        view.translatesAutoresizingMaskIntoConstraints = false
        configureSettingsHorizontalExpansion(for: view)
        super.addArrangedSubview(view)
        NSLayoutConstraint.activate(settingsFullWidthConstraints(for: view, in: self))
    }

    override func insertArrangedSubview(_ view: NSView, at index: Int) {
        view.translatesAutoresizingMaskIntoConstraints = false
        configureSettingsHorizontalExpansion(for: view)
        super.insertArrangedSubview(view, at: index)
        NSLayoutConstraint.activate(settingsFullWidthConstraints(for: view, in: self))
    }
}

private func settingsFullWidthConstraints(for view: NSView, in container: NSView) -> [NSLayoutConstraint] {
    let insets: NSEdgeInsets
    if let stackView = container as? NSStackView {
        insets = stackView.edgeInsets
    } else {
        insets = NSEdgeInsets(top: 0, left: 0, bottom: 0, right: 0)
    }

    return [
        view.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: insets.left),
        view.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -insets.right)
    ]
}

func makeSettingsContentStack() -> NSStackView {
    let stack = SettingsFullWidthStackView()
    stack.orientation = .vertical
    stack.alignment = .leading
    stack.distribution = .fill
    stack.spacing = 16
    stack.detachesHiddenViews = true
    stack.translatesAutoresizingMaskIntoConstraints = false
    stack.setContentHuggingPriority(.defaultLow, for: .horizontal)
    stack.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    return stack
}

func makeSettingsRootView(with content: NSView) -> NSView {
    let rootView = NSView()
    rootView.translatesAutoresizingMaskIntoConstraints = false

    content.translatesAutoresizingMaskIntoConstraints = false
    rootView.addSubview(content)

    var constraints = [
        content.topAnchor.constraint(equalTo: rootView.topAnchor),
        content.leadingAnchor.constraint(equalTo: rootView.leadingAnchor),
        content.trailingAnchor.constraint(equalTo: rootView.trailingAnchor),
        content.bottomAnchor.constraint(equalTo: rootView.bottomAnchor)
    ]

    if let stack = content as? NSStackView, !(stack is SettingsFullWidthStackView) {
        for arrangedSubview in stack.arrangedSubviews {
            configureSettingsHorizontalExpansion(for: arrangedSubview)
            constraints.append(contentsOf: settingsFullWidthConstraints(for: arrangedSubview, in: stack))
        }
    }

    NSLayoutConstraint.activate(constraints)

    return rootView
}

enum SettingsPaneSection {
    case plain([NSView], spacing: CGFloat = 16)
    case card([NSView], spacing: CGFloat = 12)
    case managedTable(SettingsManagedTableSectionView)

    fileprivate func makeView() -> NSView {
        switch self {
        case .plain(let views, let spacing):
            let stack = makeSettingsContentStack()
            stack.spacing = spacing
            views.forEach { stack.addArrangedSubview($0) }
            return stack
        case .card(let views, let spacing):
            return makeSettingsSectionCard(views, spacing: spacing)
        case .managedTable(let sectionView):
            return sectionView
        }
    }
}

final class SettingsPaneHostView: NSView {
    private let contentStack = makeSettingsContentStack()

    init(sections: [SettingsPaneSection]) {
        super.init(frame: .zero)
        translatesAutoresizingMaskIntoConstraints = false

        addSubview(contentStack)
        NSLayoutConstraint.activate([
            contentStack.topAnchor.constraint(equalTo: topAnchor),
            contentStack.leadingAnchor.constraint(equalTo: leadingAnchor),
            contentStack.trailingAnchor.constraint(equalTo: trailingAnchor),
            contentStack.bottomAnchor.constraint(equalTo: bottomAnchor)
        ])

        sections.forEach { contentStack.addArrangedSubview($0.makeView()) }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
}

func styleSettingsLabel(_ label: NSTextField, secondary: Bool = false) {
    label.textColor = secondary ? MahoAppColors.secondaryText : MahoAppColors.primaryText
    label.maximumNumberOfLines = 2
    label.lineBreakMode = .byWordWrapping
}

func makeSettingsSectionTitle(_ title: String) -> NSTextField {
    let label = NSTextField(labelWithString: title)
    label.font = .systemFont(ofSize: 13, weight: .semibold)
    styleSettingsLabel(label)
    return label
}

enum SettingsManagedTableContainerStyle {
    case card(titleView: NSView? = nil, footerView: NSView? = nil, spacing: CGFloat = 12)
    case plain(footerView: NSView? = nil, spacing: CGFloat = 16)
    case mixedCard(headerViews: [NSView] = [], footerViews: [NSView] = [], spacing: CGFloat = 12)
    case mixedPlain(headerViews: [NSView] = [], footerViews: [NSView] = [], spacing: CGFloat = 16)
}

enum SettingsManagedTableShellStyle {
    case compact(emptyStateView: NSView? = nil, emptyStateHeight: CGFloat? = nil)
    case fixedHeight(CGFloat)
}

final class SettingsManagedTableSectionView: NSView {
    private let tableView: NSTableView
    private let shellStyle: SettingsManagedTableShellStyle
    private let emptyStateView: NSView?
    private lazy var shellView: NSView = makeShellView()
    private var storedHeightConstraint: NSLayoutConstraint?

    init(
        tableView: NSTableView,
        containerStyle: SettingsManagedTableContainerStyle,
        shellStyle: SettingsManagedTableShellStyle
    ) {
        self.tableView = tableView
        self.shellStyle = shellStyle

        switch shellStyle {
        case .compact(let emptyStateView, _):
            self.emptyStateView = emptyStateView
        case .fixedHeight:
            self.emptyStateView = nil
        }

        super.init(frame: .zero)
        translatesAutoresizingMaskIntoConstraints = false

        let contentView = makeContentView(
            with: shellView,
            containerStyle: containerStyle
        )

        addSubview(contentView)
        NSLayoutConstraint.activate([
            contentView.topAnchor.constraint(equalTo: topAnchor),
            contentView.leadingAnchor.constraint(equalTo: leadingAnchor),
            contentView.trailingAnchor.constraint(equalTo: trailingAnchor),
            contentView.bottomAnchor.constraint(equalTo: bottomAnchor)
        ])
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func updateManagedTableLayout(rowCount: Int, minimumVisibleRows: CGFloat = 1) {
        guard case .compact = shellStyle else { return }

        let rowHeight = tableView.rowHeight + tableView.intercellSpacing.height
        let headerHeight = tableView.headerView?.frame.height ?? 0
        let minimumHeight: CGFloat

        switch shellStyle {
        case .compact(_, let emptyStateHeight):
            minimumHeight = emptyStateHeight ?? (headerHeight + (minimumVisibleRows * tableView.rowHeight) + 16)
        case .fixedHeight:
            minimumHeight = headerHeight + (minimumVisibleRows * tableView.rowHeight) + 16
        }

        let contentHeight = headerHeight + (CGFloat(rowCount) * rowHeight) + 16
        storedHeightConstraint?.constant = max(minimumHeight, contentHeight)

        tableView.isHidden = rowCount == 0
        emptyStateView?.isHidden = rowCount != 0
    }

    func setManagedTableShellAlpha(_ alphaValue: CGFloat) {
        shellView.alphaValue = alphaValue
    }

    private func makeShellView() -> NSView {
        switch shellStyle {
        case .compact(let emptyStateView, _):
            var heightConstraint: NSLayoutConstraint?
            let shellView = makeSettingsCompactTableShell(
                tableView: tableView,
                emptyStateView: emptyStateView,
                storedHeightConstraint: &heightConstraint
            )
            storedHeightConstraint = heightConstraint
            return shellView

        case .fixedHeight(let height):
            styleSettingsTable(tableView)

            let scrollView = NSScrollView()
            scrollView.translatesAutoresizingMaskIntoConstraints = false
            scrollView.hasVerticalScroller = true
            scrollView.hasHorizontalScroller = false
            scrollView.autohidesScrollers = true
            styleSettingsScrollView(scrollView)

            tableView.translatesAutoresizingMaskIntoConstraints = false
            configureSettingsHorizontalExpansion(for: tableView)
            scrollView.documentView = tableView
            scrollView.heightAnchor.constraint(equalToConstant: height).isActive = true
            return scrollView
        }
    }

    private func makeContentView(with shellView: NSView, containerStyle: SettingsManagedTableContainerStyle) -> NSView {
        switch containerStyle {
        case .card(let titleView, let footerView, let spacing):
            var contentViews: [NSView] = []
            if let titleView {
                contentViews.append(titleView)
            }
            contentViews.append(shellView)
            if let footerView {
                contentViews.append(footerView)
            }
            return makeSettingsSectionCard(contentViews, spacing: spacing)

        case .plain(let footerView, let spacing):
            let stack = makeSettingsContentStack()
            stack.spacing = spacing
            stack.addArrangedSubview(shellView)
            if let footerView {
                stack.addArrangedSubview(footerView)
            }
            return stack

        case .mixedCard(let headerViews, let footerViews, let spacing):
            return makeSettingsSectionCard(headerViews + [shellView] + footerViews, spacing: spacing)

        case .mixedPlain(let headerViews, let footerViews, let spacing):
            let stack = makeSettingsContentStack()
            stack.spacing = spacing
            for view in headerViews + [shellView] + footerViews {
                stack.addArrangedSubview(view)
            }
            return stack
        }
    }
}

func makeSettingsCompactEmptyStateLabel(_ text: String) -> NSTextField {
    let label = NSTextField(labelWithString: text)
    label.font = .systemFont(ofSize: 12)
    label.alignment = .center
    styleSettingsLabel(label, secondary: true)
    label.isHidden = true
    return label
}

func styleSettingsEditableTextField(_ textField: NSTextField) {
    textField.isBordered = false
    textField.focusRingType = .none
    textField.drawsBackground = true
    textField.backgroundColor = MahoAppColors.searchFieldBackground.withAlphaComponent(0.88)
    textField.textColor = MahoAppColors.primaryText
    textField.font = .systemFont(ofSize: 13, weight: .regular)
    textField.wantsLayer = true
    textField.layer?.cornerRadius = 10
    textField.layer?.masksToBounds = true
    textField.layer?.borderWidth = 1
    textField.layer?.borderColor = MahoAppColors.searchFieldBorder.withAlphaComponent(0.76).cgColor
    if let placeholder = textField.placeholderString {
        textField.placeholderAttributedString = NSAttributedString(
            string: placeholder,
            attributes: [.foregroundColor: MahoAppColors.secondaryText]
        )
    }
}

private enum SettingsLayoutMetrics {
    static let paneTopInset: CGFloat = 26
    static let paneLeadingInset: CGFloat = 24
    static let paneTrailingInset: CGFloat = 24
    static let paneBottomInset: CGFloat = 28

    static let rowLabelWidth: CGFloat = 150
    static let rowControlMinimumWidth: CGFloat = 250
    static let rowSpacing: CGFloat = 14
    static let rowMinimumHeight: CGFloat = 40
    static let rowVerticalInset: CGFloat = 6
    static let rowHorizontalInset: CGFloat = 16
    static let controlContainerVerticalInset: CGFloat = 6
    static let controlContainerHorizontalInset: CGFloat = 10
}

private final class SettingsContentContainerView: NSView {
    private var activeContentConstraints: [NSLayoutConstraint] = []
    private let scrollView = NSScrollView()
    private let documentContainer = NSView()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        configureScrollView()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configureScrollView()
    }

    private func configureScrollView() {
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.drawsBackground = false
        scrollView.borderType = .noBorder
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true
        scrollView.scrollerInsets = NSEdgeInsets(top: 0, left: 0, bottom: 0, right: 20)

        documentContainer.translatesAutoresizingMaskIntoConstraints = false
        scrollView.documentView = documentContainer

        addSubview(scrollView)
        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: topAnchor),
            scrollView.leadingAnchor.constraint(equalTo: leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: bottomAnchor),

            documentContainer.leadingAnchor.constraint(equalTo: scrollView.contentView.leadingAnchor),
            documentContainer.trailingAnchor.constraint(equalTo: scrollView.contentView.trailingAnchor),
            documentContainer.topAnchor.constraint(equalTo: scrollView.contentView.topAnchor),
            documentContainer.bottomAnchor.constraint(equalTo: scrollView.contentView.bottomAnchor),
            documentContainer.widthAnchor.constraint(equalTo: scrollView.contentView.widthAnchor),
        ])
    }

    func setContentView(_ contentView: NSView) {
        NSLayoutConstraint.deactivate(activeContentConstraints)
        activeContentConstraints.removeAll()

        documentContainer.subviews.forEach { $0.removeFromSuperview() }

        contentView.translatesAutoresizingMaskIntoConstraints = false
        documentContainer.addSubview(contentView)

        let widthToAvailableSpace = contentView.widthAnchor.constraint(
            equalTo: documentContainer.widthAnchor,
            constant: -(SettingsLayoutMetrics.paneLeadingInset + SettingsLayoutMetrics.paneTrailingInset)
        )
        widthToAvailableSpace.priority = .required

        activeContentConstraints = [
            contentView.topAnchor.constraint(equalTo: documentContainer.topAnchor, constant: SettingsLayoutMetrics.paneTopInset),
            contentView.leadingAnchor.constraint(equalTo: documentContainer.leadingAnchor, constant: SettingsLayoutMetrics.paneLeadingInset),
            contentView.trailingAnchor.constraint(equalTo: documentContainer.trailingAnchor, constant: -SettingsLayoutMetrics.paneTrailingInset),
            contentView.bottomAnchor.constraint(lessThanOrEqualTo: documentContainer.bottomAnchor, constant: -SettingsLayoutMetrics.paneBottomInset),
            documentContainer.heightAnchor.constraint(greaterThanOrEqualTo: contentView.heightAnchor, constant: SettingsLayoutMetrics.paneTopInset + SettingsLayoutMetrics.paneBottomInset),
            widthToAvailableSpace,
        ]

        NSLayoutConstraint.activate(activeContentConstraints)
    }
}

private func settingsControlExpandsHorizontally(_ control: NSView) -> Bool {
    switch control {
    case is NSPopUpButton,
         is NSSegmentedControl,
         is NSTextField,
         is NSSecureTextField,
         is NSSearchField,
         is NSSlider,
         is NSPathControl,
         is NSScrollView,
         is NSStackView:
        return true
    default:
        return false
    }
}

private func configureSettingsHorizontalExpansion(for view: NSView) {
    view.setContentHuggingPriority(.defaultLow, for: .horizontal)
    view.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
}

func makeSettingsLabeledRow(_ label: String, _ control: NSView, embedded: Bool = false) -> NSStackView {
    let isToggleControl = control is NSSwitch
    let labelText = isToggleControl ? (label.hasSuffix(":") ? String(label.dropLast()) : label) : label
    let textField = NSTextField(labelWithString: labelText)

    textField.alignment = .left
    textField.setContentHuggingPriority(.required, for: .horizontal)
    textField.setContentCompressionResistancePriority(.required, for: .horizontal)
    textField.font = .systemFont(ofSize: 12, weight: .medium)
    styleSettingsLabel(textField, secondary: true)
    textField.widthAnchor.constraint(equalToConstant: SettingsLayoutMetrics.rowLabelWidth).isActive = true

    let expandsHorizontally = settingsControlExpandsHorizontally(control)

    control.translatesAutoresizingMaskIntoConstraints = false
    let controlContainer = NSView()
    controlContainer.translatesAutoresizingMaskIntoConstraints = false

    if expandsHorizontally {
        controlContainer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        controlContainer.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        controlContainer.wantsLayer = true
        controlContainer.layer?.cornerRadius = 11
        controlContainer.layer?.backgroundColor = MahoAppColors.searchFieldBackground.withAlphaComponent(0.84).cgColor
        controlContainer.layer?.borderWidth = 1
        controlContainer.layer?.borderColor = MahoAppColors.searchFieldBorder.withAlphaComponent(0.72).cgColor

        control.setContentHuggingPriority(.defaultLow, for: .horizontal)
        control.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let controlWidth = control.widthAnchor.constraint(greaterThanOrEqualToConstant: SettingsLayoutMetrics.rowControlMinimumWidth)
        controlWidth.priority = .required
        controlWidth.isActive = true

        controlContainer.addSubview(control)
        NSLayoutConstraint.activate([
            control.topAnchor.constraint(equalTo: controlContainer.topAnchor, constant: SettingsLayoutMetrics.controlContainerVerticalInset),
            control.leadingAnchor.constraint(equalTo: controlContainer.leadingAnchor, constant: SettingsLayoutMetrics.controlContainerHorizontalInset),
            control.trailingAnchor.constraint(equalTo: controlContainer.trailingAnchor, constant: -SettingsLayoutMetrics.controlContainerHorizontalInset),
            control.bottomAnchor.constraint(equalTo: controlContainer.bottomAnchor, constant: -SettingsLayoutMetrics.controlContainerVerticalInset)
        ])
    } else {
        controlContainer.setContentHuggingPriority(.required, for: .horizontal)
        controlContainer.setContentCompressionResistancePriority(.required, for: .horizontal)
        control.setContentHuggingPriority(.required, for: .horizontal)
        control.setContentCompressionResistancePriority(.required, for: .horizontal)

        controlContainer.addSubview(control)
        NSLayoutConstraint.activate([
            control.topAnchor.constraint(equalTo: controlContainer.topAnchor),
            control.leadingAnchor.constraint(equalTo: controlContainer.leadingAnchor),
            control.trailingAnchor.constraint(lessThanOrEqualTo: controlContainer.trailingAnchor),
            control.bottomAnchor.constraint(equalTo: controlContainer.bottomAnchor)
        ])
    }

    let row: NSStackView
    if expandsHorizontally {
        row = NSStackView(views: [textField, controlContainer])
    } else {
        let spacer = NSView()
        spacer.translatesAutoresizingMaskIntoConstraints = false
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        spacer.setContentCompressionResistancePriority(.init(1), for: .horizontal)
        row = NSStackView(views: [textField, controlContainer, spacer])
    }

    row.orientation = .horizontal
    row.alignment = .centerY
    row.distribution = .fill
    row.detachesHiddenViews = true
    row.spacing = SettingsLayoutMetrics.rowSpacing

    if embedded {
        row.edgeInsets = NSEdgeInsets(
            top: SettingsLayoutMetrics.rowVerticalInset,
            left: 0,
            bottom: SettingsLayoutMetrics.rowVerticalInset,
            right: 0
        )
    } else {
        row.edgeInsets = NSEdgeInsets(
            top: SettingsLayoutMetrics.rowVerticalInset,
            left: SettingsLayoutMetrics.rowHorizontalInset,
            bottom: SettingsLayoutMetrics.rowVerticalInset,
            right: SettingsLayoutMetrics.rowHorizontalInset
        )
    }

    configureSettingsHorizontalExpansion(for: row)
    row.heightAnchor.constraint(greaterThanOrEqualToConstant: SettingsLayoutMetrics.rowMinimumHeight).isActive = true

    if !embedded {
        row.wantsLayer = true
        row.layer?.cornerRadius = 14
        row.layer?.backgroundColor = MahoAppColors.cardBackground.withAlphaComponent(0.32).cgColor
        row.layer?.borderWidth = 1
        row.layer?.borderColor = MahoAppColors.contentBorder.withAlphaComponent(0.34).cgColor
        row.layer?.shadowColor = MahoAppColors.canvasInnerGlow.cgColor
        row.layer?.shadowOpacity = 0.08
        row.layer?.shadowRadius = 12
        row.layer?.shadowOffset = CGSize(width: 0, height: -2)
    }

    return row
}

func makeSettingsSectionCard(_ contentViews: [NSView], spacing: CGFloat = 12) -> NSView {
    let card = SettingsFullWidthStackView()
    card.translatesAutoresizingMaskIntoConstraints = false
    card.orientation = .vertical
    card.alignment = .leading
    card.distribution = .fill
    card.detachesHiddenViews = true
    configureSettingsHorizontalExpansion(for: card)
    card.wantsLayer = true
    card.layer?.cornerRadius = 16
    card.layer?.backgroundColor = MahoAppColors.cardBackground.withAlphaComponent(0.34).cgColor
    card.layer?.borderWidth = 1
    card.layer?.borderColor = MahoAppColors.cardBorder.withAlphaComponent(0.8).cgColor
    card.layer?.shadowColor = MahoAppColors.canvasInnerGlow.cgColor
    card.layer?.shadowOpacity = 0.08
    card.layer?.shadowRadius = 12
    card.layer?.shadowOffset = CGSize(width: 0, height: -2)

    let contentStack = makeSettingsContentStack()
    contentStack.spacing = spacing
    contentStack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)

    for contentView in contentViews {
        contentStack.addArrangedSubview(contentView)
    }

    card.addArrangedSubview(contentStack)
    return card
}

func makeSettingsPlainRow(_ views: [NSView], spacing: CGFloat = 12) -> NSStackView {
    let row = NSStackView(views: views)
    row.translatesAutoresizingMaskIntoConstraints = false
    row.orientation = .horizontal
    row.alignment = .centerY
    row.distribution = .fill
    row.detachesHiddenViews = true
    row.spacing = spacing
    row.setContentHuggingPriority(.defaultLow, for: .horizontal)
    row.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    return row
}

func makeSettingsButtonCluster(_ buttons: [NSView], spacing: CGFloat = 8) -> NSStackView {
    let cluster = NSStackView(views: buttons)
    cluster.translatesAutoresizingMaskIntoConstraints = false
    cluster.orientation = .horizontal
    cluster.alignment = .centerY
    cluster.distribution = .fill
    cluster.detachesHiddenViews = true
    cluster.spacing = spacing
    cluster.setContentHuggingPriority(.required, for: .horizontal)
    cluster.setContentCompressionResistancePriority(.required, for: .horizontal)
    return cluster
}

func makeSettingsActionRow(_ buttons: [NSView], spacing: CGFloat = 8) -> NSStackView {
    let spacer = NSView()
    spacer.translatesAutoresizingMaskIntoConstraints = false
    spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
    spacer.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

    return makeSettingsPlainRow([spacer, makeSettingsButtonCluster(buttons, spacing: spacing)], spacing: spacing)
}

func makeSettingsSectionSeparator() -> NSView {
    let separator = NSView()
    separator.translatesAutoresizingMaskIntoConstraints = false
    separator.wantsLayer = true
    separator.layer?.backgroundColor = MahoAppColors.contentBorder.withAlphaComponent(0.3).cgColor
    separator.heightAnchor.constraint(equalToConstant: 1).isActive = true
    return separator
}

func makeSettingsCompactTableShell(
    tableView: NSTableView,
    emptyStateView: NSView? = nil
) -> NSView {
    var storedHeightConstraint: NSLayoutConstraint?
    return makeSettingsCompactTableShell(
        tableView: tableView,
        emptyStateView: emptyStateView,
        storedHeightConstraint: &storedHeightConstraint
    )
}

func makeSettingsCompactTableShell(
    tableView: NSTableView,
    emptyStateView: NSView? = nil,
    storedHeightConstraint: inout NSLayoutConstraint?
) -> NSView {
    styleSettingsTable(tableView)

    let shell = NSView()
    shell.translatesAutoresizingMaskIntoConstraints = false
    configureSettingsHorizontalExpansion(for: shell)
    shell.wantsLayer = true
    shell.layer?.cornerRadius = 14
    shell.layer?.backgroundColor = MahoAppColors.cardBackground.withAlphaComponent(0.74).cgColor
    shell.layer?.borderWidth = 1
    shell.layer?.borderColor = MahoAppColors.cardBorder.withAlphaComponent(0.78).cgColor
    shell.layer?.shadowColor = MahoAppColors.canvasInnerGlow.cgColor
    shell.layer?.shadowOpacity = 0.08
    shell.layer?.shadowRadius = 12
    shell.layer?.shadowOffset = CGSize(width: 0, height: -2)

    let scrollView = NSScrollView()
    scrollView.translatesAutoresizingMaskIntoConstraints = false
    configureSettingsHorizontalExpansion(for: scrollView)
    scrollView.drawsBackground = false
    scrollView.borderType = .noBorder
    scrollView.hasVerticalScroller = true
    scrollView.hasHorizontalScroller = false
    scrollView.autohidesScrollers = true

    tableView.translatesAutoresizingMaskIntoConstraints = false
    configureSettingsHorizontalExpansion(for: tableView)
    scrollView.documentView = tableView

    shell.addSubview(scrollView)
    NSLayoutConstraint.activate([
        scrollView.topAnchor.constraint(equalTo: shell.topAnchor),
        scrollView.leadingAnchor.constraint(equalTo: shell.leadingAnchor),
        scrollView.trailingAnchor.constraint(equalTo: shell.trailingAnchor),
        scrollView.bottomAnchor.constraint(equalTo: shell.bottomAnchor),
    ])

    let heightConstraint = scrollView.heightAnchor.constraint(equalToConstant: 1)
    heightConstraint.priority = .defaultHigh
    heightConstraint.isActive = true
    storedHeightConstraint = heightConstraint

    if let emptyStateView {
        emptyStateView.translatesAutoresizingMaskIntoConstraints = false
        shell.addSubview(emptyStateView)
        NSLayoutConstraint.activate([
            emptyStateView.centerYAnchor.constraint(equalTo: shell.centerYAnchor),
            emptyStateView.leadingAnchor.constraint(equalTo: shell.leadingAnchor, constant: 16),
            emptyStateView.trailingAnchor.constraint(equalTo: shell.trailingAnchor, constant: -16),
            emptyStateView.topAnchor.constraint(greaterThanOrEqualTo: shell.topAnchor, constant: 16),
            emptyStateView.bottomAnchor.constraint(lessThanOrEqualTo: shell.bottomAnchor, constant: -16),
        ])
    }

    return shell
}

func makeSettingsCenteredTableActionCell(_ contentView: NSView) -> NSTableCellView {
    let cell = NSTableCellView()
    let container = NSView()
    container.translatesAutoresizingMaskIntoConstraints = false
    contentView.translatesAutoresizingMaskIntoConstraints = false

    cell.addSubview(container)
    container.addSubview(contentView)

    NSLayoutConstraint.activate([
        container.topAnchor.constraint(equalTo: cell.topAnchor),
        container.leadingAnchor.constraint(equalTo: cell.leadingAnchor),
        container.trailingAnchor.constraint(equalTo: cell.trailingAnchor),
        container.bottomAnchor.constraint(equalTo: cell.bottomAnchor),

        contentView.centerXAnchor.constraint(equalTo: container.centerXAnchor),
        contentView.centerYAnchor.constraint(equalTo: container.centerYAnchor),
    ])

    return cell
}

func styleSettingsScrollView(_ scrollView: NSScrollView) {
    scrollView.drawsBackground = false
    scrollView.borderType = .noBorder
    scrollView.wantsLayer = true
    scrollView.layer?.backgroundColor = MahoAppColors.cardBackground.withAlphaComponent(0.74).cgColor
    scrollView.layer?.cornerRadius = 14
    scrollView.layer?.borderWidth = 1
    scrollView.layer?.borderColor = MahoAppColors.cardBorder.withAlphaComponent(0.78).cgColor
}

func styleSettingsTable(_ tableView: NSTableView) {
    tableView.backgroundColor = .clear
    tableView.usesAlternatingRowBackgroundColors = false
    tableView.gridStyleMask = []
    tableView.focusRingType = .none
    tableView.rowHeight = 34
    tableView.intercellSpacing = NSSize(width: 0, height: 2)
    tableView.rowSizeStyle = .custom

    if let headerView = tableView.headerView {
        headerView.wantsLayer = true
        headerView.layer?.backgroundColor = MahoAppColors.sidebarBackground.withAlphaComponent(0.64).cgColor
        for column in tableView.tableColumns {
            column.headerCell.attributedStringValue = NSAttributedString(
                string: column.title.uppercased(),
                attributes: [
                    .foregroundColor: MahoAppColors.secondaryText.withAlphaComponent(0.86),
                    .font: NSFont.systemFont(ofSize: 10.5, weight: .semibold)
                ]
            )
        }
    }
}

func makeSettingsTableCell(text: String, secondary: Bool = false) -> NSTableCellView {
    let cell = NSTableCellView()
    let textField = NSTextField(labelWithString: text)
    textField.translatesAutoresizingMaskIntoConstraints = false
    textField.font = .systemFont(ofSize: 12.5, weight: secondary ? .regular : .medium)
    styleSettingsLabel(textField, secondary: secondary)
    cell.textField = textField
    cell.addSubview(textField)

    NSLayoutConstraint.activate([
        textField.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 12),
        textField.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -12),
        textField.centerYAnchor.constraint(equalTo: cell.centerYAnchor)
    ])

    return cell
}
