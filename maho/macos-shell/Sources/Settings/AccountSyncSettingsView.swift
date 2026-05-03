import AppKit

final class AccountSyncSettingsViewController: NSViewController, SettingsPaneRefreshable, CoreUpdateObserver {
    private var serverUrlField = NSTextField()
    private var emailField = NSTextField()
    private var passwordField = NSSecureTextField()
    private var syncKeyField = NSTextField()
    private var syncStatusLabel = NSTextField(labelWithString: "")
    private var authStatusLabel = NSTextField(labelWithString: "")
    private var lastSyncLabel = NSTextField(labelWithString: "")
    private var generatedKeyLabel = NSTextField(labelWithString: "")
    private var devicesTableView = NSTableView()
    private var devices: [ConnectedDeviceInfo] = []
    private var syncToggleButton = NSButton()
    private var syncNowButton = NSButton()
    private var authActionButton = NSButton()
    private var loginButton = NSButton()
    private var logoutButton = NSButton()
    private var devicesSectionView: SettingsManagedTableSectionView?
    private var currentAccount: AccountInfo?

    override func loadView() {
        let statusHeader = NSTextField(labelWithString: "Sync Status")
        statusHeader.font = .systemFont(ofSize: 13, weight: .semibold)
        styleSettingsLabel(statusHeader)

        syncStatusLabel.font = .systemFont(ofSize: 12)
        styleSettingsLabel(syncStatusLabel, secondary: true)
        syncStatusLabel.maximumNumberOfLines = 2
        syncStatusLabel.lineBreakMode = .byWordWrapping

        authStatusLabel.font = .systemFont(ofSize: 11)
        styleSettingsLabel(authStatusLabel, secondary: true)
        authStatusLabel.maximumNumberOfLines = 2
        authStatusLabel.lineBreakMode = .byWordWrapping

        lastSyncLabel.font = .systemFont(ofSize: 11)
        styleSettingsLabel(lastSyncLabel, secondary: true)

        syncToggleButton = NSButton(title: "Start Polling", target: self, action: #selector(toggleSyncClicked(_:)))
        syncNowButton = NSButton(title: "Sync Now", target: self, action: #selector(syncNowClicked(_:)))
        syncNowButton.isEnabled = false
        let syncControlRow = makeSettingsActionRow([syncToggleButton, syncNowButton])
        let syncStatusCard = makeSettingsSectionCard([
            statusHeader,
            syncStatusLabel,
            authStatusLabel,
            lastSyncLabel,
            syncControlRow,
        ])

        let authHeader = NSTextField(labelWithString: "Relay Account")
        authHeader.font = .systemFont(ofSize: 13, weight: .semibold)
        styleSettingsLabel(authHeader)

        serverUrlField.placeholderString = "https://sync.maho.app"
        styleSettingsEditableTextField(serverUrlField)
        let serverUrlRow = makeSettingsLabeledRow("Server URL:", serverUrlField, embedded: true)

        emailField.placeholderString = "name@example.com"
        styleSettingsEditableTextField(emailField)
        let emailRow = makeSettingsLabeledRow("Email:", emailField, embedded: true)

        passwordField.placeholderString = "Password"
        styleSettingsEditableTextField(passwordField)
        let passwordRow = makeSettingsLabeledRow("Password:", passwordField, embedded: true)

        authActionButton = NSButton(title: "Sign Up", target: self, action: #selector(signUpClicked(_:)))
        loginButton = NSButton(title: "Log In", target: self, action: #selector(loginClicked(_:)))
        logoutButton = NSButton(title: "Log Out", target: self, action: #selector(logoutClicked(_:)))
        let authActionRow = makeSettingsActionRow([authActionButton, loginButton, logoutButton])
        let authCard = makeSettingsSectionCard([
            authHeader,
            serverUrlRow,
            emailRow,
            passwordRow,
            authActionRow,
        ])

        let syncConfigHeader = NSTextField(labelWithString: "Polling Sync")
        syncConfigHeader.font = .systemFont(ofSize: 13, weight: .semibold)
        styleSettingsLabel(syncConfigHeader)

        syncKeyField.placeholderString = "Sync Key"
        styleSettingsEditableTextField(syncKeyField)
        let syncKeyRow = makeSettingsLabeledRow("Sync Key:", syncKeyField, embedded: true)

        let startButton = NSButton(title: "Save & Start", target: self, action: #selector(startSyncClicked(_:)))
        let stopButton = NSButton(title: "Stop Polling", target: self, action: #selector(stopSyncClicked(_:)))
        let genKeyButton = NSButton(title: "Generate Sync Key", target: self, action: #selector(generateKeyClicked(_:)))
        let startRow = makeSettingsActionRow([startButton, stopButton, genKeyButton])
        generatedKeyLabel.font = .systemFont(ofSize: 11)
        generatedKeyLabel.isSelectable = true
        styleSettingsLabel(generatedKeyLabel, secondary: true)
        let copyKeyButton = NSButton(title: "Copy Key", target: self, action: #selector(copyKeyClicked(_:)))
        let keyRow = makeSettingsPlainRow([generatedKeyLabel, copyKeyButton])
        let configCard = makeSettingsSectionCard([
            syncConfigHeader,
            syncKeyRow,
            startRow,
            keyRow,
        ])

        let devicesHeader = NSTextField(labelWithString: "Connected Devices")
        devicesHeader.font = .systemFont(ofSize: 13, weight: .semibold)
        styleSettingsLabel(devicesHeader)

        devicesTableView.delegate = self
        devicesTableView.dataSource = self
        let nameCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("DeviceName"))
        nameCol.title = "Name"
        nameCol.width = 200
        nameCol.resizingMask = .autoresizingMask
        devicesTableView.addTableColumn(nameCol)
        let lastSeenCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("DeviceLastSeen"))
        lastSeenCol.title = "Last Seen"
        lastSeenCol.width = 150
        lastSeenCol.resizingMask = []
        devicesTableView.addTableColumn(lastSeenCol)

        devicesTableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle

        let removeDeviceButton = NSButton(title: "Remove Device", target: self, action: #selector(removeDeviceClicked(_:)))
        let sendTabButton = NSButton(title: "Send Tab to Device", target: self, action: #selector(sendTabClicked(_:)))
        let deviceActionRow = makeSettingsActionRow([removeDeviceButton, sendTabButton])
        let devicesSectionView = SettingsManagedTableSectionView(
            tableView: devicesTableView,
            containerStyle: .mixedCard(
                headerViews: [devicesHeader],
                footerViews: [deviceActionRow]
            ),
            shellStyle: .compact()
        )
        self.devicesSectionView = devicesSectionView

        let backupHeader = NSTextField(labelWithString: "Backup")
        backupHeader.font = .systemFont(ofSize: 13, weight: .semibold)
        styleSettingsLabel(backupHeader)

        let createBackupButton = NSButton(title: "Create Backup", target: self, action: #selector(createBackupClicked(_:)))
        let restoreBackupButton = NSButton(title: "Restore Backup", target: self, action: #selector(restoreBackupClicked(_:)))
        let backupRow = makeSettingsActionRow([createBackupButton, restoreBackupButton])
        let backupCard = makeSettingsSectionCard([
            backupHeader,
            backupRow,
        ])

        view = SettingsPaneHostView(sections: [
            .plain([syncStatusCard]),
            .plain([authCard]),
            .plain([configCard]),
            .managedTable(devicesSectionView),
            .plain([backupCard]),
        ])
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        serverUrlField.stringValue = UserDefaults.standard.string(forKey: "maho_sync_server_url") ?? "https://sync.maho.app"
        syncKeyField.stringValue = UserDefaults.standard.string(forKey: "maho_sync_key") ?? ""
        if let session = RelayAuthManager.shared.session(for: serverUrlField.stringValue) {
            emailField.stringValue = session.email
        }
        NotificationCenter.default.addObserver(self, selector: #selector(onSyncStatusChanged(_:)), name: .syncStatusChanged, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(onSyncDevicesChanged), name: .syncDevicesChanged, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(onRelayAuthStateChanged(_:)), name: RelayAuthManager.authStateChangedNotification, object: nil)
        refreshStatus()
        refreshDevices()
        updateSyncControls()
        refreshAuthState()
    }

    func refreshSettingsContent() {
        refreshStatus()
        refreshDevices()
        updateSyncControls()
        refreshAuthState()
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .accountChanged(let account):
            currentAccount = account
            DispatchQueue.main.async { [weak self] in
                self?.refreshAuthState()
                self?.refreshStatus()
            }
        case .accountSyncStateChanged:
            DispatchQueue.main.async { [weak self] in
                self?.updateSyncControls()
                self?.refreshStatus()
            }
        default:
            break
        }
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
        NotificationCenter.default.removeObserver(self)
    }

    @objc private func onSyncStatusChanged(_ notification: Notification) {
        refreshStatus()
        updateSyncControls()
        if let error = notification.userInfo?["error"] as? String {
            syncStatusLabel.stringValue = "Error: \(error)"
        }
    }

    @objc private func onSyncDevicesChanged() {
        refreshDevices()
    }

    @objc private func onRelayAuthStateChanged(_ notification: Notification) {
        refreshAuthState()
        refreshStatus()
        updateSyncControls()
    }

    private func updateSyncControls() {
        let syncing = SyncManager.shared.isSyncing
        syncToggleButton.title = syncing ? "Stop Polling" : "Start Polling"
        syncNowButton.isEnabled = syncing
        if let date = SyncManager.shared.lastSyncDate {
            let formatter = DateFormatter()
            formatter.dateStyle = .short
            formatter.timeStyle = .medium
            lastSyncLabel.stringValue = "Last sync attempt: \(formatter.string(from: date))"
        } else {
            lastSyncLabel.stringValue = "Last sync attempt: Never"
        }
    }

    private func refreshAuthState() {
        let serverURL = serverUrlField.stringValue
        if let session = RelayAuthManager.shared.session(for: serverURL) {
            emailField.stringValue = session.email
            authStatusLabel.stringValue = "Logged in as \(session.email). Relay tokens are stored securely on this Mac."
            logoutButton.isEnabled = true
        } else if let account = currentAccount {
            emailField.stringValue = account.email
            authStatusLabel.stringValue = "Account state is active for \(account.email), but relay tokens are not available for the current server URL."
            logoutButton.isEnabled = false
        } else {
            authStatusLabel.stringValue = "Create or log into a relay account before starting periodic sync."
            logoutButton.isEnabled = false
        }
    }

    private func refreshStatus() {
        if let json = MahoBridge.shared.getSyncStatus(),
           let status: SyncStatusInfo = MahoBridge.shared.decode(json) {
            let active = status.isActive ? "Polling enabled" : "Polling stopped"
            let lastSync = status.lastSyncAt ?? "Never"
            syncStatusLabel.stringValue = "\(active) | Last applied sync: \(lastSync) | Devices: \(status.deviceCount)"
        } else {
            syncStatusLabel.stringValue = "Unable to fetch sync status"
        }
    }

    private func refreshDevices() {
        if let json = MahoBridge.shared.getConnectedDevices(),
           let decoded: [ConnectedDeviceInfo] = MahoBridge.shared.decode(json) {
            devices = decoded
        } else {
            devices = []
        }
        devicesTableView.reloadData()
    }

    private func persistServerFields() {
        UserDefaults.standard.set(serverUrlField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines), forKey: "maho_sync_server_url")
        UserDefaults.standard.set(syncKeyField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines), forKey: "maho_sync_key")
    }

    private func presentError(_ message: String) {
        let alert = NSAlert()
        alert.messageText = "Relay Sync"
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }

    private func startRelayAuth(_ action: RelayAuthAction) {
        persistServerFields()
        let serverURL = serverUrlField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let email = emailField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let password = passwordField.stringValue

        Task { @MainActor [weak self] in
            guard let self else { return }
            do {
                _ = try await RelayAuthManager.shared.authenticate(
                    action: action,
                    serverURL: serverURL,
                    email: email,
                    password: password
                )
                passwordField.stringValue = ""
                refreshAuthState()
                refreshStatus()
            } catch {
                presentError((error as? LocalizedError)?.errorDescription ?? error.localizedDescription)
            }
        }
    }

    @objc private func toggleSyncClicked(_ sender: NSButton) {
        if SyncManager.shared.isSyncing {
            SyncManager.shared.stopSync()
        } else {
            startSyncFlow()
        }
    }

    @objc private func syncNowClicked(_ sender: NSButton) {
        SyncManager.shared.syncNow()
    }

    @objc private func signUpClicked(_ sender: NSButton) {
        startRelayAuth(.signUp)
    }

    @objc private func loginClicked(_ sender: NSButton) {
        startRelayAuth(.logIn)
    }

    @objc private func logoutClicked(_ sender: NSButton) {
        let serverURL = serverUrlField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        Task { @MainActor [weak self] in
            guard let self else { return }
            SyncManager.shared.stopSync()
            await RelayAuthManager.shared.signOut(serverURL: serverURL)
            passwordField.stringValue = ""
            refreshAuthState()
            refreshStatus()
        }
    }

    private func startSyncFlow() {
        let serverUrl = serverUrlField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let syncKey = syncKeyField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !serverUrl.isEmpty, !syncKey.isEmpty else {
            presentError("Enter both the relay server URL and sync key before starting sync.")
            return
        }
        guard RelayAuthManager.shared.hasSession(for: serverUrl) else {
            presentError("Log in to the relay for this server URL before starting sync.")
            return
        }
        persistServerFields()
        SyncManager.shared.startSync(serverUrl: serverUrl, syncKey: syncKey)
        refreshStatus()
        updateSyncControls()
    }

    @objc private func startSyncClicked(_ sender: NSButton) {
        startSyncFlow()
    }

    @objc private func stopSyncClicked(_ sender: NSButton) {
        SyncManager.shared.stopSync()
        refreshStatus()
        updateSyncControls()
    }

    @objc private func generateKeyClicked(_ sender: NSButton) {
        if let key = MahoBridge.shared.generateSyncKey() {
            generatedKeyLabel.stringValue = key
        }
    }

    @objc private func removeDeviceClicked(_ sender: NSButton) {
        let row = devicesTableView.selectedRow
        guard row >= 0, row < devices.count else { return }
        MahoBridge.shared.removeSyncDevice(deviceId: devices[row].id)
        refreshDevices()
    }

    @objc private func copyKeyClicked(_ sender: NSButton) {
        let key = generatedKeyLabel.stringValue
        guard !key.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(key, forType: .string)
    }

    @objc private func sendTabClicked(_ sender: NSButton) {
        let row = devicesTableView.selectedRow
        guard row >= 0, row < devices.count else { return }
        let device = devices[row]

        guard let appDelegate = NSApp.delegate as? AppDelegate,
              let mainWC = appDelegate.mainWindowControllerRef,
              let tabId = mainWC.activeTabId else {
            let alert = NSAlert()
            alert.messageText = "No Active Tab"
            alert.informativeText = "Open a tab before sending it to another device."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }

        let tabs = MahoBridge.shared.getTabViewModels()
        guard let tab = tabs.first(where: { $0.id == tabId }) else {
            let alert = NSAlert()
            alert.messageText = "Tab Not Found"
            alert.informativeText = "Could not retrieve the active tab information."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }

        MahoBridge.shared.sendTab(url: tab.url, title: tab.title, targetDeviceId: device.id)
    }

    @objc private func createBackupClicked(_ sender: NSButton) {
        let result = MahoBridge.shared.createBackup(configJson: "{}")
        let alert = NSAlert()
        if let backupData = result, !backupData.isEmpty {
            alert.messageText = "Backup Created"
            alert.informativeText = "Sync data backup was created successfully."
            alert.alertStyle = .informational
        } else {
            alert.messageText = "Backup Failed"
            alert.informativeText = "Could not create sync backup. Check that sync is configured and try again."
            alert.alertStyle = .warning
        }
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }

    @objc private func restoreBackupClicked(_ sender: NSButton) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        guard let data = try? Data(contentsOf: url) else {
            let alert = NSAlert()
            alert.messageText = "Read Error"
            alert.informativeText = "Could not read the selected backup file."
            alert.alertStyle = .warning
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }

        let passwordAlert = NSAlert()
        passwordAlert.messageText = "Backup Password"
        passwordAlert.informativeText = "Enter the password for this backup, or leave blank if none was set."
        passwordAlert.addButton(withTitle: "Restore")
        passwordAlert.addButton(withTitle: "Cancel")
        let passwordField = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        passwordField.placeholderString = "Password (optional)"
        passwordAlert.accessoryView = passwordField
        guard passwordAlert.runModal() == .alertFirstButtonReturn else { return }
        let password = passwordField.stringValue

        let b64 = data.base64EncodedString()
        let result = MahoBridge.shared.restoreBackup(backupB64: b64, password: password)
        let resultAlert = NSAlert()
        if let r = result, !r.isEmpty {
            resultAlert.messageText = "Backup Restored"
            resultAlert.informativeText = "Sync data was restored successfully."
            resultAlert.alertStyle = .informational
        } else {
            resultAlert.messageText = "Restore Failed"
            resultAlert.informativeText = "Could not restore from the selected backup. The file may be corrupted or the password incorrect."
            resultAlert.alertStyle = .warning
        }
        resultAlert.addButton(withTitle: "OK")
        resultAlert.runModal()
        refreshStatus()
    }
}

private struct SyncStatusInfo: Codable {
    let isActive: Bool
    let lastSyncAt: String?
    let deviceCount: Int
}

private struct ConnectedDeviceInfo: Codable {
    let id: String
    let name: String
    let lastSeen: String?
}

extension AccountSyncSettingsViewController: NSTableViewDelegate, NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        devices.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < devices.count else { return nil }
        let device = devices[row]
        let colId = tableColumn?.identifier.rawValue ?? ""
        switch colId {
        case "DeviceName":
            return makeSettingsTableCell(text: device.name)
        case "DeviceLastSeen":
            return makeSettingsTableCell(text: device.lastSeen ?? "Unknown", secondary: true)
        default:
            return nil
        }
    }
}
