use serde::{Deserialize, Serialize};

use crate::autofill::{AutofillSettings, AutofillSettingsUpdate};
use crate::content_blocking::ContentBlockingMode;
use crate::keyboard::KeyModifier;
use crate::max_settings::{MaxSettings, MaxSettingsUpdate};

fn default_true() -> bool {
    true
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Theme {
    Light,
    Dark,
    System,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Density {
    Compact,
    Comfortable,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RestorePolicy {
    RestoreAll,
    RestorePinned,
    StartFresh,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ReaderTheme {
    Light,
    Sepia,
    Dark,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AutoplayPolicy {
    Allow,
    BlockAll,
    BlockAudio,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum PinnedCloseBehavior {
    #[serde(rename = "switch")]
    Switch,
    #[serde(rename = "reset")]
    Reset,
    #[serde(rename = "reset-switch")]
    ResetSwitch,
    #[serde(rename = "unload-switch")]
    UnloadSwitch,
    #[serde(rename = "reset-unload-switch")]
    ResetUnloadSwitch,
    #[serde(rename = "close")]
    Close,
}

fn default_pinned_close_behavior() -> PinnedCloseBehavior {
    PinnedCloseBehavior::Switch
}

/// Where a newly created tab is inserted in the sidebar's normal-tab section.
/// `Top` reproduces the historical behaviour (insert above existing tabs).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum NewTabPosition {
    Top,
    Bottom,
}

/// Unknown wire values decode to `Top` rather than failing. Settings arrive as
/// one JSON blob, so a value written by a newer/older shell (or a hand-edited
/// store) must not take the whole settings decode down with it.
impl<'de> Deserialize<'de> for NewTabPosition {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let raw = String::deserialize(deserializer)?;
        Ok(match raw.as_str() {
            "bottom" => NewTabPosition::Bottom,
            _ => NewTabPosition::Top,
        })
    }
}

fn default_new_tab_position() -> NewTabPosition {
    NewTabPosition::Top
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReaderSettings {
    pub font_family: String,
    pub font_size: f64,
    pub theme: ReaderTheme,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SearchEngine {
    pub name: String,
    pub url_template: String,
    pub is_default: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppearanceSettings {
    pub theme: Theme,
    pub density: Density,
    pub sidebar_width: f64,
    pub show_tab_bar: bool,
    pub window_transparency: bool,
    #[serde(default)]
    pub sidebar_collapsed: bool,
    #[serde(default)]
    pub custom_icon_path: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GeneralSettings {
    pub default_search_engine: SearchEngine,
    pub today_tab_timeout_hours: f64,
    pub restore_on_launch: RestorePolicy,
    pub download_path: String,
    pub autoplay_policy: AutoplayPolicy,
    #[serde(default = "default_archive_timeout")]
    pub archive_timeout_hours: f64,
    #[serde(default = "default_site_search_entries")]
    pub site_search_entries: Vec<SiteSearchEntry>,
    #[serde(default = "default_pinned_close_behavior")]
    pub pinned_close_behavior: PinnedCloseBehavior,
    #[serde(default = "default_true")]
    pub auto_delete_empty_folders_on_tidy: bool,
    #[serde(default = "default_new_tab_position")]
    pub new_tab_position: NewTabPosition,
}

fn default_archive_timeout() -> f64 {
    24.0
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SiteSearchEntry {
    pub keyword: String,
    pub name: String,
    pub url_template: String,
    #[serde(default)]
    pub color_name: Option<String>,
}

fn default_site_search_entries() -> Vec<SiteSearchEntry> {
    vec![
        SiteSearchEntry {
            keyword: "g".to_string(),
            name: "Google".to_string(),
            url_template: "https://www.google.com/search?q={query}".to_string(),
            color_name: None,
        },
        SiteSearchEntry {
            keyword: "yt".to_string(),
            name: "YouTube".to_string(),
            url_template: "https://www.youtube.com/results?search_query={query}".to_string(),
            color_name: None,
        },
        SiteSearchEntry {
            keyword: "gh".to_string(),
            name: "GitHub".to_string(),
            url_template: "https://github.com/search?q={query}".to_string(),
            color_name: None,
        },
        SiteSearchEntry {
            keyword: "w".to_string(),
            name: "Wikipedia".to_string(),
            url_template: "https://en.wikipedia.org/wiki/Special:Search?search={query}".to_string(),
            color_name: None,
        },
        SiteSearchEntry {
            keyword: "r".to_string(),
            name: "Reddit".to_string(),
            url_template: "https://www.reddit.com/search/?q={query}".to_string(),
            color_name: None,
        },
    ]
}

#[derive(Clone, Debug)]
pub struct PrivacySettings {
    pub do_not_track: bool,
    pub block_third_party_cookies: bool,
    pub content_blocking_mode: ContentBlockingMode,
    pub content_blocker_enabled: bool,
    pub popup_blocker_enabled: bool,
    pub search_suggestions_enabled: bool,
    pub secure_dns_enabled: bool,
    pub secure_dns_provider: String,
    pub secure_dns_custom_url: Option<String>,
    pub clear_data_on_exit: bool,
    pub safe_browsing_enabled: bool,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct PrivacySettingsDe {
    pub do_not_track: bool,
    pub block_third_party_cookies: bool,
    pub content_blocking_mode: Option<ContentBlockingMode>,
    pub content_blocker_enabled: Option<bool>,
    pub popup_blocker_enabled: bool,
    #[serde(default)]
    pub search_suggestions_enabled: bool,
    #[serde(default)]
    pub secure_dns_enabled: bool,
    #[serde(default)]
    pub secure_dns_provider: String,
    #[serde(default)]
    pub secure_dns_custom_url: Option<String>,
    #[serde(default)]
    pub clear_data_on_exit: bool,
    #[serde(default = "default_true")]
    pub safe_browsing_enabled: bool,
}

impl<'de> Deserialize<'de> for PrivacySettings {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let raw = PrivacySettingsDe::deserialize(deserializer)?;
        let mode = match (raw.content_blocking_mode, raw.content_blocker_enabled) {
            (Some(mode), _) => mode,
            (None, Some(enabled)) => ContentBlockingMode::from_legacy_bool(enabled),
            (None, None) => ContentBlockingMode::Native,
        };
        let content_blocker_enabled = mode.to_legacy_bool();
        Ok(PrivacySettings {
            do_not_track: raw.do_not_track,
            block_third_party_cookies: raw.block_third_party_cookies,
            content_blocking_mode: mode,
            content_blocker_enabled,
            popup_blocker_enabled: raw.popup_blocker_enabled,
            search_suggestions_enabled: raw.search_suggestions_enabled,
            secure_dns_enabled: raw.secure_dns_enabled,
            secure_dns_provider: raw.secure_dns_provider,
            secure_dns_custom_url: raw.secure_dns_custom_url,
            clear_data_on_exit: raw.clear_data_on_exit,
            safe_browsing_enabled: raw.safe_browsing_enabled,
        })
    }
}

impl Serialize for PrivacySettings {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        use serde::ser::SerializeStruct;
        let mut state = serializer.serialize_struct("PrivacySettings", 11)?;
        state.serialize_field("doNotTrack", &self.do_not_track)?;
        state.serialize_field("blockThirdPartyCookies", &self.block_third_party_cookies)?;
        state.serialize_field("contentBlockingMode", &self.content_blocking_mode)?;
        state.serialize_field("contentBlockerEnabled", &self.content_blocker_enabled)?;
        state.serialize_field("popupBlockerEnabled", &self.popup_blocker_enabled)?;
        state.serialize_field("searchSuggestionsEnabled", &self.search_suggestions_enabled)?;
        state.serialize_field("secureDnsEnabled", &self.secure_dns_enabled)?;
        state.serialize_field("secureDnsProvider", &self.secure_dns_provider)?;
        state.serialize_field("secureDnsCustomUrl", &self.secure_dns_custom_url)?;
        state.serialize_field("clearDataOnExit", &self.clear_data_on_exit)?;
        state.serialize_field("safeBrowsingEnabled", &self.safe_browsing_enabled)?;
        state.end()
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct KeyboardShortcut {
    pub action: String,
    pub key: String,
    pub modifiers: Vec<KeyModifier>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PerSiteSettings {
    pub url_pattern: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub zoom_level: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub permissions: Option<std::collections::HashMap<String, PermissionPolicy>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub notifications: Option<PermissionPolicy>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum PermissionPolicy {
    Allow,
    Deny,
    Ask,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ToolbarItemKind {
    BackForward,
    Reload,
    AddressBar,
    Share,
    Downloads,
    Extensions,
    SplitView,
    Spacer,
    FlexibleSpacer,
    Custom,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolbarItem {
    pub id: String,
    pub kind: ToolbarItemKind,
    pub visible: bool,
    pub label: String,
}

fn default_settings_schema_version() -> u32 {
    2
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    #[serde(default = "default_settings_schema_version")]
    pub settings_schema_version: u32,
    pub general: GeneralSettings,
    pub appearance: AppearanceSettings,
    pub privacy: PrivacySettings,
    pub reader: ReaderSettings,
    pub keyboard_shortcuts: Vec<KeyboardShortcut>,
    pub per_site_settings: Vec<PerSiteSettings>,
    #[serde(default)]
    pub toolbar_items: Vec<ToolbarItem>,
    #[serde(default)]
    pub max: MaxSettings,
    #[serde(default)]
    pub autofill: AutofillSettings,
    #[serde(default)]
    pub advanced: AdvancedSettings,
    #[serde(default)]
    pub notifications: NotificationSettings,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct SettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub general: Option<GeneralSettingsUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub appearance: Option<AppearanceSettingsUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub privacy: Option<PrivacySettingsUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reader: Option<ReaderSettingsUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub keyboard_shortcuts: Option<Vec<KeyboardShortcut>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub per_site_settings: Option<Vec<PerSiteSettings>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub toolbar_items: Option<Vec<ToolbarItem>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub max: Option<MaxSettingsUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub autofill: Option<AutofillSettingsUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub advanced: Option<AdvancedSettingsUpdate>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub notifications: Option<NotificationSettingsUpdate>,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct GeneralSettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub default_search_engine: Option<SearchEngine>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub today_tab_timeout_hours: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub restore_on_launch: Option<RestorePolicy>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub download_path: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub autoplay_policy: Option<AutoplayPolicy>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub archive_timeout_hours: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub site_search_entries: Option<Vec<SiteSearchEntry>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pinned_close_behavior: Option<PinnedCloseBehavior>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub auto_delete_empty_folders_on_tidy: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub new_tab_position: Option<NewTabPosition>,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct AppearanceSettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub theme: Option<Theme>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub density: Option<Density>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sidebar_width: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub show_tab_bar: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub window_transparency: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sidebar_collapsed: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub custom_icon_path: Option<Option<String>>,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct PrivacySettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub do_not_track: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub block_third_party_cookies: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub content_blocking_mode: Option<ContentBlockingMode>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub content_blocker_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub popup_blocker_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub search_suggestions_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub secure_dns_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub secure_dns_provider: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub secure_dns_custom_url: Option<Option<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub clear_data_on_exit: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub safe_browsing_enabled: Option<bool>,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct ReaderSettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub font_family: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub font_size: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub theme: Option<ReaderTheme>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AdvancedSettings {
    pub developer_mode: bool,
    pub hardware_acceleration: bool,
    pub experimental_features: bool,
}

impl Default for AdvancedSettings {
    fn default() -> Self {
        Self {
            developer_mode: false,
            hardware_acceleration: true,
            experimental_features: false,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct AdvancedSettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub developer_mode: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hardware_acceleration: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub experimental_features: Option<bool>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NotificationSettings {
    pub enabled: bool,
    pub calendar_notifications: bool,
    pub update_notifications: bool,
    pub sound_enabled: bool,
}

impl Default for NotificationSettings {
    fn default() -> Self {
        Self {
            enabled: true,
            calendar_notifications: true,
            update_notifications: true,
            sound_enabled: true,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct NotificationSettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub calendar_notifications: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub update_notifications: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sound_enabled: Option<bool>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppIconOption {
    pub id: String,
    pub name: String,
    pub preview_path: String,
    pub is_default: bool,
}

pub fn available_app_icons() -> Vec<AppIconOption> {
    vec![
        AppIconOption {
            id: "default".to_string(),
            name: "Default".to_string(),
            preview_path: "icons/default.png".to_string(),
            is_default: true,
        },
        AppIconOption {
            id: "dark".to_string(),
            name: "Dark".to_string(),
            preview_path: "icons/dark.png".to_string(),
            is_default: false,
        },
        AppIconOption {
            id: "light".to_string(),
            name: "Light".to_string(),
            preview_path: "icons/light.png".to_string(),
            is_default: false,
        },
        AppIconOption {
            id: "colorful".to_string(),
            name: "Colorful".to_string(),
            preview_path: "icons/colorful.png".to_string(),
            is_default: false,
        },
        AppIconOption {
            id: "minimal".to_string(),
            name: "Minimal".to_string(),
            preview_path: "icons/minimal.png".to_string(),
            is_default: false,
        },
        AppIconOption {
            id: "retro".to_string(),
            name: "Retro".to_string(),
            preview_path: "icons/retro.png".to_string(),
            is_default: false,
        },
    ]
}
