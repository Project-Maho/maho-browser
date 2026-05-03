import AppKit

private enum ProfileColorPresets {
    static let colors: [(title: String, hex: String)] = [
        ("Blue", "#4A90D9"),
        ("Purple", "#9B59B6"),
        ("Pink", "#E91E8C"),
        ("Red", "#E74C3C"),
        ("Green", "#27AE60"),
        ("Orange", "#F39C12"),
        ("Yellow", "#F1C40F"),
    ]
}

final class ProfileConfigSheetController: NSViewController, NSTextFieldDelegate {
    enum Mode {
        case create
        case edit(ProfileConfig)
    }

    private let mode: Mode
    private let nameField = NSTextField()
    private let colorPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let downloadPathControl = NSPathControl()
    private let choosePathButton = NSButton(title: "Choose...", target: nil, action: nil)
    private let cancelButton = NSButton(title: "Cancel", target: nil, action: nil)
    private let saveButton: NSButton

    var onSave: (() -> Void)?

    init(mode: Mode) {
        self.mode = mode
        switch mode {
        case .create:
            saveButton = NSButton(title: "Create", target: nil, action: nil)
        case .edit:
            saveButton = NSButton(title: "Save", target: nil, action: nil)
        }
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 420, height: 210))

        let titleLabel = NSTextField(labelWithString: titleText)
        titleLabel.font = .systemFont(ofSize: 16, weight: .semibold)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(titleLabel)

        nameField.translatesAutoresizingMaskIntoConstraints = false
        nameField.placeholderString = "Profile name"
        nameField.delegate = self
        nameField.setAccessibilityIdentifier("profile.config.name")

        colorPopup.translatesAutoresizingMaskIntoConstraints = false
        colorPopup.setAccessibilityIdentifier("profile.config.color")
        for color in ProfileColorPresets.colors {
            colorPopup.addItem(withTitle: color.title)
        }

        downloadPathControl.translatesAutoresizingMaskIntoConstraints = false
        downloadPathControl.pathStyle = .popUp
        downloadPathControl.setAccessibilityIdentifier("profile.config.path")

        choosePathButton.target = self
        choosePathButton.action = #selector(chooseDownloadPath(_:))

        let pathStack = NSStackView(views: [downloadPathControl, choosePathButton])
        pathStack.orientation = .horizontal
        pathStack.alignment = .centerY
        pathStack.spacing = 8
        pathStack.translatesAutoresizingMaskIntoConstraints = false

        let nameRow = makeRow(label: "Name", control: nameField)
        let colorRow = makeRow(label: "Avatar Color", control: colorPopup)
        let pathRow = makeRow(label: "Download Path", control: pathStack)

        let buttonRow = NSStackView(views: [cancelButton, saveButton])
        buttonRow.orientation = .horizontal
        buttonRow.alignment = .centerY
        buttonRow.spacing = 8
        buttonRow.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(buttonRow)

        cancelButton.target = self
        cancelButton.action = #selector(cancelPressed)
        cancelButton.keyEquivalent = "\u{1b}"
        cancelButton.setAccessibilityIdentifier("profile.config.cancel")

        saveButton.target = self
        saveButton.action = #selector(savePressed)
        saveButton.keyEquivalent = "\r"
        saveButton.setAccessibilityIdentifier("profile.config.save")

        view.addSubview(nameRow)
        view.addSubview(colorRow)
        view.addSubview(pathRow)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: view.topAnchor, constant: 20),
            titleLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 20),

            nameRow.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 18),
            nameRow.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 20),
            nameRow.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -20),

            colorRow.topAnchor.constraint(equalTo: nameRow.bottomAnchor, constant: 12),
            colorRow.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor),
            colorRow.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor),

            pathRow.topAnchor.constraint(equalTo: colorRow.bottomAnchor, constant: 12),
            pathRow.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor),
            pathRow.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor),

            buttonRow.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -20),
            buttonRow.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -18),
        ])

        applyInitialValues()
        updateSaveButtonState()
    }

    override func viewDidAppear() {
        super.viewDidAppear()
        view.window?.makeFirstResponder(nameField)
        nameField.selectText(nil)
    }

    func controlTextDidChange(_ obj: Notification) {
        updateSaveButtonState()
    }

    @objc private func cancelPressed() {
        dismiss(nil)
    }

    @objc private func chooseDownloadPath(_ sender: NSButton) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        downloadPathControl.url = url
    }

    @objc private func savePressed() {
        let trimmedName = nameField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedName.isEmpty else {
            NSSound.beep()
            updateSaveButtonState()
            return
        }

        switch mode {
        case .create:
            createProfile(name: trimmedName)
        case .edit(let profile):
            CoreUpdateDispatcher.shared.sendAndDispatch(.updateProfile(
                profileId: profile.id,
                name: trimmedName,
                avatarColor: selectedColorHex,
                downloadPath: selectedDownloadPath,
                archiveTimeoutHours: nil
            ))
        }

        onSave?()
        dismiss(nil)
    }

    private var titleText: String {
        switch mode {
        case .create:
            return "New Profile"
        case .edit:
            return "Edit Profile"
        }
    }

    private var selectedColorHex: String {
        let selectedIndex = colorPopup.indexOfSelectedItem
        guard selectedIndex >= 0, selectedIndex < ProfileColorPresets.colors.count else {
            return ProfileColorPresets.colors[0].hex
        }
        return ProfileColorPresets.colors[selectedIndex].hex
    }

    private var selectedDownloadPath: String? {
        let path = downloadPathControl.url?.path.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return path.isEmpty ? nil : path
    }

    private func createProfile(name: String) {
        let selectedColor = selectedColorHex
        let selectedPath = selectedDownloadPath

        CoreUpdateDispatcher.shared.sendAndDispatch(.createProfile(name: name))

        DispatchQueue.main.async {
            guard let json = MahoBridge.shared.listProfiles(),
                  let data = json.data(using: .utf8),
                  let profiles = try? JSONDecoder().decode([ProfileConfig].self, from: data),
                  let newest = profiles.last else {
                return
            }

            CoreUpdateDispatcher.shared.sendAndDispatch(.updateProfile(
                profileId: newest.id,
                name: nil,
                avatarColor: selectedColor,
                downloadPath: selectedPath,
                archiveTimeoutHours: nil
            ))
        }
    }

    private func applyInitialValues() {
        switch mode {
        case .create:
            colorPopup.selectItem(at: 0)
        case .edit(let profile):
            nameField.stringValue = profile.name
            if let colorIndex = ProfileColorPresets.colors.firstIndex(where: { preset in
                preset.hex.caseInsensitiveCompare(profile.avatarColor) == .orderedSame
            }) {
                colorPopup.selectItem(at: colorIndex)
            } else {
                colorPopup.selectItem(at: 0)
            }

            let trimmedPath = profile.downloadPath.trimmingCharacters(in: .whitespacesAndNewlines)
            if !trimmedPath.isEmpty {
                downloadPathControl.url = URL(fileURLWithPath: trimmedPath)
            }
        }
    }

    private func updateSaveButtonState() {
        let trimmedName = nameField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        saveButton.isEnabled = !trimmedName.isEmpty
    }

    private func makeRow(label: String, control: NSView) -> NSStackView {
        let labelField = NSTextField(labelWithString: label)
        labelField.font = .systemFont(ofSize: 12, weight: .medium)
        labelField.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.9)
        labelField.setContentHuggingPriority(.required, for: .horizontal)

        let row = NSStackView(views: [labelField, control])
        row.orientation = .horizontal
        row.alignment = .centerY
        row.spacing = 12
        row.translatesAutoresizingMaskIntoConstraints = false

        control.translatesAutoresizingMaskIntoConstraints = false
        control.widthAnchor.constraint(equalToConstant: 260).isActive = true
        return row
    }
}
