@file:OptIn(ExperimentalSerializationApi::class)

package dev.maho.browser.models

import dev.maho.browser.support.Patchable
import kotlinx.serialization.EncodeDefault
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.KSerializer
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder

@Serializable
enum class Theme {
    @SerialName("light") Light,
    @SerialName("dark") Dark,
    @SerialName("system") System,
}

@Serializable
enum class Density {
    @SerialName("compact") Compact,
    @SerialName("comfortable") Comfortable,
}

@Serializable
enum class RestorePolicy {
    @SerialName("restore_all") RestoreAll,
    @SerialName("restore_pinned") RestorePinned,
    @SerialName("start_fresh") StartFresh,
}

@Serializable
enum class ReaderTheme {
    @SerialName("light") Light,
    @SerialName("sepia") Sepia,
    @SerialName("dark") Dark,
}

@Serializable
enum class AutoplayPolicy {
    @SerialName("allow") Allow,
    @SerialName("block_all") BlockAll,
    @SerialName("block_audio") BlockAudio,
}

@Serializable
enum class KeyModifier {
    @SerialName("ctrl") Ctrl,
    @SerialName("shift") Shift,
    @SerialName("alt") Alt,
    @SerialName("meta") Meta,
}

@Serializable
enum class PermissionPolicy {
    @SerialName("allow") Allow,
    @SerialName("deny") Deny,
    @SerialName("ask") Ask,
}

@Serializable
enum class ToolbarItemKind {
    @SerialName("back_forward") BackForward,
    @SerialName("reload") Reload,
    @SerialName("address_bar") AddressBar,
    @SerialName("share") Share,
    @SerialName("downloads") Downloads,
    @SerialName("extensions") Extensions,
    @SerialName("split_view") SplitView,
    @SerialName("spacer") Spacer,
    @SerialName("flexible_spacer") FlexibleSpacer,
    @SerialName("custom") Custom,
}

@Serializable
enum class PinnedCloseBehavior {
    @SerialName("switch") Switch,
    @SerialName("reset") Reset,
    @SerialName("reset-switch") ResetSwitch,
    @SerialName("unload-switch") UnloadSwitch,
    @SerialName("reset-unload-switch") ResetUnloadSwitch,
    @SerialName("close") Close,
}

@Serializable
data class ReaderSettings(
    @SerialName("fontFamily") val fontFamily: String,
    @SerialName("fontSize") val fontSize: Double,
    val theme: ReaderTheme,
)

@Serializable
data class SettingsSearchEngine(
    val name: String,
    @SerialName("urlTemplate") val urlTemplate: String,
    @SerialName("isDefault") val isDefault: Boolean,
)

@Serializable
data class SiteSearchEntry(
    val keyword: String,
    val name: String,
    @SerialName("urlTemplate") val urlTemplate: String,
    @SerialName("colorName") val colorName: String? = null,
)

@Serializable
data class AppearanceSettings(
    val theme: Theme,
    val density: Density,
    @SerialName("sidebarWidth") val sidebarWidth: Double,
    @SerialName("showTabBar") val showTabBar: Boolean,
    @SerialName("windowTransparency") val windowTransparency: Boolean,
    @SerialName("sidebarCollapsed") val sidebarCollapsed: Boolean = false,
    @SerialName("customChromeCss") val customChromeCss: String? = null,
    @SerialName("customIconPath") val customIconPath: String? = null,
)

@Serializable
data class GeneralSettings(
    @SerialName("defaultSearchEngine") val defaultSearchEngine: SettingsSearchEngine,
    @SerialName("todayTabTimeoutHours") val todayTabTimeoutHours: Double,
    @SerialName("restoreOnLaunch") val restoreOnLaunch: RestorePolicy,
    @SerialName("downloadPath") val downloadPath: String,
    @SerialName("autoplayPolicy") val autoplayPolicy: AutoplayPolicy,
    @SerialName("archiveTimeoutHours") val archiveTimeoutHours: Double = 24.0,
    @SerialName("siteSearchEntries") val siteSearchEntries: List<SiteSearchEntry> = emptyList(),
    @SerialName("pinnedCloseBehavior") val pinnedCloseBehavior: PinnedCloseBehavior = PinnedCloseBehavior.Switch,
)

@Serializable(with = ContentBlockingModeSerializer::class)
enum class ContentBlockingMode {
    @SerialName("native") NATIVE,
    @SerialName("extension") EXTENSION,
    @SerialName("disabled") DISABLED,
    UNKNOWN;

    val isNative: Boolean get() = this == NATIVE
}

object ContentBlockingModeSerializer : KSerializer<ContentBlockingMode> {
    override val descriptor = kotlinx.serialization.descriptors.PrimitiveSerialDescriptor(
        "ContentBlockingMode",
        kotlinx.serialization.descriptors.PrimitiveKind.STRING,
    )

    override fun serialize(encoder: Encoder, value: ContentBlockingMode) {
        encoder.encodeString(
            when (value) {
                ContentBlockingMode.NATIVE -> "native"
                ContentBlockingMode.EXTENSION -> "extension"
                ContentBlockingMode.DISABLED -> "disabled"
                ContentBlockingMode.UNKNOWN -> "unknown"
            },
        )
    }

    override fun deserialize(decoder: Decoder): ContentBlockingMode = when (decoder.decodeString()) {
        "native" -> ContentBlockingMode.NATIVE
        "extension" -> ContentBlockingMode.EXTENSION
        "disabled" -> ContentBlockingMode.DISABLED
        else -> ContentBlockingMode.UNKNOWN
    }
}

@Serializable(with = PrivacySettingsSerializer::class)
data class PrivacySettings(
    @SerialName("doNotTrack") val doNotTrack: Boolean,
    @SerialName("blockThirdPartyCookies") val blockThirdPartyCookies: Boolean,
    @SerialName("contentBlockingMode") val contentBlockingMode: ContentBlockingMode = ContentBlockingMode.NATIVE,
    @SerialName("contentBlockerEnabled") val contentBlockerEnabled: Boolean = true,
    @SerialName("popupBlockerEnabled") val popupBlockerEnabled: Boolean,
    @SerialName("searchSuggestionsEnabled") val searchSuggestionsEnabled: Boolean = false,
    @SerialName("secureDnsEnabled") val secureDnsEnabled: Boolean = false,
    @SerialName("secureDnsProvider") val secureDnsProvider: String = "",
    @SerialName("secureDnsCustomUrl") val secureDnsCustomUrl: String? = null,
    @SerialName("clearDataOnExit") val clearDataOnExit: Boolean = false,
    @SerialName("safeBrowsingEnabled") val safeBrowsingEnabled: Boolean = true,
) {
    val isNativeBlockingEnabled: Boolean get() = contentBlockingMode.isNative
}

private object PrivacySettingsSerializer : KSerializer<PrivacySettings> {
    private val delegate = PrivacySettingsWire.serializer()

    override val descriptor = delegate.descriptor

    override fun serialize(encoder: Encoder, value: PrivacySettings) {
        delegate.serialize(
            encoder,
            PrivacySettingsWire(
                doNotTrack = value.doNotTrack,
                blockThirdPartyCookies = value.blockThirdPartyCookies,
                contentBlockingMode = value.contentBlockingMode,
                contentBlockerEnabled = value.contentBlockerEnabled,
                popupBlockerEnabled = value.popupBlockerEnabled,
                searchSuggestionsEnabled = value.searchSuggestionsEnabled,
                secureDnsEnabled = value.secureDnsEnabled,
                secureDnsProvider = value.secureDnsProvider,
                secureDnsCustomUrl = value.secureDnsCustomUrl,
                clearDataOnExit = value.clearDataOnExit,
                safeBrowsingEnabled = value.safeBrowsingEnabled,
            ),
        )
    }

    override fun deserialize(decoder: Decoder): PrivacySettings {
        val wire = delegate.deserialize(decoder)
        return PrivacySettings(
            doNotTrack = wire.doNotTrack,
            blockThirdPartyCookies = wire.blockThirdPartyCookies,
            contentBlockingMode = wire.contentBlockingMode
                ?: if (wire.contentBlockerEnabled) ContentBlockingMode.NATIVE else ContentBlockingMode.DISABLED,
            contentBlockerEnabled = wire.contentBlockerEnabled,
            popupBlockerEnabled = wire.popupBlockerEnabled,
            searchSuggestionsEnabled = wire.searchSuggestionsEnabled,
            secureDnsEnabled = wire.secureDnsEnabled,
            secureDnsProvider = wire.secureDnsProvider,
            secureDnsCustomUrl = wire.secureDnsCustomUrl,
            clearDataOnExit = wire.clearDataOnExit,
            safeBrowsingEnabled = wire.safeBrowsingEnabled,
        )
    }
}

@Serializable
private data class PrivacySettingsWire(
    @SerialName("doNotTrack") val doNotTrack: Boolean,
    @SerialName("blockThirdPartyCookies") val blockThirdPartyCookies: Boolean,
    @SerialName("contentBlockingMode") val contentBlockingMode: ContentBlockingMode? = null,
    @SerialName("contentBlockerEnabled") val contentBlockerEnabled: Boolean = true,
    @SerialName("popupBlockerEnabled") val popupBlockerEnabled: Boolean,
    @SerialName("searchSuggestionsEnabled") val searchSuggestionsEnabled: Boolean = false,
    @SerialName("secureDnsEnabled") val secureDnsEnabled: Boolean = false,
    @SerialName("secureDnsProvider") val secureDnsProvider: String = "",
    @SerialName("secureDnsCustomUrl") val secureDnsCustomUrl: String? = null,
    @SerialName("clearDataOnExit") val clearDataOnExit: Boolean = false,
    @SerialName("safeBrowsingEnabled") val safeBrowsingEnabled: Boolean = true,
)

@Serializable
data class KeyboardShortcut(
    val action: String,
    val key: String,
    val modifiers: List<KeyModifier>,
)

@Serializable
data class PerSiteSettings(
    @SerialName("urlPattern") val urlPattern: String,
    @SerialName("zoomLevel") val zoomLevel: Double? = null,
    val permissions: Map<String, PermissionPolicy>? = null,
    val notifications: PermissionPolicy? = null,
)

@Serializable
data class ToolbarItem(
    val id: String,
    val kind: ToolbarItemKind,
    val visible: Boolean,
    val label: String,
)

@Serializable
data class MaxSettings(
    val enabled: Boolean = false,
    @SerialName("pagePreviews") val pagePreviews: Boolean = false,
    @SerialName("tidyTabTitles") val tidyTabTitles: Boolean = false,
    @SerialName("tidyDownloads") val tidyDownloads: Boolean = false,
    @SerialName("tidyTabs") val tidyTabs: Boolean = false,
    @SerialName("aiCommandBar") val aiCommandBar: Boolean = false,
    @SerialName("instantLinks") val instantLinks: Boolean = false,
)

@Serializable
data class AutofillSettings(
    @SerialName("addressesEnabled") val addressesEnabled: Boolean = true,
    @SerialName("paymentsEnabled") val paymentsEnabled: Boolean = true,
)

@Serializable
data class AdvancedSettings(
    @SerialName("developerMode") val developerMode: Boolean = false,
    @SerialName("hardwareAcceleration") val hardwareAcceleration: Boolean = true,
    @SerialName("experimentalFeatures") val experimentalFeatures: Boolean = false,
)

@Serializable
data class NotificationSettings(
    val enabled: Boolean = true,
    @SerialName("calendarNotifications") val calendarNotifications: Boolean = true,
    @SerialName("updateNotifications") val updateNotifications: Boolean = true,
    @SerialName("soundEnabled") val soundEnabled: Boolean = true,
)

@Serializable
data class Settings(
    val general: GeneralSettings,
    val appearance: AppearanceSettings,
    val privacy: PrivacySettings,
    val reader: ReaderSettings,
    @SerialName("keyboardShortcuts") val keyboardShortcuts: List<KeyboardShortcut>,
    @SerialName("perSiteSettings") val perSiteSettings: List<PerSiteSettings>,
    @SerialName("toolbarItems") val toolbarItems: List<ToolbarItem> = emptyList(),
    val max: MaxSettings = MaxSettings(),
    val autofill: AutofillSettings = AutofillSettings(),
    val advanced: AdvancedSettings = AdvancedSettings(),
    val notifications: NotificationSettings = NotificationSettings(),
)

@Serializable
data class SettingsUpdate(
    val general: GeneralSettingsUpdate? = null,
    val appearance: AppearanceSettingsUpdate? = null,
    val privacy: PrivacySettingsUpdate? = null,
    val reader: ReaderSettingsUpdate? = null,
    @SerialName("keyboardShortcuts") val keyboardShortcuts: List<KeyboardShortcut>? = null,
    @SerialName("perSiteSettings") val perSiteSettings: List<PerSiteSettings>? = null,
    @SerialName("toolbarItems") val toolbarItems: List<ToolbarItem>? = null,
    val max: MaxSettingsUpdate? = null,
    val autofill: AutofillSettingsUpdate? = null,
    val advanced: AdvancedSettingsUpdate? = null,
    val notifications: NotificationSettingsUpdate? = null,
)

@Serializable
data class GeneralSettingsUpdate(
    @SerialName("defaultSearchEngine") val defaultSearchEngine: SettingsSearchEngine? = null,
    @SerialName("todayTabTimeoutHours") val todayTabTimeoutHours: Double? = null,
    @SerialName("restoreOnLaunch") val restoreOnLaunch: RestorePolicy? = null,
    @SerialName("downloadPath") val downloadPath: String? = null,
    @SerialName("autoplayPolicy") val autoplayPolicy: AutoplayPolicy? = null,
    @SerialName("archiveTimeoutHours") val archiveTimeoutHours: Double? = null,
    @SerialName("siteSearchEntries") val siteSearchEntries: List<SiteSearchEntry>? = null,
    @SerialName("pinnedCloseBehavior") val pinnedCloseBehavior: PinnedCloseBehavior? = null,
)

@Serializable
data class AppearanceSettingsUpdate(
    val theme: Theme? = null,
    val density: Density? = null,
    @SerialName("sidebarWidth") val sidebarWidth: Double? = null,
    @SerialName("showTabBar") val showTabBar: Boolean? = null,
    @SerialName("windowTransparency") val windowTransparency: Boolean? = null,
    @SerialName("sidebarCollapsed") val sidebarCollapsed: Boolean? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("customChromeCss") val customChromeCss: Patchable<String> = Patchable.Absent,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("customIconPath") val customIconPath: Patchable<String> = Patchable.Absent,
)

@Serializable
data class PrivacySettingsUpdate(
    @SerialName("doNotTrack") val doNotTrack: Boolean? = null,
    @SerialName("blockThirdPartyCookies") val blockThirdPartyCookies: Boolean? = null,
    @SerialName("contentBlockerEnabled") val contentBlockerEnabled: Boolean? = null,
    @SerialName("contentBlockingMode") val contentBlockingMode: ContentBlockingMode? = null,
    @SerialName("popupBlockerEnabled") val popupBlockerEnabled: Boolean? = null,
    @SerialName("searchSuggestionsEnabled") val searchSuggestionsEnabled: Boolean? = null,
    @SerialName("secureDnsEnabled") val secureDnsEnabled: Boolean? = null,
    @SerialName("secureDnsProvider") val secureDnsProvider: String? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("secureDnsCustomUrl") val secureDnsCustomUrl: Patchable<String> = Patchable.Absent,
    @SerialName("clearDataOnExit") val clearDataOnExit: Boolean? = null,
    @SerialName("safeBrowsingEnabled") val safeBrowsingEnabled: Boolean? = null,
)

@Serializable
data class ReaderSettingsUpdate(
    @SerialName("fontFamily") val fontFamily: String? = null,
    @SerialName("fontSize") val fontSize: Double? = null,
    val theme: ReaderTheme? = null,
)

@Serializable
data class MaxSettingsUpdate(
    val enabled: Boolean? = null,
    @SerialName("pagePreviews") val pagePreviews: Boolean? = null,
    @SerialName("tidyTabTitles") val tidyTabTitles: Boolean? = null,
    @SerialName("tidyDownloads") val tidyDownloads: Boolean? = null,
    @SerialName("tidyTabs") val tidyTabs: Boolean? = null,
    @SerialName("aiCommandBar") val aiCommandBar: Boolean? = null,
    @SerialName("instantLinks") val instantLinks: Boolean? = null,
)

@Serializable
data class AutofillSettingsUpdate(
    @SerialName("addressesEnabled") val addressesEnabled: Boolean? = null,
    @SerialName("paymentsEnabled") val paymentsEnabled: Boolean? = null,
)

@Serializable
data class AdvancedSettingsUpdate(
    @SerialName("developerMode") val developerMode: Boolean? = null,
    @SerialName("hardwareAcceleration") val hardwareAcceleration: Boolean? = null,
    @SerialName("experimentalFeatures") val experimentalFeatures: Boolean? = null,
)

@Serializable
data class NotificationSettingsUpdate(
    val enabled: Boolean? = null,
    @SerialName("calendarNotifications") val calendarNotifications: Boolean? = null,
    @SerialName("updateNotifications") val updateNotifications: Boolean? = null,
    @SerialName("soundEnabled") val soundEnabled: Boolean? = null,
)
