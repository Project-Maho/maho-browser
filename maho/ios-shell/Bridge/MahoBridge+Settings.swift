import Foundation

extension MahoBridge {

    // MARK: - Full Settings

    func getSettings() -> SettingsViewModel? {
        withCore { ptr in
            FFIString.consumeJSON(maho_core_get_settings_view_model(ptr))
        } ?? nil
    }

    func updateSettings(_ update: SettingsUpdate) {
        sendEvent(.updateSettings(changes: update))
    }

    func resetSettings() {
        sendEvent(.resetSettings)
    }

    // MARK: - General

    func updateGeneralSettings(_ update: GeneralSettingsUpdate) {
        updateSettings(SettingsUpdate(general: update))
    }

    func setRestorePolicy(_ policy: RestorePolicy) {
        updateGeneralSettings(GeneralSettingsUpdate(restoreOnLaunch: policy))
    }

    func setAutoplayPolicy(_ policy: AutoplayPolicy) {
        updateGeneralSettings(GeneralSettingsUpdate(autoplayPolicy: policy))
    }

    func setArchiveTimeout(_ hours: Double) {
        updateGeneralSettings(GeneralSettingsUpdate(archiveTimeoutHours: hours))
    }

    func setDownloadPath(_ path: String) {
        updateGeneralSettings(GeneralSettingsUpdate(downloadPath: path))
    }

    // MARK: - Appearance

    func updateAppearanceSettings(_ update: AppearanceSettingsUpdate) {
        updateSettings(SettingsUpdate(appearance: update))
    }

    func setTheme(_ theme: Theme) {
        updateAppearanceSettings(AppearanceSettingsUpdate(theme: theme))
        NotificationCenter.default.post(name: .mahoThemeChanged, object: theme)
    }

    func setDensity(_ density: Density) {
        updateAppearanceSettings(AppearanceSettingsUpdate(density: density))
    }

    func setShowTabBar(_ show: Bool) {
        updateAppearanceSettings(AppearanceSettingsUpdate(showTabBar: show))
    }

    // MARK: - Privacy

    func updatePrivacySettings(_ update: PrivacySettingsUpdate) {
        updateSettings(SettingsUpdate(privacy: update))
    }

    func setDoNotTrack(_ enabled: Bool) {
        updatePrivacySettings(PrivacySettingsUpdate(doNotTrack: enabled))
    }

    func setBlockThirdPartyCookies(_ enabled: Bool) {
        updatePrivacySettings(PrivacySettingsUpdate(blockThirdPartyCookies: enabled))
    }

    func setContentBlocker(_ enabled: Bool) {
        updatePrivacySettings(PrivacySettingsUpdate(contentBlockerEnabled: enabled))
    }

    func setClearDataOnExit(_ enabled: Bool) {
        updatePrivacySettings(PrivacySettingsUpdate(clearDataOnExit: enabled))
    }

    // MARK: - Reader

    func updateReaderSettings(_ update: ReaderSettingsUpdate) {
        updateSettings(SettingsUpdate(reader: update))
    }

    func setReaderFont(_ fontFamily: String) {
        updateReaderSettings(ReaderSettingsUpdate(fontFamily: fontFamily))
    }

    func setReaderFontSize(_ size: Double) {
        updateReaderSettings(ReaderSettingsUpdate(fontSize: size))
    }

    func setReaderTheme(_ theme: ReaderTheme) {
        updateReaderSettings(ReaderSettingsUpdate(theme: theme))
    }

    // MARK: - Max

    func updateMaxSettings(_ update: MaxSettingsUpdate) {
        updateSettings(SettingsUpdate(max: update))
    }

    // MARK: - Autofill

    func updateAutofillSettings(_ update: AutofillSettingsUpdate) {
        updateSettings(SettingsUpdate(autofill: update))
    }

    // MARK: - Advanced

    func updateAdvancedSettings(_ update: AdvancedSettingsUpdate) {
        updateSettings(SettingsUpdate(advanced: update))
    }

    // MARK: - Notifications

    func updateNotificationSettings(_ update: NotificationSettingsUpdate) {
        updateSettings(SettingsUpdate(notifications: update))
    }
}
