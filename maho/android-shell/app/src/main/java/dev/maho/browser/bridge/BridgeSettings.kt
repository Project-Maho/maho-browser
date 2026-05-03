package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.AdvancedSettingsUpdate
import dev.maho.browser.models.AppearanceSettingsUpdate
import dev.maho.browser.models.AutofillSettingsUpdate
import dev.maho.browser.models.GeneralSettingsUpdate
import dev.maho.browser.models.MaxSettingsUpdate
import dev.maho.browser.models.NotificationSettingsUpdate
import dev.maho.browser.models.PrivacySettingsUpdate
import dev.maho.browser.models.ReaderSettingsUpdate
import dev.maho.browser.models.Settings
import dev.maho.browser.models.SettingsUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

object BridgeSettings {

    fun getSettingsTyped(): Settings? {
        val json = MahoBridge.getSettings() ?: return null
        return try {
            MahoJson.instance.decodeFromString<Settings>(json)
        } catch (_: Exception) {
            null
        }
    }

    fun updateSettingsTyped(update: SettingsUpdate) {
        val json = MahoJson.instance.encodeToString(SettingsUpdate.serializer(), update)
        MahoBridge.updateSettings(json)
    }

    fun updateGeneralSettings(update: GeneralSettingsUpdate) {
        updateSettingsTyped(SettingsUpdate(general = update))
    }

    fun updateAppearanceSettings(update: AppearanceSettingsUpdate) {
        updateSettingsTyped(SettingsUpdate(appearance = update))
    }

    fun updatePrivacySettings(update: PrivacySettingsUpdate) {
        updateSettingsTyped(SettingsUpdate(privacy = update))
    }

    fun updateReaderSettings(update: ReaderSettingsUpdate) {
        updateSettingsTyped(SettingsUpdate(reader = update))
    }

    fun updateMaxSettings(update: MaxSettingsUpdate) {
        updateSettingsTyped(SettingsUpdate(max = update))
    }

    fun updateAutofillSettings(update: AutofillSettingsUpdate) {
        updateSettingsTyped(SettingsUpdate(autofill = update))
    }

    fun updateAdvancedSettings(update: AdvancedSettingsUpdate) {
        updateSettingsTyped(SettingsUpdate(advanced = update))
    }

    fun updateNotificationSettings(update: NotificationSettingsUpdate) {
        updateSettingsTyped(SettingsUpdate(notifications = update))
    }

    fun resetAllSettings() {
        MahoBridge.sendEvent(ShellEvent.ResetSettings)
    }
}
