import AppKit

final class ContentBlockerSettingsViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver {

    private struct FilterListEntry: Codable {
        let id: String
        let name: String
        let url: String
        let enabled: Bool
        let ruleCount: Int
    }

    private var masterSwitch = NSSwitch()
    private var tableView = NSTableView()
    private var filterLists: [FilterListEntry] = []
    private let filterListsEmptyStateLabel = makeSettingsCompactEmptyStateLabel("No filter lists added")
    private var filterListsSectionView: SettingsManagedTableSectionView?

    private var totalRulesLabel = NSTextField(labelWithString: "Total rules: 0")
    private var blockedTodayLabel = NSTextField(labelWithString: "Blocked today: 0")
    private var siteExceptions: [String] = []
    private var siteExceptionsTableView = NSTableView()
    private let siteExceptionsEmptyStateLabel = makeSettingsCompactEmptyStateLabel("No per-site exceptions")
    private var siteExceptionsSectionView: SettingsManagedTableSectionView?

    private static let exceptionsKey = "maho_content_blocker_exceptions"
    private static let blockedCountKey = "maho_blocked_count"
    private static let blockedDateKey = "maho_blocked_date"
    private static let compactEmptyStateHeight: CGFloat = 54

    override func loadView() {
        masterSwitch.target = self
        masterSwitch.action = #selector(masterToggleChanged(_:))
        let masterRow = makeSettingsLabeledRow("Content Blocker:", masterSwitch)

        let nameColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("name"))
        nameColumn.title = "Name"
        nameColumn.width = 250
        nameColumn.resizingMask = .autoresizingMask
        tableView.addTableColumn(nameColumn)

        let ruleCountColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("ruleCount"))
        ruleCountColumn.title = "Rules"
        ruleCountColumn.width = 80
        ruleCountColumn.resizingMask = []
        tableView.addTableColumn(ruleCountColumn)

        let enabledColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("enabled"))
        enabledColumn.title = "Enabled"
        enabledColumn.width = 80
        enabledColumn.resizingMask = []
        tableView.addTableColumn(enabledColumn)

        tableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle

        tableView.headerView = NSTableHeaderView()
        tableView.delegate = self
        tableView.dataSource = self

        let addButton = NSButton(title: "Add Filter List", target: self, action: #selector(addFilterList(_:)))
        let removeButton = NSButton(title: "Remove", target: self, action: #selector(removeFilterList(_:)))
        let filterListActionRow = makeSettingsActionRow([addButton, removeButton])

        let compileButton = NSButton(title: "Compile Rules Now", target: self, action: #selector(compileNow(_:)))
        let compileActionRow = makeSettingsActionRow([compileButton])

        totalRulesLabel.font = .systemFont(ofSize: 12)
        totalRulesLabel.textColor = .secondaryLabelColor
        blockedTodayLabel.font = .systemFont(ofSize: 12)
        blockedTodayLabel.textColor = .secondaryLabelColor
        let statsRow = makeSettingsPlainRow([totalRulesLabel, blockedTodayLabel], spacing: 16)

        let filterListsSectionView = SettingsManagedTableSectionView(
            tableView: tableView,
            containerStyle: .mixedCard(
                footerViews: [filterListActionRow, compileActionRow, statsRow]
            ),
            shellStyle: .compact(
                emptyStateView: filterListsEmptyStateLabel,
                emptyStateHeight: Self.compactEmptyStateHeight
            )
        )
        self.filterListsSectionView = filterListsSectionView

        let siteColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("site"))
        siteColumn.title = "Site"
        siteColumn.width = 300
        siteColumn.resizingMask = .autoresizingMask
        siteExceptionsTableView.addTableColumn(siteColumn)

        siteExceptionsTableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle
        siteExceptionsTableView.headerView = NSTableHeaderView()
        siteExceptionsTableView.tag = 1
        siteExceptionsTableView.delegate = self
        siteExceptionsTableView.dataSource = self

        let addExceptionButton = NSButton(title: "Add Exception", target: self, action: #selector(addSiteException(_:)))
        let removeExceptionButton = NSButton(title: "Remove", target: self, action: #selector(removeSiteException(_:)))
        let siteExceptionActionRow = makeSettingsActionRow([addExceptionButton, removeExceptionButton])

        let siteExceptionsSectionView = SettingsManagedTableSectionView(
            tableView: siteExceptionsTableView,
            containerStyle: .mixedCard(
                headerViews: [makeSettingsSectionTitle("Per-Site Exceptions:")],
                footerViews: [siteExceptionActionRow]
            ),
            shellStyle: .compact(
                emptyStateView: siteExceptionsEmptyStateLabel,
                emptyStateHeight: Self.compactEmptyStateHeight
            )
        )
        self.siteExceptionsSectionView = siteExceptionsSectionView

        view = SettingsPaneHostView(sections: [
            .plain([masterRow]),
            .managedTable(filterListsSectionView),
            .managedTable(siteExceptionsSectionView)
        ])

        updateFilterListsTableHeight()
        updateSiteExceptionsTableHeight()
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        loadData()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            switch update {
            case .filterListUpdated:
                self.loadData()
            case .contentRulesCompiled(let ruleCount):
                self.totalRulesLabel.stringValue = "Total rules: \(ruleCount)"
            case .contentBlockerStateChanged(let enabled, _):
                self.masterSwitch.state = enabled ? .on : .off
            case .settingsChanged(let settings):
                self.masterSwitch.state = settings.privacy.contentBlockerEnabled ? .on : .off
            default:
                break
            }
        }
    }

    private func loadData() {
        if let settings = MahoBridge.shared.getSettings() {
            masterSwitch.state = settings.privacy.contentBlockerEnabled ? .on : .off
        }
        if let json = MahoBridge.shared.getFilterLists() {
            if let entries: [FilterListEntry] = MahoBridge.shared.decode(json) {
                filterLists = entries
            }
        }
        let totalRules = filterLists.reduce(0) { $0 + $1.ruleCount }
        totalRulesLabel.stringValue = "Total rules: \(totalRules)"
        siteExceptions = UserDefaults.standard.stringArray(forKey: Self.exceptionsKey) ?? []
        updateBlockedTodayLabel()
        tableView.reloadData()
        siteExceptionsTableView.reloadData()
        updateFilterListsTableHeight()
        updateSiteExceptionsTableHeight()
    }

    @objc private func masterToggleChanged(_ sender: NSSwitch) {
        let update = SettingsUpdate(
            general: nil, appearance: nil,
            privacy: PrivacySettingsUpdate(
                doNotTrack: nil, blockThirdPartyCookies: nil,
                contentBlockerEnabled: sender.state == .on,
                popupBlockerEnabled: nil, searchSuggestionsEnabled: nil,
                secureDnsEnabled: nil, secureDnsProvider: nil,
                secureDnsCustomUrl: nil, clearDataOnExit: nil,
                safeBrowsingEnabled: nil
            ),
            reader: nil, keyboardShortcuts: nil, perSiteSettings: nil,
            toolbarItems: nil, max: nil, autofill: nil, advanced: nil,
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func addFilterList(_ sender: NSButton) {
        let alert = NSAlert()
        alert.messageText = "Add Filter List"
        alert.informativeText = "Enter the name and URL for the filter list."
        alert.addButton(withTitle: "Add")
        alert.addButton(withTitle: "Cancel")

        let inputStack = NSStackView()
        inputStack.orientation = .vertical
        inputStack.spacing = 8

        let nameField = NSTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
        nameField.placeholderString = "Filter list name"
        styleSettingsEditableTextField(nameField)

        let urlField = NSTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
        urlField.placeholderString = "https://example.com/filters.txt"
        styleSettingsEditableTextField(urlField)

        inputStack.addArrangedSubview(nameField)
        inputStack.addArrangedSubview(urlField)
        inputStack.frame = NSRect(x: 0, y: 0, width: 300, height: 56)
        alert.accessoryView = inputStack

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let name = nameField.stringValue
        let url = urlField.stringValue
        guard !name.isEmpty, !url.isEmpty else { return }

        CoreUpdateDispatcher.shared.sendAndDispatch(
            .addFilterList(id: UUID().uuidString, name: name, url: url)
        )
    }

    @objc private func removeFilterList(_ sender: NSButton) {
        let row = tableView.selectedRow
        guard row >= 0, row < filterLists.count else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.removeFilterList(id: filterLists[row].id))
        filterLists.remove(at: row)
        tableView.reloadData()
        updateFilterListsTableHeight()
    }

    @objc private func filterToggleChanged(_ sender: NSSwitch) {
        let row = sender.tag
        guard row >= 0, row < filterLists.count else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(
            .toggleFilterList(id: filterLists[row].id, enabled: sender.state == .on)
        )
    }

    // MARK: - NSTableViewDataSource

    func numberOfRows(in tableView: NSTableView) -> Int {
        if tableView.tag == 1 {
            return siteExceptions.count
        }
        return filterLists.count
    }

    // MARK: - NSTableViewDelegate

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        if tableView.tag == 1 {
            guard row < siteExceptions.count else { return nil }
            return makeSettingsTableCell(text: siteExceptions[row])
        }

        guard row < filterLists.count else { return nil }
        let entry = filterLists[row]

        switch tableColumn?.identifier.rawValue {
        case "name":
            return makeSettingsTableCell(text: entry.name)
        case "ruleCount":
            return makeSettingsTableCell(text: "\(entry.ruleCount)", secondary: true)
        case "enabled":
            let toggle = NSSwitch()
            toggle.state = entry.enabled ? .on : .off
            toggle.target = self
            toggle.action = #selector(filterToggleChanged(_:))
            toggle.tag = row
            return makeSettingsCenteredTableActionCell(toggle)
        default:
            return nil
        }
    }

    @objc private func compileNow(_ sender: NSButton) {
        MahoBridge.shared.rebuildContentRules()
    }

    @objc private func addSiteException(_ sender: NSButton) {
        let alert = NSAlert()
        alert.messageText = "Add Site Exception"
        alert.informativeText = "Enter the domain to exclude from content blocking."
        alert.addButton(withTitle: "Add")
        alert.addButton(withTitle: "Cancel")

        let domainField = NSTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 24))
        domainField.placeholderString = "example.com"
        styleSettingsEditableTextField(domainField)
        alert.accessoryView = domainField

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let domain = domainField.stringValue.trimmingCharacters(in: .whitespaces)
        guard !domain.isEmpty, !siteExceptions.contains(domain) else { return }

        siteExceptions.append(domain)
        UserDefaults.standard.set(siteExceptions, forKey: Self.exceptionsKey)
        siteExceptionsTableView.reloadData()
        updateSiteExceptionsTableHeight()
    }

    @objc private func removeSiteException(_ sender: NSButton) {
        let row = siteExceptionsTableView.selectedRow
        guard row >= 0, row < siteExceptions.count else { return }
        siteExceptions.remove(at: row)
        UserDefaults.standard.set(siteExceptions, forKey: Self.exceptionsKey)
        siteExceptionsTableView.reloadData()
        updateSiteExceptionsTableHeight()
    }

    private func updateBlockedTodayLabel() {
        let today = todayString()
        let savedDate = UserDefaults.standard.string(forKey: Self.blockedDateKey) ?? ""
        let count: Int
        if savedDate == today {
            count = UserDefaults.standard.integer(forKey: Self.blockedCountKey)
        } else {
            count = 0
            UserDefaults.standard.set(0, forKey: Self.blockedCountKey)
            UserDefaults.standard.set(today, forKey: Self.blockedDateKey)
        }
        blockedTodayLabel.stringValue = "Blocked today: \(count)"
    }

    func incrementBlockedCount() {
        let today = todayString()
        let savedDate = UserDefaults.standard.string(forKey: Self.blockedDateKey) ?? ""
        var count = 0
        if savedDate == today {
            count = UserDefaults.standard.integer(forKey: Self.blockedCountKey)
        } else {
            UserDefaults.standard.set(today, forKey: Self.blockedDateKey)
        }
        count += 1
        UserDefaults.standard.set(count, forKey: Self.blockedCountKey)
        blockedTodayLabel.stringValue = "Blocked today: \(count)"
    }

    private func todayString() -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd"
        return formatter.string(from: Date())
    }

    private func updateFilterListsTableHeight() {
        filterListsSectionView?.updateManagedTableLayout(rowCount: filterLists.count)
    }

    private func updateSiteExceptionsTableHeight() {
        siteExceptionsSectionView?.updateManagedTableLayout(rowCount: siteExceptions.count)
    }

}
