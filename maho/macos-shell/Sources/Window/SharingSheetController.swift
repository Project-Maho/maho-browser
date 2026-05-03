import AppKit

final class SharingSheetController: NSViewController {
    private let spaceId: String
    private let spaceName: String
    private var collectionId: String?

    private let linkField = NSTextField()
    private let copyButton = NSButton(title: "Copy Link", target: nil, action: nil)
    private let permissionPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let createButton = NSButton(title: "Create Share Link", target: nil, action: nil)
    private let revokeButton = NSButton(title: "Revoke Access", target: nil, action: nil)
    private let memberLabel = NSTextField(labelWithString: "")
    private let doneButton = NSButton(title: "Done", target: nil, action: nil)

    init(spaceId: String, spaceName: String) {
        self.spaceId = spaceId
        self.spaceName = spaceName
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) { fatalError() }

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 400, height: 250))
        self.view = container

        let titleLabel = NSTextField(labelWithString: "Share \(spaceName)")
        titleLabel.font = .boldSystemFont(ofSize: 16)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(titleLabel)

        linkField.isEditable = false
        linkField.isSelectable = true
        linkField.placeholderString = "No share link generated"
        linkField.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(linkField)

        copyButton.target = self
        copyButton.action = #selector(copyLink)
        copyButton.translatesAutoresizingMaskIntoConstraints = false
        copyButton.isEnabled = false
        container.addSubview(copyButton)

        permissionPopup.addItems(withTitles: ["Can view", "Can edit"])
        permissionPopup.target = self
        permissionPopup.action = #selector(permissionChanged)
        permissionPopup.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(permissionPopup)

        createButton.target = self
        createButton.action = #selector(createShareLink)
        createButton.bezelStyle = .rounded
        createButton.keyEquivalent = "\r"
        createButton.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(createButton)

        revokeButton.target = self
        revokeButton.action = #selector(revokeAccess)
        revokeButton.bezelStyle = .rounded
        revokeButton.contentTintColor = .systemRed
        revokeButton.isEnabled = false
        revokeButton.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(revokeButton)

        memberLabel.translatesAutoresizingMaskIntoConstraints = false
        memberLabel.stringValue = "No members"
        container.addSubview(memberLabel)

        doneButton.target = self
        doneButton.action = #selector(dismissSheet)
        doneButton.bezelStyle = .rounded
        doneButton.keyEquivalent = "\u{1b}"
        doneButton.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(doneButton)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: container.topAnchor, constant: 20),
            titleLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 20),

            linkField.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 16),
            linkField.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 20),
            linkField.trailingAnchor.constraint(equalTo: copyButton.leadingAnchor, constant: -8),

            copyButton.centerYAnchor.constraint(equalTo: linkField.centerYAnchor),
            copyButton.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -20),
            copyButton.widthAnchor.constraint(equalToConstant: 90),

            permissionPopup.topAnchor.constraint(equalTo: linkField.bottomAnchor, constant: 12),
            permissionPopup.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 20),
            permissionPopup.widthAnchor.constraint(equalToConstant: 120),

            memberLabel.centerYAnchor.constraint(equalTo: permissionPopup.centerYAnchor),
            memberLabel.leadingAnchor.constraint(equalTo: permissionPopup.trailingAnchor, constant: 16),

            createButton.topAnchor.constraint(equalTo: permissionPopup.bottomAnchor, constant: 20),
            createButton.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 20),

            revokeButton.centerYAnchor.constraint(equalTo: createButton.centerYAnchor),
            revokeButton.leadingAnchor.constraint(equalTo: createButton.trailingAnchor, constant: 12),

            doneButton.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -20),
            doneButton.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -20),
        ])
    }

    @objc private func createShareLink() {
        guard let id = MahoBridge.shared.createSharedCollection(spaceId: spaceId, name: spaceName) else { return }
        collectionId = id
        let permission = permissionPopup.indexOfSelectedItem == 0 ? "view" : "edit"
        MahoBridge.shared.updateSharePermissions(collectionId: id, permission: permission)
        if let link = MahoBridge.shared.getShareLink(collectionId: id) {
            linkField.stringValue = link
        }
        copyButton.isEnabled = true
        revokeButton.isEnabled = true
        createButton.title = "Regenerate Link"
    }

    @objc private func copyLink() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(linkField.stringValue, forType: .string)
    }

    @objc private func permissionChanged() {
        guard let id = collectionId else { return }
        let permission = permissionPopup.indexOfSelectedItem == 0 ? "view" : "edit"
        MahoBridge.shared.updateSharePermissions(collectionId: id, permission: permission)
    }

    @objc private func revokeAccess() {
        guard let id = collectionId else { return }
        MahoBridge.shared.revokeShare(collectionId: id)
        collectionId = nil
        linkField.stringValue = ""
        copyButton.isEnabled = false
        revokeButton.isEnabled = false
        createButton.title = "Create Share Link"
        memberLabel.stringValue = "No members"
    }

    @objc private func dismissSheet() {
        dismiss(nil)
    }
}
