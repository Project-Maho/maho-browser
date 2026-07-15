use maho_types::animation::performance_contract::*;
use maho_types::animation::spring_config::*;
use maho_types::common::*;
use maho_types::easel::*;
use maho_types::events::core_update::*;
use maho_types::events::shell_event::*;
use maho_types::folder::*;
use maho_types::identifiers::*;
use maho_types::profile::*;
use maho_types::settings::*;
use maho_types::space::*;
use maho_types::tab::*;
use maho_types::traits::shell_renderer::SuggestionType;

// === Branded Identifiers ===

#[test]
fn creates_branded_tab_id() {
    let id = TabId::new("abc-123");
    assert_eq!(id.0, "abc-123");
    assert_eq!(id.as_ref(), "abc-123");
}

#[test]
fn creates_all_identifier_types() {
    assert_eq!(TabId::new("t1").0, "t1");
    assert_eq!(SpaceId::new("s1").0, "s1");
    assert_eq!(BoostId::new("b1").0, "b1");
    assert_eq!(NoteId::new("n1").0, "n1");
    assert_eq!(EaselId::new("e1").0, "e1");
    assert_eq!(ExtensionId::new("ext1").0, "ext1");
    assert_eq!(PaneId::new("p1").0, "p1");
    assert_eq!(DownloadId::new("d1").0, "d1");
}

#[test]
fn create_url_creates_branded_url() {
    let url = Url::new("https://example.com");
    assert_eq!(url.0, "https://example.com");
}

#[test]
fn create_date_time_creates_iso_string() {
    let dt = DateTime::now();
    assert!(!dt.0.is_empty());
}

// === Folder and Profile ===

#[test]
fn creates_branded_folder_id() {
    let id = FolderId::new("f1");
    assert_eq!(id.0, "f1");
}

#[test]
fn creates_branded_profile_id() {
    let id = ProfileId::new("p1");
    assert_eq!(id.0, "p1");
}

#[test]
fn folder_struct_is_valid() {
    let folder = Folder {
        id: FolderId::new("f1"),
        name: "Work Tabs".to_string(),
        tab_ids: vec![TabId::new("t1"), TabId::new("t2")],
        is_expanded: true,
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    };
    assert_eq!(folder.name, "Work Tabs");
    assert_eq!(folder.tab_ids.len(), 2);
}

#[test]
fn profile_contains_all_entity_arrays() {
    let profile = Profile {
        id: ProfileId::new("p1"),
        name: "Default".to_string(),
        spaces: vec![],
        boosts: vec![],
        notes: vec![],
        easels: vec![],
        split_view_configs: vec![],
        search_engines: vec![],
        extensions: vec![],
        settings: Settings {
            appearance: AppearanceSettings {
                theme: Theme::System,
                accent_color: None,
                density: Density::Comfortable,
                sidebar_width: 280.0,
                show_tab_bar: true,
                window_transparency: false,
                sidebar_collapsed: false,
                custom_icon_path: None,
            },
            general: GeneralSettings {
                default_search_engine: SearchEngine {
                    name: "Google".to_string(),
                    url_template: "https://google.com/search?q=%s".to_string(),
                    is_default: true,
                },
                today_tab_timeout_hours: 12.0,
                restore_on_launch: RestorePolicy::RestoreAll,
                download_path: "/downloads".to_string(),
                autoplay_policy: AutoplayPolicy::BlockAudio,
                archive_timeout_hours: 24.0,
                site_search_entries: vec![],
                pinned_close_behavior: PinnedCloseBehavior::Switch,
                auto_delete_empty_folders_on_tidy: true,
            },
            privacy: PrivacySettings {
                do_not_track: true,
                block_third_party_cookies: false,
                content_blocker_enabled: true,
                popup_blocker_enabled: true,
                search_suggestions_enabled: false,
                secure_dns_enabled: false,
                secure_dns_provider: String::new(),
                secure_dns_custom_url: None,
                clear_data_on_exit: false,
                safe_browsing_enabled: true,
            },
            reader: ReaderSettings {
                font_family: "Georgia".to_string(),
                font_size: 18.0,
                theme: ReaderTheme::Light,
            },
            keyboard_shortcuts: vec![],
            per_site_settings: vec![],
            toolbar_items: vec![],
            max: Default::default(),
            autofill: Default::default(),
            advanced: Default::default(),
            notifications: Default::default(),
        },
    };
    assert_eq!(profile.name, "Default");
    assert_eq!(profile.spaces.len(), 0);
}

// === Spring Animation Config ===

#[test]
fn spring_snappy_has_correct_values() {
    assert_eq!(SPRING_SNAPPY.damping_ratio, 0.85);
    assert_eq!(SPRING_SNAPPY.stiffness, 300.0);
    assert_eq!(SPRING_SNAPPY.mass, 1.0);
    assert_eq!(SPRING_SNAPPY.initial_velocity, 0.0);
}

#[test]
fn spring_smooth_has_correct_values() {
    assert_eq!(SPRING_SMOOTH.damping_ratio, 0.9);
    assert_eq!(SPRING_SMOOTH.stiffness, 200.0);
}

#[test]
fn spring_bouncy_has_correct_values() {
    assert_eq!(SPRING_BOUNCY.damping_ratio, 0.6);
    assert_eq!(SPRING_BOUNCY.stiffness, 250.0);
}

#[test]
fn spring_presets_contains_all_3() {
    let snappy = SpringPresetName::Snappy.config();
    let smooth = SpringPresetName::Smooth.config();
    let bouncy = SpringPresetName::Bouncy.config();
    assert_eq!(snappy.damping_ratio, SPRING_SNAPPY.damping_ratio);
    assert_eq!(smooth.damping_ratio, SPRING_SMOOTH.damping_ratio);
    assert_eq!(bouncy.damping_ratio, SPRING_BOUNCY.damping_ratio);
}

// === Performance Contract ===

#[test]
fn has_11_metrics() {
    assert_eq!(PERFORMANCE_CONTRACT.len(), 11);
}

#[test]
fn tab_switch_active_to_active_target() {
    let metric = PERFORMANCE_CONTRACT
        .iter()
        .find(|m| m.name == "tab_switch_active_to_active")
        .unwrap();
    assert_eq!(metric.target, 16);
    assert_eq!(metric.unit, "ms");
    assert_eq!(metric.percentile, "p99");
    assert_eq!(metric.owner, "engine");
}

#[test]
fn cold_launch_target() {
    let metric = PERFORMANCE_CONTRACT
        .iter()
        .find(|m| m.name == "cold_launch_to_interactive")
        .unwrap();
    assert_eq!(metric.target, 800);
    assert_eq!(metric.owner, "both");
}

#[test]
fn memory_targets_match_contract() {
    assert_eq!(MEMORY_TARGETS.webkit_per_tab_bytes, 50 * 1024 * 1024);
    assert_eq!(MEMORY_TARGETS.chromium_per_tab_bytes, 80 * 1024 * 1024);
    assert_eq!(MEMORY_TARGETS.suspended_per_tab_bytes, 0);
    assert_eq!(MEMORY_TARGETS.archived_per_tab_bytes, 500);
}

#[test]
fn tab_lifecycle_timeouts_match_contract() {
    assert_eq!(TAB_LIFECYCLE_TIMEOUTS.active_to_frozen_minutes, 5);
    assert_eq!(TAB_LIFECYCLE_TIMEOUTS.frozen_to_suspended_minutes, 15);
    assert_eq!(TAB_LIFECYCLE_TIMEOUTS.suspended_to_archived_hours, 24);
}

#[test]
fn tab_restore_targets_match_contract() {
    assert_eq!(TAB_RESTORE_TARGETS.frozen_to_active.target_ms, 16);
    assert_eq!(TAB_RESTORE_TARGETS.suspended_to_active.target_ms, 300);
}

// === ShellEvent Type Guards ===

#[test]
fn is_navigation_event() {
    let nav = ShellEvent::GoBack {
        tab_id: TabId::new("t1"),
    };
    assert!(nav.is_navigation_event());
    let close = ShellEvent::CloseTab {
        tab_id: TabId::new("t1"),
        expected_space_id: None,
    };
    assert!(!close.is_navigation_event());
}

#[test]
fn is_tab_management_event() {
    let close = ShellEvent::CloseTab {
        tab_id: TabId::new("t1"),
        expected_space_id: None,
    };
    assert!(close.is_tab_management_event());
    let reopen = ShellEvent::ReopenLastClosed;
    assert!(reopen.is_tab_management_event());
    let launch = ShellEvent::AppLaunched;
    assert!(!launch.is_tab_management_event());
}

#[test]
fn is_space_management_event() {
    let evt = ShellEvent::CreateSpace {
        name: "Work".to_string(),
        color: SpaceColor {
                hue: 200.0,
                saturation: 0.8,
                brightness: 0.9,
                grain: 0.0,
            },
        profile_id: maho_types::identifiers::ProfileId::new(""),
    };
    assert!(evt.is_space_management_event());
}

#[test]
fn is_command_bar_event() {
    assert!(ShellEvent::CommandBarOpened.is_command_bar_event());
    let query = ShellEvent::CommandBarQuery {
        text: "hello".to_string(),
        mode: None,
        is_incognito: false,
    };
    assert!(query.is_command_bar_event());
    assert!(!ShellEvent::AppLaunched.is_command_bar_event());
}

#[test]
fn is_lifecycle_event() {
    assert!(ShellEvent::AppLaunched.is_lifecycle_event());
    assert!(ShellEvent::AppWillTerminate.is_lifecycle_event());
    let mem = ShellEvent::MemoryWarning {
        level: MemoryPressureLevel::Critical,
    };
    assert!(mem.is_lifecycle_event());
    let close = ShellEvent::CloseTab {
        tab_id: TabId::new("t1"),
        expected_space_id: None,
    };
    assert!(!close.is_lifecycle_event());
}

// === CoreUpdate Type Guards ===

#[test]
fn is_tab_update() {
    use maho_types::traits::shell_renderer::TabViewModel;
    let tab_created = CoreUpdate::TabCreated {
        tab: TabViewModel {
            id: TabId::new("t1"),
            space_id: SpaceId::new("s1"),
            title: "Test".to_string(),
            custom_title: None,
            url: "https://x.com".to_string(),
            favicon: None,
            is_loading: false,
            is_pinned: false,
            is_favorite: false,
            is_muted: false,
            is_playing_audio: false,
            lifecycle_state: "active".to_string(),
            children: vec![],
            created_at: DateTime::now(),
            last_active_at: DateTime::now(),
            favorite_order: None,
            role: maho_types::tab::TabRole::Normal,
            is_private: false,
        },
    };
    assert!(tab_created.is_tab_update());
    let tab_closed = CoreUpdate::TabClosed {
        tab_id: TabId::new("t1"),
        animated: true,
    };
    assert!(tab_closed.is_tab_update());
    let error = CoreUpdate::Error {
        context: "test".to_string(),
        error: MahoError {
            code: "E1".to_string(),
            message: "err".to_string(),
            details: None,
        },
    };
    assert!(!error.is_tab_update());
}

#[test]
fn is_space_update() {
    use maho_types::traits::shell_renderer::SpaceViewModel;
    let space_created = CoreUpdate::SpaceCreated {
        space: SpaceViewModel {
            id: SpaceId::new("s1"),
            name: "Work".to_string(),
            color: SpaceColor {
                hue: 200.0,
                saturation: 0.8,
                brightness: 0.9,
                grain: 0.0,
            },
            tab_count: 0,
            is_active: false,
            icon: None,
            profile_id: Some(ProfileId::new("p1")),
            profile_name: None,
            order_index: Some(0),
            theme: None,
        },
    };
    assert!(space_created.is_space_update());
    let active = CoreUpdate::ActiveSpaceChanged {
        space_id: SpaceId::new("s1"),
        active_tab_id: None,
    };
    assert!(active.is_space_update());
}

#[test]
fn is_download_update() {
    use maho_types::traits::shell_renderer::{DownloadState, DownloadViewModel};
    let started = CoreUpdate::DownloadStarted {
        download: DownloadViewModel {
            id: "d1".to_string(),
            filename: "test.pdf".to_string(),
            url: "https://x.com/test.pdf".to_string(),
            total_bytes: 1000,
            received_bytes: 0,
            state: DownloadState::Downloading,
            file_path: None,
            mime_type: None,
            error: None,
            started_at: "2024-01-01T00:00:00Z".to_string(),
            completed_at: None,
            original_filename: None,
        },
    };
    assert!(started.is_download_update());
    let completed = CoreUpdate::DownloadCompleted {
        download_id: DownloadId::new("d1"),
    };
    assert!(completed.is_download_update());
}

// === Discriminated Union Completeness ===

#[test]
fn tab_lifecycle_state_covers_all_4() {
    let states: Vec<TabLifecycleState> = vec![
        TabLifecycleState::Active,
        TabLifecycleState::Frozen,
        TabLifecycleState::Suspended {
            snapshot: TabSnapshot {
                url: "https://x.com".to_string(),
                title: "X".to_string(),
                scroll_position: ScrollPosition::default(),
                interaction_state: vec![],
                captured_at: DateTime::now(),
            },
        },
        TabLifecycleState::Archived {
            metadata_only: true,
        },
    ];
    assert_eq!(states.len(), 4);
}

#[test]
fn canvas_item_covers_all_4() {
    let rect = Rect {
        origin: Point { x: 0.0, y: 0.0 },
        size: Size {
            width: 100.0,
            height: 100.0,
        },
    };
    let items: Vec<CanvasItem> = vec![
        CanvasItem::WebEmbed {
            url: Url::new("https://x.com"),
            rect: rect.clone(),
        },
        CanvasItem::Note {
            content: "hello".to_string(),
            rect: rect.clone(),
        },
        CanvasItem::Drawing {
            paths: vec![],
            rect: rect.clone(),
        },
        CanvasItem::Image {
            data: ImageData {
                data: vec![],
                width: 0,
                height: 0,
                format: ImageFormat::Png,
            },
            rect,
        },
    ];
    assert_eq!(items.len(), 4);
}

#[test]
fn sync_status_covers_all_5() {
    let statuses: Vec<SyncStatus> = vec![
        SyncStatus::Idle,
        SyncStatus::Syncing { progress: 0.5 },
        SyncStatus::Synced {
            last_sync_at: "2026-01-01".to_string(),
        },
        SyncStatus::Error {
            message: "fail".to_string(),
        },
        SyncStatus::Offline,
    ];
    assert_eq!(statuses.len(), 5);
}

#[test]
fn atc_condition_covers_all_3() {
    use maho_types::space::ATCCondition;
    let conditions: Vec<ATCCondition> = vec![
        ATCCondition::InactiveDuration { hours: 24.0 },
        ATCCondition::TabCountExceeded { max_tabs: 50 },
        ATCCondition::UrlPattern {
            pattern: "*.example.com".to_string(),
        },
    ];
    assert_eq!(conditions.len(), 3);
}

#[test]
fn quick_action_covers_all_6() {
    let actions: Vec<QuickAction> = vec![
        QuickAction::NewTab,
        QuickAction::NewSpace,
        QuickAction::ClearHistory,
        QuickAction::OpenSettings,
        QuickAction::ToggleBoost,
        QuickAction::Custom {
            action_id: "my-action".to_string(),
        },
    ];
    assert_eq!(actions.len(), 6);
}

#[test]
fn memory_action_covers_all_4() {
    let actions: Vec<MemoryAction> = vec![
        MemoryAction::FrozeTabs { count: 5 },
        MemoryAction::SuspendedTabs { count: 3 },
        MemoryAction::KilledTabs { count: 1 },
        MemoryAction::ReleasedWebviews { count: 10 },
    ];
    assert_eq!(actions.len(), 4);
}

// === PinnedCloseBehavior serde tests ===

#[test]
fn pinned_close_behavior_defaults_to_switch_when_missing() {
    let json = r#"{
        "defaultSearchEngine": {"name":"G","urlTemplate":"https://g.com/search?q=%s","isDefault":true},
        "todayTabTimeoutHours": 12.0,
        "restoreOnLaunch": "restore_all",
        "downloadPath": "/tmp",
        "autoplayPolicy": "block_audio",
        "archiveTimeoutHours": 24.0,
        "siteSearchEntries": []
    }"#;
    let settings: GeneralSettings = serde_json::from_str(json).unwrap();
    assert!(matches!(settings.pinned_close_behavior, PinnedCloseBehavior::Switch));
}

#[test]
fn pinned_close_behavior_round_trips_reset_unload_switch() {
    let original = GeneralSettings {
        default_search_engine: SearchEngine {
            name: "G".to_string(),
            url_template: "https://g.com/search?q=%s".to_string(),
            is_default: true,
        },
        today_tab_timeout_hours: 12.0,
        restore_on_launch: RestorePolicy::RestoreAll,
        download_path: "/tmp".to_string(),
        autoplay_policy: AutoplayPolicy::BlockAudio,
        archive_timeout_hours: 24.0,
        site_search_entries: vec![],
        pinned_close_behavior: PinnedCloseBehavior::ResetUnloadSwitch,
        auto_delete_empty_folders_on_tidy: true,
    };
    let serialized = serde_json::to_string(&original).unwrap();
    assert!(serialized.contains("reset-unload-switch"));
    let deserialized: GeneralSettings = serde_json::from_str(&serialized).unwrap();
    assert!(matches!(deserialized.pinned_close_behavior, PinnedCloseBehavior::ResetUnloadSwitch));
}

// === SuggestionType wire-format contract (Rust <-> Swift) ===

#[test]
fn suggestion_type_ai_answer_serializes_camelcase_for_swift_contract() {
    // The enum carries #[serde(rename_all = "lowercase")], which would emit
    // "aianswer" by default. Swift's `SuggestionType.aiAnswer` (String raw value)
    // decodes "aiAnswer", so the explicit #[serde(rename = "aiAnswer")] override is
    // load-bearing: without it the AI-search suggestion row silently fails to decode
    // on iOS and never renders. Lock the exact wire string in both directions.
    let serialized = serde_json::to_string(&SuggestionType::AiAnswer).unwrap();
    assert_eq!(serialized, "\"aiAnswer\"");

    let deserialized: SuggestionType = serde_json::from_str("\"aiAnswer\"").unwrap();
    assert!(matches!(deserialized, SuggestionType::AiAnswer));
}
