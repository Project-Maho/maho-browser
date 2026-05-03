import AppKit

private struct BrowserProfile: Codable {
    let browser: String
    let profileName: String
    let profilePath: String
}

final class BrowserImportController: NSViewController {

    private let titleLabel = NSTextField(labelWithString: "Import Browser Data")
    private let browserStackView = NSStackView()
    private let importButton = NSButton(title: "Import", target: nil, action: nil)
    private let closeButton = NSButton(title: "Close", target: nil, action: nil)
    private let progressIndicator = NSProgressIndicator()
    private let statusLabel = NSTextField(labelWithString: "")

    private var profiles: [BrowserProfile] = []
    private var checkboxes: [NSButton] = []

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 420, height: 340))

        titleLabel.font = .boldSystemFont(ofSize: 16)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(titleLabel)

        browserStackView.orientation = .vertical
        browserStackView.alignment = .leading
        browserStackView.spacing = 6
        browserStackView.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(browserStackView)

        importButton.target = self
        importButton.action = #selector(importClicked(_:))
        importButton.translatesAutoresizingMaskIntoConstraints = false
        importButton.bezelStyle = .rounded
        importButton.keyEquivalent = "\r"
        container.addSubview(importButton)

        closeButton.target = self
        closeButton.action = #selector(closeClicked(_:))
        closeButton.translatesAutoresizingMaskIntoConstraints = false
        closeButton.bezelStyle = .rounded
        closeButton.keyEquivalent = "\u{1b}"
        container.addSubview(closeButton)

        progressIndicator.style = .spinning
        progressIndicator.isDisplayedWhenStopped = false
        progressIndicator.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(progressIndicator)

        statusLabel.font = .systemFont(ofSize: 12)
        statusLabel.textColor = .secondaryLabelColor
        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        statusLabel.alignment = .center
        container.addSubview(statusLabel)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: container.topAnchor, constant: 20),
            titleLabel.centerXAnchor.constraint(equalTo: container.centerXAnchor),

            browserStackView.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 16),
            browserStackView.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 30),
            browserStackView.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -30),

            progressIndicator.centerXAnchor.constraint(equalTo: container.centerXAnchor),
            progressIndicator.bottomAnchor.constraint(equalTo: statusLabel.topAnchor, constant: -8),

            statusLabel.centerXAnchor.constraint(equalTo: container.centerXAnchor),
            statusLabel.bottomAnchor.constraint(equalTo: importButton.topAnchor, constant: -12),

            importButton.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -16),
            importButton.trailingAnchor.constraint(equalTo: closeButton.leadingAnchor, constant: -8),

            closeButton.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -16),
            closeButton.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -20),
        ])

        self.view = container
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        detectBrowsers()
    }

    private func detectBrowsers() {
        if let json = MahoBridge.shared.detectBrowserProfiles(),
           let decoded: [BrowserProfile] = MahoBridge.shared.decode(json) {
            profiles = decoded
        } else {
            profiles = []
        }

        checkboxes.removeAll()
        browserStackView.arrangedSubviews.forEach { $0.removeFromSuperview() }

        if profiles.isEmpty {
            let label = NSTextField(labelWithString: "No browsers detected for import.")
            label.textColor = .secondaryLabelColor
            browserStackView.addArrangedSubview(label)
            importButton.isEnabled = false
            return
        }

        for (index, profile) in profiles.enumerated() {
            let checkbox = NSButton(checkboxWithTitle: "\(profile.browser) — \(profile.profileName)", target: nil, action: nil)
            checkbox.tag = index
            checkboxes.append(checkbox)
            browserStackView.addArrangedSubview(checkbox)
        }
    }

    @objc private func importClicked(_ sender: NSButton) {
        let selectedIndices = checkboxes.enumerated().compactMap { (i, cb) -> Int? in
            cb.state == .on ? i : nil
        }
        guard !selectedIndices.isEmpty else {
            statusLabel.stringValue = "Please select at least one browser."
            return
        }

        importButton.isEnabled = false
        closeButton.isEnabled = false
        progressIndicator.startAnimation(nil)
        statusLabel.stringValue = "Importing..."

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }
            var successCount = 0
            var failCount = 0

            for index in selectedIndices {
                let profile = self.profiles[index]
                let result: String?
                switch profile.browser.lowercased() {
                case let b where b.contains("chrome") || b.contains("chromium") || b.contains("edge") || b.contains("brave"):
                    result = MahoBridge.shared.importChromeBookmarks(profilePath: profile.profilePath)
                case let b where b.contains("firefox"):
                    result = MahoBridge.shared.importFirefoxBookmarks(profilePath: profile.profilePath)
                default:
                    result = nil
                }
                if result != nil {
                    successCount += 1
                } else {
                    failCount += 1
                }
            }

            DispatchQueue.main.async { [weak self] in
                guard let self = self else { return }
                self.progressIndicator.stopAnimation(nil)
                self.importButton.isEnabled = true
                self.closeButton.isEnabled = true

                if failCount == 0 {
                    self.statusLabel.stringValue = "Successfully imported from \(successCount) browser(s)."
                } else {
                    self.statusLabel.stringValue = "Imported \(successCount), failed \(failCount)."
                }
            }
        }
    }

    @objc private func closeClicked(_ sender: NSButton) {
        if let sheet = view.window {
            sheet.sheetParent?.endSheet(sheet)
        }
    }
}
