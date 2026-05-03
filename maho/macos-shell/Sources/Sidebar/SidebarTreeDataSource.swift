import AppKit

private final class NewTabRowView: NSView {
    var onActivate: (() -> Void)?

    override func accessibilityPerformPress() -> Bool {
        onActivate?()
        return true
    }
}

// MARK: - NSOutlineViewDataSource + NSOutlineViewDelegate

class SidebarTreeDataSource: NSObject, NSOutlineViewDataSource, NSOutlineViewDelegate {
    var snapshot: SidebarSnapshot = SidebarSnapshot(rootSections: [])
    weak var controller: SidebarController? {
        didSet {
            if let outlineView = controller?.outlineView {
                setupContextMenu(for: outlineView)
                outlineView.target = self
                outlineView.doubleAction = #selector(handleDoubleClick(_:))
            }
        }
    }
    var dragController: SidebarDragController?

    // MARK: - NSOutlineViewDataSource

    func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int {
        if item == nil {
            return snapshot.rootSections.count
        }
        if let section = item as? SectionNode {
            return section.children.count
        }
        return 0
    }

    func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any {
        if item == nil {
            return snapshot.rootSections[index]
        }
        if let section = item as? SectionNode {
            return section.children[index]
        }
        fatalError("Unexpected item type in outlineView child(index:ofItem:)")
    }

    func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool {
        if let section = item as? SectionNode {
            if section.sectionId == .pinned || section.sectionId == .regular {
                return true
            }
            if section.sectionId == .separator || section.sectionId == .newTab {
                return false
            }
            return !section.children.isEmpty
        }
        return false
    }

    // MARK: - NSOutlineViewDelegate

    func outlineView(_ outlineView: NSOutlineView, viewFor tableColumn: NSTableColumn?, item: Any) -> NSView? {
        if let section = item as? SectionNode {
            if section.sectionId == .separator {
                let container = NSView(frame: .zero)
                container.wantsLayer = true
                let line = NSView()
                line.wantsLayer = true
                line.layer?.backgroundColor = NSColor.white.withAlphaComponent(0.15).cgColor
                line.translatesAutoresizingMaskIntoConstraints = false
                container.addSubview(line)
                NSLayoutConstraint.activate([
                    line.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: SidebarMetrics.sidebarHorizontalInset),
                    line.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -SidebarMetrics.sidebarHorizontalInset),
                    line.centerYAnchor.constraint(equalTo: container.centerYAnchor),
                    line.heightAnchor.constraint(equalToConstant: 1),
                ])
                return container
            }

            if section.sectionId == .newTab {
                let container = NewTabRowView(frame: .zero)
                container.wantsLayer = true
                container.setAccessibilityRole(.button)
                container.setAccessibilityLabel("New Tab")
                container.setAccessibilityIdentifier("sidebar.newTab")
                container.setAccessibilityElement(true)
                container.onActivate = { [weak self] in
                    self?.controller?.newTab()
                }

                let iconView = NSImageView()
                iconView.translatesAutoresizingMaskIntoConstraints = false
                let config = NSImage.SymbolConfiguration(pointSize: 12, weight: .medium)
                iconView.image = NSImage(systemSymbolName: "plus", accessibilityDescription: nil)?.withSymbolConfiguration(config)
                iconView.contentTintColor = MahoAppColors.primaryText.withAlphaComponent(0.60)
                container.addSubview(iconView)

                let label = NSTextField(labelWithString: "New Tab")
                label.translatesAutoresizingMaskIntoConstraints = false
                label.font = .systemFont(ofSize: 12, weight: .regular)
                label.textColor = MahoAppColors.primaryText.withAlphaComponent(0.60)
                container.addSubview(label)

                NSLayoutConstraint.activate([
                    iconView.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: SidebarMetrics.sidebarHorizontalInset),
                    iconView.centerYAnchor.constraint(equalTo: container.centerYAnchor),
                    iconView.widthAnchor.constraint(equalToConstant: 16),
                    iconView.heightAnchor.constraint(equalToConstant: 16),

                    label.leadingAnchor.constraint(equalTo: iconView.trailingAnchor, constant: 6),
                    label.centerYAnchor.constraint(equalTo: container.centerYAnchor),
                    label.trailingAnchor.constraint(lessThanOrEqualTo: container.trailingAnchor, constant: -SidebarMetrics.sidebarHorizontalInset),
                ])

                return container
            }

            let cellId = NSUserInterfaceItemIdentifier("SectionCell")
            let cell = outlineView.makeView(withIdentifier: cellId, owner: nil) as? SidebarSectionCellView
                ?? SidebarSectionCellView(identifier: cellId)
            cell.configure(with: section)
            cell.onPrimaryClick = { [weak self] in
                guard section.isFolder else { return }
                self?.controller?.toggleFolder(section)
            }
            return cell
        }

        if let tab = item as? TabNode {
            let cellId = NSUserInterfaceItemIdentifier("TabCell")
            let cell = outlineView.makeView(withIdentifier: cellId, owner: nil) as? SidebarTabCellView
                ?? SidebarTabCellView(identifier: cellId)
            cell.configure(with: tab)
            if let localTitle = controller?.localCustomTitle(for: tab.tabId) {
                cell.applyLocalCustomTitle(localTitle)
            }
            cell.onClose = { [weak self] in
                self?.controller?.closeTab(tab.tabId)
            }
            cell.onTitleChanged = { [weak self] tabId, newTitle in
                self?.controller?.handleTabTitleChanged(tabId: tabId, newTitle: newTitle)
            }
            return cell
        }

        if let liveItem = item as? LiveFolderItemNode {
            let cellId = NSUserInterfaceItemIdentifier("LiveFolderItemCell")
            let cell = outlineView.makeView(withIdentifier: cellId, owner: nil) as? SidebarTabCellView
                ?? SidebarTabCellView(identifier: cellId)
            cell.configureForLiveItem(liveItem.item)
            cell.onClose = nil
            return cell
        }

        return nil
    }

    func outlineView(_ outlineView: NSOutlineView, heightOfRowByItem item: Any) -> CGFloat {
        if let section = item as? SectionNode {
            if section.sectionId == .separator { return 8 }
            if section.sectionId == .newTab { return 30 }
            if section.isFolder { return 30 }
            return CGFloat.leastNonzeroMagnitude
        }
        return 30
    }

    func outlineView(_ outlineView: NSOutlineView, shouldSelectItem item: Any) -> Bool {
        if let section = item as? SectionNode {
            return section.sectionId != .separator
        }
        return item is TabNode || item is LiveFolderItemNode
    }

    func outlineViewSelectionDidChange(_ notification: Notification) {
        guard let outlineView = notification.object as? NSOutlineView else { return }
        let row = outlineView.selectedRow
        guard row >= 0 else { return }

        if let tab = outlineView.item(atRow: row) as? TabNode {
            controller?.activateTab(tab.tabId)
        } else if let liveItem = outlineView.item(atRow: row) as? LiveFolderItemNode {
            if let url = URL(string: liveItem.item.url) {
                NSWorkspace.shared.open(url)
            }
            outlineView.deselectRow(row)
        }
    }

    func outlineView(_ outlineView: NSOutlineView, shouldShowOutlineCellForItem item: Any) -> Bool {
        return false
    }

    func outlineView(_ outlineView: NSOutlineView, shouldCollapseItem item: Any) -> Bool {
        if let section = item as? SectionNode, !section.isFolder {
            return false
        }
        return true
    }

    func outlineView(_ outlineView: NSOutlineView, isGroupItem item: Any) -> Bool {
        return false
    }

    // Custom row drawing for selection highlight
    func outlineView(_ outlineView: NSOutlineView, rowViewForItem item: Any) -> NSTableRowView? {
        let rowId = NSUserInterfaceItemIdentifier("SidebarRow")
        if let existing = outlineView.makeView(withIdentifier: rowId, owner: nil) as? SidebarRowView {
            return existing
        }
        let rowView = SidebarRowView()
        rowView.identifier = rowId
        return rowView
    }

    func outlineView(_ outlineView: NSOutlineView, pasteboardWriterForItem item: Any) -> NSPasteboardWriting? {
        return dragController?.pasteboardWriterForItem(item)
    }

    func outlineView(_ outlineView: NSOutlineView, validateDrop info: NSDraggingInfo, proposedItem item: Any?, proposedChildIndex index: Int) -> NSDragOperation {
        return dragController?.validateDrop(outlineView: outlineView, info: info, item: item, childIndex: index) ?? []
    }

    func outlineView(_ outlineView: NSOutlineView, acceptDrop info: NSDraggingInfo, item: Any?, childIndex index: Int) -> Bool {
        return dragController?.acceptDrop(outlineView: outlineView, info: info, item: item, childIndex: index) ?? false
    }

    func outlineView(_ outlineView: NSOutlineView, draggingSession session: NSDraggingSession, willBeginAt screenPoint: NSPoint, forItems draggedItems: [Any]) {
        dragController?.draggingSessionWillBegin()
    }

    func outlineView(_ outlineView: NSOutlineView, draggingSession session: NSDraggingSession, endedAt screenPoint: NSPoint, operation: NSDragOperation) {
        dragController?.draggingSessionEnded()
    }
    @objc func handleDoubleClick(_ sender: NSOutlineView) {
        guard controller?.isDragging != true else { return }
        let clickedRow = sender.clickedRow
        guard clickedRow >= 0 else { return }
        guard let item = sender.item(atRow: clickedRow) else { return }

        // Only allow title editing for tab cells
        guard item is TabNode else { return }
        guard let cellView = sender.view(atColumn: 0, row: clickedRow, makeIfNecessary: false) as? SidebarTabCellView else { return }

        cellView.beginTitleEditing()
    }
}

// MARK: - Context Menu

extension SidebarTreeDataSource: NSMenuDelegate {
    func setupContextMenu(for outlineView: NSOutlineView) {
        let menu = NSMenu()
        menu.delegate = self
        outlineView.menu = menu
    }

    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        guard let outlineView = controller?.outlineView else { return }
        let clickedRow = outlineView.clickedRow

        if clickedRow < 0 {
            // Background right-click — show space-level context menu
            if let backgroundMenu = controller?.backgroundContextMenu() {
                while let menuItem = backgroundMenu.items.first {
                    backgroundMenu.removeItem(menuItem)
                    menu.addItem(menuItem)
                }
            }
            return
        }

        guard let item = outlineView.item(atRow: clickedRow) else { return }
        guard let contextMenu = controller?.contextMenu(forSidebarItem: item) else { return }

        while let menuItem = contextMenu.items.first {
            contextMenu.removeItem(menuItem)
            menu.addItem(menuItem)
        }
    }
}

// MARK: - Custom Row View

class SidebarRowView: NSTableRowView {
    private var trackingArea: NSTrackingArea?
    private var isHovered = false
    private let hoverLayer = CALayer()
    private var hoverLayerAdded = false
    var splitHighlightSide: SplitSide? = nil {
        didSet {
            if splitHighlightSide != oldValue {
                needsDisplay = true
                updateBackground()
            }
        }
    }
    var customDropTargetHighlight = false {
        didSet {
            guard customDropTargetHighlight != oldValue else { return }
            needsDisplay = true
            updateBackground()
        }
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        hoverLayer.cornerRadius = 6
        setAccessibilityElement(false)
        updateBackground()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        wantsLayer = true
        hoverLayer.cornerRadius = 6
        setAccessibilityElement(false)
        updateBackground()
    }

    override var isSelected: Bool {
        didSet {
            updateBackground()
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea {
            removeTrackingArea(trackingArea)
        }
        let trackingArea = NSTrackingArea(
            rect: bounds,
            options: [.activeInActiveApp, .inVisibleRect, .mouseEnteredAndExited],
            owner: self,
            userInfo: nil
        )
        self.trackingArea = trackingArea
        addTrackingArea(trackingArea)
    }

    override func mouseEntered(with event: NSEvent) {
        isHovered = true
        updateBackground()
    }

    override func mouseExited(with event: NSEvent) {
        isHovered = false
        updateBackground()
    }

    override func prepareForReuse() {
        super.prepareForReuse()
        isHovered = false
        splitHighlightSide = nil
        customDropTargetHighlight = false
        updateBackground()
    }

    override func layout() {
        super.layout()
        if !hoverLayerAdded, let layer = layer {
            layer.insertSublayer(hoverLayer, at: 0)
            hoverLayerAdded = true
        }
        hoverLayer.frame = bounds.insetBy(dx: 4, dy: 0)
        updateBackground()
    }

    override var isTargetForDropOperation: Bool {
        didSet {
            updateBackground()
        }
    }

    private func updateBackground() {
        let newColor: CGColor
        if splitHighlightSide != nil {
            newColor = NSColor.clear.cgColor
        } else if customDropTargetHighlight || isTargetForDropOperation {
            newColor = MahoAppColors.dropIndicator.withAlphaComponent(0.18).cgColor
        } else if isSelected {
            newColor = MahoAppColors.sidebarSelected.cgColor
        } else if isHovered {
            newColor = MahoAppColors.sidebarHover.cgColor
        } else {
            newColor = NSColor.clear.cgColor
        }

        let anim = CABasicAnimation(keyPath: "backgroundColor")
        anim.fromValue = hoverLayer.backgroundColor
        anim.toValue = newColor
        anim.duration = 0.15
        hoverLayer.add(anim, forKey: "backgroundColorAnim")
        hoverLayer.backgroundColor = newColor
    }

    override func drawSelection(in dirtyRect: NSRect) {
        // Selection is handled by hoverLayer in updateBackground()
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        if customDropTargetHighlight && splitHighlightSide == nil {
            let targetRect = bounds.insetBy(dx: 4, dy: 1)
            let targetPath = NSBezierPath(roundedRect: targetRect, xRadius: 6, yRadius: 6)
            MahoAppColors.dropIndicator.withAlphaComponent(0.12).setFill()
            targetPath.fill()
            MahoAppColors.dropIndicator.withAlphaComponent(0.52).setStroke()
            targetPath.lineWidth = 1.5
            targetPath.stroke()
        }

        if let side = splitHighlightSide {
            let halfRect: NSRect
            switch side {
            case .left:
                halfRect = NSRect(
                    x: bounds.minX + 4,
                    y: bounds.minY + 1,
                    width: max(0, bounds.width / 2 - 4),
                    height: max(0, bounds.height - 2)
                )
            case .right:
                halfRect = NSRect(
                    x: bounds.midX,
                    y: bounds.minY + 1,
                    width: max(0, bounds.width / 2 - 4),
                    height: max(0, bounds.height - 2)
                )
            default:
                halfRect = bounds.insetBy(dx: 4, dy: 1)
            }

            NSColor.controlAccentColor.withAlphaComponent(0.25).setFill()
            let path = NSBezierPath(roundedRect: halfRect, xRadius: 4, yRadius: 4)
            path.fill()

            NSColor.controlAccentColor.withAlphaComponent(0.5).setStroke()
            path.lineWidth = 1.5
            path.stroke()
        }
    }

    override func drawDraggingDestinationFeedback(in dirtyRect: NSRect) {
        guard splitHighlightSide == nil else {
            return
        }

        MahoAppColors.dropIndicator.withAlphaComponent(0.18).setFill()
        let path = NSBezierPath(roundedRect: bounds.insetBy(dx: 4, dy: 1), xRadius: 6, yRadius: 6)
        path.fill()

        MahoAppColors.dropIndicator.withAlphaComponent(0.56).setStroke()
        let strokePath = NSBezierPath(roundedRect: bounds.insetBy(dx: 4, dy: 1), xRadius: 6, yRadius: 6)
        strokePath.lineWidth = 1.5
        strokePath.stroke()
    }

    override var isEmphasized: Bool {
        get { return true }
        set { }
    }
}
