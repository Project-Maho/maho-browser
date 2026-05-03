import AppKit

final class AddLiveFolderController: NSViewController {
    private let providerPopup = NSPopUpButton()
    private let nameField = NSTextField()
    private let configStack = NSStackView()
    private var configFields: [String: NSTextField] = [:]
    var spaceId: SpaceId?
    var onAdd: (() -> Void)?

    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 400, height: 300))

        let mainStack = NSStackView()
        mainStack.orientation = .vertical
        mainStack.alignment = .leading
        mainStack.spacing = 12
        mainStack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(mainStack)

        let titleLabel = NSTextField(labelWithString: "Add Live Folder")
        titleLabel.font = .systemFont(ofSize: 16, weight: .semibold)
        mainStack.addArrangedSubview(titleLabel)

        let providerRow = NSStackView()
        providerRow.orientation = .horizontal
        providerRow.addArrangedSubview(NSTextField(labelWithString: "Provider:"))
        for provider in LiveFolderManager.shared.availableProviders {
            providerPopup.addItem(withTitle: provider.displayName)
        }
        providerPopup.target = self
        providerPopup.action = #selector(providerChanged(_:))
        providerRow.addArrangedSubview(providerPopup)
        mainStack.addArrangedSubview(providerRow)

        let nameRow = NSStackView()
        nameRow.orientation = .horizontal
        nameRow.addArrangedSubview(NSTextField(labelWithString: "Name:"))
        nameField.placeholderString = "My Live Folder"
        nameField.translatesAutoresizingMaskIntoConstraints = false
        nameField.widthAnchor.constraint(equalToConstant: 200).isActive = true
        nameRow.addArrangedSubview(nameField)
        mainStack.addArrangedSubview(nameRow)

        configStack.orientation = .vertical
        configStack.alignment = .leading
        configStack.spacing = 8
        mainStack.addArrangedSubview(configStack)

        let buttonRow = NSStackView()
        buttonRow.orientation = .horizontal
        let cancelBtn = NSButton(title: "Cancel", target: self, action: #selector(cancelClicked(_:)))
        let addBtn = NSButton(title: "Add", target: self, action: #selector(addClicked(_:)))
        addBtn.bezelStyle = .rounded
        addBtn.keyEquivalent = "\r"
        buttonRow.addArrangedSubview(cancelBtn)
        buttonRow.addArrangedSubview(addBtn)
        mainStack.addArrangedSubview(buttonRow)

        NSLayoutConstraint.activate([
            mainStack.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 20),
            mainStack.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -20),
            mainStack.topAnchor.constraint(equalTo: view.topAnchor, constant: 20),
        ])

        providerChanged(nil)
    }

    @objc private func providerChanged(_ sender: Any?) {
        configStack.arrangedSubviews.forEach { $0.removeFromSuperview() }
        configFields.removeAll()

        let providers = LiveFolderManager.shared.availableProviders
        guard providerPopup.indexOfSelectedItem < providers.count else { return }
        let provider = providers[providerPopup.indexOfSelectedItem]

        switch provider.providerId {
        case "github_prs":
            addConfigField(key: "repo", label: "Repository (owner/repo):", placeholder: "user/repo")
            addConfigField(key: "token", label: "GitHub Token (optional):", placeholder: "ghp_...")
        case "google_calendar":
            let label = NSTextField(labelWithString: "Opens Google Calendar directly")
            label.font = .systemFont(ofSize: 11)
            label.textColor = .secondaryLabelColor
            configStack.addArrangedSubview(label)
        default:
            break
        }
    }

    private func addConfigField(key: String, label: String, placeholder: String) {
        let row = NSStackView()
        row.orientation = .horizontal
        row.addArrangedSubview(NSTextField(labelWithString: label))
        let field = NSTextField()
        field.placeholderString = placeholder
        field.translatesAutoresizingMaskIntoConstraints = false
        field.widthAnchor.constraint(equalToConstant: 200).isActive = true
        row.addArrangedSubview(field)
        configStack.addArrangedSubview(row)
        configFields[key] = field
    }

    @objc private func cancelClicked(_ sender: Any) {
        dismiss(nil)
    }

    @objc private func addClicked(_ sender: Any) {
        let providers = LiveFolderManager.shared.availableProviders
        guard providerPopup.indexOfSelectedItem < providers.count else { return }
        let provider = providers[providerPopup.indexOfSelectedItem]
        let name = nameField.stringValue.isEmpty ? provider.displayName : nameField.stringValue
        var config: [String: String] = [:]
        for (key, field) in configFields {
            if !field.stringValue.isEmpty {
                config[key] = field.stringValue
            }
        }
        _ = LiveFolderManager.shared.addFolder(providerId: provider.providerId, name: name, config: config, spaceId: spaceId)
        onAdd?()
        dismiss(nil)
    }
}
