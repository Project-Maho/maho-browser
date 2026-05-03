import AppKit

// MARK: - Custom NSOutlineView

class SidebarOutlineView: NSOutlineView {
    private enum InsertionIndicatorMetrics {
        static let height: CGFloat = 2
        static let horizontalPadding: CGFloat = 10
        static let cornerRadius: CGFloat = 1
        static let fadeDuration: CFTimeInterval = 0.16
    }

    private let insertionIndicatorLayer = CALayer()
    private var insertionIndicatorFrame: CGRect?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setup()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setup()
    }

    private func setup() {
        // RULE 5: Custom indent via frameOfCell only
        indentationPerLevel = 0
        // RULE 14: Conflicts with fresh-node-per-snapshot
        autosaveExpandedItems = false
        selectionHighlightStyle = .none
        headerView = nil
        floatsGroupRows = false
        rowSizeStyle = .custom
        allowsMultipleSelection = true
        backgroundColor = MahoAppColors.sidebarBackground
        intercellSpacing = NSSize(width: 0, height: 2)
        wantsLayer = true
        insertionIndicatorLayer.opacity = 0
        insertionIndicatorLayer.cornerRadius = InsertionIndicatorMetrics.cornerRadius
        insertionIndicatorLayer.actions = [
            "position": NSNull(),
            "bounds": NSNull(),
            "opacity": NSNull(),
            "backgroundColor": NSNull()
        ]
        layer?.addSublayer(insertionIndicatorLayer)
        refreshAppearance()
    }

    func refreshAppearance() {
        backgroundColor = MahoAppColors.sidebarBackground
        insertionIndicatorLayer.backgroundColor = MahoAppColors.dropIndicator.cgColor
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    override func frameOfCell(atColumn column: Int, row: Int) -> NSRect {
        var frame = super.frameOfCell(atColumn: column, row: row)
        let rowRect = rect(ofRow: row)
        let item = self.item(atRow: row)

        // Use rowRect as base for all items to ensure consistent horizontal alignment
        frame.origin.x = rowRect.origin.x
        frame.size.width = rowRect.size.width

        // Indent items inside folders (accumulate for nested folders)
        var indent: CGFloat = 0
        var current = self.parent(forItem: item)
        while let p = current as? SectionNode, p.isFolder {
            indent += 15
            current = self.parent(forItem: current)
        }
        frame.origin.x += indent
        frame.size.width -= indent

        return frame
    }

    override func frameOfOutlineCell(atRow row: Int) -> NSRect {
        return .zero
    }

    override func mouseDown(with event: NSEvent) {
        let localPoint = convert(event.locationInWindow, from: nil)
        let clickedRow = row(at: localPoint)
        if clickedRow >= 0, let section = item(atRow: clickedRow) as? SectionNode {
            if section.sectionId == .newTab {
                findSidebarController()?.newTab()
                return
            }
        }
        super.mouseDown(with: event)
    }

    override func keyDown(with event: NSEvent) {
        guard let sidebarController = findSidebarController() else {
            super.keyDown(with: event)
            return
        }

        let row = selectedRow
        guard row >= 0 else {
            super.keyDown(with: event)
            return
        }

        switch event.keyCode {
        case 36:
            if let tab = item(atRow: row) as? TabNode {
                sidebarController.activateTab(tab.tabId)
            } else if let section = item(atRow: row) as? SectionNode, section.sectionId == .newTab {
                sidebarController.newTab()
            } else if let section = item(atRow: row) as? SectionNode, section.isFolder {
                sidebarController.toggleFolder(section)
            }
        case 49:
            if let section = item(atRow: row) as? SectionNode, section.sectionId == .newTab {
                sidebarController.newTab()
            }
        case 51:
            if let tab = item(atRow: row) as? TabNode {
                sidebarController.closeTab(tab.tabId)
            }
        default:
            super.keyDown(with: event)
        }
    }

    override func draggingExited(_ sender: NSDraggingInfo?) {
        findSidebarController()?.handleOutlineDraggingExited()
        super.draggingExited(sender)
    }


    override func setDropItem(_ item: Any?, dropChildIndex index: Int) {
        super.setDropItem(item, dropChildIndex: index)
        updateCustomInsertionIndicator(for: item, childIndex: index)
    }

    func setCustomInsertionIndicatorVisible(_ visible: Bool) {
        if visible, let insertionIndicatorFrame {
            showCustomInsertionIndicator(in: insertionIndicatorFrame)
        } else {
            hideCustomInsertionIndicator(animated: true)
        }
    }

    override func layout() {
        super.layout()
        if let insertionIndicatorFrame {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            insertionIndicatorLayer.frame = insertionIndicatorFrame
            CATransaction.commit()
        }
    }

    private func updateCustomInsertionIndicator(for item: Any?, childIndex: Int) {
        guard childIndex != NSOutlineViewDropOnItemIndex else {
            insertionIndicatorFrame = nil
            hideCustomInsertionIndicator(animated: true)
            return
        }

        let targetFrame = indicatorFrame(for: item, childIndex: childIndex)
        insertionIndicatorFrame = targetFrame

        let usingDefaultFeedback = draggingDestinationFeedbackStyle != .none
        guard usingDefaultFeedback else {
            hideCustomInsertionIndicator(animated: true)
            return
        }

        showCustomInsertionIndicator(in: targetFrame)
    }

    private func showCustomInsertionIndicator(in rect: NSRect) {
        let indicatorRect = rect.isEmpty ? (insertionIndicatorFrame ?? indicatorFrame(for: nil, childIndex: -1)) : rect
        guard indicatorRect.width > 0, indicatorRect.height > 0 else {
            hideCustomInsertionIndicator(animated: false)
            return
        }

        insertionIndicatorFrame = indicatorRect
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        insertionIndicatorLayer.frame = indicatorRect
        CATransaction.commit()

        let fade = CABasicAnimation(keyPath: "opacity")
        fade.fromValue = insertionIndicatorLayer.presentation()?.opacity ?? insertionIndicatorLayer.opacity
        fade.toValue = 1
        fade.duration = InsertionIndicatorMetrics.fadeDuration
        insertionIndicatorLayer.add(fade, forKey: "sidebarInsertionFadeIn")
        insertionIndicatorLayer.opacity = 1
    }

    private func hideCustomInsertionIndicator(animated: Bool) {
        insertionIndicatorFrame = nil
        if animated, insertionIndicatorLayer.opacity > 0.001 {
            let fade = CABasicAnimation(keyPath: "opacity")
            fade.fromValue = insertionIndicatorLayer.presentation()?.opacity ?? insertionIndicatorLayer.opacity
            fade.toValue = 0
            fade.duration = InsertionIndicatorMetrics.fadeDuration
            insertionIndicatorLayer.add(fade, forKey: "sidebarInsertionFadeOut")
        } else {
            insertionIndicatorLayer.removeAnimation(forKey: "sidebarInsertionFadeOut")
        }
        insertionIndicatorLayer.opacity = 0
    }

    private func indicatorFrame(for item: Any?, childIndex: Int) -> CGRect {
        let visibleBounds = bounds.intersection(visibleRect)
        let fallbackY = visibleBounds.minY + 2
        let rowPadding = InsertionIndicatorMetrics.horizontalPadding
        let lineHeight = InsertionIndicatorMetrics.height

                if let section = item as? SectionNode, childIndex >= 0 {
            if childIndex < section.children.count {
                let targetChild = section.children[childIndex]
                let childRow = row(forItem: targetChild)
                if childRow >= 0 {
                    let rowRect = rect(ofRow: childRow)
                    return CGRect(
                        x: rowRect.minX + rowPadding,
                        y: rowRect.maxY - (lineHeight / 2),
                        width: max(0, rowRect.width - (rowPadding * 2)),
                        height: lineHeight
                    )
                }
            }

            let sectionRow = row(forItem: section)
            if sectionRow >= 0 {
                let sectionRect = rect(ofRow: sectionRow)
                let lastChildRect: NSRect?
                if let lastChild = section.children.last {
                    let lastChildRow = row(forItem: lastChild)
                    lastChildRect = lastChildRow >= 0 ? rect(ofRow: lastChildRow) : nil
                } else {
                    lastChildRect = nil
                }
                let anchorRect = lastChildRect ?? sectionRect
                return CGRect(
                    x: anchorRect.minX + rowPadding,
                    y: anchorRect.minY - (lineHeight / 2),
                    width: max(0, anchorRect.width - (rowPadding * 2)),
                    height: lineHeight
                )
            }
        }

        if let item {
            let row = row(forItem: item)
            if row >= 0 {
                let rowRect = rect(ofRow: row)
                let y = childIndex == 0 ? rowRect.maxY - (lineHeight / 2) : rowRect.minY - (lineHeight / 2)
                return CGRect(
                    x: rowRect.minX + rowPadding,
                    y: y,
                    width: max(0, rowRect.width - (rowPadding * 2)),
                    height: lineHeight
                )
            }
        }

        return CGRect(
            x: visibleBounds.minX + rowPadding,
            y: fallbackY,
            width: max(0, visibleBounds.width - (rowPadding * 2)),
            height: lineHeight
        )
    }

    private func findSidebarController() -> SidebarController? {
        var responder: NSResponder? = nextResponder
        while let r = responder {
            if let sc = r as? SidebarController { return sc }
            responder = r.nextResponder
        }
        return nil
    }
}
