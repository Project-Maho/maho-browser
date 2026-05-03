import AppKit

final class PasswordsSettingsViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver {

    private let providerPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private var searchField = NSSearchField()
    private var tableView = NSTableView()
    private var addButton = NSButton(title: "Add", target: nil, action: nil)
    private var deleteButton = NSButton(title: "Delete", target: nil, action: nil)
    private var exportButton = NSButton(title: "Export…", target: nil, action: nil)
    private var passwords: [SavedPassword] = []
    private let emptyStateLabel = makeSettingsCompactEmptyStateLabel("No saved passwords yet")
    private var passwordsSectionView: SettingsManagedTableSectionView?

    private static let compactEmptyStateHeight: CGFloat = 54

    override func loadView() {
        providerPopup.removeAllItems()
        providerPopup.addItems(withTitles: ["Maho Built-in", "System (Keychain, 1Password, etc.)", "Disabled"])
        providerPopup.target = self
        providerPopup.action = #selector(providerChanged(_:))

        let providerRow = makeSettingsLabeledRow("Password Provider:", providerPopup)

        searchField.placeholderString = "Search passwords..."
        searchField.target = self
        searchField.action = #selector(searchChanged(_:))
        searchField.translatesAutoresizingMaskIntoConstraints = false
        styleSettingsEditableTextField(searchField)

        let domainColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("domain"))
        domainColumn.title = "Domain"
        domainColumn.width = 200
        domainColumn.resizingMask = .autoresizingMask
        tableView.addTableColumn(domainColumn)

        let usernameColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("username"))
        usernameColumn.title = "Username"
        usernameColumn.width = 200
        usernameColumn.resizingMask = []
        tableView.addTableColumn(usernameColumn)

        let createdColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("created"))
        createdColumn.title = "Created"
        createdColumn.width = 150
        createdColumn.resizingMask = []
        tableView.addTableColumn(createdColumn)

        tableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle

        tableView.headerView = NSTableHeaderView()
        tableView.delegate = self
        tableView.dataSource = self

        addButton = NSButton(title: "Add", target: self, action: #selector(addPassword(_:)))
        deleteButton = NSButton(title: "Delete", target: self, action: #selector(deletePassword(_:)))
        exportButton = NSButton(title: "Export…", target: self, action: #selector(exportPasswords(_:)))
        let buttonRow = makeSettingsActionRow([addButton, deleteButton, exportButton])

        let passwordsSectionView = SettingsManagedTableSectionView(
            tableView: tableView,
            containerStyle: .mixedCard(
                headerViews: [searchField],
                footerViews: [buttonRow]
            ),
            shellStyle: .compact(
                emptyStateView: emptyStateLabel,
                emptyStateHeight: Self.compactEmptyStateHeight
            )
        )
        self.passwordsSectionView = passwordsSectionView

        view = SettingsPaneHostView(sections: [
            .plain([providerRow]),
            .managedTable(passwordsSectionView)
        ])

        updateTableHeight()
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        CoreUpdateDispatcher.shared.sendAndDispatch(.searchPasswords(query: ""))

        if let settings = MahoBridge.shared.getSettings() {
            switch settings.autofill.passwordProvider {
            case .builtin:
                providerPopup.selectItem(at: 0)
            case .system:
                providerPopup.selectItem(at: 1)
            case .disabled:
                providerPopup.selectItem(at: 2)
            }
            updateProviderState(settings.autofill.passwordProvider)
        }
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .passwordsSearchResult(let results):
            passwords = results
            tableView.reloadData()
            updateTableHeight()
        case .settingsChanged(let settings):
            switch settings.autofill.passwordProvider {
            case .builtin:
                providerPopup.selectItem(at: 0)
            case .system:
                providerPopup.selectItem(at: 1)
            case .disabled:
                providerPopup.selectItem(at: 2)
            }
            updateProviderState(settings.autofill.passwordProvider)
        case .passwordDeleted:
            CoreUpdateDispatcher.shared.sendAndDispatch(.searchPasswords(query: searchField.stringValue))
        default:
            break
        }
    }

    @objc private func providerChanged(_ sender: NSPopUpButton) {
        let provider: PasswordAutofillProvider
        switch sender.indexOfSelectedItem {
        case 0:
            provider = .builtin
        case 1:
            provider = .system
        case 2:
            provider = .disabled
        default:
            provider = .builtin
        }

        let currentProvider = MahoBridge.shared.getSettings()?.autofill.passwordProvider ?? .builtin

        if currentProvider == .builtin && provider != .builtin && !passwords.isEmpty {
            let providerName = provider == .system ? "system" : "disabled"
            let alert = NSAlert()
            alert.messageText = "Switch Password Provider?"
            alert.informativeText = "Your \(passwords.count) saved password(s) in Maho will not be available through the \(providerName) provider. Consider exporting them first."
            alert.addButton(withTitle: "Switch Anyway")
            alert.addButton(withTitle: "Export First")
            alert.addButton(withTitle: "Cancel")
            alert.alertStyle = .warning

            let response = alert.runModal()
            if response == .alertSecondButtonReturn {
                exportPasswords(sender)
            } else if response == .alertThirdButtonReturn {
                switch currentProvider {
                case .builtin:
                    sender.selectItem(at: 0)
                case .system:
                    sender.selectItem(at: 1)
                case .disabled:
                    sender.selectItem(at: 2)
                }
                return
            }
        }

        let update = SettingsUpdate(
            general: nil, appearance: nil, privacy: nil, reader: nil,
            keyboardShortcuts: nil, perSiteSettings: nil, toolbarItems: nil, max: nil,
            autofill: AutofillSettingsUpdate(
                addressesEnabled: nil,
                paymentsEnabled: nil,
                passwordProvider: provider
            ),
            advanced: nil, notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))

        updateProviderState(provider)
    }

    @objc private func exportPasswords(_ sender: NSButton) {
        guard !passwords.isEmpty else {
            let alert = NSAlert()
            alert.messageText = "No Passwords to Export"
            alert.informativeText = "There are no saved passwords to export."
            alert.runModal()
            return
        }

        let savePanel = NSSavePanel()
        savePanel.allowedContentTypes = [.commaSeparatedText]
        savePanel.nameFieldStringValue = "maho-passwords.csv"
        savePanel.title = "Export Passwords"

        guard savePanel.runModal() == .OK, let url = savePanel.url else { return }

        var csv = "domain,username,password,created_at,last_used\n"
        for pw in passwords {
            let domain = pw.domain.replacingOccurrences(of: ",", with: ";")
            let username = pw.username.replacingOccurrences(of: ",", with: ";")
            let password = (pw.password ?? "").replacingOccurrences(of: ",", with: ";")
            let created = pw.createdAt.replacingOccurrences(of: ",", with: ";")
            let lastUsed = (pw.lastUsed ?? "").replacingOccurrences(of: ",", with: ";")
            csv += "\(domain),\(username),\(password),\(created),\(lastUsed)\n"
        }

        do {
            try csv.write(to: url, atomically: true, encoding: .utf8)
        } catch {
            let alert = NSAlert()
            alert.messageText = "Export Failed"
            alert.informativeText = error.localizedDescription
            alert.runModal()
        }
    }

    private func updateProviderState(_ provider: PasswordAutofillProvider) {
        let isBuiltin = provider == .builtin
        searchField.isEnabled = isBuiltin
        tableView.alphaValue = isBuiltin ? 1.0 : 0.4
        addButton.isEnabled = isBuiltin
        deleteButton.isEnabled = isBuiltin
        passwordsSectionView?.setManagedTableShellAlpha(isBuiltin ? 1.0 : 0.4)
        updateTableHeight()
    }

    @objc private func searchChanged(_ sender: NSSearchField) {
        CoreUpdateDispatcher.shared.sendAndDispatch(.searchPasswords(query: sender.stringValue))
    }

    @objc private func deletePassword(_ sender: NSButton) {
        let row = tableView.selectedRow
        guard row >= 0, row < passwords.count else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.deletePassword(passwordId: passwords[row].id))
    }

    @objc private func addPassword(_ sender: NSButton) {
        let alert = NSAlert()
        alert.messageText = "Add Password"
        alert.addButton(withTitle: "Add")
        alert.addButton(withTitle: "Cancel")

        let inputStack = NSStackView()
        inputStack.orientation = .vertical
        inputStack.spacing = 8

        let domainField = NSTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
        domainField.placeholderString = "Domain (e.g. example.com)"
        styleSettingsEditableTextField(domainField)

        let usernameField = NSTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
        usernameField.placeholderString = "Username"
        styleSettingsEditableTextField(usernameField)

        let passwordField = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
        passwordField.placeholderString = "Password"
        styleSettingsEditableTextField(passwordField)

        inputStack.addArrangedSubview(domainField)
        inputStack.addArrangedSubview(usernameField)
        inputStack.addArrangedSubview(passwordField)
        inputStack.frame = NSRect(x: 0, y: 0, width: 300, height: 84)
        alert.accessoryView = inputStack

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let domain = domainField.stringValue
        let username = usernameField.stringValue
        let password = passwordField.stringValue
        guard !domain.isEmpty, !username.isEmpty else { return }

        CoreUpdateDispatcher.shared.sendAndDispatch(.addPassword(domain: domain, username: username, password: password))
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        return passwords.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < passwords.count else { return nil }
        let pw = passwords[row]

        switch tableColumn?.identifier.rawValue {
        case "domain":
            return makeSettingsTableCell(text: pw.domain)
        case "username":
            return makeSettingsTableCell(text: pw.username)
        case "created":
            return makeSettingsTableCell(text: pw.createdAt, secondary: true)
        default:
            return nil
        }
    }

    private func updateTableHeight() {
        passwordsSectionView?.updateManagedTableLayout(rowCount: passwords.count)
    }
}
