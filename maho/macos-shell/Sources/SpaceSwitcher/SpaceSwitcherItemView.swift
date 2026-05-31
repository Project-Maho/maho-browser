import AppKit

protocol SpaceSwitcherItemViewDelegate: AnyObject {
    func spaceSwitcherItemViewDidActivate(_ itemView: SpaceSwitcherItemView)
    func spaceSwitcherItemViewDidSelect(_ itemView: SpaceSwitcherItemView)
    func spaceSwitcherItemViewDidRequestContextMenu(_ itemView: SpaceSwitcherItemView, event: NSEvent)
}

final class SpaceSwitcherItemView: NSView, NSDraggingSource {
    weak var delegate: SpaceSwitcherItemViewDelegate?

    var index: Int = 0
    var isSelected: Bool = false {
        didSet { updateAppearance(animated: true) }
    }

    var isHovered: Bool = false {
        didSet {
            window?.invalidateCursorRects(for: self)
            updateAppearance(animated: true)
        }
    }

    var spaceId: SpaceId? {
        item.space?.id
    }

    private let indicatorContainerView = NSView()
    private let colorBadgeView = NSView()
    private let colorBadgeGlowView = NSView()
    private let profileBadgeView = NSView()
    private let iconTextLabel = NSTextField(labelWithString: "")
    private let iconImageView = NSImageView()

    private var colorBadgeSizeConstraint: NSLayoutConstraint?
    private var glowSizeW: NSLayoutConstraint?
    private var glowSizeH: NSLayoutConstraint?
    private var dragStartPoint: NSPoint?
    private var didStartDragging = false
    private var item: SpaceSwitcherDisplayItem
    private var trackingArea: NSTrackingArea?
    private var hasIcon = false
    private var showsTextIcon = false

    init(item: SpaceSwitcherDisplayItem) {
        self.item = item
        super.init(frame: .zero)
        setupUI()
        configure(with: item)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override var acceptsFirstResponder: Bool { false }

    private func setupUI() {
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        setAccessibilityElement(true)
        setAccessibilityRole(.button)

        indicatorContainerView.translatesAutoresizingMaskIntoConstraints = false
        indicatorContainerView.wantsLayer = true
        addSubview(indicatorContainerView)

        colorBadgeGlowView.translatesAutoresizingMaskIntoConstraints = false
        colorBadgeGlowView.wantsLayer = true
        colorBadgeGlowView.alphaValue = 0
        indicatorContainerView.addSubview(colorBadgeGlowView)

        colorBadgeView.translatesAutoresizingMaskIntoConstraints = false
        colorBadgeView.wantsLayer = true
        indicatorContainerView.addSubview(colorBadgeView)

        profileBadgeView.translatesAutoresizingMaskIntoConstraints = false
        profileBadgeView.wantsLayer = true
        profileBadgeView.isHidden = true
        profileBadgeView.layer?.cornerRadius = 2
        profileBadgeView.layer?.masksToBounds = true
        profileBadgeView.layer?.borderWidth = 1
        profileBadgeView.layer?.borderColor = NSColor.windowBackgroundColor.cgColor
        indicatorContainerView.addSubview(profileBadgeView)

        iconTextLabel.translatesAutoresizingMaskIntoConstraints = false
        iconTextLabel.alignment = .center
        iconTextLabel.font = .systemFont(ofSize: 10, weight: .medium)
        iconTextLabel.isHidden = true
        indicatorContainerView.addSubview(iconTextLabel)

        iconImageView.translatesAutoresizingMaskIntoConstraints = false
        iconImageView.imageScaling = .scaleProportionallyDown
        iconImageView.contentTintColor = NSColor(white: 0.84, alpha: 1.0)
        iconImageView.isHidden = true
        indicatorContainerView.addSubview(iconImageView)

        let badgeSizeConstraint = colorBadgeView.widthAnchor.constraint(equalToConstant: 6)
        self.colorBadgeSizeConstraint = badgeSizeConstraint

        let glowW = colorBadgeGlowView.widthAnchor.constraint(equalToConstant: 14)
        let glowH = colorBadgeGlowView.heightAnchor.constraint(equalToConstant: 14)
        self.glowSizeW = glowW
        self.glowSizeH = glowH

        NSLayoutConstraint.activate([
            widthAnchor.constraint(equalToConstant: 20),
            heightAnchor.constraint(equalToConstant: 20),

            indicatorContainerView.centerXAnchor.constraint(equalTo: centerXAnchor),
            indicatorContainerView.centerYAnchor.constraint(equalTo: centerYAnchor),
            indicatorContainerView.widthAnchor.constraint(equalToConstant: 16),
            indicatorContainerView.heightAnchor.constraint(equalToConstant: 16),

            colorBadgeGlowView.centerXAnchor.constraint(equalTo: indicatorContainerView.centerXAnchor),
            colorBadgeGlowView.centerYAnchor.constraint(equalTo: indicatorContainerView.centerYAnchor),
            glowW,
            glowH,

            colorBadgeView.centerXAnchor.constraint(equalTo: indicatorContainerView.centerXAnchor),
            colorBadgeView.centerYAnchor.constraint(equalTo: indicatorContainerView.centerYAnchor),
            badgeSizeConstraint,
            colorBadgeView.heightAnchor.constraint(equalTo: colorBadgeView.widthAnchor),

            profileBadgeView.widthAnchor.constraint(equalToConstant: 4),
            profileBadgeView.heightAnchor.constraint(equalToConstant: 4),
            profileBadgeView.trailingAnchor.constraint(equalTo: indicatorContainerView.trailingAnchor, constant: -1),
            profileBadgeView.bottomAnchor.constraint(equalTo: indicatorContainerView.bottomAnchor, constant: -1),

            iconTextLabel.centerXAnchor.constraint(equalTo: indicatorContainerView.centerXAnchor),
            iconTextLabel.centerYAnchor.constraint(equalTo: indicatorContainerView.centerYAnchor),

            iconImageView.centerXAnchor.constraint(equalTo: indicatorContainerView.centerXAnchor),
            iconImageView.centerYAnchor.constraint(equalTo: indicatorContainerView.centerYAnchor),
            iconImageView.widthAnchor.constraint(equalToConstant: 11),
            iconImageView.heightAnchor.constraint(equalToConstant: 11),
        ])
    }

    func configure(with item: SpaceSwitcherDisplayItem) {
        self.item = item
        toolTip = item.toolTip
        configureIcon(from: item.icon)
        updateAppearance(animated: false)
    }

    private func configureIcon(from icon: String?) {
        guard let icon, !icon.isEmpty else {
            iconTextLabel.stringValue = ""
            iconImageView.image = nil
            hasIcon = false
            showsTextIcon = false
            return
        }

        if isEmoji(icon) {
            iconTextLabel.stringValue = icon
            iconImageView.image = nil
            hasIcon = true
            showsTextIcon = true
            return
        }

        iconTextLabel.stringValue = ""
        iconImageView.image = configuredSymbol(named: icon, pointSize: 11)
        hasIcon = iconImageView.image != nil
        showsTextIcon = false
    }

    private func configuredSymbol(named symbolName: String, pointSize: CGFloat) -> NSImage? {
        return MahoIcon.imageForSFSymbol(symbolName, pointSize: pointSize)
    }

    private func isEmoji(_ value: String) -> Bool {
        guard !value.isEmpty else { return false }
        return value.unicodeScalars.contains { $0.properties.isEmojiPresentation || $0.properties.isEmoji }
    }

    func configureProfileBadge(color: NSColor?, visible: Bool) {
        profileBadgeView.isHidden = !visible
        profileBadgeView.layer?.backgroundColor = color?.cgColor
        profileBadgeView.layer?.borderColor = NSColor.windowBackgroundColor.cgColor
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    func refreshAppearance() {
        profileBadgeView.layer?.borderColor = NSColor.windowBackgroundColor.cgColor
        updateAppearance(animated: false)
    }

    private func updateAppearance(animated: Bool = false) {
        let showIcon = isHovered && hasIcon

        let badgeSize: CGFloat
        if item.isActive {
            badgeSize = 8
        } else if isSelected {
            badgeSize = 4
        } else if isHovered && !hasIcon {
            badgeSize = 4.5
        } else {
            badgeSize = 4
        }

        let glowSize: CGFloat = badgeSize + 4

        let appearance = SpaceThemeManager.shared.itemAppearance(
            color: item.color,
            theme: item.theme,
            isActive: item.isActive,
            isSelected: isSelected,
            isHovered: isHovered && !hasIcon
        )

        let glowVisible = false
        let badgeAlpha: CGFloat = showIcon ? 0.0 : 1.0

        profileBadgeView.isHidden = true

        let applyConstraintChanges = {
            self.colorBadgeSizeConstraint?.constant = badgeSize
            self.glowSizeW?.constant = glowSize
            self.glowSizeH?.constant = glowSize
        }

        let applyLayerChanges = {
            self.colorBadgeView.layer?.cornerRadius = badgeSize / 2
            self.colorBadgeView.layer?.backgroundColor = appearance.badgeFillColor.cgColor
            self.colorBadgeView.layer?.borderColor = NSColor.clear.cgColor
            self.colorBadgeView.layer?.borderWidth = 0
            self.colorBadgeGlowView.layer?.cornerRadius = glowSize / 2
            self.colorBadgeGlowView.layer?.backgroundColor = appearance.badgeInnerGlowColor.cgColor
        }

        let applyViewChanges = {
            self.iconTextLabel.isHidden = !showIcon || !self.showsTextIcon
            self.iconImageView.isHidden = !showIcon || self.showsTextIcon
            self.iconTextLabel.alphaValue = self.item.isActive ? 1.0 : 0.9
            self.iconImageView.alphaValue = self.item.isActive ? 1.0 : 0.9
            self.colorBadgeView.alphaValue = badgeAlpha
            self.colorBadgeGlowView.alphaValue = glowVisible ? 1.0 : 0.0
            self.alphaValue = appearance.alpha
            self.layoutSubtreeIfNeeded()
        }

        if animated, window != nil {
            applyConstraintChanges()

            CATransaction.begin()
            CATransaction.setAnimationDuration(0.16)
            CATransaction.setAnimationTimingFunction(CAMediaTimingFunction(name: .easeInEaseOut))
            applyLayerChanges()
            CATransaction.commit()

            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.16
                context.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                self.colorBadgeGlowView.animator().alphaValue = glowVisible ? 1.0 : 0.0
                applyViewChanges()
            }
        } else {
            applyConstraintChanges()
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            applyLayerChanges()
            CATransaction.commit()
            applyViewChanges()
        }
    }

    override func mouseDown(with event: NSEvent) {
        delegate?.spaceSwitcherItemViewDidSelect(self)
        dragStartPoint = convert(event.locationInWindow, from: nil)
        didStartDragging = false
    }

    override func rightMouseDown(with event: NSEvent) {
        delegate?.spaceSwitcherItemViewDidSelect(self)
        delegate?.spaceSwitcherItemViewDidRequestContextMenu(self, event: event)
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

    override func resetCursorRects() {
        super.resetCursorRects()
        if isHovered {
            addCursorRect(bounds, cursor: .pointingHand)
        }
    }

    override func mouseEntered(with event: NSEvent) {
        isHovered = true
    }

    override func mouseExited(with event: NSEvent) {
        isHovered = false
    }

    override func mouseDragged(with event: NSEvent) {
        guard item.space != nil,
              let dragStartPoint else {
            return
        }

        let currentPoint = convert(event.locationInWindow, from: nil)
        let distance = hypot(currentPoint.x - dragStartPoint.x, currentPoint.y - dragStartPoint.y)
        guard distance > 4, !didStartDragging else { return }
        didStartDragging = true
        beginDrag(with: event)
    }

    override func mouseUp(with event: NSEvent) {
        defer {
            dragStartPoint = nil
            didStartDragging = false
        }

        guard !didStartDragging else { return }
        delegate?.spaceSwitcherItemViewDidActivate(self)
    }

    private func beginDrag(with event: NSEvent) {
        guard let spaceId else { return }
        let pasteboardItem = NSPasteboardItem()
        pasteboardItem.setString("\(spaceId)", forType: SpaceSwitcherView.dragType)

        let draggingItem = NSDraggingItem(pasteboardWriter: pasteboardItem)
        let dragFrame = bounds.insetBy(dx: 0, dy: 1)
        let image = bitmapImage()
        draggingItem.setDraggingFrame(dragFrame, contents: image)

        beginDraggingSession(with: [draggingItem], event: event, source: self)
    }

    private func bitmapImage() -> NSImage? {
        let rep = bitmapImageRepForCachingDisplay(in: bounds)
        guard let rep else { return nil }
        cacheDisplay(in: bounds, to: rep)
        let image = NSImage(size: bounds.size)
        image.addRepresentation(rep)
        return image
    }

    func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
        .move
    }

    func ignoreModifierKeys(for session: NSDraggingSession) -> Bool {
        true
    }
}
