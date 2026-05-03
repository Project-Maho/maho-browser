import AppKit

final class WhatsNewBannerView: NSView {
    private let titleLabel = NSTextField(labelWithString: "")
    private let bulletLabel = NSTextField(wrappingLabelWithString: "")
    private let dismissButton = NSButton(title: "✕", target: nil, action: nil)
    private let learnMoreButton = NSButton(title: "Learn More", target: nil, action: nil)

    var onDismiss: (() -> Void)?

    private static let dismissKeyPrefix = "mahoWhatsNewDismissed_"
    private static let dismissTimestampKeyPrefix = "mahoWhatsNewDismissedAt_"

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
    }

    private func setupUI() {
        wantsLayer = true
        layer?.backgroundColor = MahoAppColors.toolbarBackground.cgColor
        layer?.cornerRadius = 10
        layer?.borderWidth = 1
        layer?.borderColor = MahoAppColors.cardBorder.withAlphaComponent(0.3).cgColor

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 13, weight: .semibold)
        titleLabel.textColor = MahoAppColors.primaryText
        addSubview(titleLabel)

        bulletLabel.translatesAutoresizingMaskIntoConstraints = false
        bulletLabel.font = .systemFont(ofSize: 11)
        bulletLabel.textColor = MahoAppColors.secondaryText
        bulletLabel.maximumNumberOfLines = 5
        bulletLabel.lineBreakMode = .byWordWrapping
        addSubview(bulletLabel)

        dismissButton.translatesAutoresizingMaskIntoConstraints = false
        dismissButton.isBordered = false
        dismissButton.font = .systemFont(ofSize: 14, weight: .medium)
        dismissButton.contentTintColor = MahoAppColors.secondaryText
        dismissButton.target = self
        dismissButton.action = #selector(dismissClicked(_:))
        addSubview(dismissButton)

        learnMoreButton.translatesAutoresizingMaskIntoConstraints = false
        learnMoreButton.bezelStyle = .rounded
        learnMoreButton.font = .systemFont(ofSize: 11)
        learnMoreButton.target = self
        learnMoreButton.action = #selector(learnMoreClicked(_:))
        addSubview(learnMoreButton)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: topAnchor, constant: 10),
            titleLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            titleLabel.trailingAnchor.constraint(lessThanOrEqualTo: dismissButton.leadingAnchor, constant: -8),

            dismissButton.topAnchor.constraint(equalTo: topAnchor, constant: 6),
            dismissButton.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -8),
            dismissButton.widthAnchor.constraint(equalToConstant: 20),
            dismissButton.heightAnchor.constraint(equalToConstant: 20),

            bulletLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 4),
            bulletLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            bulletLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),

            learnMoreButton.topAnchor.constraint(equalTo: bulletLabel.bottomAnchor, constant: 8),
            learnMoreButton.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            learnMoreButton.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -10),
        ])
    }

    func configure(version: String, features: [String]) {
        titleLabel.stringValue = "What's New in Maho \(version)"
        bulletLabel.stringValue = features.map { "• \($0)" }.joined(separator: "\n")
    }

    @objc private func dismissClicked(_ sender: NSButton) {
        let version = currentAppVersion()
        UserDefaults.standard.set(true, forKey: Self.dismissKeyPrefix + version)
        UserDefaults.standard.set(Date().timeIntervalSince1970, forKey: Self.dismissTimestampKeyPrefix + version)
        onDismiss?()
        removeFromSuperview()
    }

    @objc private func learnMoreClicked(_ sender: NSButton) {
        if let url = URL(string: "https://maho.dev/changelog") {
            NSWorkspace.shared.open(url)
        }
    }

    static func shouldShow() -> Bool {
        let version = currentAppVersion()
        let lastVersion = UserDefaults.standard.string(forKey: "mahoLastKnownVersion") ?? ""

        if lastVersion == version {
            if UserDefaults.standard.bool(forKey: dismissKeyPrefix + version) {
                return false
            }
            if let timestamp = UserDefaults.standard.object(forKey: dismissTimestampKeyPrefix + version) as? TimeInterval {
                let threeDaysAgo = Date().timeIntervalSince1970 - (3 * 24 * 60 * 60)
                if timestamp < threeDaysAgo {
                    return false
                }
            }
        }

        return lastVersion != version
    }

    static func markVersionSeen() {
        let version = currentAppVersion()
        UserDefaults.standard.set(version, forKey: "mahoLastKnownVersion")
    }
}

private func currentAppVersion() -> String {
    return Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0"
}
