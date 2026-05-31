import AppKit

final class AutofillSettingsViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver, SettingsPaneRefreshable {

    private let addressesEnabledSwitch = NSSwitch()
    private let paymentsEnabledSwitch = NSSwitch()
    private let addressesTableView = NSTableView()
    private let paymentsTableView = NSTableView()
    private let addAddressButton = NSButton(title: "Add", target: nil, action: nil)
    private let deleteAddressButton = NSButton(title: "Delete", target: nil, action: nil)
    private let addPaymentButton = NSButton(title: "Add", target: nil, action: nil)
    private let deletePaymentButton = NSButton(title: "Delete", target: nil, action: nil)
    private let addressesStatusLabel = NSTextField(labelWithString: "")
    private let paymentsStatusLabel = NSTextField(labelWithString: "")
    private let addressesHeaderCountLabel = NSTextField(labelWithString: "0")
    private let paymentsHeaderCountLabel = NSTextField(labelWithString: "0")
    private let addressesEmptyStateView = AutofillEmptyStateView(
        iconName: "mappin.and.ellipse",
        title: "No saved addresses",
        subtitle: "Addresses you save for forms and checkout will appear here."
    )
    private let paymentsEmptyStateView = AutofillEmptyStateView(
        iconName: "creditcard",
        title: "No saved payment cards",
        subtitle: "Saved cards stay visible here so they feel ready for checkout."
    )

    private var addressesBodyHeightConstraint: NSLayoutConstraint?
    private var paymentsBodyHeightConstraint: NSLayoutConstraint?
    private let addressesBodyContainer = NSView()
    private let paymentsBodyContainer = NSView()
    private var addressesTableShell: NSView?
    private var paymentsTableShell: NSView?
    private var addresses: [AutofillAddress] = []
    private var payments: [AutofillPayment] = []
    private var dataLoadFailed = false

    private static let compactTableHeight: CGFloat = 146
    private static let compactEmptyStateHeight: CGFloat = 88

    override func loadView() {
        configureToggle(addressesEnabledSwitch, action: #selector(addressesEnabledChanged(_:)))
        configureToggle(paymentsEnabledSwitch, action: #selector(paymentsEnabledChanged(_:)))
        configureStatusLabel(addressesStatusLabel)
        configureStatusLabel(paymentsStatusLabel)
        configureCountLabel(addressesHeaderCountLabel)
        configureCountLabel(paymentsHeaderCountLabel)

        configureTable(
            addressesTableView,
            columnIdentifier: "name",
            title: "Saved Address"
        )
        configureTable(
            paymentsTableView,
            columnIdentifier: "card",
            title: "Saved Card"
        )

        configureButton(addAddressButton, action: #selector(addAddress(_:)))
        configureButton(deleteAddressButton, action: #selector(deleteAddress(_:)))
        configureButton(addPaymentButton, action: #selector(addPayment(_:)))
        configureButton(deletePaymentButton, action: #selector(deletePayment(_:)))

        let addressesCard = makeAutofillSectionCard(
            title: "Addresses",
            subtitle: "Saved names and delivery details you can fill into forms across the web.",
            toggleTitle: "Use saved addresses for autofill",
            countLabel: addressesHeaderCountLabel,
            toggleSwitch: addressesEnabledSwitch,
            tableView: addressesTableView,
            emptyStateView: addressesEmptyStateView,
            bodyContainer: addressesBodyContainer,
            bodyHeightConstraint: &addressesBodyHeightConstraint,
            tableShell: &addressesTableShell,
            statusLabel: addressesStatusLabel,
            primaryButton: addAddressButton,
            secondaryButton: deleteAddressButton
        )

        let paymentsCard = makeAutofillSectionCard(
            title: "Payments",
            subtitle: "Cards kept on hand for faster checkout when payment autofill is enabled.",
            toggleTitle: "Use saved payment methods for autofill",
            countLabel: paymentsHeaderCountLabel,
            toggleSwitch: paymentsEnabledSwitch,
            tableView: paymentsTableView,
            emptyStateView: paymentsEmptyStateView,
            bodyContainer: paymentsBodyContainer,
            bodyHeightConstraint: &paymentsBodyHeightConstraint,
            tableShell: &paymentsTableShell,
            statusLabel: paymentsStatusLabel,
            primaryButton: addPaymentButton,
            secondaryButton: deletePaymentButton
        )

        addressesCard.setContentHuggingPriority(NSLayoutConstraint.Priority.defaultLow, for: NSLayoutConstraint.Orientation.horizontal)
        addressesCard.setContentCompressionResistancePriority(NSLayoutConstraint.Priority.defaultLow, for: NSLayoutConstraint.Orientation.horizontal)
        paymentsCard.setContentHuggingPriority(NSLayoutConstraint.Priority.defaultLow, for: NSLayoutConstraint.Orientation.horizontal)
        paymentsCard.setContentCompressionResistancePriority(NSLayoutConstraint.Priority.defaultLow, for: NSLayoutConstraint.Orientation.horizontal)

        view = SettingsPaneHostView(sections: [
            .plain([addressesCard], spacing: 18),
            .plain([paymentsCard], spacing: 18)
        ])
        updateSectionState()
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        loadData()
    }

    func refreshSettingsContent() {
        loadData()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        if case .settingsChanged(let settings) = update {
            addressesEnabledSwitch.state = settings.autofill.addressesEnabled ? .on : .off
            paymentsEnabledSwitch.state = settings.autofill.paymentsEnabled ? .on : .off
            updateSectionState()
        }
    }

    private func loadData() {
        if let settings = MahoBridge.shared.getSettings() {
            addressesEnabledSwitch.state = settings.autofill.addressesEnabled ? .on : .off
            paymentsEnabledSwitch.state = settings.autofill.paymentsEnabled ? .on : .off
            dataLoadFailed = false
        } else {
            dataLoadFailed = true
        }

        addresses = MahoBridge.shared.getAutofillAddresses()
        payments = MahoBridge.shared.getAutofillPayments()
        addressesTableView.reloadData()
        paymentsTableView.reloadData()
        updateSectionState()
    }

    private func sendAutofillUpdate(addressesEnabled: Bool? = nil, paymentsEnabled: Bool? = nil, passwordsEnabled: Bool? = nil, passwordProvider: PasswordAutofillProvider? = nil) {
        let update = SettingsUpdate(
            general: nil,
            appearance: nil,
            privacy: nil,
            reader: nil,
            keyboardShortcuts: nil,
            perSiteSettings: nil,
            toolbarItems: nil,
            max: nil,
            autofill: AutofillSettingsUpdate(
                addressesEnabled: addressesEnabled,
                paymentsEnabled: paymentsEnabled,
                passwordsEnabled: passwordsEnabled,
                passwordProvider: passwordProvider
            ),
            advanced: nil,
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func addressesEnabledChanged(_ sender: NSSwitch) {
        sendAutofillUpdate(addressesEnabled: sender.state == .on)
        updateSectionState()
    }

    @objc private func paymentsEnabledChanged(_ sender: NSSwitch) {
        sendAutofillUpdate(paymentsEnabled: sender.state == .on)
        updateSectionState()
    }

    @objc private func addAddress(_ sender: NSButton) {
        let address = AutofillAddress(
            id: UUID().uuidString,
            name: "New Address",
            street: "",
            city: "",
            state: "",
            zip: "",
            country: "",
            phone: nil,
            email: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.addAutofillAddress(address: address))
        addresses = MahoBridge.shared.getAutofillAddresses()
        addressesTableView.reloadData()
        updateSectionState()
    }

    @objc private func deleteAddress(_ sender: NSButton) {
        let row = addressesTableView.selectedRow
        guard row >= 0, row < addresses.count else { return }
        let id = addresses[row].id
        CoreUpdateDispatcher.shared.sendAndDispatch(.deleteAutofillAddress(id: id))
        addresses = MahoBridge.shared.getAutofillAddresses()
        addressesTableView.reloadData()
        updateSectionState()
    }

    @objc private func addPayment(_ sender: NSButton) {
        let payment = AutofillPayment(
            id: UUID().uuidString,
            cardName: "New Card",
            lastFour: "0000",
            expiry: "12/30"
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.addAutofillPayment(payment: payment))
        payments = MahoBridge.shared.getAutofillPayments()
        paymentsTableView.reloadData()
        updateSectionState()
    }

    @objc private func deletePayment(_ sender: NSButton) {
        let row = paymentsTableView.selectedRow
        guard row >= 0, row < payments.count else { return }
        let id = payments[row].id
        CoreUpdateDispatcher.shared.sendAndDispatch(.deleteAutofillPayment(id: id))
        payments = MahoBridge.shared.getAutofillPayments()
        paymentsTableView.reloadData()
        updateSectionState()
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        if tableView == addressesTableView {
            return addresses.count
        }
        if tableView == paymentsTableView {
            return payments.count
        }
        return 0
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        if tableView == addressesTableView, row < addresses.count {
            let address = addresses[row]
            return AutofillSummaryCellView(
                title: address.name,
                subtitle: addressSecondaryText(for: address)
            )
        }

        if tableView == paymentsTableView, row < payments.count {
            let payment = payments[row]
            return AutofillSummaryCellView(
                title: payment.cardName,
                subtitle: paymentSecondaryText(for: payment)
            )
        }

        return nil
    }

    func tableView(_ tableView: NSTableView, rowViewForRow row: Int) -> NSTableRowView? {
        AutofillTableRowView()
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        updateSectionState()
    }

    private func configureToggle(_ toggle: NSSwitch, action: Selector) {
        toggle.target = self
        toggle.action = action
        toggle.setAccessibilityElement(false)
    }

    private func configureButton(_ button: NSButton, action: Selector) {
        button.target = self
        button.action = action
        button.controlSize = .small
        button.bezelStyle = .rounded
        button.setButtonType(.momentaryPushIn)
        button.contentTintColor = MahoAppColors.primaryText.withAlphaComponent(0.94)
    }

    private func configureStatusLabel(_ label: NSTextField) {
        label.font = .systemFont(ofSize: 11.5, weight: .medium)
        styleSettingsLabel(label, secondary: true)
        label.maximumNumberOfLines = 1
        label.lineBreakMode = .byTruncatingTail
        label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    }

    private func configureCountLabel(_ label: NSTextField) {
        label.font = .systemFont(ofSize: 10.5, weight: .semibold)
        label.alignment = .center
        label.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.92)
        label.wantsLayer = true
        label.layer?.cornerRadius = 8
        label.layer?.backgroundColor = MahoAppColors.canvasBadgeFill.withAlphaComponent(0.92).cgColor
        label.layer?.borderWidth = 1
        label.layer?.borderColor = MahoAppColors.canvasBadgeBorder.withAlphaComponent(0.72).cgColor
    }

    private func configureTable(
        _ tableView: NSTableView,
        columnIdentifier: String,
        title: String
    ) {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(columnIdentifier))
        column.title = title
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        styleSettingsTable(tableView)
        tableView.delegate = self
        tableView.dataSource = self
        tableView.selectionHighlightStyle = .none
        tableView.intercellSpacing = NSSize(width: 0, height: 4)
        tableView.rowHeight = 46
        tableView.columnAutoresizingStyle = .firstColumnOnlyAutoresizingStyle
        tableView.usesAutomaticRowHeights = false
        tableView.allowsEmptySelection = true
        tableView.floatsGroupRows = false
    }

    private func makeAutofillSectionCard(
        title: String,
        subtitle: String,
        toggleTitle: String,
        countLabel: NSTextField,
        toggleSwitch: NSSwitch,
        tableView: NSTableView,
        emptyStateView: NSView,
        bodyContainer: NSView,
        bodyHeightConstraint: inout NSLayoutConstraint?,
        tableShell: inout NSView?,
        statusLabel: NSTextField,
        primaryButton: NSButton,
        secondaryButton: NSButton
    ) -> NSView {
        let titleLabel = NSTextField(labelWithString: title)
        titleLabel.font = .systemFont(ofSize: 13.5, weight: .semibold)
        styleSettingsLabel(titleLabel)

        let subtitleLabel = NSTextField(wrappingLabelWithString: subtitle)
        subtitleLabel.font = .systemFont(ofSize: 12, weight: .regular)
        styleSettingsLabel(subtitleLabel, secondary: true)

        let headerTextStack = NSStackView(views: [titleLabel, subtitleLabel])
        headerTextStack.translatesAutoresizingMaskIntoConstraints = false
        headerTextStack.orientation = .vertical
        headerTextStack.alignment = .leading
        headerTextStack.spacing = 3
        headerTextStack.setContentHuggingPriority(.defaultLow, for: .horizontal)
        headerTextStack.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let countBadgeContainer = NSView()
        countBadgeContainer.translatesAutoresizingMaskIntoConstraints = false
        countBadgeContainer.setContentHuggingPriority(.required, for: .horizontal)
        countBadgeContainer.setContentCompressionResistancePriority(.required, for: .horizontal)
        countBadgeContainer.addSubview(countLabel)
        countLabel.translatesAutoresizingMaskIntoConstraints = false

        NSLayoutConstraint.activate([
            countLabel.topAnchor.constraint(equalTo: countBadgeContainer.topAnchor),
            countLabel.leadingAnchor.constraint(equalTo: countBadgeContainer.leadingAnchor),
            countLabel.trailingAnchor.constraint(equalTo: countBadgeContainer.trailingAnchor),
            countLabel.bottomAnchor.constraint(equalTo: countBadgeContainer.bottomAnchor),
            countLabel.widthAnchor.constraint(greaterThanOrEqualToConstant: 20),
            countLabel.heightAnchor.constraint(equalToConstant: 16),
        ])

        let headerSpacer = NSView()
        headerSpacer.translatesAutoresizingMaskIntoConstraints = false
        headerSpacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        headerSpacer.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let headerRow = makeSettingsPlainRow([headerTextStack, headerSpacer, countBadgeContainer], spacing: 12)
        let toggleRow = makeSettingsLabeledRow(toggleTitle, toggleSwitch, embedded: true)

        bodyContainer.translatesAutoresizingMaskIntoConstraints = false
        bodyContainer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        bodyContainer.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        var unusedTableHeightConstraint: NSLayoutConstraint?
        let builtTableShell = makeSettingsCompactTableShell(
            tableView: tableView,
            emptyStateView: nil,
            storedHeightConstraint: &unusedTableHeightConstraint
        )
        builtTableShell.translatesAutoresizingMaskIntoConstraints = false
        builtTableShell.isHidden = true
        bodyContainer.addSubview(builtTableShell)
        NSLayoutConstraint.activate([
            builtTableShell.topAnchor.constraint(equalTo: bodyContainer.topAnchor),
            builtTableShell.leadingAnchor.constraint(equalTo: bodyContainer.leadingAnchor),
            builtTableShell.trailingAnchor.constraint(equalTo: bodyContainer.trailingAnchor),
            builtTableShell.bottomAnchor.constraint(equalTo: bodyContainer.bottomAnchor),
        ])
        tableShell = builtTableShell

        tableView.rowHeight = 46
        tableView.intercellSpacing = NSSize(width: 0, height: 4)

        emptyStateView.translatesAutoresizingMaskIntoConstraints = false
        emptyStateView.setContentHuggingPriority(.defaultLow, for: .horizontal)
        emptyStateView.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        bodyContainer.addSubview(emptyStateView)
        NSLayoutConstraint.activate([
            emptyStateView.topAnchor.constraint(equalTo: bodyContainer.topAnchor, constant: 18),
            emptyStateView.centerXAnchor.constraint(equalTo: bodyContainer.centerXAnchor),
            emptyStateView.widthAnchor.constraint(equalToConstant: 320),
            emptyStateView.bottomAnchor.constraint(lessThanOrEqualTo: bodyContainer.bottomAnchor, constant: -18),
        ])

        let bodyHeight = bodyContainer.heightAnchor.constraint(equalToConstant: Self.compactEmptyStateHeight)
        bodyHeight.priority = .defaultHigh
        bodyHeight.isActive = true
        bodyHeightConstraint = bodyHeight

        let footerSpacer = NSView()
        footerSpacer.translatesAutoresizingMaskIntoConstraints = false
        footerSpacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        footerSpacer.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let footerActions = makeSettingsActionRow([secondaryButton, primaryButton], spacing: 6)
        let footerRow = makeSettingsPlainRow([statusLabel, footerSpacer, footerActions], spacing: 12)

        return makeSettingsSectionCard([
            headerRow,
            toggleRow,
            bodyContainer,
            footerRow,
        ])
    }

    private func addressSecondaryText(for address: AutofillAddress) -> String {
        let locationParts = [address.city, address.state, address.country]
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }

        if !locationParts.isEmpty {
            return locationParts.joined(separator: " • ")
        }

        let contactParts = [address.email, address.phone]
            .compactMap { $0?.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }

        if !contactParts.isEmpty {
            return contactParts.joined(separator: " • ")
        }

        return "Saved autofill address"
    }

    private func paymentSecondaryText(for payment: AutofillPayment) -> String {
        let lastFour = payment.lastFour.trimmingCharacters(in: .whitespacesAndNewlines)
        let maskedNumber = lastFour.isEmpty ? "Card details unavailable" : "•••• \(lastFour)"
        let expiry = payment.expiry.trimmingCharacters(in: .whitespacesAndNewlines)
        return expiry.isEmpty ? maskedNumber : "\(maskedNumber) • Expires \(expiry)"
    }

    private func updateSectionState() {
        let hasAddresses = !addresses.isEmpty
        let hasPayments = !payments.isEmpty

        addressesEmptyStateView.isHidden = hasAddresses
        paymentsEmptyStateView.isHidden = hasPayments
        addressesTableView.isHidden = !hasAddresses
        paymentsTableView.isHidden = !hasPayments
        addressesTableShell?.isHidden = !hasAddresses
        paymentsTableShell?.isHidden = !hasPayments

        addressesBodyHeightConstraint?.constant = hasAddresses ? Self.compactTableHeight : Self.compactEmptyStateHeight
        paymentsBodyHeightConstraint?.constant = hasPayments ? Self.compactTableHeight : Self.compactEmptyStateHeight

        deleteAddressButton.isEnabled = addressesTableView.selectedRow >= 0 && addressesTableView.selectedRow < addresses.count
        deletePaymentButton.isEnabled = paymentsTableView.selectedRow >= 0 && paymentsTableView.selectedRow < payments.count
        deleteAddressButton.contentTintColor = deleteAddressButton.isEnabled
            ? MahoAppColors.secondaryText.withAlphaComponent(0.96)
            : MahoAppColors.tertiaryText.withAlphaComponent(0.7)
        deletePaymentButton.contentTintColor = deletePaymentButton.isEnabled
            ? MahoAppColors.secondaryText.withAlphaComponent(0.96)
            : MahoAppColors.tertiaryText.withAlphaComponent(0.7)
        addAddressButton.contentTintColor = MahoAppColors.primaryText.withAlphaComponent(0.94)
        addPaymentButton.contentTintColor = MahoAppColors.primaryText.withAlphaComponent(0.94)

        addressesHeaderCountLabel.stringValue = "\(addresses.count)"
        paymentsHeaderCountLabel.stringValue = "\(payments.count)"

        addressesStatusLabel.stringValue = statusText(
            count: addresses.count,
            singular: "address",
            plural: "addresses",
            enabled: addressesEnabledSwitch.state == .on
        )
        paymentsStatusLabel.stringValue = statusText(
            count: payments.count,
            singular: "card",
            plural: "cards",
            enabled: paymentsEnabledSwitch.state == .on
        )
    }

    private func statusText(count: Int, singular: String, plural: String, enabled: Bool) -> String {
        let itemLabel = count == 1 ? singular : plural
        let prefix = enabled ? "" : "Autofill off • "

        if dataLoadFailed, count == 0 {
            return "\(prefix)Waiting for saved \(plural)"
        }

        if count == 0 {
            return "\(prefix)No saved \(plural) yet"
        }

        return "\(prefix)\(count) saved \(itemLabel)"
    }
}

private final class AutofillSummaryCellView: NSTableCellView {
    private let titleLabel = NSTextField(labelWithString: "")
    private let subtitleLabel = NSTextField(labelWithString: "")
    private let separatorView = NSView()

    init(title: String, subtitle: String) {
        super.init(frame: .zero)
        setupViews()
        configure(title: title, subtitle: subtitle)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func setupViews() {
        translatesAutoresizingMaskIntoConstraints = false

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 12.5, weight: .semibold)
        titleLabel.lineBreakMode = .byTruncatingTail
        styleSettingsLabel(titleLabel)
        addSubview(titleLabel)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = .systemFont(ofSize: 11, weight: .medium)
        subtitleLabel.lineBreakMode = .byTruncatingTail
        styleSettingsLabel(subtitleLabel, secondary: true)
        addSubview(subtitleLabel)

        separatorView.translatesAutoresizingMaskIntoConstraints = false
        separatorView.wantsLayer = true
        separatorView.layer?.backgroundColor = MahoAppColors.contentBorder.withAlphaComponent(0.46).cgColor
        addSubview(separatorView)

        NSLayoutConstraint.activate([
            titleLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 14),
            titleLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -14),
            titleLabel.topAnchor.constraint(equalTo: topAnchor, constant: 8),
            subtitleLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            subtitleLabel.trailingAnchor.constraint(equalTo: titleLabel.trailingAnchor),
            subtitleLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 1),
            subtitleLabel.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -8),
            separatorView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 14),
            separatorView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -14),
            separatorView.bottomAnchor.constraint(equalTo: bottomAnchor),
            separatorView.heightAnchor.constraint(equalToConstant: 1),
        ])
    }

    private func configure(title: String, subtitle: String) {
        titleLabel.stringValue = title
        subtitleLabel.stringValue = subtitle
    }
}

private final class AutofillTableRowView: NSTableRowView {
    private let cardView = NSView()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override var isSelected: Bool {
        didSet {
            updateAppearance()
        }
    }

    override var isEmphasized: Bool {
        get { false }
        set { }
    }

    override func drawSelection(in dirtyRect: NSRect) {
    }

    private func setupViews() {
        wantsLayer = true
        cardView.translatesAutoresizingMaskIntoConstraints = false
        cardView.wantsLayer = true
        addSubview(cardView)

        NSLayoutConstraint.activate([
            cardView.topAnchor.constraint(equalTo: topAnchor),
            cardView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 6),
            cardView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -6),
            cardView.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])

        updateAppearance()
    }

    private func updateAppearance() {
        cardView.layer?.cornerRadius = 9
        cardView.layer?.backgroundColor = isSelected
            ? MahoAppColors.accentBlue.withAlphaComponent(0.2).cgColor
            : MahoAppColors.searchFieldBackground.withAlphaComponent(0.42).cgColor
        cardView.layer?.borderWidth = isSelected ? 1 : 0
        cardView.layer?.borderColor = isSelected
            ? MahoAppColors.accentBlueBright.withAlphaComponent(0.7).cgColor
            : NSColor.clear.cgColor
        cardView.layer?.shadowColor = MahoAppColors.canvasInnerGlow.cgColor
        cardView.layer?.shadowOpacity = isSelected ? 0.16 : 0.04
        cardView.layer?.shadowRadius = isSelected ? 10 : 4
        cardView.layer?.shadowOffset = CGSize(width: 0, height: -2)
    }
}

private final class AutofillEmptyStateView: NSView {
    private let badgeView = NSView()
    private let iconView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let subtitleLabel = NSTextField(wrappingLabelWithString: "")

    init(iconName: String, title: String, subtitle: String) {
        super.init(frame: .zero)
        setupViews(iconName: iconName)
        titleLabel.stringValue = title
        subtitleLabel.stringValue = subtitle
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func setupViews(iconName: String) {
        wantsLayer = true

        badgeView.translatesAutoresizingMaskIntoConstraints = false
        badgeView.wantsLayer = true
        badgeView.layer?.cornerRadius = 13
        badgeView.layer?.backgroundColor = MahoAppColors.canvasBadgeFill.cgColor
        badgeView.layer?.borderWidth = 1
        badgeView.layer?.borderColor = MahoAppColors.canvasBadgeBorder.cgColor
        addSubview(badgeView)

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.image = MahoIcon.imageForSFSymbol(iconName, pointSize: 16)
        iconView.contentTintColor = MahoAppColors.accentBlueBright.withAlphaComponent(0.96)
        addSubview(iconView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 12.5, weight: .semibold)
        titleLabel.alignment = .left
        styleSettingsLabel(titleLabel)
        addSubview(titleLabel)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = .systemFont(ofSize: 11.5, weight: .medium)
        subtitleLabel.alignment = .left
        styleSettingsLabel(subtitleLabel, secondary: true)
        subtitleLabel.maximumNumberOfLines = 0
        addSubview(subtitleLabel)

        NSLayoutConstraint.activate([
            badgeView.topAnchor.constraint(equalTo: topAnchor),
            badgeView.leadingAnchor.constraint(equalTo: leadingAnchor),
            badgeView.widthAnchor.constraint(equalToConstant: 38),
            badgeView.heightAnchor.constraint(equalToConstant: 38),

            iconView.centerXAnchor.constraint(equalTo: badgeView.centerXAnchor),
            iconView.centerYAnchor.constraint(equalTo: badgeView.centerYAnchor),
            iconView.widthAnchor.constraint(equalToConstant: 18),
            iconView.heightAnchor.constraint(equalToConstant: 18),

            titleLabel.topAnchor.constraint(equalTo: topAnchor),
            titleLabel.leadingAnchor.constraint(equalTo: badgeView.trailingAnchor, constant: 14),
            titleLabel.trailingAnchor.constraint(equalTo: trailingAnchor),

            subtitleLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 4),
            subtitleLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            subtitleLabel.trailingAnchor.constraint(equalTo: trailingAnchor),
            subtitleLabel.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
    }
}
