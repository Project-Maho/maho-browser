import AppKit

final class GeneralSettingsViewController: NSViewController, CoreUpdateObserver {

    private var searchEnginePopup = NSPopUpButton()
    private var archiveTimeoutPopup = NSPopUpButton()
    private var todayTabTimeoutPopup = NSPopUpButton()
    private var restorePolicyPopup = NSPopUpButton()
    private var downloadPathControl = NSPathControl()
    private var autoplayPopup = NSPopUpButton()
    private var notesAppPopup = NSPopUpButton()

    private var searchEngines: [SearchEngineViewModel] = []
    private let archiveTimeouts: [(label: String, value: Double)] = [
        ("Off", -1),
        ("12 hours", 12),
        ("24 hours", 24),
        ("7 days", 168),
        ("30 days", 720),
        ("60 days", 1440),
        ("90 days", 2160)
    ]
    private let todayTabTimeouts: [(label: String, value: Double)] = [
        ("1 hour", 1),
        ("4 hours", 4),
        ("8 hours", 8),
        ("24 hours", 24),
        ("Never", -1)
    ]
    private let restorePolicies: [(label: String, value: RestorePolicy)] = [
        ("Restore last session", .restoreAll),
        ("Start fresh", .startFresh)
    ]
    private let autoplayPolicies: [(label: String, value: AutoplayPolicy)] = [
        ("Allow all", .allow),
        ("Block all", .blockAll),
        ("Block audio", .blockAudio)
    ]
    private let notesApps: [(label: String, value: String)] = [
        ("Maho Notes", "maho"),
        ("Apple Notes", "apple_notes"),
        ("Notion", "notion"),
        ("Google Docs", "google_docs")
    ]

    override func loadView() {
        let searchEngineRow = makeSettingsLabeledRow("Search Engine:", searchEnginePopup)
        searchEnginePopup.setAccessibilityIdentifier("settings.searchEnginePopup")
        searchEnginePopup.target = self
        searchEnginePopup.action = #selector(searchEngineChanged(_:))

        let archiveTimeoutRow = makeSettingsLabeledRow("Archive Timeout:", archiveTimeoutPopup)
        archiveTimeoutPopup.target = self
        archiveTimeoutPopup.action = #selector(archiveTimeoutChanged(_:))

        let archiveDescription = NSTextField(labelWithString: "Tabs inactive for this duration will be automatically archived")
        archiveDescription.font = .systemFont(ofSize: 11)
        archiveDescription.textColor = MahoAppColors.secondaryText
        archiveDescription.maximumNumberOfLines = 2
        archiveDescription.lineBreakMode = .byWordWrapping
        archiveDescription.translatesAutoresizingMaskIntoConstraints = false

        let todayTabTimeoutRow = makeSettingsLabeledRow("Today Tab Timeout:", todayTabTimeoutPopup)
        todayTabTimeoutPopup.target = self
        todayTabTimeoutPopup.action = #selector(todayTabTimeoutChanged(_:))

        let restorePolicyRow = makeSettingsLabeledRow("Restore Policy:", restorePolicyPopup)
        restorePolicyPopup.target = self
        restorePolicyPopup.action = #selector(restorePolicyChanged(_:))

        let chooseButton = NSButton(title: "Choose...", target: self, action: #selector(chooseDownloadPath(_:)))
        let downloadStack = NSStackView(views: [downloadPathControl, chooseButton])
        downloadStack.orientation = .horizontal
        downloadStack.spacing = 8
        let downloadRow = makeSettingsLabeledRow("Download Path:", downloadStack)
        downloadPathControl.pathStyle = .popUp

        let autoplayRow = makeSettingsLabeledRow("Autoplay:", autoplayPopup)
        autoplayPopup.target = self
        autoplayPopup.action = #selector(autoplayChanged(_:))

        let notesAppRow = makeSettingsLabeledRow("Notes App:", notesAppPopup)
        notesAppPopup.target = self
        notesAppPopup.action = #selector(notesAppChanged(_:))

        view = SettingsPaneHostView(sections: [
            .card([
                searchEngineRow,
                archiveTimeoutRow,
                archiveDescription,
                todayTabTimeoutRow,
            ]),
            .card([
                restorePolicyRow,
                downloadRow,
                autoplayRow,
            ]),
            .card([
                notesAppRow,
            ]),
        ])
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        loadData()
        hydrateFromSettings()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        if case .settingsChanged(let settings) = update {
            hydrateFromSettings(settings)
        }
    }

    private func hydrateFromSettings(_ settings: Settings? = nil) {
        let s = settings ?? MahoBridge.shared.getSettings()
        guard let s = s else { return }

        if let idx = archiveTimeouts.firstIndex(where: { $0.value == s.general.archiveTimeoutHours }) {
            archiveTimeoutPopup.selectItem(at: idx)
        }
        if let idx = todayTabTimeouts.firstIndex(where: { $0.value == s.general.todayTabTimeoutHours }) {
            todayTabTimeoutPopup.selectItem(at: idx)
        }
        if let idx = restorePolicies.firstIndex(where: { $0.value == s.general.restoreOnLaunch }) {
            restorePolicyPopup.selectItem(at: idx)
        }
        if let idx = autoplayPolicies.firstIndex(where: { $0.value == s.general.autoplayPolicy }) {
            autoplayPopup.selectItem(at: idx)
        }
        downloadPathControl.url = URL(fileURLWithPath: s.general.downloadPath)
        let notesApp = UserDefaults.standard.string(forKey: "maho_default_notes_app") ?? "maho"
        if let idx = notesApps.firstIndex(where: { $0.value == notesApp }) {
            notesAppPopup.selectItem(at: idx)
        } else {
            notesAppPopup.selectItem(at: 0)
        }
    }

    private func loadData() {
        if let json = MahoBridge.shared.getSearchEngines(),
           let engines: [SearchEngineViewModel] = MahoBridge.shared.decode(json) {
            searchEngines = engines
            searchEnginePopup.removeAllItems()
            for engine in engines {
                searchEnginePopup.addItem(withTitle: engine.name)
                if engine.isDefault {
                    searchEnginePopup.selectItem(withTitle: engine.name)
                }
            }
        }

        archiveTimeoutPopup.removeAllItems()
        for (label, _) in archiveTimeouts {
            archiveTimeoutPopup.addItem(withTitle: label)
        }

        todayTabTimeoutPopup.removeAllItems()
        for (label, _) in todayTabTimeouts {
            todayTabTimeoutPopup.addItem(withTitle: label)
        }

        restorePolicyPopup.removeAllItems()
        for (label, _) in restorePolicies {
            restorePolicyPopup.addItem(withTitle: label)
        }

        autoplayPopup.removeAllItems()
        for (label, _) in autoplayPolicies {
            autoplayPopup.addItem(withTitle: label)
        }

        notesAppPopup.removeAllItems()
        for (label, _) in notesApps {
            notesAppPopup.addItem(withTitle: label)
        }
    }

    @objc private func searchEngineChanged(_ sender: NSPopUpButton) {
        let index = sender.indexOfSelectedItem
        guard index >= 0, index < searchEngines.count else { return }
        let engine = searchEngines[index]
        CoreUpdateDispatcher.shared.sendAndDispatch(.setDefaultSearchEngine(id: engine.id))
        AppDelegate.writeUITestState([
            "defaultSearchEngineId": engine.id,
            "defaultSearchEngineName": engine.name
        ])
    }

    @objc private func archiveTimeoutChanged(_ sender: NSPopUpButton) {
        let index = sender.indexOfSelectedItem
        guard index >= 0, index < archiveTimeouts.count else { return }
        let value = archiveTimeouts[index].value
        let update = SettingsUpdate(
            general: GeneralSettingsUpdate(
                defaultSearchEngine: nil,
                todayTabTimeoutHours: nil,
                restoreOnLaunch: nil,
                downloadPath: nil,
                autoplayPolicy: nil,
                archiveTimeoutHours: value,
                siteSearchEntries: nil
            ),
            appearance: nil,
            privacy: nil,
            reader: nil,
            keyboardShortcuts: nil,
            perSiteSettings: nil,
            toolbarItems: nil,
            max: nil,
            autofill: nil,
            advanced: nil,
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func restorePolicyChanged(_ sender: NSPopUpButton) {
        let index = sender.indexOfSelectedItem
        guard index >= 0, index < restorePolicies.count else { return }
        let value = restorePolicies[index].value
        let update = SettingsUpdate(
            general: GeneralSettingsUpdate(
                defaultSearchEngine: nil,
                todayTabTimeoutHours: nil,
                restoreOnLaunch: value,
                downloadPath: nil,
                autoplayPolicy: nil,
                archiveTimeoutHours: nil,
                siteSearchEntries: nil
            ),
            appearance: nil,
            privacy: nil,
            reader: nil,
            keyboardShortcuts: nil,
            perSiteSettings: nil,
            toolbarItems: nil,
            max: nil,
            autofill: nil,
            advanced: nil,
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func chooseDownloadPath(_ sender: NSButton) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        downloadPathControl.url = url
        let update = SettingsUpdate(
            general: GeneralSettingsUpdate(
                defaultSearchEngine: nil,
                todayTabTimeoutHours: nil,
                restoreOnLaunch: nil,
                downloadPath: url.path,
                autoplayPolicy: nil,
                archiveTimeoutHours: nil,
                siteSearchEntries: nil
            ),
            appearance: nil,
            privacy: nil,
            reader: nil,
            keyboardShortcuts: nil,
            perSiteSettings: nil,
            toolbarItems: nil,
            max: nil,
            autofill: nil,
            advanced: nil,
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func autoplayChanged(_ sender: NSPopUpButton) {
        let index = sender.indexOfSelectedItem
        guard index >= 0, index < autoplayPolicies.count else { return }
        let value = autoplayPolicies[index].value
        let update = SettingsUpdate(
            general: GeneralSettingsUpdate(
                defaultSearchEngine: nil,
                todayTabTimeoutHours: nil,
                restoreOnLaunch: nil,
                downloadPath: nil,
                autoplayPolicy: value,
                archiveTimeoutHours: nil,
                siteSearchEntries: nil
            ),
            appearance: nil,
            privacy: nil,
            reader: nil,
            keyboardShortcuts: nil,
            perSiteSettings: nil,
            toolbarItems: nil,
            max: nil,
            autofill: nil,
            advanced: nil,
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func todayTabTimeoutChanged(_ sender: NSPopUpButton) {
        let index = sender.indexOfSelectedItem
        guard index >= 0, index < todayTabTimeouts.count else { return }
        let value = todayTabTimeouts[index].value
        let update = SettingsUpdate(
            general: GeneralSettingsUpdate(
                defaultSearchEngine: nil,
                todayTabTimeoutHours: value,
                restoreOnLaunch: nil,
                downloadPath: nil,
                autoplayPolicy: nil,
                archiveTimeoutHours: nil,
                siteSearchEntries: nil
            ),
            appearance: nil,
            privacy: nil,
            reader: nil,
            keyboardShortcuts: nil,
            perSiteSettings: nil,
            toolbarItems: nil,
            max: nil,
            autofill: nil,
            advanced: nil,
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func notesAppChanged(_ sender: NSPopUpButton) {
        let index = sender.indexOfSelectedItem
        guard index >= 0, index < notesApps.count else { return }
        UserDefaults.standard.set(notesApps[index].value, forKey: "maho_default_notes_app")
    }
}
