import AppKit

final class ExtensionsSettingsViewController: NSViewController, CoreUpdateObserver {
    private let tableView = NSTableView()
    private let emptyLabel = NSTextField(labelWithString: "No extensions installed")
    private let globalBoostsToggle = NSButton(checkboxWithTitle: "Enable Global Boosts", target: nil, action: nil)
    private let jsBoostsToggle = NSButton(checkboxWithTitle: "Enable JS Boosts", target: nil, action: nil)
    private var extensions: [ExtensionViewModel] = []
    private var extensionsSectionView: SettingsManagedTableSectionView?

    override func loadView() {
        globalBoostsToggle.target = self
        globalBoostsToggle.action = #selector(globalBoostsToggled(_:))
        globalBoostsToggle.state = .on

        jsBoostsToggle.target = self
        jsBoostsToggle.action = #selector(jsBoostsToggled(_:))
        jsBoostsToggle.state = .on

        let nameCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Name"))
        nameCol.title = "Name"
        nameCol.width = 200
        nameCol.resizingMask = .autoresizingMask
        tableView.addTableColumn(nameCol)

        let versionCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Version"))
        versionCol.title = "Version"
        versionCol.width = 80
        versionCol.resizingMask = []
        tableView.addTableColumn(versionCol)

        let enabledCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Enabled"))
        enabledCol.title = "Enabled"
        enabledCol.width = 60
        enabledCol.resizingMask = []
        tableView.addTableColumn(enabledCol)

        let actionsCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Actions"))
        actionsCol.title = ""
        actionsCol.width = 60
        actionsCol.resizingMask = []
        tableView.addTableColumn(actionsCol)

        let pinCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Pin"))
        pinCol.title = "Toolbar"
        pinCol.width = 60
        pinCol.resizingMask = []
        tableView.addTableColumn(pinCol)

        tableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle

        tableView.delegate = self
        tableView.dataSource = self

        emptyLabel.translatesAutoresizingMaskIntoConstraints = false
        emptyLabel.textColor = MahoAppColors.secondaryText
        emptyLabel.font = .systemFont(ofSize: 13)
        emptyLabel.alignment = .center
        emptyLabel.isHidden = true

        let extensionsSectionView = SettingsManagedTableSectionView(
            tableView: tableView,
            containerStyle: .card(titleView: makeSettingsSectionTitle("Installed Extensions")),
            shellStyle: .compact(emptyStateView: emptyLabel)
        )
        self.extensionsSectionView = extensionsSectionView

        view = SettingsPaneHostView(sections: [
            .card([globalBoostsToggle, jsBoostsToggle]),
            .managedTable(extensionsSectionView)
        ])

        updateTableHeight()
    }

    private func updateTableHeight() {
        extensionsSectionView?.updateManagedTableLayout(rowCount: extensions.count)
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        reloadExtensions()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    private func reloadExtensions() {
        extensions = MahoBridge.shared.getInstalledExtensions()
        tableView.reloadData()
        updateTableHeight()
    }

    @objc private func globalBoostsToggled(_ sender: NSButton) {
        CoreUpdateDispatcher.shared.sendAndDispatch(.toggleGlobalBoosts)
    }

    @objc private func jsBoostsToggled(_ sender: NSButton) {
        CoreUpdateDispatcher.shared.sendAndDispatch(.toggleJsBoosts)
    }

    @objc private func toggleExtensionClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < extensions.count else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.toggleExtension(extensionId: extensions[row].id))
        reloadExtensions()
    }

    @objc private func removeExtensionClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < extensions.count else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.removeExtension(extensionId: extensions[row].id))
        reloadExtensions()
    }

    @objc private func pinExtensionClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < extensions.count else { return }
        let extId = extensions[row].id
        let pinnedKey = "maho_pinned_extensions"
        var pinned = UserDefaults.standard.stringArray(forKey: pinnedKey) ?? []
        if pinned.contains(extId) {
            pinned.removeAll { $0 == extId }
            NotificationCenter.default.post(name: .unpinExtension, object: nil, userInfo: ["extensionId": extId])
        } else {
            pinned.append(extId)
            NotificationCenter.default.post(name: .pinExtension, object: nil, userInfo: ["extensionId": extId])
        }
        UserDefaults.standard.set(pinned, forKey: pinnedKey)
        tableView.reloadData()
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .extensionToggled, .extensionRemoved, .fullState:
            DispatchQueue.main.async { [weak self] in self?.reloadExtensions() }
        default:
            break
        }
    }
}

extension ExtensionsSettingsViewController: NSTableViewDelegate, NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        return extensions.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < extensions.count else { return nil }
        let ext = extensions[row]

        switch tableColumn?.identifier.rawValue {
        case "Name":
            return makeSettingsTableCell(text: ext.name)
        case "Version":
            return makeSettingsTableCell(text: ext.version, secondary: true)
        case "Enabled":
            let toggle = NSButton(checkboxWithTitle: "", target: self, action: #selector(toggleExtensionClicked(_:)))
            toggle.state = ext.enabled ? .on : .off
            toggle.tag = row
            return makeSettingsCenteredTableActionCell(toggle)
        case "Actions":
            let removeBtn = NSButton(title: "Remove", target: self, action: #selector(removeExtensionClicked(_:)))
            removeBtn.bezelStyle = .inline
            removeBtn.font = .systemFont(ofSize: 10)
            removeBtn.tag = row
            removeBtn.contentTintColor = MahoAppColors.dangerRed
            return makeSettingsCenteredTableActionCell(removeBtn)
        case "Pin":
            let isPinned = UserDefaults.standard.stringArray(forKey: "maho_pinned_extensions")?.contains(ext.id) ?? false
            let pinBtn = NSButton(title: isPinned ? "Unpin" : "Pin", target: self, action: #selector(pinExtensionClicked(_:)))
            pinBtn.bezelStyle = .inline
            pinBtn.font = .systemFont(ofSize: 10)
            pinBtn.tag = row
            return makeSettingsCenteredTableActionCell(pinBtn)
        default:
            return nil
        }
    }
}
