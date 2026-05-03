import Foundation

// MARK: - Settings Operations

extension MahoBridge {

    func getSettings() -> Settings? {
        guard let json = callCoreString({ maho_core_get_settings($0) }) else { return nil }
        return decode(json)
    }

    func getSettingsViewModel() -> SettingsViewModel? {
        guard let json = callCoreString({ maho_core_get_settings_view_model($0) }) else { return nil }
        return decode(json)
    }

    func updateSettings(settingsJson: String) {
        withFFIString(settingsJson) { cStr in
            callCoreVoid { maho_core_update_settings($0, cStr) }
        }
    }

    // MARK: - ATC Rules

    func getATCRules() -> String? {
        return callCoreString { maho_core_get_atc_rules($0) }
    }

    func addATCRule(ruleJson: String) -> String? {
        return withFFIString(ruleJson) { cStr -> String? in
            return callCoreString { maho_core_add_atc_rule($0, cStr) }
        }
    }

    func removeATCRule(ruleId: String) -> String? {
        return withFFIString(ruleId) { cStr -> String? in
            return callCoreString { maho_core_remove_atc_rule($0, cStr) }
        }
    }

    func toggleATCRule(ruleId: String, enabled: Bool) {
        withFFIString(ruleId) { cStr in
            callCoreVoid { maho_core_toggle_atc_rule($0, cStr, enabled) }
        }
    }

    // MARK: - Shortcuts

    func getShortcuts() -> String? {
        return callCoreString { maho_core_get_shortcuts($0) }
    }

    func setShortcut(action: String, keyComboJson: String) -> String? {
        return withFFIString(action) { actionCStr -> String? in
            return withFFIString(keyComboJson) { keyCStr -> String? in
                return callCoreString { maho_core_set_shortcut($0, actionCStr, keyCStr) }
            }
        }
    }

    func resetShortcut(action: String) {
        withFFIString(action) { cStr in
            callCoreVoid { maho_core_reset_shortcut($0, cStr) }
        }
    }

    func resetAllShortcuts() {
        callCoreVoid { maho_core_reset_all_shortcuts($0) }
    }

    func resolveShortcut(keyComboJson: String) -> String? {
        return withFFIString(keyComboJson) { cStr -> String? in
            return callCoreString { maho_core_resolve_shortcut($0, cStr) }
        }
    }

    // MARK: - Density & Appearance

    func setDensity(density: String) {
        withFFIString(density) { cStr in
            callCoreVoid { maho_core_set_density($0, cStr) }
        }
    }

    func setAccentColor(colorJson: String) {
        withFFIString(colorJson) { cStr in
            callCoreVoid { maho_core_set_accent_color($0, cStr) }
        }
    }

    func setCustomChromeCss(css: String) {
        withFFIString(css) { cStr in
            callCoreVoid { maho_core_set_custom_chrome_css($0, cStr) }
        }
    }

    func setWindowTransparency(enabled: Bool) {
        callCoreVoid { maho_core_set_window_transparency($0, enabled) }
    }

    // MARK: - Toolbar

    func getToolbarItems() -> String? {
        return callCoreString { maho_core_get_toolbar_items($0) }
    }

    func setToolbarItems(itemsJson: String) -> String? {
        return withFFIString(itemsJson) { cStr -> String? in
            return callCoreString { maho_core_set_toolbar_items($0, cStr) }
        }
    }

    func getDefaultToolbarItems() -> String? {
        return callCoreString { maho_core_get_default_toolbar_items($0) }
    }

    // MARK: - App Icon

    func setAppIcon(path: String) -> String? {
        return withFFIString(path) { cStr -> String? in
            return callCoreString { maho_core_set_app_icon($0, cStr) }
        }
    }

    // MARK: - Autofill

    func getAutofillAddresses() -> [AutofillAddress] {
        guard let json = callCoreString({ maho_core_get_autofill_addresses($0) }) else { return [] }
        return decode(json) ?? []
    }

    func getAutofillPayments() -> [AutofillPayment] {
        guard let json = callCoreString({ maho_core_get_autofill_payments($0) }) else { return [] }
        return decode(json) ?? []
    }
}
