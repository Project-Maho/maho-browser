import AppKit

final class AppearanceSettingsViewController: NSViewController, CoreUpdateObserver, NSTextViewDelegate {

    private var themeControl = NSSegmentedControl(labels: ["Light", "Dark", "System"], trackingMode: .selectOne, target: nil, action: nil)
    private var densityControl = NSSegmentedControl(labels: ["Compact", "Comfortable"], trackingMode: .selectOne, target: nil, action: nil)
    private var sidebarWidthSlider = NSSlider()
    private var transparencySwitch = NSSwitch()
    private var cssTextView = NSTextView()
    private var appIconButton = NSButton(title: "Change App Icon...", target: nil, action: nil)
    private var sidebarWidthDebounceTimer: Timer?

    private let accentColors: [Color] = [
        Color(r: 0, g: 122, b: 255, a: 1.0),
        Color(r: 255, g: 59, b: 48, a: 1.0),
        Color(r: 52, g: 199, b: 89, a: 1.0),
        Color(r: 255, g: 204, b: 0, a: 1.0),
        Color(r: 175, g: 82, b: 222, a: 1.0),
        Color(r: 255, g: 149, b: 0, a: 1.0)
    ]
    private var accentColorButtons: [NSButton] = []

    override func loadView() {
        themeControl.target = self
        themeControl.action = #selector(themeChanged(_:))
        themeControl.setAccessibilityIdentifier("settings.appearance.theme")
        let themeRow = makeSettingsLabeledRow("Theme:", themeControl)

        let accentStack = NSStackView()
        accentStack.orientation = .horizontal
        accentStack.spacing = 8
        for (index, color) in accentColors.enumerated() {
            let button = NSButton()
            button.bezelStyle = .circular
            button.setButtonType(.momentaryPushIn)
            button.target = self
            button.action = #selector(accentColorChanged(_:))
            button.wantsLayer = true
            if let layer = button.layer {
                layer.backgroundColor = NSColor(
                    red: CGFloat(color.r) / 255.0,
                    green: CGFloat(color.g) / 255.0,
                    blue: CGFloat(color.b) / 255.0,
                    alpha: CGFloat(color.a)
                ).cgColor
                layer.cornerRadius = 10
            }
            button.widthAnchor.constraint(equalToConstant: 20).isActive = true
            button.heightAnchor.constraint(equalToConstant: 20).isActive = true
            accentColorButtons.append(button)
            button.setAccessibilityIdentifier("settings.appearance.accentColor.\(index)")
            accentStack.addArrangedSubview(button)
        }
        let accentRow = makeSettingsLabeledRow("Accent Color:", accentStack)

        densityControl.target = self
        densityControl.action = #selector(densityChanged(_:))
        densityControl.setAccessibilityIdentifier("settings.appearance.density")
        let densityRow = makeSettingsLabeledRow("Density:", densityControl)

        sidebarWidthSlider.minValue = 180
        sidebarWidthSlider.maxValue = 400
        sidebarWidthSlider.numberOfTickMarks = 0
        sidebarWidthSlider.target = self
        sidebarWidthSlider.action = #selector(sidebarWidthChanged(_:))
        sidebarWidthSlider.setAccessibilityIdentifier("settings.appearance.sidebarWidth")
        let widthRow = makeSettingsLabeledRow("Sidebar Width:", sidebarWidthSlider)

        transparencySwitch.target = self
        transparencySwitch.action = #selector(transparencyChanged(_:))
        transparencySwitch.setAccessibilityIdentifier("settings.appearance.transparency")
        let transparencyRow = makeSettingsLabeledRow("Window Transparency:", transparencySwitch)

        appIconButton.target = self
        appIconButton.action = #selector(changeAppIconClicked(_:))
        appIconButton.setAccessibilityIdentifier("settings.appearance.appIcon")
        let appIconRow = makeSettingsLabeledRow("App Icon:", appIconButton)

        let scrollView = NSScrollView()
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true
        styleSettingsScrollView(scrollView)
        cssTextView.isRichText = false
        cssTextView.font = NSFont.userFixedPitchFont(ofSize: 12)
        cssTextView.textColor = MahoAppColors.primaryText
        cssTextView.backgroundColor = .clear
        cssTextView.delegate = self
        cssTextView.setAccessibilityIdentifier("settings.appearance.chromeCss")
        scrollView.documentView = cssTextView
        scrollView.heightAnchor.constraint(equalToConstant: 80).isActive = true
        let cssRow = makeSettingsLabeledRow("Custom Chrome CSS:", scrollView)

        view = SettingsPaneHostView(sections: [
            .card([
                themeRow,
                accentRow,
                densityRow,
            ]),
            .card([
                widthRow,
                transparencyRow,
                appIconRow,
                cssRow,
            ]),
        ])
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        hydrateFromSettings()
    }

    deinit {
        sidebarWidthDebounceTimer?.invalidate()
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        if case .settingsChanged(let settings) = update {
            hydrateFromSettings(settings)
        }
    }

    private func hydrateFromSettings(_ settings: Settings? = nil) {
        let s = settings ?? MahoBridge.shared.getSettings()
        guard let s = s else { return }

        switch s.appearance.theme {
        case .light: themeControl.selectedSegment = 0
        case .dark: themeControl.selectedSegment = 1
        case .system: themeControl.selectedSegment = 2
        }

        switch s.appearance.density {
        case .compact: densityControl.selectedSegment = 0
        case .comfortable: densityControl.selectedSegment = 1
        }

        sidebarWidthSlider.doubleValue = s.appearance.sidebarWidth
        transparencySwitch.state = s.appearance.windowTransparency ? .on : .off
        cssTextView.string = s.appearance.customChromeCss ?? ""

        for (i, button) in accentColorButtons.enumerated() {
            if i < accentColors.count, let accent = s.appearance.accentColor {
                let c = accentColors[i]
                let match = c.r == accent.r
                    && c.g == accent.g
                    && c.b == accent.b
                button.layer?.borderWidth = match ? 2 : 0
                button.layer?.borderColor = match ? NSColor.white.cgColor : nil
            }
        }
    }

    @objc private func themeChanged(_ sender: NSSegmentedControl) {
        let theme: Theme
        switch sender.selectedSegment {
        case 0: theme = .light
        case 1: theme = .dark
        default: theme = .system
        }
        let update = SettingsUpdate(
            appearance: AppearanceSettingsUpdate(theme: theme)
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func accentColorChanged(_ sender: NSButton) {
        guard let index = accentColorButtons.firstIndex(of: sender) else { return }
        let color = accentColors[index]
        guard let json = MahoBridge.shared.encode(color) else { return }
        MahoBridge.shared.setAccentColor(colorJson: json)
        let update = SettingsUpdate(
            appearance: AppearanceSettingsUpdate(accentColor: color)
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func densityChanged(_ sender: NSSegmentedControl) {
        let density: Density
        switch sender.selectedSegment {
        case 0: density = .compact
        default: density = .comfortable
        }
        MahoBridge.shared.setDensity(density: density.rawValue)
        let update = SettingsUpdate(
            appearance: AppearanceSettingsUpdate(density: density)
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func sidebarWidthChanged(_ sender: NSSlider) {
        sidebarWidthDebounceTimer?.invalidate()
        sidebarWidthDebounceTimer = Timer.scheduledTimer(withTimeInterval: 0.15, repeats: false) { [weak self] _ in
            guard let self = self else { return }
            let value = sender.doubleValue
            let update = SettingsUpdate(
                appearance: AppearanceSettingsUpdate(sidebarWidth: value)
            )
            CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
        }
    }

    @objc private func transparencyChanged(_ sender: NSSwitch) {
        MahoBridge.shared.setWindowTransparency(enabled: sender.state == .on)
        let update = SettingsUpdate(
            appearance: AppearanceSettingsUpdate(windowTransparency: sender.state == .on)
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func changeAppIconClicked(_ sender: NSButton) {
        let controller = IconPickerController()
        presentAsSheet(controller)
    }

    func textDidEndEditing(_ notification: Notification) {
        let css = cssTextView.string.isEmpty ? nil : cssTextView.string
        let update = SettingsUpdate(
            appearance: AppearanceSettingsUpdate(customChromeCss: css)
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }
}
