import AppKit

final class PasswordsSettingsViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver {

    private let passwordsEnabledSwitch = NSSwitch()
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
        passwordsEnabledSwitch.target = self
        passwordsEnabledSwitch.action = #selector(passwordsEnabledChanged(_:))

        providerPopup.removeAllItems()
        providerPopup.addItems(withTitles: ["Chromium", "1Password", "Bitwarden"])
        providerPopup.target = self
        providerPopup.action = #selector(providerChanged(_:))

        let passwordsEnabledRow = makeSettingsLabeledRow("Passwords Enabled:", passwordsEnabledSwitch)
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
            .plain([passwordsEnabledRow, providerRow]),
            .managedTable(passwordsSectionView)
        ])

        updateTableHeight()
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        CoreUpdateDispatcher.shared.sendAndDispatch(.searchPasswords(query: ""))

        if let settings = MahoBridge.shared.getSettings() {
            passwordsEnabledSwitch.state = settings.autofill.passwordsEnabled ? .on : .off
            switch settings.autofill.passwordProvider {
            case .chromium:
                providerPopup.selectItem(at: 0)
            case .onepassword:
                providerPopup.selectItem(at: 1)
            case .bitwarden:
                providerPopup.selectItem(at: 2)
            }
            updateProviderState(passwordsEnabled: settings.autofill.passwordsEnabled, provider: settings.autofill.passwordProvider)
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
            passwordsEnabledSwitch.state = settings.autofill.passwordsEnabled ? .on : .off
            switch settings.autofill.passwordProvider {
            case .chromium:
                providerPopup.selectItem(at: 0)
            case .onepassword:
                providerPopup.selectItem(at: 1)
            case .bitwarden:
                providerPopup.selectItem(at: 2)
            }
            updateProviderState(passwordsEnabled: settings.autofill.passwordsEnabled, provider: settings.autofill.passwordProvider)
        case .passwordDeleted:
            CoreUpdateDispatcher.shared.sendAndDispatch(.searchPasswords(query: searchField.stringValue))
        default:
            break
        }
    }

    @objc private func passwordsEnabledChanged(_ sender: NSSwitch) {
        let update = SettingsUpdate(
            general: nil, appearance: nil, privacy: nil, reader: nil,
            keyboardShortcuts: nil, perSiteSettings: nil, toolbarItems: nil, max: nil,
            autofill: AutofillSettingsUpdate(
                addressesEnabled: nil,
                paymentsEnabled: nil,
                passwordsEnabled: sender.state == .on,
                passwordProvider: nil
            ),
            advanced: nil, notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))

        updateProviderState(passwordsEnabled: sender.state == .on, provider: MahoBridge.shared.getSettings()?.autofill.passwordProvider ?? .chromium)
    }

    @objc private func providerChanged(_ sender: NSPopUpButton) {
        let provider: PasswordAutofillProvider
        switch sender.indexOfSelectedItem {
        case 0:
            provider = .chromium
        case 1:
            provider = .onepassword
        case 2:
            provider = .bitwarden
        default:
            provider = .chromium
        }

        let currentSettings = MahoBridge.shared.getSettings()?.autofill
        let currentProvider = currentSettings?.passwordProvider ?? .chromium
        let passwordsEnabled = currentSettings?.passwordsEnabled ?? true

        if currentProvider == .chromium && provider != .chromium && !passwords.isEmpty {
            let providerName: String
            switch provider {
            case .chromium:
                providerName = "Chromium"
            case .onepassword:
                providerName = "1Password"
            case .bitwarden:
                providerName = "Bitwarden"
            }
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
                case .chromium:
                    sender.selectItem(at: 0)
                case .onepassword:
                    sender.selectItem(at: 1)
                case .bitwarden:
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
                passwordsEnabled: passwordsEnabled,
                passwordProvider: provider
            ),
            advanced: nil, notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))

        updateProviderState(passwordsEnabled: passwordsEnabled, provider: provider)
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

    private func updateProviderState(passwordsEnabled: Bool, provider: PasswordAutofillProvider) {
        let isChromium = provider == .chromium
        let isInteractive = passwordsEnabled && isChromium
        passwordsEnabledSwitch.state = passwordsEnabled ? .on : .off
        providerPopup.isEnabled = passwordsEnabled
        searchField.isEnabled = isInteractive
        tableView.alphaValue = isInteractive ? 1.0 : 0.4
        addButton.isEnabled = isInteractive
        deleteButton.isEnabled = isInteractive
        exportButton.isEnabled = isInteractive
        passwordsSectionView?.setManagedTableShellAlpha(isInteractive ? 1.0 : 0.4)
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
