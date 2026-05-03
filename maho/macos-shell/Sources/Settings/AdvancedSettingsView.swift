import AppKit

final class AdvancedSettingsViewController: NSViewController, CoreUpdateObserver {

    private var developerModeSwitch = NSSwitch()
    private var hardwareAccelerationSwitch = NSSwitch()
    private var experimentalFeaturesSwitch = NSSwitch()

    override func loadView() {
        developerModeSwitch.target = self
        developerModeSwitch.action = #selector(developerModeChanged(_:))
        let developerModeRow = makeSettingsLabeledRow("Developer Mode:", developerModeSwitch)

        hardwareAccelerationSwitch.target = self
        hardwareAccelerationSwitch.action = #selector(hardwareAccelerationChanged(_:))
        let hardwareAccelerationRow = makeSettingsLabeledRow("Hardware Acceleration:", hardwareAccelerationSwitch)

        experimentalFeaturesSwitch.target = self
        experimentalFeaturesSwitch.action = #selector(experimentalFeaturesChanged(_:))
        let experimentalFeaturesRow = makeSettingsLabeledRow("Experimental Features:", experimentalFeaturesSwitch)

        let resetButton = NSButton(title: "Reset Settings", target: self, action: #selector(resetSettings(_:)))
        let resetRow = makeSettingsActionRow([resetButton])

        view = SettingsPaneHostView(sections: [
            .plain([
                developerModeRow,
                hardwareAccelerationRow,
                experimentalFeaturesRow,
            ]),
            .card([resetRow]),
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

        developerModeSwitch.state = s.advanced.developerMode ? .on : .off
        hardwareAccelerationSwitch.state = s.advanced.hardwareAcceleration ? .on : .off
        experimentalFeaturesSwitch.state = s.advanced.experimentalFeatures ? .on : .off
    }

    private func sendAdvancedUpdate(
        developerMode: Bool? = nil,
        hardwareAcceleration: Bool? = nil,
        experimentalFeatures: Bool? = nil
    ) {
        let update = SettingsUpdate(
            general: nil,
            appearance: nil,
            privacy: nil,
            reader: nil,
            keyboardShortcuts: nil,
            perSiteSettings: nil,
            toolbarItems: nil,
            max: nil,
            autofill: nil,
            advanced: AdvancedSettingsUpdate(
                developerMode: developerMode,
                hardwareAcceleration: hardwareAcceleration,
                experimentalFeatures: experimentalFeatures
            ),
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func developerModeChanged(_ sender: NSSwitch) {
        sendAdvancedUpdate(developerMode: sender.state == .on)
    }

    @objc private func hardwareAccelerationChanged(_ sender: NSSwitch) {
        sendAdvancedUpdate(hardwareAcceleration: sender.state == .on)
    }

    @objc private func experimentalFeaturesChanged(_ sender: NSSwitch) {
        sendAdvancedUpdate(experimentalFeatures: sender.state == .on)
    }

    @objc private func resetSettings(_ sender: NSButton) {
        let alert = NSAlert()
        alert.messageText = "Reset All Settings?"
        alert.informativeText = "This will restore all settings to their default values. This action cannot be undone."
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Reset")
        alert.addButton(withTitle: "Cancel")
        guard let window = view.window else {
            // Fallback if no window
            if alert.runModal() == .alertFirstButtonReturn {
                CoreUpdateDispatcher.shared.sendAndDispatch(.resetSettings)
            }
            return
        }
        alert.beginSheetModal(for: window) { response in
            if response == .alertFirstButtonReturn {
                CoreUpdateDispatcher.shared.sendAndDispatch(.resetSettings)
            }
        }
    }
}
