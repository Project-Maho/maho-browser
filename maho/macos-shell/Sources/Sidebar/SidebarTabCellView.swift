import AppKit

// MARK: - Tab Cell View

class SidebarTabCellView: NSTableCellView, NSTextFieldDelegate {
    private let faviconView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let closeButton = NSButton()
    private let audioIcon = NSImageView()
    private let loadingIndicator = NSProgressIndicator()
    private let mutedIcon = NSImageView()
    private let stackView = NSStackView()
    private let lifecycleIndicator = NSTextField(labelWithString: "")
    private var trackingArea: NSTrackingArea?
    private var currentTabId: TabId?
    private var originalTitle: String = ""

    var onClose: (() -> Void)?
    var onTitleChanged: ((TabId, String) -> Void)?

    convenience init(identifier: NSUserInterfaceItemIdentifier) {
        self.init(frame: .zero)
        self.identifier = identifier
        setupViews()
    }

    private func setupViews() {
        wantsLayer = true

        // Keep the sidebar compact, but restore enough visual weight to stay legible.
        faviconView.translatesAutoresizingMaskIntoConstraints = false
        faviconView.imageScaling = .scaleProportionallyUpOrDown
        faviconView.setContentHuggingPriority(.required, for: .horizontal)
        NSLayoutConstraint.activate([
            faviconView.widthAnchor.constraint(equalToConstant: 16),
            faviconView.heightAnchor.constraint(equalToConstant: 16),
        ])

        titleLabel.font = NSFont.systemFont(ofSize: 12, weight: .regular)
        titleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.92)
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 1
        titleLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        titleLabel.setContentHuggingPriority(.fittingSizeCompression, for: .horizontal)

        // Close button — hidden by default
        closeButton.image = configuredSymbol(named: "xmark", pointSize: 10, accessibilityDescription: "Close tab")
        closeButton.bezelStyle = .inline
        closeButton.isBordered = false
        closeButton.target = self
        closeButton.action = #selector(closeAction)
        closeButton.isHidden = true
        closeButton.translatesAutoresizingMaskIntoConstraints = false
        closeButton.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.84)
        closeButton.setContentHuggingPriority(.required, for: .horizontal)
        NSLayoutConstraint.activate([
            closeButton.widthAnchor.constraint(equalToConstant: 16),
            closeButton.heightAnchor.constraint(equalToConstant: 16),
        ])

        // Audio indicator
        audioIcon.image = configuredSymbol(named: "speaker.wave.2.fill", pointSize: 10, accessibilityDescription: "Playing audio")
        audioIcon.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.84)
        audioIcon.translatesAutoresizingMaskIntoConstraints = false
        audioIcon.isHidden = true
        audioIcon.setContentHuggingPriority(.required, for: .horizontal)
        NSLayoutConstraint.activate([
            audioIcon.widthAnchor.constraint(equalToConstant: 12),
            audioIcon.heightAnchor.constraint(equalToConstant: 12),
        ])

        // Muted indicator
        mutedIcon.image = configuredSymbol(named: "speaker.slash.fill", pointSize: 10, accessibilityDescription: "Muted")
        mutedIcon.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.84)
        mutedIcon.translatesAutoresizingMaskIntoConstraints = false
        mutedIcon.isHidden = true
        mutedIcon.setContentHuggingPriority(.required, for: .horizontal)
        NSLayoutConstraint.activate([
            mutedIcon.widthAnchor.constraint(equalToConstant: 12),
            mutedIcon.heightAnchor.constraint(equalToConstant: 12),
        ])

        // Loading spinner
        loadingIndicator.style = .spinning
        loadingIndicator.controlSize = .small
        loadingIndicator.translatesAutoresizingMaskIntoConstraints = false
        loadingIndicator.isHidden = true
        loadingIndicator.setContentHuggingPriority(.required, for: .horizontal)
        NSLayoutConstraint.activate([
            loadingIndicator.widthAnchor.constraint(equalToConstant: 13),
            loadingIndicator.heightAnchor.constraint(equalToConstant: 13),
        ])

        lifecycleIndicator.font = .systemFont(ofSize: 10)
        lifecycleIndicator.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.60)
        lifecycleIndicator.isHidden = true
        lifecycleIndicator.setContentHuggingPriority(.required, for: .horizontal)
        lifecycleIndicator.setContentCompressionResistancePriority(.required, for: .horizontal)

        stackView.orientation = .horizontal
        stackView.alignment = .centerY
        stackView.spacing = 6
        stackView.edgeInsets = NSEdgeInsets(top: 0, left: SidebarMetrics.sidebarHorizontalInset, bottom: 0, right: SidebarMetrics.sidebarHorizontalInset)
        stackView.translatesAutoresizingMaskIntoConstraints = false

        stackView.addArrangedSubview(faviconView)
        stackView.addArrangedSubview(loadingIndicator)
        stackView.addArrangedSubview(titleLabel)
        stackView.addArrangedSubview(lifecycleIndicator)
        stackView.addArrangedSubview(audioIcon)
        stackView.addArrangedSubview(mutedIcon)
        stackView.addArrangedSubview(closeButton)

        addSubview(stackView)
        NSLayoutConstraint.activate([
            stackView.leadingAnchor.constraint(equalTo: leadingAnchor),
            stackView.trailingAnchor.constraint(equalTo: trailingAnchor),
            stackView.topAnchor.constraint(equalTo: topAnchor),
            stackView.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
    }

    func configure(with tab: TabNode) {
        let displayTitle: String
        if let customTitle = tab.customTitle, !customTitle.isEmpty {
            displayTitle = customTitle
        } else if tab.title.isEmpty {
            displayTitle = tab.url
        } else if tab.isPinned,
                  let settings = MahoBridge.shared.getSettings(),
                  settings.max.tidyTabTitles {
            displayTitle = Self.tidyTitle(tab.title)
        } else {
            displayTitle = tab.title
        }
        let accessibilityParts = [displayTitle, tab.url].filter { !$0.isEmpty }
        let accessibilityLabel = Array(NSOrderedSet(array: accessibilityParts)).compactMap { $0 as? String }.joined(separator: " ")

        titleLabel.stringValue = displayTitle
        currentTabId = tab.tabId
        originalTitle = displayTitle

        setAccessibilityElement(true)
        setAccessibilityIdentifier("sidebar.tab.\(tab.tabId)")
        setAccessibilityLabel(accessibilityLabel)

        faviconView.setAccessibilityElement(false)
        titleLabel.setAccessibilityElement(false)
        closeButton.setAccessibilityElement(false)
        audioIcon.setAccessibilityElement(false)
        mutedIcon.setAccessibilityElement(false)
        loadingIndicator.setAccessibilityElement(false)
        lifecycleIndicator.setAccessibilityElement(false)

        // Favicon
        if let faviconData = tab.favicon {
            let nsData = Data(faviconData.data)
            faviconView.image = NSImage(data: nsData)
            faviconView.contentTintColor = nil
            faviconView.isHidden = false
        } else {
            faviconView.image = configuredSymbol(named: "globe", pointSize: 13, accessibilityDescription: "Web page")
            faviconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.82)
            faviconView.isHidden = false
        }

        // Loading
        if tab.isLoading {
            loadingIndicator.isHidden = false
            loadingIndicator.startAnimation(nil)
            faviconView.isHidden = true
        } else {
            loadingIndicator.isHidden = true
            loadingIndicator.stopAnimation(nil)
        }

        // Audio state
        audioIcon.isHidden = !tab.isPlayingAudio || tab.isMuted
        mutedIcon.isHidden = !tab.isMuted

        // Close button stays hidden until hover
        closeButton.isHidden = true

        switch tab.lifecycleState {
        case "frozen":
            alphaValue = 0.55
            lifecycleIndicator.stringValue = "❄️"
            lifecycleIndicator.isHidden = false
        case "suspended":
            alphaValue = 0.40
            lifecycleIndicator.stringValue = "💤"
            lifecycleIndicator.isHidden = false
        default:
            alphaValue = 1.0
            lifecycleIndicator.isHidden = true
        }
    }

    @objc private func closeAction() {
        onClose?()
    }

    func applyLocalCustomTitle(_ title: String) {
        titleLabel.stringValue = title
        originalTitle = title
    }

    // MARK: - Inline Title Editing

    func beginTitleEditing() {
        titleLabel.isEditable = true
        titleLabel.isSelectable = true
        titleLabel.delegate = self
        window?.makeFirstResponder(titleLabel)
        titleLabel.selectText(nil)
    }

    private func endTitleEditing(save: Bool) {
        guard titleLabel.isEditable else { return }
        let newTitle = titleLabel.stringValue
        titleLabel.isEditable = false
        titleLabel.isSelectable = false
        titleLabel.delegate = nil
        window?.makeFirstResponder(nil)

        if save, newTitle != originalTitle, let tabId = currentTabId {
            originalTitle = newTitle
            onTitleChanged?(tabId, newTitle)
        } else {
            titleLabel.stringValue = originalTitle
        }
    }

    func controlTextDidEndEditing(_ obj: Notification) {
        endTitleEditing(save: true)
    }

    func control(_ control: NSControl, textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        if commandSelector == #selector(NSResponder.insertNewline(_:)) {
            endTitleEditing(save: true)
            return true
        }
        if commandSelector == #selector(NSResponder.cancelOperation(_:)) {
            endTitleEditing(save: false)
            return true
        }
        return false
    }

    // MARK: - Hover Tracking

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let existing = trackingArea {
            removeTrackingArea(existing)
        }
        trackingArea = NSTrackingArea(
            rect: bounds,
            options: [.mouseEnteredAndExited, .activeInActiveApp, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        if let area = trackingArea {
            addTrackingArea(area)
        }
    }

    override func mouseEntered(with event: NSEvent) {
        closeButton.isHidden = false
    }

    override func mouseExited(with event: NSEvent) {
        closeButton.isHidden = true
    }

    private static func tidyTitle(_ title: String) -> String {
        let separators = [" - ", " | ", " — ", " – ", " · "]
        for sep in separators {
            if let range = title.range(of: sep, options: .backwards) {
                return String(title[..<range.lowerBound])
            }
        }
        if title.count > 30 {
            return String(title.prefix(27)) + "..."
        }
        return title
    }

    func configureForLiveItem(_ item: LiveFolderItem) {
        titleLabel.stringValue = item.title
        setAccessibilityElement(true)
        setAccessibilityLabel(item.title)

        if let iconName = item.icon {
            faviconView.image = configuredSymbol(named: iconName, pointSize: 13, accessibilityDescription: nil)
        } else {
            faviconView.image = configuredSymbol(named: "globe", pointSize: 13, accessibilityDescription: nil)
        }
        faviconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.82)
        faviconView.isHidden = false
        loadingIndicator.isHidden = true
        loadingIndicator.stopAnimation(nil)
        audioIcon.isHidden = true
        mutedIcon.isHidden = true
        closeButton.isHidden = true
        lifecycleIndicator.isHidden = true
        alphaValue = 1.0
    }

    private func configuredSymbol(named symbolName: String, pointSize: CGFloat, accessibilityDescription: String?) -> NSImage? {
        return MahoIcon.imageForSFSymbol(symbolName, pointSize: pointSize, accessibilityDescription: accessibilityDescription)
    }
}
