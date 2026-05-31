import AppKit

// MARK: - Section / Folder Header Cell

class SidebarSectionCellView: NSTableCellView {
    private let titleLabel = NSTextField(labelWithString: "")
    private let folderIcon = NSImageView()
    private let stackView = NSStackView()
    var onPrimaryClick: (() -> Void)?
    private var isFolderRow = false
    private var folderId: String?
    private var dragStartEvent: NSEvent?
    private var dragStartLocation: NSPoint?
    private var didStartDrag = false
    private var pendingPrimaryClick = false

    convenience init(identifier: NSUserInterfaceItemIdentifier) {
        self.init(frame: .zero)
        self.identifier = identifier
        setupViews()
    }

    private func setupViews() {
        folderIcon.translatesAutoresizingMaskIntoConstraints = false
        folderIcon.imageScaling = .scaleProportionallyUpOrDown
        folderIcon.setContentHuggingPriority(.required, for: .horizontal)
        NSLayoutConstraint.activate([
            folderIcon.widthAnchor.constraint(equalToConstant: 16),
            folderIcon.heightAnchor.constraint(equalToConstant: 16),
        ])

        titleLabel.font = NSFont.systemFont(ofSize: 11, weight: .medium)
        titleLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.50)
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        stackView.orientation = .horizontal
        stackView.alignment = .centerY
        stackView.spacing = 6
        stackView.edgeInsets = NSEdgeInsets(top: 0, left: SidebarMetrics.sidebarHorizontalInset, bottom: 0, right: SidebarMetrics.sidebarHorizontalInset)
        stackView.translatesAutoresizingMaskIntoConstraints = false

        stackView.addArrangedSubview(folderIcon)
        stackView.addArrangedSubview(titleLabel)

        self.textField = titleLabel
        addSubview(stackView)
        NSLayoutConstraint.activate([
            stackView.leadingAnchor.constraint(equalTo: leadingAnchor),
            stackView.trailingAnchor.constraint(equalTo: trailingAnchor),
            stackView.topAnchor.constraint(equalTo: topAnchor),
            stackView.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
    }

    func configure(with section: SectionNode) {
        isFolderRow = section.isFolder
        folderId = section.folderId
        dragStartEvent = nil
        dragStartLocation = nil
        didStartDrag = false
        pendingPrimaryClick = false
        titleLabel.stringValue = section.title.uppercased()
        setAccessibilityElement(true)

        folderIcon.setAccessibilityElement(false)
        titleLabel.setAccessibilityElement(false)

        if section.isFolder {
            titleLabel.stringValue = section.name
            titleLabel.font = NSFont.systemFont(ofSize: 12, weight: .medium)
            titleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.84)
            folderIcon.isHidden = false
            let symbolName = section.isExpanded ? "folder" : "folder.fill"
            folderIcon.image = MahoIcon.imageForSFSymbol(symbolName, pointSize: 13, accessibilityDescription: section.isExpanded ? "Expanded" : "Collapsed")
            folderIcon.contentTintColor = MahoAppColors.folderIconColor

            if let folderId = section.folderId {
                setAccessibilityIdentifier("sidebar.folder.\(folderId)")
            }
            setAccessibilityValue(nil)
            setAccessibilityLabel("\(section.name) \(section.isExpanded ? "expanded" : "collapsed")")
        } else {
            titleLabel.stringValue = ""
            titleLabel.font = NSFont.systemFont(ofSize: 8, weight: .medium)
            titleLabel.textColor = .clear
            folderIcon.isHidden = true
            setAccessibilityIdentifier("sidebar.section.\(section.sectionId.rawValue)")
            setAccessibilityValue(nil)
            setAccessibilityLabel("")
        }
    }

    override func mouseDown(with event: NSEvent) {
        if isFolderRow, event.type == .leftMouseDown {
            dragStartEvent = event
            dragStartLocation = convert(event.locationInWindow, from: nil)
            didStartDrag = false
            pendingPrimaryClick = true
            return
        }
        super.mouseDown(with: event)
    }

    override func mouseUp(with event: NSEvent) {
        if pendingPrimaryClick && !didStartDrag {
            pendingPrimaryClick = false
            dragStartEvent = nil
            dragStartLocation = nil
            onPrimaryClick?()
            return
        }
        pendingPrimaryClick = false
        dragStartEvent = nil
        dragStartLocation = nil
        super.mouseUp(with: event)
    }

    override func mouseDragged(with event: NSEvent) {
        guard isFolderRow,
              let folderId,
              let dragStartEvent,
              let dragStartLocation,
              let controller = findSidebarController() else {
            pendingPrimaryClick = false
            super.mouseDragged(with: event)
            return
        }

        let currentLocation = convert(event.locationInWindow, from: nil)
        let dx = currentLocation.x - dragStartLocation.x
        let dy = currentLocation.y - dragStartLocation.y
        guard !didStartDrag, (dx * dx + dy * dy) > 9 else { return }

        didStartDrag = true
        pendingPrimaryClick = false
        let pasteboardItem = NSPasteboardItem()
        let json = "{\"type\":\"folder\",\"id\":\"\(folderId)\"}"
        pasteboardItem.setString(json, forType: SidebarDragController.dragType)
        let draggingItem = NSDraggingItem(pasteboardWriter: pasteboardItem)
        let frameInOutline = convert(bounds, to: controller.outlineView)
        draggingItem.setDraggingFrame(frameInOutline, contents: snapshot())
        controller.beginFolderDragSession()
        controller.outlineView.beginDraggingSession(with: [draggingItem], event: dragStartEvent, source: self)
    }

    override func accessibilityPerformPress() -> Bool {
        if isFolderRow {
            onPrimaryClick?()
            return true
        }
        return super.accessibilityPerformPress()
    }

    override func hitTest(_ point: NSPoint) -> NSView? {
        if isFolderRow {
            let local = convert(point, from: superview)
            if bounds.contains(local) {
                return self
            }
        }
        return super.hitTest(point)
    }

    private func snapshot() -> NSImage {
        let scale: CGFloat = 0.95
        let cornerRadius: CGFloat = 6
        let shadowBlur: CGFloat = 14
        let shadowOffset = CGSize(width: 0, height: -4)
        let shadowPadding = CGSize(width: 10, height: 12)
        let snapshotSize = NSSize(
            width: bounds.width + shadowPadding.width * 2,
            height: bounds.height + shadowPadding.height * 2
        )
        let scaledBounds = CGRect(
            x: shadowPadding.width + ((bounds.width * (1 - scale)) / 2),
            y: shadowPadding.height + ((bounds.height * (1 - scale)) / 2),
            width: bounds.width * scale,
            height: bounds.height * scale
        )

        let image = NSImage(size: snapshotSize)
        image.lockFocus()
        guard let context = NSGraphicsContext.current?.cgContext else {
            image.unlockFocus()
            return image
        }

        context.setAllowsAntialiasing(true)
        context.setShouldAntialias(true)

        let clipPath = CGPath(
            roundedRect: scaledBounds,
            cornerWidth: cornerRadius,
            cornerHeight: cornerRadius,
            transform: nil
        )

        context.saveGState()
        context.setShadow(offset: shadowOffset, blur: shadowBlur, color: NSColor.black.withAlphaComponent(0.20).cgColor)
        context.addPath(clipPath)
        context.setFillColor(NSColor.black.withAlphaComponent(0.01).cgColor)
        context.fillPath()
        context.restoreGState()

        context.addPath(clipPath)
        context.clip()
        context.translateBy(x: scaledBounds.minX, y: scaledBounds.minY)
        context.scaleBy(x: scale, y: scale)
        context.setAlpha(0.82)
        layer?.render(in: context)
        image.unlockFocus()
        return image
    }

    private func findSidebarController() -> SidebarController? {
        var responder: NSResponder? = nextResponder
        while let current = responder {
            if let controller = current as? SidebarController {
                return controller
            }
            responder = current.nextResponder
        }
        return nil
    }
}

extension SidebarSectionCellView: NSDraggingSource {
    func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
        .move
    }

    func draggingSession(_ session: NSDraggingSession, endedAt screenPoint: NSPoint, operation: NSDragOperation) {
        findSidebarController()?.endFolderDragSession()
        dragStartEvent = nil
        dragStartLocation = nil
        didStartDrag = false
        pendingPrimaryClick = false
    }
}

private extension SectionNode {
    var name: String { title }
}
