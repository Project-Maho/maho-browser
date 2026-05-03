import AppKit

final class PrivacySettingsViewController: NSViewController, CoreUpdateObserver {

    private var doNotTrackSwitch = NSSwitch()
    private var blockThirdPartyCookiesSwitch = NSSwitch()
    private var contentBlockerSwitch = NSSwitch()
    private var popupBlockerSwitch = NSSwitch()
    private var searchSuggestionsSwitch = NSSwitch()
    private var safeBrowsingSwitch = NSSwitch()
    private var clearDataOnExitSwitch = NSSwitch()
    private var secureDnsSwitch = NSSwitch()
    private var secureDnsCustomUrlField = NSTextField()
    private var secureDnsRow = NSStackView()

    override func loadView() {
        doNotTrackSwitch.target = self
        doNotTrackSwitch.action = #selector(doNotTrackChanged(_:))
        let doNotTrackRow = makeSettingsLabeledRow("Do Not Track:", doNotTrackSwitch)

        blockThirdPartyCookiesSwitch.target = self
        blockThirdPartyCookiesSwitch.action = #selector(blockThirdPartyCookiesChanged(_:))
        let blockCookiesRow = makeSettingsLabeledRow("Block 3rd Party Cookies:", blockThirdPartyCookiesSwitch)

        contentBlockerSwitch.target = self
        contentBlockerSwitch.action = #selector(contentBlockerChanged(_:))
        let contentBlockerRow = makeSettingsLabeledRow("Content Blocker:", contentBlockerSwitch)

        popupBlockerSwitch.target = self
        popupBlockerSwitch.action = #selector(popupBlockerChanged(_:))
        let popupBlockerRow = makeSettingsLabeledRow("Popup Blocker:", popupBlockerSwitch)

        searchSuggestionsSwitch.target = self
        searchSuggestionsSwitch.action = #selector(searchSuggestionsChanged(_:))
        let searchSuggestionsRow = makeSettingsLabeledRow("Search Suggestions:", searchSuggestionsSwitch)

        safeBrowsingSwitch.target = self
        safeBrowsingSwitch.action = #selector(safeBrowsingChanged(_:))
        let safeBrowsingRow = makeSettingsLabeledRow("Safe Browsing:", safeBrowsingSwitch)

        clearDataOnExitSwitch.target = self
        clearDataOnExitSwitch.action = #selector(clearDataOnExitChanged(_:))
        let clearDataRow = makeSettingsLabeledRow("Clear Data on Exit:", clearDataOnExitSwitch)

        secureDnsSwitch.target = self
        secureDnsSwitch.action = #selector(secureDnsChanged(_:))
        let secureDnsToggleRow = makeSettingsLabeledRow("Secure DNS:", secureDnsSwitch)

        secureDnsCustomUrlField.placeholderString = "Custom DNS URL"
        styleSettingsEditableTextField(secureDnsCustomUrlField)
        secureDnsCustomUrlField.target = self
        secureDnsCustomUrlField.action = #selector(secureDnsCustomUrlChanged(_:))
        secureDnsRow = makeSettingsLabeledRow("DNS URL:", secureDnsCustomUrlField, embedded: true)
        secureDnsRow.isHidden = true

        view = SettingsPaneHostView(sections: [
            .card([
                doNotTrackRow,
                blockCookiesRow,
                contentBlockerRow,
                popupBlockerRow,
                searchSuggestionsRow,
                safeBrowsingRow,
                clearDataRow,
            ]),
            .card([
                secureDnsToggleRow,
                secureDnsRow,
            ]),
        ])
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
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

        doNotTrackSwitch.state = s.privacy.doNotTrack ? .on : .off
        blockThirdPartyCookiesSwitch.state = s.privacy.blockThirdPartyCookies ? .on : .off
        contentBlockerSwitch.state = s.privacy.contentBlockerEnabled ? .on : .off
        popupBlockerSwitch.state = s.privacy.popupBlockerEnabled ? .on : .off
        searchSuggestionsSwitch.state = s.privacy.searchSuggestionsEnabled ? .on : .off
        safeBrowsingSwitch.state = s.privacy.safeBrowsingEnabled ? .on : .off
        clearDataOnExitSwitch.state = s.privacy.clearDataOnExit ? .on : .off
        secureDnsSwitch.state = s.privacy.secureDnsEnabled ? .on : .off
        secureDnsCustomUrlField.stringValue = s.privacy.secureDnsCustomUrl ?? ""
        secureDnsRow.isHidden = !s.privacy.secureDnsEnabled
    }

    private func sendPrivacyUpdate(
        doNotTrack: Bool? = nil,
        blockThirdPartyCookies: Bool? = nil,
        contentBlockerEnabled: Bool? = nil,
        popupBlockerEnabled: Bool? = nil,
        searchSuggestionsEnabled: Bool? = nil,
        secureDnsEnabled: Bool? = nil,
        secureDnsProvider: String? = nil,
        secureDnsCustomUrl: String?? = nil,
        clearDataOnExit: Bool? = nil,
        safeBrowsingEnabled: Bool? = nil
    ) {
        let update = SettingsUpdate(
            general: nil,
            appearance: nil,
            privacy: PrivacySettingsUpdate(
                doNotTrack: doNotTrack,
                blockThirdPartyCookies: blockThirdPartyCookies,
                contentBlockerEnabled: contentBlockerEnabled,
                popupBlockerEnabled: popupBlockerEnabled,
                searchSuggestionsEnabled: searchSuggestionsEnabled,
                secureDnsEnabled: secureDnsEnabled,
                secureDnsProvider: secureDnsProvider,
                secureDnsCustomUrl: secureDnsCustomUrl,
                clearDataOnExit: clearDataOnExit,
                safeBrowsingEnabled: safeBrowsingEnabled
            ),
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

    @objc private func doNotTrackChanged(_ sender: NSSwitch) {
        sendPrivacyUpdate(doNotTrack: sender.state == .on)
    }

    @objc private func blockThirdPartyCookiesChanged(_ sender: NSSwitch) {
        sendPrivacyUpdate(blockThirdPartyCookies: sender.state == .on)
    }

    @objc private func contentBlockerChanged(_ sender: NSSwitch) {
        sendPrivacyUpdate(contentBlockerEnabled: sender.state == .on)
    }

    @objc private func popupBlockerChanged(_ sender: NSSwitch) {
        sendPrivacyUpdate(popupBlockerEnabled: sender.state == .on)
    }

    @objc private func searchSuggestionsChanged(_ sender: NSSwitch) {
        sendPrivacyUpdate(searchSuggestionsEnabled: sender.state == .on)
    }

    @objc private func safeBrowsingChanged(_ sender: NSSwitch) {
        sendPrivacyUpdate(safeBrowsingEnabled: sender.state == .on)
    }

    @objc private func clearDataOnExitChanged(_ sender: NSSwitch) {
        sendPrivacyUpdate(clearDataOnExit: sender.state == .on)
    }

    @objc private func secureDnsChanged(_ sender: NSSwitch) {
        secureDnsRow.isHidden = sender.state == .off
        sendPrivacyUpdate(secureDnsEnabled: sender.state == .on)
    }

    @objc private func secureDnsCustomUrlChanged(_ sender: NSTextField) {
        let value = sender.stringValue.isEmpty ? nil : sender.stringValue
        sendPrivacyUpdate(secureDnsCustomUrl: value)
    }
}
