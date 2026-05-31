import AppKit
import WebKit
import UniformTypeIdentifiers

enum AIDiaPalette {
    private static func dynamicColor(light: NSColor, dark: NSColor) -> NSColor {
        NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? dark : light
        }
    }

    static func color(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat, alpha: CGFloat = 1.0) -> NSColor {
        NSColor(red: red / 255.0, green: green / 255.0, blue: blue / 255.0, alpha: alpha)
    }

    static var panelBackground: NSColor {
        dynamicColor(
            light: color(244, 243, 241),
            dark: color(17, 19, 24)
        )
    }

    static var panelShellTop: NSColor {
        dynamicColor(
            light: color(251, 250, 247, alpha: 0.30),
            dark: NSColor.white.withAlphaComponent(0.045)
        )
    }

    static var panelHighlightGlow: NSColor {
        dynamicColor(
            light: color(255, 255, 255, alpha: 0.20),
            dark: NSColor.white.withAlphaComponent(0.05)
        )
    }

    static var panelAccentGlow: NSColor {
        dynamicColor(
            light: color(132, 149, 198, alpha: 0.14),
            dark: color(118, 133, 168, alpha: 0.10)
        )
    }

    static var panelInnerBackground: NSColor {
        dynamicColor(
            light: color(250, 249, 246, alpha: 0.96),
            dark: color(13, 15, 20, alpha: 0.42)
        )
    }

    static var panelInnerBottom: NSColor {
        dynamicColor(
            light: color(247, 246, 243, alpha: 0.22),
            dark: color(13, 15, 20, alpha: 0.18)
        )
    }

    static var sectionBackground: NSColor {
        dynamicColor(
            light: color(243, 241, 236),
            dark: color(22, 24, 30)
        )
    }

    static var documentBackground: NSColor {
        dynamicColor(
            light: color(252, 251, 248),
            dark: color(13, 15, 20, alpha: 0.01)
        )
    }

    static var elevatedBackground: NSColor {
        dynamicColor(
            light: color(252, 251, 247),
            dark: color(43, 43, 45)
        )
    }

    static var controlBackground: NSColor {
        dynamicColor(
            light: color(243, 241, 236),
            dark: color(22, 24, 30)
        )
    }

    static var controlActiveBackground: NSColor {
        dynamicColor(
            light: color(235, 239, 249),
            dark: color(27, 30, 37)
        )
    }

    static var border: NSColor {
        dynamicColor(
            light: color(213, 211, 206),
            dark: NSColor.white.withAlphaComponent(0.07)
        )
    }

    static var softBorder: NSColor {
        dynamicColor(
            light: color(225, 223, 217),
            dark: NSColor.white.withAlphaComponent(0.04)
        )
    }

    static var divider: NSColor {
        dynamicColor(
            light: color(220, 218, 212, alpha: 0.55),
            dark: NSColor.white.withAlphaComponent(0.03)
        )
    }

    static var titleText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.164, green: 0.164, blue: 0.176, alpha: 1.0),
            dark: NSColor(red: 0.890, green: 0.905, blue: 0.937, alpha: 1.0)
        )
    }

    static var bodyText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.240, green: 0.242, blue: 0.257, alpha: 1.0),
            dark: NSColor(red: 0.860, green: 0.874, blue: 0.908, alpha: 0.92)
        )
    }

    static var secondaryText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.455, green: 0.449, blue: 0.431, alpha: 1.0),
            dark: NSColor(red: 0.744, green: 0.760, blue: 0.808, alpha: 0.74)
        )
    }

    static var mutedText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.607, green: 0.598, blue: 0.575, alpha: 1.0),
            dark: NSColor(red: 0.520, green: 0.564, blue: 0.640, alpha: 0.94)
        )
    }

    static var accent: NSColor {
        dynamicColor(
            light: NSColor(red: 0.318, green: 0.480, blue: 0.871, alpha: 1.0),
            dark: NSColor(red: 0.760, green: 0.808, blue: 1.000, alpha: 1.0)
        )
    }

    static var accentWash: NSColor {
        dynamicColor(
            light: NSColor(red: 0.318, green: 0.480, blue: 0.871, alpha: 0.10),
            dark: NSColor(red: 0.517, green: 0.584, blue: 0.776, alpha: 0.14)
        )
    }

    static var accentBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.318, green: 0.480, blue: 0.871, alpha: 0.24),
            dark: NSColor(red: 0.517, green: 0.584, blue: 0.776, alpha: 0.28)
        )
    }

    static var userBubbleBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.932, green: 0.950, blue: 0.986, alpha: 1.0),
            dark: NSColor(red: 0.118, green: 0.125, blue: 0.153, alpha: 0.96)
        )
    }

    static var userBubbleBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.806, green: 0.860, blue: 0.947, alpha: 1.0),
            dark: NSColor(white: 1.0, alpha: 0.08)
        )
    }

    static var assistantBubbleBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.985, green: 0.985, blue: 0.980, alpha: 1.0),
            dark: NSColor(red: 0.122, green: 0.126, blue: 0.145, alpha: 0.94)
        )
    }

    static var assistantBubbleBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.868, green: 0.862, blue: 0.842, alpha: 1.0),
            dark: NSColor(white: 1.0, alpha: 0.05)
        )
    }

    static var codeBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.946, green: 0.942, blue: 0.930, alpha: 1.0),
            dark: NSColor(red: 0.088, green: 0.092, blue: 0.113, alpha: 0.96)
        )
    }

    static var codeBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.868, green: 0.862, blue: 0.842, alpha: 1.0),
            dark: NSColor(white: 1.0, alpha: 0.05)
        )
    }

    static var chipBackground: NSColor {
        dynamicColor(
            light: color(233, 231, 225, alpha: 0.94),
            dark: color(34, 36, 42, alpha: 0.90)
        )
    }

    static var chipSheen: NSColor {
        dynamicColor(
            light: NSColor.white.withAlphaComponent(0.04),
            dark: NSColor.white.withAlphaComponent(0.015)
        )
    }

    static var chipInsetHighlight: NSColor {
        dynamicColor(
            light: NSColor.white.withAlphaComponent(0.06),
            dark: NSColor.white.withAlphaComponent(0.02)
        )
    }

    static var chipBorder: NSColor {
        dynamicColor(
            light: NSColor.clear,
            dark: NSColor.clear
        )
    }

    static var chipText: NSColor {
        dynamicColor(
            light: color(61, 62, 66),
            dark: color(229, 232, 238, alpha: 0.88)
        )
    }

    static var composerBackground: NSColor {
        dynamicColor(
            light: color(246, 244, 238),
            dark: color(43, 43, 45)
        )
    }

    static var composerBottomBackground: NSColor {
        dynamicColor(
            light: color(245, 242, 238),
            dark: color(44, 44, 46)
        )
    }

    static var composerStreamingBackground: NSColor {
        dynamicColor(
            light: color(239, 243, 250),
            dark: color(47, 50, 57)
        )
    }

    static var composerSheen: NSColor {
        dynamicColor(
            light: NSColor.white.withAlphaComponent(0.04),
            dark: NSColor.white.withAlphaComponent(0.025)
        )
    }

    static var composerBorder: NSColor {
        dynamicColor(
            light: color(215, 211, 201, alpha: 0.90),
            dark: NSColor.white.withAlphaComponent(0.09)
        )
    }

    static var composerPlaceholderText: NSColor {
        dynamicColor(
            light: color(110, 118, 134, alpha: 0.92),
            dark: color(194, 196, 202, alpha: 0.80)
        )
    }

    static var composerInputText: NSColor {
        dynamicColor(
            light: color(44, 45, 49),
            dark: color(231, 234, 240, alpha: 0.94)
        )
    }

    static var composerControlBackground: NSColor {
        dynamicColor(
            light: color(238, 235, 229),
            dark: color(22, 24, 30)
        )
    }

    static var composerControlBorder: NSColor {
        dynamicColor(
            light: NSColor.clear,
            dark: NSColor.clear
        )
    }

    static var contextPillBackground: NSColor {
        dynamicColor(
            light: color(223, 220, 213, alpha: 0.88),
            dark: color(86, 86, 89, alpha: 0.42)
        )
    }

    static var contextPillSheen: NSColor {
        dynamicColor(
            light: NSColor.white.withAlphaComponent(0.03),
            dark: NSColor.white.withAlphaComponent(0.02)
        )
    }

    static var contextPillBorder: NSColor {
        dynamicColor(
            light: color(214, 210, 201, alpha: 0.45),
            dark: NSColor.white.withAlphaComponent(0.04)
        )
    }

    static var contextIconBackground: NSColor {
        dynamicColor(
            light: color(20, 22, 28),
            dark: color(7, 7, 8)
        )
    }

    static var contextIconTint: NSColor {
        dynamicColor(
            light: color(225, 228, 239),
            dark: color(223, 228, 239)
        )
    }

    static var contextRemoveBackground: NSColor {
        dynamicColor(
            light: color(112, 112, 115, alpha: 0.90),
            dark: color(112, 112, 115, alpha: 0.90)
        )
    }

    static var sendButtonBackground: NSColor {
        dynamicColor(
            light: NSColor.white,
            dark: NSColor.white
        )
    }

    static var sendIconTint: NSColor {
        dynamicColor(
            light: color(12, 13, 16),
            dark: color(12, 13, 16)
        )
    }

    static var shadow: NSColor {
        dynamicColor(
            light: NSColor(calibratedWhite: 0.0, alpha: 0.12),
            dark: color(0, 0, 0, alpha: 0.52)
        )
    }
}

struct TabReference {
    let id: String
    let title: String
    let url: String
    let favicon: ImageData?
    let webView: WKWebView
}

protocol AISidePanelDelegate: AnyObject {
    func aiSidePanelDidRequestClose()
    func aiSidePanelDidRequestTidyTabs(from sourceView: NSView)
    func aiSidePanelCanRequestTidyTabs() -> Bool
    func aiSidePanelAllTabs() -> [TabReference]
    func aiSidePanelToolRegistry() -> MCPToolRegistry?
    func aiSidePanelCaptureActiveTab(completion: @escaping (NSImage?) -> Void)
    func aiSidePanelDidRequestDetach()
    func aiSidePanelDidRequestSidebarMode()
    func aiSidePanelDidRequestOpenInNewTab()
}

enum AISidePanelDisplayMode {
    case sidebar
    case floating
    case newTab
}

final class AISidePanel: NSView, AIChatHandlerDelegate, AISidePanelHeaderDelegate, AISidePanelComposerDelegate, TabMentionAutocompleteDelegate, SkillPickerDelegate, SuggestionChipBarDelegate {
    private var displayMode: AISidePanelDisplayMode = .sidebar
    private struct PanelTheme {
        let shellBackground: NSColor
        let shellBorder: NSColor
        let tintBackground: NSColor
        let dividerColor: NSColor
        let onboardingBadgeBackground: NSColor
        let onboardingBadgeBorder: NSColor
        let onboardingIconTint: NSColor
        let onboardingEyebrowText: NSColor
        let onboardingTitleText: NSColor
        let onboardingDescriptionText: NSColor
        let settingsButtonTint: NSColor
        let settingsButtonBackground: NSColor
        let settingsButtonBorder: NSColor
    }

    weak var delegate: AISidePanelDelegate?

    private let panelShell = NSView()
    private let panelTintView = NSView()
    private let panelTextureView = AIPanelTextureOverlayView(frame: .zero)
    private let bodyDivider = NSView()

    private let headerView = AISidePanelHeaderView(frame: .zero)
    private let workspaceView = NSView()
    private let chatView = AIChatView(frame: .zero)
    private let suggestionChipBar = SuggestionChipBar(frame: .zero)
    private let composerView = AISidePanelComposerView(frame: .zero)

    private let onboardingView = NSView()
    private let onboardingBadgeView = NSView()
    private let onboardingIconView = NSImageView()
    private let onboardingHeaderStack = NSStackView()
    private let onboardingHeaderTextStack = NSStackView()
    private let onboardingEyebrowLabel = NSTextField(labelWithString: "SET UP MAHO AI")
    private let onboardingTitleLabel = NSTextField(labelWithString: "Open Settings → Maho AI")
    private let onboardingDescriptionLabel = NSTextField(labelWithString: "Use the button below to open Settings and finish setup.")
    private let openSettingsButton = NSButton(title: "Open Settings → Maho AI", target: nil, action: nil)

    private let chatHandler = AIChatHandler()
    private let autocompleteView = TabMentionAutocompleteView(frame: .zero)
    private var autocompleteHeightConstraint: NSLayoutConstraint?
    private var isAutocompleteVisible = false
    private let skillPickerView = SkillPickerView(frame: .zero)
    private var skillPickerHeightConstraint: NSLayoutConstraint?
    private var isSkillPickerVisible = false
    private lazy var onboardingBottomToShellConstraint = onboardingView.bottomAnchor.constraint(equalTo: panelShell.bottomAnchor, constant: -24)
    private var isShowingOnboarding = false
    private var isStreamingResponse = false
    private var pendingScreenshot: NSImage?
    private var hasAppliedPreviewSeedPageContext = false

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        setAccessibilityIdentifier("ai.panel.content")
        chatHandler.delegate = self
        headerView.delegate = self
        composerView.delegate = self
        autocompleteView.delegate = self
        skillPickerView.delegate = self
        setupUI()
        observeConfigurationChanges()
    }

    required init?(coder: NSCoder) { fatalError() }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        applyTheme()
        composerView.updateComposerChrome(isStreaming: isStreamingResponse)
    }

    private func panelTheme() -> PanelTheme {
        PanelTheme(
            shellBackground: AIDiaPalette.panelBackground,
            shellBorder: AIDiaPalette.border,
            tintBackground: AIDiaPalette.panelInnerBackground,
            dividerColor: AIDiaPalette.divider,
            onboardingBadgeBackground: AIDiaPalette.controlBackground,
            onboardingBadgeBorder: AIDiaPalette.softBorder,
            onboardingIconTint: AIDiaPalette.accent,
            onboardingEyebrowText: AIDiaPalette.mutedText,
            onboardingTitleText: AIDiaPalette.titleText,
            onboardingDescriptionText: AIDiaPalette.secondaryText,
            settingsButtonTint: AIDiaPalette.bodyText,
            settingsButtonBackground: AIDiaPalette.controlBackground,
            settingsButtonBorder: AIDiaPalette.border
        )
    }

    private func applyTheme() {
        let theme = panelTheme()
        layer?.backgroundColor = NSColor.clear.cgColor
        panelShell.configureSurface(
            background: theme.shellBackground,
            border: theme.shellBorder.withAlphaComponent(0.84),
            radius: 26,
            shadowOpacity: 0.28,
            shadowRadius: 24,
            shadowOffset: CGSize(width: 0, height: -14)
        )
        panelShell.aiEnsureGradientLayer(
            named: "ai.panel.outer.gradient",
            colors: [theme.shellBackground, theme.shellBackground],
            startPoint: CGPoint(x: 0.5, y: 1),
            endPoint: CGPoint(x: 0.5, y: 0),
            locations: [0, 0.12],
            opacity: 1,
            cornerRadius: 28
        )
        panelShell.aiEnsureRadialGlowLayer(
            named: "ai.panel.outer.highlight",
            color: AIDiaPalette.panelHighlightGlow,
            center: CGPoint(x: 0.20, y: 1.0),
            radius: max(bounds.width * 0.28, 1),
            opacity: 1,
            cornerRadius: 28
        )
        panelShell.aiEnsureRadialGlowLayer(
            named: "ai.panel.outer.accent",
            color: AIDiaPalette.panelAccentGlow,
            center: CGPoint(x: 0.80, y: 0.0),
            radius: max(bounds.width * 0.32, 1),
            opacity: 1,
            cornerRadius: 28
        )
        panelTextureView.lineColor = NSColor.white.withAlphaComponent(0.012)
        panelTintView.layer?.backgroundColor = theme.tintBackground.cgColor
        panelTintView.layer?.borderWidth = 1
        panelTintView.layer?.borderColor = NSColor.white.withAlphaComponent(0.04).cgColor
        panelTintView.aiEnsureGradientLayer(
            named: "ai.panel.inner.gradient",
            colors: [NSColor.white.withAlphaComponent(0.02), AIDiaPalette.panelInnerBottom],
            startPoint: CGPoint(x: 0.5, y: 1),
            endPoint: CGPoint(x: 0.5, y: 0),
            locations: [0, 1],
            opacity: 1,
            cornerRadius: 22
        )
        bodyDivider.layer?.backgroundColor = theme.dividerColor.cgColor
        onboardingBadgeView.configureSurface(
            background: theme.onboardingBadgeBackground,
            border: theme.onboardingBadgeBorder,
            radius: 14
        )
        onboardingIconView.contentTintColor = theme.onboardingIconTint
        onboardingEyebrowLabel.textColor = theme.onboardingEyebrowText
        onboardingTitleLabel.textColor = theme.onboardingTitleText
        onboardingDescriptionLabel.textColor = theme.onboardingDescriptionText
        openSettingsButton.contentTintColor = theme.settingsButtonTint
        openSettingsButton.styleChromeButton(
            background: theme.settingsButtonBackground,
            border: theme.settingsButtonBorder,
            radius: 14
        )
    }

    private func setupUI() {
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor

        panelShell.translatesAutoresizingMaskIntoConstraints = false
        panelShell.setAccessibilityElement(true)
        panelShell.setAccessibilityRole(.group)
        panelShell.setAccessibilityIdentifier("ai.panel.shell")
        panelShell.configureSurface(
            background: AIDiaPalette.panelBackground,
            border: AIDiaPalette.border.withAlphaComponent(0.88),
            radius: 28,
            shadowOpacity: 0.34,
            shadowRadius: 30,
            shadowOffset: CGSize(width: 0, height: -18)
        )
        addSubview(panelShell)

        panelTintView.translatesAutoresizingMaskIntoConstraints = false
        panelTintView.wantsLayer = true
        panelTintView.layer?.backgroundColor = AIDiaPalette.panelInnerBackground.cgColor
        panelTintView.layer?.cornerRadius = 22
        panelShell.addSubview(panelTintView)

        panelTextureView.lineColor = NSColor.white.withAlphaComponent(0.012)
        panelShell.addSubview(panelTextureView)

        bodyDivider.translatesAutoresizingMaskIntoConstraints = false
        bodyDivider.wantsLayer = true
        bodyDivider.layer?.backgroundColor = AIDiaPalette.divider.cgColor
        bodyDivider.isHidden = true
        panelShell.addSubview(bodyDivider)

        panelShell.addSubview(headerView)

        workspaceView.translatesAutoresizingMaskIntoConstraints = false
        workspaceView.setAccessibilityElement(true)
        workspaceView.setAccessibilityRole(.group)
        workspaceView.setAccessibilityIdentifier("ai.panel.workspace")
        panelShell.addSubview(workspaceView)

        chatView.translatesAutoresizingMaskIntoConstraints = false
        chatView.setContentCompressionResistancePriority(.defaultLow, for: .vertical)
        chatView.setContentHuggingPriority(.defaultLow, for: .vertical)
        workspaceView.addSubview(chatView)

        suggestionChipBar.translatesAutoresizingMaskIntoConstraints = false
        suggestionChipBar.setAccessibilityIdentifier("ai.panel.workspace.suggestions")
        suggestionChipBar.delegate = self
        workspaceView.addSubview(suggestionChipBar)

        setupOnboardingUI()

        workspaceView.addSubview(composerView)

        autocompleteView.translatesAutoresizingMaskIntoConstraints = false
        autocompleteView.isHidden = true
        autocompleteView.setContentCompressionResistancePriority(.required, for: .vertical)
        panelShell.addSubview(autocompleteView)

        skillPickerView.translatesAutoresizingMaskIntoConstraints = false
        skillPickerView.isHidden = true
        skillPickerView.setContentCompressionResistancePriority(.required, for: .vertical)
        panelShell.addSubview(skillPickerView)

        let autocompleteConstraint = autocompleteView.heightAnchor.constraint(equalToConstant: 0)
        autocompleteHeightConstraint = autocompleteConstraint

        let skillConstraint = skillPickerView.heightAnchor.constraint(equalToConstant: 0)
        skillPickerHeightConstraint = skillConstraint

        NSLayoutConstraint.activate([
            panelShell.topAnchor.constraint(equalTo: topAnchor),
            panelShell.leadingAnchor.constraint(equalTo: leadingAnchor),
            panelShell.trailingAnchor.constraint(equalTo: trailingAnchor),
            panelShell.bottomAnchor.constraint(equalTo: bottomAnchor),

            panelTintView.topAnchor.constraint(equalTo: panelShell.topAnchor, constant: 0),
            panelTintView.leadingAnchor.constraint(equalTo: panelShell.leadingAnchor, constant: 10),
            panelTintView.trailingAnchor.constraint(equalTo: panelShell.trailingAnchor, constant: -10),
            panelTintView.bottomAnchor.constraint(equalTo: panelShell.bottomAnchor, constant: -10),

            panelTextureView.topAnchor.constraint(equalTo: panelShell.topAnchor),
            panelTextureView.leadingAnchor.constraint(equalTo: panelShell.leadingAnchor),
            panelTextureView.trailingAnchor.constraint(equalTo: panelShell.trailingAnchor),
            panelTextureView.bottomAnchor.constraint(equalTo: panelShell.bottomAnchor),

            bodyDivider.bottomAnchor.constraint(equalTo: workspaceView.bottomAnchor, constant: -10),
            bodyDivider.leadingAnchor.constraint(equalTo: panelShell.leadingAnchor, constant: 24),
            bodyDivider.trailingAnchor.constraint(equalTo: panelShell.trailingAnchor, constant: -24),
            bodyDivider.heightAnchor.constraint(equalToConstant: 1),

            headerView.topAnchor.constraint(equalTo: panelShell.topAnchor, constant: 22),
            headerView.leadingAnchor.constraint(equalTo: panelShell.leadingAnchor, constant: 22),
            headerView.trailingAnchor.constraint(equalTo: panelShell.trailingAnchor, constant: -22),

            workspaceView.topAnchor.constraint(equalTo: headerView.bottomAnchor, constant: 5),
            workspaceView.leadingAnchor.constraint(equalTo: panelShell.leadingAnchor, constant: 24),
            workspaceView.trailingAnchor.constraint(equalTo: panelShell.trailingAnchor, constant: -24),
            workspaceView.bottomAnchor.constraint(equalTo: panelShell.bottomAnchor, constant: -12),

            chatView.topAnchor.constraint(equalTo: workspaceView.topAnchor),
            chatView.leadingAnchor.constraint(equalTo: workspaceView.leadingAnchor),
            chatView.trailingAnchor.constraint(equalTo: workspaceView.trailingAnchor),
            chatView.bottomAnchor.constraint(equalTo: suggestionChipBar.topAnchor, constant: -18),

            suggestionChipBar.leadingAnchor.constraint(equalTo: workspaceView.leadingAnchor, constant: 2),
            suggestionChipBar.trailingAnchor.constraint(lessThanOrEqualTo: workspaceView.trailingAnchor),
            suggestionChipBar.bottomAnchor.constraint(equalTo: composerView.topAnchor, constant: -18),

            autocompleteView.leadingAnchor.constraint(equalTo: panelShell.leadingAnchor, constant: 22),
            autocompleteView.trailingAnchor.constraint(equalTo: panelShell.trailingAnchor, constant: -22),
            autocompleteView.bottomAnchor.constraint(equalTo: composerView.topAnchor, constant: -8),
            autocompleteConstraint,

            skillPickerView.leadingAnchor.constraint(equalTo: panelShell.leadingAnchor, constant: 22),
            skillPickerView.trailingAnchor.constraint(equalTo: panelShell.trailingAnchor, constant: -22),
            skillPickerView.bottomAnchor.constraint(equalTo: composerView.topAnchor, constant: -8),
            skillConstraint,

            composerView.leadingAnchor.constraint(equalTo: workspaceView.leadingAnchor),
            composerView.trailingAnchor.constraint(equalTo: workspaceView.trailingAnchor),
            composerView.bottomAnchor.constraint(equalTo: workspaceView.bottomAnchor),
        ])

        applyTheme()
        composerView.updateComposerChrome(isStreaming: false)
        updateTidyTabsActionState()
        updateCompareTabsState()
        refreshConfigurationState()
    }

    private func setupOnboardingUI() {
        onboardingView.translatesAutoresizingMaskIntoConstraints = false
        onboardingView.setAccessibilityElement(true)
        onboardingView.setAccessibilityRole(.group)
        onboardingView.setAccessibilityIdentifier("ai.panel.onboarding")
        onboardingView.isHidden = true
        panelShell.addSubview(onboardingView)

        onboardingBadgeView.configureSurface(
            background: AIDiaPalette.controlBackground,
            border: AIDiaPalette.softBorder,
            radius: 14
        )
        onboardingBadgeView.translatesAutoresizingMaskIntoConstraints = false

        onboardingIconView.translatesAutoresizingMaskIntoConstraints = false
        onboardingIconView.image = MahoIcon.imageForSFSymbol("sparkles.rectangle.stack", accessibilityDescription: "Set up AI")
        onboardingIconView.imageScaling = .scaleProportionallyDown
        onboardingIconView.contentTintColor = AIDiaPalette.accent
        onboardingBadgeView.addSubview(onboardingIconView)

        onboardingHeaderStack.translatesAutoresizingMaskIntoConstraints = false
        onboardingHeaderStack.orientation = .horizontal
        onboardingHeaderStack.alignment = .top
        onboardingHeaderStack.spacing = 14
        onboardingHeaderStack.distribution = .fill
        onboardingView.addSubview(onboardingHeaderStack)

        onboardingHeaderTextStack.translatesAutoresizingMaskIntoConstraints = false
        onboardingHeaderTextStack.orientation = .vertical
        onboardingHeaderTextStack.alignment = .leading
        onboardingHeaderTextStack.spacing = 6
        onboardingHeaderTextStack.distribution = .fill

        onboardingEyebrowLabel.translatesAutoresizingMaskIntoConstraints = false
        onboardingEyebrowLabel.setAccessibilityIdentifier("ai.panel.onboarding.eyebrow")
        onboardingEyebrowLabel.font = .systemFont(ofSize: 10, weight: .semibold)
        onboardingEyebrowLabel.textColor = AIDiaPalette.mutedText

        onboardingTitleLabel.translatesAutoresizingMaskIntoConstraints = false
        onboardingTitleLabel.setAccessibilityIdentifier("ai.panel.onboarding.title")
        onboardingTitleLabel.font = .systemFont(ofSize: 24, weight: .semibold)
        onboardingTitleLabel.textColor = AIDiaPalette.titleText
        onboardingTitleLabel.maximumNumberOfLines = 2
        onboardingTitleLabel.lineBreakMode = .byWordWrapping

        onboardingDescriptionLabel.translatesAutoresizingMaskIntoConstraints = false
        onboardingDescriptionLabel.setAccessibilityIdentifier("ai.panel.onboarding.description")
        onboardingDescriptionLabel.font = .systemFont(ofSize: 13, weight: .regular)
        onboardingDescriptionLabel.textColor = AIDiaPalette.secondaryText
        onboardingDescriptionLabel.maximumNumberOfLines = 0
        onboardingDescriptionLabel.lineBreakMode = .byWordWrapping

        onboardingHeaderTextStack.addArrangedSubview(onboardingEyebrowLabel)
        onboardingHeaderTextStack.addArrangedSubview(onboardingTitleLabel)
        onboardingHeaderTextStack.addArrangedSubview(onboardingDescriptionLabel)
        onboardingHeaderStack.addArrangedSubview(onboardingBadgeView)
        onboardingHeaderStack.addArrangedSubview(onboardingHeaderTextStack)

        openSettingsButton.translatesAutoresizingMaskIntoConstraints = false
        openSettingsButton.setAccessibilityIdentifier("ai.panel.onboarding.openSettings")
        openSettingsButton.image = MahoIcon.imageForSFSymbol("slider.horizontal.3", accessibilityDescription: "Open AI Settings")
        openSettingsButton.imagePosition = .imageLeading
        openSettingsButton.imageScaling = .scaleProportionallyDown
        openSettingsButton.font = .systemFont(ofSize: 12, weight: .semibold)
        openSettingsButton.contentTintColor = AIDiaPalette.bodyText
        openSettingsButton.isBordered = false
        openSettingsButton.styleChromeButton(
            background: AIDiaPalette.controlBackground,
            border: AIDiaPalette.border,
            radius: 14
        )
        openSettingsButton.target = self
        openSettingsButton.action = #selector(openAISettingsTapped)
        onboardingView.addSubview(openSettingsButton)

        NSLayoutConstraint.activate([
            onboardingView.topAnchor.constraint(equalTo: headerView.bottomAnchor, constant: 24),
            onboardingView.leadingAnchor.constraint(equalTo: panelShell.leadingAnchor, constant: 22),
            onboardingView.trailingAnchor.constraint(equalTo: panelShell.trailingAnchor, constant: -22),
            onboardingBottomToShellConstraint,

            onboardingHeaderStack.topAnchor.constraint(equalTo: onboardingView.topAnchor, constant: 40),
            onboardingHeaderStack.leadingAnchor.constraint(equalTo: onboardingView.leadingAnchor, constant: 24),
            onboardingHeaderStack.trailingAnchor.constraint(equalTo: onboardingView.trailingAnchor, constant: -24),

            onboardingBadgeView.widthAnchor.constraint(equalToConstant: 36),
            onboardingBadgeView.heightAnchor.constraint(equalToConstant: 36),

            onboardingIconView.centerXAnchor.constraint(equalTo: onboardingBadgeView.centerXAnchor),
            onboardingIconView.centerYAnchor.constraint(equalTo: onboardingBadgeView.centerYAnchor),
            onboardingIconView.widthAnchor.constraint(equalToConstant: 16),
            onboardingIconView.heightAnchor.constraint(equalToConstant: 16),

            openSettingsButton.topAnchor.constraint(equalTo: onboardingHeaderStack.bottomAnchor, constant: 24),
            openSettingsButton.leadingAnchor.constraint(equalTo: onboardingView.leadingAnchor, constant: 24),
            openSettingsButton.trailingAnchor.constraint(equalTo: onboardingView.trailingAnchor, constant: -24),
            openSettingsButton.heightAnchor.constraint(equalToConstant: 40),
            openSettingsButton.bottomAnchor.constraint(lessThanOrEqualTo: onboardingView.bottomAnchor, constant: -24),
        ])
    }

    private func observeConfigurationChanges() {
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleConfigurationChanged),
            name: UserDefaults.didChangeNotification,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleConfigurationChanged),
            name: .mahoAIConfigurationDidChange,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleConfigurationChanged),
            name: NSApplication.didBecomeActiveNotification,
            object: nil
        )
    }

    @objc private func handleConfigurationChanged() {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.refreshConfigurationState()
        }
    }

    private func refreshConfigurationState() {
        let shouldShowOnboarding = !LLMService.shared.isConfigured
        isShowingOnboarding = shouldShowOnboarding
        composerView.setOnboardingVisible(shouldShowOnboarding)

        chatHandler.toolRegistry = delegate?.aiSidePanelToolRegistry()

        onboardingView.isHidden = !shouldShowOnboarding
        workspaceView.isHidden = shouldShowOnboarding
        chatView.isHidden = shouldShowOnboarding
        composerView.setComposerHidden(shouldShowOnboarding)
        bodyDivider.isHidden = true

        if shouldShowOnboarding {
            hasAppliedPreviewSeedPageContext = false
            dismissAutocomplete()
            dismissSkillPicker()
            headerView.updateSubtitle("Open Settings to connect Maho AI")
        } else {
            headerView.updateSubtitle("Ask, compare, and organize")
        }

        onboardingBottomToShellConstraint.isActive = shouldShowOnboarding

        headerView.setSettingsHidden(shouldShowOnboarding)
        if ProcessInfo.processInfo.arguments.contains("--ui-testing") {
            AppDelegate.writeUITestState([
                "aiPanelMode": shouldShowOnboarding ? "onboarding" : "chat"
            ])
        }
        updateTidyTabsActionState()
        updateCompareTabsState()
        updateProviderButtonSelection()
        updateSuggestionChips()
        if ProcessInfo.processInfo.arguments.contains("--ui-testing-ai-preview-seed"),
           !shouldShowOnboarding,
           !hasAppliedPreviewSeedPageContext {
            composerView.setPageContextEnabled(true)
            updateComposerContextSummary()
            hasAppliedPreviewSeedPageContext = true
        }
        needsLayout = true
        layoutSubtreeIfNeeded()
    }

    private func updateTidyTabsActionState() {
        let canTidy = delegate?.aiSidePanelCanRequestTidyTabs() ?? false
        composerView.updateTidyTabsActionState(canTidy: canTidy)
    }

    private func updateCompareTabsState() {
        let tabCount = delegate?.aiSidePanelAllTabs().count ?? 0
        composerView.updateCompareTabsActionState(canCompare: tabCount >= 2)
    }

    private func updateSuggestionChips() {
        let hasPage = !(delegate?.aiSidePanelAllTabs().isEmpty ?? true)
        suggestionChipBar.updateSuggestions(hasPageContext: hasPage)
        suggestionChipBar.isHidden = isShowingOnboarding || !chatHandler.messages.isEmpty
        updateComposerContextSummary()
    }

    func refreshContextSummary() {
        updateSuggestionChips()
        updateComposerContextSummary()
    }

    private func updateComposerContextSummary() {
        guard composerView.isPageContextEnabled else { return }

        guard let activeTab = delegate?.aiSidePanelAllTabs().first else {
            composerView.updatePageContext(title: "This page", url: "Current tab context", icon: nil)
            return
        }

        let rawTitle = activeTab.title.trimmingCharacters(in: .whitespacesAndNewlines)
        let resolvedTitle: String
        if ProcessInfo.processInfo.arguments.contains("--ui-testing-ai-preview-seed"),
           let host = URL(string: activeTab.url)?.host,
           host.contains("diabrowser.com") {
            resolvedTitle = "Dia Browser"
        } else if rawTitle.isEmpty {
            resolvedTitle = URL(string: activeTab.url)?.host ?? activeTab.url
        } else {
            resolvedTitle = rawTitle
        }

        if let host = URL(string: activeTab.url)?.host {
            composerView.updatePageContext(title: resolvedTitle, url: host, icon: activeTab.favicon)
        } else {
            composerView.updatePageContext(title: resolvedTitle, url: activeTab.url, icon: activeTab.favicon)
        }
    }

    private func currentProviderType() -> LLMProviderType {
        if let provider = LLMService.shared.currentProviderType {
            return provider
        }

        if let rawValue = UserDefaults.standard.string(forKey: AIProviderStorageKey.provider),
           let provider = LLMProviderType.persistedType(from: rawValue) {
            return provider
        }

        return .openai
    }

    private func updateProviderButtonSelection() {
        composerView.updateProviderButtonSelection(selectedProvider: currentProviderType())
    }

    @objc private func openAISettingsTapped() {
        (NSApp.delegate as? AppDelegate)?.openAISettings()
        refreshConfigurationState()
    }

    // MARK: - AISidePanelHeaderDelegate

    func headerDidTapCustomize(from button: NSButton) {
        let menu = NSMenu()
        menu.autoenablesItems = false

        let pageItem = NSMenuItem(title: "Include This Page", action: #selector(customizeTogglePageContext(_:)), keyEquivalent: "")
        pageItem.target = self
        pageItem.image = MahoIcon.imageForSFSymbol("doc.text")
        pageItem.state = composerView.isPageContextEnabled ? .on : .off
        menu.addItem(pageItem)

        menu.addItem(.separator())

        let settingsItem = NSMenuItem(title: "Open AI Settings…", action: #selector(openAISettingsTapped), keyEquivalent: ",")
        settingsItem.target = self
        settingsItem.image = MahoIcon.imageForSFSymbol("gearshape")
        menu.addItem(settingsItem)

        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: button.bounds.height), in: button)
    }

    @objc private func customizeTogglePageContext(_ sender: NSMenuItem) {
        let newState = !composerView.isPageContextEnabled
        composerView.setPageContextEnabled(newState)
        if newState {
            updateComposerContextSummary()
        }
    }

    private func currentDisplayMode() -> AISidePanelDisplayMode {
        displayMode
    }

    private func displayModeMenuItem(title: String, mode: AISidePanelDisplayMode, currentMode: AISidePanelDisplayMode) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: #selector(displayModeItemSelected(_:)), keyEquivalent: "")
        item.target = self
        item.representedObject = mode
        item.state = mode == currentMode ? .on : .off
        return item
    }

    @objc private func displayModeItemSelected(_ sender: NSMenuItem) {
        guard let mode = sender.representedObject as? AISidePanelDisplayMode else { return }
        switch mode {
        case .sidebar:
            delegate?.aiSidePanelDidRequestSidebarMode()
        case .floating:
            delegate?.aiSidePanelDidRequestDetach()
        case .newTab:
            delegate?.aiSidePanelDidRequestOpenInNewTab()
        }
    }

    func headerDidTapClose() {
        AppDelegate.writeUITestState(["aiPanelLastAction": "close"])
        delegate?.aiSidePanelDidRequestClose()
    }

    func setDisplayMode(_ mode: AISidePanelDisplayMode) {
        displayMode = mode
    }

    func headerDidTapNewChat() {
        chatHandler.startNewConversation()
        chatView.reloadMessages([])
        updateSuggestionChips()
        composerView.makeInputFirstResponder()
        AppDelegate.writeUITestState([
            "aiPanelLastAction": "newChat",
            "aiPanelMessageCount": "0",
            "aiPanelAssistantMessageCount": "0",
            "aiPanelLatestAssistantText": "",
            "aiPanelStreaming": "0"
        ])
    }

    func headerDidTapDisplayMode() {
        let menu = NSMenu()
        let currentMode = currentDisplayMode()

        menu.addItem(displayModeMenuItem(title: "Sidebar", mode: .sidebar, currentMode: currentMode))
        menu.addItem(displayModeMenuItem(title: "Floating", mode: .floating, currentMode: currentMode))
        menu.addItem(displayModeMenuItem(title: "Open in New Tab", mode: .newTab, currentMode: currentMode))

        let point = NSPoint(x: headerView.frame.maxX - 100, y: headerView.frame.minY)
        menu.popUp(positioning: nil, at: point, in: panelShell)
    }

    // MARK: - AISidePanelComposerDelegate

    func composerDidRequestTidyTabs(from button: NSButton) {
        delegate?.aiSidePanelDidRequestTidyTabs(from: button)
    }

    func composerDidTogglePageContext(enabled: Bool) {
        if enabled {
            updateComposerContextSummary()
        }
    }

    func composerDidRequestEntryPointMenu(from button: NSButton) {
        let menu = NSMenu()
        menu.autoenablesItems = false

        let pageItem = NSMenuItem(title: "Include This Page", action: #selector(entryPointTogglePageContext(_:)), keyEquivalent: "")
        pageItem.target = self
        pageItem.image = MahoIcon.imageForSFSymbol("doc.text")
        pageItem.state = composerView.isPageContextEnabled ? .on : .off
        menu.addItem(pageItem)

        let allTabs = delegate?.aiSidePanelAllTabs() ?? []
        let compareItem = NSMenuItem(title: "Compare Tabs…", action: #selector(entryPointCompareTabs(_:)), keyEquivalent: "")
        compareItem.target = self
        compareItem.image = MahoIcon.imageForSFSymbol("rectangle.on.rectangle")
        compareItem.isEnabled = allTabs.count >= 2
        menu.addItem(compareItem)

        menu.addItem(.separator())

        let fileItem = NSMenuItem(title: "Upload File…", action: #selector(entryPointUploadFile(_:)), keyEquivalent: "")
        fileItem.target = self
        fileItem.image = MahoIcon.imageForSFSymbol("doc.badge.plus")
        menu.addItem(fileItem)

        menu.addItem(.separator())

        let toolsItem = NSMenuItem(title: "Browse Tools…", action: #selector(entryPointBrowseTools(_:)), keyEquivalent: "")
        toolsItem.target = self
        toolsItem.image = MahoIcon.imageForSFSymbol("wrench")
        toolsItem.isEnabled = delegate?.aiSidePanelToolRegistry() != nil
        menu.addItem(toolsItem)

        let appsItem = NSMenuItem(title: "Connect App…", action: #selector(entryPointConnectApp(_:)), keyEquivalent: "")
        appsItem.target = self
        appsItem.image = MahoIcon.imageForSFSymbol("app.connected.to.app.below.fill")
        menu.addItem(appsItem)

        let formatItem = NSMenuItem(title: "Response Format", action: nil, keyEquivalent: "")
        formatItem.image = MahoIcon.imageForSFSymbol("text.alignleft")
        let formatSubmenu = NSMenu()
        for format in ["Auto", "Concise", "Detailed", "Step-by-Step"] {
            let fItem = NSMenuItem(title: format, action: #selector(entryPointSetFormat(_:)), keyEquivalent: "")
            fItem.target = self
            fItem.state = format == selectedResponseFormat ? .on : .off
            formatSubmenu.addItem(fItem)
        }
        formatItem.submenu = formatSubmenu
        menu.addItem(formatItem)

        menu.addItem(.separator())

        let tidyItem = NSMenuItem(title: "Tidy Tabs", action: #selector(entryPointTidyTabs(_:)), keyEquivalent: "")
        tidyItem.target = self
        tidyItem.image = MahoIcon.imageForSFSymbol("sparkles")
        tidyItem.isEnabled = delegate?.aiSidePanelCanRequestTidyTabs() ?? false
        menu.addItem(tidyItem)

        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: button.bounds.height), in: button)
    }

    @objc private func entryPointTogglePageContext(_ sender: NSMenuItem) {
        let newState = !composerView.isPageContextEnabled
        composerView.setPageContextEnabled(newState)
        if newState {
            updateComposerContextSummary()
        }
    }

    @objc private func entryPointCompareTabs(_ sender: NSMenuItem) {
        showCompareTabsMenu(from: composerView)
    }

    @objc private func entryPointTidyTabs(_ sender: NSMenuItem) {
        delegate?.aiSidePanelDidRequestTidyTabs(from: composerView)
    }

    @objc private func entryPointUploadFile(_ sender: NSMenuItem) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [.plainText, .pdf, .image]
        panel.beginSheetModal(for: self.window ?? NSApp.mainWindow ?? NSWindow()) { [weak self] response in
            guard response == .OK, let url = panel.url else { return }
            let filename = url.lastPathComponent
            if let contents = try? String(contentsOf: url, encoding: .utf8) {
                let truncated = String(contents.prefix(8000))
                self?.composerView.inputText += "\n[File: \(filename)]\n\(truncated)"
            } else {
                self?.composerView.inputText += "\n[Attached: \(filename)]"
            }
        }
    }

    @objc private func entryPointBrowseTools(_ sender: NSMenuItem) {
        guard let registry = delegate?.aiSidePanelToolRegistry() else { return }
        let tools = registry.listTools()
        guard !tools.isEmpty else { return }
        let toolNames = tools.compactMap { $0["name"] as? String }.joined(separator: ", ")
        composerView.inputText += "\n/tools: \(toolNames)"
        composerView.makeInputFirstResponder()
    }

    @objc private func entryPointConnectApp(_ sender: NSMenuItem) {
        (NSApp.delegate as? AppDelegate)?.openAISettings()
    }

    private var selectedResponseFormat: String = "Auto"

    @objc private func entryPointSetFormat(_ sender: NSMenuItem) {
        selectedResponseFormat = sender.title
        if let menu = sender.menu {
            for item in menu.items {
                item.state = item.title == sender.title ? .on : .off
            }
        }
    }

    func composerDidRequestCompareTabs(from button: NSButton) {
        showCompareTabsMenu(from: button)
    }

    private func showCompareTabsMenu(from sourceView: NSView) {
        let allTabs = delegate?.aiSidePanelAllTabs() ?? []
        guard allTabs.count >= 2 else { return }

        let menu = NSMenu()
        for tab in allTabs {
            let item = NSMenuItem(title: tab.title.isEmpty ? tab.url : tab.title, action: #selector(compareTabMenuItemToggled(_:)), keyEquivalent: "")
            item.target = self
            item.state = .off
            item.representedObject = tab.id
            menu.addItem(item)
        }
        menu.addItem(NSMenuItem.separator())
        let compareItem = NSMenuItem(title: "Compare Selected", action: #selector(compareSelectedTabs(_:)), keyEquivalent: "")
        compareItem.target = self
        compareItem.representedObject = menu
        menu.addItem(compareItem)

        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: sourceView.bounds.height), in: sourceView)
    }

    @objc private func compareTabMenuItemToggled(_ sender: NSMenuItem) {
        sender.state = sender.state == .on ? .off : .on
    }

    @objc private func compareSelectedTabs(_ sender: NSMenuItem) {
        guard let menu = sender.representedObject as? NSMenu else { return }
        let allTabs = delegate?.aiSidePanelAllTabs() ?? []
        let selectedIDs = menu.items.compactMap { item -> String? in
            guard item.state == .on, let id = item.representedObject as? String else { return nil }
            return id
        }
        guard selectedIDs.count >= 2 else { return }

        let selectedTabs = allTabs.filter { selectedIDs.contains($0.id) }
        let group = DispatchGroup()
        var contexts: [PageContext] = []
        let lock = NSLock()

        for tab in selectedTabs {
            group.enter()
            PageContextExtractor.extractText(from: tab.webView) { context in
                lock.lock()
                contexts.append(context)
                lock.unlock()
                group.leave()
            }
        }

        group.notify(queue: .main) { [weak self] in
            self?.chatHandler.sendWithReferencedPages("Compare and analyze these pages:", referencedPages: contexts, responseFormat: self?.selectedResponseFormat ?? "Auto")
        }
    }

    // MARK: - SuggestionChipBarDelegate

    func suggestionChipTapped(_ prompt: String) {
        composerView.inputText = prompt
        composerDidRequestConfirmSelection()
    }

    func submitQuery(_ query: String) {
        composerView.inputText = query
        composerDidRequestConfirmSelection()
    }

    func composerTextDidChange(_ text: String) {
        if text.hasPrefix("/") {
            dismissAutocomplete()
            handleSkillAutocomplete(text: text)
        } else {
            dismissSkillPicker()
            handleMentionAutocomplete(text: text)
        }
    }

    func composerDidRequestMoveUp() -> Bool {
        if isAutocompleteVisible {
            autocompleteView.moveSelectionUp()
            return true
        } else if isSkillPickerVisible {
            skillPickerView.moveSelectionUp()
            return true
        }
        return false
    }

    func composerDidRequestMoveDown() -> Bool {
        if isAutocompleteVisible {
            autocompleteView.moveSelectionDown()
            return true
        } else if isSkillPickerVisible {
            skillPickerView.moveSelectionDown()
            return true
        }
        return false
    }

    func composerDidRequestConfirmSelection() {
        if isAutocompleteVisible {
            autocompleteView.confirmSelection()
            return
        }
        if isSkillPickerVisible {
            skillPickerView.confirmSelection()
            return
        }
        let text = composerView.inputText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        composerView.inputText = ""
        dismissAutocomplete()
        dismissSkillPicker()

        if let screenshot = pendingScreenshot {
            pendingScreenshot = nil
            headerView.updateSubtitle("Ask, compare, and organize")
            let imageData = screenshot.tiffRepresentation.flatMap { NSBitmapImageRep(data: $0) }
            let base64 = imageData?.representation(using: .png, properties: [:])?.base64EncodedString() ?? ""
            let screenshotContext = base64.isEmpty ? "[Screenshot captured but encoding failed]" : "[Screenshot attached (base64-png, \(screenshot.size.width)x\(screenshot.size.height))]"
            let augmentedText = text + "\n\n" + screenshotContext
            chatHandler.send(augmentedText, responseFormat: selectedResponseFormat)
            return
        }

        if composerView.isPageContextEnabled {
            sendWithPageContext(text)
            return
        }

        sendWithMentions(text)
    }

    func composerDidRequestDismiss() {
        if isAutocompleteVisible {
            dismissAutocomplete()
        } else if isSkillPickerVisible {
            dismissSkillPicker()
        }
    }

    func composerDidRequestStop() {
        isStreamingResponse = false
        chatHandler.cancel()
        composerView.setIdleState()
        composerView.makeInputFirstResponder()
        AppDelegate.writeUITestState([
            "aiPanelLastAction": "stop",
            "aiPanelStreaming": "0"
        ])
    }

    func composerDidRequestCamera() {
        delegate?.aiSidePanelCaptureActiveTab { [weak self] image in
            guard let self, let image else { return }
            self.pendingScreenshot = image
            self.headerView.updateSubtitle("📷 Screenshot attached — will be sent with your next message")
            AppDelegate.writeUITestState([
                "aiPanelLastAction": "camera",
                "aiPanelCameraAttached": "1"
            ])
        }
    }

    func composerDidRequestMicrophone() {
        AppDelegate.writeUITestState([
            "aiPanelLastAction": "microphone",
            "aiPanelMicrophoneActivated": "1"
        ])
        let alert = NSAlert()
        alert.messageText = "Voice Input"
        alert.informativeText = "Voice input is not yet available. This feature is coming in a future update."
        alert.alertStyle = .informational
        alert.addButton(withTitle: "OK")
        if let window = self.window {
            alert.beginSheetModal(for: window)
        } else {
            alert.runModal()
        }
    }

    // MARK: - AIChatHandlerDelegate

    func aiChatHandlerRequiresConfiguration() {
        refreshConfigurationState()
    }

    func didUpdateMessages() {
        chatView.reloadMessages(chatHandler.messages)
        updateSuggestionChips()
        if ProcessInfo.processInfo.arguments.contains("--ui-testing") {
            let assistantMessages = chatHandler.messages.filter { $0.role == .assistant }
            let latestAssistantText = assistantMessages.last?.content ?? ""
            AppDelegate.writeUITestState([
                "aiPanelMessageCount": "\(chatHandler.messages.count)",
                "aiPanelAssistantMessageCount": "\(assistantMessages.count)",
                "aiPanelLatestAssistantText": latestAssistantText
            ])
        }
    }

    func didStartStreaming() {
        isStreamingResponse = true
        composerView.setStreamingState()
        AppDelegate.writeUITestState([
            "aiPanelStreaming": "1",
            "aiPanelLastAction": "streamStart"
        ])
    }

    func didFinishStreaming() {
        isStreamingResponse = false
        composerView.setIdleState()
        composerView.makeInputFirstResponder()
        AppDelegate.writeUITestState([
            "aiPanelStreaming": "0",
            "aiPanelLastAction": "streamFinish"
        ])
    }

    // MARK: - TabMentionAutocompleteDelegate

    func tabMentionDidSelect(title: String) {
        var text = composerView.inputText
        if let atIndex = text.lastIndex(of: "@") {
            text = String(text[..<atIndex]) + "@" + title + " "
        }
        composerView.inputText = text
        dismissAutocomplete()
        composerView.moveInputCursorToEnd()
    }

    func tabMentionDidDismiss() {
        dismissAutocomplete()
    }

    // MARK: - SkillPickerDelegate

    func skillPickerDidSelect(_ skill: SkillInfo) {
        composerView.inputText = skill.slashCommand + " "
        dismissSkillPicker()
        composerView.moveInputCursorToEnd()
    }

    func skillPickerDidDismiss() {
        dismissSkillPicker()
    }

    // MARK: - Autocomplete / Skill Picker coordination

    private func handleMentionAutocomplete(text: String) {
        guard let mentionQuery = extractActiveMentionQuery(from: text) else {
            dismissAutocomplete()
            return
        }

        let allTabs = delegate?.aiSidePanelAllTabs() ?? []
        let query = mentionQuery.lowercased()
        let filtered: [(title: String, url: String)]
        if query.isEmpty {
            filtered = allTabs.map { ($0.title, $0.url) }
        } else {
            filtered = allTabs
                .filter { $0.title.lowercased().contains(query) || $0.url.lowercased().contains(query) }
                .map { ($0.title, $0.url) }
        }

        if filtered.isEmpty {
            dismissAutocomplete()
            return
        }

        autocompleteView.update(tabs: filtered)
        showAutocomplete()
    }

    private func extractActiveMentionQuery(from text: String) -> String? {
        guard let atIndex = text.lastIndex(of: "@") else { return nil }
        let afterAt = text[text.index(after: atIndex)...]
        let query = String(afterAt)
        if query.contains(" ") { return nil }
        return query
    }

    private func showAutocomplete() {
        guard !isShowingOnboarding else { return }
        dismissSkillPicker()
        autocompleteView.isHidden = false
        isAutocompleteVisible = true
        autocompleteHeightConstraint?.constant = autocompleteView.preferredHeight
        layoutSubtreeIfNeeded()
    }

    private func dismissAutocomplete() {
        guard isAutocompleteVisible else { return }
        autocompleteView.isHidden = true
        isAutocompleteVisible = false
        autocompleteHeightConstraint?.constant = 0
        layoutSubtreeIfNeeded()
    }

    private func handleSkillAutocomplete(text: String) {
        let query = String(text.dropFirst())
        let filtered = SkillsStore.shared.filter(query: query)

        if filtered.isEmpty {
            dismissSkillPicker()
            return
        }

        skillPickerView.update(skills: filtered)
        showSkillPicker()
    }

    private func showSkillPicker() {
        guard !isShowingOnboarding else { return }
        dismissAutocomplete()
        skillPickerView.isHidden = false
        isSkillPickerVisible = true
        skillPickerHeightConstraint?.constant = skillPickerView.preferredHeight
        layoutSubtreeIfNeeded()
    }

    private func dismissSkillPicker() {
        guard isSkillPickerVisible else { return }
        skillPickerView.isHidden = true
        isSkillPickerVisible = false
        skillPickerHeightConstraint?.constant = 0
        layoutSubtreeIfNeeded()
    }

    // MARK: - Send logic

    private func sendWithMentions(_ text: String) {
        if let (skill, userInput) = parseSkillCommand(from: text) {
            chatHandler.sendWithSkill(skill, userInput: userInput, responseFormat: selectedResponseFormat)
            persistUITestSendState(inputText: text)
            return
        }

        let allTabs = delegate?.aiSidePanelAllTabs() ?? []
        let mentionedTitles = parseMentions(from: text)

        var mentionedTabRefs: [TabReference] = []
        for mention in mentionedTitles {
            let mentionLower = mention.lowercased()
            if let match = allTabs.first(where: { $0.title.lowercased() == mentionLower }) {
                mentionedTabRefs.append(match)
            } else if let match = allTabs.first(where: { $0.title.lowercased().contains(mentionLower) }) {
                mentionedTabRefs.append(match)
            }
        }

        if mentionedTabRefs.isEmpty {
            chatHandler.send(text, responseFormat: selectedResponseFormat)
            persistUITestSendState(inputText: text)
            return
        }

        let group = DispatchGroup()
        var mentionedContexts: [PageContext] = []
        let lock = NSLock()

        for tabRef in mentionedTabRefs {
            group.enter()
            PageContextExtractor.extractText(from: tabRef.webView) { context in
                lock.lock()
                mentionedContexts.append(context)
                lock.unlock()
                group.leave()
            }
        }

        group.notify(queue: .main) { [weak self] in
            self?.chatHandler.sendWithReferencedPages(text, referencedPages: mentionedContexts, responseFormat: self?.selectedResponseFormat ?? "Auto")
            self?.persistUITestSendState(inputText: text)
        }
    }

    private func sendWithPageContext(_ text: String) {
        guard let tab = delegate?.aiSidePanelAllTabs().first else {
            composerView.resetPageContext()
            sendWithMentions(text)
            return
        }

        PageContextExtractor.extractRichContext(from: tab.webView) { [weak self] context in
            guard let self else { return }
            self.chatHandler.sendWithReferencedPages(text, referencedPages: [context], responseFormat: self.selectedResponseFormat)
            self.composerView.resetPageContext()
            self.persistUITestSendState(inputText: text)
        }
    }

    private func persistUITestSendState(inputText: String) {
        guard ProcessInfo.processInfo.arguments.contains("--ui-testing") else { return }
        AppDelegate.writeUITestState([
            "aiPanelLastAction": "send",
            "aiPanelLastInput": inputText,
            "aiPanelMessageCount": "\(chatHandler.messages.count)",
            "aiPanelAssistantMessageCount": "\(chatHandler.messages.filter { $0.role == .assistant }.count)",
            "aiPanelLatestAssistantText": chatHandler.messages.last(where: { $0.role == .assistant })?.content ?? "",
            "aiPanelStreaming": isStreamingResponse ? "1" : "0"
        ])
    }

    private func parseSkillCommand(from text: String) -> (SkillInfo, String)? {
        guard text.hasPrefix("/") else { return nil }
        let allSkills = SkillsStore.shared.filter(query: "")
        for skill in allSkills {
            let command = skill.slashCommand
            if text == command || text.hasPrefix(command + " ") {
                let userInput = String(text.dropFirst(command.count)).trimmingCharacters(in: .whitespaces)
                return (skill, userInput)
            }
        }
        return nil
    }

    private func parseMentions(from text: String) -> [String] {
        let pattern = "@([^@\\s][^@]*?)(?=\\s|$|@)"
        guard let regex = try? NSRegularExpression(pattern: pattern) else { return [] }
        let range = NSRange(text.startIndex..., in: text)
        let matches = regex.matches(in: text, range: range)
        return matches.compactMap { match -> String? in
            guard let captureRange = Range(match.range(at: 1), in: text) else { return nil }
            return String(text[captureRange]).trimmingCharacters(in: .whitespaces)
        }
    }
}
