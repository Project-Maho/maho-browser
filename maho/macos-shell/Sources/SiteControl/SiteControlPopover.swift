import AppKit

final class SiteControlPopover: NSPopover {
    private let domainLabel = NSTextField(labelWithString: "")
    private let zoomSlider = NSSlider(value: 1.0, minValue: 0.5, maxValue: 2.0, target: nil, action: nil)
    private let zoomValueLabel = NSTextField(labelWithString: "100%")
    private var tabId: TabId?

    private let cameraSwitch = NSSwitch()
    private let microphoneSwitch = NSSwitch()
    private let locationSwitch = NSSwitch()
    private let notificationsSwitch = NSSwitch()
    private let contentBlockerSwitch = NSSwitch()
    private let blockedCountLabel = NSTextField(labelWithString: "")
    private let contentBlockerStatusLabel = NSTextField(labelWithString: "")
    private var extensionToggles: [String: NSSwitch] = [:]
    private var extensionsStack: NSStackView?

    override init() {
        super.init()
        setupContent()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupContent()
    }

    private func setupContent() {
        behavior = .transient
        animates = true

        let viewController = NSViewController()
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 280, height: 320))
        viewController.view = container

        let stack = NSStackView()
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        container.addSubview(stack)

        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: container.topAnchor, constant: 16),
            stack.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 16),
            stack.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -16),
            stack.bottomAnchor.constraint(lessThanOrEqualTo: container.bottomAnchor, constant: -16),
        ])

        domainLabel.font = .systemFont(ofSize: 14, weight: .semibold)
        domainLabel.textColor = MahoAppColors.primaryText
        stack.addArrangedSubview(domainLabel)

        let separator = NSBox()
        separator.boxType = .separator
        separator.translatesAutoresizingMaskIntoConstraints = false
        separator.widthAnchor.constraint(equalToConstant: 248).isActive = true
        stack.addArrangedSubview(separator)

        let zoomLabel = NSTextField(labelWithString: "Zoom")
        zoomLabel.font = .systemFont(ofSize: 12, weight: .medium)
        zoomLabel.textColor = MahoAppColors.primaryText
        stack.addArrangedSubview(zoomLabel)

        zoomSlider.translatesAutoresizingMaskIntoConstraints = false
        zoomSlider.target = self
        zoomSlider.action = #selector(zoomChanged(_:))
        zoomSlider.isContinuous = true
        zoomValueLabel.font = .systemFont(ofSize: 11)
        zoomValueLabel.textColor = MahoAppColors.secondaryText
        let zoomRow = NSStackView(views: [zoomSlider, zoomValueLabel])
        zoomRow.orientation = .horizontal
        zoomRow.spacing = 8
        zoomSlider.widthAnchor.constraint(equalToConstant: 180).isActive = true
        stack.addArrangedSubview(zoomRow)

        let permSeparator = NSBox()
        permSeparator.boxType = .separator
        permSeparator.translatesAutoresizingMaskIntoConstraints = false
        permSeparator.widthAnchor.constraint(equalToConstant: 248).isActive = true
        stack.addArrangedSubview(permSeparator)

        let permLabel = NSTextField(labelWithString: "Permissions")
        permLabel.font = .systemFont(ofSize: 12, weight: .medium)
        permLabel.textColor = MahoAppColors.primaryText
        stack.addArrangedSubview(permLabel)

        contentBlockerSwitch.state = .on
        let cbRow = makePermissionRow("Content Blocker", toggle: contentBlockerSwitch, action: #selector(contentBlockerToggled(_:)))
        stack.addArrangedSubview(cbRow)

        blockedCountLabel.font = .systemFont(ofSize: 11)
        blockedCountLabel.textColor = MahoAppColors.secondaryText
        stack.addArrangedSubview(blockedCountLabel)

        contentBlockerStatusLabel.font = .systemFont(ofSize: 11)
        contentBlockerStatusLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.70)
        contentBlockerStatusLabel.isHidden = true
        stack.addArrangedSubview(contentBlockerStatusLabel)

        stack.addArrangedSubview(makePermissionRow("Camera", toggle: cameraSwitch, action: #selector(cameraToggled(_:))))
        stack.addArrangedSubview(makePermissionRow("Microphone", toggle: microphoneSwitch, action: #selector(microphoneToggled(_:))))
        stack.addArrangedSubview(makePermissionRow("Location", toggle: locationSwitch, action: #selector(locationToggled(_:))))
        stack.addArrangedSubview(makePermissionRow("Notifications", toggle: notificationsSwitch, action: #selector(notificationsToggled(_:))))

        // Extensions section
        let extSeparator = NSBox()
        extSeparator.boxType = .separator
        extSeparator.translatesAutoresizingMaskIntoConstraints = false
        extSeparator.widthAnchor.constraint(equalToConstant: 248).isActive = true
        stack.addArrangedSubview(extSeparator)

        let extLabel = NSTextField(labelWithString: "Extensions")
        extLabel.font = .systemFont(ofSize: 12, weight: .medium)
        extLabel.textColor = MahoAppColors.primaryText
        stack.addArrangedSubview(extLabel)

        let extStack = NSStackView()
        extStack.orientation = .vertical
        extStack.alignment = .leading
        extStack.spacing = 6
        extensionsStack = extStack
        stack.addArrangedSubview(extStack)

        contentViewController = viewController
    }

    private func makePermissionRow(_ title: String, toggle: NSSwitch, action: Selector) -> NSStackView {
        let label = NSTextField(labelWithString: title)
        label.font = .systemFont(ofSize: 12)
        label.textColor = MahoAppColors.primaryText
        toggle.target = self
        toggle.action = action
        toggle.controlSize = .small
        let row = NSStackView(views: [label, toggle])
        row.orientation = .horizontal
        row.spacing = 8
        label.setContentHuggingPriority(.defaultLow, for: .horizontal)
        return row
    }

    func configure(domain: String, tabId: TabId?, extensions: [ExtensionViewModel] = []) {
        self.tabId = tabId
        domainLabel.stringValue = domain.isEmpty ? "No site" : domain

        let ruleCount = MahoBridge.shared.getContentRuleCount()
        blockedCountLabel.stringValue = "\(ruleCount) content rules active"

        let exceptions = UserDefaults.standard.stringArray(forKey: "maho_content_blocker_exceptions") ?? []
        if exceptions.contains(domain) {
            contentBlockerSwitch.state = .off
            contentBlockerStatusLabel.stringValue = "Content blocker disabled for this site"
            contentBlockerStatusLabel.isHidden = false
        } else {
            contentBlockerSwitch.state = .on
            contentBlockerStatusLabel.isHidden = true
        }

        extensionsStack?.arrangedSubviews.forEach { $0.removeFromSuperview() }
        extensionToggles.removeAll()

        let disabledKey = "maho_disabled_extensions_\(domain)"
        let disabledIds = UserDefaults.standard.stringArray(forKey: disabledKey) ?? []

        for ext in extensions where ext.enabled {
            let toggle = NSSwitch()
            toggle.controlSize = .small
            toggle.state = disabledIds.contains(ext.id) ? .off : .on
            toggle.target = self
            toggle.action = #selector(extensionSiteToggled(_:))
            extensionToggles[ext.id] = toggle

            let label = NSTextField(labelWithString: ext.name)
            label.font = .systemFont(ofSize: 12)
            label.textColor = MahoAppColors.primaryText
            label.setContentHuggingPriority(.defaultLow, for: .horizontal)

            let row = NSStackView(views: [label, toggle])
            row.orientation = .horizontal
            row.spacing = 8
            extensionsStack?.addArrangedSubview(row)
        }

        if extensions.filter({ $0.enabled }).isEmpty {
            let noExtLabel = NSTextField(labelWithString: "No extensions active")
            noExtLabel.font = .systemFont(ofSize: 11)
            noExtLabel.textColor = MahoAppColors.secondaryText
            extensionsStack?.addArrangedSubview(noExtLabel)
        }
    }

    @objc private func extensionSiteToggled(_ sender: NSSwitch) {
        let domain = domainLabel.stringValue
        guard !domain.isEmpty, domain != "No site" else { return }
        guard let extId = extensionToggles.first(where: { $0.value === sender })?.key else { return }

        let disabledKey = "maho_disabled_extensions_\(domain)"
        var disabledIds = UserDefaults.standard.stringArray(forKey: disabledKey) ?? []
        if sender.state == .off {
            if !disabledIds.contains(extId) { disabledIds.append(extId) }
        } else {
            disabledIds.removeAll { $0 == extId }
        }
        UserDefaults.standard.set(disabledIds, forKey: disabledKey)
    }

    @objc private func zoomChanged(_ sender: NSSlider) {
        guard let tabId = tabId else { return }
        let level = sender.doubleValue
        zoomValueLabel.stringValue = "\(Int(level * 100))%"
        CoreUpdateDispatcher.shared.sendAndDispatch(.setZoom(tabId: tabId, zoomLevel: level))
    }

    @objc private func contentBlockerToggled(_ sender: NSSwitch) {
        let isEnabled = sender.state == .on
        CoreUpdateDispatcher.shared.sendAndDispatch(.toggleContentBlocker(enabled: isEnabled))
        if isEnabled {
            contentBlockerStatusLabel.isHidden = true
        } else {
            contentBlockerStatusLabel.stringValue = "Content blocker disabled for this site"
            contentBlockerStatusLabel.isHidden = false
        }
    }

    @objc private func cameraToggled(_ sender: NSSwitch) {
        sendPermission("camera", granted: sender.state == .on)
    }

    @objc private func microphoneToggled(_ sender: NSSwitch) {
        sendPermission("microphone", granted: sender.state == .on)
    }

    @objc private func locationToggled(_ sender: NSSwitch) {
        sendPermission("location", granted: sender.state == .on)
    }

    @objc private func notificationsToggled(_ sender: NSSwitch) {
        sendPermission("notifications", granted: sender.state == .on)
    }

    private func sendPermission(_ permission: String, granted: Bool) {
        let origin = domainLabel.stringValue
        guard !origin.isEmpty, origin != "No site" else { return }
        if granted {
            CoreUpdateDispatcher.shared.sendAndDispatch(.grantPermission(origin: origin, permission: permission))
        } else {
            CoreUpdateDispatcher.shared.sendAndDispatch(.revokePermission(origin: origin, permission: permission))
        }
    }
}
