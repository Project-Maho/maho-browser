use std::collections::HashMap;

use maho_types::autofill::AutofillSettings;
use maho_types::keyboard::KeyModifier;
use maho_types::max_settings::MaxSettings;
use maho_types::settings::*;
use maho_types::traits::shell_renderer::{
    SettingsItem, SettingsItemType, SettingsSection, SettingsViewModel,
};

pub struct SettingsManager {
    settings: Settings,
    per_site_zoom: HashMap<String, f64>,
}

impl SettingsManager {
    pub fn new() -> Self {
        Self::with_settings(Self::default_settings())
    }

    pub fn with_settings(settings: Settings) -> Self {
        Self {
            settings,
            per_site_zoom: HashMap::new(),
        }
    }

    pub fn update_privacy(&mut self, _update: PrivacySettingsUpdate) {}

    fn default_settings() -> Settings {
        Settings {
            settings_schema_version: 2,
            general: GeneralSettings {
                default_search_engine: SearchEngine {
                    name: "Google".to_string(),
                    url_template: "https://www.google.com/search?q={query}".to_string(),
                    is_default: true,
                },
                today_tab_timeout_hours: 12.0,
                restore_on_launch: RestorePolicy::RestoreAll,
                download_path: "~/Downloads".to_string(),
                autoplay_policy: AutoplayPolicy::BlockAudio,
                archive_timeout_hours: 24.0,
                site_search_entries: vec![
                    SiteSearchEntry {
                        keyword: "g".to_string(),
                        name: "Google".to_string(),
                        url_template: "https://www.google.com/search?q={query}".to_string(),
                        color_name: None,
                    },
                    SiteSearchEntry {
                        keyword: "yt".to_string(),
                        name: "YouTube".to_string(),
                        url_template: "https://www.youtube.com/results?search_query={query}"
                            .to_string(),
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
                        url_template: "https://en.wikipedia.org/wiki/Special:Search?search={query}"
                            .to_string(),
                        color_name: None,
                    },
                    SiteSearchEntry {
                        keyword: "r".to_string(),
                        name: "Reddit".to_string(),
                        url_template: "https://www.reddit.com/search/?q={query}".to_string(),
                        color_name: None,
                    },
                ],
                pinned_close_behavior: PinnedCloseBehavior::Switch,
                auto_delete_empty_folders_on_tidy: true,
                new_tab_position: NewTabPosition::Top,
            },
            appearance: AppearanceSettings {
                theme: Theme::System,
                density: Density::Comfortable,
                sidebar_width: 260.0,
                show_tab_bar: true,
                window_transparency: false,
                sidebar_collapsed: false,
                custom_icon_path: None,
            },
            privacy: PrivacySettings {
                do_not_track: true,
                block_third_party_cookies: false,
                content_blocking_mode: maho_types::content_blocking::ContentBlockingMode::Native,
                content_blocker_enabled: true,
                popup_blocker_enabled: true,
                search_suggestions_enabled: true,
                secure_dns_enabled: false,
                secure_dns_provider: "automatic".to_string(),
                secure_dns_custom_url: None,
                clear_data_on_exit: false,
                safe_browsing_enabled: true,
            },
            reader: ReaderSettings {
                font_family: "Georgia".to_string(),
                font_size: 18.0,
                theme: ReaderTheme::Light,
            },
            keyboard_shortcuts: Vec::new(),
            per_site_settings: Vec::new(),
            toolbar_items: default_toolbar_items(),
            max: MaxSettings::default(),
            autofill: AutofillSettings::default(),
            advanced: AdvancedSettings::default(),
            notifications: NotificationSettings::default(),
        }
    }

    pub fn get_settings(&self) -> &Settings {
        &self.settings
    }

    pub fn restore_settings(&mut self, settings: Settings) {
        self.settings = settings;
    }

    pub fn update_settings(&mut self, changes: SettingsUpdate) -> &Settings {
        if let Some(general) = changes.general {
            if let Some(engine) = general.default_search_engine {
                self.settings.general.default_search_engine = engine;
            }
            if let Some(hours) = general.today_tab_timeout_hours {
                self.settings.general.today_tab_timeout_hours = if hours < 0.0 {
                    -1.0
                } else {
                    hours.clamp(1.0, 720.0)
                };
            }
            if let Some(policy) = general.restore_on_launch {
                self.settings.general.restore_on_launch = policy;
            }
            if let Some(path) = general.download_path {
                self.settings.general.download_path = path;
            }
            if let Some(policy) = general.autoplay_policy {
                self.settings.general.autoplay_policy = policy;
            }
            if let Some(hours) = general.archive_timeout_hours {
                self.settings.general.archive_timeout_hours = if hours < 0.0 {
                    -1.0
                } else {
                    hours.clamp(12.0, 2160.0)
                };
            }
            if let Some(entries) = general.site_search_entries {
                self.settings.general.site_search_entries = entries;
            }
            if let Some(behavior) = general.pinned_close_behavior {
                self.settings.general.pinned_close_behavior = behavior;
            }
            if let Some(v) = general.auto_delete_empty_folders_on_tidy {
                self.settings.general.auto_delete_empty_folders_on_tidy = v;
            }
            if let Some(position) = general.new_tab_position {
                self.settings.general.new_tab_position = position;
            }
        }
        if let Some(appearance) = changes.appearance {
            if let Some(theme) = appearance.theme {
                self.settings.appearance.theme = theme;
            }
            if let Some(density) = appearance.density {
                self.settings.appearance.density = density;
            }
            if let Some(sidebar_width) = appearance.sidebar_width {
                self.settings.appearance.sidebar_width = sidebar_width.clamp(120.0, 600.0);
            }
            if let Some(show_tab_bar) = appearance.show_tab_bar {
                self.settings.appearance.show_tab_bar = show_tab_bar;
            }
            if let Some(window_transparency) = appearance.window_transparency {
                self.settings.appearance.window_transparency = window_transparency;
            }
            if let Some(sidebar_collapsed) = appearance.sidebar_collapsed {
                self.settings.appearance.sidebar_collapsed = sidebar_collapsed;
            }
            if let Some(custom_icon_path) = appearance.custom_icon_path {
                self.settings.appearance.custom_icon_path = custom_icon_path;
            }
        }
        if let Some(privacy) = changes.privacy {
            if let Some(do_not_track) = privacy.do_not_track {
                self.settings.privacy.do_not_track = do_not_track;
            }
            if let Some(block_third_party_cookies) = privacy.block_third_party_cookies {
                self.settings.privacy.block_third_party_cookies = block_third_party_cookies;
            }
            if let Some(content_blocking_mode) = privacy.content_blocking_mode {
                self.settings.privacy.content_blocking_mode = content_blocking_mode;
                self.settings.privacy.content_blocker_enabled =
                    content_blocking_mode.to_legacy_bool();
            }
            if let Some(content_blocker_enabled) = privacy.content_blocker_enabled {
                if privacy.content_blocking_mode.is_none() {
                    let mode = if content_blocker_enabled {
                        maho_types::content_blocking::ContentBlockingMode::Native
                    } else {
                        maho_types::content_blocking::ContentBlockingMode::Disabled
                    };
                    self.settings.privacy.content_blocking_mode = mode;
                }
                self.settings.privacy.content_blocker_enabled = content_blocker_enabled;
            }
            if let Some(popup_blocker_enabled) = privacy.popup_blocker_enabled {
                self.settings.privacy.popup_blocker_enabled = popup_blocker_enabled;
            }
            if let Some(search_suggestions_enabled) = privacy.search_suggestions_enabled {
                self.settings.privacy.search_suggestions_enabled = search_suggestions_enabled;
            }
            if let Some(secure_dns_enabled) = privacy.secure_dns_enabled {
                self.settings.privacy.secure_dns_enabled = secure_dns_enabled;
            }
            if let Some(secure_dns_provider) = privacy.secure_dns_provider {
                self.settings.privacy.secure_dns_provider = secure_dns_provider;
            }
            if let Some(secure_dns_custom_url) = privacy.secure_dns_custom_url {
                self.settings.privacy.secure_dns_custom_url = secure_dns_custom_url;
            }
            if let Some(clear_data_on_exit) = privacy.clear_data_on_exit {
                self.settings.privacy.clear_data_on_exit = clear_data_on_exit;
            }
            if let Some(safe_browsing_enabled) = privacy.safe_browsing_enabled {
                self.settings.privacy.safe_browsing_enabled = safe_browsing_enabled;
            }
        }
        if let Some(shortcuts) = changes.keyboard_shortcuts {
            self.settings.keyboard_shortcuts = shortcuts;
        }
        if let Some(per_site) = changes.per_site_settings {
            self.settings.per_site_settings = per_site;
        }
        if let Some(reader) = changes.reader {
            if let Some(font_family) = reader.font_family {
                self.settings.reader.font_family = font_family;
            }
            if let Some(font_size) = reader.font_size {
                self.settings.reader.font_size = font_size.clamp(8.0, 72.0);
            }
            if let Some(theme) = reader.theme {
                self.settings.reader.theme = theme;
            }
        }
        if let Some(toolbar) = changes.toolbar_items {
            self.settings.toolbar_items = toolbar;
        }
        if let Some(max) = changes.max {
            if let Some(enabled) = max.enabled {
                self.settings.max.enabled = enabled;
            }
            if let Some(page_previews) = max.page_previews {
                self.settings.max.page_previews = page_previews;
            }
            if let Some(tidy_tab_titles) = max.tidy_tab_titles {
                self.settings.max.tidy_tab_titles = tidy_tab_titles;
            }
            if let Some(tidy_downloads) = max.tidy_downloads {
                self.settings.max.tidy_downloads = tidy_downloads;
            }
            if let Some(tidy_tabs) = max.tidy_tabs {
                self.settings.max.tidy_tabs = tidy_tabs;
            }
            if let Some(ai_command_bar) = max.ai_command_bar {
                self.settings.max.ai_command_bar = ai_command_bar;
            }
            if let Some(instant_links) = max.instant_links {
                self.settings.max.instant_links = instant_links;
            }
        }
        if let Some(autofill) = changes.autofill {
            if let Some(addresses_enabled) = autofill.addresses_enabled {
                self.settings.autofill.addresses_enabled = addresses_enabled;
            }
            if let Some(payments_enabled) = autofill.payments_enabled {
                self.settings.autofill.payments_enabled = payments_enabled;
            }
            if let Some(passwords_enabled) = autofill.passwords_enabled {
                self.settings.autofill.passwords_enabled = passwords_enabled;
            }
            if let Some(password_provider) = autofill.password_provider {
                self.settings.autofill.password_provider = password_provider;
            }
            if let Some(vault_auto_lock_minutes) = autofill.vault_auto_lock_minutes {
                self.settings.autofill.vault_auto_lock_minutes = vault_auto_lock_minutes;
            }
            if let Some(vault_require_device_auth) = autofill.vault_require_device_auth {
                self.settings.autofill.vault_require_device_auth = vault_require_device_auth;
            }
        }
        if let Some(advanced) = changes.advanced {
            if let Some(developer_mode) = advanced.developer_mode {
                self.settings.advanced.developer_mode = developer_mode;
            }
            if let Some(hardware_acceleration) = advanced.hardware_acceleration {
                self.settings.advanced.hardware_acceleration = hardware_acceleration;
            }
            if let Some(experimental_features) = advanced.experimental_features {
                self.settings.advanced.experimental_features = experimental_features;
            }
        }
        if let Some(notifications) = changes.notifications {
            if let Some(enabled) = notifications.enabled {
                self.settings.notifications.enabled = enabled;
            }
            if let Some(calendar_notifications) = notifications.calendar_notifications {
                self.settings.notifications.calendar_notifications = calendar_notifications;
            }
            if let Some(update_notifications) = notifications.update_notifications {
                self.settings.notifications.update_notifications = update_notifications;
            }
            if let Some(sound_enabled) = notifications.sound_enabled {
                self.settings.notifications.sound_enabled = sound_enabled;
            }
        }
        &self.settings
    }

    pub fn get_zoom_for_site(&self, url: &str) -> f64 {
        let domain = extract_domain(url);
        self.per_site_zoom.get(&domain).copied().unwrap_or(1.0)
    }

    pub fn set_zoom_for_site(&mut self, url: &str, zoom: f64) {
        let domain = extract_domain(url);
        self.per_site_zoom.insert(domain, zoom);
    }

    pub fn get_reader_settings(&self) -> &ReaderSettings {
        &self.settings.reader
    }

    pub fn reset_to_defaults(&mut self) -> &Settings {
        self.settings = Self::default_settings();
        self.per_site_zoom.clear();
        &self.settings
    }

    pub fn get_per_site_settings(&self, url: &str) -> Option<&PerSiteSettings> {
        let domain = extract_domain(url);
        self.settings
            .per_site_settings
            .iter()
            .find(|s| domain.contains(&s.url_pattern) || s.url_pattern.contains(&domain))
    }

    pub fn to_view_model(&self) -> SettingsViewModel {
        let general_section = SettingsSection {
            title: "General".to_string(),
            items: vec![
                SettingsItem {
                    key: "search_engine".to_string(),
                    label: "Default Search Engine".to_string(),
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "current": self.settings.general.default_search_engine.name,
                        "options": ["Google", "Bing", "DuckDuckGo", "Brave", "Ecosia"]
                    }),
                },
                SettingsItem {
                    key: "today_tab_timeout".to_string(),
                    label: "Today Tab Timeout (hours)".to_string(),
                    item_type: SettingsItemType::Number,
                    value: serde_json::json!(self.settings.general.today_tab_timeout_hours),
                },
                SettingsItem {
                    key: "download_path".to_string(),
                    label: "Download Path".to_string(),
                    item_type: SettingsItemType::Text,
                    value: serde_json::json!(self.settings.general.download_path),
                },
                SettingsItem {
                    key: "search_suggestions".to_string(),
                    label: "Search Suggestions".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.privacy.search_suggestions_enabled),
                },
                SettingsItem {
                    key: "popup_blocker".to_string(),
                    label: "Popup Blocker".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.privacy.popup_blocker_enabled),
                },
                SettingsItem {
                    key: "general.archive_timeout".to_string(),
                    label: "Auto-archive inactive tabs".to_string(),
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "current": match self.settings.general.archive_timeout_hours as i64 {
                            12 => "12 hours",
                            24 => "24 hours",
                            168 => "7 days",
                            720 => "30 days",
                            1440 => "60 days",
                            2160 => "90 days",
                            _ if self.settings.general.archive_timeout_hours < 0.0 => "Off",
                            _ => "24 hours",
                        },
                        "options": ["Off", "12 hours", "24 hours", "7 days", "30 days", "60 days", "90 days"]
                    }),
                },
                SettingsItem {
                    key: "general.pinned_close_behavior".to_string(),
                    label: "Pinned Tab Close Behavior".to_string(),
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "current": match self.settings.general.pinned_close_behavior {
                            PinnedCloseBehavior::Switch => "switch",
                            PinnedCloseBehavior::Reset => "reset",
                            PinnedCloseBehavior::ResetSwitch => "reset-switch",
                            PinnedCloseBehavior::UnloadSwitch => "unload-switch",
                            PinnedCloseBehavior::ResetUnloadSwitch => "reset-unload-switch",
                            PinnedCloseBehavior::Close => "close",
                        },
                        "options": ["switch", "reset", "reset-switch", "unload-switch", "reset-unload-switch", "close"]
                    }),
                },
            ],
        };

        let appearance_section = SettingsSection {
            title: "Appearance".to_string(),
            items: vec![
                SettingsItem {
                    key: "theme".to_string(),
                    label: "Theme".to_string(),
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "current": format!("{:?}", self.settings.appearance.theme),
                        "options": ["System", "Light", "Dark"]
                    }),
                },
                SettingsItem {
                    key: "density".to_string(),
                    label: "Density".to_string(),
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "current": format!("{:?}", self.settings.appearance.density),
                        "options": ["Compact", "Default", "Comfortable"]
                    }),
                },
                SettingsItem {
                    key: "sidebar_width".to_string(),
                    label: "Sidebar Width".to_string(),
                    item_type: SettingsItemType::Number,
                    value: serde_json::json!(self.settings.appearance.sidebar_width),
                },
                SettingsItem {
                    key: "show_tab_bar".to_string(),
                    label: "Show Tab Bar".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.appearance.show_tab_bar),
                },
            ],
        };

        let privacy_section = SettingsSection {
            title: "Privacy".to_string(),
            items: vec![
                SettingsItem {
                    key: "do_not_track".to_string(),
                    label: "Do Not Track".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.privacy.do_not_track),
                },
                SettingsItem {
                    key: "block_third_party_cookies".to_string(),
                    label: "Block Third-Party Cookies".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.privacy.block_third_party_cookies),
                },
                SettingsItem {
                    key: "content_blocker".to_string(),
                    label: "Content Blocker".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.privacy.content_blocker_enabled),
                },
                SettingsItem {
                    key: "secure_dns".to_string(),
                    label: "Secure DNS".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.privacy.secure_dns_enabled),
                },
                SettingsItem {
                    key: "secure_dns_provider".to_string(),
                    label: "Secure DNS Provider".to_string(),
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "current": self.settings.privacy.secure_dns_provider,
                        "options": ["Cloudflare", "Google", "Quad9", "Custom"]
                    }),
                },
                SettingsItem {
                    key: "clear_data_on_exit".to_string(),
                    label: "Clear Data on Exit".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.privacy.clear_data_on_exit),
                },
                SettingsItem {
                    key: "safe_browsing".to_string(),
                    label: "Safe Browsing".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.privacy.safe_browsing_enabled),
                },
            ],
        };

        let reader_section = SettingsSection {
            title: "Reader Mode".to_string(),
            items: vec![
                SettingsItem {
                    key: "reader_font".to_string(),
                    label: "Reader Font".to_string(),
                    item_type: SettingsItemType::Text,
                    value: serde_json::json!(self.settings.reader.font_family),
                },
                SettingsItem {
                    key: "reader_font_size".to_string(),
                    label: "Reader Font Size".to_string(),
                    item_type: SettingsItemType::Number,
                    value: serde_json::json!(self.settings.reader.font_size),
                },
                SettingsItem {
                    key: "reader_theme".to_string(),
                    label: "Reader Theme".to_string(),
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "current": format!("{:?}", self.settings.reader.theme),
                        "options": ["Light", "Sepia", "Dark"]
                    }),
                },
            ],
        };

        let shortcuts_section = SettingsSection {
            title: "Keyboard Shortcuts".to_string(),
            items: self
                .settings
                .keyboard_shortcuts
                .iter()
                .map(|s| {
                    let modifiers_str: Vec<&str> = s
                        .modifiers
                        .iter()
                        .map(|m| match m {
                            KeyModifier::Ctrl => "Ctrl",
                            KeyModifier::Alt => "Alt",
                            KeyModifier::Shift => "Shift",
                            KeyModifier::Meta => "Meta",
                        })
                        .collect();
                    let display = if modifiers_str.is_empty() {
                        s.key.clone()
                    } else {
                        format!("{}+{}", modifiers_str.join("+"), s.key)
                    };
                    SettingsItem {
                        key: format!("shortcut_{}", s.action),
                        label: s.action.clone(),
                        item_type: SettingsItemType::KeyCapture,
                        value: serde_json::json!(display),
                    }
                })
                .collect(),
        };

        let per_site_section = SettingsSection {
            title: "Per-site Settings".to_string(),
            items: self
                .settings
                .per_site_settings
                .iter()
                .map(|ps| SettingsItem {
                    key: format!("site_{}", ps.url_pattern),
                    label: ps.url_pattern.clone(),
                    item_type: SettingsItemType::UrlPattern,
                    value: serde_json::json!({
                        "url_pattern": ps.url_pattern,
                        "zoom_level": ps.zoom_level,
                    }),
                })
                .collect(),
        };

        let toolbar_section = SettingsSection {
            title: "Toolbar Items".to_string(),
            items: self
                .settings
                .toolbar_items
                .iter()
                .map(|t| SettingsItem {
                    key: format!("toolbar_{}", t.id),
                    label: t.label.clone(),
                    item_type: SettingsItemType::DragList,
                    value: serde_json::json!({
                        "id": t.id,
                        "kind": format!("{:?}", t.kind),
                        "visible": t.visible,
                    }),
                })
                .collect(),
        };

        let max_section = SettingsSection {
            title: "Max (AI)".to_string(),
            items: vec![
                SettingsItem {
                    key: "max_enabled".to_string(),
                    label: "Enable Max AI".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.max.enabled),
                },
                SettingsItem {
                    key: "max_page_previews".to_string(),
                    label: "Page Previews".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.max.page_previews),
                },
                SettingsItem {
                    key: "max_tidy_tab_titles".to_string(),
                    label: "Tidy Tab Titles".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.max.tidy_tab_titles),
                },
                SettingsItem {
                    key: "max_tidy_downloads".to_string(),
                    label: "Tidy Downloads".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.max.tidy_downloads),
                },
                SettingsItem {
                    key: "max_tidy_tabs".to_string(),
                    label: "Tidy Tabs".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.max.tidy_tabs),
                },
                SettingsItem {
                    key: "max_ai_command_bar".to_string(),
                    label: "AI Command Bar".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.max.ai_command_bar),
                },
                SettingsItem {
                    key: "max_instant_links".to_string(),
                    label: "Instant Links".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.max.instant_links),
                },
            ],
        };

        let advanced_section = SettingsSection {
            title: "Advanced".to_string(),
            items: vec![
                SettingsItem {
                    key: "developer_mode".to_string(),
                    label: "Developer Mode".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.advanced.developer_mode),
                },
                SettingsItem {
                    key: "hardware_acceleration".to_string(),
                    label: "Hardware Acceleration".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.advanced.hardware_acceleration),
                },
                SettingsItem {
                    key: "experimental_features".to_string(),
                    label: "Experimental Features".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.advanced.experimental_features),
                },
            ],
        };

        let notifications_section = SettingsSection {
            title: "Notifications".to_string(),
            items: vec![
                SettingsItem {
                    key: "notifications_enabled".to_string(),
                    label: "Enable Notifications".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.notifications.enabled),
                },
                SettingsItem {
                    key: "calendar_notifications".to_string(),
                    label: "Calendar Notifications".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.notifications.calendar_notifications),
                },
                SettingsItem {
                    key: "update_notifications".to_string(),
                    label: "Update Notifications".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.notifications.update_notifications),
                },
                SettingsItem {
                    key: "notification_sound".to_string(),
                    label: "Notification Sound".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(self.settings.notifications.sound_enabled),
                },
            ],
        };

        let app_icons_section = SettingsSection {
            title: "App Icon".to_string(),
            items: available_app_icons()
                .into_iter()
                .map(|icon| SettingsItem {
                    key: format!("app_icon_{}", icon.id),
                    label: icon.name,
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "id": icon.id,
                        "previewPath": icon.preview_path,
                        "isDefault": icon.is_default,
                    }),
                })
                .collect(),
        };

        SettingsViewModel {
            sections: vec![
                general_section,
                appearance_section,
                privacy_section,
                reader_section,
                shortcuts_section,
                per_site_section,
                toolbar_section,
                max_section,
                advanced_section,
                notifications_section,
                app_icons_section,
            ],
        }
    }
}

impl Default for SettingsManager {
    fn default() -> Self {
        Self::new()
    }
}

fn default_toolbar_items() -> Vec<ToolbarItem> {
    vec![
        ToolbarItem {
            id: "back_forward".to_string(),
            kind: ToolbarItemKind::BackForward,
            visible: true,
            label: "Back/Forward".to_string(),
        },
        ToolbarItem {
            id: "reload".to_string(),
            kind: ToolbarItemKind::Reload,
            visible: true,
            label: "Reload".to_string(),
        },
        ToolbarItem {
            id: "address_bar".to_string(),
            kind: ToolbarItemKind::AddressBar,
            visible: true,
            label: "Address Bar".to_string(),
        },
        ToolbarItem {
            id: "share".to_string(),
            kind: ToolbarItemKind::Share,
            visible: true,
            label: "Share".to_string(),
        },
        ToolbarItem {
            id: "downloads".to_string(),
            kind: ToolbarItemKind::Downloads,
            visible: true,
            label: "Downloads".to_string(),
        },
        ToolbarItem {
            id: "extensions".to_string(),
            kind: ToolbarItemKind::Extensions,
            visible: true,
            label: "Extensions".to_string(),
        },
    ]
}

fn extract_domain(url: &str) -> String {
    let without_scheme = url
        .strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))
        .unwrap_or(url);

    without_scheme
        .split('/')
        .next()
        .unwrap_or(without_scheme)
        .split(':')
        .next()
        .unwrap_or(without_scheme)
        .to_string()
}
