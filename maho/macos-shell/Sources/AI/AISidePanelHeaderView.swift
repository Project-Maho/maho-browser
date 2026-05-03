import AppKit

protocol AISidePanelHeaderDelegate: AnyObject {
    func headerDidTapCustomize(from button: NSButton)
    func headerDidTapClose()
    func headerDidTapNewChat()
    func headerDidTapDisplayMode()
}

final class AISidePanelHeaderView: NSView {
    private struct HeaderTheme {
        let statusText: NSColor
        let buttonTint: NSColor
        let emphasisTint: NSColor
    }

    weak var delegate: AISidePanelHeaderDelegate?

    private let topBar = NSView()
    private let leftControlsStack = NSStackView()
    private let rightControlsStack = NSStackView()
    private let displayModeControlStack = NSStackView()
    private let statusLabel = NSTextField(labelWithString: "")
    private let settingsButton = NSButton(title: "", target: nil, action: nil)
    private let newChatButton = NSButton(title: "", target: nil, action: nil)
    private let displayModeButton = NSButton(title: "", target: nil, action: nil)
    private let displayModeChevronButton = NSButton(title: "", target: nil, action: nil)
    private let closeButton = NSButton(title: "", target: nil, action: nil)
    private var statusHeightConstraint: NSLayoutConstraint?
    private var currentSubtitle: String = "Ask, compare, and organize"

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) { fatalError() }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
    }

    func updateSubtitle(_ text: String) {
        currentSubtitle = text
        statusLabel.stringValue = text
        let shouldHide = text == "Ask, compare, and organize"
        statusLabel.isHidden = shouldHide
        statusHeightConstraint?.constant = shouldHide ? 0 : 16
        applyTextStyling()
    }

    func setSettingsHidden(_ hidden: Bool) {
        settingsButton.isHidden = hidden
    }

    private func headerTheme() -> HeaderTheme {
        HeaderTheme(
            statusText: AIDiaPalette.mutedText,
            buttonTint: AIDiaPalette.color(233, 236, 242, alpha: 0.72),
            emphasisTint: AIDiaPalette.color(233, 236, 242, alpha: 0.82)
        )
    }

    private func applyTheme() {
        let theme = headerTheme()
        statusLabel.textColor = theme.statusText
        applyTextStyling()

        styleIconButton(settingsButton, tint: theme.buttonTint)
        styleIconButton(newChatButton, tint: theme.buttonTint)
        styleIconButton(displayModeButton, tint: theme.buttonTint)
        styleIconButton(displayModeChevronButton, tint: theme.buttonTint)
        styleIconButton(closeButton, tint: theme.emphasisTint)
    }

    private func applyTextStyling() {
        statusLabel.font = .systemFont(ofSize: 11, weight: .medium)
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.lineBreakMode = .byTruncatingTail
        statusLabel.attributedStringValue = NSAttributedString(
            string: statusLabel.stringValue,
            attributes: [
                .font: statusLabel.font as Any,
                .foregroundColor: statusLabel.textColor as Any,
                .kern: -0.24,
                .paragraphStyle: paragraphStyle,
            ]
        )
    }

    private func setupUI() {
        translatesAutoresizingMaskIntoConstraints = false
        setAccessibilityIdentifier("ai.panel.header")

        topBar.translatesAutoresizingMaskIntoConstraints = false
        addSubview(topBar)

        leftControlsStack.translatesAutoresizingMaskIntoConstraints = false
        leftControlsStack.orientation = .horizontal
        leftControlsStack.alignment = .centerY
        leftControlsStack.spacing = 0
        topBar.addSubview(leftControlsStack)

        rightControlsStack.translatesAutoresizingMaskIntoConstraints = false
        rightControlsStack.orientation = .horizontal
        rightControlsStack.alignment = .centerY
        rightControlsStack.spacing = 12
        rightControlsStack.detachesHiddenViews = true
        topBar.addSubview(rightControlsStack)

        displayModeControlStack.translatesAutoresizingMaskIntoConstraints = false
        displayModeControlStack.orientation = .horizontal
        displayModeControlStack.alignment = .centerY
        displayModeControlStack.spacing = -3
        displayModeControlStack.detachesHiddenViews = true

        configureControlButton(
            settingsButton,
            accessibilityIdentifier: "ai.panel.header.customize",
            symbolName: "square.and.pencil",
            description: "Edit page context",
            action: #selector(settingsTapped),
            pointSize: 14
        )
        configureControlButton(
            newChatButton,
            accessibilityIdentifier: "ai.panel.header.newChat",
            symbolName: "lines.measurement.horizontal",
            description: "Assistant style control",
            action: #selector(newChatTapped),
            pointSize: 13
        )
        configureControlButton(
            displayModeButton,
            accessibilityIdentifier: "ai.panel.header.displayMode",
            symbolName: "rectangle.split.2x1",
            description: "Panel layout",
            action: #selector(displayModeTapped),
            pointSize: 13
        )
        configureControlButton(
            displayModeChevronButton,
            accessibilityIdentifier: "ai.panel.header.displayModeChevron",
            symbolName: "chevron.down",
            description: "Panel layout options",
            action: #selector(displayModeTapped),
            pointSize: 10
        )
        configureControlButton(
            closeButton,
            accessibilityIdentifier: "ai.panel.header.close",
            symbolName: "xmark",
            description: "Close panel",
            action: #selector(closeTapped),
            pointSize: 13
        )

        leftControlsStack.addArrangedSubview(settingsButton)
        displayModeControlStack.addArrangedSubview(displayModeButton)
        displayModeControlStack.addArrangedSubview(displayModeChevronButton)
        [newChatButton, displayModeControlStack, closeButton].forEach {
            rightControlsStack.addArrangedSubview($0)
        }

        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        statusLabel.font = .systemFont(ofSize: 11, weight: .medium)
        statusLabel.maximumNumberOfLines = 2
        statusLabel.lineBreakMode = .byTruncatingTail
        statusLabel.isHidden = true
        addSubview(statusLabel)
        let statusHeightConstraint = statusLabel.heightAnchor.constraint(equalToConstant: 0)
        self.statusHeightConstraint = statusHeightConstraint

        NSLayoutConstraint.activate([
            topBar.topAnchor.constraint(equalTo: topAnchor),
            topBar.leadingAnchor.constraint(equalTo: leadingAnchor),
            topBar.trailingAnchor.constraint(equalTo: trailingAnchor),
            topBar.heightAnchor.constraint(equalToConstant: 24),

            leftControlsStack.leadingAnchor.constraint(equalTo: topBar.leadingAnchor),
            leftControlsStack.centerYAnchor.constraint(equalTo: topBar.centerYAnchor),

            rightControlsStack.leadingAnchor.constraint(greaterThanOrEqualTo: leftControlsStack.trailingAnchor, constant: 14),
            rightControlsStack.trailingAnchor.constraint(equalTo: topBar.trailingAnchor),
            rightControlsStack.centerYAnchor.constraint(equalTo: topBar.centerYAnchor),

            settingsButton.widthAnchor.constraint(equalToConstant: 24),
            settingsButton.heightAnchor.constraint(equalToConstant: 24),
            newChatButton.widthAnchor.constraint(equalToConstant: 24),
            newChatButton.heightAnchor.constraint(equalToConstant: 24),
            displayModeButton.widthAnchor.constraint(equalToConstant: 24),
            displayModeButton.heightAnchor.constraint(equalToConstant: 24),
            displayModeChevronButton.widthAnchor.constraint(equalToConstant: 12),
            displayModeChevronButton.heightAnchor.constraint(equalToConstant: 24),
            closeButton.widthAnchor.constraint(equalToConstant: 24),
            closeButton.heightAnchor.constraint(equalToConstant: 24),

            statusLabel.topAnchor.constraint(equalTo: topBar.bottomAnchor, constant: 8),
            statusLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 2),
            statusLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -2),
            statusHeightConstraint,
            statusLabel.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])

        applyTheme()
        updateSubtitle(currentSubtitle)
    }

    private func configureControlButton(
        _ button: NSButton,
        accessibilityIdentifier: String,
        symbolName: String,
        description: String,
        action: Selector,
        pointSize: CGFloat
    ) {
        button.translatesAutoresizingMaskIntoConstraints = false
        button.setAccessibilityIdentifier(accessibilityIdentifier)
        button.setAccessibilityLabel(description)
        button.title = ""
        let configuration = NSImage.SymbolConfiguration(pointSize: pointSize, weight: .medium)
        button.image = NSImage(systemSymbolName: symbolName, accessibilityDescription: description)?.withSymbolConfiguration(configuration)
        button.isBordered = false
        button.imageScaling = .scaleProportionallyDown
        button.bezelStyle = .regularSquare
        button.target = self
        button.action = action
        styleIconButton(button, tint: AIDiaPalette.secondaryText)
    }

    private func styleIconButton(_ button: NSButton, tint: NSColor) {
        button.wantsLayer = true
        button.layer?.backgroundColor = NSColor.clear.cgColor
        button.layer?.borderWidth = 0
        button.layer?.borderColor = NSColor.clear.cgColor
        button.layer?.cornerRadius = 10
        button.layer?.shadowOpacity = 0
        button.contentTintColor = tint
    }

    @objc private func settingsTapped() {
        delegate?.headerDidTapCustomize(from: settingsButton)
    }

    @objc private func closeTapped() {
        delegate?.headerDidTapClose()
    }

    @objc private func newChatTapped() {
        delegate?.headerDidTapNewChat()
    }

    @objc private func displayModeTapped() {
        delegate?.headerDidTapDisplayMode()
    }
}
