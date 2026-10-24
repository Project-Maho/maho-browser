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

    /// Full persisted settings document (camelCase keys). Some fields, such as
    /// restore-on-launch and autoplay, are not emitted in the view model.
    func getSettingsDocument() -> [String: Any]? {
        guard let json = withCore({ ptr in FFIString.consume(maho_core_get_settings(ptr)) }) ?? nil,
              let data = json.data(using: .utf8) else {
            return nil
        }
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }

    /// Autoplay policy for new web views, from the full settings document.
    func autoplayPolicy() -> AutoplayPolicy {
        guard let general = getSettingsDocument()?["general"] as? [String: Any],
              let raw = general["autoplayPolicy"] as? String,
              let policy = AutoplayPolicy(rawValue: raw) else {
            return .blockAudio
        }
        return policy
    }

    /// Restore-on-launch policy from the full settings document.
    func restorePolicy() -> RestorePolicy {
        guard let general = getSettingsDocument()?["general"] as? [String: Any],
              let raw = general["restoreOnLaunch"] as? String,
              let policy = RestorePolicy(rawValue: raw) else {
            return .restoreAll
        }
        return policy
    }

    /// Applies "On Launch" once after state is loaded: Restore Pinned Only
    /// closes normal tabs; Start Fresh also returns pinned tabs to their home.
    /// Core prunes on this device only and queues no sync deletions, so tabs
    /// open on the user's other devices are never closed by this.
    func applyRestorePolicyOnLaunch() {
        guard restorePolicy() != .restoreAll else { return }
        sendEvent(.applyRestorePolicyOnLaunch)
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
        NotificationCenter.default.post(name: .mahoContentBlockerChanged, object: nil)
        if enabled {
            FilterListUpdater.shared.refreshIfNeeded(force: true)
        }
    }

    /// Rules in the filter lists currently installed in core (0 until the first
    /// successful download).
    func contentBlockerRuleCount() -> Int {
        Int(withCore { ptr in maho_core_get_content_rule_count(ptr) } ?? 0)
    }

    /// Persisted Safe Browsing flag, applied to new web views.
    func isSafeBrowsingEnabled() -> Bool {
        (CoreSettingsSnapshot.values(from: self)["safe_browsing"] as? Bool) ?? true
    }

    func isClearDataOnExitEnabled() -> Bool {
        (CoreSettingsSnapshot.values(from: self)["clear_data_on_exit"] as? Bool) ?? false
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
