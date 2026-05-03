import AppKit

final class NotificationSettingsViewController: NSViewController, CoreUpdateObserver {

    private var enabledSwitch = NSSwitch()
    private var calendarSwitch = NSSwitch()
    private var updatesSwitch = NSSwitch()
    private var soundSwitch = NSSwitch()

    override func loadView() {
        enabledSwitch.target = self
        enabledSwitch.action = #selector(enabledChanged(_:))
        let enabledRow = makeSettingsLabeledRow("Notifications Enabled:", enabledSwitch)

        calendarSwitch.target = self
        calendarSwitch.action = #selector(calendarChanged(_:))
        let calendarRow = makeSettingsLabeledRow("Calendar Notifications:", calendarSwitch)

        updatesSwitch.target = self
        updatesSwitch.action = #selector(updatesChanged(_:))
        let updatesRow = makeSettingsLabeledRow("Update Notifications:", updatesSwitch)

        soundSwitch.target = self
        soundSwitch.action = #selector(soundChanged(_:))
        let soundRow = makeSettingsLabeledRow("Sound Enabled:", soundSwitch)

        view = SettingsPaneHostView(sections: [
            .card([
                enabledRow,
                calendarRow,
                updatesRow,
                soundRow,
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

        enabledSwitch.state = s.notifications.enabled ? .on : .off
        calendarSwitch.state = s.notifications.calendarNotifications ? .on : .off
        updatesSwitch.state = s.notifications.updateNotifications ? .on : .off
        soundSwitch.state = s.notifications.soundEnabled ? .on : .off
    }

    private func sendNotificationUpdate(
        enabled: Bool? = nil,
        calendarNotifications: Bool? = nil,
        updateNotifications: Bool? = nil,
        soundEnabled: Bool? = nil
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
            advanced: nil,
            notifications: NotificationSettingsUpdate(
                enabled: enabled,
                calendarNotifications: calendarNotifications,
                updateNotifications: updateNotifications,
                soundEnabled: soundEnabled
            )
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func enabledChanged(_ sender: NSSwitch) {
        sendNotificationUpdate(enabled: sender.state == .on)
    }

    @objc private func calendarChanged(_ sender: NSSwitch) {
        sendNotificationUpdate(calendarNotifications: sender.state == .on)
    }

    @objc private func updatesChanged(_ sender: NSSwitch) {
        sendNotificationUpdate(updateNotifications: sender.state == .on)
    }

    @objc private func soundChanged(_ sender: NSSwitch) {
        sendNotificationUpdate(soundEnabled: sender.state == .on)
    }
}
