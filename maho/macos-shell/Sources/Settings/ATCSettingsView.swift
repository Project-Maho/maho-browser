import AppKit

final class ATCSettingsViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver {

    private var coreTableView = NSTableView()
    private var rules: [ATCRule] = []

    private var conditionPopup = NSPopUpButton()
    private var conditionValueField = NSTextField()
    private var actionPopup = NSPopUpButton()
    private var actionValueField = NSTextField()
    private var addRuleStack = NSStackView()

    private var linkTableView = NSTableView()
    private var linkDomainField = NSTextField()
    private var linkSpacePopup = NSPopUpButton()
    private var linkProfilePopup = NSPopUpButton()
    private var linkNewTabCheckbox = NSButton()
    private var spaces: [SpaceViewModel] = []
    private var coreRulesSectionView: SettingsManagedTableSectionView?
    private var linkRulesSectionView: SettingsManagedTableSectionView?

    override func loadView() {
        let coreLabel = NSTextField(labelWithString: "Tab Lifecycle Rules")
        coreLabel.font = NSFont.systemFont(ofSize: 13, weight: .semibold)
        styleSettingsLabel(coreLabel)

        let conditionColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("condition"))
        conditionColumn.title = "Condition"
        conditionColumn.width = 250
        conditionColumn.resizingMask = .autoresizingMask
        coreTableView.addTableColumn(conditionColumn)

        let actionColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("action"))
        actionColumn.title = "Action"
        actionColumn.width = 150
        actionColumn.resizingMask = []
        coreTableView.addTableColumn(actionColumn)

        let enabledColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("enabled"))
        enabledColumn.title = "Enabled"
        enabledColumn.width = 80
        enabledColumn.resizingMask = []
        coreTableView.addTableColumn(enabledColumn)

        coreTableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle

        coreTableView.headerView = NSTableHeaderView()
        styleSettingsTable(coreTableView)
        coreTableView.delegate = self
        coreTableView.dataSource = self
        coreTableView.tag = 1

        conditionPopup.addItems(withTitles: [
            "URL Contains", "URL Equals", "URL Pattern", "Inactive Duration", "Tab Count Exceeded"
        ])

        conditionValueField.placeholderString = "Value"
        styleSettingsEditableTextField(conditionValueField)
        conditionValueField.widthAnchor.constraint(equalToConstant: 160).isActive = true

        actionPopup.addItems(withTitles: ["Close", "Archive", "Route to Space"])
        actionPopup.target = self
        actionPopup.action = #selector(actionPopupChanged(_:))

        actionValueField.placeholderString = "Space ID"
        styleSettingsEditableTextField(actionValueField)
        actionValueField.widthAnchor.constraint(equalToConstant: 120).isActive = true
        actionValueField.isHidden = true

        addRuleStack = NSStackView(views: [
            conditionPopup, conditionValueField, actionPopup, actionValueField
        ])
        addRuleStack.orientation = .horizontal
        addRuleStack.spacing = 8

        let addButton = NSButton(title: "Add Rule", target: self, action: #selector(addRule(_:)))
        let removeButton = NSButton(title: "Remove", target: self, action: #selector(removeRule(_:)))

        let coreRulesSectionView = SettingsManagedTableSectionView(
            tableView: coreTableView,
            containerStyle: .mixedCard(
                headerViews: [coreLabel, addRuleStack],
                footerViews: [makeSettingsActionRow([addButton, removeButton], spacing: 8)]
            ),
            shellStyle: .fixedHeight(160)
        )
        self.coreRulesSectionView = coreRulesSectionView

        let linkLabel = NSTextField(labelWithString: "Link Routing Rules")
        linkLabel.font = NSFont.systemFont(ofSize: 13, weight: .semibold)
        styleSettingsLabel(linkLabel)

        let linkEnabledCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("linkEnabled"))
        linkEnabledCol.title = "On"
        linkEnabledCol.width = 50
        linkEnabledCol.resizingMask = []
        linkEnabledCol.minWidth = 50
        linkEnabledCol.maxWidth = 50
        linkTableView.addTableColumn(linkEnabledCol)

        let domainCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("linkDomain"))
        domainCol.title = "Domain Pattern"
        domainCol.width = 200
        domainCol.resizingMask = .autoresizingMask
        linkTableView.addTableColumn(domainCol)

        let spaceCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("linkSpace"))
        spaceCol.title = "Target Space"
        spaceCol.width = 120
        spaceCol.resizingMask = []
        spaceCol.minWidth = 120
        spaceCol.maxWidth = 120
        linkTableView.addTableColumn(spaceCol)

        let profileCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("linkProfile"))
        profileCol.title = "Profile"
        profileCol.width = 80
        profileCol.resizingMask = []
        profileCol.minWidth = 80
        profileCol.maxWidth = 80
        linkTableView.addTableColumn(profileCol)

        let newTabCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("linkNewTab"))
        newTabCol.title = "New Tab"
        newTabCol.width = 60
        newTabCol.resizingMask = []
        newTabCol.minWidth = 60
        newTabCol.maxWidth = 60
        linkTableView.addTableColumn(newTabCol)

        linkTableView.columnAutoresizingStyle = .sequentialColumnAutoresizingStyle

        linkTableView.headerView = NSTableHeaderView()
        styleSettingsTable(linkTableView)
        linkTableView.delegate = self
        linkTableView.dataSource = self
        linkTableView.tag = 2

        linkDomainField.placeholderString = "e.g., *.github.com"
        styleSettingsEditableTextField(linkDomainField)
        linkDomainField.widthAnchor.constraint(equalToConstant: 180).isActive = true

        linkSpacePopup = NSPopUpButton()
        linkSpacePopup.addItem(withTitle: "(None)")

        linkProfilePopup = NSPopUpButton()
        linkProfilePopup.addItem(withTitle: "(None)")

        linkNewTabCheckbox = NSButton(checkboxWithTitle: "New Tab", target: nil, action: nil)
        linkNewTabCheckbox.state = .on

        let linkInputStack = NSStackView(views: [
            linkDomainField, linkSpacePopup, linkProfilePopup, linkNewTabCheckbox
        ])
        linkInputStack.orientation = .horizontal
        linkInputStack.spacing = 8

        let addLinkButton = NSButton(title: "Add", target: self, action: #selector(addLinkRule(_:)))
        let removeLinkButton = NSButton(title: "Remove", target: self, action: #selector(removeLinkRule(_:)))

        let linkRulesSectionView = SettingsManagedTableSectionView(
            tableView: linkTableView,
            containerStyle: .mixedCard(
                headerViews: [linkLabel, linkInputStack],
                footerViews: [makeSettingsActionRow([addLinkButton, removeLinkButton], spacing: 8)]
            ),
            shellStyle: .fixedHeight(160)
        )
        self.linkRulesSectionView = linkRulesSectionView

        view = SettingsPaneHostView(sections: [
            .managedTable(coreRulesSectionView),
            .managedTable(linkRulesSectionView)
        ])
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        loadData()
        loadSpacesAndProfiles()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .fullState, .spaceCreated, .spaceDeleted, .spaceUpdated, .spaceOrderChanged, .spaceConfigUpdated, .activeSpaceChanged(_, _), .activeProfileChanged:
            loadData()
            loadSpacesAndProfiles()
        default:
            break
        }
    }

    private func loadData() {
        if let json = MahoBridge.shared.getATCRules() {
            if let decoded: [ATCRule] = MahoBridge.shared.decode(json) {
                rules = decoded
            }
        }
        coreTableView.reloadData()
        linkTableView.reloadData()
    }

    private func loadSpacesAndProfiles() {
        spaces = MahoBridge.shared.getCanonicalSpaceViewModels()
        linkSpacePopup.removeAllItems()
        linkSpacePopup.addItem(withTitle: "(None)")
        for space in spaces {
            linkSpacePopup.addItem(withTitle: space.name)
        }

        linkProfilePopup.removeAllItems()
        linkProfilePopup.addItem(withTitle: "(None)")
        if let profilesJson = MahoBridge.shared.listProfiles(),
           let profiles = try? JSONDecoder().decode([[String: String]].self, from: Data(profilesJson.utf8)) {
            for profile in profiles {
                if let name = profile["name"] {
                    linkProfilePopup.addItem(withTitle: name)
                    linkProfilePopup.lastItem?.representedObject = profile["id"]
                }
            }
        }
    }

    @objc private func actionPopupChanged(_ sender: NSPopUpButton) {
        actionValueField.isHidden = sender.indexOfSelectedItem != 2
    }

    @objc private func addRule(_ sender: NSButton) {
        let conditionValue = conditionValueField.stringValue
        guard !conditionValue.isEmpty else { return }

        let condition: ATCCondition
        switch conditionPopup.indexOfSelectedItem {
        case 0: condition = .urlContains(text: conditionValue)
        case 1: condition = .urlEquals(url: conditionValue)
        case 2: condition = .urlPattern(pattern: conditionValue)
        case 3: condition = .inactiveDuration(hours: Double(conditionValue) ?? 1.0)
        case 4: condition = .tabCountExceeded(maxTabs: UInt32(conditionValue) ?? 10)
        default: return
        }

        let action: ATCAction
        switch actionPopup.indexOfSelectedItem {
        case 0: action = .close
        case 1: action = .archive
        case 2: action = .route(spaceId: actionValueField.stringValue)
        default: return
        }

        let rule = ATCRule(id: UUID().uuidString, condition: condition, action: action, enabled: true)
        if let ruleJson = MahoBridge.shared.encode(rule) {
            let _ = MahoBridge.shared.addATCRule(ruleJson: ruleJson)
        }
        rules.append(rule)
        coreTableView.reloadData()
        conditionValueField.stringValue = ""
        actionValueField.stringValue = ""
    }

    @objc private func removeRule(_ sender: NSButton) {
        let row = coreTableView.selectedRow
        guard row >= 0, row < rules.count else { return }
        let _ = MahoBridge.shared.removeATCRule(ruleId: rules[row].id)
        rules.remove(at: row)
        coreTableView.reloadData()
    }

    @objc private func ruleToggleChanged(_ sender: NSSwitch) {
        let row = sender.tag
        guard row >= 0, row < rules.count else { return }
        MahoBridge.shared.toggleATCRule(ruleId: rules[row].id, enabled: sender.state == .on)
    }

    @objc private func addLinkRule(_ sender: NSButton) {
        let domain = linkDomainField.stringValue.trimmingCharacters(in: .whitespaces)
        guard !domain.isEmpty else { return }

        let spaceIdx = linkSpacePopup.indexOfSelectedItem
        let targetSpaceId: String? = spaceIdx > 0 && spaceIdx - 1 < spaces.count ? spaces[spaceIdx - 1].id : nil

        let profileIdx = linkProfilePopup.indexOfSelectedItem
        let targetProfileId: String? = profileIdx > 0 ? linkProfilePopup.selectedItem?.representedObject as? String : nil

        let rule = ATCLinkRule(
            domainPattern: domain,
            targetSpaceId: targetSpaceId,
            targetProfileId: targetProfileId,
            openInNewTab: linkNewTabCheckbox.state == .on,
            enabled: true
        )
        ATCRuleEngine.shared.addRule(rule)
        linkTableView.reloadData()
        linkDomainField.stringValue = ""
    }

    @objc private func removeLinkRule(_ sender: NSButton) {
        let row = linkTableView.selectedRow
        guard row >= 0, row < ATCRuleEngine.shared.rules.count else { return }
        ATCRuleEngine.shared.removeRule(at: row)
        linkTableView.reloadData()
    }

    @objc private func linkRuleToggleChanged(_ sender: NSSwitch) {
        let row = sender.tag
        guard row >= 0, row < ATCRuleEngine.shared.rules.count else { return }
        var rule = ATCRuleEngine.shared.rules[row]
        rule.enabled = sender.state == .on
        ATCRuleEngine.shared.updateRule(at: row, with: rule)
    }

    private func formatCondition(_ condition: ATCCondition) -> String {
        switch condition {
        case .urlContains(let text): return "URL contains \"\(text)\""
        case .urlEquals(let url): return "URL equals \"\(url)\""
        case .urlPattern(let pattern): return "URL pattern \"\(pattern)\""
        case .inactiveDuration(let hours): return "Inactive \(hours)h"
        case .tabCountExceeded(let maxTabs): return "Tabs > \(maxTabs)"
        }
    }

    private func formatAction(_ action: ATCAction) -> String {
        switch action {
        case .close: return "Close"
        case .archive: return "Archive"
        case .route(let spaceId): return "Route \u{2192} \(spaceId)"
        }
    }

    private func spaceNameForId(_ id: String?) -> String {
        guard let id = id else { return "-" }
        return spaces.first(where: { $0.id == id })?.name ?? id
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        if tableView.tag == 2 {
            return ATCRuleEngine.shared.rules.count
        }
        return rules.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        if tableView.tag == 2 {
            return linkRuleCellView(for: tableColumn, row: row)
        }
        return coreRuleCellView(for: tableColumn, row: row)
    }

    private func coreRuleCellView(for tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < rules.count else { return nil }
        let rule = rules[row]

        switch tableColumn?.identifier.rawValue {
        case "condition":
            return makeSettingsTableCell(text: formatCondition(rule.condition))
        case "action":
            return makeSettingsTableCell(text: formatAction(rule.action))
        case "enabled":
            let toggle = NSSwitch()
            toggle.state = rule.enabled ? .on : .off
            toggle.target = self
            toggle.action = #selector(ruleToggleChanged(_:))
            toggle.tag = row
            let cell = NSTableCellView()
            toggle.translatesAutoresizingMaskIntoConstraints = false
            cell.addSubview(toggle)
            NSLayoutConstraint.activate([
                toggle.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
                toggle.centerXAnchor.constraint(equalTo: cell.centerXAnchor),
            ])
            return cell
        default:
            return nil
        }
    }

    private func linkRuleCellView(for tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let linkRules = ATCRuleEngine.shared.rules
        guard row < linkRules.count else { return nil }
        let rule = linkRules[row]

        switch tableColumn?.identifier.rawValue {
        case "linkEnabled":
            let toggle = NSSwitch()
            toggle.state = rule.enabled ? .on : .off
            toggle.target = self
            toggle.action = #selector(linkRuleToggleChanged(_:))
            toggle.tag = row
            let cell = NSTableCellView()
            toggle.translatesAutoresizingMaskIntoConstraints = false
            cell.addSubview(toggle)
            NSLayoutConstraint.activate([
                toggle.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
                toggle.centerXAnchor.constraint(equalTo: cell.centerXAnchor),
            ])
            return cell
        case "linkDomain":
            return makeSettingsTableCell(text: rule.domainPattern)
        case "linkSpace":
            return makeSettingsTableCell(text: spaceNameForId(rule.targetSpaceId))
        case "linkProfile":
            return makeSettingsTableCell(text: rule.targetProfileId ?? "-")
        case "linkNewTab":
            return makeSettingsTableCell(text: rule.openInNewTab ? "Yes" : "No")
        default:
            return nil
        }
    }
}
