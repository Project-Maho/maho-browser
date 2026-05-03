import AppKit

final class PermissionSheetController: NSViewController {
    private let originLabel = NSTextField(labelWithString: "")
    private let typeLabel = NSTextField(labelWithString: "")
    private let messageLabel = NSTextField(labelWithString: "")
    private let allowButton = NSButton(title: "Allow", target: nil, action: nil)
    private let denyButton = NSButton(title: "Deny", target: nil, action: nil)

    private var request: PermissionRequest?

    override func loadView() {
        let container = NSView()
        container.translatesAutoresizingMaskIntoConstraints = false
        self.view = container

        originLabel.translatesAutoresizingMaskIntoConstraints = false
        originLabel.font = NSFont.systemFont(ofSize: 13, weight: .semibold)
        originLabel.textColor = MahoAppColors.sidebarText
        container.addSubview(originLabel)

        typeLabel.translatesAutoresizingMaskIntoConstraints = false
        typeLabel.font = NSFont.systemFont(ofSize: 12, weight: .medium)
        typeLabel.textColor = MahoAppColors.sidebarSecondaryText
        container.addSubview(typeLabel)

        messageLabel.translatesAutoresizingMaskIntoConstraints = false
        messageLabel.font = NSFont.systemFont(ofSize: 12)
        messageLabel.textColor = MahoAppColors.sidebarText
        messageLabel.maximumNumberOfLines = 0
        messageLabel.preferredMaxLayoutWidth = 280
        container.addSubview(messageLabel)

        allowButton.translatesAutoresizingMaskIntoConstraints = false
        allowButton.bezelStyle = .rounded
        allowButton.setButtonType(.momentaryPushIn)
        allowButton.target = self
        allowButton.action = #selector(allowClicked(_:))
        container.addSubview(allowButton)

        denyButton.translatesAutoresizingMaskIntoConstraints = false
        denyButton.bezelStyle = .rounded
        denyButton.setButtonType(.momentaryPushIn)
        denyButton.target = self
        denyButton.action = #selector(denyClicked(_:))
        container.addSubview(denyButton)

        NSLayoutConstraint.activate([
            originLabel.topAnchor.constraint(equalTo: container.topAnchor, constant: 20),
            originLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 20),
            originLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -20),

            typeLabel.topAnchor.constraint(equalTo: originLabel.bottomAnchor, constant: 4),
            typeLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 20),
            typeLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -20),

            messageLabel.topAnchor.constraint(equalTo: typeLabel.bottomAnchor, constant: 12),
            messageLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 20),
            messageLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -20),

            allowButton.topAnchor.constraint(equalTo: messageLabel.bottomAnchor, constant: 20),
            allowButton.trailingAnchor.constraint(equalTo: container.centerXAnchor, constant: -8),
            allowButton.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -20),

            denyButton.topAnchor.constraint(equalTo: messageLabel.bottomAnchor, constant: 20),
            denyButton.leadingAnchor.constraint(equalTo: container.centerXAnchor, constant: 8),
            denyButton.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -20),
        ])
    }

    func configure(request: PermissionRequest) {
        self.request = request
        originLabel.stringValue = request.origin
        typeLabel.stringValue = request.permission
        messageLabel.stringValue = request.message
    }

    @objc private func allowClicked(_ sender: NSButton) {
        guard let request = request else {
            closeSheet(nil)
            return
        }
        CoreUpdateDispatcher.shared.sendAndDispatch(.grantPermission(origin: request.origin, permission: request.permission))
        MahoBridge.shared.grantPermission(origin: request.origin, permission: request.permission)
        closeSheet(nil)
    }

    @objc private func denyClicked(_ sender: NSButton) {
        guard let request = request else {
            closeSheet(nil)
            return
        }
        CoreUpdateDispatcher.shared.sendAndDispatch(.revokePermission(origin: request.origin, permission: request.permission))
        MahoBridge.shared.revokePermission(origin: request.origin, permission: request.permission)
        closeSheet(nil)
    }

    private func closeSheet(_ sender: Any?) {
        presentingViewController?.dismiss(self)
        view.window?.close()
    }
}
