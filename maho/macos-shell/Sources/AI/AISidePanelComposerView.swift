import AppKit
import CoreText

protocol AISidePanelComposerDelegate: AnyObject {
    func composerDidRequestEntryPointMenu(from button: NSButton)
    func composerDidRequestTidyTabs(from button: NSButton)
    func composerDidRequestCompareTabs(from button: NSButton)
    func composerDidTogglePageContext(enabled: Bool)
    func composerTextDidChange(_ text: String)
    func composerDidRequestMoveUp() -> Bool
    func composerDidRequestMoveDown() -> Bool
    func composerDidRequestConfirmSelection()
    func composerDidRequestDismiss()
    func composerDidRequestStop()
    func composerDidRequestCamera()
    func composerDidRequestMicrophone()
}

private final class ComposerFocusForwardingView: NSView {
    weak var composerView: AISidePanelComposerView?

    override func mouseDown(with event: NSEvent) {
        composerView?.focusComposerInputFromChromeClick()
    }
}

private final class ComposerFocusForwardingLabel: NSTextField {
    weak var composerView: AISidePanelComposerView?

    override func mouseDown(with event: NSEvent) {
        composerView?.focusComposerInputFromChromeClick()
    }
}

private final class ComposerFocusForwardingClipView: NSClipView {
    weak var composerView: AISidePanelComposerView?

    override func mouseDown(with event: NSEvent) {
        composerView?.focusComposerInputFromChromeClick()
        super.mouseDown(with: event)
    }
}

final class AISidePanelComposerView: NSView, NSTextViewDelegate {
    private var isStreaming = false

    weak var delegate: AISidePanelComposerDelegate?

    private let composerMaterialView = NSVisualEffectView()
    private let composerShell = NSView()
    private let composerHeadRow = NSView()
    private let contextPillView = NSView()
    private let contextIconView = NSView()
    private let contextIconImageView = NSImageView()
    private let contextTextStack = NSStackView()
    private let contextTitleLabel = NSTextField(labelWithString: "This page")
    private let contextURLLabel = NSTextField(labelWithString: "Current tab context")
    private let contextRemoveButton = NSButton(title: "", target: nil, action: nil)
    private let inputPillView = ComposerFocusForwardingView()
    private let inputScrollView = NSScrollView()
    private let inputClipView = ComposerFocusForwardingClipView()
    private let textView = NSTextView()
    private let placeholderLabel = ComposerFocusForwardingLabel(labelWithString: "Ask a question about this page...")
    private let stopButton = NSButton(title: "Stop", target: nil, action: nil)
    private let footerRow = NSView()
    private let leadingControlsStack = NSStackView()
    private let trailingControlsStack = NSStackView()
    private let entryPointButton = NSButton(title: "", target: nil, action: nil)
    private let moreButton = NSButton(title: "", target: nil, action: nil)
    private let cameraButton = NSButton(title: "", target: nil, action: nil)
    private let microphoneButton = NSButton(title: "", target: nil, action: nil)
    private let sendButton = NSButton(title: "", target: nil, action: nil)
    private var providerQuickChoiceButtons: [NSButton] = []
    private var hasAttachedPageContext = false
    private var pillHeightConstraint: NSLayoutConstraint!
    private var composerHeadMinimumHeightConstraint: NSLayoutConstraint!
    private var inputTopToHeadConstraint: NSLayoutConstraint!
    private var inputTopToShellConstraint: NSLayoutConstraint!

    var inputText: String {
        get { textView.string }
        set {
            textView.string = newValue
            updatePlaceholderVisibility()
            updatePillHeight()
        }
    }

    private(set) var isShowingOnboarding = false
    private(set) var isPageContextEnabled = false

    func setOnboardingVisible(_ visible: Bool) {
        isShowingOnboarding = visible
        entryPointButton.isHidden = visible
        moreButton.isHidden = visible
        updateContextVisibility()
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    func makeInputFirstResponder() {
        window?.makeFirstResponder(textView)
    }

    func moveInputCursorToEnd() {
        let length = (textView.string as NSString).length
        textView.setSelectedRange(NSRange(location: length, length: 0))
    }

    fileprivate func focusComposerInputFromChromeClick() {
        guard !isShowingOnboarding, textView.isEditable else { return }
        if window?.firstResponder !== textView {
            makeInputFirstResponder()
            moveInputCursorToEnd()
        }
    }

    func updateComposerChrome(isStreaming: Bool) {
        self.isStreaming = isStreaming
        if isStreaming {
            composerShell.layer?.borderColor = AIDiaPalette.accentBorder.cgColor
            composerShell.layer?.backgroundColor = AIDiaPalette.composerStreamingBackground.cgColor
            inputPillView.layer?.backgroundColor = NSColor.clear.cgColor
            inputPillView.layer?.borderColor = NSColor.clear.cgColor
        } else {
            composerShell.layer?.borderColor = AIDiaPalette.composerBorder.cgColor
            composerShell.layer?.backgroundColor = AIDiaPalette.composerBackground.cgColor
            inputPillView.layer?.backgroundColor = NSColor.clear.cgColor
            inputPillView.layer?.borderColor = NSColor.clear.cgColor
        }
    }

    func setEnabled(_ enabled: Bool) {
        textView.isEditable = enabled
        textView.isSelectable = enabled
    }

    func setStreamingState() {
        textView.isEditable = false
        placeholderLabel.stringValue = "Maho AI is responding…"
        updatePlaceholderVisibility()
        stopButton.isHidden = false
        updateComposerChrome(isStreaming: true)
    }

    func setIdleState() {
        textView.isEditable = true
        placeholderLabel.stringValue = "Ask a question about this page..."
        updatePlaceholderVisibility()
        stopButton.isHidden = true
        updateComposerChrome(isStreaming: false)
    }

    func updateTidyTabsActionState(canTidy: Bool) {}

    func updateCompareTabsActionState(canCompare: Bool) {}

    func setPageContextEnabled(_ enabled: Bool) {
        isPageContextEnabled = enabled
        if !enabled {
            clearPageContextDisplay()
        } else {
            updateContextVisibility()
        }
    }

    func setComposerHidden(_ hidden: Bool) {
        composerShell.isHidden = hidden
        entryPointButton.isHidden = hidden || isShowingOnboarding
        moreButton.isHidden = hidden || isShowingOnboarding
    }

    func resetPageContext() {
        setPageContextEnabled(false)
    }

    func updatePageContext(title: String, url: String, icon: ImageData?) {
        let trimmedTitle = title.trimmingCharacters(in: .whitespacesAndNewlines)
        let trimmedURL = url.trimmingCharacters(in: .whitespacesAndNewlines)
        contextTitleLabel.stringValue = trimmedTitle.isEmpty ? "This page" : trimmedTitle
        if trimmedURL.isEmpty, let host = URL(string: url)?.host {
            contextURLLabel.stringValue = host
        } else {
            contextURLLabel.stringValue = trimmedURL.isEmpty ? "Current tab context" : trimmedURL
        }
        contextIconImageView.image = Self.contextIconImage(from: icon)
        hasAttachedPageContext = true
        applyTextStyling()
        updateContextVisibility()
    }

    func clearPageContextDisplay() {
        contextTitleLabel.stringValue = "This page"
        contextURLLabel.stringValue = "Current tab context"
        contextIconImageView.image = Self.contextIconImage(from: nil)
        hasAttachedPageContext = false
        applyTextStyling()
        updateContextVisibility()
    }

    func updateProviderButtonSelection(selectedProvider: LLMProviderType) {
        for button in providerQuickChoiceButtons {
            button.state = button.title == selectedProvider.rawValue ? .on : .off
        }
    }

    func textDidChange(_ notification: Notification) {
        guard !isShowingOnboarding else { return }
        updatePlaceholderVisibility()
        updatePillHeight()
        delegate?.composerTextDidChange(textView.string)
    }

    func textView(_ tv: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        guard !isShowingOnboarding else { return false }
        if commandSelector == #selector(NSResponder.insertNewline(_:)) {
            if let event = NSApp.currentEvent, event.modifierFlags.contains(.shift) {
                tv.insertNewlineIgnoringFieldEditor(nil)
                return true
            }
            delegate?.composerDidRequestConfirmSelection()
            return true
        }
        if commandSelector == #selector(NSResponder.moveUp(_:)) {
            return delegate?.composerDidRequestMoveUp() ?? false
        }
        if commandSelector == #selector(NSResponder.moveDown(_:)) {
            return delegate?.composerDidRequestMoveDown() ?? false
        }
        if commandSelector == #selector(NSResponder.cancelOperation(_:)) {
            delegate?.composerDidRequestDismiss()
            return true
        }
        return false
    }

    private func applyTheme() {
        composerMaterialView.material = .hudWindow
        composerMaterialView.blendingMode = .withinWindow
        composerMaterialView.state = .active
        composerMaterialView.wantsLayer = true
        composerMaterialView.layer?.cornerRadius = 28
        composerMaterialView.layer?.masksToBounds = true

        composerShell.configureSurface(
            background: AIDiaPalette.composerBackground.withAlphaComponent(0.80),
            border: AIDiaPalette.composerBorder.withAlphaComponent(0.80),
            radius: 28,
            borderWidth: 1,
            shadowOpacity: 0.10,
            shadowRadius: 12,
            shadowOffset: CGSize(width: 0, height: -5)
        )
        composerShell.aiEnsureGradientLayer(
            named: "ai.composer.surface",
            colors: [AIDiaPalette.composerSheen, AIDiaPalette.composerBottomBackground],
            startPoint: CGPoint(x: 0.5, y: 1),
            endPoint: CGPoint(x: 0.5, y: 0),
            locations: [0, 1],
            opacity: 1,
            cornerRadius: 28
        )
        composerShell.aiEnsureRadialGlowLayer(
            named: "ai.composer.glow",
            color: NSColor.white.withAlphaComponent(0.025),
            center: CGPoint(x: 0.5, y: 1.0),
            radius: max(bounds.width * 0.42, 1),
            opacity: 1,
            cornerRadius: 28
        )
        composerHeadRow.layer?.backgroundColor = NSColor.clear.cgColor
        contextPillView.configureSurface(
            background: AIDiaPalette.contextPillBackground,
            border: AIDiaPalette.contextPillBorder,
            radius: 20,
            borderWidth: 1
        )
        contextPillView.aiEnsureGradientLayer(
            named: "ai.context.surface",
            colors: [AIDiaPalette.contextPillSheen, AIDiaPalette.contextPillBackground.withAlphaComponent(0.92)],
            startPoint: CGPoint(x: 0.5, y: 1),
            endPoint: CGPoint(x: 0.5, y: 0),
            locations: [0, 1],
            opacity: 1,
            cornerRadius: 20
        )
        contextIconView.configureSurface(
            background: AIDiaPalette.contextIconBackground,
            border: AIDiaPalette.contextIconBackground,
            radius: 10,
            borderWidth: 0
        )
        contextIconImageView.contentTintColor = AIDiaPalette.contextIconTint
        contextTitleLabel.textColor = AIDiaPalette.titleText
        contextURLLabel.textColor = AIDiaPalette.secondaryText
        applyTextStyling()

        textView.textColor = AIDiaPalette.composerInputText
        textView.insertionPointColor = AIDiaPalette.titleText
        placeholderLabel.textColor = AIDiaPalette.composerPlaceholderText

        let footerTint = AIDiaPalette.color(205, 208, 214, alpha: 0.62)
        styleGlyphButton(entryPointButton, tint: footerTint)
        styleGlyphButton(moreButton, tint: footerTint)
        styleGlyphButton(cameraButton, tint: footerTint)
        styleGlyphButton(microphoneButton, tint: footerTint)

        sendButton.wantsLayer = true
        sendButton.layer?.backgroundColor = AIDiaPalette.sendButtonBackground.cgColor
        sendButton.layer?.borderColor = AIDiaPalette.sendButtonBackground.cgColor
        sendButton.layer?.borderWidth = 0
        sendButton.layer?.cornerRadius = 24
        sendButton.layer?.shadowColor = AIDiaPalette.shadow.cgColor
        sendButton.layer?.shadowOpacity = 0.28
        sendButton.layer?.shadowRadius = 24
        sendButton.layer?.shadowOffset = CGSize(width: 0, height: -10)
        sendButton.contentTintColor = AIDiaPalette.sendIconTint

        stopButton.contentTintColor = AIDiaPalette.bodyText
        stopButton.styleChromeButton(
            background: AIDiaPalette.composerControlBackground,
            border: AIDiaPalette.accentBorder,
            radius: 16,
            borderWidth: 1
        )
        styleContextRemoveButton()
        updateComposerChrome(isStreaming: isStreaming)
    }

    private func applyTextStyling() {
        contextTitleLabel.font = .systemFont(ofSize: 16, weight: NSFont.Weight(rawValue: 0.34))
        contextURLLabel.font = .systemFont(ofSize: 14, weight: NSFont.Weight(rawValue: 0.10))
        placeholderLabel.font = .systemFont(ofSize: 16, weight: NSFont.Weight(rawValue: 0.06))
        textView.font = .systemFont(ofSize: 16, weight: NSFont.Weight(rawValue: 0.10))

        let contextTitleStyle = NSMutableParagraphStyle()
        contextTitleStyle.lineBreakMode = .byTruncatingTail
        let contextURLStyle = NSMutableParagraphStyle()
        contextURLStyle.lineBreakMode = .byTruncatingTail
        let placeholderStyle = NSMutableParagraphStyle()
        placeholderStyle.lineBreakMode = .byWordWrapping

        contextTitleLabel.attributedStringValue = NSAttributedString(
            string: contextTitleLabel.stringValue,
            attributes: [
                .font: contextTitleLabel.font as Any,
                .foregroundColor: contextTitleLabel.textColor as Any,
                .kern: -0.48,
                .paragraphStyle: contextTitleStyle,
            ]
        )
        contextURLLabel.attributedStringValue = NSAttributedString(
            string: contextURLLabel.stringValue,
            attributes: [
                .font: contextURLLabel.font as Any,
                .foregroundColor: contextURLLabel.textColor as Any,
                .kern: -0.28,
                .paragraphStyle: contextURLStyle,
            ]
        )
        placeholderLabel.attributedStringValue = NSAttributedString(
            string: placeholderLabel.stringValue,
            attributes: [
                .font: placeholderLabel.font as Any,
                .foregroundColor: placeholderLabel.textColor as Any,
                .kern: -0.45,
                .paragraphStyle: placeholderStyle,
            ]
        )
    }

    private func styleGlyphButton(_ button: NSButton, tint: NSColor) {
        button.wantsLayer = true
        button.layer?.backgroundColor = NSColor.clear.cgColor
        button.layer?.borderWidth = 0
        button.layer?.borderColor = NSColor.clear.cgColor
        button.layer?.cornerRadius = 12
        button.layer?.shadowOpacity = 0
        button.contentTintColor = tint
    }

    private func styleContextRemoveButton() {
        contextRemoveButton.wantsLayer = true
        contextRemoveButton.layer?.backgroundColor = AIDiaPalette.contextRemoveBackground.cgColor
        contextRemoveButton.layer?.borderColor = NSColor.clear.cgColor
        contextRemoveButton.layer?.borderWidth = 0
        contextRemoveButton.layer?.cornerRadius = 15
        contextRemoveButton.layer?.shadowColor = AIDiaPalette.shadow.cgColor
        contextRemoveButton.layer?.shadowOpacity = 0.18
        contextRemoveButton.layer?.shadowRadius = 10
        contextRemoveButton.layer?.shadowOffset = CGSize(width: 0, height: -4)
        contextRemoveButton.contentTintColor = AIDiaPalette.titleText
    }

    private func updateContextVisibility() {
        let showsContext = !isShowingOnboarding && isPageContextEnabled && hasAttachedPageContext
        contextPillView.isHidden = !showsContext
        contextRemoveButton.isHidden = !showsContext
        composerHeadRow.isHidden = !showsContext
        composerHeadMinimumHeightConstraint.constant = showsContext ? 38 : 0
        inputTopToHeadConstraint.isActive = showsContext
        inputTopToShellConstraint.isActive = !showsContext
    }

    private func updatePlaceholderVisibility() {
        placeholderLabel.isHidden = !textView.string.isEmpty
        applyTextStyling()
    }

    private func updatePillHeight() {
        guard let layoutManager = textView.layoutManager,
              let textContainer = textView.textContainer else { return }
        layoutManager.ensureLayout(for: textContainer)
        let textHeight = layoutManager.usedRect(for: textContainer).height
        let verticalPadding: CGFloat = 14
        let desiredHeight = max(62, min(142, textHeight + verticalPadding))
        if pillHeightConstraint.constant != desiredHeight {
            pillHeightConstraint.constant = desiredHeight
            invalidateIntrinsicContentSize()
            superview?.needsLayout = true
        }
    }

    private func setupUI() {
        translatesAutoresizingMaskIntoConstraints = false

        composerMaterialView.translatesAutoresizingMaskIntoConstraints = false
        composerMaterialView.wantsLayer = true
        composerMaterialView.material = .hudWindow
        composerMaterialView.blendingMode = .withinWindow
        composerMaterialView.state = .active
        addSubview(composerMaterialView)

        composerShell.translatesAutoresizingMaskIntoConstraints = false
        composerShell.setAccessibilityIdentifier("ai.panel.composer")
        composerShell.setContentHuggingPriority(.required, for: .vertical)
        composerShell.setContentCompressionResistancePriority(.required, for: .vertical)
        addSubview(composerShell)

        composerHeadRow.translatesAutoresizingMaskIntoConstraints = false
        composerHeadRow.wantsLayer = true
        composerShell.addSubview(composerHeadRow)

        contextPillView.translatesAutoresizingMaskIntoConstraints = false
        composerHeadRow.addSubview(contextPillView)

        contextIconView.translatesAutoresizingMaskIntoConstraints = false
        contextPillView.addSubview(contextIconView)

        contextIconImageView.translatesAutoresizingMaskIntoConstraints = false
        contextIconImageView.image = Self.contextIconImage(from: nil)
        contextIconImageView.imageScaling = .scaleProportionallyDown
        contextIconView.addSubview(contextIconImageView)

        contextTextStack.translatesAutoresizingMaskIntoConstraints = false
        contextTextStack.orientation = .vertical
        contextTextStack.alignment = .leading
        contextTextStack.spacing = 2
        contextPillView.addSubview(contextTextStack)

        contextTitleLabel.translatesAutoresizingMaskIntoConstraints = false
        contextTitleLabel.font = .systemFont(ofSize: 16, weight: .semibold)
        contextTitleLabel.lineBreakMode = .byTruncatingTail
        contextTitleLabel.maximumNumberOfLines = 1
        contextTextStack.addArrangedSubview(contextTitleLabel)

        contextURLLabel.translatesAutoresizingMaskIntoConstraints = false
        contextURLLabel.font = .systemFont(ofSize: 14, weight: .medium)
        contextURLLabel.lineBreakMode = .byTruncatingTail
        contextURLLabel.maximumNumberOfLines = 1
        contextTextStack.addArrangedSubview(contextURLLabel)

        contextRemoveButton.translatesAutoresizingMaskIntoConstraints = false
        contextRemoveButton.setAccessibilityLabel("Remove page context")
        contextRemoveButton.title = ""
        contextRemoveButton.image = NSImage(systemSymbolName: "xmark", accessibilityDescription: "Remove page context")?.withSymbolConfiguration(NSImage.SymbolConfiguration(pointSize: 11, weight: .bold))
        contextRemoveButton.imagePosition = .imageOnly
        contextRemoveButton.imageScaling = .scaleProportionallyDown
        contextRemoveButton.isBordered = false
        contextRemoveButton.target = self
        contextRemoveButton.action = #selector(clearContextTapped)
        composerHeadRow.addSubview(contextRemoveButton)

        inputPillView.configureSurface(
            background: NSColor.clear,
            border: .clear,
            radius: 20,
            borderWidth: 0
        )
        inputPillView.translatesAutoresizingMaskIntoConstraints = false
        inputPillView.composerView = self
        composerShell.addSubview(inputPillView)

        inputScrollView.translatesAutoresizingMaskIntoConstraints = false
        inputScrollView.hasVerticalScroller = true
        inputScrollView.hasHorizontalScroller = false
        inputScrollView.autohidesScrollers = true
        inputScrollView.borderType = .noBorder
        inputScrollView.drawsBackground = false
        inputClipView.drawsBackground = false
        inputClipView.backgroundColor = .clear
        inputClipView.composerView = self
        inputScrollView.contentView = inputClipView
        inputPillView.addSubview(inputScrollView)

        textView.setAccessibilityIdentifier("ai.panel.composer.input")
        textView.font = .systemFont(ofSize: 16, weight: .medium)
        textView.textColor = AIDiaPalette.composerPlaceholderText
        textView.backgroundColor = .clear
        textView.drawsBackground = false
        textView.isRichText = false
        textView.allowsUndo = true
        textView.isVerticallyResizable = true
        textView.isHorizontallyResizable = false
        textView.textContainer?.widthTracksTextView = true
        textView.textContainer?.lineFragmentPadding = 0
        textView.textContainerInset = NSSize(width: 0, height: 0)
        textView.focusRingType = .none
        textView.isAutomaticQuoteSubstitutionEnabled = false
        textView.isAutomaticDashSubstitutionEnabled = false
        textView.isAutomaticTextReplacementEnabled = false
        textView.delegate = self
        inputScrollView.documentView = textView

        placeholderLabel.translatesAutoresizingMaskIntoConstraints = false
        placeholderLabel.font = .systemFont(ofSize: 16, weight: NSFont.Weight(rawValue: 0.06))
        placeholderLabel.textColor = AIDiaPalette.composerPlaceholderText
        placeholderLabel.backgroundColor = .clear
        placeholderLabel.drawsBackground = false
        placeholderLabel.isBordered = false
        placeholderLabel.isEditable = false
        placeholderLabel.isSelectable = false
        placeholderLabel.maximumNumberOfLines = 0
        placeholderLabel.lineBreakMode = .byWordWrapping
        placeholderLabel.composerView = self
        inputPillView.addSubview(placeholderLabel)

        stopButton.translatesAutoresizingMaskIntoConstraints = false
        stopButton.setAccessibilityIdentifier("ai.panel.composer.stop")
        stopButton.title = "Stop"
        stopButton.image = NSImage(systemSymbolName: "stop.fill", accessibilityDescription: "Stop")
        stopButton.imagePosition = .imageLeading
        stopButton.imageScaling = .scaleProportionallyDown
        stopButton.font = .systemFont(ofSize: 11, weight: .semibold)
        stopButton.isBordered = false
        stopButton.setButtonType(.momentaryPushIn)
        stopButton.setContentHuggingPriority(.required, for: .horizontal)
        stopButton.setContentCompressionResistancePriority(.required, for: .horizontal)
        stopButton.target = self
        stopButton.action = #selector(stopTapped)
        stopButton.isHidden = true
        inputPillView.addSubview(stopButton)

        footerRow.translatesAutoresizingMaskIntoConstraints = false
        footerRow.wantsLayer = true
        composerShell.addSubview(footerRow)

        leadingControlsStack.translatesAutoresizingMaskIntoConstraints = false
        leadingControlsStack.orientation = .horizontal
        leadingControlsStack.alignment = .centerY
        leadingControlsStack.spacing = 18
        footerRow.addSubview(leadingControlsStack)

        trailingControlsStack.translatesAutoresizingMaskIntoConstraints = false
        trailingControlsStack.orientation = .horizontal
        trailingControlsStack.alignment = .centerY
        trailingControlsStack.spacing = 18
        footerRow.addSubview(trailingControlsStack)

        configureFooterButton(
            entryPointButton,
            accessibilityIdentifier: "ai.panel.composer.entryPoint",
            accessibilityLabel: "Add",
            symbolName: "plus",
            pointSize: 16,
            action: #selector(entryPointTapped)
        )
        configureFooterButton(
            moreButton,
            accessibilityIdentifier: "ai.panel.composer.more",
            accessibilityLabel: "More options",
            symbolName: "ellipsis",
            pointSize: 15,
            action: #selector(entryPointTapped)
        )
        configureFooterButton(
            cameraButton,
            accessibilityIdentifier: "ai.panel.composer.camera",
            accessibilityLabel: "Camera",
            symbolName: "video",
            pointSize: 15,
            action: #selector(cameraTapped)
        )
        configureFooterButton(
            microphoneButton,
            accessibilityIdentifier: "ai.panel.composer.microphone",
            accessibilityLabel: "Microphone",
            symbolName: "mic",
            pointSize: 15,
            action: #selector(microphoneTapped)
        )

        sendButton.translatesAutoresizingMaskIntoConstraints = false
        sendButton.setAccessibilityIdentifier("ai.panel.composer.send")
        sendButton.setAccessibilityLabel("Send")
        sendButton.title = ""
        sendButton.image = NSImage(systemSymbolName: "arrow.right", accessibilityDescription: "Send")?.withSymbolConfiguration(NSImage.SymbolConfiguration(pointSize: 18, weight: .bold))
        sendButton.imagePosition = .imageOnly
        sendButton.imageScaling = .scaleProportionallyDown
        sendButton.isBordered = false
        sendButton.setButtonType(.momentaryPushIn)
        sendButton.setContentHuggingPriority(.required, for: .horizontal)
        sendButton.setContentCompressionResistancePriority(.required, for: .horizontal)
        sendButton.target = self
        sendButton.action = #selector(sendTapped)

        leadingControlsStack.addArrangedSubview(entryPointButton)
        leadingControlsStack.addArrangedSubview(moreButton)
        trailingControlsStack.addArrangedSubview(cameraButton)
        trailingControlsStack.addArrangedSubview(microphoneButton)
        trailingControlsStack.addArrangedSubview(sendButton)

        pillHeightConstraint = inputPillView.heightAnchor.constraint(equalToConstant: 132)
        pillHeightConstraint.priority = .defaultHigh
        composerHeadMinimumHeightConstraint = composerHeadRow.heightAnchor.constraint(greaterThanOrEqualToConstant: 38)
        inputTopToHeadConstraint = inputPillView.topAnchor.constraint(equalTo: composerHeadRow.bottomAnchor, constant: 10)
        inputTopToShellConstraint = inputPillView.topAnchor.constraint(equalTo: composerShell.topAnchor, constant: 28)

        NSLayoutConstraint.activate([
            composerMaterialView.topAnchor.constraint(equalTo: topAnchor),
            composerMaterialView.leadingAnchor.constraint(equalTo: leadingAnchor),
            composerMaterialView.trailingAnchor.constraint(equalTo: trailingAnchor),
            composerMaterialView.bottomAnchor.constraint(equalTo: bottomAnchor),

            composerShell.topAnchor.constraint(equalTo: topAnchor),
            composerShell.leadingAnchor.constraint(equalTo: leadingAnchor),
            composerShell.trailingAnchor.constraint(equalTo: trailingAnchor),
            composerShell.bottomAnchor.constraint(equalTo: bottomAnchor),
            composerShell.heightAnchor.constraint(greaterThanOrEqualToConstant: 286),

            composerHeadRow.topAnchor.constraint(equalTo: composerShell.topAnchor, constant: 28),
            composerHeadRow.leadingAnchor.constraint(equalTo: composerShell.leadingAnchor, constant: 22),
            composerHeadRow.trailingAnchor.constraint(equalTo: composerShell.trailingAnchor, constant: -22),
            composerHeadMinimumHeightConstraint,

            contextPillView.topAnchor.constraint(equalTo: composerHeadRow.topAnchor),
            contextPillView.leadingAnchor.constraint(equalTo: composerHeadRow.leadingAnchor),
            contextPillView.bottomAnchor.constraint(equalTo: composerHeadRow.bottomAnchor),
            contextPillView.widthAnchor.constraint(lessThanOrEqualToConstant: 256),

            contextIconView.leadingAnchor.constraint(equalTo: contextPillView.leadingAnchor, constant: 16),
            contextIconView.centerYAnchor.constraint(equalTo: contextPillView.centerYAnchor),
            contextIconView.widthAnchor.constraint(equalToConstant: 34),
            contextIconView.heightAnchor.constraint(equalToConstant: 34),

            contextIconImageView.centerXAnchor.constraint(equalTo: contextIconView.centerXAnchor),
            contextIconImageView.centerYAnchor.constraint(equalTo: contextIconView.centerYAnchor),
            contextIconImageView.widthAnchor.constraint(equalToConstant: 16),
            contextIconImageView.heightAnchor.constraint(equalToConstant: 16),

            contextTextStack.leadingAnchor.constraint(equalTo: contextIconView.trailingAnchor, constant: 11),
            contextTextStack.topAnchor.constraint(equalTo: contextPillView.topAnchor, constant: 14),
            contextTextStack.trailingAnchor.constraint(equalTo: contextPillView.trailingAnchor, constant: -16),
            contextTextStack.bottomAnchor.constraint(equalTo: contextPillView.bottomAnchor, constant: -14),

            contextRemoveButton.leadingAnchor.constraint(equalTo: contextPillView.trailingAnchor, constant: -6),
            contextRemoveButton.topAnchor.constraint(equalTo: composerHeadRow.topAnchor, constant: -10),
            contextRemoveButton.widthAnchor.constraint(equalToConstant: 30),
            contextRemoveButton.heightAnchor.constraint(equalToConstant: 30),
            contextRemoveButton.trailingAnchor.constraint(lessThanOrEqualTo: composerHeadRow.trailingAnchor),

            inputTopToHeadConstraint,
            inputPillView.leadingAnchor.constraint(equalTo: composerShell.leadingAnchor, constant: 22),
            inputPillView.trailingAnchor.constraint(equalTo: composerShell.trailingAnchor, constant: -22),
            pillHeightConstraint,
            inputPillView.heightAnchor.constraint(greaterThanOrEqualToConstant: 128),

            footerRow.topAnchor.constraint(equalTo: inputPillView.bottomAnchor, constant: 18),
            footerRow.leadingAnchor.constraint(equalTo: composerShell.leadingAnchor, constant: 22),
            footerRow.trailingAnchor.constraint(equalTo: composerShell.trailingAnchor, constant: -22),
            footerRow.bottomAnchor.constraint(equalTo: composerShell.bottomAnchor, constant: -18),
            footerRow.heightAnchor.constraint(equalToConstant: 48),

            leadingControlsStack.leadingAnchor.constraint(equalTo: footerRow.leadingAnchor),
            leadingControlsStack.centerYAnchor.constraint(equalTo: footerRow.centerYAnchor),

            trailingControlsStack.leadingAnchor.constraint(greaterThanOrEqualTo: leadingControlsStack.trailingAnchor, constant: 16),
            trailingControlsStack.trailingAnchor.constraint(equalTo: footerRow.trailingAnchor),
            trailingControlsStack.centerYAnchor.constraint(equalTo: footerRow.centerYAnchor),

            entryPointButton.widthAnchor.constraint(equalToConstant: 24),
            entryPointButton.heightAnchor.constraint(equalToConstant: 24),
            moreButton.widthAnchor.constraint(equalToConstant: 24),
            moreButton.heightAnchor.constraint(equalToConstant: 24),
            cameraButton.widthAnchor.constraint(equalToConstant: 24),
            cameraButton.heightAnchor.constraint(equalToConstant: 24),
            microphoneButton.widthAnchor.constraint(equalToConstant: 24),
            microphoneButton.heightAnchor.constraint(equalToConstant: 24),
            sendButton.widthAnchor.constraint(equalToConstant: 48),
            sendButton.heightAnchor.constraint(equalToConstant: 48),

            stopButton.trailingAnchor.constraint(equalTo: inputPillView.trailingAnchor, constant: -4),
            stopButton.bottomAnchor.constraint(equalTo: inputPillView.bottomAnchor, constant: -6),
            stopButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 56),
            stopButton.heightAnchor.constraint(equalToConstant: 28),

            inputScrollView.leadingAnchor.constraint(equalTo: inputPillView.leadingAnchor),
            inputScrollView.trailingAnchor.constraint(equalTo: inputPillView.trailingAnchor),
            inputScrollView.topAnchor.constraint(equalTo: inputPillView.topAnchor, constant: 10),
            inputScrollView.bottomAnchor.constraint(equalTo: inputPillView.bottomAnchor, constant: -8),
            inputScrollView.widthAnchor.constraint(greaterThanOrEqualToConstant: 92),

            placeholderLabel.leadingAnchor.constraint(equalTo: inputScrollView.leadingAnchor),
            placeholderLabel.topAnchor.constraint(equalTo: inputScrollView.topAnchor, constant: 10),
            placeholderLabel.widthAnchor.constraint(lessThanOrEqualToConstant: 340),
            placeholderLabel.trailingAnchor.constraint(lessThanOrEqualTo: inputScrollView.trailingAnchor, constant: -8),
        ])

        applyTheme()
        setPageContextEnabled(false)
        updateComposerChrome(isStreaming: false)
        updateContextVisibility()
    }

    private static func contextIconImage(from imageData: ImageData?) -> NSImage? {
        if let imageData,
           let image = NSImage(data: Data(imageData.data)) {
            return image
        }

        let image = NSImage(size: NSSize(width: 18, height: 18))
        image.lockFocus()
        defer { image.unlockFocus() }

        guard let context = NSGraphicsContext.current?.cgContext else { return image }
        context.setStrokeColor(AIDiaPalette.contextIconTint.cgColor)
        context.setLineWidth(1.65)
        context.setLineCap(.round)
        context.setLineJoin(.round)

        let outer = NSBezierPath(roundedRect: NSRect(x: 2.5, y: 2.5, width: 13, height: 13), xRadius: 3.6, yRadius: 3.6)
        outer.lineWidth = 1.65
        outer.stroke()

        let curve = NSBezierPath()
        curve.move(to: NSPoint(x: 6, y: 6))
        curve.curve(to: NSPoint(x: 13, y: 9), controlPoint1: NSPoint(x: 7.2, y: 8.1), controlPoint2: NSPoint(x: 10.5, y: 9.8))
        curve.lineWidth = 1.65
        curve.lineCapStyle = .round
        curve.stroke()

        let dot = NSBezierPath(ovalIn: NSRect(x: 5.6, y: 10.8, width: 1.8, height: 1.8))
        dot.fill()
        return image
    }

    private func configureFooterButton(
        _ button: NSButton,
        accessibilityIdentifier: String,
        accessibilityLabel: String,
        symbolName: String,
        pointSize: CGFloat,
        action: Selector
    ) {
        button.translatesAutoresizingMaskIntoConstraints = false
        button.setAccessibilityIdentifier(accessibilityIdentifier)
        button.setAccessibilityLabel(accessibilityLabel)
        button.title = ""
        button.image = NSImage(systemSymbolName: symbolName, accessibilityDescription: accessibilityLabel)?.withSymbolConfiguration(NSImage.SymbolConfiguration(pointSize: pointSize, weight: .medium))
        button.imagePosition = .imageOnly
        button.imageScaling = .scaleProportionallyDown
        button.isBordered = false
        button.setButtonType(.momentaryPushIn)
        button.target = self
        button.action = action
        styleGlyphButton(button, tint: AIDiaPalette.secondaryText)
    }

    @objc private func clearContextTapped() {
        setPageContextEnabled(false)
        delegate?.composerDidTogglePageContext(enabled: false)
    }

    @objc private func entryPointTapped() {
        delegate?.composerDidRequestEntryPointMenu(from: entryPointButton)
    }

    @objc private func stopTapped() {
        delegate?.composerDidRequestStop()
    }

    @objc private func cameraTapped() {
        delegate?.composerDidRequestCamera()
    }

    @objc private func microphoneTapped() {
        delegate?.composerDidRequestMicrophone()
    }

    @objc private func sendTapped() {
        delegate?.composerDidRequestConfirmSelection()
    }
}
