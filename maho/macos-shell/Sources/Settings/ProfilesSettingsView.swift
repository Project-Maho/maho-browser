import AppKit

final class ProfilesSettingsViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver, SettingsPaneRefreshable {

    private var tableView = NSTableView()
    private var addButton = NSButton()
    private var editButton = NSButton()
    private var deleteButton = NSButton()
    private var switchButton = NSButton()
    private var nameField = NSTextField()
    private var avatarColorField = NSTextField()
    private var downloadPathControl = NSPathControl()

    private var profiles: [ProfileConfig] = []
    private var selectedProfileId: String? = nil
    private var profilesSectionView: SettingsManagedTableSectionView?

    override func loadView() {
        let nameColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("name"))
        nameColumn.title = "Name"
        nameColumn.width = 150
        nameColumn.resizingMask = []
        tableView.addTableColumn(nameColumn)

        let colorColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("color"))
        colorColumn.title = "Color"
        colorColumn.width = 100
        colorColumn.resizingMask = []
        tableView.addTableColumn(colorColumn)

        let pathColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("path"))
        pathColumn.title = "Download Path"
        pathColumn.width = 200
        pathColumn.resizingMask = .autoresizingMask
        tableView.addTableColumn(pathColumn)

        tableView.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle

        tableView.headerView = NSTableHeaderView()
        styleSettingsTable(tableView)
        tableView.delegate = self
        tableView.dataSource = self

        addButton.title = "Add Profile"
        addButton.target = self
        addButton.action = #selector(addProfile(_:))

        editButton.title = "Edit"
        editButton.target = self
        editButton.action = #selector(editProfile(_:))

        deleteButton.title = "Delete"
        deleteButton.target = self
        deleteButton.action = #selector(deleteProfile(_:))

        switchButton.title = "Switch Profile"
        switchButton.target = self
        switchButton.action = #selector(switchProfile(_:))

        let profileListSectionView = SettingsManagedTableSectionView(
            tableView: tableView,
            containerStyle: .mixedCard(
                footerViews: [makeSettingsActionRow([addButton, editButton, deleteButton, switchButton], spacing: 12)]
            ),
            shellStyle: .fixedHeight(160)
        )
        self.profilesSectionView = profileListSectionView

        nameField.placeholderString = "Profile Name"
        styleSettingsEditableTextField(nameField)
        let nameRow = makeSettingsLabeledRow("Name:", nameField, embedded: true)

        avatarColorField.placeholderString = "Avatar Color"
        styleSettingsEditableTextField(avatarColorField)
        let colorRow = makeSettingsLabeledRow("Avatar Color:", avatarColorField, embedded: true)

        let chooseButton = NSButton(title: "Choose...", target: self, action: #selector(chooseDownloadPath(_:)))
        let pathStack = NSStackView(views: [downloadPathControl, chooseButton])
        pathStack.orientation = .horizontal
        pathStack.spacing = 8
        let pathRow = makeSettingsLabeledRow("Download Path:", pathStack, embedded: true)
        downloadPathControl.pathStyle = .popUp

        let updateButton = NSButton(title: "Update", target: self, action: #selector(updateProfile(_:)))

        let editCard = makeSettingsSectionCard([
            nameRow,
            colorRow,
            pathRow,
            makeSettingsActionRow([updateButton])
        ])

        view = SettingsPaneHostView(sections: [
            .managedTable(profileListSectionView),
            .plain([editCard])
        ])
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        loadProfiles()
    }

    func refreshSettingsContent() {
        loadProfiles()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .settingsChanged, .profileCreated, .profileUpdated, .profileDeleted, .activeProfileChanged:
            DispatchQueue.main.async { [weak self] in
                self?.loadProfiles()
            }
        default:
            break
        }
    }

    private func loadProfiles() {
        if let json = MahoBridge.shared.listProfiles(),
           let list: [ProfileConfig] = MahoBridge.shared.decode(json) {
            profiles = list
        }
        tableView.reloadData()
    }

    // MARK: - NSTableViewDataSource

    func numberOfRows(in tableView: NSTableView) -> Int {
        return profiles.count
    }

    // MARK: - NSTableViewDelegate

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < profiles.count else { return nil }
        let profile = profiles[row]
        let text: String
        switch tableColumn?.identifier.rawValue {
        case "name": text = profile.name
        case "color": text = profile.avatarColor
        case "path": text = profile.downloadPath
        default: text = ""
        }
        let secondary = tableColumn?.identifier.rawValue != "name"
        return makeSettingsTableCell(text: text, secondary: secondary)
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = tableView.selectedRow
        guard row >= 0, row < profiles.count else { return }
        selectedProfileId = profiles[row].id
        nameField.stringValue = profiles[row].name
        avatarColorField.stringValue = profiles[row].avatarColor
        downloadPathControl.url = URL(fileURLWithPath: profiles[row].downloadPath)
    }

    @objc private func addProfile(_ sender: NSButton) {
        let sheet = ProfileConfigSheetController(mode: .create)
        sheet.onSave = { [weak self] in
            self?.loadProfiles()
        }
        presentProfileSheet(sheet)
    }

    @objc private func editProfile(_ sender: NSButton) {
        guard let id = selectedProfileId,
              let profile = profiles.first(where: { $0.id == id }) else { return }
        let sheet = ProfileConfigSheetController(mode: .edit(profile))
        sheet.onSave = { [weak self] in
            self?.loadProfiles()
        }
        presentProfileSheet(sheet)
    }

    @objc private func deleteProfile(_ sender: NSButton) {
        guard let id = selectedProfileId,
              let profile = profiles.first(where: { $0.id == id }) else { return }
        let alert = NSAlert()
        alert.messageText = "Delete \"\(profile.name)\"?"
        alert.informativeText = "This profile and its browsing data will be permanently removed."
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        guard let window = view.window else { return }
        alert.beginSheetModal(for: window) { [weak self] response in
            if response == .alertFirstButtonReturn {
                CoreUpdateDispatcher.shared.sendAndDispatch(.deleteProfile(profileId: ProfileId(id)))
                self?.loadProfiles()
            }
        }
    }

    @objc private func switchProfile(_ sender: NSButton) {
        guard let id = selectedProfileId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.switchProfile(profileId: ProfileId(id)))
    }

    @objc private func updateProfile(_ sender: NSButton) {
        guard let id = selectedProfileId else { return }
        let name = nameField.stringValue.isEmpty ? nil : nameField.stringValue
        let color = avatarColorField.stringValue.isEmpty ? nil : avatarColorField.stringValue
        let path = downloadPathControl.url?.path
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateProfile(
            profileId: ProfileId(id),
            name: name,
            avatarColor: color,
            downloadPath: path,
            archiveTimeoutHours: nil
        ))
        loadProfiles()
    }

    @objc private func chooseDownloadPath(_ sender: NSButton) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        downloadPathControl.url = url
    }

    private func presentProfileSheet(_ sheet: ProfileConfigSheetController) {
        if let window = view.window,
           let presenter = window.contentViewController {
            presenter.presentAsSheet(sheet)
        } else {
            presentAsSheet(sheet)
        }
    }
}
