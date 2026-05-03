import AppKit

final class NotificationBannerView: NSView {
    private let titleLabel = NSTextField(labelWithString: "")
    private let messageLabel = NSTextField(labelWithString: "")
    private let dismissButton = NSButton(title: "×", target: nil, action: nil)
    private var actionButtons: [NSButton] = []
    private var dismissTimer: Timer?

    private var notificationId: String?
    private var actions: [NotificationAction] = []

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
    }

    deinit {
        dismissTimer?.invalidate()
    }

    private func setupUI() {
        wantsLayer = true
        layer?.backgroundColor = MahoAppColors.toolbarBackground.cgColor
        layer?.cornerRadius = 8
        layer?.shadowOpacity = 0.3
        layer?.shadowRadius = 4
        layer?.shadowOffset = CGSize(width: 0, height: 2)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = NSFont.systemFont(ofSize: 14, weight: .semibold)
        titleLabel.textColor = MahoAppColors.sidebarText
        addSubview(titleLabel)

        messageLabel.translatesAutoresizingMaskIntoConstraints = false
        messageLabel.font = NSFont.systemFont(ofSize: 12)
        messageLabel.textColor = MahoAppColors.sidebarSecondaryText
        messageLabel.maximumNumberOfLines = 3
        messageLabel.lineBreakMode = .byTruncatingTail
        addSubview(messageLabel)

        dismissButton.translatesAutoresizingMaskIntoConstraints = false
        dismissButton.bezelStyle = .rounded
        dismissButton.setButtonType(.momentaryPushIn)
        dismissButton.font = NSFont.systemFont(ofSize: 14, weight: .semibold)
        dismissButton.target = self
        dismissButton.action = #selector(dismissClicked(_:))
        addSubview(dismissButton)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: topAnchor, constant: 12),
            titleLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            titleLabel.trailingAnchor.constraint(lessThanOrEqualTo: dismissButton.leadingAnchor, constant: -8),

            dismissButton.topAnchor.constraint(equalTo: topAnchor, constant: 8),
            dismissButton.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -8),

            messageLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 4),
            messageLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            messageLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),

            widthAnchor.constraint(equalToConstant: 320),
        ])
    }

    func configure(notification: NotificationViewModel) {
        self.notificationId = notification.id
        self.actions = notification.actions

        titleLabel.stringValue = notification.title
        messageLabel.stringValue = notification.message

        for button in actionButtons {
            button.removeFromSuperview()
        }
        actionButtons.removeAll()

        var lastAnchor = messageLabel.bottomAnchor
        for action in notification.actions {
            let button = NSButton(title: action.label, target: self, action: #selector(actionButtonClicked(_:)))
            button.translatesAutoresizingMaskIntoConstraints = false
            button.bezelStyle = .rounded
            button.setButtonType(.momentaryPushIn)
            button.tag = actionButtons.count
            actionButtons.append(button)
            addSubview(button)

            NSLayoutConstraint.activate([
                button.topAnchor.constraint(equalTo: lastAnchor, constant: 8),
                button.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            ])
            lastAnchor = button.bottomAnchor
        }

        NSLayoutConstraint.activate([
            lastAnchor.constraint(equalTo: bottomAnchor, constant: -12),
        ])

        dismissTimer?.invalidate()
        dismissTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: false) { [weak self] _ in
            self?.dismiss()
        }
    }

    @objc private func dismissClicked(_ sender: NSButton) {
        dismiss()
    }

    @objc private func actionButtonClicked(_ sender: NSButton) {
        guard let notificationId = notificationId else { return }
        let index = sender.tag
        if index >= 0, index < actions.count {
            let action = actions[index]
            CoreUpdateDispatcher.shared.sendAndDispatch(.notificationAction(notificationId: notificationId, actionId: action.id))
        }
        dismiss()
    }

    private func dismiss() {
        dismissTimer?.invalidate()
        dismissTimer = nil
        guard let notificationId = notificationId else {
            removeFromSuperview()
            return
        }
        CoreUpdateDispatcher.shared.sendAndDispatch(.dismissNotification(notificationId: notificationId))
        removeFromSuperview()
    }
}
