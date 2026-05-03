import AppKit

final class SidebarAddressBarView: NSView, NSTextFieldDelegate {

    private static let shadowColor = NSColor.black.withAlphaComponent(0.12)

    enum State {
        case idle
        case editing
    }

    // MARK: - Subviews

    private let containerView = AddressBarContainerView()
    private let faviconView = NSImageView()
    private let domainLabel = NSTextField(labelWithString: "Search or enter URL")
    private let urlTextField = NSTextField()
    private var urlTextFieldConstraints: [NSLayoutConstraint] = []

    // MARK: - Suggestions

    private let suggestionDropdown = SuggestionDropdownView()
    private var queryDebounceWork: DispatchWorkItem?

    // MARK: - State

    private(set) var currentState: State = .idle
    private var currentUrl: String = ""
    private var currentFavicon: NSImage?
    private var activeTabId: TabId?
    private var isSecure: Bool = true

    // MARK: - Progress

    private let progressLayer = CALayer()
    private var progressGeneration = 0

    // MARK: - Hover

    private var trackingArea: NSTrackingArea?
    private var isHovered = false

    // MARK: - Site Control

    private let siteControlButton: NSButton = {
        let btn = NSButton()
        btn.translatesAutoresizingMaskIntoConstraints = false
        btn.isBordered = false
        btn.bezelStyle = .inline
        btn.title = ""
        let config = NSImage.SymbolConfiguration(pointSize: 10, weight: .medium)
        btn.image = NSImage(systemSymbolName: "ellipsis.circle", accessibilityDescription: "Site controls")?.withSymbolConfiguration(config)
        btn.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
        btn.target = nil
        btn.action = nil
        btn.isHidden = true
        btn.setAccessibilityIdentifier("sidebar.addressBar.siteControl")
        return btn
    }()

    // MARK: - Callbacks

    var onUrlSubmitted: ((String, TabId) -> Void)?
    var onCommandBarRequested: (() -> Void)?
    var onSiteControlRequested: ((NSView) -> Void)?

    // MARK: - Init

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    // MARK: - Setup

    private func setupViews() {
        wantsLayer = true
        layer?.cornerRadius = ChromeGeometry.addressBarCornerRadius
        layer?.borderWidth = 1
        layer?.shadowOpacity = 1
        layer?.shadowRadius = 3
        layer?.shadowOffset = NSSize(width: 0, height: -1)

        setAccessibilityElement(true)
        setAccessibilityIdentifier("sidebar.addressBar")
        setAccessibilityLabel("Address Bar")

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(activeSpaceThemeDidChange),
            name: .mahoActiveSpaceThemeChanged,
            object: nil
        )

        // Container for idle-state subviews
        containerView.translatesAutoresizingMaskIntoConstraints = false
        containerView.wantsLayer = true
        containerView.setAccessibilityElement(true)
        containerView.setAccessibilityIdentifier("sidebar.search")
        containerView.setAccessibilityLabel("Search")
        containerView.setAccessibilityRole(.button)
        addSubview(containerView)

        // Favicon
        faviconView.translatesAutoresizingMaskIntoConstraints = false
        faviconView.imageScaling = .scaleProportionallyUpOrDown
        let defaultIcon = configuredSymbol(named: "magnifyingglass", pointSize: 12, accessibilityDescription: nil)
        faviconView.image = defaultIcon
        faviconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
        containerView.addSubview(faviconView)

        // Domain label
        domainLabel.translatesAutoresizingMaskIntoConstraints = false
        domainLabel.font = .systemFont(ofSize: 12, weight: .regular)
        domainLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
        domainLabel.lineBreakMode = .byTruncatingTail
        domainLabel.maximumNumberOfLines = 1
        domainLabel.setAccessibilityElement(false)
        containerView.addSubview(domainLabel)

        siteControlButton.target = self
        siteControlButton.action = #selector(siteControlTapped(_:))
        containerView.addSubview(siteControlButton)

        // URL text field (for editing state)
        urlTextField.translatesAutoresizingMaskIntoConstraints = false
        urlTextField.font = .systemFont(ofSize: 12, weight: .regular)
        urlTextField.textColor = MahoAppColors.primaryText
        urlTextField.backgroundColor = .clear
        urlTextField.isBordered = false
        urlTextField.focusRingType = .none
        urlTextField.placeholderString = "Search or enter URL"
        urlTextField.lineBreakMode = .byTruncatingTail
        urlTextField.cell?.isScrollable = true
        urlTextField.cell?.wraps = false
        urlTextField.isHidden = true
        urlTextField.setAccessibilityElement(false)

        NSLayoutConstraint.activate([
            containerView.leadingAnchor.constraint(equalTo: leadingAnchor),
            containerView.trailingAnchor.constraint(equalTo: trailingAnchor),
            containerView.topAnchor.constraint(equalTo: topAnchor),
            containerView.bottomAnchor.constraint(equalTo: bottomAnchor),

            faviconView.leadingAnchor.constraint(equalTo: containerView.leadingAnchor, constant: ChromeGeometry.addressBarFaviconLeading),
            faviconView.centerYAnchor.constraint(equalTo: containerView.centerYAnchor),
            faviconView.widthAnchor.constraint(equalToConstant: ChromeGeometry.addressBarFaviconSize),
            faviconView.heightAnchor.constraint(equalToConstant: ChromeGeometry.addressBarFaviconSize),

            domainLabel.leadingAnchor.constraint(equalTo: faviconView.trailingAnchor, constant: ChromeGeometry.addressBarIconTextGap),
            domainLabel.centerYAnchor.constraint(equalTo: containerView.centerYAnchor),
            domainLabel.trailingAnchor.constraint(lessThanOrEqualTo: siteControlButton.leadingAnchor, constant: -4),

            siteControlButton.trailingAnchor.constraint(equalTo: containerView.trailingAnchor, constant: -ChromeGeometry.addressBarTrailingInset),
            siteControlButton.centerYAnchor.constraint(equalTo: containerView.centerYAnchor),
            siteControlButton.widthAnchor.constraint(equalToConstant: 16),
            siteControlButton.heightAnchor.constraint(equalToConstant: 16),
        ])

        urlTextFieldConstraints = [
            urlTextField.leadingAnchor.constraint(equalTo: leadingAnchor, constant: ChromeGeometry.addressBarFaviconLeading),
            urlTextField.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -ChromeGeometry.addressBarTrailingInset),
            urlTextField.centerYAnchor.constraint(equalTo: centerYAnchor),
        ]

        containerView.addressBarView = self

        let clickGesture = NSClickGestureRecognizer(target: self, action: #selector(handleClick(_:)))
        containerView.addGestureRecognizer(clickGesture)

        suggestionDropdown.onSuggestionSelected = { [weak self] suggestion in
            self?.suggestionDropdown.dismiss()
            self?.queryDebounceWork?.cancel()
            self?.handleSuggestionSelected(suggestion)
        }

        progressLayer.isHidden = true
        progressLayer.frame = CGRect(x: 0, y: 0, width: 0, height: 2)
        progressLayer.cornerRadius = 1
        self.layer?.addSublayer(progressLayer)

        refreshAppearance()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    // MARK: - Public API

    func refreshAppearance() {
        let appearance = window?.effectiveAppearance ?? effectiveAppearance
        appearance.performAsCurrentDrawingAppearance {
            var bgColor = MahoAppColors.searchFieldBackground
            if let tint = SpaceThemeManager.shared.chromeTint(alpha: 0.06) {
                bgColor = bgColor.blended(withFraction: 0.15, of: tint) ?? bgColor
            }
            layer?.backgroundColor = bgColor.cgColor
            layer?.borderColor = MahoAppColors.searchFieldBorder.cgColor
            layer?.shadowColor = Self.shadowColor.cgColor
            progressLayer.backgroundColor = NSColor.controlAccentColor.cgColor
        }
        urlTextField.textColor = MahoAppColors.primaryText
        updateIdleDisplay()
        suggestionDropdown.refreshAppearance()
    }

    func update(url: String, favicon: NSImage?, tabId: TabId, isSecure: Bool = true) {
        activeTabId = tabId
        currentUrl = url
        currentFavicon = favicon
        self.isSecure = isSecure

        // If we're editing and tab changed, cancel editing
        if currentState == .editing {
            cancelEditing()
        }

        updateIdleDisplay()
    }

    func clearActiveTab() {
        activeTabId = nil
        currentUrl = ""
        currentFavicon = nil

        if currentState == .editing {
            cancelEditing()
        }

        // Show search placeholder
        faviconView.image = configuredSymbol(named: "magnifyingglass", pointSize: 12, accessibilityDescription: nil)
        faviconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
        domainLabel.stringValue = "Search or enter URL"
        domainLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
    }

    // MARK: - State Transitions

    func enterEditingState() {
        guard activeTabId != nil else {
            // No active tab — open command bar instead
            onCommandBarRequested?()
            return
        }

        currentState = .editing
        containerView.isHidden = true
        if urlTextField.superview == nil {
            addSubview(urlTextField)
            NSLayoutConstraint.activate(urlTextFieldConstraints)
        }
        urlTextField.isHidden = false
        urlTextField.delegate = self
        urlTextField.stringValue = currentUrl
        window?.makeFirstResponder(urlTextField)
        urlTextField.selectText(nil)
    }

    private func exitEditingState() {
        currentState = .idle
        urlTextField.delegate = nil
        urlTextField.isHidden = true
        if urlTextField.superview != nil {
            NSLayoutConstraint.deactivate(urlTextFieldConstraints)
            urlTextField.removeFromSuperview()
        }
        containerView.isHidden = false
        suggestionDropdown.dismiss()
        queryDebounceWork?.cancel()
        window?.makeFirstResponder(nil)
        updateIdleDisplay()
    }

    private func cancelEditing() {
        exitEditingState()
    }

    private func submitUrl() {
        guard let tabId = activeTabId else {
            exitEditingState()
            return
        }

        let input = urlTextField.stringValue
        exitEditingState()

        guard let url = UrlSubmissionHelper.resolvedUrl(from: input) else { return }
        onUrlSubmitted?(url.value, tabId)
    }

    // MARK: - Display

    private func updateIdleDisplay() {
        let domain = UrlSubmissionHelper.displayDomain(from: currentUrl)
        let hasUrl = !domain.isEmpty && !currentUrl.isEmpty

        siteControlButton.isHidden = !hasUrl

        if !hasUrl {
            faviconView.image = configuredSymbol(named: "magnifyingglass", pointSize: 12, accessibilityDescription: nil)
            faviconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
            domainLabel.stringValue = "Search or enter URL"
            domainLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
        } else if !isSecure {
            faviconView.image = configuredSymbol(named: "exclamationmark.triangle.fill", pointSize: 12, accessibilityDescription: "Not Secure")
            faviconView.contentTintColor = .systemOrange
            domainLabel.stringValue = domain
            domainLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.85)
        } else {
            if let favicon = currentFavicon {
                faviconView.image = favicon
                faviconView.contentTintColor = nil
            } else {
                faviconView.image = configuredSymbol(named: "globe", pointSize: 13, accessibilityDescription: "Web page")
                faviconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
            }
            domainLabel.stringValue = domain
            domainLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.85)
        }
    }

    // MARK: - Actions

    @objc private func handleClick(_ sender: NSClickGestureRecognizer) {
        enterEditingState()
    }

    // MARK: - NSTextFieldDelegate

    func controlTextDidChange(_ obj: Notification) {
        queryDebounceWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let text = self?.urlTextField.stringValue, !text.isEmpty else {
                self?.suggestionDropdown.dismiss()
                return
            }
            CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarQuery(text: text, mode: nil))
        }
        queryDebounceWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: work)
    }

    func controlTextDidEndEditing(_ obj: Notification) {
        if currentState == .editing {
            exitEditingState()
        }
    }

    func control(_ control: NSControl, textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        if commandSelector == #selector(NSResponder.insertNewline(_:)) {
            if let selected = suggestionDropdown.selectedSuggestion {
                suggestionDropdown.dismiss()
                queryDebounceWork?.cancel()
                handleSuggestionSelected(selected)
            } else {
                submitUrl()
            }
            return true
        }
        if commandSelector == #selector(NSResponder.cancelOperation(_:)) {
            if !suggestionDropdown.isHidden {
                suggestionDropdown.dismiss()
                queryDebounceWork?.cancel()
            } else {
                cancelEditing()
            }
            return true
        }
        if commandSelector == #selector(NSResponder.moveDown(_:)) {
            suggestionDropdown.moveSelectionDown()
            return true
        }
        if commandSelector == #selector(NSResponder.moveUp(_:)) {
            suggestionDropdown.moveSelectionUp()
            return true
        }
        return false
    }

    // MARK: - Suggestions API

    func updateSuggestions(_ suggestions: [SuggestionViewModel]) {
        guard currentState == .editing else { return }
        if suggestions.isEmpty {
            suggestionDropdown.dismiss()
        } else {
            suggestionDropdown.updateSuggestions(suggestions)
            suggestionDropdown.show(relativeTo: self)
        }
    }

    private func handleSuggestionSelected(_ suggestion: SuggestionViewModel) {
        guard let tabId = activeTabId else {
            exitEditingState()
            return
        }
        exitEditingState()

        if let destination = SuggestionDestinationResolver.resolve(
            kind: suggestion.kind,
            key: suggestion.key,
            title: suggestion.title,
            subtitle: suggestion.subtitle,
            executionPayload: suggestion.executionPayload
        ) {
            onUrlSubmitted?(destination, tabId)
        }
    }

    // MARK: - Helpers

    func updateProgress(_ progress: Double, isLoading: Bool) {
        let barY: CGFloat = 0
        let barHeight: CGFloat = 2

        if isLoading && progress > 0 && progress < 1 {
            progressGeneration += 1
            progressLayer.isHidden = false
            progressLayer.opacity = 1
            let targetWidth = bounds.width * CGFloat(progress)
            CATransaction.begin()
            CATransaction.setAnimationDuration(0.25)
            progressLayer.frame = CGRect(x: 0, y: barY, width: targetWidth, height: barHeight)
            CATransaction.commit()
        } else if !isLoading && progress == 0 {
            progressGeneration += 1
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            progressLayer.isHidden = true
            progressLayer.frame = CGRect(x: 0, y: barY, width: 0, height: barHeight)
            progressLayer.opacity = 1
            CATransaction.commit()
        } else {
            progressGeneration += 1
            let gen = progressGeneration
            CATransaction.begin()
            CATransaction.setAnimationDuration(0.2)
            progressLayer.frame = CGRect(x: 0, y: barY, width: bounds.width, height: barHeight)
            CATransaction.commit()

            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
                guard let self, self.progressGeneration == gen else { return }
                CATransaction.begin()
                CATransaction.setAnimationDuration(0.2)
                self.progressLayer.opacity = 0
                CATransaction.commit()

                DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
                    guard let self, self.progressGeneration == gen else { return }
                    self.progressLayer.isHidden = true
                    self.progressLayer.frame = CGRect(x: 0, y: barY, width: 0, height: barHeight)
                    self.progressLayer.opacity = 1
                }
            }
        }
    }

    @objc private func siteControlTapped(_ sender: NSButton) {
        onSiteControlRequested?(siteControlButton)
    }

    @objc private func activeSpaceThemeDidChange() {
        refreshAppearance()
    }

    // MARK: - Hover

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let existing = trackingArea {
            removeTrackingArea(existing)
        }
        let area = NSTrackingArea(
            rect: bounds,
            options: [.mouseEnteredAndExited, .activeInKeyWindow],
            owner: self,
            userInfo: nil
        )
        trackingArea = area
        addTrackingArea(area)
    }

    override func mouseEntered(with event: NSEvent) {
        super.mouseEntered(with: event)
        guard currentState == .idle else { return }
        isHovered = true
        applyHoverEffect(true)
    }

    override func mouseExited(with event: NSEvent) {
        super.mouseExited(with: event)
        isHovered = false
        applyHoverEffect(false)
    }

    private func applyHoverEffect(_ hovered: Bool) {
        CATransaction.begin()
        CATransaction.setAnimationDuration(0.15)
        let appearance = window?.effectiveAppearance ?? effectiveAppearance
        appearance.performAsCurrentDrawingAppearance {
            var baseColor = MahoAppColors.searchFieldBackground
            if let tint = SpaceThemeManager.shared.chromeTint(alpha: 0.06) {
                baseColor = baseColor.blended(withFraction: 0.15, of: tint) ?? baseColor
            }
            if hovered {
                layer?.backgroundColor = baseColor.blended(
                    withFraction: 0.08,
                    of: NSColor.controlAccentColor
                )?.cgColor ?? baseColor.cgColor
            } else {
                layer?.backgroundColor = baseColor.cgColor
            }
        }
        CATransaction.commit()
    }

    private func configuredSymbol(named symbolName: String, pointSize: CGFloat, accessibilityDescription: String?) -> NSImage? {
        let configuration = NSImage.SymbolConfiguration(pointSize: pointSize, weight: .medium)
        return NSImage(systemSymbolName: symbolName, accessibilityDescription: accessibilityDescription)?.withSymbolConfiguration(configuration)
    }
}

private final class AddressBarContainerView: NSView, NSDraggingSource {
    weak var addressBarView: SidebarAddressBarView?
    private var dragOrigin: NSPoint?

    func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
        context == .outsideApplication ? .copy : .generic
    }

    override func mouseDown(with event: NSEvent) {
        dragOrigin = convert(event.locationInWindow, from: nil)
        super.mouseDown(with: event)
    }

    override func mouseDragged(with event: NSEvent) {
        guard let origin = dragOrigin,
              let addressBar = addressBarView,
              addressBar.currentState == .idle,
              !addressBar.currentUrlForDrag.isEmpty else {
            super.mouseDragged(with: event)
            return
        }

        let current = convert(event.locationInWindow, from: nil)
        let dx = abs(current.x - origin.x)
        let dy = abs(current.y - origin.y)
        guard dx > 3 || dy > 3 else { return }

        let item = NSDraggingItem(pasteboardWriter: NSPasteboardItem())
        if let writer = item.item as? NSPasteboardItem {
            writer.setString(addressBar.currentUrlForDrag, forType: .URL)
            writer.setString(addressBar.currentUrlForDrag, forType: .string)
        }
        item.setDraggingFrame(bounds, contents: snapshot())
        beginDraggingSession(with: [item], event: event, source: self)
        dragOrigin = nil
    }

    override func mouseUp(with event: NSEvent) {
        dragOrigin = nil
        super.mouseUp(with: event)
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

    override func menu(for event: NSEvent) -> NSMenu? {
        guard addressBarView?.currentState == .idle else { return super.menu(for: event) }
        guard let addressBarView else { return nil }
        return AddressBarContextMenu.menu(for: addressBarView.currentUrlForContextMenu, onEditAddress: { [weak addressBarView] in
            addressBarView?.enterEditingState()
        })
    }
}

private extension SidebarAddressBarView {
    var currentUrlForContextMenu: String {
        currentUrl
    }

    var currentUrlForDrag: String {
        currentUrl
    }
}
