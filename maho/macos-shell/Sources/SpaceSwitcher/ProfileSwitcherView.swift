import AppKit

final class ProfileSwitcherView: NSView {
    private let profileButton = NSButton(frame: .zero)
    private let colorIndicator = NSView(frame: .zero)
    private var activeProfileId: ProfileId?
    private var popover: NSPopover?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
        updateCurrentProfile()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
        updateCurrentProfile()
    }

    private func setupUI() {
        colorIndicator.translatesAutoresizingMaskIntoConstraints = false
        colorIndicator.wantsLayer = true
        colorIndicator.layer?.cornerRadius = 5
        colorIndicator.layer?.backgroundColor = MahoAppColors.accentBlue.cgColor
        addSubview(colorIndicator)

        profileButton.translatesAutoresizingMaskIntoConstraints = false
        profileButton.bezelStyle = .inline
        profileButton.font = .systemFont(ofSize: 11)
        profileButton.contentTintColor = MahoAppColors.secondaryText
        profileButton.target = self
        profileButton.action = #selector(profileButtonClicked(_:))
        profileButton.isBordered = false
        addSubview(profileButton)

        NSLayoutConstraint.activate([
            colorIndicator.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            colorIndicator.centerYAnchor.constraint(equalTo: centerYAnchor),
            colorIndicator.widthAnchor.constraint(equalToConstant: 10),
            colorIndicator.heightAnchor.constraint(equalToConstant: 10),

            profileButton.leadingAnchor.constraint(equalTo: colorIndicator.trailingAnchor, constant: 6),
            profileButton.centerYAnchor.constraint(equalTo: centerYAnchor),
            profileButton.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -8),
        ])
    }

    func updateCurrentProfile() {
        activeProfileId = MahoBridge.shared.getActiveProfileId()

        guard let jsonString = MahoBridge.shared.listProfiles(),
              let data = jsonString.data(using: .utf8),
              let profiles = try? JSONDecoder().decode([ProfileConfig].self, from: data) else {
            profileButton.title = "Default Profile"
            return
        }

        if let active = profiles.first(where: { $0.id == activeProfileId }) {
            profileButton.title = active.name
            colorIndicator.layer?.backgroundColor = NSColor.fromProfileAvatarString(active.avatarColor).cgColor
        } else if let first = profiles.first {
            profileButton.title = first.name
            colorIndicator.layer?.backgroundColor = NSColor.fromProfileAvatarString(first.avatarColor).cgColor
        } else {
            profileButton.title = "Default Profile"
        }
    }

    @objc private func profileButtonClicked(_ sender: NSButton) {
        let pop = NSPopover()
        pop.behavior = .transient
        pop.contentSize = NSSize(width: 220, height: 200)

        let vc = ProfileListViewController()
        vc.activeProfileId = activeProfileId
        vc.onDismiss = { [weak self] in
            self?.popover?.performClose(nil)
            self?.popover = nil
            self?.updateCurrentProfile()
        }
        pop.contentViewController = vc
        pop.show(relativeTo: sender.bounds, of: sender, preferredEdge: .maxY)
        popover = pop
    }

}

private final class ProfileListViewController: NSViewController {
    var activeProfileId: ProfileId?
    var onDismiss: (() -> Void)?
    private let tableView = NSTableView()
    private var profiles: [ProfileConfig] = []
    private let manageButton = NSButton(title: "Manage Profiles", target: nil, action: nil)
    private let newProfileButton = NSButton(title: "New Profile", target: nil, action: nil)

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 220, height: 200))
        container.wantsLayer = true
        container.layer?.backgroundColor = MahoAppColors.cardBackground.cgColor

        let scrollView = NSScrollView()
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.documentView = tableView
        scrollView.hasVerticalScroller = true
        scrollView.drawsBackground = false
        container.addSubview(scrollView)

        tableView.delegate = self
        tableView.dataSource = self
        tableView.headerView = nil
        tableView.backgroundColor = .clear
        let col = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("ProfileCol"))
        col.title = ""
        tableView.addTableColumn(col)

        manageButton.translatesAutoresizingMaskIntoConstraints = false
        manageButton.bezelStyle = .inline
        manageButton.font = .systemFont(ofSize: 11)
        manageButton.contentTintColor = MahoAppColors.accentBlue
        manageButton.target = self
        manageButton.action = #selector(manageProfilesClicked(_:))
        container.addSubview(manageButton)

        newProfileButton.translatesAutoresizingMaskIntoConstraints = false
        newProfileButton.bezelStyle = .inline
        newProfileButton.font = .systemFont(ofSize: 11)
        newProfileButton.contentTintColor = MahoAppColors.accentBlue
        newProfileButton.target = self
        newProfileButton.action = #selector(newProfileClicked(_:))
        container.addSubview(newProfileButton)

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: container.topAnchor, constant: 4),
            scrollView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: manageButton.topAnchor, constant: -4),

            manageButton.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 8),
            manageButton.bottomAnchor.constraint(equalTo: container.bottomAnchor, constant: -8),

            newProfileButton.leadingAnchor.constraint(equalTo: manageButton.trailingAnchor, constant: 10),
            newProfileButton.centerYAnchor.constraint(equalTo: manageButton.centerYAnchor),
        ])

        self.view = container
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        loadProfiles()
    }

    private func loadProfiles() {
        guard let jsonString = MahoBridge.shared.listProfiles(),
              let data = jsonString.data(using: .utf8),
              let decoded = try? JSONDecoder().decode([ProfileConfig].self, from: data) else {
            return
        }
        profiles = decoded
        tableView.reloadData()
    }

    @objc private func manageProfilesClicked(_ sender: NSButton) {
        onDismiss?()
        // Open settings window to Profiles tab
        let settingsWC = SettingsWindowController()
        settingsWC.showWindow(nil)
        if let tabVC = settingsWC.window?.contentViewController as? NSTabViewController {
            // Profiles tab is at index 4
            tabVC.selectedTabViewItemIndex = 4
        }
    }

    @objc private func newProfileClicked(_ sender: NSButton) {
        onDismiss?()
        guard let window = NSApp.mainWindow else { return }
        let sheet = ProfileConfigSheetController(mode: .create)
        sheet.onSave = {
            // Profile list refreshes via CoreUpdate observation.
        }
        window.contentViewController?.presentAsSheet(sheet)
    }

    @objc private func switchProfileClicked(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0, row < profiles.count else { return }
        let profile = profiles[row]
        CoreUpdateDispatcher.shared.sendAndDispatch(.switchProfile(profileId: profile.id))
        onDismiss?()
    }
}

extension ProfileListViewController: NSTableViewDelegate, NSTableViewDataSource {
    func numberOfRows(in tableView: NSTableView) -> Int {
        return profiles.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < profiles.count else { return nil }
        let profile = profiles[row]

        let cell = NSView()

        let dot = NSView()
        dot.translatesAutoresizingMaskIntoConstraints = false
        dot.wantsLayer = true
        dot.layer?.cornerRadius = 4
        let color = NSColor.fromProfileAvatarString(profile.avatarColor)
        dot.layer?.backgroundColor = color.cgColor
        cell.addSubview(dot)

        let nameLabel = NSTextField(labelWithString: profile.name)
        nameLabel.translatesAutoresizingMaskIntoConstraints = false
        nameLabel.font = .systemFont(ofSize: 12)
        nameLabel.textColor = profile.id == activeProfileId ? MahoAppColors.accentBlue : MahoAppColors.primaryText
        cell.addSubview(nameLabel)

        let switchBtn = NSButton(title: profile.id == activeProfileId ? "Active" : "Switch", target: self, action: #selector(switchProfileClicked(_:)))
        switchBtn.translatesAutoresizingMaskIntoConstraints = false
        switchBtn.bezelStyle = .inline
        switchBtn.font = .systemFont(ofSize: 10)
        switchBtn.tag = row
        switchBtn.isEnabled = profile.id != activeProfileId
        cell.addSubview(switchBtn)

        NSLayoutConstraint.activate([
            dot.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 8),
            dot.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            dot.widthAnchor.constraint(equalToConstant: 8),
            dot.heightAnchor.constraint(equalToConstant: 8),

            nameLabel.leadingAnchor.constraint(equalTo: dot.trailingAnchor, constant: 6),
            nameLabel.centerYAnchor.constraint(equalTo: cell.centerYAnchor),

            switchBtn.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -8),
            switchBtn.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
        ])

        return cell
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat {
        return 28
    }
}
