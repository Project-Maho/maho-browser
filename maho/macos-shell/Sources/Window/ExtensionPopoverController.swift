import AppKit

extension Notification.Name {
    static let unpinExtension = Notification.Name("maho.unpinExtension")
    static let pinExtension = Notification.Name("maho.pinExtension")
}

final class ExtensionPopoverController: NSPopover {
    private let extensionId: String

    init(extensionId: String) {
        self.extensionId = extensionId
        super.init()
        behavior = .transient
        animates = true

        let container = NSView(frame: NSRect(x: 0, y: 0, width: 250, height: 150))
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(stack)

        let titleLabel = NSTextField(labelWithString: "Extension: \(extensionId)")
        titleLabel.font = .systemFont(ofSize: 14, weight: .semibold)
        stack.addArrangedSubview(titleLabel)

        let disableButton = NSButton(title: "Disable Extension", target: self, action: #selector(disableClicked(_:)))
        let unpinButton = NSButton(title: "Unpin from Toolbar", target: self, action: #selector(unpinClicked(_:)))
        stack.addArrangedSubview(disableButton)
        stack.addArrangedSubview(unpinButton)

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 16),
            stack.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -16),
            stack.topAnchor.constraint(equalTo: container.topAnchor, constant: 16),
        ])

        let vc = NSViewController()
        vc.view = container
        contentViewController = vc
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    @objc private func disableClicked(_ sender: Any) {
        CoreUpdateDispatcher.shared.sendAndDispatch(.toggleExtension(extensionId: extensionId))
        performClose(sender)
    }

    @objc private func unpinClicked(_ sender: Any) {
        NotificationCenter.default.post(name: .unpinExtension, object: nil, userInfo: ["extensionId": extensionId])
        performClose(sender)
    }
}
