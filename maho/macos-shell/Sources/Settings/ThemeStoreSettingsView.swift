import AppKit

final class ThemeStoreSettingsViewController: NSViewController {
    private let tableView = NSTableView()
    private let emptyLabel = NSTextField(labelWithString: "No themes available")
    private let loadingLabel = NSTextField(labelWithString: "Loading themes…")
    private let refreshButton = NSButton(title: "Refresh", target: nil, action: nil)
    private var themes: [ThemeStoreEntry] = []
    private var themesSectionView: SettingsManagedTableSectionView?

    override func loadView() {
        let descLabel = NSTextField(wrappingLabelWithString: "Browse and install community-created themes for your spaces.")
        descLabel.font = .systemFont(ofSize: 12)
        descLabel.textColor = MahoAppColors.secondaryText

        refreshButton.target = self
        refreshButton.action = #selector(refreshClicked(_:))
        refreshButton.bezelStyle = .rounded
        refreshButton.setAccessibilityIdentifier("settings.themeStore.refresh")

        let topRow = NSStackView(views: [descLabel, refreshButton])
        topRow.orientation = .horizontal
        topRow.alignment = .centerY
        topRow.spacing = 12

        loadingLabel.translatesAutoresizingMaskIntoConstraints = false
        loadingLabel.textColor = MahoAppColors.secondaryText
        loadingLabel.font = .systemFont(ofSize: 13)
        loadingLabel.alignment = .center
        loadingLabel.isHidden = true
        loadingLabel.setAccessibilityIdentifier("settings.themeStore.loading")

        let nameCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Name"))
        nameCol.title = "Name"
        nameCol.width = 180
        nameCol.resizingMask = []
        nameCol.minWidth = 180
        nameCol.maxWidth = 180
        tableView.addTableColumn(nameCol)

        let authorCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Author"))
        authorCol.title = "Author"
        authorCol.width = 120
        authorCol.resizingMask = []
        authorCol.minWidth = 120
        authorCol.maxWidth = 120
        tableView.addTableColumn(authorCol)

        let descCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Description"))
        descCol.title = "Description"
        descCol.width = 220
        descCol.resizingMask = .autoresizingMask
        tableView.addTableColumn(descCol)

        let actionsCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("Actions"))
        actionsCol.title = ""
        actionsCol.width = 80
        actionsCol.resizingMask = []
        actionsCol.minWidth = 80
        actionsCol.maxWidth = 80
        tableView.addTableColumn(actionsCol)

        tableView.columnAutoresizingStyle = .sequentialColumnAutoresizingStyle

        tableView.delegate = self
        tableView.dataSource = self
        tableView.setAccessibilityIdentifier("settings.themeStore.table")

        emptyLabel.translatesAutoresizingMaskIntoConstraints = false
        emptyLabel.textColor = MahoAppColors.secondaryText
        emptyLabel.font = .systemFont(ofSize: 13)
        emptyLabel.alignment = .center
        emptyLabel.isHidden = true
        emptyLabel.setAccessibilityIdentifier("settings.themeStore.emptyState")

        let themesSectionView = SettingsManagedTableSectionView(
            tableView: tableView,
            containerStyle: .mixedCard(
                headerViews: [makeSettingsSectionTitle("Available Themes"), loadingLabel]
            ),
            shellStyle: .compact(emptyStateView: emptyLabel)
        )
        self.themesSectionView = themesSectionView

        view = SettingsPaneHostView(sections: [
            .plain([makeSettingsSectionCard([topRow])]),
            .managedTable(themesSectionView)
        ])
        updateTableHeight()
    }

    private func updateTableHeight() {
        themesSectionView?.updateManagedTableLayout(rowCount: themes.count)
        emptyLabel.isHidden = !themes.isEmpty || !loadingLabel.isHidden
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        fetchThemes()
    }

    private func fetchThemes() {
        loadingLabel.isHidden = false
        emptyLabel.isHidden = true

        ThemeStoreClient.shared.fetchThemes { [weak self] result in
            DispatchQueue.main.async {
                self?.loadingLabel.isHidden = true
                switch result {
                case .success(let entries):
                    self?.themes = entries
                    self?.tableView.reloadData()
                    self?.updateTableHeight()
                case .failure:
                    self?.themes = []
                    self?.tableView.reloadData()
                    self?.updateTableHeight()
                }
            }
        }
    }

    @objc private func refreshClicked(_ sender: NSButton) {
        fetchThemes()
    }

    @objc private func installThemeClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < themes.count else { return }
        ThemeStoreClient.shared.installTheme(themes[row])

        let alert = NSAlert()
        alert.messageText = "Theme Installed"
        alert.informativeText = "\"\(themes[row].name)\" has been applied to your active space."
        alert.alertStyle = .informational
        alert.runModal()
    }
}

extension ThemeStoreSettingsViewController: NSTableViewDelegate, NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        return themes.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < themes.count else { return nil }
        let theme = themes[row]

        switch tableColumn?.identifier.rawValue {
        case "Name":
            return makeSettingsTableCell(text: theme.name)
        case "Author":
            return makeSettingsTableCell(text: theme.author, secondary: true)
        case "Description":
            return makeSettingsTableCell(text: theme.description, secondary: true)
        case "Actions":
            let installBtn = NSButton(title: "Install", target: self, action: #selector(installThemeClicked(_:)))
            installBtn.bezelStyle = .inline
            installBtn.font = .systemFont(ofSize: 10)
            installBtn.tag = row
            installBtn.setAccessibilityIdentifier("settings.themeStore.install.\(row)")
            return makeSettingsCenteredTableActionCell(installBtn)
        default:
            return nil
        }
    }
}
