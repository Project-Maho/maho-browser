use std::cell::{Cell, RefCell};
use std::collections::{HashMap, HashSet};
use std::rc::Rc;
use std::time::{Duration, Instant};

const LMDB_DIRTY_TABS: u8 = 0b000001;
const LMDB_DIRTY_SPACES: u8 = 0b000010;
const LMDB_DIRTY_ACTIVE_SPACE: u8 = 0b000100;
const LMDB_DIRTY_PROFILES: u8 = 0b001000;
const LMDB_DIRTY_ACTIVE_PROFILE: u8 = 0b010000;
const LMDB_DIRTY_ACCOUNT: u8 = 0b100000;
const LMDB_DIRTY_SPLIT_VIEW: u8 = 0b1000000;
const LMDB_DIRTY_ALL: u8 = 0b1111111;

const PERSIST_THROTTLE_MS: u64 = 50;

struct PersistThrottle {
    pending_bits: u8,
    last_flush: Instant,
}

impl PersistThrottle {
    fn new() -> Self {
        Self {
            pending_bits: 0,
            last_flush: Instant::now() - Duration::from_secs(3600),
        }
    }
}

use maho_types::account::{AccountInfo, AuthState};
use maho_types::ai::{AiProfile, AiWorkspace};
use maho_types::boost::{Boost, BoostUpdate};
use maho_types::common::{DateTime, MemoryPressureLevel, Url};
use maho_types::events::core_update::{CoreUpdate, MemoryAction, SPLIT_VIEW_SCHEMA_VERSION};
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{BoostId, FolderId, NoteId, ProfileId, SpaceId, TabId, WindowId};
use maho_types::keyboard::{KeyCombo, KeyModifier};
use maho_types::note::Note;
use maho_types::settings::{
    AppearanceSettingsUpdate, PinnedCloseBehavior, Settings, SettingsUpdate,
};
use maho_types::space::{RootItem, Space, SpaceColor};
use maho_types::split_view::{SplitConfig, SplitViewConfig};
use maho_types::tab::{Tab, TabLifecycleState, TabRole};
use maho_types::traits::shell_renderer::{
    ArchivedTabViewModel, DownloadState, SearchContext, SpaceViewModel, SuggestionViewModel, TabViewModel,
};

use crate::account_manager::AccountManager;
use crate::atc_manager::ATCManager;
use crate::autofill_manager::AutofillManager;
use crate::backup_manager::BackupManager;
use crate::bookmark_manager::BookmarkManager;
use crate::boost_manager::BoostManager;
use crate::command_bar::CommandBarEngine;
use crate::content_blocker::ContentBlocker;
use crate::crash_recovery::CrashRecoveryManager;
use crate::css_mod_manager::CssModManager;
use crate::download_manager::DownloadManager;
use crate::easel_manager::EaselManager;
use crate::event_dispatcher::{
    CommandBarInterface, EventDispatcher, SpaceManagerInterface, TabManagerInterface,
};
use crate::extension_bridge::ExtensionBridge;
use crate::find_manager::FindManager;
use crate::import_export_manager::ImportExportManager;
use crate::llm_manager::LLMManager;
use crate::memory_manager::{AuthSnapshot, MemoryManager, MemoryResult};
use crate::note_manager::NoteManager;
use crate::notification_manager::NotificationManager;
use crate::password_manager::PasswordManager;
use crate::permission_manager::PermissionManager;
use crate::profile_manager::ProfileManager;
use crate::reading_list_manager::ReadingListManager;
use crate::settings_manager::SettingsManager;
use crate::sharing_manager::SharingManager;
use crate::shortcut_manager::ShortcutManager;
use crate::skills_manager::SkillsManager;
use crate::space_manager::SpaceManager;
use crate::sync_models::{SyncEntity, SyncEntityType};
use crate::tab_lifecycle::{timestamp_millis_to_iso8601, TabLifecycleManager};
use crate::tool_registry::ToolRegistry;

fn private_tab_ids_from_tabs<'a, I>(tabs: I) -> HashSet<TabId>
where
    I: IntoIterator<Item = &'a Tab>,
{
    tabs.into_iter()
        .filter(|tab| tab.is_private)
        .map(|tab| tab.id.clone())
        .collect()
}

fn sanitize_space_for_private_tabs(space: &Space, private_ids: &HashSet<TabId>) -> Space {
    if private_ids.is_empty() {
        return space.clone();
    }

    let mut sanitized = space.clone();
    sanitized
        .tab_order
        .retain(|tab_id| !private_ids.contains(tab_id));
    for folder in &mut sanitized.folders {
        folder
            .tab_ids
            .retain(|tab_id| !private_ids.contains(tab_id));
    }
    sanitized.root_order.retain(|item| match item {
        RootItem::Tab(tab_id) => !private_ids.contains(tab_id),
        RootItem::Folder(_) => true,
    });
    if sanitized
        .last_active_tab_id
        .as_ref()
        .is_some_and(|tab_id| private_ids.contains(tab_id))
    {
        sanitized.last_active_tab_id = None;
    }
    sanitized
}

fn trusted_origin_from_url(url: &str) -> Option<String> {
    let trimmed = url.trim();
    let (scheme, rest) = trimmed.split_once("://")?;
    if !is_valid_origin_scheme(scheme) || rest.is_empty() {
        return None;
    }

    let authority_end = rest.find(['/', '?', '#']).unwrap_or(rest.len());
    let authority = &rest[..authority_end];
    if authority.is_empty() || authority.chars().any(char::is_whitespace) {
        return None;
    }

    let host_port = authority
        .rsplit_once('@')
        .map_or(authority, |(_, host)| host);
    if host_port.is_empty() {
        return None;
    }

    Some(format!(
        "{}://{}",
        scheme.to_ascii_lowercase(),
        host_port.to_ascii_lowercase()
    ))
}

fn is_valid_origin_scheme(scheme: &str) -> bool {
    let mut chars = scheme.chars();
    let Some(first) = chars.next() else {
        return false;
    };
    first.is_ascii_alphabetic()
        && chars.all(|ch| ch.is_ascii_alphanumeric() || matches!(ch, '+' | '-' | '.'))
}

fn default_agent_workspace(workspace_root: &str) -> AiWorkspace {
    AiWorkspace {
        id: "default".to_string(),
        name: "Default Workspace".to_string(),
        profile_id: Some("default".to_string()),
        space_id: None,
        workspace_root: Some(workspace_root.to_string()),
        created_at: String::new(),
        updated_at: String::new(),
    }
}

fn default_agent_profile() -> AiProfile {
    AiProfile {
        id: "default".to_string(),
        name: "Default Profile".to_string(),
        system_prompt: String::new(),
        preferred_model: None,
        tools: vec![],
        mcp_servers: vec![],
        is_default: true,
        created_at: String::new(),
        updated_at: String::new(),
    }
}

fn resolve_agent_workspace(
    storage: &maho_storage::sqlite::SqliteStorage,
    workspace_root: &str,
    active_space_id: Option<&str>,
) -> AiWorkspace {
    let workspaces = storage.list_workspaces().unwrap_or_default();
    if let Some(space_id) = active_space_id {
        if let Some(workspace) = workspaces
            .iter()
            .find(|workspace| workspace.space_id.as_deref() == Some(space_id))
        {
            return workspace.clone();
        }
    }

    workspaces
        .into_iter()
        .find(|workspace| workspace.workspace_root.as_deref() == Some(workspace_root))
        .unwrap_or_else(|| default_agent_workspace(workspace_root))
}

fn resolve_agent_profile(
    storage: &maho_storage::sqlite::SqliteStorage,
    workspace: &AiWorkspace,
) -> AiProfile {
    workspace
        .profile_id
        .as_deref()
        .and_then(|profile_id| storage.get_ai_profile(profile_id).ok().flatten())
        .or_else(|| storage.get_ai_profile("blank").ok().flatten())
        .unwrap_or_else(default_agent_profile)
}

fn sanitize_spaces_for_private_tabs<'a, I>(spaces: I, private_ids: &HashSet<TabId>) -> Vec<Space>
where
    I: IntoIterator<Item = &'a Space>,
{
    spaces
        .into_iter()
        .map(|space| sanitize_space_for_private_tabs(space, private_ids))
        .collect()
}

// One-time cleanup (LMDB schema v2 -> v3). Pre-fix, closing a Favorite/Pinned
// tab's WebContents was suppressed and never removed the core tab, so add+close
// cycles accumulated duplicate ghost entries. For each (space, role class, url)
// group with more than one tab, keep the most-recently-active and drop the rest
// from the tab list and every space ordering (tab_order, root_order, folders).
fn dedup_duplicate_pinned_favorites(
    tabs: Vec<Tab>,
    mut spaces: Vec<Space>,
) -> (Vec<Tab>, Vec<Space>, usize) {
    let mut groups: std::collections::HashMap<(SpaceId, u8, String), Vec<usize>> =
        std::collections::HashMap::new();
    for (idx, tab) in tabs.iter().enumerate() {
        let role_class = match tab.role {
            TabRole::Pinned => 1u8,
            TabRole::Favorite { .. } => 2u8,
            _ => continue,
        };
        groups
            .entry((tab.space_id.clone(), role_class, tab.url.0.clone()))
            .or_default()
            .push(idx);
    }

    let mut remove_ids: HashSet<TabId> = HashSet::new();
    for idxs in groups.values() {
        if idxs.len() <= 1 {
            continue;
        }
        let keep = *idxs
            .iter()
            .max_by(|&&a, &&b| tabs[a].last_active_at.0.cmp(&tabs[b].last_active_at.0))
            .expect("group is non-empty");
        for &i in idxs {
            if i != keep {
                remove_ids.insert(tabs[i].id.clone());
            }
        }
    }

    if remove_ids.is_empty() {
        return (tabs, spaces, 0);
    }

    let removed = remove_ids.len();
    let kept: Vec<Tab> = tabs
        .into_iter()
        .filter(|tab| !remove_ids.contains(&tab.id))
        .collect();
    for space in &mut spaces {
        space
            .tab_order
            .retain(|tab_id| !remove_ids.contains(tab_id));
        space.root_order.retain(|item| match item {
            RootItem::Tab(tab_id) => !remove_ids.contains(tab_id),
            RootItem::Folder(_) => true,
        });
        for folder in &mut space.folders {
            folder.tab_ids.retain(|tab_id| !remove_ids.contains(tab_id));
        }
        if space
            .last_active_tab_id
            .as_ref()
            .is_some_and(|tab_id| remove_ids.contains(tab_id))
        {
            space.last_active_tab_id = None;
        }
    }
    (kept, spaces, removed)
}
use crate::tab_preview_manager::TabPreviewManager;

type UpdateListeners = Rc<RefCell<Vec<Box<dyn Fn(CoreUpdate)>>>>;

/// Errors that can occur during state persistence operations
#[derive(Debug)]
pub enum PersistenceError {
    Serialization(String),
    Storage(String),
}

#[cfg(test)]
#[allow(clippy::items_after_test_module)]
mod tests {
    use super::*;

    fn create_tab(core: &mut MahoCore) -> TabId {
        let space_id = core.get_active_space_id().clone();
        let before: Vec<TabId> = core
            .get_tab_view_models()
            .iter()
            .map(|t| t.id.clone())
            .collect();
        core.handle_event(ShellEvent::CreateTab {
            space_id,
            url: None,
            parent_id: None,
            tab_id: None,
            window_id: None,
            is_private: false,
        });
        core.get_tab_view_models()
            .iter()
            .find(|t| !before.contains(&t.id))
            .expect("New tab should appear in view models")
            .id
            .clone()
    }

    fn write_agent_skill_config(skills: serde_json::Value) -> tempfile::NamedTempFile {
        use std::io::Write as _;

        let mut file = tempfile::Builder::new()
            .suffix(".json")
            .tempfile()
            .expect("skill config tempfile must open");
        let config = serde_json::json!({
            "version": 1,
            "min_brain_version": 1,
            "skills": skills,
        });
        write!(file, "{}", config).expect("skill config tempfile must be written");
        file
    }

    #[test]
    fn agent_slash_tools_resolve_from_shared_config_skills() {
        let mut core = MahoCore::new();
        let config = write_agent_skill_config(serde_json::json!([
            {
                "id": "hot-config-skill",
                "name": "Hot Config Skill",
                "slash_command": "/hot-config",
                "description": "Loaded through maho_core_apply_config/shared manager",
                "icon": "sparkles",
                "first_party": false,
                "allowed_tools": ["browser_tab_get", "browser_page_read"],
                "system_prompt": "Use hot-delivered skill instructions."
            }
        ]));
        core.load_skills_from_dir(config.path())
            .expect("shared skills config should load");

        let tools = core
            .resolve_agent_slash_allowed_tools("/hot-config")
            .expect("slash command should resolve from shared manager");

        assert_eq!(
            tools,
            vec![
                "browser_tab_get".to_string(),
                "browser_page_read".to_string(),
            ]
        );
        assert!(core
            .resolve_agent_slash_allowed_tools("/disk-only-missing")
            .is_none());
    }

    #[test]
    fn agent_system_prompt_appends_site_guidance_for_trusted_origin() {
        let mut core = MahoCore::new();
        let config = write_agent_skill_config(serde_json::json!([
            {
                "id": "notion-advisor",
                "name": "Notion Advisor",
                "slash_command": "/notion-advisor",
                "description": "Advises on Notion pages",
                "icon": "lightbulb",
                "first_party": false,
                "auto_inject": true,
                "url_patterns": ["https://team.notion.so/*"],
                "allowed_tools": ["browser_privileged_tool"],
                "system_prompt": "Use Notion-specific project guidance."
            }
        ]));
        core.load_skills_from_dir(config.path())
            .expect("shared skills config should load");

        let prompt = core
            .compose_agent_system_prompt_with_skill_context(
                "Profile prompt.",
                Some("https://team.notion.so"),
                2_000,
            )
            .expect("trusted origin should produce injected guidance");

        assert!(prompt.starts_with("Profile prompt.\n\n<maho_site_guidance"));
        assert!(prompt.contains("Advisory only"));
        assert!(prompt.contains("Use Notion-specific project guidance."));
        assert!(!prompt.contains("browser_privileged_tool"));
    }

    #[test]
    fn agent_site_guidance_rejects_page_content_as_origin() {
        let mut core = MahoCore::new();
        let config = write_agent_skill_config(serde_json::json!([
            {
                "id": "notion-advisor",
                "name": "Notion Advisor",
                "slash_command": "/notion-advisor",
                "description": "Advises on Notion pages",
                "icon": "lightbulb",
                "first_party": false,
                "auto_inject": true,
                "url_patterns": ["https://team.notion.so/*"],
                "system_prompt": "Use Notion-specific project guidance."
            }
        ]));
        core.load_skills_from_dir(config.path())
            .expect("shared skills config should load");

        let page_body = "This page body mentions https://team.notion.so but is not a browser-trusted active-tab origin.";

        assert!(core
            .compose_agent_system_prompt_with_skill_context(
                "Profile prompt.",
                Some(page_body),
                2_000
            )
            .is_none());
    }

    #[test]
    fn trusted_active_tab_origin_uses_tab_url_origin_only() {
        let mut core = MahoCore::new();
        let tab_id = create_tab(&mut core);

        core.handle_event(ShellEvent::TabUrlUpdated {
            tab_id,
            url: Url::new("https://Docs.Example.com:8443/path?q=1#body"),
        });

        assert_eq!(
            core.trusted_active_tab_origin().as_deref(),
            Some("https://docs.example.com:8443")
        );
    }

    #[test]
    fn get_favorite_tabs_returns_unique_tab_ids() {
        let mut core = MahoCore::new();
        let tab_id = create_tab(&mut core);
        let active_space_id = core.get_active_space_id().clone();
        let profile_id = core
            .get_space_view_models()
            .into_iter()
            .find(|space| space.id == active_space_id)
            .and_then(|space| space.profile_id)
            .expect("active space profile should exist");
        let other_space = core.create_space(
            "Dup Space",
            SpaceColor {
                hue: 210.0,
                saturation: 0.6,
                brightness: 0.8,
                grain: 0.0,
            },
            profile_id,
        );

        core.favorite_tab(&tab_id);
        core.space_manager
            .add_tab_to_space(&active_space_id, tab_id.clone(), None);
        core.space_manager
            .add_tab_to_space(&other_space.id, tab_id.clone(), None);

        let favorites = core.get_favorite_tabs(&active_space_id);
        let matching: Vec<_> = favorites.iter().filter(|tab| tab.id == tab_id).collect();
        assert_eq!(matching.len(), 1);
    }

    #[test]
    fn reorder_favorite_persists_new_order() {
        let mut core = MahoCore::new();
        let active_space_id = core.get_active_space_id().clone();

        let tab_a = create_tab(&mut core);
        let tab_b = create_tab(&mut core);
        let tab_c = create_tab(&mut core);
        core.favorite_tab(&tab_a);
        core.favorite_tab(&tab_b);
        core.favorite_tab(&tab_c);

        let favs = core.get_favorite_tabs(&active_space_id);
        let initial_ids: Vec<TabId> = favs.iter().map(|f| f.id.clone()).collect();
        let idx_a = initial_ids.iter().position(|id| *id == tab_a).unwrap();
        let idx_b = initial_ids.iter().position(|id| *id == tab_b).unwrap();
        let idx_c = initial_ids.iter().position(|id| *id == tab_c).unwrap();
        assert!(idx_a < idx_b && idx_b < idx_c, "initial order: A < B < C");

        // Move A to position idx_b (after removing A, A goes to where B was)
        core.handle_event(ShellEvent::ReorderFavorite {
            tab_id: tab_a.clone(),
            new_index: idx_b,
        });

        let favs = core.get_favorite_tabs(&active_space_id);
        let reordered_ids: Vec<TabId> = favs.iter().map(|f| f.id.clone()).collect();
        let new_idx_a = reordered_ids.iter().position(|id| *id == tab_a).unwrap();
        let new_idx_b = reordered_ids.iter().position(|id| *id == tab_b).unwrap();
        assert!(
            new_idx_b < new_idx_a,
            "After reorder, B({new_idx_b}) should precede A({new_idx_a})"
        );
    }

    #[test]
    fn reorder_favorite_via_json_roundtrip() {
        // Simulate the exact JSON that C++ DispatchShellEventEx produces
        let json = r#"{"kind":"reorder_favorite","tab_id":"test-tab-id","new_index":1}"#;
        let event: Result<ShellEvent, _> = serde_json::from_str(json);
        assert!(
            event.is_ok(),
            "JSON deserialization should succeed, got: {:?}",
            event.err()
        );
        match event.unwrap() {
            ShellEvent::ReorderFavorite { tab_id, new_index } => {
                assert_eq!(tab_id.0, "test-tab-id");
                assert_eq!(new_index, 1);
            }
            other => panic!("Expected ReorderFavorite, got {:?}", other),
        }
    }

    #[test]
    fn reorder_favorite_move_to_end() {
        let mut core = MahoCore::new();
        let active_space_id = core.get_active_space_id().clone();

        let tab_a = create_tab(&mut core);
        let tab_b = create_tab(&mut core);
        let tab_c = create_tab(&mut core);
        core.favorite_tab(&tab_a);
        core.favorite_tab(&tab_b);
        core.favorite_tab(&tab_c);

        let total_favs = core.get_favorite_tabs(&active_space_id).len();

        // Move A to the very end
        core.handle_event(ShellEvent::ReorderFavorite {
            tab_id: tab_a.clone(),
            new_index: total_favs - 1,
        });

        let favs = core.get_favorite_tabs(&active_space_id);
        let reordered_ids: Vec<TabId> = favs.iter().map(|f| f.id.clone()).collect();
        assert_eq!(
            reordered_ids.last().unwrap(),
            &tab_a,
            "A should be last after move-to-end"
        );
    }

    #[test]
    fn favorite_tab_removes_from_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        assert!(
            core.space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id.clone())),
            "tab should be in root_order before favoriting"
        );

        core.favorite_tab(&tab_id);

        assert!(
            !core
                .space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "tab must not be in root_order after favoriting"
        );
    }

    #[test]
    fn unfavorite_tab_restores_to_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.favorite_tab(&tab_id);
        assert!(
            !core
                .space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id.clone())),
            "tab must not be in root_order after favoriting"
        );

        core.transition_tab_role(&tab_id, TabRole::Normal);

        assert!(
            core.space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "tab must be back in root_order after unfavoriting"
        );
    }

    #[test]
    fn favorite_unfavorite_roundtrip_root_order_contains_no_duplicates() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.favorite_tab(&tab_id);
        core.transition_tab_role(&tab_id, TabRole::Normal);

        let order = core.space_manager.get_root_order(&space_id);
        let count = order
            .iter()
            .filter(|i| *i == &maho_types::space::RootItem::Tab(tab_id.clone()))
            .count();
        assert_eq!(
            count, 1,
            "tab should appear exactly once in root_order after roundtrip"
        );
    }

    #[test]
    fn handle_event_favorite_tab_removes_from_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.handle_event(ShellEvent::FavoriteTab {
            tab_id: tab_id.clone(),
        });

        assert!(
            !core
                .space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "tab must not be in root_order after FavoriteTab event"
        );
    }

    #[test]
    fn change_tab_role_to_normal_restores_to_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.handle_event(ShellEvent::FavoriteTab {
            tab_id: tab_id.clone(),
        });
        core.handle_event(ShellEvent::ChangeTabRole {
            tab_id: tab_id.clone(),
            new_role: TabRole::Normal,
        });

        assert!(
            core.space_manager.get_root_order(&space_id).contains(&maho_types::space::RootItem::Tab(tab_id)),
            "tab must be back in root_order after ChangeTabRole to Normal (was: UnfavoriteTab, replaced by ADR 11 role transition contract)"
        );
    }

    #[test]
    fn delete_space_migrated_favorited_tab_absent_from_destination_root_order() {
        let mut core = MahoCore::new();
        let src_space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.favorite_tab(&tab_id);
        assert!(
            !core
                .space_manager
                .get_root_order(&src_space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id.clone())),
            "pre-condition: favorited tab not in source root_order"
        );

        let profile_id = core
            .get_space_view_models()
            .into_iter()
            .find(|s| s.id == src_space_id)
            .and_then(|s| s.profile_id)
            .expect("active space must have profile");
        let dest = core.create_space(
            "Dest",
            SpaceColor {
                hue: 0.0,
                saturation: 0.5,
                brightness: 0.8,
                grain: 0.0,
            },
            profile_id,
        );
        let dest_space_id = dest.id.clone();

        core.handle_event(ShellEvent::DeleteSpace {
            space_id: src_space_id.clone(),
        });

        assert!(
            !core
                .space_manager
                .get_root_order(&dest_space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "favorited tab must not appear in destination root_order after DeleteSpace migration"
        );
    }

    #[test]
    fn export_space_into_folder_favorited_tab_absent_from_destination_root_order() {
        let mut core = MahoCore::new();
        let src_space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.favorite_tab(&tab_id);

        let profile_id = core
            .get_space_view_models()
            .into_iter()
            .find(|s| s.id == src_space_id)
            .and_then(|s| s.profile_id)
            .expect("active space must have profile");
        let dest = core.create_space(
            "Dest",
            SpaceColor {
                hue: 0.0,
                saturation: 0.5,
                brightness: 0.8,
                grain: 0.0,
            },
            profile_id,
        );
        let dest_space_id = dest.id.clone();

        core.handle_event(ShellEvent::ExportSpaceIntoFolder {
            space_id: src_space_id.clone(),
            target_space_id: dest_space_id.clone(),
        });

        assert!(
            !core
                .space_manager
                .get_root_order(&dest_space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "favorited tab must not appear in destination root_order after ExportSpaceIntoFolder"
        );
    }

    #[test]
    fn unfavorite_previously_folder_resident_tab_restores_to_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        let folder_vm = core
            .space_manager
            .create_folder(&space_id, "F", false, None)
            .unwrap();
        core.space_manager
            .add_tab_to_folder(&space_id, &folder_vm.id, tab_id.clone());

        core.favorite_tab(&tab_id);
        // ADR-11: favoriting decouples the tab from its folder.
        assert!(
            !core.space_manager.is_tab_in_folder(&space_id, &tab_id),
            "favoriting must decouple the tab from its folder"
        );

        core.transition_tab_role(&tab_id, TabRole::Normal);

        // Decoupled at favorite time, the tab is no longer folder-resident, so
        // unfavoriting restores it to root_order like any non-folder tab.
        assert!(
            core.space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "decoupled tab must return to root_order after unfavoriting"
        );
    }

    #[test]
    fn unfavorite_non_folder_tab_restores_to_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.favorite_tab(&tab_id);
        core.transition_tab_role(&tab_id, TabRole::Normal);

        assert!(
            core.space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "non-folder tab must be restored to root_order after unfavoriting"
        );
    }

    #[test]
    fn move_tab_favorited_does_not_add_to_root_order_on_destination() {
        let mut core = MahoCore::new();
        let src_space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.favorite_tab(&tab_id);
        assert!(
            !core
                .space_manager
                .get_root_order(&src_space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id.clone())),
            "pre-condition: favorited tab not in src root_order"
        );

        let profile_id = core
            .get_space_view_models()
            .into_iter()
            .find(|s| s.id == src_space_id)
            .and_then(|s| s.profile_id)
            .expect("active space must have profile");
        let dest = core.create_space(
            "Dest",
            SpaceColor {
                hue: 0.0,
                saturation: 0.5,
                brightness: 0.8,
                grain: 0.0,
            },
            profile_id,
        );
        let dest_id = dest.id.clone();

        core.handle_event(ShellEvent::MoveTab {
            tab_id: tab_id.clone(),
            target_space: dest_id.clone(),
            position: 0,
        });

        assert!(
            !core
                .space_manager
                .get_root_order(&dest_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "favorited tab must not appear in destination root_order after MoveTab"
        );
    }

    #[test]
    fn move_tab_non_favorited_appears_in_destination_root_order() {
        let mut core = MahoCore::new();
        let src_space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        let profile_id = core
            .get_space_view_models()
            .into_iter()
            .find(|s| s.id == src_space_id)
            .and_then(|s| s.profile_id)
            .expect("active space must have profile");
        let dest = core.create_space(
            "Dest",
            SpaceColor {
                hue: 0.0,
                saturation: 0.5,
                brightness: 0.8,
                grain: 0.0,
            },
            profile_id,
        );
        let dest_id = dest.id.clone();

        core.handle_event(ShellEvent::MoveTab {
            tab_id: tab_id.clone(),
            target_space: dest_id.clone(),
            position: 0,
        });

        assert!(
            core.space_manager
                .get_root_order(&dest_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "non-favorited tab must appear in destination root_order after MoveTab"
        );
    }

    #[test]
    fn move_tab_to_space_favorited_does_not_add_to_root_order_on_destination() {
        let mut core = MahoCore::new();
        let src_space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.favorite_tab(&tab_id);

        let profile_id = core
            .get_space_view_models()
            .into_iter()
            .find(|s| s.id == src_space_id)
            .and_then(|s| s.profile_id)
            .expect("active space must have profile");
        let dest = core.create_space(
            "Dest",
            SpaceColor {
                hue: 0.0,
                saturation: 0.5,
                brightness: 0.8,
                grain: 0.0,
            },
            profile_id,
        );
        let dest_id = dest.id.clone();

        core.handle_event(ShellEvent::MoveTabToSpace {
            tab_id: tab_id.clone(),
            target_space_id: dest_id.clone(),
            section: String::new(),
        });

        assert!(
            !core
                .space_manager
                .get_root_order(&dest_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "favorited tab must not appear in destination root_order after MoveTabToSpace"
        );
    }

    #[test]
    fn reopen_last_closed_favorited_tab_does_not_add_to_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.favorite_tab(&tab_id);
        assert!(
            !core
                .space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id.clone())),
            "pre-condition: favorited tab not in root_order"
        );

        core.handle_event(ShellEvent::CloseTab {
            tab_id: tab_id.clone(),
            expected_space_id: None,
        });
        core.handle_event(ShellEvent::ReopenLastClosed);

        assert!(
            !core
                .space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "reopened favorited tab must not appear in root_order"
        );
    }

    #[test]
    fn reopen_last_closed_non_favorited_tab_restores_to_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        core.handle_event(ShellEvent::CloseTab {
            tab_id: tab_id.clone(),
            expected_space_id: None,
        });
        core.handle_event(ShellEvent::ReopenLastClosed);

        assert!(
            core.space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "reopened non-favorited tab must appear in root_order"
        );
    }

    #[test]
    fn remove_tab_from_folder_favorited_does_not_add_to_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        let folder_vm = core
            .space_manager
            .create_folder(&space_id, "F", false, None)
            .unwrap();
        core.space_manager
            .add_tab_to_folder(&space_id, &folder_vm.id, tab_id.clone());
        core.favorite_tab(&tab_id);

        core.handle_event(ShellEvent::RemoveTabFromFolder {
            space_id: space_id.clone(),
            folder_id: folder_vm.id.clone(),
            tab_id: tab_id.clone(),
        });

        assert!(
            !core
                .space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "favorited tab must not be added to root_order when removed from folder"
        );
    }

    #[test]
    fn remove_tab_from_folder_non_favorited_restores_to_root_order() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        let folder_vm = core
            .space_manager
            .create_folder(&space_id, "F", false, None)
            .unwrap();
        core.space_manager
            .add_tab_to_folder(&space_id, &folder_vm.id, tab_id.clone());
        assert!(
            !core
                .space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id.clone())),
            "pre-condition: folder-resident tab not in root_order"
        );

        core.handle_event(ShellEvent::RemoveTabFromFolder {
            space_id: space_id.clone(),
            folder_id: folder_vm.id.clone(),
            tab_id: tab_id.clone(),
        });

        assert!(
            core.space_manager
                .get_root_order(&space_id)
                .contains(&maho_types::space::RootItem::Tab(tab_id)),
            "non-favorited tab must be restored to root_order when removed from folder"
        );
    }

    #[test]
    fn move_folder_into_folder_scoped_to_space_rejects_cross_space_parent() {
        let mut core = MahoCore::new();
        let space_a = core.get_active_space_id().clone();

        let folder_a = core
            .space_manager
            .create_folder(&space_a, "FA", false, None)
            .unwrap();

        let profile_id = core
            .get_space_view_models()
            .into_iter()
            .find(|s| s.id == space_a)
            .and_then(|s| s.profile_id)
            .expect("active space must have profile");
        let space_b_space = core.create_space(
            "B",
            SpaceColor {
                hue: 0.5,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            profile_id,
        );
        let space_b = space_b_space.id.clone();

        let parent_b = core
            .space_manager
            .create_folder(&space_b, "Parent", false, None)
            .unwrap();

        let updates = core.handle_event(ShellEvent::MoveFolderIntoFolder {
            space_id: space_a.clone(),
            folder_id: folder_a.id.clone(),
            target_folder_id: parent_b.id.clone(),
        });

        assert!(
            updates.is_empty(),
            "cross-space parent move should emit no updates"
        );

        let space_a_data = core.space_manager.get_space(&space_a).unwrap();
        let folder_in_a = space_a_data
            .folders
            .iter()
            .find(|f| f.id == folder_a.id)
            .unwrap();
        assert_eq!(
            folder_in_a.parent_folder_id, None,
            "folder in space_a should NOT have parent from space_b"
        );
    }

    #[test]
    fn sidebar_state_v2_includes_tree() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        create_tab(&mut core);

        let json_str = core
            .get_sidebar_state_v2(&space_id)
            .expect("should return Some for space with tabs");
        let parsed: serde_json::Value = serde_json::from_str(&json_str).unwrap();
        assert!(parsed.get("tree").is_some(), "JSON must have 'tree' key");
        assert!(
            parsed.get("favicons").is_some(),
            "JSON must have 'favicons' key"
        );
        assert!(parsed["tree"].is_array());
        assert!(parsed["favicons"].is_object());
    }

    #[test]
    fn sidebar_state_v2_favicons_are_base64() {
        use base64::Engine;

        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_id = create_tab(&mut core);

        let favicon_data = maho_types::common::ImageData {
            data: vec![0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A],
            width: 16,
            height: 16,
            format: maho_types::common::ImageFormat::Png,
        };
        core.handle_event(ShellEvent::TabFaviconUpdated {
            tab_id: tab_id.clone(),
            favicon: Some(favicon_data.clone()),
        });

        let json_str = core
            .get_sidebar_state_v2(&space_id)
            .expect("should return Some");
        let parsed: serde_json::Value = serde_json::from_str(&json_str).unwrap();
        let favicons = parsed["favicons"].as_object().unwrap();
        let b64_str = favicons[&tab_id.to_string()]
            .as_str()
            .expect("favicon value must be a string");
        let decoded = base64::engine::general_purpose::STANDARD
            .decode(b64_str)
            .expect("favicon must be valid base64");
        assert_eq!(decoded, favicon_data.data);
    }

    #[test]
    fn sidebar_state_v2_unknown_space_returns_none() {
        let core = MahoCore::new();
        let bogus_space = SpaceId::new("nonexistent-space-id");
        assert!(core.get_sidebar_state_v2(&bogus_space).is_none());
    }

    #[test]
    fn tidy_capability_create_folder_and_add_tab_directly() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let tab_a = create_tab(&mut core);
        let tab_b = create_tab(&mut core);

        let folder = core
            .space_manager
            .create_folder(&space_id, "Wappalyzer", false, None)
            .expect("create_folder must return a FolderViewModel");
        core.space_manager
            .add_tab_to_folder(&space_id, &folder.id, tab_a.clone());
        core.space_manager
            .add_tab_to_folder(&space_id, &folder.id, tab_b.clone());

        let folders = core.space_manager.get_folder_view_models(&space_id);
        let created = folders
            .iter()
            .find(|f| f.id == folder.id)
            .expect("folder must appear in space");
        assert_eq!(created.name, "Wappalyzer");
        assert!(created.tab_ids.contains(&tab_a));
        assert!(created.tab_ids.contains(&tab_b));
        assert_eq!(created.tab_count, 2);
    }

    #[test]
    fn tidy_tabs_below_threshold_emits_no_llm_request() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        for _ in 0..5 {
            create_tab(&mut core);
        }
        let updates = core.handle_event(ShellEvent::RequestTidyTabs {
            space_id: space_id.clone(),
        });
        let has_llm_request = updates
            .iter()
            .any(|u| matches!(u, CoreUpdate::RequestLlmCompletion { .. }));
        assert!(
            !has_llm_request,
            "Tidy must not fire LLM when fewer than 6 unpinned tabs"
        );
    }

    #[test]
    fn tidy_tabs_end_to_end_creates_folders_with_correct_membership() {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id().clone();
        let mut tab_ids: Vec<TabId> = Vec::new();
        for _ in 0..6 {
            tab_ids.push(create_tab(&mut core));
        }

        let updates = core.handle_event(ShellEvent::RequestTidyTabs {
            space_id: space_id.clone(),
        });
        let request_id = updates
            .iter()
            .find_map(|u| match u {
                CoreUpdate::RequestLlmCompletion { request_id, .. } => Some(request_id.clone()),
                _ => None,
            })
            .expect("RequestTidyTabs with 6+ tabs must emit RequestLlmCompletion");

        let llm_result = serde_json::json!({
            "folders": [
                {
                    "name": "Group A",
                    "tab_ids": [tab_ids[0].to_string(), tab_ids[1].to_string(), tab_ids[2].to_string()]
                },
                {
                    "name": "Group B",
                    "tab_ids": [tab_ids[3].to_string(), tab_ids[4].to_string()]
                },
            ]
        })
        .to_string();
        let updates = core.handle_event(ShellEvent::LlmResult {
            request_id,
            result: llm_result,
        });
        let tidy_ready = updates
            .iter()
            .any(|u| matches!(u, CoreUpdate::TidyTabsReady { .. }));
        assert!(tidy_ready, "LlmResult must emit TidyTabsReady");

        let updates = core.handle_event(ShellEvent::ApplyTidyTabs {
            space_id: space_id.clone(),
        });
        let folder_ids: Vec<FolderId> = updates
            .iter()
            .filter_map(|u| match u {
                CoreUpdate::FolderCreated { folder } => Some(folder.id.clone()),
                _ => None,
            })
            .collect();
        assert_eq!(folder_ids.len(), 2, "ApplyTidyTabs must create 2 folders");

        let folders = core.space_manager.get_folder_view_models(&space_id);
        let group_a = folders
            .iter()
            .find(|f| f.name == "Group A")
            .expect("Group A folder must exist after Apply");
        let group_b = folders
            .iter()
            .find(|f| f.name == "Group B")
            .expect("Group B folder must exist after Apply");
        assert_eq!(group_a.tab_count, 3);
        assert_eq!(group_b.tab_count, 2);
        assert!(group_a.tab_ids.contains(&tab_ids[0]));
        assert!(group_a.tab_ids.contains(&tab_ids[1]));
        assert!(group_a.tab_ids.contains(&tab_ids[2]));
        assert!(group_b.tab_ids.contains(&tab_ids[3]));
        assert!(group_b.tab_ids.contains(&tab_ids[4]));
    }
}

impl std::fmt::Display for PersistenceError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Serialization(msg) => write!(f, "Serialization error: {}", msg),
            Self::Storage(msg) => write!(f, "Storage error: {}", msg),
        }
    }
}

impl std::error::Error for PersistenceError {}

fn urlencoding_decode(s: &str) -> String {
    let mut bytes = Vec::with_capacity(s.len());
    let mut iter = s.bytes();
    while let Some(b) = iter.next() {
        if b == b'%' {
            let hi = iter.next().unwrap_or(b'0');
            let lo = iter.next().unwrap_or(b'0');
            let hex = [hi, lo];
            if let Ok(hex_str) = std::str::from_utf8(&hex) {
                if let Ok(val) = u8::from_str_radix(hex_str, 16) {
                    bytes.push(val);
                    continue;
                }
            }
            bytes.push(b'%');
            bytes.push(hi);
            bytes.push(lo);
        } else if b == b'+' {
            bytes.push(b' ');
        } else {
            bytes.push(b);
        }
    }
    String::from_utf8(bytes).unwrap_or_else(|e| String::from_utf8_lossy(e.as_bytes()).into_owned())
}

fn format_key_combo_label(key_combo: &KeyCombo) -> String {
    let is_mac = cfg!(target_os = "macos");
    let mut parts: Vec<&str> = Vec::new();

    for modifier in [
        KeyModifier::Ctrl,
        KeyModifier::Alt,
        KeyModifier::Shift,
        KeyModifier::Meta,
    ] {
        if key_combo.modifiers.contains(&modifier) {
            if is_mac {
                parts.push(match modifier {
                    KeyModifier::Ctrl => "\u{2303}",
                    KeyModifier::Alt => "\u{2325}",
                    KeyModifier::Shift => "\u{21E7}",
                    KeyModifier::Meta => "\u{2318}",
                });
            } else {
                parts.push(match modifier {
                    KeyModifier::Ctrl => "Ctrl",
                    KeyModifier::Alt => "Alt",
                    KeyModifier::Shift => "Shift",
                    KeyModifier::Meta => "Super",
                });
            }
        }
    }

    let key = if key_combo.key.len() == 1 {
        key_combo.key.to_uppercase()
    } else {
        key_combo.key.clone()
    };

    if is_mac {
        // macOS: compact symbols without separators e.g. ⌃⌥⇧⌘K
        let mut label = parts.join("");
        label.push_str(&key);
        label
    } else {
        // Other platforms: text with + separator e.g. Ctrl+Shift+K
        parts.push(&key);
        parts.join("+")
    }
}

#[allow(dead_code)]
pub struct MahoCore {
    tab_manager: TabLifecycleManager,
    space_manager: SpaceManager,
    command_bar: CommandBarEngine,
    dispatcher: EventDispatcher,
    update_listeners: UpdateListeners,
    storage: Option<maho_storage::sqlite::SqliteStorage>,
    lmdb_storage: Option<maho_storage::lmdb::LmdbStorage>,
    bookmark_manager: BookmarkManager,
    boost_manager: BoostManager,
    css_mod_manager: CssModManager,
    note_manager: NoteManager,
    easel_manager: EaselManager,
    download_manager: DownloadManager,
    permission_manager: PermissionManager,
    password_manager: PasswordManager,
    autofill_manager: AutofillManager,
    atc_manager: ATCManager,
    find_manager: FindManager,
    content_blocker: ContentBlocker,
    crash_recovery: CrashRecoveryManager,
    tab_preview_manager: TabPreviewManager,
    settings_manager: SettingsManager,
    profile_manager: ProfileManager,
    import_export: ImportExportManager,
    notification_manager: NotificationManager,
    extension_bridge: ExtensionBridge,
    backup_manager: BackupManager,
    reading_list_manager: ReadingListManager,
    shortcut_manager: ShortcutManager,
    last_hlc_ts: Cell<u64>,
    account_manager: AccountManager,
    split_view_configs: HashMap<WindowId, SplitViewConfig>,
    sync_manager: Option<crate::sync_manager::SyncManager>,
    llm_manager: LLMManager,
    tool_registry: ToolRegistry,
    skills_manager: SkillsManager,
    sharing_manager: SharingManager,
    memory_manager: MemoryManager,
    persist_throttle: RefCell<PersistThrottle>,
    /// B3: Tracks last-persisted received_bytes per download to throttle SQLite writes.
    /// Progress is only flushed to storage every DOWNLOAD_PERSIST_INTERVAL_BYTES.
    download_progress_last_persisted: HashMap<String, u64>,
    sync_push_count: Cell<usize>,
    sync_last_snapshot_time: Cell<chrono::DateTime<chrono::Utc>>,
    sync_startup_time: Cell<chrono::DateTime<chrono::Utc>>,
    sync_last_upload_attempt: Cell<chrono::DateTime<chrono::Utc>>,
    emitted_during_dispatch: Rc<RefCell<Vec<CoreUpdate>>>,
}

impl Default for MahoCore {
    fn default() -> Self {
        Self::new()
    }
}

impl MahoCore {
    pub fn load_skills_from_dir(&mut self, dir_path: &std::path::Path) -> Result<(), String> {
        self.skills_manager.load_skills_from_dir(dir_path)
    }

    pub fn resolve_agent_slash_allowed_tools(&self, slash_command: &str) -> Option<Vec<String>> {
        self.skills_manager
            .find_by_slash(slash_command)
            .map(|skill| skill.allowed_tools.clone())
    }

    pub fn trusted_active_tab_origin(&self) -> Option<String> {
        self.tab_manager
            .get_active_tab_id()
            .and_then(|tab_id| self.tab_manager.get_tab(tab_id))
            .and_then(|tab| trusted_origin_from_url(tab.url.as_ref()))
    }

    pub fn resolve_agent_base_system_prompt(
        &self,
        session_id: &str,
        workspace_root: &str,
        active_space_id: Option<&str>,
    ) -> String {
        let Some(storage) = self.storage.as_ref() else {
            return String::new();
        };

        if let Ok(Some(snapshot)) = storage.get_system_prompt_snapshot(session_id) {
            return snapshot;
        }

        let workspace = resolve_agent_workspace(storage, workspace_root, active_space_id);
        let profile = resolve_agent_profile(storage, &workspace);
        crate::config_resolver::resolve_config(None, &profile, &workspace).system_prompt
    }

    pub fn compose_agent_system_prompt_with_skill_context(
        &self,
        base_system_prompt: &str,
        trusted_origin: Option<&str>,
        token_budget_chars: usize,
    ) -> Option<String> {
        let origin = trusted_origin?;
        let skill_context = self
            .skills_manager
            .build_injected_context(origin, token_budget_chars);
        if skill_context.is_empty() {
            return None;
        }

        if base_system_prompt.is_empty() {
            return Some(skill_context);
        }

        let mut composed =
            String::with_capacity(base_system_prompt.len() + skill_context.len() + 2);
        composed.push_str(base_system_prompt);
        composed.push_str("\n\n");
        composed.push_str(&skill_context);
        Some(composed)
    }

    pub fn space_manager(&self) -> &SpaceManager {
        &self.space_manager
    }

    pub fn space_manager_mut(&mut self) -> &mut SpaceManager {
        &mut self.space_manager
    }

    pub fn tab_manager(&self) -> &TabLifecycleManager {
        &self.tab_manager
    }

    pub fn tool_registry(&self) -> &ToolRegistry {
        &self.tool_registry
    }

    pub fn new() -> Self {
        let update_listeners: UpdateListeners = Rc::new(RefCell::new(Vec::new()));
        let emitted_during_dispatch = Rc::new(RefCell::new(Vec::new()));

        let listeners_for_tab = update_listeners.clone();
        let emitted_for_tab = emitted_during_dispatch.clone();
        let tab_callback = Box::new(move |update: CoreUpdate| {
            emitted_for_tab.borrow_mut().push(update.clone());
            for listener in listeners_for_tab.borrow().iter() {
                listener(update.clone());
            }
        });

        let tab_manager = TabLifecycleManager::new(tab_callback);
        let space_manager = SpaceManager::new();
        let mut command_bar = CommandBarEngine::new();

        command_bar.add_action("close_tab".into(), "Close Tab".into(), "Tabs".into());
        command_bar.add_action(
            "close_other_tabs".into(),
            "Close Other Tabs".into(),
            "Tabs".into(),
        );
        command_bar.add_action(
            "close_all_tabs".into(),
            "Close All Tabs".into(),
            "Tabs".into(),
        );
        command_bar.add_action(
            "duplicate_tab".into(),
            "Duplicate Tab".into(),
            "Tabs".into(),
        );
        command_bar.add_action("pin_tab".into(), "Pin Tab".into(), "Tabs".into());
        command_bar.add_action("unpin_tab".into(), "Unpin Tab".into(), "Tabs".into());
        command_bar.add_action("mute_tab".into(), "Mute Tab".into(), "Tabs".into());
        command_bar.add_action("unmute_tab".into(), "Unmute Tab".into(), "Tabs".into());
        command_bar.add_action(
            "mute_all_tabs".into(),
            "Mute All Tabs".into(),
            "Tabs".into(),
        );
        command_bar.add_action("freeze_tab".into(), "Freeze Tab".into(), "Tabs".into());
        command_bar.add_action("unfreeze_tab".into(), "Unfreeze Tab".into(), "Tabs".into());
        command_bar.add_action("reload_tab".into(), "Reload Tab".into(), "Tabs".into());
        command_bar.add_action("hard_reload".into(), "Hard Reload".into(), "Tabs".into());
        command_bar.add_action("archive_tab".into(), "Archive Tab".into(), "Tabs".into());
        command_bar.add_action(
            "restore_last_closed".into(),
            "Restore Last Closed Tab".into(),
            "Tabs".into(),
        );
        command_bar.add_action("copy_url".into(), "Copy URL".into(), "Navigation".into());
        command_bar.add_action("new_space".into(), "New Space".into(), "Navigation".into());
        command_bar.add_action(
            "toggle_sidebar".into(),
            "Toggle Sidebar".into(),
            "Navigation".into(),
        );
        command_bar.add_action(
            "toggle_full_screen".into(),
            "Toggle Full Screen".into(),
            "Navigation".into(),
        );
        command_bar.add_action(
            "split_left".into(),
            "Split Left".into(),
            "Split View".into(),
        );
        command_bar.add_action(
            "split_right".into(),
            "Split Right".into(),
            "Split View".into(),
        );
        command_bar.add_action(
            "close_split".into(),
            "Close Split".into(),
            "Split View".into(),
        );
        command_bar.add_action(
            "toggle_split_view".into(),
            "Toggle Split View".into(),
            "Split View".into(),
        );
        command_bar.add_action(
            "new_folder".into(),
            "New Folder".into(),
            "Navigation".into(),
        );
        command_bar.add_action("zoom_in".into(), "Zoom In".into(), "View".into());
        command_bar.add_action("zoom_out".into(), "Zoom Out".into(), "View".into());
        command_bar.add_action("reset_zoom".into(), "Reset Zoom".into(), "View".into());
        command_bar.add_action("find_in_page".into(), "Find in Page".into(), "View".into());
        command_bar.add_action("view_source".into(), "View Source".into(), "View".into());
        command_bar.add_action(
            "toggle_dev_tools".into(),
            "Toggle Developer Tools".into(),
            "View".into(),
        );
        command_bar.add_action("print_page".into(), "Print Page".into(), "View".into());
        command_bar.add_action("settings".into(), "Settings".into(), "Settings".into());
        command_bar.add_action(
            "clear_history".into(),
            "Clear History".into(),
            "Settings".into(),
        );
        command_bar.add_action(
            "clear_cookies".into(),
            "Clear Cookies".into(),
            "Settings".into(),
        );
        command_bar.add_action(
            "open_downloads".into(),
            "Downloads".into(),
            "Settings".into(),
        );
        command_bar.add_action(
            "open_bookmarks".into(),
            "Bookmarks".into(),
            "Settings".into(),
        );
        command_bar.add_action("open_history".into(), "History".into(), "Settings".into());
        command_bar.add_action(
            "view_archive".into(),
            "View Archive".into(),
            "Archive".into(),
        );
        command_bar.add_action(
            "clear_archive".into(),
            "Clear Archive".into(),
            "Archive".into(),
        );
        command_bar.add_action(
            "toggle_boost".into(),
            "Toggle Boost".into(),
            "Extensions".into(),
        );
        command_bar.add_action(
            "open_boost_editor".into(),
            "Boost Editor".into(),
            "Extensions".into(),
        );
        command_bar.add_action(
            "toggle_content_blocker".into(),
            "Toggle Content Blocker".into(),
            "Extensions".into(),
        );
        command_bar.add_action(
            "next_space".into(),
            "Next Space".into(),
            "Navigation".into(),
        );
        command_bar.add_action(
            "prev_space".into(),
            "Previous Space".into(),
            "Navigation".into(),
        );
        command_bar.add_action(
            "share_page".into(),
            "Share Page".into(),
            "Navigation".into(),
        );
        command_bar.add_action(
            "new_incognito".into(),
            "New Incognito Window".into(),
            "Navigation".into(),
        );
        command_bar.add_action(
            "clear_browsing_data".into(),
            "Clear Browsing Data".into(),
            "Settings".into(),
        );
        command_bar.add_action(
            "focus_url_bar".into(),
            "Focus URL Bar".into(),
            "Navigation".into(),
        );
        command_bar.add_action(
            "scroll_to_top".into(),
            "Scroll to Top".into(),
            "View".into(),
        );
        command_bar.add_action(
            "scroll_to_bottom".into(),
            "Scroll to Bottom".into(),
            "View".into(),
        );

        let dispatcher = EventDispatcher::new();

        let core = Self {
            tab_manager,
            space_manager,
            command_bar,
            dispatcher,
            update_listeners,
            storage: None,
            lmdb_storage: None,
            bookmark_manager: BookmarkManager::new(),
            boost_manager: BoostManager::new(),
            css_mod_manager: CssModManager::new(),
            note_manager: NoteManager::new(),
            easel_manager: EaselManager::new(),
            download_manager: DownloadManager::new(),
            permission_manager: PermissionManager::new(),
            password_manager: PasswordManager::new(),
            autofill_manager: AutofillManager::new(),
            atc_manager: ATCManager::new(),
            find_manager: FindManager::new(),
            content_blocker: ContentBlocker::new(),
            crash_recovery: CrashRecoveryManager::new(),
            tab_preview_manager: TabPreviewManager::new(),
            settings_manager: SettingsManager::new(),
            profile_manager: ProfileManager::new(),
            import_export: ImportExportManager::new(),
            notification_manager: NotificationManager::new(),
            extension_bridge: ExtensionBridge::new(Box::new(Vec::new), Box::new(|| None)),
            backup_manager: BackupManager::new(),
            reading_list_manager: ReadingListManager::new(),
            shortcut_manager: ShortcutManager::new(),
            last_hlc_ts: Cell::new(0),
            account_manager: AccountManager::new(),
            split_view_configs: HashMap::new(),
            sync_manager: None,
            llm_manager: LLMManager::new(),
            tool_registry: ToolRegistry::new(),
            skills_manager: SkillsManager::new(),
            sharing_manager: SharingManager::new(),
            memory_manager: MemoryManager::new(),
            persist_throttle: RefCell::new(PersistThrottle::new()),
            download_progress_last_persisted: HashMap::new(),
            sync_push_count: Cell::new(0),
            sync_last_snapshot_time: Cell::new(chrono::Utc::now() - chrono::Duration::hours(6)),
            sync_startup_time: Cell::new(chrono::Utc::now()),
            sync_last_upload_attempt: Cell::new(chrono::Utc::now() - chrono::Duration::hours(1)), // Initialize to 1 hour ago so first upload is allowed
            emitted_during_dispatch,
        };

        core
    }

    // INTEGRATION POINT: RoutineScheduler
    // To wire up the cron scheduler, the Chromium shell should call:
    //   let scheduler = RoutineScheduler::new(agent_runner, tier_provider, storage_get, storage_set);
    //   tokio::spawn(scheduler.run_loop());
    // after MahoCore is initialized and the tokio runtime is available.
    // The tier_provider should call core.get_account_tier() each tick.

    /// Seeds the active space with an initial favorite tab and a pinned tab.
    ///
    /// Not called from `new()` in the Chromium build path: Chromium already has
    /// real live tabs in its `TabStripModel` before the sidebar is ready, and
    /// calling this would produce ghost core-only tabs (about:blank,
    /// `tab_strip_index = -1`) that break sidebar actions. Instead,
    /// `MahoSidebarView::MaybeReconcileStartupTabs()` performs a one-time
    /// startup reconciliation once the `Browser` and `TabStripModel` are ready.
    ///
    /// Callers that need the placeholder seed (e.g. unit tests that run without
    /// a Chromium browser) may call this explicitly after `new()`.
    #[allow(dead_code)]
    pub fn seed_first_run_tabs(&mut self) {
        if !self.tab_manager.get_all_tabs().is_empty() {
            return;
        }

        let active_space_id = self.space_manager.get_active_space_id().clone();

        let ntp_url = Some(maho_types::common::Url::new("chrome://newtab"));

        let favorite_tab = self.tab_manager.create_tab(
            active_space_id.clone(),
            ntp_url.clone(),
            None,
            None,
            false,
        );
        self.space_manager
            .add_tab_to_space(&active_space_id, favorite_tab.id.clone(), None);
        self.tab_manager.favorite_tab(&favorite_tab.id);

        let pinned_tab =
            self.tab_manager
                .create_tab(active_space_id.clone(), ntp_url, None, None, false);
        self.space_manager
            .add_tab_to_space(&active_space_id, pinned_tab.id.clone(), None);
        self.tab_manager.pin_tab(&pinned_tab.id);

        self.space_manager
            .set_last_active_tab(&active_space_id, Some(favorite_tab.id));
    }

    pub fn storage_ref(&self) -> Option<&maho_storage::sqlite::SqliteStorage> {
        self.storage.as_ref()
    }

    pub fn sqlite_db_path(&self) -> Option<&str> {
        self.storage.as_ref().map(|s| s.path())
    }

    pub fn with_storage(mut self, path: &str) -> Self {
        match maho_storage::sqlite::SqliteStorage::open(path) {
            Ok(storage) => {
                eprintln!("[MahoCore::with_storage] OK: opened SQLite storage at {path}");
                self.storage = Some(storage);
                if let Some(ref storage) = self.storage {
                    if let Ok(Some(json)) = storage.get_setting("browser_settings") {
                        if let Ok(settings) = serde_json::from_str::<Settings>(&json) {
                            self.settings_manager = SettingsManager::with_settings(settings);
                        }
                    }
                    if let Ok(shortcuts_data) = storage.load_shortcuts() {
                        for (action, key_combo_json, _, _) in &shortcuts_data {
                            if !key_combo_json.is_empty() {
                                if let Ok(key_combo) = serde_json::from_str(key_combo_json) {
                                    let _ = self.shortcut_manager.set_shortcut(action, key_combo);
                                }
                            }
                        }
                        for (action, _, enabled, _) in shortcuts_data {
                            if !enabled {
                                self.shortcut_manager.toggle_shortcut(&action, false);
                            }
                        }
                    }
                    if let Ok(downloads_data) = storage.load_downloads() {
                        for (id, filename, url, total_bytes, received_bytes, state, file_path, started_at, completed_at, mime_type, chromium_guid) in downloads_data {
                            let dl_state = match state.as_str() {
                                "paused" => DownloadState::Paused,
                                "completed" => DownloadState::Completed,
                                "failed" => DownloadState::Failed,
                                "cancelled" => DownloadState::Cancelled,
                                _ => DownloadState::Downloading,
                            };
                            // Active rows are re-ingested live by the browser download
                            // manager on restart under a fresh id; drop the stale row so
                            // the same file does not appear twice.
                            if matches!(dl_state, DownloadState::Downloading | DownloadState::Paused) {
                                let _ = storage.delete_download(&id);
                                continue;
                            }
                            // D4: delete stale completed rows with empty filename AND empty/missing file_path
                            if matches!(dl_state, DownloadState::Completed)
                                && filename.is_empty()
                                && file_path.as_deref().unwrap_or("").is_empty()
                            {
                                let _ = storage.delete_download(&id);
                                continue;
                            }
                            let dl_id = maho_types::identifiers::DownloadId::new(id);
                            let download = crate::download_manager::Download {
                                id: dl_id.clone(),
                                filename,
                                url,
                                total_bytes,
                                received_bytes,
                                state: dl_state,
                                started_at: DateTime::from_iso(started_at),
                                file_path,
                                resume_data: None,
                                mime_type,
                                error: None,
                                completed_at: completed_at.map(DateTime::from_iso),
                                original_filename: None,
                                chromium_guid,
                            };
                            self.download_manager.seed_download(download);
                        }
                    }
                    // Memory feature: build HNSW from persisted embeddings
                    #[cfg(not(target_os = "ios"))]
                    let _ = self.memory_manager.rebuild_index(storage);
                    #[cfg(not(target_os = "ios"))]
                    self.memory_manager.start_browsing_summary_loop();
                }
            }
            Err(e) => {
                eprintln!(
                    "[MahoCore::with_storage] FAIL: SqliteStorage::open({path}) returned Err: {e}. \
                     Storage will remain None; AI settings, agent sessions, and memory features will NOT persist."
                );
                self.storage = None;
            }
        }
        self
    }

    pub fn with_lmdb_storage(mut self, path: &std::path::Path) -> Self {
        #[cfg(target_os = "ios")]
        {
            let _ = path;
            eprintln!("[MahoCore::with_lmdb_storage] SKIPPED on iOS (LMDB incompatible with APFS mmap semantics). Tab/space state will not persist across restarts.");
            self.lmdb_storage = None;
            return self;
        }
        #[cfg(not(target_os = "ios"))]
        match maho_storage::lmdb::LmdbStorage::open(path) {
            Ok(storage) => {
                self.lmdb_storage = Some(storage);
            }
            Err(e) => {
                eprintln!(
                    "[MahoCore::with_lmdb_storage] FAIL: LmdbStorage::open({}) returned Err: {e}. \
                     LMDB storage will remain None; tab/space state will NOT persist across restarts.",
                    path.display()
                );
                self.lmdb_storage = None;
            }
        }
        self
    }

    pub fn save_state(&self) -> Result<(), PersistenceError> {
        self.save_state_partial(LMDB_DIRTY_ALL)
    }

    fn save_state_partial(&self, dirty: u8) -> Result<(), PersistenceError> {
        if dirty == 0 {
            return Ok(());
        }

        let lmdb = self
            .lmdb_storage
            .as_ref()
            .ok_or_else(|| PersistenceError::Storage("LMDB storage not initialized".to_string()))?;

        if dirty & LMDB_DIRTY_TABS != 0 {
            let tabs: Vec<&maho_types::tab::Tab> = self
                .tab_manager
                .get_all_tabs()
                .into_iter()
                .filter(|tab| !tab.is_private)
                .collect();
            let favorite_count = tabs.iter().filter(|t| t.role.is_favorite()).count(); // L3-EXEMPT: role query
            eprintln!(
                "[Maho] save_state: persisting {} tabs ({} favorites), {} spaces",
                tabs.len(),
                favorite_count,
                self.space_manager.get_all_spaces().len(),
            );
            let tabs_json = serde_json::to_vec(&tabs)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            lmdb.put("tabs", &tabs_json)
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
            let private_ids = private_tab_ids_from_tabs(self.tab_manager.get_all_tabs());
            self.tab_preview_manager
                .persist_to_storage(lmdb, &private_ids);
        }

        if dirty & LMDB_DIRTY_SPACES != 0 {
            let private_ids = private_tab_ids_from_tabs(self.tab_manager.get_all_tabs());
            let spaces =
                sanitize_spaces_for_private_tabs(self.space_manager.get_all_spaces(), &private_ids);
            let spaces_json = serde_json::to_vec(&spaces)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            lmdb.put("spaces", &spaces_json)
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        }

        if dirty & LMDB_DIRTY_ACTIVE_SPACE != 0 {
            let active_space_id = self.space_manager.get_active_space_id();
            let active_space_json = serde_json::to_vec(active_space_id)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            lmdb.put("active_space_id", &active_space_json)
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        }

        if dirty & LMDB_DIRTY_PROFILES != 0 {
            let profiles = self.profile_manager.list_profiles();
            let profiles_json = serde_json::to_vec(&profiles)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            lmdb.put("profiles", &profiles_json)
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        }

        if dirty & LMDB_DIRTY_ACTIVE_PROFILE != 0 {
            let active_profile_id = self.profile_manager.get_active_profile_id();
            let active_profile_json = serde_json::to_vec(&active_profile_id)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            lmdb.put("active_profile_id", &active_profile_json)
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        }

        if dirty & LMDB_DIRTY_ACCOUNT != 0 {
            let persisted = crate::account_manager::PersistedAccount {
                account: self.account_manager.get_account().cloned(),
                device_id: self.account_manager.device_id(),
            };
            let account_json = serde_json::to_vec(&persisted)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            lmdb.put("account", &account_json)
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        }
        if dirty & LMDB_DIRTY_SPLIT_VIEW != 0 {
            let mut persist_map = HashMap::new();
            for (window_id, config) in &self.split_view_configs {
                let mut stable_panes = Vec::new();
                for pane in &config.panes {
                    if let Some(tab) = self.tab_manager.get_tab(&pane.tab_id) {
                        if tab.is_private {
                            continue;
                        }
                        let tab_creation_time_micros = if let Ok(dt) =
                            chrono::DateTime::parse_from_rfc3339(&tab.created_at.0)
                        {
                            dt.timestamp_micros() as u64
                        } else {
                            0
                        };
                        stable_panes.push(maho_types::split_view::StablePaneIdentity {
                            url: tab.url.0.clone(),
                            tab_creation_time_micros,
                            window_session_uuid: tab.window_id.map(|id| id.to_string()),
                        });
                    }
                }
                persist_map.insert(
                    window_id.0.clone(),
                    maho_types::split_view::SplitViewPersistConfig {
                        panes: stable_panes,
                        orientation: config.orientation.clone(),
                        ratios: config.ratios.clone(),
                    },
                );
            }
            let split_view_json = serde_json::to_vec(&persist_map)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            lmdb.put("split_view_configs", &split_view_json)
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        }

        Ok(())
    }

    fn persist_lmdb_now_partial(&self, bits: u8) {
        if self.lmdb_storage.is_none() || bits == 0 {
            return;
        }
        let to_flush = {
            let mut t = self.persist_throttle.borrow_mut();
            t.pending_bits |= bits;
            let elapsed = t.last_flush.elapsed();
            if elapsed < Duration::from_millis(PERSIST_THROTTLE_MS) {
                return;
            }
            let bits = t.pending_bits;
            t.pending_bits = 0;
            t.last_flush = Instant::now();
            bits
        };
        if let Err(e) = self.save_state_partial(to_flush) {
            eprintln!("[Maho] persist_lmdb_now_partial failed: {}", e);
            self.persist_throttle.borrow_mut().pending_bits |= to_flush;
        }
    }

    fn persist_lmdb_force_flush(&self) {
        if self.lmdb_storage.is_none() {
            return;
        }
        let to_flush = {
            let mut t = self.persist_throttle.borrow_mut();
            let bits = t.pending_bits | LMDB_DIRTY_ALL;
            t.pending_bits = 0;
            t.last_flush = Instant::now();
            bits
        };
        if let Err(e) = self.save_state_partial(to_flush) {
            eprintln!("[Maho] persist_lmdb_force_flush failed: {}", e);
        }
    }

    pub fn load_state(&mut self) -> Result<(), PersistenceError> {
        let lmdb = self
            .lmdb_storage
            .as_ref()
            .ok_or_else(|| PersistenceError::Storage("LMDB storage not initialized".to_string()))?;

        // One-shot LMDB schema migration for favorites schema v1 -> v2 (HC-1)
        let schema_version: u32 = if let Some(version_data) = lmdb
            .get("schema_version")
            .map_err(|e| PersistenceError::Storage(e.to_string()))?
        {
            serde_json::from_slice(&version_data).unwrap_or(0)
        } else {
            0
        };

        if schema_version < 2 {
            if let Some(tabs_data) = lmdb
                .get("tabs")
                .map_err(|e| PersistenceError::Storage(e.to_string()))?
            {
                let tabs: Vec<maho_types::tab::Tab> = serde_json::from_slice(&tabs_data)
                    .map_err(|e| PersistenceError::Serialization(e.to_string()))?;

                let migrated_tabs: Vec<&maho_types::tab::Tab> =
                    tabs.iter().filter(|tab| !tab.is_private).collect();
                let migrated_data = serde_json::to_vec(&migrated_tabs)
                    .map_err(|e| PersistenceError::Serialization(e.to_string()))?;

                let version_bytes = serde_json::to_vec(&2u32)
                    .map_err(|e| PersistenceError::Serialization(e.to_string()))?;

                // Atomic batch write (M1)
                lmdb.put_batch(&[
                    ("tabs.pre_migration_backup", &tabs_data),
                    ("tabs", &migrated_data),
                    ("schema_version", &version_bytes),
                ])
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;

                let fav_count = tabs.iter().filter(|t| t.role.is_favorite()).count(); // L3-EXEMPT: role query
                eprintln!(
                    "[Maho] LMDB migration v1->v2: {} tabs migrated, {} favorites",
                    tabs.len(),
                    fav_count
                );
            } else {
                let version_bytes = serde_json::to_vec(&2u32)
                    .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
                lmdb.put("schema_version", &version_bytes)
                    .map_err(|e| PersistenceError::Storage(e.to_string()))?;
            }
        }

        if schema_version < 4 {
            let tabs_raw = lmdb
                .get("tabs")
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
            let spaces_raw = lmdb
                .get("spaces")
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
            let version_bytes = serde_json::to_vec(&4u32)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            if let (Some(tabs_data), Some(spaces_data)) = (tabs_raw, spaces_raw) {
                let tabs: Vec<maho_types::tab::Tab> = serde_json::from_slice(&tabs_data)
                    .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
                let spaces: Vec<maho_types::space::Space> = serde_json::from_slice(&spaces_data)
                    .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
                let (deduped_tabs, deduped_spaces, removed) =
                    dedup_duplicate_pinned_favorites(tabs, spaces);
                if removed > 0 {
                    let tabs_out = serde_json::to_vec(&deduped_tabs)
                        .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
                    let spaces_out = serde_json::to_vec(&deduped_spaces)
                        .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
                    lmdb.put_batch(&[
                        ("tabs", &tabs_out),
                        ("spaces", &spaces_out),
                        ("schema_version", &version_bytes),
                    ])
                    .map_err(|e| PersistenceError::Storage(e.to_string()))?;
                    eprintln!(
                        "[Maho] LMDB dedup migration (schema v4): removed {} duplicate ghost favorite/pinned tabs",
                        removed
                    );
                } else {
                    lmdb.put("schema_version", &version_bytes)
                        .map_err(|e| PersistenceError::Storage(e.to_string()))?;
                }
            } else {
                lmdb.put("schema_version", &version_bytes)
                    .map_err(|e| PersistenceError::Storage(e.to_string()))?;
            }
        }

        let tabs_data = lmdb
            .get("tabs")
            .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        let spaces_data = lmdb
            .get("spaces")
            .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        let profiles_data = lmdb
            .get("profiles")
            .map_err(|e| PersistenceError::Storage(e.to_string()))?;

        let is_first_run = tabs_data.is_none() && spaces_data.is_none();

        if tabs_data.is_some() || spaces_data.is_some() || profiles_data.is_some() {
            self.tab_manager.clear();
        }

        let mut loaded_private_ids = HashSet::new();
        if let Some(tabs_data) = tabs_data {
            let tabs: Vec<maho_types::tab::Tab> = serde_json::from_slice(&tabs_data)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            loaded_private_ids = private_tab_ids_from_tabs(tabs.iter());
            let persisted_tabs: Vec<maho_types::tab::Tab> =
                tabs.into_iter().filter(|tab| !tab.is_private).collect();
            let fav_count = persisted_tabs
                .iter()
                .filter(|t| t.role.is_favorite())
                .count(); // L3-EXEMPT: role query
            eprintln!(
                "[Maho] load_state: restoring {} tabs ({} favorites)",
                persisted_tabs.len(),
                fav_count,
            );
            for mut tab in persisted_tabs {
                if tab.role.is_pinned() && tab.pinned_url.is_none() {
                    tab.pinned_url = Some(tab.url.clone());
                }
                self.tab_manager.restore_tab(tab);
            }
            let tab_ids: Vec<maho_types::identifiers::TabId> = self
                .tab_manager
                .get_all_tabs()
                .iter()
                .map(|t| t.id.clone())
                .collect();
            self.tab_preview_manager.load_from_storage(lmdb, &tab_ids);
        }

        let active_space_id: maho_types::identifiers::SpaceId = if let Some(active_data) = lmdb
            .get("active_space_id")
            .map_err(|e| PersistenceError::Storage(e.to_string()))?
        {
            serde_json::from_slice(&active_data)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?
        } else {
            self.space_manager.get_active_space_id().clone()
        };

        if let Some(spaces_data) = spaces_data {
            let spaces: Vec<maho_types::space::Space> = serde_json::from_slice(&spaces_data)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            let spaces = sanitize_spaces_for_private_tabs(spaces.iter(), &loaded_private_ids);
            self.space_manager.restore_spaces(spaces, active_space_id);

            // Heal cross-space tab_order/root_order duplication that may have
            // been persisted before the invariant was enforced. tab.space_id
            // is the single ground truth for tab residency.
            let tab_owners: std::collections::HashMap<
                maho_types::identifiers::TabId,
                maho_types::identifiers::SpaceId,
            > = self
                .tab_manager
                .get_all_tabs()
                .into_iter()
                .map(|t| (t.id.clone(), t.space_id.clone()))
                .collect();
            {
                let tm = &self.tab_manager;
                self.space_manager
                    .enforce_cross_space_tab_residency(&tab_owners, &|tid| {
                        tm.get_tab(tid)
                            .map(|t| t.role.is_favorite())
                            .unwrap_or(false) // L3-EXEMPT: role query
                    });
            }

            let favorited_ids: std::collections::HashSet<maho_types::identifiers::TabId> = self
                .tab_manager
                .get_all_favorite_tabs()
                .into_iter()
                .map(|t| t.id.clone())
                .collect();
            self.space_manager
                .strip_favorited_tabs_from_root_orders(&favorited_ids);
        }

        // Restore split_view_configs
        if let Some(split_view_data) = lmdb
            .get("split_view_configs")
            .map_err(|e| PersistenceError::Storage(e.to_string()))?
        {
            if let Ok(persist_map) = serde_json::from_slice::<
                HashMap<String, maho_types::split_view::SplitViewPersistConfig>,
            >(&split_view_data)
            {
                self.split_view_configs.clear();
                for (window_str, persist_config) in persist_map {
                    let window_id = WindowId::new(window_str);
                    let mut resolved_panes = Vec::new();
                    let mut skip_window = false;
                    for stable_pane in &persist_config.panes {
                        let matching_tabs: Vec<&maho_types::tab::Tab> = self
                            .tab_manager
                            .get_all_tabs()
                            .into_iter()
                            .filter(|t| {
                                let t_created_micros = if let Ok(dt) =
                                    chrono::DateTime::parse_from_rfc3339(&t.created_at.0)
                                {
                                    dt.timestamp_micros() as u64
                                } else {
                                    0
                                };
                                t.url.0 == stable_pane.url
                                    && t_created_micros == stable_pane.tab_creation_time_micros
                            })
                            .collect();

                        if matching_tabs.len() == 1 {
                            resolved_panes.push(maho_types::split_view::SplitPane {
                                tab_id: matching_tabs[0].id.clone(),
                            });
                        } else if matching_tabs.len() > 1 {
                            eprintln!(
                                "[Maho] split-view restore: Ambiguity found for URL {} ({} matches). Skipping window {}",
                                stable_pane.url,
                                matching_tabs.len(),
                                window_id
                            );
                            skip_window = true;
                            break;
                        } else {
                            skip_window = true;
                            break;
                        }
                    }

                    if !skip_window && resolved_panes.len() >= 2 {
                        self.split_view_configs.insert(
                            window_id,
                            maho_types::split_view::SplitViewConfig {
                                panes: resolved_panes,
                                orientation: persist_config.orientation,
                                ratios: persist_config.ratios,
                            },
                        );
                    }
                }
            }
        }

        if let Some(profiles_data) = profiles_data {
            let profiles: Vec<maho_types::profile::ProfileConfig> =
                serde_json::from_slice(&profiles_data)
                    .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            let active_profile_id: Option<maho_types::identifiers::ProfileId> =
                if let Some(active_data) = lmdb
                    .get("active_profile_id")
                    .map_err(|e| PersistenceError::Storage(e.to_string()))?
                {
                    serde_json::from_slice(&active_data).ok()
                } else {
                    None
                };
            self.profile_manager
                .restore_profiles(profiles, active_profile_id);
        }

        let account_data = lmdb
            .get("account")
            .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        let (persisted, dirty) = self.account_manager.load_or_create(account_data.as_deref());
        if dirty {
            let serialized = serde_json::to_vec(&persisted)
                .map_err(|e| PersistenceError::Serialization(e.to_string()))?;
            lmdb.put("account", &serialized)
                .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        }

        self.load_persisted_data()?;

        if is_first_run {
            let active_id = self.get_active_space_id();
            self.seed_default_favorite_for_space(&active_id);
        }

        Ok(())
    }

    pub fn load_persisted_data(&mut self) -> Result<(), PersistenceError> {
        let storage = match self.storage.as_ref() {
            Some(s) => s,
            None => return Ok(()),
        };

        // Initialize HLC logical clock cache
        let last_hlc = match storage.get_last_hlc_ts() {
            Ok(ts) => ts,
            Err(e) => {
                eprintln!(
                    "[sync] Failed to load last HLC timestamp, defaulting to 0: {}",
                    e
                );
                0
            }
        };
        self.last_hlc_ts.set(last_hlc);

        // Prune acknowledged sync entities older than 24 hours
        if let Err(e) = storage.prune_acked_sync_entities() {
            eprintln!("[sync] Failed to prune acknowledged sync entities: {}", e);
        }

        // Load notes
        if let Ok(notes_data) = storage.load_notes() {
            for (id, linked_tab_id, linked_url, content, created_at, updated_at) in notes_data {
                let note = maho_types::note::Note {
                    id: maho_types::identifiers::NoteId::new(&id),
                    linked_tab_id: linked_tab_id.map(maho_types::identifiers::TabId::new),
                    linked_url: linked_url.map(maho_types::common::Url::new),
                    content,
                    created_at: maho_types::common::DateTime::from_iso(created_at),
                    updated_at: maho_types::common::DateTime::from_iso(updated_at),
                };
                self.note_manager.restore_note(note);
            }
        }

        // Load boosts
        if let Ok(boosts_data) = storage.load_boosts() {
            for boost in boosts_data {
                let domain = boost.domain.clone();
                let id = boost.id.clone();
                self.boost_manager.restore_boost(boost);
                if let Ok(Some(active_id_str)) = storage.get_active_boost(&domain) {
                    if active_id_str == id.as_ref() {
                        self.boost_manager.restore_active_boost(domain, id);
                    }
                }
            }
        }

        if let Ok(css_mods) = storage.load_css_mods() {
            self.css_mod_manager.restore_mods(css_mods);
        }

        // Load permissions
        if let Ok(perms_data) = storage.load_permissions() {
            for (origin, permission, policy_str) in perms_data {
                let policy = match policy_str.as_str() {
                    "allow" => maho_types::settings::PermissionPolicy::Allow,
                    "deny" => maho_types::settings::PermissionPolicy::Deny,
                    _ => maho_types::settings::PermissionPolicy::Ask,
                };
                self.permission_manager
                    .set_permission(&origin, &permission, policy);
            }
        }

        // Load ATC rules
        if let Ok(rules_data) = storage.load_atc_rules() {
            for (id, name, space_id, url_pattern, max_age_hours, max_tabs, enabled, match_type) in
                rules_data
            {
                let rule = crate::atc_manager::ATCRule {
                    id,
                    name,
                    space_id: space_id.map(maho_types::identifiers::SpaceId::new),
                    url_pattern,
                    max_age_hours,
                    max_tabs,
                    enabled,
                };
                let traffic_match_type = match_type
                    .as_deref()
                    .and_then(maho_types::air_traffic::MatchType::parse);
                self.atc_manager.restore_rule(rule, traffic_match_type);
            }
        }

        // Load default link behavior and open_external_links_in_little_arc
        if let Ok(Some(behavior_json)) = storage.get_setting("default_link_behavior") {
            if let Ok(behavior) = serde_json::from_str(&behavior_json) {
                self.atc_manager.set_default_link_behavior(behavior);
            }
        }
        if let Ok(Some(flag_str)) = storage.get_setting("open_external_links_in_little_arc") {
            let flag = flag_str == "true";
            self.atc_manager.set_open_external_links_in_little_arc(flag);
        }

        // Load autofill addresses
        if let Ok(addresses) = storage.load_autofill_addresses() {
            for (id, name, street, city, state, zip, country, phone, email, address_line2) in
                addresses
            {
                let addr = maho_types::autofill::AutofillAddress {
                    id,
                    name,
                    street,
                    city,
                    state,
                    zip,
                    country,
                    phone,
                    email,
                    address_line2,
                };
                self.autofill_manager.add_address(addr);
            }
        }

        // Load autofill payments
        if let Ok(payments) = storage.load_autofill_payments() {
            for (id, card_name, last_four, expiry, card_network) in payments {
                let payment = maho_types::autofill::AutofillPayment {
                    id,
                    card_name,
                    last_four,
                    expiry,
                    card_network,
                };
                self.autofill_manager.add_payment(payment);
            }
        }
        // Load profiles
        if let Ok(profile_jsons) = storage.get_all_profiles() {
            let profiles: Vec<maho_types::profile::ProfileConfig> = profile_jsons
                .iter()
                .filter_map(|j| serde_json::from_str(j).ok())
                .collect();
            if !profiles.is_empty() {
                let mut active_id = self.profile_manager.get_active_profile_id().cloned();
                if let Ok(Some(saved_id_str)) = storage.get_setting("active_profile_id") {
                    active_id = Some(maho_types::identifiers::ProfileId::new(saved_id_str));
                }
                self.profile_manager.restore_profiles(profiles, active_id);
            }
        }

        if let Ok(passwords) = storage.get_all_passwords() {
            for (id, domain, username, created_at, last_used, password) in passwords {
                self.password_manager
                    .restore_password(maho_types::passwords::SavedPassword {
                        id,
                        domain,
                        username,
                        created_at,
                        last_used,
                        password,
                    });
            }
        }

        // Load custom search engines from SQLite (skip built-in ones: google, duckduckgo, bing)
        if let Ok(engines) = storage.load_search_engines() {
            for (id, name, url_template, shortcut, icon_url, is_default) in engines {
                // Skip built-in engines as they are already initialized
                if id == "google"
                    || id == "duckduckgo"
                    || id == "bing"
                    || id == "brave"
                    || id == "ecosia"
                {
                    // Only update the is_default status for built-in engines
                    if is_default {
                        self.command_bar.set_default_search_engine(&id);
                    }
                    continue;
                }
                let engine = maho_types::search_engine::SearchEngine {
                    id,
                    name,
                    url_template,
                    shortcut,
                    icon_url,
                    is_default,
                };
                self.command_bar.add_search_engine(engine);
            }
        }

        // Load recent searches from search_history
        if let Ok(searches) = storage.load_recent_searches(20) {
            for (query, _searched_at) in searches.into_iter().rev() {
                self.command_bar.save_search(query);
            }
        }

        // Load reading list
        if let Ok(items) = storage.load_reading_list() {
            for (
                id,
                url,
                title,
                excerpt,
                site_name,
                favicon_url,
                preview_image_url,
                added_at,
                read_at,
                is_read,
                estimated_read_minutes,
                tags_json,
            ) in items
            {
                // Parse tags from JSON
                let tags: Vec<String> = tags_json
                    .and_then(|j| serde_json::from_str(&j).ok())
                    .unwrap_or_default();
                // Build ReadingListItem and insert into manager
                let item = crate::reading_list_manager::ReadingListItem {
                    id,
                    url,
                    title,
                    excerpt,
                    site_name,
                    favicon_url,
                    preview_image_url,
                    added_at: maho_types::common::DateTime(added_at),
                    read_at: read_at.map(maho_types::common::DateTime),
                    is_read,
                    estimated_read_minutes,
                    offline_content: None,
                    tags,
                };
                self.reading_list_manager.insert_item(item);
            }
        }

        // Load shortcuts
        if let Ok(shortcuts_data) = storage.load_shortcuts() {
            for (action, key_combo_json, enabled, updated_at) in shortcuts_data {
                if let Ok(key_combo) =
                    serde_json::from_str::<maho_types::keyboard::KeyCombo>(&key_combo_json)
                {
                    // Ignore conflicts during restore, restore full state
                    self.shortcut_manager
                        .restore_shortcut_state(&action, key_combo, enabled, updated_at);
                }
            }
        }

        // Load bookmark folders
        if let Ok(folders_data) = storage.load_bookmark_folders() {
            for (id, name, parent_id, created_at) in folders_data {
                let folder = crate::bookmark_manager::BookmarkFolder {
                    id,
                    name,
                    parent_id,
                    created_at: maho_types::common::DateTime(created_at),
                };
                self.bookmark_manager.restore_folder(folder);
            }
        }

        Ok(())
    }

    pub fn search_history(&self, query: &str, limit: usize) -> Vec<(String, String, String)> {
        if let Some(ref storage) = self.storage {
            storage.search_history(query, limit).unwrap_or_default()
        } else {
            Vec::new()
        }
    }

    pub fn search_passwords(&self, query: &str) -> Vec<&maho_types::passwords::SavedPassword> {
        self.password_manager.search_passwords(query)
    }

    pub fn list_passwords(&self) -> &[maho_types::passwords::SavedPassword] {
        self.password_manager.list_passwords()
    }

    fn assert_password_security(&self) {
        if self.storage.is_some() {
            assert!(
                maho_storage::sqlite::is_key_configured(),
                "SQLCipher key must be configured when storage is initialized"
            );
        }
    }

    pub fn password_by_id(&self, id: &str) -> Option<&maho_types::passwords::SavedPassword> {
        self.assert_password_security();
        self.password_manager.password_by_id(id)
    }

    pub fn delete_password_persisted(&mut self, password_id: &str) -> bool {
        self.assert_password_security();
        if self.password_manager.delete_password(password_id) {
            if let Some(ref storage) = self.storage {
                let _ = storage.delete_password(password_id);
            }
            return true;
        }
        false
    }

    pub fn add_password_persisted(
        &mut self,
        domain: String,
        username: String,
        password: Option<String>,
    ) -> maho_types::passwords::SavedPassword {
        self.assert_password_security();
        let added = self
            .password_manager
            .add_password(domain, username, password)
            .clone();
        if let Some(ref storage) = self.storage {
            let _ = storage.save_password(
                &added.id,
                &added.domain,
                &added.username,
                &added.created_at,
                added.password.as_deref(),
            );
        }
        added
    }

    pub fn update_password_username_persisted(&mut self, id: &str, username: String) -> bool {
        self.assert_password_security();
        if !self.password_manager.update_username(id, username) {
            return false;
        }

        if let Some(password) = self
            .password_manager
            .list_passwords()
            .iter()
            .find(|p| p.id == id)
        {
            if let Some(ref storage) = self.storage {
                let _ = storage.save_password(
                    &password.id,
                    &password.domain,
                    &password.username,
                    &password.created_at,
                    password.password.as_deref(),
                );
            }
        }

        true
    }

    /// Full command bar search using the CommandBarEngine.
    ///
    /// `external_tabs` provides the current tab list from the caller (e.g. C++
    /// TabStripModel). Spaces, bookmarks, archived tabs, closed tabs and
    /// extensions are sourced from internal MahoCore state.
    pub fn command_bar_search_with_tabs(
        &self,
        query: &str,
        mode: Option<&str>,
        external_tabs: Vec<TabViewModel>,
        is_incognito: bool,
    ) -> Vec<SuggestionViewModel> {
        let space_view_models = self.get_space_view_models();

        let archived_tabs: Vec<TabViewModel> = self
            .space_manager
            .get_space_order()
            .iter()
            .flat_map(|space_id| {
                self.tab_manager
                    .get_archived_tabs(space_id)
                    .into_iter()
                    .map(|tab| self.make_view_model(tab))
            })
            .collect();

        let bookmarks: Vec<(String, String, String)> = self
            .bookmark_manager
            .get_all_bookmarks()
            .iter()
            .map(|b| (b.id.0.clone(), b.title.clone(), b.url.clone()))
            .collect();

        let bookmark_favicons: Vec<(String, maho_types::common::ImageData)> = self
            .bookmark_manager
            .get_all_bookmarks()
            .iter()
            .filter_map(|b| {
                b.favicon.as_deref().and_then(|favicon| {
                    crate::command_bar::CommandBarEngine::bookmark_icon_from_data_url(favicon)
                        .map(|icon| (b.id.0.clone(), icon))
                })
            })
            .collect();

        let closed_tabs: Vec<TabViewModel> = self
            .tab_manager
            .get_closed_tabs()
            .into_iter()
            .map(|tab| self.make_view_model(tab))
            .collect();

        let extensions: Vec<(String, String)> = self
            .extension_bridge
            .get_installed_extensions()
            .iter()
            .filter(|ext| ext.enabled)
            .map(|ext| (ext.id.clone(), ext.name.clone()))
            .collect();

        let active_space_id = self.space_manager.get_active_space_id().clone();
        let folders = self.get_folder_view_models(&active_space_id);

        let recent_tabs: Vec<TabViewModel> = self
            .tab_manager
            .get_recent_tabs(12)
            .into_iter()
            .map(|tab| self.make_view_model(tab))
            .collect();

        let ctx = SearchContext {
            tabs: external_tabs,
            spaces: space_view_models,
            folders,
            archived_tabs,
            bookmarks,
            bookmark_favicons,
            closed_tabs,
            extensions,
            is_incognito,
            recent_tabs,
        };

        let mut results = self.command_bar.search(query, mode, &ctx);
        let bindings = self.shortcut_manager.get_all_bindings();

        for suggestion in &mut results {
            let Some(action_id) = suggestion.key.strip_prefix("action:") else {
                continue;
            };

            if let Some(binding) = bindings.iter().find(|b| b.action == action_id) {
                suggestion.shortcut = Some(format_key_combo_label(&binding.key_combo));
            }
        }

        results
    }

    fn favicon_for_url(&self, url: &str) -> Option<maho_types::common::ImageData> {
        self.tab_manager
            .get_all_tabs()
            .into_iter()
            .find(|tab| tab.url.0 == url)
            .and_then(|tab| tab.favicon.clone())
            .or_else(|| {
                self.space_manager
                    .get_space_order()
                    .iter()
                    .flat_map(|space_id| self.tab_manager.get_archived_tabs(space_id))
                    .find(|tab| tab.url.0 == url)
                    .and_then(|tab| tab.favicon.clone())
            })
    }

    fn active_tab_id_for_privacy_gate(&self) -> Option<TabId> {
        let active_space_id = self.space_manager.get_active_space_id();
        self.space_manager
            .get_last_active_tab(active_space_id)
            .cloned()
            .or_else(|| self.tab_manager.get_active_tab_id().cloned())
    }

    fn tab_id_is_private(&self, tab_id: &TabId) -> bool {
        self.tab_manager
            .get_tab(tab_id)
            .map(|tab| tab.is_private)
            .unwrap_or(false)
    }

    fn active_tab_is_private(&self) -> bool {
        self.active_tab_id_for_privacy_gate()
            .as_ref()
            .map(|tab_id| self.tab_id_is_private(tab_id))
            .unwrap_or(false)
    }

    pub fn add_history_entry(&mut self, url: &str, title: &str) {
        let active_tab_id = self.active_tab_id_for_privacy_gate();
        self.add_history_entry_for_tab(active_tab_id.as_ref(), url, title);
    }

    pub fn add_history_entry_for_tab(&mut self, tab_id: Option<&TabId>, url: &str, title: &str) {
        let is_private_context = tab_id
            .map(|id| self.tab_id_is_private(id))
            .unwrap_or_else(|| self.active_tab_is_private());
        if is_private_context {
            return;
        }

        self.command_bar.add_history_entry(
            url.to_string(),
            title.to_string(),
            self.favicon_for_url(url),
        );

        if let Some(ref storage) = self.storage {
            let _ = storage.add_history_entry(url, title);
        }
    }

    pub fn clear_history(&mut self) {
        self.command_bar.clear_history();
        if let Some(ref storage) = self.storage {
            let _ = storage.clear_history();
        }
    }

    pub fn delete_history_entry(&mut self, entry_id: &str) {
        if let Some(ref storage) = self.storage {
            // Try as database ID first (iOS passes numeric IDs)
            if let Ok(id) = entry_id.parse::<i64>() {
                let _ = storage.delete_history_entry(id);
            } else {
                // Fall back to URL-based deletion (macOS passes URLs)
                let _ = storage.delete_history_entry_by_url(entry_id);
            }
        }
    }

    pub fn add_bookmark(&mut self, url: &str, title: &str, folder_id: Option<&str>) {
        if self.active_tab_is_private() {
            return;
        }

        let bm = self.bookmark_manager.add_bookmark(
            title.to_string(),
            url.to_string(),
            folder_id.map(String::from),
            None,
        );
        let id_str = bm.id.0.clone();
        if let Some(ref storage) = self.storage {
            let _ = storage.save_bookmark(&id_str, url, title, folder_id, &bm.created_at.0);
        }
        if let Ok(payload) = serde_json::to_string(&bm) {
            self.push_sync_entity(SyncEntityType::Bookmark, id_str, payload, false);
        }
    }

    pub fn remove_bookmark(&mut self, bookmark_id: &str) {
        self.bookmark_manager.remove_bookmark(bookmark_id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_bookmark(bookmark_id);
        }
        self.push_sync_entity(
            SyncEntityType::Bookmark,
            bookmark_id.to_string(),
            "{}".to_string(),
            true,
        );
    }

    pub fn move_bookmark(&mut self, bookmark_id: &str, folder_id: Option<&str>) {
        self.bookmark_manager
            .move_bookmark(bookmark_id, folder_id.map(String::from));
        if let Some(ref storage) = self.storage {
            let _ = storage.move_bookmark(bookmark_id, folder_id);
        }
        if let Some(bm) = self
            .bookmark_manager
            .get_all_bookmarks()
            .iter()
            .find(|b| b.id.0 == bookmark_id)
            .cloned()
        {
            if let Ok(payload) = serde_json::to_string(&bm) {
                self.push_sync_entity(
                    SyncEntityType::Bookmark,
                    bookmark_id.to_string(),
                    payload,
                    false,
                );
            }
        }
    }

    pub fn search_bookmarks(
        &self,
        query: &str,
    ) -> Vec<(String, String, String, Option<String>, String)> {
        if let Some(ref storage) = self.storage {
            storage.search_bookmarks(query).unwrap_or_default()
        } else {
            Vec::new()
        }
    }

    pub fn grant_permission(&mut self, origin: &str, permission: &str) {
        self.permission_manager.set_permission(
            origin,
            permission,
            maho_types::settings::PermissionPolicy::Allow,
        );
        if let Some(ref storage) = self.storage {
            let _ = storage.save_permission(origin, permission, "allow");
        }
    }

    pub fn revoke_permission(&mut self, origin: &str, permission: &str) {
        self.permission_manager.set_permission(
            origin,
            permission,
            maho_types::settings::PermissionPolicy::Deny,
        );
        if let Some(ref storage) = self.storage {
            let _ = storage.save_permission(origin, permission, "deny");
        }
    }

    pub fn query_permission(&self, origin: &str, permission: &str) -> String {
        let policy = self.permission_manager.get_permission(origin, permission);
        format!("{:?}", policy).to_lowercase()
    }

    fn persist_note(&self, note: &Note) {
        if let Some(ref storage) = self.storage {
            let _ = storage.save_note(
                note.id.as_ref(),
                note.linked_tab_id.as_ref().map(|tab_id| tab_id.as_ref()),
                note.linked_url.as_ref().map(|url| url.as_ref()),
                &note.content,
                note.created_at.as_ref(),
                note.updated_at.as_ref(),
            );
        }
    }

    fn persist_profile(&self, profile: &maho_types::profile::ProfileConfig) {
        if let Some(ref storage) = self.storage {
            match serde_json::to_string(profile) {
                Ok(json) => {
                    if let Err(e) = storage.save_profile(profile.id.as_ref(), &json) {
                        eprintln!("Failed to persist profile {}: {}", profile.id, e);
                    }
                }
                Err(e) => eprintln!("Failed to serialize profile {}: {}", profile.id, e),
            }
        }
    }

    fn delete_persisted_profile(&self, id: &maho_types::identifiers::ProfileId) {
        if let Some(ref storage) = self.storage {
            if let Err(e) = storage.delete_profile(id.as_ref()) {
                eprintln!("Failed to delete persisted profile {}: {}", id, e);
            }
        }
    }

    fn persist_boost(&self, boost: &Boost) {
        if let Some(ref storage) = self.storage {
            let _ = storage.save_boost(boost);
        }
    }

    fn persist_traffic_rule(&self, rule: &maho_types::air_traffic::TrafficRule) {
        if let Some(ref storage) = self.storage {
            let _ = storage.save_atc_rule(
                &rule.id,
                &rule.url_pattern,
                Some(rule.target_space_id.as_ref()),
                None,
                None,
                None,
                rule.enabled,
                Some(rule.match_type.as_str()),
            );
        }
    }

    pub fn create_note_persisted(&mut self, linked_tab: Option<TabId>, content: String) -> Note {
        let note = self.note_manager.create_note(linked_tab, content);
        self.persist_note(&note);
        if let Ok(payload) = serde_json::to_string(&note) {
            self.push_sync_entity(
                SyncEntityType::Note,
                note.id.as_ref().to_string(),
                payload,
                false,
            );
        }
        note
    }

    pub fn update_note_persisted(&mut self, note_id: &NoteId, content: String) {
        self.note_manager.update_note(note_id, content);
        if let Some(note) = self.note_manager.get_note(note_id) {
            self.persist_note(note);
            if let Ok(payload) = serde_json::to_string(note) {
                self.push_sync_entity(
                    SyncEntityType::Note,
                    note_id.as_ref().to_string(),
                    payload,
                    false,
                );
            }
        }
    }

    pub fn delete_note_persisted(&mut self, note_id: &NoteId) -> Option<Note> {
        let note = self.note_manager.delete_note(note_id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_note(note_id.as_ref());
        }
        self.push_sync_entity(
            SyncEntityType::Note,
            note_id.as_ref().to_string(),
            "{}".to_string(),
            true,
        );
        note
    }

    pub fn create_boost_persisted(&mut self, domain: String, name: String) -> Boost {
        let boost = self.boost_manager.create(domain, name);
        self.persist_boost(&boost);
        if let Ok(payload) = serde_json::to_string(&boost) {
            self.push_sync_entity(
                SyncEntityType::Boost,
                boost.id.as_ref().to_string(),
                payload,
                false,
            );
        }
        boost
    }

    pub fn update_boost_persisted(&mut self, boost_id: &BoostId, changes: BoostUpdate) {
        self.boost_manager.update(boost_id, changes);
        if let Some(boost) = self.boost_manager.get_boost(boost_id) {
            self.persist_boost(boost);
            if let Ok(payload) = serde_json::to_string(boost) {
                self.push_sync_entity(
                    SyncEntityType::Boost,
                    boost_id.as_ref().to_string(),
                    payload,
                    false,
                );
            }
        }
    }

    pub fn delete_boost_persisted(&mut self, boost_id: &BoostId) -> Option<Boost> {
        let boost = self.boost_manager.delete(boost_id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_boost(boost_id.as_ref());
        }
        self.push_sync_entity(
            SyncEntityType::Boost,
            boost_id.as_ref().to_string(),
            "{}".to_string(),
            true,
        );
        boost
    }

    pub fn set_active_boost_persisted(&mut self, domain: &str, boost_id: Option<BoostId>) {
        self.boost_manager.set_active(domain, boost_id.clone());
        if let Some(ref storage) = self.storage {
            let _ = storage.set_active_boost(domain, boost_id.as_ref().map(|id| id.as_ref()));
        }
    }

    pub fn get_active_boost_for_domain(&self, domain: &str) -> Option<Boost> {
        self.boost_manager.get_active(domain)
    }

    pub fn list_boosts_for_domain(&self, domain: &str) -> Vec<Boost> {
        self.boost_manager.list_for_domain(domain)
    }

    pub fn get_boost_by_id(&self, boost_id: &BoostId) -> Option<Boost> {
        self.boost_manager.get_boost(boost_id).cloned()
    }

    pub fn compose_css_for_boost(
        &self,
        boost_id: &BoostId,
        workspace_gradient_hue_deg: Option<f64>,
    ) -> Option<String> {
        self.boost_manager.get_boost(boost_id).map(|b| {
            self.boost_manager
                .compose_css(b, workspace_gradient_hue_deg)
        })
    }

    pub fn create_temp_boost_persisted(&mut self, domain: String) -> Boost {
        let boost = self.boost_manager.create_temp(domain.clone());
        self.persist_boost(&boost);
        if let Some(ref storage) = self.storage {
            let _ = storage.set_active_boost(&domain, Some(boost.id.as_ref()));
        }
        boost
    }

    pub fn commit_boost_persisted(&mut self, boost_id: &BoostId) -> Option<Boost> {
        let boost = self.boost_manager.commit_boost(boost_id)?;
        self.persist_boost(&boost);
        Some(boost)
    }

    pub fn discard_boost_persisted(&mut self, boost_id: &BoostId) -> Option<BoostId> {
        let domain = self.boost_manager.get_boost(boost_id)?.domain.clone();
        let prev = self.boost_manager.discard_boost(boost_id)?;
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_boost(boost_id.as_ref());
            let _ = storage.set_active_boost(&domain, Some(prev.as_ref()));
        }
        Some(prev)
    }

    pub fn shuffle_boost_persisted(&mut self, boost_id: &BoostId) -> Option<Boost> {
        let boost = self.boost_manager.shuffle_boost(boost_id)?;
        self.persist_boost(&boost);
        Some(boost)
    }

    pub fn reset_boost_persisted(&mut self, boost_id: &BoostId) -> Option<Boost> {
        let boost = self.boost_manager.reset_boost(boost_id)?;
        self.persist_boost(&boost);
        Some(boost)
    }

    pub fn export_boost(&self, boost_id: &BoostId) -> Option<String> {
        self.boost_manager.export_boost(boost_id)
    }

    pub fn import_boost_persisted(&mut self, domain: String, json: &str) -> Option<Boost> {
        let boost = self.boost_manager.import_boost(domain, json)?;
        self.persist_boost(&boost);
        Some(boost)
    }

    pub fn append_zap_selector_persisted(
        &mut self,
        boost_id: &BoostId,
        selector: String,
    ) -> Option<Boost> {
        let boost = self.boost_manager.append_zap_selector(boost_id, selector)?;
        self.persist_boost(&boost);
        Some(boost)
    }

    pub fn remove_zap_selector_persisted(
        &mut self,
        boost_id: &BoostId,
        selector: &str,
    ) -> Option<Boost> {
        let boost = self.boost_manager.remove_zap_selector(boost_id, selector)?;
        self.persist_boost(&boost);
        Some(boost)
    }

    pub fn grant_permission_persisted(&mut self, origin: &str, permission: &str) {
        self.grant_permission(origin, permission);
    }

    pub fn revoke_permission_persisted(&mut self, origin: &str, permission: &str) {
        self.revoke_permission(origin, permission);
    }

    pub fn create_traffic_rule_persisted(
        &mut self,
        rule: maho_types::air_traffic::TrafficRule,
    ) -> maho_types::air_traffic::TrafficRule {
        let created = self.atc_manager.create_traffic_rule(rule);
        self.persist_traffic_rule(&created);
        created
    }

    pub fn update_traffic_rule_persisted(
        &mut self,
        rule: maho_types::air_traffic::TrafficRule,
    ) -> bool {
        let updated = self.atc_manager.update_traffic_rule(rule.clone());
        if updated {
            self.persist_traffic_rule(&rule);
        }
        updated
    }

    pub fn delete_traffic_rule_persisted(
        &mut self,
        rule_id: &str,
    ) -> Option<maho_types::air_traffic::TrafficRule> {
        let rule = self.atc_manager.delete_traffic_rule(rule_id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_atc_rule(rule_id);
        }
        rule
    }

    pub fn set_open_external_links_in_little_arc_persisted(&mut self, enabled: bool) {
        self.atc_manager.set_open_external_links_in_little_arc(enabled);
        if let Some(ref storage) = self.storage {
            let val_str = if enabled { "true" } else { "false" };
            let _ = storage.set_setting("open_external_links_in_little_arc", val_str);
        }
    }

    pub fn get_open_external_links_in_little_arc(&self) -> bool {
        self.atc_manager.get_open_external_links_in_little_arc()
    }

    pub fn get_default_link_behavior(&self) -> &maho_types::air_traffic::DefaultLinkBehavior {
        self.atc_manager.get_default_link_behavior()
    }

    pub fn decide_link_destination(
        &self,
        url: &str,
        is_external: bool,
        space_rules: &[(maho_types::identifiers::SpaceId, Vec<maho_types::space::ATCRule>)],
    ) -> maho_types::air_traffic::LinkDestination {
        self.atc_manager.decide_link_destination(url, is_external, space_rules)
    }

    pub fn add_autofill_address_persisted(
        &mut self,
        address: maho_types::autofill::AutofillAddress,
    ) -> maho_types::autofill::AutofillAddress {
        let added = self.autofill_manager.add_address(address).clone();
        if let Some(ref storage) = self.storage {
            let _ = storage.save_autofill_address(maho_storage::sqlite::AutofillAddressParams {
                id: &added.id,
                name: &added.name,
                street: &added.street,
                city: &added.city,
                state: &added.state,
                zip: &added.zip,
                country: &added.country,
                phone: added.phone.as_deref(),
                email: added.email.as_deref(),
                address_line2: added.address_line2.as_deref(),
            });
        }
        if let Ok(payload) = serde_json::to_string(&added) {
            self.push_sync_entity(
                SyncEntityType::AutofillAddress,
                added.id.clone(),
                payload,
                false,
            );
        }
        added
    }

    pub fn delete_autofill_address_persisted(&mut self, id: &str) -> bool {
        let deleted = self.autofill_manager.delete_address(id);
        if deleted {
            if let Some(ref storage) = self.storage {
                let _ = storage.delete_autofill_address(id);
            }
            self.push_sync_entity(
                SyncEntityType::AutofillAddress,
                id.to_string(),
                "{}".to_string(),
                true,
            );
        }
        deleted
    }

    pub fn add_autofill_payment_persisted(
        &mut self,
        payment: maho_types::autofill::AutofillPayment,
    ) -> maho_types::autofill::AutofillPayment {
        let added = self.autofill_manager.add_payment(payment).clone();
        if let Some(ref storage) = self.storage {
            let _ = storage.save_autofill_payment(
                &added.id,
                &added.card_name,
                &added.last_four,
                &added.expiry,
                added.card_network.as_deref(),
            );
        }
        if let Ok(payload) = serde_json::to_string(&added) {
            self.push_sync_entity(
                SyncEntityType::AutofillPayment,
                added.id.clone(),
                payload,
                false,
            );
        }
        added
    }

    pub fn delete_autofill_payment_persisted(&mut self, id: &str) -> bool {
        let deleted = self.autofill_manager.delete_payment(id);
        if deleted {
            if let Some(ref storage) = self.storage {
                let _ = storage.delete_autofill_payment(id);
            }
            self.push_sync_entity(
                SyncEntityType::AutofillPayment,
                id.to_string(),
                "{}".to_string(),
                true,
            );
        }
        deleted
    }

    pub fn get_autofill_addresses(&self) -> &[maho_types::autofill::AutofillAddress] {
        self.autofill_manager.list_addresses()
    }

    pub fn get_autofill_payments(&self) -> &[maho_types::autofill::AutofillPayment] {
        self.autofill_manager.list_payments()
    }

    // === Reading List Persisted Methods ===

    pub fn add_reading_list_item_persisted(
        &mut self,
        url: &str,
        title: &str,
    ) -> crate::reading_list_manager::ReadingListItem {
        let item = self.reading_list_manager.add_item(url, title);
        if let Some(ref storage) = self.storage {
            let tags_json = serde_json::to_string(&item.tags).unwrap_or_default();
            let _ = storage.save_reading_list_item(
                &item.id,
                &item.url,
                &item.title,
                item.excerpt.as_deref(),
                item.site_name.as_deref(),
                item.favicon_url.as_deref(),
                item.preview_image_url.as_deref(),
                &item.added_at.0,
                item.read_at.as_ref().map(|d| d.0.as_str()),
                item.is_read,
                item.estimated_read_minutes,
                Some(&tags_json),
            );
        }
        if let Ok(payload) = serde_json::to_string(&item) {
            self.push_sync_entity(
                SyncEntityType::ReadingListItem,
                item.id.clone(),
                payload,
                false,
            );
        }
        item
    }

    pub fn remove_reading_list_item_persisted(
        &mut self,
        id: &str,
    ) -> Option<crate::reading_list_manager::ReadingListItem> {
        let item = self.reading_list_manager.remove_item(id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_reading_list_item(id);
        }
        self.push_sync_entity(
            SyncEntityType::ReadingListItem,
            id.to_string(),
            "{}".to_string(),
            true,
        );
        item
    }

    pub fn mark_reading_list_read_persisted(&mut self, id: &str) -> bool {
        let result = self.reading_list_manager.mark_as_read(id);
        if result {
            if let Some(ref storage) = self.storage {
                let now = maho_types::common::DateTime::now();
                let _ = storage.update_reading_list_read_status(id, true, Some(&now.0));
            }
            if let Some(item) = self
                .reading_list_manager
                .get_all_items()
                .iter()
                .find(|i| i.id == id)
                .cloned()
            {
                if let Ok(payload) = serde_json::to_string(&item) {
                    self.push_sync_entity(
                        SyncEntityType::ReadingListItem,
                        id.to_string(),
                        payload,
                        false,
                    );
                }
            }
        }
        result
    }

    pub fn mark_reading_list_unread_persisted(&mut self, id: &str) -> bool {
        let result = self.reading_list_manager.mark_as_unread(id);
        if result {
            if let Some(ref storage) = self.storage {
                let _ = storage.update_reading_list_read_status(id, false, None);
            }
            if let Some(item) = self
                .reading_list_manager
                .get_all_items()
                .iter()
                .find(|i| i.id == id)
                .cloned()
            {
                if let Ok(payload) = serde_json::to_string(&item) {
                    self.push_sync_entity(
                        SyncEntityType::ReadingListItem,
                        id.to_string(),
                        payload,
                        false,
                    );
                }
            }
        }
        result
    }

    // === Easel Methods ===

    pub fn get_easel_view_models(&self) -> Vec<crate::easel_manager::EaselViewModel> {
        self.easel_manager.get_view_models()
    }

    pub fn get_easel(&self, id: &str) -> Option<&maho_types::easel::Easel> {
        let easel_id = maho_types::identifiers::EaselId::new(id);
        self.easel_manager.get_easel(&easel_id)
    }

    pub fn create_easel_persisted(&mut self, name: &str) -> maho_types::easel::Easel {
        let easel = self.easel_manager.create_easel(name.to_string());
        self.persist_easel(&easel);
        if let Ok(payload) = serde_json::to_string(&easel) {
            self.push_sync_entity(SyncEntityType::Easel, easel.id.to_string(), payload, false);
        }
        easel
    }

    pub fn delete_easel_persisted(&mut self, id: &str) -> bool {
        let easel_id = maho_types::identifiers::EaselId::new(id);
        let removed = self.easel_manager.delete_easel(&easel_id).is_some();
        if removed {
            if let Some(ref storage) = self.storage {
                let _ = storage.delete_easel(id);
            }
            self.push_sync_entity(
                SyncEntityType::Easel,
                id.to_string(),
                "{}".to_string(),
                true,
            );
        }
        removed
    }

    pub fn update_easel_persisted(&mut self, id: &str, json: &str) -> bool {
        let easel_id = maho_types::identifiers::EaselId::new(id);
        #[derive(serde::Deserialize)]
        struct EaselUpdate {
            name: Option<String>,
            canvas_items: Option<Vec<maho_types::easel::CanvasItem>>,
            viewport: Option<maho_types::common::Viewport>,
        }
        let update: EaselUpdate = match serde_json::from_str(json) {
            Ok(u) => u,
            Err(_) => {
                self.emit_error("update_easel", "INVALID_DATA", "Invalid easel update data");
                return false;
            }
        };
        let updated = self.easel_manager.update_easel(
            &easel_id,
            update.name,
            update.canvas_items,
            update.viewport,
        );
        if updated {
            if let Some(easel) = self.easel_manager.get_easel(&easel_id) {
                self.persist_easel(easel);
                if let Ok(payload) = serde_json::to_string(easel) {
                    self.push_sync_entity(SyncEntityType::Easel, id.to_string(), payload, false);
                }
            }
        } else {
            self.emit_error("update_easel", "EASEL_NOT_FOUND", "Easel not found");
        }
        updated
    }

    fn persist_easel(&self, easel: &maho_types::easel::Easel) {
        if let Some(ref storage) = self.storage {
            let _ = storage.save_easel(easel);
        }
    }

    pub fn create_shared_collection(&mut self, space_id: &str, name: &str) -> Option<String> {
        let collection = self.sharing_manager.create_collection(space_id, name);
        if let Some(ref storage) = self.storage {
            let _ = storage.save_shared_collection(&collection);
        }
        if let Ok(payload) = serde_json::to_string(&collection) {
            self.push_sync_entity(
                SyncEntityType::SharedCollection,
                collection.id.clone(),
                payload,
                false,
            );
        }
        match serde_json::to_string(&collection) {
            Ok(json) => Some(json),
            Err(_) => {
                self.emit_error(
                    "create_shared_collection",
                    "SERIALIZATION_FAILED",
                    "Failed to create shared collection",
                );
                None
            }
        }
    }

    pub fn get_share_link(&self, collection_id: &str) -> Option<String> {
        self.sharing_manager
            .get_share_link(collection_id)
            .map(|s| s.to_string())
    }

    pub fn update_share_permissions(&mut self, collection_id: &str, permission: &str) -> bool {
        let updated = self
            .sharing_manager
            .update_permissions(collection_id, permission);
        if updated {
            if let Some(ref storage) = self.storage {
                let _ = storage.update_shared_collection_permission(collection_id, permission);
            }
            if let Some(col) = self
                .sharing_manager
                .get_all_for_storage()
                .iter()
                .find(|c| c.id == collection_id)
                .cloned()
            {
                if let Ok(payload) = serde_json::to_string(col) {
                    self.push_sync_entity(
                        SyncEntityType::SharedCollection,
                        collection_id.to_string(),
                        payload,
                        false,
                    );
                }
            }
        } else {
            self.emit_error(
                "update_share_permissions",
                "SHARE_UPDATE_FAILED",
                "Failed to update share permissions",
            );
        }
        updated
    }

    pub fn revoke_share(&mut self, collection_id: &str) {
        self.sharing_manager.revoke_share(collection_id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_shared_collection(collection_id);
        }
        self.push_sync_entity(
            SyncEntityType::SharedCollection,
            collection_id.to_string(),
            "{}".to_string(),
            true,
        );
    }

    pub fn get_shared_collections(&self) -> Option<String> {
        let vms = self.sharing_manager.get_all_collections();
        serde_json::to_string(&vms).ok()
    }

    pub fn join_shared_collection(&mut self, share_link: &str) -> bool {
        let joined = self.sharing_manager.join_collection(share_link);
        if joined {
            if let Some(id) = share_link.split('/').next_back() {
                if let Some(col) = self
                    .sharing_manager
                    .get_all_for_storage()
                    .iter()
                    .find(|c| c.id == id)
                    .cloned()
                {
                    if let Some(ref storage) = self.storage {
                        let _ = storage.save_shared_collection(col);
                    }
                    if let Ok(payload) = serde_json::to_string(col) {
                        self.push_sync_entity(
                            SyncEntityType::SharedCollection,
                            id.to_string(),
                            payload,
                            false,
                        );
                    }
                }
            }
        } else {
            self.emit_error(
                "join_shared_collection",
                "JOIN_FAILED",
                "Failed to join shared collection",
            );
        }
        joined
    }

    fn save_download_to_storage(&self, download_id: &str) {
        if let Some(ref storage) = self.storage {
            let id = maho_types::identifiers::DownloadId::new(download_id);
            if let Some(download) = self.download_manager.get_download(&id) {
                let state_str = match download.state {
                    DownloadState::Downloading => "downloading",
                    DownloadState::Paused => "paused",
                    DownloadState::Completed => "completed",
                    DownloadState::Failed => "failed",
                    DownloadState::Cancelled => "cancelled",
                };
                let _ = storage.save_download(
                    download.id.as_ref(),
                    &download.filename,
                    &download.url,
                    download.total_bytes,
                    download.received_bytes,
                    state_str,
                    download.file_path.as_deref(),
                    download.started_at.as_ref(),
                    download.completed_at.as_ref().map(|dt| dt.as_ref()),
                    download.mime_type.as_deref(),
                    download.chromium_guid.as_deref(),
                );
            }
        }
    }

    pub fn pause_download(&mut self, download_id: &str) {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager.pause_download(&id);
        self.save_download_to_storage(download_id);
    }

    pub fn resume_download(&mut self, download_id: &str) {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager.resume_download(&id);
        self.save_download_to_storage(download_id);
    }

    pub fn cancel_download(&mut self, download_id: &str) {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager.cancel_download(&id);
        self.save_download_to_storage(download_id);
        // Clean up throttle tracking entry — download is terminal.
        self.download_progress_last_persisted.remove(download_id);
    }

    pub fn remove_download(&mut self, download_id: &str) {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager.remove_download(&id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_download(download_id);
        }
        // Clean up throttle tracking entry to prevent unbounded growth.
        self.download_progress_last_persisted.remove(download_id);
    }

    pub fn start_download(
        &mut self,
        filename: &str,
        url: &str,
        total_bytes: u64,
        file_path: Option<&str>,
        mime_type: Option<&str>,
        chromium_guid: Option<&str>,
    ) -> String {
        let id = self.download_manager.start_download(
            filename.to_string(),
            url.to_string(),
            total_bytes,
            file_path.map(String::from),
            mime_type.map(String::from),
            chromium_guid.map(String::from),
        );
        let id_str = id.to_string();
        self.save_download_to_storage(&id_str);
        id_str
    }

    pub fn find_download_by_chromium_guid(&self, guid: &str) -> Option<String> {
        self.download_manager
            .find_by_chromium_guid(guid)
            .map(|d| d.id.to_string())
    }

    pub fn update_download_progress(&mut self, download_id: &str, received_bytes: u64) {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager.update_progress(&id, received_bytes);
        // B3: Throttle SQLite progress writes to every 512 KB to avoid disk write storms
        // on large/fast downloads. State transitions (complete/fail/cancel) always write
        // immediately via save_download_to_storage.
        const DOWNLOAD_PERSIST_INTERVAL_BYTES: u64 = 512 * 1024;
        let last = self
            .download_progress_last_persisted
            .get(download_id)
            .copied()
            .unwrap_or(0);
        if received_bytes.saturating_sub(last) >= DOWNLOAD_PERSIST_INTERVAL_BYTES {
            if let Some(ref storage) = self.storage {
                if let Some(download) = self.download_manager.get_download(&id) {
                    let state_str = match download.state {
                        DownloadState::Downloading => "downloading",
                        DownloadState::Paused => "paused",
                        DownloadState::Completed => "completed",
                        DownloadState::Failed => "failed",
                        DownloadState::Cancelled => "cancelled",
                    };
                    let _ = storage.update_download_progress(
                        download_id,
                        received_bytes,
                        state_str,
                    );
                }
            }
            self.download_progress_last_persisted
                .insert(download_id.to_string(), received_bytes);
        }
    }

    pub fn update_download_metadata(
        &mut self,
        download_id: &str,
        filename: Option<&str>,
        file_path: Option<&str>,
        mime_type: Option<&str>,
        total_bytes: Option<u64>,
    ) {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager.update_metadata(
            &id,
            filename.map(String::from),
            file_path.map(String::from),
            mime_type.map(String::from),
            total_bytes,
        );
        self.save_download_to_storage(download_id);
    }

    pub fn fail_download(&mut self, download_id: &str, error: &str) {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager.fail_download(&id, error.to_string());
        self.save_download_to_storage(download_id);
        // Clean up throttle tracking entry — download is terminal.
        self.download_progress_last_persisted.remove(download_id);
    }

    pub fn complete_download(&mut self, download_id: &str) -> Vec<CoreUpdate> {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager.complete_download(&id);
        self.save_download_to_storage(download_id);
        // Clean up throttle tracking entry — download is terminal.
        self.download_progress_last_persisted.remove(download_id);
        let mut updates = vec![CoreUpdate::DownloadCompleted {
            download_id: id.clone(),
        }];
        if self.settings_manager.get_settings().max.tidy_downloads {
            if let Some(download) = self.download_manager.get_download(&id) {
                let llm_update = self.llm_manager.request_tidy_download(
                    download.id.as_ref(),
                    &download.filename,
                    &download.url,
                    "",
                );
                updates.push(llm_update);
            }
        }
        updates
    }

    pub fn restore_download_name(&mut self, download_id: &str) -> Option<CoreUpdate> {
        let id = maho_types::identifiers::DownloadId::new(download_id);
        self.download_manager
            .restore_download_name(&id)
            .map(|(old_name, new_name)| CoreUpdate::DownloadRenamed {
                download_id: id,
                old_name,
                new_name,
            })
    }

    pub fn set_zoom(&mut self, _tab_id: &maho_types::identifiers::TabId, _zoom_level: f64) {}

    pub fn get_split_view_config(&self, window_id: &WindowId) -> Option<SplitViewConfig> {
        self.split_view_configs.get(window_id).cloned()
    }

    pub fn clear_split_view_config(&mut self, window_id: &WindowId) {
        self.split_view_configs.remove(window_id);
    }

    fn set_split_view_config(&mut self, window_id: WindowId, config: SplitConfig) {
        if let Some(cfg) = config.into_split_view_config() {
            self.split_view_configs.insert(window_id, cfg);
        } else {
            self.split_view_configs.remove(&window_id);
        }
    }

    pub fn window_closed(&mut self, window_id: &WindowId) {
        self.split_view_configs.remove(window_id);
    }

    fn remove_split_view_pane(
        &mut self,
        window_id: &WindowId,
        pane_id: &str,
    ) -> Option<SplitViewConfig> {
        let mut config = self.split_view_configs.get(window_id)?.clone();
        if let Some(pos) = config.panes.iter().position(|p| p.tab_id.0 == pane_id) {
            config.panes.remove(pos);
            if pos < config.ratios.len() {
                config.ratios.remove(pos);
            }
            if config.panes.len() < 2 {
                self.split_view_configs.remove(window_id);
                None
            } else {
                let sum: f64 = config.ratios.iter().sum();
                if sum > 0.0 {
                    for r in &mut config.ratios {
                        *r /= sum;
                    }
                } else {
                    config.ratios = vec![1.0 / config.panes.len() as f64; config.panes.len()];
                }
                self.split_view_configs
                    .insert(window_id.clone(), config.clone());
                Some(config)
            }
        } else {
            Some(config)
        }
    }

    fn resize_split_view_pane(
        &mut self,
        window_id: &WindowId,
        pane_id: &str,
        ratio: f64,
    ) -> Option<SplitViewConfig> {
        let mut config = self.split_view_configs.get(window_id)?.clone();
        if let Some(pos) = config.panes.iter().position(|p| p.tab_id.0 == pane_id) {
            if config.panes.len() > 1 && pos < config.ratios.len() {
                let old_ratio = config.ratios[pos];
                config.ratios[pos] = ratio;
                let remaining_new = 1.0 - ratio;
                let remaining_old = 1.0 - old_ratio;
                if remaining_old > 0.0 && remaining_new >= 0.0 {
                    let scale = remaining_new / remaining_old;
                    for (i, r) in config.ratios.iter_mut().enumerate() {
                        if i != pos {
                            *r *= scale;
                        }
                    }
                } else {
                    let rest_count = config.panes.len() - 1;
                    let rest_val = remaining_new / rest_count as f64;
                    for (i, r) in config.ratios.iter_mut().enumerate() {
                        if i != pos {
                            *r = rest_val;
                        }
                    }
                }
                self.split_view_configs
                    .insert(window_id.clone(), config.clone());
                Some(config)
            } else {
                Some(config)
            }
        } else {
            Some(config)
        }
    }

    pub fn handle_event(&mut self, event: ShellEvent) -> Vec<CoreUpdate> {
        let dirty_bits = Self::event_dirty_flags(&event);
        let urgent = Self::event_persist_is_urgent(&event);
        let event_cloned = event.clone();
        self.emitted_during_dispatch.borrow_mut().clear();
        let result = self.handle_event_inner(event);
        if dirty_bits != 0 {
            if urgent {
                self.persist_lmdb_now_partial(dirty_bits);
            } else {
                self.persist_throttle.borrow_mut().pending_bits |= dirty_bits;
            }
        }
        self.post_dispatch_sync(&event_cloned, &result);

        let emitted = self.emitted_during_dispatch.borrow().clone();
        let emitted_strs: std::collections::HashSet<String> = emitted
            .iter()
            .filter_map(|e| serde_json::to_string(e).ok())
            .collect();
        let filtered: Vec<CoreUpdate> = result
            .iter()
            .filter(|u| {
                if let Ok(u_str) = serde_json::to_string(u) {
                    !emitted_strs.contains(&u_str)
                } else {
                    true
                }
            })
            .cloned()
            .collect();
        self.emit_updates(filtered);

        result
    }

    fn post_dispatch_sync(&mut self, event: &ShellEvent, result: &[CoreUpdate]) {
        for update in result {
            match update {
                CoreUpdate::TabCreated { tab } => {
                    self.push_tab_sync(&tab.id);
                }
                CoreUpdate::TabUpdated { tab_id, .. } => {
                    self.push_tab_sync(tab_id);
                }
                CoreUpdate::TabClosed { tab_id, .. } => {
                    self.push_sync_entity(
                        SyncEntityType::Tab,
                        tab_id.to_string(),
                        "{}".to_string(),
                        true,
                    );
                }
                CoreUpdate::SpaceCreated { space } => {
                    self.push_space_sync(&space.id);
                }
                CoreUpdate::SpaceUpdated { space_id, .. } => {
                    self.push_space_sync(space_id);
                }
                CoreUpdate::SpaceDeleted { space_id } => {
                    self.push_sync_entity(
                        SyncEntityType::Space,
                        space_id.to_string(),
                        "{}".to_string(),
                        true,
                    );
                }
                CoreUpdate::ActiveSpaceChanged { space_id, .. } => {
                    self.push_space_sync(space_id);
                }
                CoreUpdate::SpaceConfigUpdated { space_id, .. } => {
                    self.push_space_sync(space_id);
                }
                CoreUpdate::TabOrderChanged { space_id, .. } => {
                    self.push_space_sync(space_id);
                }
                CoreUpdate::SpaceOrderChanged { order } => {
                    for space_id in order {
                        self.push_space_sync(space_id);
                    }
                }
                CoreUpdate::FolderCreated { folder } => {
                    let all_spaces: Vec<SpaceId> = self
                        .space_manager
                        .get_all_spaces()
                        .iter()
                        .map(|s| s.id.clone())
                        .collect();
                    for sid in all_spaces {
                        if let Some(space) = self.space_manager.get_space(&sid) {
                            if space.folders.iter().any(|f| f.id == folder.id) {
                                self.push_space_sync(&sid);
                                break;
                            }
                        }
                    }
                }
                CoreUpdate::FolderUpdated { folder_id, .. } => {
                    let all_spaces: Vec<SpaceId> = self
                        .space_manager
                        .get_all_spaces()
                        .iter()
                        .map(|s| s.id.clone())
                        .collect();
                    for sid in all_spaces {
                        if let Some(space) = self.space_manager.get_space(&sid) {
                            if space.folders.iter().any(|f| f.id == *folder_id) {
                                self.push_space_sync(&sid);
                                break;
                            }
                        }
                    }
                }
                _ => {}
            }
        }

        match event {
            ShellEvent::PinTab { tab_id }
            | ShellEvent::UnpinTab { tab_id }
            | ShellEvent::MuteTab { tab_id }
            | ShellEvent::UnmuteTab { tab_id }
            | ShellEvent::FreezeTab { tab_id }
            | ShellEvent::UnfreezeTab { tab_id }
            | ShellEvent::SuspendTab { tab_id }
            | ShellEvent::SetZoom { tab_id, .. }
            | ShellEvent::ResetZoom { tab_id }
            | ShellEvent::SetTabCustomTitle { tab_id, .. }
            | ShellEvent::ResetPinnedTab { tab_id }
            | ShellEvent::SetTabParent { tab_id, .. } => {
                self.push_tab_sync(tab_id);
            }
            ShellEvent::MoveTab { tab_id, .. } => {
                self.push_tab_sync(tab_id);
                if let Some(space_id) = self.tab_manager.get_tab(tab_id).map(|t| t.space_id.clone())
                {
                    self.push_space_sync(&space_id);
                }
            }
            ShellEvent::MoveTabToSpace {
                tab_id,
                target_space_id,
                ..
            } => {
                self.push_tab_sync(tab_id);
                self.push_space_sync(target_space_id);
            }
            ShellEvent::MoveTabToRoot {
                tab_id, space_id, ..
            }
            | ShellEvent::MoveTabToFolder {
                tab_id, space_id, ..
            }
            | ShellEvent::RemoveTabFromFolder {
                tab_id, space_id, ..
            } => {
                self.push_tab_sync(tab_id);
                self.push_space_sync(space_id);
            }
            ShellEvent::CreateFolder { space_id, .. }
            | ShellEvent::RenameFolder { space_id, .. }
            | ShellEvent::DeleteFolder { space_id, .. }
            | ShellEvent::SetFolderPinned { space_id, .. }
            | ShellEvent::ReorderFolder { space_id, .. }
            | ShellEvent::ReorderTabInFolder { space_id, .. }
            | ShellEvent::ReorderRootItem { space_id, .. }
            | ShellEvent::ApplyTidyTabs { space_id, .. } => {
                self.push_space_sync(space_id);
            }
            _ => {}
        }
    }

    // ActivateTab fires on every sidebar click and only mutates a timestamp on
    // one tab and one space field. Persisting all tabs + spaces synchronously
    // on the caller thread dominates click latency with hundreds of tabs.
    // Deferring lets the bits accumulate and flush with the next structural
    // event or shutdown flush.
    fn event_persist_is_urgent(event: &ShellEvent) -> bool {
        !matches!(event, ShellEvent::ActivateTab { .. })
    }

    fn event_dirty_flags(event: &ShellEvent) -> u8 {
        match event {
            ShellEvent::TabTitleUpdated { .. }
            | ShellEvent::TabUrlUpdated { .. }
            | ShellEvent::TabFaviconUpdated { .. }
            | ShellEvent::NavigateTo { .. }
            | ShellEvent::GoBack { .. }
            | ShellEvent::GoForward { .. }
            | ShellEvent::SetZoom { .. }
            | ShellEvent::ResetZoom { .. }
            | ShellEvent::MuteTab { .. }
            | ShellEvent::UnmuteTab { .. }
            | ShellEvent::FreezeTab { .. }
            | ShellEvent::UnfreezeTab { .. }
            | ShellEvent::SuspendTab { .. }
            | ShellEvent::ArchiveTabById { .. }
            | ShellEvent::FavoriteTab { .. }
            | ShellEvent::ChangeTabRole { .. }
            | ShellEvent::UnpinTab { .. }
            | ShellEvent::SetTabCustomTitle { .. }
            | ShellEvent::ResetPinnedTab { .. }
            | ShellEvent::SetTabParent { .. } => LMDB_DIRTY_TABS,

            ShellEvent::CreateTab { .. }
            | ShellEvent::CloseTab { .. }
            | ShellEvent::ActivateTab { .. }
            | ShellEvent::CloseOtherTabs { .. }
            | ShellEvent::CloseTabsToRight { .. }
            | ShellEvent::CloseTabsToLeft { .. }
            | ShellEvent::ReopenLastClosed
            | ShellEvent::DuplicateTab { .. }
            | ShellEvent::RestoreArchivedTab { .. }
            | ShellEvent::DeleteArchivedTab { .. }
            | ShellEvent::PinTab { .. }
            | ShellEvent::ReorderTab { .. }
            | ShellEvent::ReorderFavorite { .. }
            | ShellEvent::MoveTab { .. }
            | ShellEvent::MoveTabToSpace { .. }
            | ShellEvent::MoveTabToRoot { .. }
            | ShellEvent::MoveTabToFolder { .. }
            | ShellEvent::RemoveTabFromFolder { .. }
            | ShellEvent::CreateFolder { .. }
            | ShellEvent::RenameFolder { .. }
            | ShellEvent::DeleteFolder { .. }
            | ShellEvent::SetFolderPinned { .. }
            | ShellEvent::ConvertFolderToSpace { .. }
            | ShellEvent::MoveFolderIntoFolder { .. }
            | ShellEvent::MoveFolderToRoot { .. }
            | ShellEvent::ReorderFolder { .. }
            | ShellEvent::ReorderTabInFolder { .. }
            | ShellEvent::ReorderRootItem { .. }
            | ShellEvent::ApplyTidyTabs { .. } => LMDB_DIRTY_TABS | LMDB_DIRTY_SPACES,

            ShellEvent::CreateSpace { .. } | ShellEvent::DeleteSpace { .. } => {
                LMDB_DIRTY_SPACES | LMDB_DIRTY_ACTIVE_SPACE
            }

            ShellEvent::RenameSpace { .. }
            | ShellEvent::RecolorSpace { .. }
            | ShellEvent::ReorderSpace { .. }
            | ShellEvent::UpdateSpaceConfig { .. }
            | ShellEvent::ExportSpaceIntoFolder { .. } => LMDB_DIRTY_SPACES,

            ShellEvent::ActivateSpace { .. } => LMDB_DIRTY_ACTIVE_SPACE,

            ShellEvent::CreateProfile { .. } | ShellEvent::DeleteProfile { .. } => {
                LMDB_DIRTY_PROFILES | LMDB_DIRTY_ACTIVE_PROFILE
            }
            ShellEvent::UpdateProfile { .. } => LMDB_DIRTY_PROFILES,

            ShellEvent::SwitchProfile { .. } => LMDB_DIRTY_ACTIVE_PROFILE,

            ShellEvent::SignIn { .. } | ShellEvent::SignOut | ShellEvent::ToggleSync => {
                LMDB_DIRTY_ACCOUNT
            }

            ShellEvent::ClearSplitView { .. }
            | ShellEvent::CreateSplit { .. }
            | ShellEvent::RemoveSplit { .. }
            | ShellEvent::ResizeSplit { .. } => LMDB_DIRTY_SPLIT_VIEW,

            _ => 0,
        }
    }

    fn handle_event_inner(&mut self, event: ShellEvent) -> Vec<CoreUpdate> {
        if let ShellEvent::CreateTab {
            tab_id: Some(explicit_id),
            window_id,
            is_private,
            ..
        } = &event
        {
            if let Some(mut tab) = self.tab_manager.get_tab(explicit_id).cloned() {
                if tab.window_id != *window_id || tab.is_private != *is_private {
                    tab.window_id = *window_id;
                    tab.is_private = *is_private;
                    self.tab_manager.restore_tab(tab);

                    if let Some(w_id) = window_id {
                        let new_win_key = WindowId::new(w_id.to_string());
                        let mut found_old_key = None;
                        let mut found_config = None;
                        for (win_key, cfg) in &self.split_view_configs {
                            if cfg.panes.iter().any(|p| p.tab_id == *explicit_id) {
                                found_old_key = Some(win_key.clone());
                                found_config = Some(cfg.clone());
                                break;
                            }
                        }
                        if let (Some(old_key), Some(cfg)) = (found_old_key, found_config) {
                            if old_key != new_win_key {
                                self.split_view_configs.remove(&old_key);
                                self.split_view_configs.insert(new_win_key, cfg);
                            }
                        }
                    }
                }
            }
        }

        // Handle command bar mutation events before dispatch (dispatcher only has read-only access)
        match &event {
            ShellEvent::ClearSplitView { window_id } => {
                self.clear_split_view_config(window_id);
                return vec![CoreUpdate::SplitViewChanged {
                    window_id: window_id.clone(),
                    config: None,
                    schema_version: SPLIT_VIEW_SCHEMA_VERSION,
                }];
            }
            ShellEvent::CreateSplit {
                window_id,
                tab_ids,
                orientation,
            } => {
                self.set_split_view_config(
                    window_id.clone(),
                    SplitConfig {
                        tab_ids: tab_ids.clone(),
                        orientation: orientation.clone(),
                    },
                );
                let config = self.get_split_view_config(window_id);
                return vec![CoreUpdate::SplitViewChanged {
                    window_id: window_id.clone(),
                    config,
                    schema_version: SPLIT_VIEW_SCHEMA_VERSION,
                }];
            }
            ShellEvent::RemoveSplit { window_id, pane_id } => {
                let config = self.remove_split_view_pane(window_id, pane_id);
                return vec![CoreUpdate::SplitViewChanged {
                    window_id: window_id.clone(),
                    config,
                    schema_version: SPLIT_VIEW_SCHEMA_VERSION,
                }];
            }
            ShellEvent::ResizeSplit {
                window_id,
                pane_id,
                ratio,
            } => {
                let config = self.resize_split_view_pane(window_id, pane_id, *ratio);
                return vec![CoreUpdate::SplitViewChanged {
                    window_id: window_id.clone(),
                    config,
                    schema_version: SPLIT_VIEW_SCHEMA_VERSION,
                }];
            }
            ShellEvent::SaveSearch { query } => {
                self.save_search_persisted(query.clone());
                return vec![CoreUpdate::RecentSearchesUpdated {
                    searches: self.command_bar.get_recent_searches().to_vec(),
                }];
            }
            ShellEvent::CommandBarSelect { key, .. } => {
                self.record_command_bar_usage(key);
                return vec![];
            }
            ShellEvent::AddSearchEngine { engine } => {
                self.add_search_engine_persisted(engine.clone());
                return vec![CoreUpdate::SearchEnginesUpdated {
                    engines: self.command_bar.get_search_engines(),
                }];
            }
            ShellEvent::RemoveSearchEngine { id } => {
                self.remove_search_engine_persisted(id);
                return vec![CoreUpdate::SearchEnginesUpdated {
                    engines: self.command_bar.get_search_engines(),
                }];
            }
            ShellEvent::SetDefaultSearchEngine { id } => {
                if self.set_default_search_engine_persisted(id) {
                    return vec![CoreUpdate::SearchEnginesUpdated {
                        engines: self.command_bar.get_search_engines(),
                    }];
                }
                return vec![Self::make_error_update(
                    "set_default_search_engine",
                    "ENGINE_NOT_FOUND",
                    "Search engine not found",
                )];
            }
            ShellEvent::AppWillTerminate => {
                self.persist_lmdb_force_flush();
                return vec![];
            }
            ShellEvent::CreateProfile { name } => {
                if let Err(e) = self.create_profile_persisted(name.clone()) {
                    eprintln!("[Maho] CreateProfile failed for '{name}': {e}");
                }
                return vec![];
            }
            ShellEvent::DeleteProfile { profile_id } => {
                self.delete_profile(profile_id);
                return vec![];
            }
            ShellEvent::UpdateProfile {
                profile_id,
                name,
                avatar_color,
                download_path,
                archive_timeout_hours,
            } => {
                if let Some(profile) = self.update_profile_persisted(
                    profile_id,
                    name.clone(),
                    avatar_color.clone(),
                    download_path.clone(),
                    archive_timeout_hours.map(|hours| Some(hours as f64)),
                ) {
                    return vec![CoreUpdate::ProfileUpdated { profile }];
                }
                return vec![Self::make_error_update(
                    "update_profile",
                    "PROFILE_NOT_FOUND",
                    "Profile not found",
                )];
            }
            ShellEvent::SwitchProfile { profile_id } => {
                if self.profile_manager.switch_profile(profile_id) {
                    return vec![CoreUpdate::ActiveProfileChanged {
                        profile_id: profile_id.clone(),
                    }];
                }
                return vec![Self::make_error_update(
                    "switch_profile",
                    "PROFILE_NOT_FOUND",
                    "Profile not found",
                )];
            }

            ShellEvent::ToggleExtension { extension_id } => {
                if let Some(enabled) = self.extension_bridge.toggle_extension(extension_id) {
                    return vec![CoreUpdate::ExtensionToggled {
                        extension_id: extension_id.clone(),
                        enabled,
                    }];
                }
                return vec![Self::make_error_update(
                    "toggle_extension",
                    "EXTENSION_NOT_FOUND",
                    "Extension not found",
                )];
            }
            ShellEvent::RemoveExtension { extension_id } => {
                if self
                    .extension_bridge
                    .remove_extension(extension_id)
                    .is_some()
                {
                    return vec![CoreUpdate::ExtensionRemoved {
                        extension_id: extension_id.clone(),
                    }];
                }
                return vec![Self::make_error_update(
                    "remove_extension",
                    "EXTENSION_NOT_FOUND",
                    "Extension not found",
                )];
            }
            ShellEvent::SignIn {
                email,
                password: _,
                display_name,
                access_token,
                user_id,
                device_id,
            } => {
                let mut account = self.account_manager.begin_sign_in(
                    email.clone(),
                    display_name.clone(),
                    user_id.clone(),
                    device_id.clone(),
                );
                if let Some(ref tok) = access_token {
                    account.auth_state = AuthState::Authenticated;
                    self.account_manager
                        .set_auth_state(AuthState::Authenticated);
                    if let Some(ref storage) = self.storage {
                        if let Err(e) = storage.set_setting("auth:access_token", tok) {
                            eprintln!(
                                "[auth] Failed to persist access_token; sync snapshot upload/download will fail: {}",
                                e
                            );
                        }
                    }
                }
                return vec![
                    CoreUpdate::AccountChanged {
                        account: Some(account),
                    },
                    CoreUpdate::AccountAuthStateChanged {
                        auth_state: self.account_manager.get_auth_state(),
                    },
                ];
            }
            ShellEvent::SignOut => {
                self.account_manager.sign_out();
                if let Some(ref storage) = self.storage {
                    if let Err(e) = storage.set_setting("auth:access_token", "") {
                        eprintln!(
                            "[auth] Failed to clear access_token on sign-out; stale token may remain in storage: {}",
                            e
                        );
                    }
                    let _ = storage.delete_all_memories();
                    let _ = self.memory_manager.rebuild_index(storage);
                }
                return vec![
                    CoreUpdate::AccountChanged { account: None },
                    CoreUpdate::AccountAuthStateChanged {
                        auth_state: self.account_manager.get_auth_state(),
                    },
                ];
            }
            ShellEvent::ToggleSync => {
                let account = self.account_manager.toggle_sync();
                return vec![CoreUpdate::AccountChanged { account }];
            }
            ShellEvent::CreateBoost { domain } => {
                let name = format!("{} Boost", domain);
                let boost = self.create_boost_persisted(domain.clone(), name);
                return vec![CoreUpdate::BoostCreated { boost }];
            }
            ShellEvent::UpdateBoost { boost_id, changes } => {
                self.update_boost_persisted(boost_id, changes.clone());
                if let Some(boost) = self.boost_manager.get_boost(boost_id).cloned() {
                    let boost_id = boost_id.clone();
                    return vec![CoreUpdate::BoostUpdated { boost_id, boost }];
                }
                return vec![];
            }
            ShellEvent::SetActiveBoost { domain, boost_id } => {
                self.set_active_boost_persisted(domain, boost_id.clone());
                return vec![CoreUpdate::BoostActiveChanged {
                    domain: domain.clone(),
                    active_boost_id: boost_id.clone(),
                }];
            }
            ShellEvent::DeleteBoost { boost_id } => {
                if let Some(deleted) = self.delete_boost_persisted(boost_id) {
                    return vec![CoreUpdate::BoostDeleted {
                        boost_id: boost_id.clone(),
                        domain: deleted.domain,
                    }];
                }
                return vec![];
            }
            ShellEvent::InstallCssMod {
                name,
                css,
                description,
                author,
                version,
                homepage,
                source_url,
            } => {
                let now = chrono::Utc::now().to_rfc3339();
                let css_mod = maho_types::css_mod::CssMod {
                    id: uuid::Uuid::new_v4().to_string(),
                    name: name.clone(),
                    description: description.clone(),
                    author: author.clone(),
                    version: version.clone(),
                    css: css.clone(),
                    enabled: true,
                    homepage: homepage.clone(),
                    source_url: source_url.clone(),
                    created_at: now.clone(),
                    updated_at: now,
                };
                let _id = self.css_mod_manager.install_mod(css_mod.clone());
                if let Some(storage) = &self.storage {
                    let _ = storage.save_css_mod(&css_mod);
                }
                if let Ok(payload) = serde_json::to_string(&css_mod) {
                    self.push_sync_entity(
                        SyncEntityType::CssMod,
                        css_mod.id.clone(),
                        payload,
                        false,
                    );
                }
                return vec![];
            }
            ShellEvent::ToggleCssMod { mod_id } => {
                if self.css_mod_manager.toggle_mod(mod_id).is_some() {
                    if let Some(m) = self.css_mod_manager.get_mod(mod_id) {
                        if let Some(storage) = &self.storage {
                            let _ = storage.save_css_mod(m);
                        }
                        if let Ok(payload) = serde_json::to_string(m) {
                            self.push_sync_entity(
                                SyncEntityType::CssMod,
                                mod_id.clone(),
                                payload,
                                false,
                            );
                        }
                    }
                }
                return vec![];
            }
            ShellEvent::UninstallCssMod { mod_id } => {
                self.css_mod_manager.uninstall_mod(mod_id);
                if let Some(storage) = &self.storage {
                    let _ = storage.delete_css_mod(mod_id);
                }
                self.push_sync_entity(
                    SyncEntityType::CssMod,
                    mod_id.clone(),
                    "{}".to_string(),
                    true,
                );
                return vec![];
            }
            ShellEvent::UpdateCssMod { mod_id, css } => {
                let now = chrono::Utc::now().to_rfc3339();
                if let Some(updated) =
                    self.css_mod_manager
                        .update_mod_css(mod_id, css.clone(), &now)
                {
                    if let Some(storage) = &self.storage {
                        let _ = storage.save_css_mod(updated);
                    }
                    if let Ok(payload) = serde_json::to_string(updated) {
                        self.push_sync_entity(
                            SyncEntityType::CssMod,
                            mod_id.clone(),
                            payload,
                            false,
                        );
                    }
                }
                return vec![];
            }
            ShellEvent::SearchHistory {
                query,
                limit,
                is_incognito,
            } => {
                let entries = if *is_incognito {
                    Vec::new()
                } else {
                    self.search_history(query, *limit)
                        .into_iter()
                        .map(
                            |(id, url, title)| maho_types::events::core_update::HistoryEntry {
                                id,
                                url,
                                title,
                                visited_at: String::new(),
                            },
                        )
                        .collect()
                };
                return vec![CoreUpdate::HistoryResults { entries }];
            }
            ShellEvent::CreateNote {
                linked_tab,
                content,
            } => {
                let note = self.create_note_persisted(linked_tab.clone(), content.clone());
                let _ = note;
                return vec![];
            }
            ShellEvent::UpdateNote { note_id, content } => {
                self.update_note_persisted(note_id, content.clone());
                return vec![];
            }
            ShellEvent::DeleteNote { note_id } => {
                self.delete_note_persisted(note_id);
                return vec![];
            }
            ShellEvent::GrantPermission { origin, permission } => {
                self.grant_permission_persisted(origin, permission);
                return vec![];
            }
            ShellEvent::RevokePermission { origin, permission } => {
                self.revoke_permission_persisted(origin, permission);
                return vec![];
            }
            ShellEvent::CreateTrafficRule { rule } => {
                let created = self.create_traffic_rule_persisted(rule.clone());
                return vec![CoreUpdate::TrafficRuleCreated { rule: created }];
            }
            ShellEvent::DeleteTrafficRule { rule_id } => {
                if self.delete_traffic_rule_persisted(rule_id).is_some() {
                    return vec![CoreUpdate::TrafficRuleDeleted {
                        rule_id: rule_id.clone(),
                    }];
                }
                return vec![];
            }
            ShellEvent::UpdateTrafficRule { rule } => {
                self.update_traffic_rule_persisted(rule.clone());
                return vec![];
            }
            ShellEvent::SetDefaultLinkBehavior { behavior } => {
                self.atc_manager.set_default_link_behavior(behavior.clone());
                if let Some(ref storage) = self.storage {
                    if let Ok(json) = serde_json::to_string(&behavior) {
                        let _ = storage.set_setting("default_link_behavior", &json);
                    }
                }
                return vec![];
            }
            ShellEvent::RenameSpace { space_id, name } => {
                return self.update_space_config_internal(maho_types::space::SpaceConfigUpdate {
                    space_id: space_id.clone(),
                    name: Some(name.clone()),
                    color: None,
                    theme: None,
                    icon: None,
                    profile_id: None,
                });
            }
            ShellEvent::RecolorSpace { space_id, color } => {
                return self.update_space_config_internal(maho_types::space::SpaceConfigUpdate {
                    space_id: space_id.clone(),
                    name: None,
                    color: Some(color.clone()),
                    theme: None,
                    icon: None,
                    profile_id: None,
                });
            }
            ShellEvent::UpdateSpaceConfig { changes } => {
                return self.update_space_config_internal(changes.clone());
            }
            ShellEvent::UnfreezeTab { tab_id } => {
                self.tab_manager.unfreeze_tab(tab_id);
                return vec![];
            }
            ShellEvent::SearchPasswords { query } => {
                let results: Vec<maho_types::passwords::SavedPassword> = self
                    .password_manager
                    .search_passwords(query)
                    .into_iter()
                    .cloned()
                    .collect();
                return vec![CoreUpdate::PasswordsSearchResult { passwords: results }];
            }
            ShellEvent::DeletePassword { password_id } => {
                if self.password_manager.delete_password(password_id) {
                    if let Some(ref storage) = self.storage {
                        let _ = storage.delete_password(password_id);
                    }
                    return vec![CoreUpdate::PasswordDeleted {
                        password_id: password_id.clone(),
                    }];
                }
                return vec![];
            }
            ShellEvent::AddPassword {
                domain,
                username,
                password,
            } => {
                let pw = self
                    .password_manager
                    .add_password(domain.clone(), username.clone(), Some(password.clone()))
                    .clone();
                if let Some(ref storage) = self.storage {
                    let _ = storage.save_password(
                        &pw.id,
                        &pw.domain,
                        &pw.username,
                        &pw.created_at,
                        pw.password.as_deref(),
                    );
                }
                return vec![CoreUpdate::PasswordsSearchResult {
                    passwords: vec![pw],
                }];
            }
            ShellEvent::AddAutofillAddress { address } => {
                let addr = self.add_autofill_address_persisted(address.clone());
                return vec![CoreUpdate::AutofillAddressAdded { address: addr }];
            }
            ShellEvent::DeleteAutofillAddress { id } => {
                if self.delete_autofill_address_persisted(id) {
                    return vec![CoreUpdate::AutofillAddressDeleted { id: id.clone() }];
                }
                return vec![];
            }
            ShellEvent::AddAutofillPayment { payment } => {
                let pay = self.add_autofill_payment_persisted(payment.clone());
                return vec![CoreUpdate::AutofillPaymentAdded { payment: pay }];
            }
            ShellEvent::DeleteAutofillPayment { id } => {
                if self.delete_autofill_payment_persisted(id) {
                    return vec![CoreUpdate::AutofillPaymentDeleted { id: id.clone() }];
                }
                return vec![];
            }
            ShellEvent::UpdateSettings { changes } => {
                self.update_settings(changes.clone());
                return vec![];
            }
            ShellEvent::ResetSettings => {
                let settings = self.settings_manager.reset_to_defaults().clone();
                // Persist reset
                if let Some(ref storage) = self.storage {
                    if let Ok(json) = serde_json::to_string(&settings) {
                        let _ = storage.set_setting("browser_settings", &json);
                    }
                }
                if let Ok(payload) = serde_json::to_string(&settings) {
                    self.push_sync_entity(
                        SyncEntityType::Settings,
                        "global_settings".to_string(),
                        payload,
                        false,
                    );
                }
                return vec![CoreUpdate::SettingsChanged { settings }];
            }
            ShellEvent::AddToReadingList { url, title } => {
                let item = self.add_reading_list_item_persisted(url, title);
                let _ = item;
                return vec![];
            }
            ShellEvent::RemoveFromReadingList { item_id } => {
                self.remove_reading_list_item_persisted(item_id);
                return vec![];
            }
            ShellEvent::MarkReadingListItemRead { item_id } => {
                self.mark_reading_list_read_persisted(item_id);
                return vec![];
            }
            ShellEvent::MarkReadingListItemUnread { item_id } => {
                self.mark_reading_list_unread_persisted(item_id);
                return vec![];
            }
            ShellEvent::RestoreDownloadName { download_id } => {
                if let Some(update) = self.restore_download_name(download_id) {
                    return vec![update];
                }
                return vec![];
            }
            ShellEvent::RequestTidyTabs { ref space_id } => {
                let tab_order = self.space_manager.get_tab_order(space_id);
                let tabs: Vec<(TabId, String, String)> = tab_order
                    .iter()
                    .filter_map(|tab_id| {
                        let tab = self.tab_manager.get_tab(tab_id)?;
                        if tab.role.is_pinned() {
                            return None;
                        }
                        Some((tab_id.clone(), tab.title.clone(), tab.url.0.clone()))
                    })
                    .collect();
                if tabs.len() < 6 {
                    return vec![];
                }
                let update = self.llm_manager.request_tidy_tabs(space_id, tabs);
                return vec![update];
            }
            ShellEvent::ApplyTidyTabs { ref space_id } => {
                if let Some((_sid, folders)) = self.llm_manager.take_pending_tidy_tabs() {
                    let mut updates = Vec::new();
                    for tidy_folder in &folders {
                        if let Some(folder_vm) = self.space_manager.create_folder(
                            space_id,
                            &tidy_folder.name,
                            false,
                            None,
                        ) {
                            for tab_id in &tidy_folder.tab_ids {
                                self.space_manager.add_tab_to_folder(
                                    space_id,
                                    &folder_vm.id,
                                    tab_id.clone(),
                                );
                            }
                            updates.push(CoreUpdate::FolderCreated { folder: folder_vm });
                        }
                    }
                    if self
                        .settings_manager
                        .get_settings()
                        .general
                        .auto_delete_empty_folders_on_tidy
                    {
                        let removed = self.space_manager.remove_empty_folders_in_space(space_id);
                        for folder_id in removed {
                            updates.push(CoreUpdate::FolderDeleted { folder_id });
                        }
                    }
                    return updates;
                }
                return vec![];
            }
            ShellEvent::FavoriteTab { ref tab_id } => {
                let already_favorite = self
                    .tab_manager
                    .get_tab(tab_id)
                    .map(|tab| tab.role.is_favorite()) // L3-EXEMPT: role query
                    .unwrap_or(false);
                if already_favorite {
                    return vec![];
                }

                let profile_space_ids = self
                    .tab_manager
                    .get_tab(tab_id)
                    .and_then(|tab| self.space_manager.get_space(&tab.space_id))
                    .map(|space| {
                        self.space_manager
                            .get_space_ids_for_profile(&space.profile_id)
                    })
                    .unwrap_or_default();

                if self
                    .tab_manager
                    .count_favorite_tabs_in_spaces(&profile_space_ids)
                    >= crate::tab_lifecycle::MAX_FAVORITES
                {
                    return vec![CoreUpdate::FavoriteLimitReached {
                        max: crate::tab_lifecycle::MAX_FAVORITES,
                    }];
                }

                let max_order = self
                    .tab_manager
                    .get_all_favorite_tabs()
                    .iter()
                    .filter(|t| profile_space_ids.contains(&t.space_id))
                    .filter_map(|t| t.role.favorite_order())
                    .max()
                    .unwrap_or(0);

                let new_role = TabRole::Favorite {
                    order: max_order + 1,
                };
                return self.transition_tab_role(tab_id, new_role);
            }
            ShellEvent::ChangeTabRole {
                ref tab_id,
                ref new_role,
            } => {
                return self.transition_tab_role(tab_id, new_role.clone());
            }
            ShellEvent::PinTab { ref tab_id } => {
                let mut updates = self.transition_tab_role(tab_id, TabRole::Pinned);
                if let Some(space_id) =
                    self.tab_manager.get_tab(tab_id).map(|t| t.space_id.clone())
                {
                    let pinned_tab_ids: HashSet<TabId> = self
                        .tab_manager
                        .get_all_tabs()
                        .iter()
                        .filter(|t| t.space_id == space_id && t.role.is_pinned())
                        .map(|t| t.id.clone())
                        .collect();
                    self.space_manager.move_root_tab_to_end_of_pinned(
                        &space_id,
                        tab_id,
                        &pinned_tab_ids,
                    );
                    updates.push(CoreUpdate::TabOrderChanged {
                        space_id,
                        order: vec![],
                    });
                }
                if self.settings_manager.get_settings().max.tidy_tab_titles {
                    if let Some(tab) = self.tab_manager.get_tab(tab_id) {
                        let tidy_update = self
                            .llm_manager
                            .request_tidy_title(tab_id, &tab.title, &tab.url.0);
                        updates.push(tidy_update);
                    }
                }
                return updates;
            }
            ShellEvent::UnpinTab { ref tab_id } => {
                return self.transition_tab_role(tab_id, TabRole::Normal);
            }
            ShellEvent::CloseTab {
                ref tab_id,
                ref expected_space_id,
            } => {
                if let Some(ref expected_sid) = expected_space_id {
                    if let Some(tab) = self.tab_manager.get_tab(tab_id) {
                        debug_assert_eq!(
                            &tab.space_id, expected_sid,
                            "CloseTab expected_space_id mismatch: tab has space {}, expected {}",
                            tab.space_id, expected_sid
                        );
                        if &tab.space_id != expected_sid {
                            eprintln!(
                                "CloseTab ignored due to expected_space_id mismatch (tab has space {}, expected {})",
                                tab.space_id, expected_sid
                            );
                            return vec![];
                        }
                    }
                }

                let behavior = self
                    .settings_manager
                    .get_settings()
                    .general
                    .pinned_close_behavior
                    .clone();
                let tab_opt = self.tab_manager.get_tab(tab_id);
                // Architectural Shift (B-Architecture):
                // Under B-architecture, favorites on the grid and tabs on the normal strip are
                // strictly decoupled using unique TabIds. Pinned tabs remain on the strip and
                // require close-guard protection, whereas favorites do not reside on the active
                // tab strip as regular closable tabs and thus do not require close-guard protection.
                let is_pinned = tab_opt.map(|t| t.role == TabRole::Pinned).unwrap_or(false);
                let is_favorite = tab_opt.map(|t| t.role.is_favorite()).unwrap_or(false); // L3-EXEMPT: role query
                let already_suspended = tab_opt
                    .map(|t| matches!(t.state, TabLifecycleState::Suspended { .. }))
                    .unwrap_or(false);

                if is_favorite {
                    // L3-EXEMPT: close protection check
                    if !already_suspended {
                        self.tab_manager.suspend_tab(tab_id);
                        self.persist_lmdb_now_partial(LMDB_DIRTY_TABS);
                        self.push_tab_sync(tab_id);
                    }
                    return vec![];
                }

                if is_pinned && !matches!(behavior, PinnedCloseBehavior::Close) {
                    if already_suspended {
                        // 2nd close on a ghost pinned tab -> fall through to full delete
                    } else {
                        let space_id =
                            match self.tab_manager.get_tab(tab_id).map(|t| t.space_id.clone()) {
                                Some(sid) => sid,
                                None => return vec![],
                            };

                        let needs_reset = matches!(
                            behavior,
                            PinnedCloseBehavior::Reset
                                | PinnedCloseBehavior::ResetSwitch
                                | PinnedCloseBehavior::ResetUnloadSwitch
                        );
                        if needs_reset {
                            self.tab_manager.reset_pinned_tab(tab_id);
                        }

                        // Under B-architecture, we must transition the tab to Suspended when the WebContents
                        // is closed natively (e.g. via close_tab dispatch), even if PinnedCloseBehavior is not
                        // ResetUnloadSwitch/UnloadSwitch.
                        self.tab_manager.suspend_tab(tab_id);

                        let needs_focus_switch = matches!(
                            behavior,
                            PinnedCloseBehavior::Switch
                                | PinnedCloseBehavior::ResetSwitch
                                | PinnedCloseBehavior::UnloadSwitch
                                | PinnedCloseBehavior::ResetUnloadSwitch
                        );
                        if needs_focus_switch {
                            let currently_active =
                                self.space_manager.get_last_active_tab(&space_id).cloned();
                            if currently_active.as_ref() == Some(tab_id) {
                                let next_active = self
                                    .space_manager
                                    .get_tab_order(&space_id)
                                    .into_iter()
                                    .filter(|id| id != tab_id)
                                    .find(|id| {
                                        self.tab_manager
                                            .get_tab(id)
                                            .map(|t| {
                                                !matches!(
                                                    t.state,
                                                    TabLifecycleState::Archived { .. }
                                                )
                                            })
                                            .unwrap_or(false)
                                    });
                                self.space_manager
                                    .set_last_active_tab(&space_id, next_active);
                            }
                        }

                        return vec![];
                    }
                }
            }
            ShellEvent::ResetPinnedTab { ref tab_id } => {
                self.reset_pinned_tab(tab_id);
                return vec![];
            }
            ShellEvent::DeleteSpace { ref space_id } => {
                let space_order = self.space_manager.get_space_order();
                if space_order.len() <= 1 {
                    return vec![CoreUpdate::Error {
                        context: "delete_space".to_string(),
                        error: maho_types::events::core_update::MahoError {
                            code: "LAST_SPACE".to_string(),
                            message: "Cannot delete the last space".to_string(),
                            details: None,
                        },
                    }];
                }

                let tab_ids = self.space_manager.get_tab_order(space_id);
                let next_space_id = space_order.iter().find(|id| *id != space_id).cloned();
                let is_active_space = self.space_manager.get_active_space_id() == space_id;

                let mut updates = Vec::new();
                if let Some(ref target_space_id) = next_space_id {
                    for tab_id in &tab_ids {
                        self.tab_manager
                            .move_tab(tab_id, target_space_id.clone(), 0);
                        self.space_manager.remove_tab_from_space(space_id, tab_id);
                        self.space_manager
                            .add_tab_to_space(target_space_id, tab_id.clone(), None);
                        let is_favorite = self // L3-EXEMPT: local query
                            .tab_manager
                            .get_tab(tab_id)
                            .map(|t| t.role.is_favorite()) // L3-EXEMPT: local query
                            .unwrap_or(false);
                        self.space_manager.apply_tab_residency(
                            target_space_id,
                            tab_id,
                            is_favorite,
                            false,
                        ); // L3-EXEMPT: local parameter
                    }
                    if !tab_ids.is_empty() || is_active_space {
                        updates.push(CoreUpdate::TabsMigrated {
                            tab_ids,
                            from_space_id: space_id.clone(),
                            to_space_id: target_space_id.clone(),
                        });
                    }
                }

                if let Some(active_space_id) = self.space_manager.delete_space(space_id) {
                    updates.push(CoreUpdate::SpaceDeleted {
                        space_id: space_id.clone(),
                    });
                    let active_tab_id = self
                        .space_manager
                        .get_last_active_tab(&active_space_id)
                        .cloned();
                    updates.push(CoreUpdate::ActiveSpaceChanged {
                        space_id: active_space_id,
                        active_tab_id,
                    });
                } else {
                    updates.push(CoreUpdate::SpaceDeleted {
                        space_id: space_id.clone(),
                    });
                }
                return updates;
            }
            ShellEvent::ExportSpaceIntoFolder {
                ref space_id,
                ref target_space_id,
            } => {
                if space_id == target_space_id {
                    return vec![CoreUpdate::Error {
                        context: "export_space_into_folder".to_string(),
                        error: maho_types::events::core_update::MahoError {
                            code: "INVALID_TARGET".to_string(),
                            message: "Cannot export a space into itself".to_string(),
                            details: None,
                        },
                    }];
                }

                let source_space = match self.space_manager.get_space(space_id) {
                    Some(space) => space.clone(),
                    None => {
                        return vec![CoreUpdate::Error {
                            context: "export_space_into_folder".to_string(),
                            error: maho_types::events::core_update::MahoError {
                                code: "NOT_FOUND".to_string(),
                                message: "Source space not found".to_string(),
                                details: None,
                            },
                        }]
                    }
                };
                if self.space_manager.get_space(target_space_id).is_none() {
                    return vec![CoreUpdate::Error {
                        context: "export_space_into_folder".to_string(),
                        error: maho_types::events::core_update::MahoError {
                            code: "NOT_FOUND".to_string(),
                            message: "Target space not found".to_string(),
                            details: None,
                        },
                    }];
                }

                let folder_vm = match self.space_manager.create_folder(
                    target_space_id,
                    &source_space.name,
                    false,
                    None,
                ) {
                    Some(folder) => folder,
                    None => {
                        return vec![CoreUpdate::Error {
                            context: "export_space_into_folder".to_string(),
                            error: maho_types::events::core_update::MahoError {
                                code: "EXPORT_FAILED".to_string(),
                                message: "Failed to create destination folder".to_string(),
                                details: None,
                            },
                        }]
                    }
                };

                let tab_ids = self.space_manager.get_tab_order(space_id);
                for tab_id in &tab_ids {
                    self.tab_manager
                        .move_tab(tab_id, target_space_id.clone(), 0);
                    self.space_manager.remove_tab_from_space(space_id, tab_id);
                    self.space_manager
                        .add_tab_to_space(target_space_id, tab_id.clone(), None);
                    self.space_manager.add_tab_to_folder(
                        target_space_id,
                        &folder_vm.id,
                        tab_id.clone(),
                    );
                }

                let source_was_active = self.space_manager.get_active_space_id() == space_id;

                let mut updates = vec![CoreUpdate::FolderCreated { folder: folder_vm }];
                if !tab_ids.is_empty() {
                    updates.push(CoreUpdate::TabsMigrated {
                        tab_ids,
                        from_space_id: space_id.clone(),
                        to_space_id: target_space_id.clone(),
                    });
                }

                self.space_manager.delete_space(space_id);
                updates.push(CoreUpdate::SpaceDeleted {
                    space_id: space_id.clone(),
                });

                if source_was_active {
                    self.space_manager.activate_space(target_space_id);
                    let active_tab_id = self
                        .space_manager
                        .get_last_active_tab(target_space_id)
                        .cloned();
                    updates.push(CoreUpdate::ActiveSpaceChanged {
                        space_id: target_space_id.clone(),
                        active_tab_id,
                    });
                }
                return updates;
            }
            ShellEvent::RequestSkillsList => {
                let skills = self.skills_manager.get_all_skill_infos();
                return vec![CoreUpdate::SkillsListReady { skills }];
            }
            ShellEvent::ChatRequestModeChanged { .. } => return vec![],
            ShellEvent::ChatMessage {
                ref message,
                ref context,
            } => {
                // Memory: persist user turn for future fact extraction
                if let Some(ref storage) = self.storage {
                    let id = uuid::Uuid::new_v4().to_string();
                    let session_id = context
                        .session_id
                        .clone()
                        .unwrap_or_else(|| "default".to_string());
                    let url_ctx = context.page_url.clone();
                    let _ = storage.insert_conversation_turn(
                        &id,
                        &session_id,
                        "user",
                        message,
                        url_ctx.as_deref(),
                    );
                }
                return vec![self.llm_manager.request_chat_completion(message, context)];
            }
            ShellEvent::UseSkill {
                skill_id,
                user_input,
                page_context,
            } => {
                let page_context_text = page_context.as_ref().map(|ctx| {
                    serde_json::to_string_pretty(ctx).unwrap_or_else(|_| ctx.title.clone())
                });
                if let Some(prompt) = self.skills_manager.build_prompt(
                    skill_id,
                    user_input,
                    page_context_text.as_deref().unwrap_or(""),
                ) {
                    if let Some(skill) = self.skills_manager.get_skill(skill_id) {
                        return vec![CoreUpdate::SkillPromptReady {
                            skill_id: skill.id.clone(),
                            skill_name: skill.name.clone(),
                            system_prompt: prompt,
                        }];
                    }
                }
                return vec![];
            }
            ShellEvent::LlmResult {
                ref request_id,
                ref result,
            } => {
                // Memory: persist assistant turn
                if let Some(ref storage) = self.storage {
                    if let Some(session_id) =
                        self.llm_manager.lookup_session_for_request(request_id)
                    {
                        let id = uuid::Uuid::new_v4().to_string();
                        let _ = storage.insert_conversation_turn(
                            &id,
                            &session_id,
                            "assistant",
                            result,
                            None,
                        );
                    }
                }
                // Try to detect tool_calls in the response
                if let Ok(json) = serde_json::from_str::<serde_json::Value>(result) {
                    if let Some(tool_calls) = json.get("tool_calls").and_then(|tc| tc.as_array()) {
                        if !tool_calls.is_empty() {
                            let messages = json
                                .get("messages")
                                .cloned()
                                .unwrap_or(serde_json::json!([]));

                            let tab_summaries: Vec<(String, String, String)> = self
                                .tab_manager
                                .get_all_tabs()
                                .iter()
                                .map(|t| (t.id.to_string(), t.title.clone(), t.url.0.clone()))
                                .collect();
                            let active_tab_info = self
                                .tab_manager
                                .get_active_tab_id()
                                .and_then(|id| self.tab_manager.get_tab(id))
                                .map(|t| (t.title.clone(), t.url.0.clone()));
                            let active_ref = active_tab_info
                                .as_ref()
                                .map(|(t, u)| (t.as_str(), u.as_str()));

                            // Temporarily take storage to avoid borrow conflict with tool_registry
                            let storage = self.storage.take();
                            let mcp_updates = self.tool_registry.handle_llm_response_with_tools(
                                request_id,
                                tool_calls,
                                messages,
                                &tab_summaries,
                                active_ref,
                                &|query, limit| {
                                    storage
                                        .as_ref()
                                        .and_then(|s| s.search_history(query, limit).ok())
                                        .unwrap_or_default()
                                },
                                &|query| {
                                    storage
                                        .as_ref()
                                        .and_then(|s| s.search_bookmarks(query).ok())
                                        .unwrap_or_default()
                                },
                            );
                            self.storage = storage; // Put it back
                            return mcp_updates;
                        }
                    }
                }
                // No tool_calls → normal path through llm_manager
                return self.llm_manager.handle_result(request_id, result);
            }
            ShellEvent::LlmError {
                ref request_id,
                ref error,
            } => {
                return self.llm_manager.handle_error(request_id, error);
            }
            ShellEvent::ChatSessionEnded { ref session_id } => {
                if let Some(ref storage) = self.storage {
                    if let Ok(turns) = storage.load_conversation_session(session_id) {
                        let raw_text = turns
                            .iter()
                            .map(|(role, content)| format!("[{}] {}", role, content))
                            .collect::<Vec<_>>()
                            .join("\n");
                        if !raw_text.is_empty() {
                            self.memory_manager
                                .extract_from_session(session_id.clone(), raw_text);
                        }
                    }
                }
                return vec![];
            }
            ShellEvent::ToolPermissionResponse {
                ref call_id,
                granted,
            } => {
                return self
                    .tool_registry
                    .handle_tool_permission_response(call_id, *granted);
            }
            ShellEvent::ToolActionResult {
                ref call_id,
                success,
                ref result,
            } => {
                return self
                    .tool_registry
                    .handle_tool_action_result(call_id, *success, result);
            }
            _ => {}
        }
        let tab_view_models = self.get_tab_view_models();
        let space_view_models = self.get_space_view_models();

        let archived_tabs: Vec<TabViewModel> = self
            .space_manager
            .get_space_order()
            .iter()
            .flat_map(|space_id| {
                self.tab_manager
                    .get_archived_tabs(space_id)
                    .into_iter()
                    .map(|tab| self.make_view_model(tab))
            })
            .collect();

        let bookmarks: Vec<(String, String, String)> = self
            .bookmark_manager
            .get_all_bookmarks()
            .iter()
            .map(|b| (b.id.0.clone(), b.title.clone(), b.url.clone()))
            .collect();

        let bookmark_favicons: Vec<(String, maho_types::common::ImageData)> = self
            .bookmark_manager
            .get_all_bookmarks()
            .iter()
            .filter_map(|b| {
                b.favicon.as_deref().and_then(|favicon| {
                    crate::command_bar::CommandBarEngine::bookmark_icon_from_data_url(favicon)
                        .map(|icon| (b.id.0.clone(), icon))
                })
            })
            .collect();

        let closed_tabs: Vec<TabViewModel> = self
            .tab_manager
            .get_closed_tabs()
            .into_iter()
            .map(|tab| self.make_view_model(tab))
            .collect();

        let extensions: Vec<(String, String)> = self
            .extension_bridge
            .get_installed_extensions()
            .iter()
            .filter(|ext| ext.enabled)
            .map(|ext| (ext.id.clone(), ext.name.clone()))
            .collect();

        let active_space_id = self.space_manager.get_active_space_id().clone();
        let folders = self.get_folder_view_models(&active_space_id);

        let event_is_incognito = match &event {
            ShellEvent::CommandBarQuery { is_incognito, .. }
            | ShellEvent::SearchHistory { is_incognito, .. } => *is_incognito,
            _ => false,
        };

        let ctx = SearchContext {
            tabs: tab_view_models,
            spaces: space_view_models,
            folders,
            archived_tabs,
            bookmarks,
            bookmark_favicons,
            closed_tabs,
            extensions,
            is_incognito: event_is_incognito,
            recent_tabs: Vec::new(),
        };

        // Extract PinTab info before dispatch consumes the event
        let pin_tab_id = if let ShellEvent::PinTab { tab_id } = &event {
            Some(tab_id.clone())
        } else {
            None
        };

        let mut updates = self.dispatcher.dispatch(
            event.clone(),
            &mut self.tab_manager,
            &mut self.space_manager,
            &self.command_bar,
            &mut self.note_manager,
            &mut self.settings_manager,
            &mut self.content_blocker,
            &mut self.notification_manager,
            &mut self.llm_manager,
            &ctx,
        );

        // Tidy Tab Titles: when a tab is pinned and the setting is enabled, ask LLM to shorten
        if let Some(tab_id) = pin_tab_id {
            if self.settings_manager.get_settings().max.tidy_tab_titles {
                if let Some(tab) = self.tab_manager.get_tab(&tab_id) {
                    let tidy_update = self
                        .llm_manager
                        .request_tidy_title(&tab_id, &tab.title, &tab.url.0);
                    updates.push(tidy_update);
                }
            }
        }

        // Post-process DownloadRenamed from LLM: perform actual rename in download_manager
        for update in &mut updates {
            if let CoreUpdate::DownloadRenamed {
                download_id,
                old_name,
                new_name,
            } = update
            {
                if old_name.is_empty() {
                    if let Some((actual_old, _)) =
                        self.download_manager.rename_download(download_id, new_name)
                    {
                        *old_name = actual_old;
                    }
                }
            }
        }

        updates
    }

    pub fn tick(&mut self) -> Vec<CoreUpdate> {
        let global_archive_timeout = self
            .settings_manager
            .get_settings()
            .general
            .archive_timeout_hours;
        let archive_timeout = self
            .profile_manager
            .get_active_profile()
            .and_then(|p| p.archive_timeout_hours)
            .unwrap_or(global_archive_timeout);
        let today_timeout = self
            .settings_manager
            .get_settings()
            .general
            .today_tab_timeout_hours;
        let mut updates = self
            .tab_manager
            .tick_with_settings(archive_timeout, today_timeout);

        let evicted = self.tab_manager.enforce_archive_cap();
        for (tab_id, space_id) in &evicted {
            self.space_manager.remove_tab_from_space(space_id, tab_id);
        }

        // Memory: drain background extraction/embedding/summary jobs
        for result in self.memory_manager.drain_results() {
            self.handle_memory_result(result, &mut updates);
        }

        updates
    }

    pub fn get_tab_snapshot_json(&self, tab_id: &TabId) -> Option<String> {
        self.tab_manager.get_tab_snapshot_json(tab_id)
    }

    pub fn get_tab_view_models(&self) -> Vec<TabViewModel> {
        let all_tabs: Vec<&maho_types::tab::Tab> = self
            .tab_manager
            .get_all_tabs()
            .into_iter()
            .filter(|tab| !matches!(tab.state, TabLifecycleState::Archived { .. }))
            .collect();
        all_tabs
            .iter()
            .map(|tab| {
                let children: Vec<TabId> = all_tabs
                    .iter()
                    .filter(|t| t.parent_id.as_ref() == Some(&tab.id))
                    .map(|t| t.id.clone())
                    .collect();
                TabViewModel {
                    id: tab.id.clone(),
                    space_id: tab.space_id.clone(),
                    title: tab.title.clone(),
                    custom_title: tab.custom_title.clone(),
                    url: tab.url.0.clone(),
                    favicon: tab.favicon.clone(),
                    is_loading: self.tab_manager.is_tab_loading(&tab.id),
                    is_pinned: tab.role.is_pinned(),
                    is_favorite: tab.role.is_favorite(), // L3-EXEMPT: views display
                    is_muted: tab.is_muted,
                    is_playing_audio: false,
                    lifecycle_state: tab.state.kind_str().to_string(),
                    children,
                    created_at: tab.created_at.clone(),
                    last_active_at: tab.last_active_at.clone(),
                    favorite_order: tab.role.favorite_order(),
                    role: tab.role.clone(),
                    is_private: tab.is_private,
                }
            })
            .collect()
    }

    pub fn cluster_tabs(&self, threshold: f64) -> Vec<crate::tab_clustering::TabClusterResult> {
        let tabs = self.tab_manager.get_all_tabs();
        crate::tab_clustering::cluster_tabs(&tabs, threshold)
    }

    pub fn get_space_view_models(&self) -> Vec<SpaceViewModel> {
        self.space_manager
            .get_all_spaces()
            .into_iter()
            .map(|space| {
                let mut vm = self.space_manager.to_view_model(space);
                let archived_count = space
                    .tab_order
                    .iter()
                    .filter(|tid| {
                        self.tab_manager
                            .get_tab(tid)
                            .map(|t| matches!(t.state, TabLifecycleState::Archived { .. }))
                            .unwrap_or(false)
                    })
                    .count();
                vm.tab_count = vm.tab_count.saturating_sub(archived_count);
                vm
            })
            .collect()
    }

    pub fn get_space_theme(&self, space_id: &SpaceId) -> Option<maho_types::space::SpaceTheme> {
        self.space_manager
            .get_space(space_id)
            .and_then(|s| s.theme.clone())
    }

    pub fn get_active_space_id(&self) -> SpaceId {
        self.space_manager.get_active_space_id().clone()
    }

    pub fn get_last_active_tab_for_space(&self, space_id: &SpaceId) -> Option<TabId> {
        self.space_manager.get_last_active_tab(space_id).cloned()
    }

    pub fn get_activation_target_for_space(&self, space_id: &SpaceId) -> Option<TabId> {
        if let Some(tab_id) = self.space_manager.get_last_active_tab(space_id).cloned() {
            Some(tab_id)
        } else {
            self.space_manager
                .get_tab_order(space_id)
                .into_iter()
                .find(|tab_id| {
                    self.tab_manager
                        .get_tab(tab_id)
                        .map(|tab| !matches!(tab.state, TabLifecycleState::Archived { .. }))
                        .unwrap_or(false)
                })
        }
    }

    pub fn create_space(&mut self, name: &str, color: SpaceColor, profile_id: ProfileId) -> Space {
        let space = self.space_manager.create_space(name, color, profile_id);
        self.seed_default_favorite_for_space(&space.id);
        let vm = self.space_manager.to_view_model(&space);
        let update = CoreUpdate::SpaceCreated { space: vm };
        for listener in self.update_listeners.borrow().iter() {
            listener(update.clone());
        }
        self.persist_lmdb_now_partial(LMDB_DIRTY_TABS | LMDB_DIRTY_SPACES);
        space
    }

    fn seed_default_favorite_for_space(&mut self, space_id: &SpaceId) {
        let google_url = maho_types::common::Url::new("https://www.google.com");
        let tab =
            self.tab_manager
                .create_tab(space_id.clone(), Some(google_url), None, None, false);
        self.tab_manager
            .update_tab_title(&tab.id, "Google".to_string());
        self.space_manager
            .add_tab_to_space(space_id, tab.id.clone(), None);
        self.tab_manager.favorite_tab(&tab.id);
    }

    pub fn delete_space(&mut self, space_id: &SpaceId) {
        let _updates = self.handle_event(ShellEvent::DeleteSpace {
            space_id: space_id.clone(),
        });
    }

    pub fn update_space_config(&mut self, changes: maho_types::space::SpaceConfigUpdate) {
        let _updates = self.handle_event(ShellEvent::UpdateSpaceConfig { changes });
    }

    pub fn rename_space(&mut self, space_id: &SpaceId, name: &str) {
        let _updates = self.handle_event(ShellEvent::UpdateSpaceConfig {
            changes: maho_types::space::SpaceConfigUpdate {
                space_id: space_id.clone(),
                name: Some(name.to_string()),
                color: None,
                theme: None,
                icon: None,
                profile_id: None,
            },
        });
    }

    pub fn recolor_space(&mut self, space_id: &SpaceId, color: SpaceColor) {
        let _updates = self.handle_event(ShellEvent::UpdateSpaceConfig {
            changes: maho_types::space::SpaceConfigUpdate {
                space_id: space_id.clone(),
                name: None,
                color: Some(color),
                theme: None,
                icon: None,
                profile_id: None,
            },
        });
    }

    pub fn reorder_space(&mut self, space_id: &SpaceId, from: usize, to: usize) {
        let _updates = self.handle_event(ShellEvent::ReorderSpace {
            space_id: space_id.clone(),
            from,
            to,
        });
    }

    pub fn activate_space(&mut self, space_id: &SpaceId) {
        let _updates = self.handle_event(ShellEvent::ActivateSpace {
            space_id: space_id.clone(),
        });
    }

    pub fn on_update(&self, callback: Box<dyn Fn(CoreUpdate)>) {
        self.update_listeners.borrow_mut().push(callback);
    }

    fn emit_error(&self, context: &str, code: &str, message: &str) {
        let update = CoreUpdate::Error {
            context: context.to_string(),
            error: maho_types::events::core_update::MahoError {
                code: code.to_string(),
                message: message.to_string(),
                details: None,
            },
        };
        for listener in self.update_listeners.borrow().iter() {
            listener(update.clone());
        }
    }

    fn emit_updates(&self, updates: Vec<CoreUpdate>) {
        for update in updates {
            for listener in self.update_listeners.borrow().iter() {
                listener(update.clone());
            }
        }
    }

    fn space_update_error_to_update(
        &self,
        context: &str,
        err: crate::space_manager::SpaceUpdateError,
    ) -> CoreUpdate {
        CoreUpdate::Error {
            context: context.to_string(),
            error: maho_types::events::core_update::MahoError {
                code: err.code().to_string(),
                message: err.to_string(),
                details: None,
            },
        }
    }

    fn update_space_config_internal(
        &mut self,
        changes: maho_types::space::SpaceConfigUpdate,
    ) -> Vec<CoreUpdate> {
        match self.space_manager.update_space_config(changes) {
            Ok(Some(updated)) => vec![CoreUpdate::SpaceConfigUpdated {
                space_id: updated.space_id.clone(),
                changes: updated,
            }],
            Ok(None) => vec![],
            Err(err) => vec![self.space_update_error_to_update("update_space_config", err)],
        }
    }

    fn make_error_update(context: &str, code: &str, message: &str) -> CoreUpdate {
        CoreUpdate::Error {
            context: context.to_string(),
            error: maho_types::events::core_update::MahoError {
                code: code.to_string(),
                message: message.to_string(),
                details: None,
            },
        }
    }

    fn emit_settings_changed(&self, settings: Settings) {
        let update = CoreUpdate::SettingsChanged { settings };
        for listener in self.update_listeners.borrow().iter() {
            listener(update.clone());
        }
    }

    pub fn get_settings(&self) -> &Settings {
        self.settings_manager.get_settings()
    }

    pub fn update_settings(&mut self, update: SettingsUpdate) {
        let settings = self.settings_manager.update_settings(update).clone();
        self.emit_settings_changed(settings.clone());
        // Persist to SQLite
        if let Some(ref storage) = self.storage {
            if let Ok(json) = serde_json::to_string(&settings) {
                let _ = storage.set_setting("browser_settings", &json);
            }
        }
        if let Ok(payload) = serde_json::to_string(&settings) {
            self.push_sync_entity(
                SyncEntityType::Settings,
                "global_settings".to_string(),
                payload,
                false,
            );
        }
    }

    // === Profile Methods ===

    pub fn list_profiles(&self) -> &[maho_types::profile::ProfileConfig] {
        self.profile_manager.list_profiles()
    }

    pub fn create_profile_persisted(&mut self, name: String) -> Result<maho_types::profile::ProfileConfig, crate::profile_manager::ProfileError> {
        let profile = self.profile_manager.create_profile(name)?.clone();
        self.persist_profile(&profile);
        let update = CoreUpdate::ProfileCreated {
            profile: profile.clone(),
        };
        for listener in self.update_listeners.borrow().iter() {
            listener(update.clone());
        }
        Ok(profile)
    }

    pub fn delete_profile(&mut self, id: &maho_types::identifiers::ProfileId) -> bool {
        let spaces_using_profile = self
            .space_manager
            .get_all_spaces()
            .iter()
            .any(|s| s.profile_id == *id);

        if spaces_using_profile {
            let update = CoreUpdate::Error {
                context: "delete_profile".to_string(),
                error: maho_types::events::core_update::MahoError {
                    code: "PROFILE_IN_USE".to_string(),
                    message: "Remove this profile from all spaces before deleting".to_string(),
                    details: None,
                },
            };
            for listener in self.update_listeners.borrow().iter() {
                listener(update.clone());
            }
            return false;
        }

        let data_store_id = self
            .profile_manager
            .get_profile(id)
            .and_then(|p| p.data_store_id.clone());

        let deleted = self.profile_manager.delete_profile(id);
        if deleted {
            self.delete_persisted_profile(id);
            let update = CoreUpdate::ProfileDeleted {
                profile_id: id.clone(),
                data_store_id,
            };
            for listener in self.update_listeners.borrow().iter() {
                listener(update.clone());
            }
        }
        deleted
    }

    pub fn update_profile(
        &mut self,
        id: &maho_types::identifiers::ProfileId,
        name: Option<String>,
        avatar_color: Option<String>,
        download_path: Option<String>,
        archive_timeout_hours: Option<Option<f64>>,
    ) -> Option<maho_types::profile::ProfileConfig> {
        let profile = self
            .profile_manager
            .update_profile(id, name, avatar_color, download_path, archive_timeout_hours)?
            .clone();
        let update = CoreUpdate::ProfileUpdated {
            profile: profile.clone(),
        };
        for listener in self.update_listeners.borrow().iter() {
            listener(update.clone());
        }
        Some(profile)
    }

    pub fn update_profile_persisted(
        &mut self,
        id: &maho_types::identifiers::ProfileId,
        name: Option<String>,
        avatar_color: Option<String>,
        download_path: Option<String>,
        archive_timeout_hours: Option<Option<f64>>,
    ) -> Option<maho_types::profile::ProfileConfig> {
        let result = self
            .profile_manager
            .update_profile(id, name, avatar_color, download_path, archive_timeout_hours)
            .cloned();
        if result.is_some() {
            self.persist_lmdb_now_partial(LMDB_DIRTY_PROFILES);
        }
        let switched = self.profile_manager.switch_profile(id);
        if switched {
            let update = CoreUpdate::ActiveProfileChanged {
                profile_id: id.clone(),
            };
            for listener in self.update_listeners.borrow().iter() {
                listener(update.clone());
            }
        }
        result
    }

    pub fn get_active_profile_id(&self) -> Option<&maho_types::identifiers::ProfileId> {
        self.profile_manager.get_active_profile_id()
    }

    pub fn get_profile_data_store_id(
        &self,
        profile_id: &maho_types::identifiers::ProfileId,
    ) -> Option<String> {
        self.profile_manager
            .get_profile(profile_id)
            .and_then(|p| p.data_store_id.clone())
    }

    pub fn switch_profile(&mut self, id: &maho_types::identifiers::ProfileId) -> bool {
        let switched = self.profile_manager.switch_profile(id);
        if switched {
            self.persist_lmdb_now_partial(LMDB_DIRTY_ACTIVE_PROFILE);
            if let Some(ref storage) = self.storage {
                let _ = storage.set_setting("active_profile_id", &id.to_string());
            }
            let update = CoreUpdate::ActiveProfileChanged {
                profile_id: id.clone(),
            };
            for listener in self.update_listeners.borrow().iter() {
                listener(update.clone());
            }
        }
        switched
    }

    pub fn start_find(
        &mut self,
        tab_id: maho_types::identifiers::TabId,
        query: String,
        case_sensitive: bool,
        whole_word: bool,
    ) -> crate::find_manager::FindSession {
        self.find_manager
            .start_find(tab_id, query, case_sensitive, whole_word)
    }

    pub fn find_next(&mut self) -> Option<u32> {
        self.find_manager.next_match()
    }

    pub fn find_previous(&mut self) -> Option<u32> {
        self.find_manager.previous_match()
    }

    pub fn dismiss_find(&mut self) {
        self.find_manager.dismiss_find()
    }

    pub fn get_atc_rules(&self) -> &[crate::atc_manager::ATCRule] {
        self.atc_manager.get_rules()
    }

    pub fn add_atc_rule(&mut self, rule: crate::atc_manager::ATCRule) -> String {
        self.atc_manager.add_rule(rule)
    }

    pub fn add_atc_rule_persisted(&mut self, rule: crate::atc_manager::ATCRule) -> String {
        let id = self.atc_manager.add_rule(rule.clone());
        if let Some(ref storage) = self.storage {
            let _ = storage.save_atc_rule(
                &rule.id,
                &rule.name,
                rule.space_id.as_ref().map(|s| s.as_ref()),
                rule.url_pattern.as_deref(),
                rule.max_age_hours,
                rule.max_tabs,
                rule.enabled,
                None,
            );
        }
        id
    }

    pub fn remove_atc_rule(&mut self, rule_id: &str) -> Option<crate::atc_manager::ATCRule> {
        self.atc_manager.remove_rule(rule_id)
    }

    pub fn remove_atc_rule_persisted(
        &mut self,
        rule_id: &str,
    ) -> Option<crate::atc_manager::ATCRule> {
        let rule = self.atc_manager.remove_rule(rule_id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_atc_rule(rule_id);
        }
        rule
    }

    pub fn toggle_atc_rule(&mut self, rule_id: &str, enabled: bool) {
        self.atc_manager.toggle_rule(rule_id, enabled)
    }

    pub fn toggle_traffic_rule_persisted(&mut self, rule_id: &str, enabled: bool) {
        if let Some(rule) = self.atc_manager.get_traffic_rules().iter().find(|r| r.id == rule_id).cloned() {
            let mut updated_rule = rule;
            updated_rule.enabled = enabled;
            self.update_traffic_rule_persisted(updated_rule);
        }
    }

    pub fn get_pinned_tabs(&self, space_id: &SpaceId) -> Vec<TabViewModel> {
        self.space_manager
            .get_tab_order(space_id)
            .into_iter()
            .filter_map(|tab_id| self.tab_manager.get_tab(&tab_id))
            .filter(|tab| {
                tab.role == TabRole::Pinned
                    && !matches!(tab.state, TabLifecycleState::Archived { .. })
            })
            .map(|tab| self.make_view_model(tab))
            .collect()
    }

    pub fn get_space_tabs(&self, space_id: &SpaceId) -> Vec<TabViewModel> {
        self.space_manager
            .get_tab_order(space_id)
            .into_iter()
            .filter_map(|tab_id| self.tab_manager.get_tab(&tab_id))
            .filter(|tab| {
                !tab.role.is_pinned() && !matches!(tab.state, TabLifecycleState::Archived { .. })
            })
            .map(|tab| self.make_view_model(tab))
            .collect()
    }

    /// Get unpinned tabs for a space that belong to a specific browser window.
    /// Returns tabs in the space's ordering, filtered to only include those
    /// whose `window_id` matches the given value.
    pub fn get_space_tabs_for_window(
        &self,
        space_id: &SpaceId,
        window_id: i64,
    ) -> Vec<TabViewModel> {
        self.space_manager
            .get_tab_order(space_id)
            .into_iter()
            .filter_map(|tab_id| self.tab_manager.get_tab(&tab_id))
            .filter(|tab| {
                !tab.role.is_pinned()
                    && tab.window_id == Some(window_id)
                    && !matches!(tab.state, TabLifecycleState::Archived { .. })
            })
            .map(|tab| self.make_view_model(tab))
            .collect()
    }

    /// Release all tabs owned by a closing window. The tabs are moved to the
    /// closed stack and removed from the live tab map. Returns the list of
    /// released tab IDs for the C++ side to clean up its TabStripModel.
    pub fn release_window_tabs(&mut self, window_id: i64) -> Vec<TabId> {
        let released = self.tab_manager.release_window_tabs(window_id);
        // Also remove released tabs from their spaces' tab_order
        for tab_id in &released {
            // Find which space this tab belonged to by checking all spaces
            let space_ids: Vec<SpaceId> = self
                .space_manager
                .get_all_spaces()
                .iter()
                .filter(|s| s.tab_order.contains(tab_id))
                .map(|s| s.id.clone())
                .collect();
            for sid in space_ids {
                self.space_manager.remove_tab_from_space(&sid, tab_id);
            }
        }
        released
    }

    pub fn get_favorite_tabs(&self, space_id: &SpaceId) -> Vec<TabViewModel> {
        let profile_space_ids = self
            .space_manager
            .get_space(space_id)
            .map(|space| {
                self.space_manager
                    .get_space_ids_for_profile(&space.profile_id)
            })
            .unwrap_or_default();

        let mut tabs: Vec<TabViewModel> = Vec::new();
        let mut seen_tab_ids = std::collections::HashSet::new();
        for sid in &profile_space_ids {
            for tab_id in self.space_manager.get_tab_order(sid) {
                if !seen_tab_ids.insert(tab_id.clone()) {
                    continue;
                }
                if let Some(tab) = self.tab_manager.get_tab(&tab_id) {
                    if tab.role.is_favorite()
                        && !matches!(tab.state, TabLifecycleState::Archived { .. })
                    {
                        // L3-EXEMPT: role query
                        tabs.push(self.make_view_model(tab));
                    }
                }
            }
        }
        tabs.sort_by_key(|t| t.favorite_order.unwrap_or(u32::MAX));
        tabs
    }

    pub fn css_mod_manager(&self) -> &CssModManager {
        &self.css_mod_manager
    }

    pub fn css_mod_manager_mut(&mut self) -> &mut CssModManager {
        &mut self.css_mod_manager
    }

    pub fn get_today_tabs(&self, space_id: &SpaceId) -> Vec<TabViewModel> {
        self.tab_manager
            .get_today_tabs(space_id)
            .into_iter()
            .map(|tab| self.make_view_model(tab))
            .collect()
    }

    pub fn get_archived_tabs(&self, space_id: &SpaceId) -> Vec<ArchivedTabViewModel> {
        self.tab_manager
            .get_archived_tabs(space_id)
            .into_iter()
            .map(|tab| ArchivedTabViewModel {
                id: tab.id.clone(),
                space_id: tab.space_id.clone(),
                title: tab.title.clone(),
                url: tab.url.0.clone(),
                favicon: tab.favicon.clone(),
                archived_at: self
                    .tab_manager
                     .state_timestamp_for_tab(&tab.id)
                     .map(timestamp_millis_to_iso8601)
                     .unwrap_or_else(|| tab.last_active_at.0.clone()),
            })
            .collect()
    }

    pub fn get_folder_view_models(
        &self,
        space_id: &SpaceId,
    ) -> Vec<maho_types::traits::shell_renderer::FolderViewModel> {
        self.space_manager
            .get_folder_view_models(space_id)
            .into_iter()
            .map(|mut vm| {
                vm.tab_ids.retain(|tid| {
                    self.tab_manager
                        .get_tab(tid)
                        .map(|t| !matches!(t.state, TabLifecycleState::Archived { .. }))
                        .unwrap_or(false)
                });
                vm.tab_count = vm.tab_ids.len();
                vm
            })
            .collect()
    }

    pub fn get_sidebar_tree(
        &self,
        space_id: &SpaceId,
    ) -> Vec<maho_types::traits::shell_renderer::SidebarNode> {
        self.space_manager.get_sidebar_tree(space_id, |tab_id| {
            let tab = self.tab_manager.get_tab(tab_id)?;
            if matches!(tab.state, TabLifecycleState::Archived { .. }) {
                return None;
            }
            Some(self.tab_manager.to_tab_view_model(tab))
        })
    }

    /// Returns a combined JSON blob containing the sidebar tree and base64-encoded
    /// favicons for all tabs in the tree. Replaces the 3-call pattern (audit C3).
    ///
    /// Format: `{"tree": [...nodes...], "favicons": {"tab_id": "base64_png", ...}}`
    pub fn get_sidebar_state_v2(&self, space_id: &SpaceId) -> Option<String> {
        let tree = self.get_sidebar_tree(space_id);
        if tree.is_empty() {
            // Space doesn't exist or has no tabs/folders
            return None;
        }

        // Collect favicons for all tabs in the tree as base64
        let mut favicons = serde_json::Map::new();
        Self::collect_favicons_from_nodes(&tree, &mut favicons);

        let blob = serde_json::json!({
            "tree": tree,
            "favicons": favicons,
        });
        Some(blob.to_string())
    }

    /// Recursively walks sidebar nodes, extracting favicon bytes as base64.
    fn collect_favicons_from_nodes(
        nodes: &[maho_types::traits::shell_renderer::SidebarNode],
        favicons: &mut serde_json::Map<String, serde_json::Value>,
    ) {
        use base64::Engine;
        use maho_types::traits::shell_renderer::SidebarNode;

        for node in nodes {
            match node {
                SidebarNode::Tab { tab } => {
                    if let Some(ref img) = tab.favicon {
                        let b64 = base64::engine::general_purpose::STANDARD.encode(&img.data);
                        favicons.insert(tab.id.to_string(), serde_json::Value::String(b64));
                    }
                }
                SidebarNode::Folder { children, .. } => {
                    Self::collect_favicons_from_nodes(children, favicons);
                }
            }
        }
    }

    /// Returns `true` if the tab identified by `tab_id` exists and has its
    /// `is_pinned` flag set. This is an O(log n) lookup and replaces the
    /// previous O(n) JSON-parse pattern on the C++ side (H2).
    pub fn is_tab_pinned(&self, tab_id: &str) -> bool {
        let tid = maho_types::identifiers::TabId::new(tab_id);
        self.tab_manager
            .get_tab(&tid)
            .map(|tab| tab.role.is_close_protected())
            .unwrap_or(false)
    }

    /// Returns `true` if the tab is close-protected: Pinned OR Favorite. Unlike
    /// [`is_tab_pinned`] (strict Pinned-only), this is the guard used to keep
    /// favorites alive when the user closes their tab (ADR-11).
    pub fn is_tab_close_protected(&self, tab_id: &str) -> bool {
        let tid = maho_types::identifiers::TabId::new(tab_id);
        self.tab_manager
            .get_tab(&tid)
            .map(|tab| tab.role.is_close_protected() || tab.role.is_favorite()) // L3-EXEMPT: close-guard covers Pinned OR Favorite
            .unwrap_or(false)
    }

    pub fn find_tabs_older_than(&self, cutoff_ts: i64, space_id: &SpaceId) -> Vec<TabId> {
        self.tab_manager.find_tabs_older_than(cutoff_ts, space_id)
    }

    pub fn favorite_tab(&mut self, tab_id: &TabId) -> bool {
        if self
            .tab_manager
            .get_tab(tab_id)
            .map(|tab| tab.role.is_favorite())
            .unwrap_or(false)
        {
            // L3-EXEMPT: role query
            return true;
        }

        let profile_space_ids = self
            .tab_manager
            .get_tab(tab_id)
            .and_then(|tab| self.space_manager.get_space(&tab.space_id))
            .map(|space| {
                self.space_manager
                    .get_space_ids_for_profile(&space.profile_id)
            })
            .unwrap_or_default();

        if self
            .tab_manager
            .count_favorite_tabs_in_spaces(&profile_space_ids)
            >= crate::tab_lifecycle::MAX_FAVORITES
        {
            return false;
        }

        let max_order = self
            .tab_manager
            .get_all_favorite_tabs()
            .iter()
            .filter(|t| profile_space_ids.contains(&t.space_id))
            .filter_map(|t| t.role.favorite_order())
            .max()
            .unwrap_or(0);

        let new_role = TabRole::Favorite {
            order: max_order + 1,
        };
        let _updates = self.transition_tab_role(tab_id, new_role);
        true
    }

    pub fn transition_tab_role(&mut self, id: &TabId, new_role: TabRole) -> Vec<CoreUpdate> {
        let old = self.tab_manager.get_tab(id).map(|t| t.role.clone());
        if old.as_ref() == Some(&new_role) {
            return vec![];
        }
        if !self.tab_manager.transition_tab_role(id, new_role.clone()) {
            return vec![];
        }
        if let Some(tab) = self.tab_manager.get_tab(id) {
            let space_id = tab.space_id.clone();
            let is_in_folder = self.space_manager.is_tab_in_folder(&space_id, id);
            self.space_manager.apply_tab_residency(
                &space_id,
                id,
                new_role.is_favorite(),
                is_in_folder, // L3-EXEMPT: role query
            );

            if new_role.is_favorite() {
                // L3-EXEMPT: role query
                let folder_ids: Vec<_> = self
                    .space_manager
                    .get_folder_view_models(&space_id)
                    .into_iter()
                    .map(|f| f.id)
                    .collect();
                for folder_id in folder_ids {
                    self.space_manager
                        .remove_tab_from_folder(&space_id, &folder_id, id);
                }
                let mut favorited = std::collections::HashSet::new();
                favorited.insert(id.clone());
                self.space_manager
                    .strip_favorited_tabs_from_root_orders(&favorited);
            }

            self.enforce_residency_for_tab(id);
            self.persist_lmdb_now_partial(LMDB_DIRTY_TABS | LMDB_DIRTY_SPACES);
            self.push_tab_sync(id);
            self.push_space_sync(&space_id);
        }
        vec![CoreUpdate::TabRoleChanged {
            tab_id: id.clone(),
            old_role: old,
            new_role,
        }]
    }

    fn enforce_residency_for_tab(&mut self, tab_id: &TabId) {
        let owner = match self.tab_manager.get_tab(tab_id).map(|t| t.space_id.clone()) {
            Some(s) => s,
            None => return,
        };
        let mut owners = std::collections::HashMap::new();
        owners.insert(tab_id.clone(), owner);
        {
            let tm = &self.tab_manager;
            self.space_manager
                .enforce_cross_space_tab_residency(&owners, &|tid| {
                    tm.get_tab(tid)
                        .map(|t| t.role.is_favorite())
                        .unwrap_or(false) // L3-EXEMPT: role query
                });
        }
    }

    pub fn reset_pinned_tab(&mut self, tab_id: &TabId) -> bool {
        self.tab_manager.reset_pinned_tab(tab_id)
    }

    pub fn check_pinned_navigation(&self, tab_id: &TabId, new_url: &Url) -> Option<CoreUpdate> {
        let tab = self.tab_manager.get_tab(tab_id)?;
        if TabLifecycleManager::should_open_in_peek(tab, new_url) {
            Some(CoreUpdate::OpenPeekTab {
                url: new_url.clone(),
                source_tab_id: tab_id.clone(),
            })
        } else {
            None
        }
    }

    pub fn set_tab_parent(&mut self, tab_id: &TabId, new_parent_id: Option<TabId>) -> bool {
        self.tab_manager.set_tab_parent(tab_id, new_parent_id)
    }

    pub fn get_boost_view_models(&self) -> Vec<maho_types::traits::shell_renderer::BoostViewModel> {
        self.boost_manager
            .get_all_boosts()
            .into_iter()
            .map(|b| {
                let enabled = self
                    .boost_manager
                    .get_active(&b.domain)
                    .map(|a| a.id == b.id)
                    .unwrap_or(false);
                maho_types::traits::shell_renderer::BoostViewModel {
                    id: b.id.clone(),
                    domain: b.domain.clone(),
                    name: b.name.clone(),
                    custom_css: Some(self.boost_manager.compose_css(b, None)),
                    enabled,
                }
            })
            .collect()
    }

    pub fn get_installed_extensions(&self) -> Vec<crate::extension_bridge::InstalledExtension> {
        self.extension_bridge
            .get_installed_extensions()
            .into_iter()
            .cloned()
            .collect()
    }

    pub fn toggle_filter_list(&mut self, id: &str, enabled: bool) {
        self.content_blocker.toggle_filter_list(id, enabled);
    }

    pub fn remove_filter_list(&mut self, id: &str) {
        self.content_blocker.remove_filter_list(id);
    }

    pub fn toggle_extension(&mut self, extension_id: &str) -> Option<bool> {
        let res = self.extension_bridge.toggle_extension(extension_id);
        if res.is_some() {
            let update = CoreUpdate::InstalledExtensionsUpdated;
            for listener in self.update_listeners.borrow().iter() {
                listener(update.clone());
            }
        }
        res
    }

    pub fn remove_extension(
        &mut self,
        extension_id: &str,
    ) -> Option<crate::extension_bridge::InstalledExtension> {
        let res = self.extension_bridge.remove_extension(extension_id);
        if res.is_some() {
            let update = CoreUpdate::InstalledExtensionsUpdated;
            for listener in self.update_listeners.borrow().iter() {
                listener(update.clone());
            }
        }
        res
    }

    pub fn register_extension(&mut self, ext: crate::extension_bridge::InstalledExtension) {
        self.extension_bridge.register_extension(ext);
        let update = CoreUpdate::InstalledExtensionsUpdated;
        for listener in self.update_listeners.borrow().iter() {
            listener(update.clone());
        }
    }

    pub fn register_extension_sync_callback(
        &mut self,
        callback: Option<Box<dyn Fn(&str, bool, bool) + Send + Sync>>,
    ) {
        self.extension_bridge.sync_callback = callback;
    }

    pub fn register_side_panel(
        &mut self,
        extension_id: &str,
        options: crate::extension_bridge::SidePanelOptions,
    ) {
        self.extension_bridge
            .register_side_panel(extension_id, options);
    }

    pub fn unregister_side_panel(&mut self, extension_id: &str) {
        self.extension_bridge.unregister_side_panel(extension_id);
    }

    pub fn get_side_panel_options(
        &self,
        extension_id: &str,
    ) -> Option<crate::extension_bridge::SidePanelOptions> {
        self.extension_bridge.side_panels.get(extension_id).cloned()
    }

    pub fn set_installed_extensions(
        &mut self,
        exts: Vec<crate::extension_bridge::InstalledExtension>,
    ) {
        for ext in &exts {
            let old_ext = self.extension_bridge.extensions.get(&ext.id);
            let state_changed = match old_ext {
                Some(old) => old.enabled != ext.enabled,
                None => true,
            };
            if state_changed {
                let cws_url = format!("https://chromewebstore.google.com/detail/{}", ext.id);
                let payload = serde_json::json!({
                    "id": ext.id,
                    "cws_url": cws_url,
                    "enabled": ext.enabled,
                });
                match self.next_hlc_ts() {
                    Ok(version) => {
                        let device_id = self.account_manager.device_id_hash();
                        let payload_str = payload.to_string();
                        let fields_hlc_json =
                            crate::sync_models::build_fields_hlc(&payload_str, version, device_id);
                        let sync_entity = SyncEntity {
                            entity_type: SyncEntityType::Extension,
                            entity_id: ext.id.clone(),
                            version,
                            device_id,
                            schema_version: 1,
                            modified_at: chrono::Utc::now().timestamp(),
                            payload_json: payload_str,
                            deleted: false,
                            queue_row_id: None,
                            fields_hlc_json,
                        };
                        self.sync_push_entity_persisted(sync_entity);
                    }
                    Err(e) => {
                        eprintln!(
                            "[sync] Skipping extension push for '{}': HLC failure: {}",
                            ext.id, e
                        );
                    }
                }
            }
        }

        self.extension_bridge.set_installed_extensions(exts);
        let update = CoreUpdate::InstalledExtensionsUpdated;
        for listener in self.update_listeners.borrow().iter() {
            listener(update.clone());
        }
    }

    pub fn clear_installed_extensions(&mut self) {
        self.extension_bridge.clear_installed_extensions();
        let update = CoreUpdate::InstalledExtensionsUpdated;
        for listener in self.update_listeners.borrow().iter() {
            listener(update.clone());
        }
    }

    pub fn get_boosts_for_url(
        &self,
        url: &str,
    ) -> Vec<maho_types::traits::shell_renderer::BoostViewModel> {
        let host = extract_domain_from_url(url);
        let parts: Vec<&str> = host.as_deref().unwrap_or("").split('.').collect();
        for i in 0..parts.len() {
            let domain = parts[i..].join(".");
            if let Some(active) = self.boost_manager.get_active(&domain) {
                let enabled = true;
                return vec![maho_types::traits::shell_renderer::BoostViewModel {
                    id: active.id.clone(),
                    domain: active.domain.clone(),
                    name: active.name.clone(),
                    custom_css: Some(self.boost_manager.compose_css(&active, None)),
                    enabled,
                }];
            }
        }
        vec![]
    }

    pub fn get_note_view_models(&self) -> Vec<maho_types::traits::shell_renderer::NoteViewModel> {
        self.note_manager.get_all_view_models()
    }

    pub fn get_download_view_models(
        &self,
    ) -> Vec<maho_types::traits::shell_renderer::DownloadViewModel> {
        self.download_manager.get_all_view_models()
    }

    pub fn get_settings_view_model(&self) -> maho_types::traits::shell_renderer::SettingsViewModel {
        use maho_types::traits::shell_renderer::{SettingsItem, SettingsItemType, SettingsSection};
        let mut vm = self.settings_manager.to_view_model();

        // Add Profiles section
        let profiles_section = SettingsSection {
            title: "Profiles".to_string(),
            items: self
                .profile_manager
                .list_profiles()
                .iter()
                .map(|p| SettingsItem {
                    key: format!("profile_{}", p.id),
                    label: p.name.clone(),
                    item_type: SettingsItemType::Select,
                    value: serde_json::json!({
                        "id": p.id.to_string(),
                        "name": p.name,
                        "avatarColor": p.avatar_color,
                        "isActive": self.profile_manager.get_active_profile_id()
                            .map(|id| id == &p.id).unwrap_or(false),
                    }),
                })
                .collect(),
        };
        vm.sections.push(profiles_section);

        // Add Spaces section
        let spaces_section = SettingsSection {
            title: "Spaces".to_string(),
            items: self
                .space_manager
                .get_space_configs()
                .iter()
                .map(|(id, name, color)| SettingsItem {
                    key: format!("space_{}", id),
                    label: name.clone(),
                    item_type: SettingsItemType::Color,
                    value: serde_json::json!({
                        "id": id.to_string(),
                        "name": name,
                        "color": {
                            "hue": color.hue,
                            "saturation": color.saturation,
                            "brightness": color.brightness,
                        },
                    }),
                })
                .collect(),
        };
        vm.sections.push(spaces_section);

        // Add Links (Air Traffic Control) section
        let links_section = SettingsSection {
            title: "Links".to_string(),
            items: self
                .atc_manager
                .get_traffic_rules()
                .iter()
                .map(|rule| SettingsItem {
                    key: format!("traffic_rule_{}", rule.id),
                    label: rule.url_pattern.clone(),
                    item_type: SettingsItemType::UrlPattern,
                    value: serde_json::json!({
                        "id": rule.id,
                        "urlPattern": rule.url_pattern,
                        "matchType": rule.match_type,
                        "targetSpaceId": rule.target_space_id.to_string(),
                        "enabled": rule.enabled,
                    }),
                })
                .collect(),
        };
        vm.sections.push(links_section);

        // Add Account section
        let account_items = if let Some(account) = self.account_manager.get_account() {
            vec![
                SettingsItem {
                    key: "account_email".to_string(),
                    label: "Email".to_string(),
                    item_type: SettingsItemType::Text,
                    value: serde_json::json!(account.email),
                },
                SettingsItem {
                    key: "account_sync".to_string(),
                    label: "Sync".to_string(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!(account.sync_enabled),
                },
            ]
        } else {
            vec![SettingsItem {
                key: "account_signed_out".to_string(),
                label: "Sign In".to_string(),
                item_type: SettingsItemType::Text,
                value: serde_json::json!(null),
            }]
        };
        let account_section = SettingsSection {
            title: "Account".to_string(),
            items: account_items,
        };
        vm.sections.push(account_section);

        // Add Extensions section
        let extensions_section = SettingsSection {
            title: "Extensions".to_string(),
            items: self
                .extension_bridge
                .get_installed_extensions()
                .iter()
                .map(|ext| SettingsItem {
                    key: format!("extension_{}", ext.id),
                    label: ext.name.clone(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!({
                        "id": ext.id,
                        "name": ext.name,
                        "version": ext.version,
                        "enabled": ext.enabled,
                    }),
                })
                .collect(),
        };
        vm.sections.push(extensions_section);

        let boost_items: Vec<SettingsItem> = self
            .boost_manager
            .get_all_boosts()
            .into_iter()
            .map(|b| {
                let is_active = self
                    .boost_manager
                    .get_active(&b.domain)
                    .map(|a| a.id == b.id)
                    .unwrap_or(false);
                SettingsItem {
                    key: format!("boost_{}", b.id),
                    label: b.domain.clone(),
                    item_type: SettingsItemType::Toggle,
                    value: serde_json::json!({
                        "id": b.id.to_string(),
                        "domain": b.domain,
                        "enabled": is_active,
                        "hasCss": !b.custom_css.is_empty(),
                    }),
                }
            })
            .collect();
        let boosts_section = SettingsSection {
            title: "Boosts".to_string(),
            items: boost_items,
        };
        vm.sections.push(boosts_section);

        // Passwords section
        let passwords_section = SettingsSection {
            title: "Passwords".to_string(),
            items: self
                .password_manager
                .list_passwords()
                .iter()
                .map(|p| SettingsItem {
                    key: format!("password_{}", p.id),
                    label: p.domain.clone(),
                    item_type: SettingsItemType::Text,
                    value: serde_json::json!({
                        "id": p.id,
                        "domain": p.domain,
                        "username": p.username,
                        "createdAt": p.created_at,
                        "lastUsed": p.last_used,
                    }),
                })
                .collect(),
        };
        vm.sections.push(passwords_section);

        let autofill_section = SettingsSection {
            title: "Autofill".to_string(),
            items: {
                let mut items = vec![
                    SettingsItem {
                        key: "autofill_addresses_enabled".to_string(),
                        label: "Autofill Addresses".to_string(),
                        item_type: SettingsItemType::Toggle,
                        value: serde_json::json!(
                            self.settings_manager
                                .get_settings()
                                .autofill
                                .addresses_enabled
                        ),
                    },
                    SettingsItem {
                        key: "autofill_payments_enabled".to_string(),
                        label: "Autofill Payments".to_string(),
                        item_type: SettingsItemType::Toggle,
                        value: serde_json::json!(
                            self.settings_manager
                                .get_settings()
                                .autofill
                                .payments_enabled
                        ),
                    },
                ];
                for addr in self.autofill_manager.list_addresses() {
                    items.push(SettingsItem {
                        key: format!("autofill_address_{}", addr.id),
                        label: addr.name.clone(),
                        item_type: SettingsItemType::Text,
                        value: serde_json::json!({
                            "id": addr.id,
                            "name": addr.name,
                            "city": addr.city,
                            "country": addr.country,
                        }),
                    });
                }
                for pay in self.autofill_manager.list_payments() {
                    items.push(SettingsItem {
                        key: format!("autofill_payment_{}", pay.id),
                        label: format!("**** {}", pay.last_four),
                        item_type: SettingsItemType::Text,
                        value: serde_json::json!({
                            "id": pay.id,
                            "cardName": pay.card_name,
                            "lastFour": pay.last_four,
                            "expiry": pay.expiry,
                        }),
                    });
                }
                items
            },
        };
        vm.sections.push(autofill_section);

        vm
    }

    pub fn get_find_bar_state(&self) -> Option<maho_types::traits::shell_renderer::FindBarState> {
        self.find_manager.to_find_bar_state()
    }

    pub fn get_crash_recovery(&self) -> &CrashRecoveryManager {
        &self.crash_recovery
    }

    pub fn get_crash_recovery_mut(&mut self) -> &mut CrashRecoveryManager {
        &mut self.crash_recovery
    }

    pub fn save_form_data(&mut self, tab_id: &str, form_json: String) {
        let typed_tab_id = TabId::new(tab_id);
        if self.tab_id_is_private(&typed_tab_id) {
            return;
        }
        self.crash_recovery.save_form_data(tab_id, form_json);
    }

    pub fn set_crash_save_interval(&mut self, seconds: u64) {
        self.crash_recovery.set_save_interval(seconds);
    }

    pub fn get_import_export(&self) -> &ImportExportManager {
        &self.import_export
    }

    pub fn get_import_export_mut(&mut self) -> &mut ImportExportManager {
        &mut self.import_export
    }

    pub fn add_content_blocker_site_exception(&mut self, origin: &str) {
        self.content_blocker.add_site_exception(origin.to_string());
    }

    pub fn remove_content_blocker_site_exception(&mut self, origin: &str) {
        self.content_blocker.remove_site_exception(origin);
    }

    pub fn get_content_blocker_site_exceptions(&self) -> Vec<&str> {
        self.content_blocker.get_site_exceptions()
    }

    pub fn should_block_request(&self, url: &str, source_url: &str, request_type: &str) -> bool {
        self.content_blocker
            .should_block_request(url, source_url, request_type)
    }

    pub fn check_request(
        &self,
        url: &str,
        source_url: &str,
        request_type: &str,
    ) -> crate::content_blocker::BlockResult {
        self.content_blocker
            .check_request(url, source_url, request_type)
    }

    pub fn get_cosmetic_resources(
        &self,
        url: &str,
    ) -> crate::content_blocker::CosmeticResourcesResponse {
        self.content_blocker.get_cosmetic_resources(url)
    }

    pub fn add_filter_list(&mut self, id: String, name: String, url: String) {
        self.content_blocker.add_filter_list(id, name, url);
    }

    pub fn update_filter_list_content(&mut self, id: &str, content: String) {
        self.content_blocker.update_filter_list_content(id, content);
    }

    pub fn rebuild_content_rules(&mut self) {
        self.content_blocker.rebuild_engine();
    }

    pub fn get_content_rules(&self) -> String {
        self.content_blocker.get_content_rules()
    }

    pub fn get_filter_lists_json(&self) -> String {
        serde_json::to_string(self.content_blocker.get_filter_lists())
            .unwrap_or_else(|_| "[]".to_string())
    }

    pub fn toggle_content_blocker(&mut self, enabled: bool) {
        self.content_blocker.set_enabled(enabled);
    }

    pub fn toggle_popup_blocking(&mut self, enabled: bool) {
        self.content_blocker.set_popup_blocking(enabled);
    }

    pub fn is_content_blocker_enabled(&self) -> bool {
        self.content_blocker.is_enabled()
    }

    pub fn is_popup_blocking_enabled(&self) -> bool {
        self.content_blocker.is_popup_blocking_enabled()
    }

    pub fn get_content_rule_count(&self) -> usize {
        self.content_blocker.get_rule_count()
    }

    pub fn save_content_engine_cache(&self, path: &str) -> Result<(), String> {
        let data = self.content_blocker.serialize_engine();
        std::fs::write(path, data).map_err(|e| format!("{e}"))
    }

    pub fn load_content_engine_cache(&mut self, path: &str) -> Result<(), String> {
        let data = std::fs::read(path).map_err(|e| format!("{e}"))?;
        self.content_blocker.deserialize_engine(&data)
    }

    pub fn get_search_url(&self, query: &str) -> String {
        self.command_bar.get_search_url(query)
    }

    pub fn get_search_engines(&self) -> Vec<maho_types::search_engine::SearchEngineViewModel> {
        self.command_bar.get_search_engines()
    }

    pub fn get_recent_searches(&self) -> Vec<String> {
        self.command_bar.get_recent_searches().to_vec()
    }

    pub fn save_search(&mut self, query: String) {
        self.command_bar.save_search(query);
    }

    pub fn add_search_engine(&mut self, engine: maho_types::search_engine::SearchEngine) {
        self.command_bar.add_search_engine(engine);
    }

    pub fn remove_search_engine(&mut self, id: &str) {
        self.command_bar.remove_search_engine(id);
    }

    pub fn set_default_search_engine(&mut self, id: &str) -> bool {
        self.command_bar.set_default_search_engine(id)
    }

    pub fn add_search_engine_persisted(&mut self, engine: maho_types::search_engine::SearchEngine) {
        self.command_bar.add_search_engine(engine.clone());
        if let Some(ref storage) = self.storage {
            let _ = storage.save_search_engine(
                &engine.id,
                &engine.name,
                &engine.url_template,
                engine.shortcut.as_deref(),
                engine.icon_url.as_deref(),
                engine.is_default,
            );
        }
        if let Ok(payload) = serde_json::to_string(&engine) {
            self.push_sync_entity(
                SyncEntityType::SearchEngine,
                engine.id.clone(),
                payload,
                false,
            );
        }
    }

    pub fn remove_search_engine_persisted(&mut self, id: &str) {
        self.command_bar.remove_search_engine(id);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_search_engine(id);
        }
        self.push_sync_entity(
            SyncEntityType::SearchEngine,
            id.to_string(),
            "{}".to_string(),
            true,
        );
    }

    pub fn set_default_search_engine_persisted(&mut self, id: &str) -> bool {
        let result = self.command_bar.set_default_search_engine(id);
        if result {
            if let Some(ref storage) = self.storage {
                let _ = storage.update_default_search_engine(id);
            }
        }
        result
    }

    pub fn save_search_persisted(&mut self, query: String) {
        self.command_bar.save_search(query.clone());
        if let Some(ref storage) = self.storage {
            let _ = storage.save_search(&query);
        }
    }

    fn make_view_model(&self, tab: &Tab) -> TabViewModel {
        TabViewModel {
            id: tab.id.clone(),
            space_id: tab.space_id.clone(),
            title: tab.title.clone(),
            custom_title: tab.custom_title.clone(),
            url: tab.url.0.clone(),
            favicon: tab.favicon.clone(),
            is_loading: self.tab_manager.is_tab_loading(&tab.id),
            is_pinned: tab.role.is_pinned(),
            is_favorite: tab.role.is_favorite(), // L3-EXEMPT: views display
            is_muted: tab.is_muted,
            is_playing_audio: false,
            lifecycle_state: tab.state.kind_str().to_string(),
            children: vec![],
            created_at: tab.created_at.clone(),
            last_active_at: tab.last_active_at.clone(),
            favorite_order: tab.role.favorite_order(),
            role: tab.role.clone(),
            is_private: tab.is_private,
        }
    }

    // === Tab Preview Methods ===

    pub fn update_tab_preview(&mut self, tab_id: &TabId, thumbnail: maho_types::common::ImageData) {
        self.tab_preview_manager
            .update_preview(tab_id.clone(), thumbnail);
    }

    pub fn get_tab_preview(
        &self,
        tab_id: &TabId,
    ) -> Option<&crate::tab_preview_manager::TabPreview> {
        self.tab_preview_manager.get_preview(tab_id)
    }

    pub fn schedule_preview_capture(&mut self, tab_id: TabId) -> bool {
        self.tab_preview_manager.schedule_capture(tab_id)
    }

    pub fn take_pending_preview_captures(&mut self) -> Vec<TabId> {
        self.tab_preview_manager.take_pending_captures()
    }

    pub fn has_tab_preview(&self, tab_id: &TabId) -> bool {
        self.tab_preview_manager.has_preview(tab_id)
    }

    pub fn remove_tab_preview(&mut self, tab_id: &TabId) {
        self.tab_preview_manager.remove_preview(tab_id);
    }

    pub fn add_bookmark_entry(
        &mut self,
        title: String,
        url: String,
        folder_id: Option<String>,
        favicon: Option<String>,
    ) -> crate::bookmark_manager::Bookmark {
        self.bookmark_manager
            .add_bookmark(title, url, folder_id, favicon)
    }

    pub fn remove_bookmark_entry(
        &mut self,
        bookmark_id: &str,
    ) -> Option<crate::bookmark_manager::Bookmark> {
        self.bookmark_manager.remove_bookmark(bookmark_id)
    }

    pub fn get_all_bookmark_entries(&self) -> Vec<&crate::bookmark_manager::Bookmark> {
        self.bookmark_manager.get_all_bookmarks()
    }

    pub fn search_bookmark_entries(&self, query: &str) -> Vec<&crate::bookmark_manager::Bookmark> {
        self.bookmark_manager.search_bookmarks(query)
    }

    pub fn move_bookmark_entry(&mut self, bookmark_id: &str, folder_id: Option<String>) {
        self.bookmark_manager.move_bookmark(bookmark_id, folder_id)
    }

    pub fn create_bookmark_folder(
        &mut self,
        name: String,
        parent_id: Option<String>,
    ) -> crate::bookmark_manager::BookmarkFolder {
        self.bookmark_manager.create_folder(name, parent_id)
    }

    pub fn delete_bookmark_folder(
        &mut self,
        folder_id: &str,
    ) -> Option<crate::bookmark_manager::BookmarkFolder> {
        self.bookmark_manager.delete_folder(folder_id)
    }

    pub fn create_bookmark_folder_persisted(
        &mut self,
        name: String,
        parent_id: Option<String>,
    ) -> crate::bookmark_manager::BookmarkFolder {
        let folder = self.bookmark_manager.create_folder(name, parent_id);
        if let Some(ref storage) = self.storage {
            let _ = storage.save_bookmark_folder(
                &folder.id,
                &folder.name,
                folder.parent_id.as_deref(),
                &folder.created_at.0,
            );
        }
        if let Ok(payload) = serde_json::to_string(&folder) {
            self.push_sync_entity(
                SyncEntityType::BookmarkFolder,
                folder.id.clone(),
                payload,
                false,
            );
        }
        folder
    }

    pub fn delete_bookmark_folder_persisted(
        &mut self,
        folder_id: &str,
    ) -> Option<crate::bookmark_manager::BookmarkFolder> {
        let folder = self.bookmark_manager.delete_folder(folder_id);
        if folder.is_some() {
            if let Some(ref storage) = self.storage {
                let _ = storage.delete_bookmark_folder(folder_id);
            }
        }
        self.push_sync_entity(
            SyncEntityType::BookmarkFolder,
            folder_id.to_string(),
            "{}".to_string(),
            true,
        );
        folder
    }

    pub fn search_history_with_offset(
        &self,
        query: &str,
        limit: usize,
        offset: usize,
    ) -> Vec<(String, String, String)> {
        if let Some(ref storage) = self.storage {
            let results = storage
                .search_history(query, limit + offset)
                .unwrap_or_default();
            results.into_iter().skip(offset).collect()
        } else {
            Vec::new()
        }
    }

    #[allow(clippy::type_complexity)]
    pub fn get_history_grouped_by_date(&self) -> Vec<(String, Vec<(String, String, String)>)> {
        if let Some(ref storage) = self.storage {
            let all_entries = storage.search_history("", 10000).unwrap_or_default();
            let mut groups: std::collections::HashMap<String, Vec<(String, String, String)>> =
                std::collections::HashMap::new();
            let mut date_order: Vec<String> = Vec::new();
            for (url, title, visited_at) in all_entries {
                let date = visited_at
                    .split('T')
                    .next()
                    .unwrap_or(&visited_at)
                    .split(' ')
                    .next()
                    .unwrap_or(&visited_at)
                    .to_string();
                if !groups.contains_key(&date) {
                    date_order.push(date.clone());
                }
                groups
                    .entry(date)
                    .or_default()
                    .push((url, title, visited_at));
            }
            date_order
                .into_iter()
                .filter_map(|date| groups.remove(&date).map(|entries| (date, entries)))
                .collect()
        } else {
            Vec::new()
        }
    }

    pub fn get_notifications(
        &self,
    ) -> Vec<maho_types::traits::shell_renderer::NotificationViewModel> {
        self.notification_manager.get_all()
    }

    pub fn dismiss_notification(&mut self, notification_id: &str) {
        self.notification_manager.dismiss(notification_id);
    }

    pub fn dismiss_all_notifications(&mut self) {
        self.notification_manager.dismiss_all();
    }

    pub fn set_notification_filter(&mut self, origin: String, allowed: bool) {
        self.notification_manager.set_filter(origin, allowed);
    }

    pub fn get_zoom_for_site(&self, site: &str) -> f64 {
        self.settings_manager.get_zoom_for_site(site)
    }

    pub fn set_zoom_for_site(&mut self, site: String, zoom: f64) {
        self.settings_manager.set_zoom_for_site(&site, zoom);
    }

    pub fn notification_manager_queue_test(
        &mut self,
        origin: String,
        title: String,
        message: String,
    ) -> Option<maho_types::traits::shell_renderer::NotificationViewModel> {
        self.notification_manager
            .queue_notification(origin, title, message, None, vec![])
    }

    pub fn toggle_reader_mode(&mut self, tab_id_str: &str) -> bool {
        let tab_id = maho_types::identifiers::TabId::new(tab_id_str);
        let toggled = self.tab_manager.toggle_reader_mode(&tab_id);
        if !toggled {
            self.emit_error(
                "toggle_reader_mode",
                "TAB_NOT_FOUND",
                "Tab not found for reader mode",
            );
        }
        toggled
    }

    pub fn is_reader_mode(&self, tab_id_str: &str) -> bool {
        let tab_id = maho_types::identifiers::TabId::new(tab_id_str);
        self.tab_manager.is_reader_mode(&tab_id)
    }

    pub fn record_command_bar_usage(&mut self, item_key: &str) {
        self.command_bar.record_usage(item_key);
        if let Some(storage) = &self.storage {
            let _ = storage.increment_usage(item_key, "command_bar");
        }
    }

    pub fn get_top_used_commands(&self, limit: usize) -> Vec<(String, u32)> {
        if let Some(storage) = &self.storage {
            storage
                .get_top_used("command_bar", limit)
                .unwrap_or_default()
        } else {
            Vec::new()
        }
    }

    pub fn link_note_to_tab(&mut self, note_id_str: &str, tab_id_str: &str) {
        let note_id = maho_types::identifiers::NoteId::new(note_id_str);
        let tab_id = maho_types::identifiers::TabId::new(tab_id_str);
        self.note_manager.link_note_to_tab(&note_id, tab_id);
        if let Some(note) = self.note_manager.get_note(&note_id) {
            self.persist_note(note);
            if let Ok(payload) = serde_json::to_string(note) {
                self.push_sync_entity(
                    SyncEntityType::Note,
                    note_id_str.to_string(),
                    payload,
                    false,
                );
            }
        }
    }

    pub fn link_note_to_url(&mut self, note_id_str: &str, url: &str) {
        let note_id = maho_types::identifiers::NoteId::new(note_id_str);
        self.note_manager
            .link_note_to_url(&note_id, url.to_string());
        if let Some(note) = self.note_manager.get_note(&note_id) {
            self.persist_note(note);
            if let Ok(payload) = serde_json::to_string(note) {
                self.push_sync_entity(
                    SyncEntityType::Note,
                    note_id_str.to_string(),
                    payload,
                    false,
                );
            }
        }
    }

    pub fn unlink_note_from_tab(&mut self, note_id_str: &str) {
        let note_id = maho_types::identifiers::NoteId::new(note_id_str);
        self.note_manager.unlink_note_from_tab(&note_id);
        if let Some(note) = self.note_manager.get_note(&note_id) {
            self.persist_note(note);
            if let Ok(payload) = serde_json::to_string(note) {
                self.push_sync_entity(
                    SyncEntityType::Note,
                    note_id_str.to_string(),
                    payload,
                    false,
                );
            }
        }
    }

    pub fn get_linked_tab_id(&self, note_id_str: &str) -> Option<String> {
        let note_id = maho_types::identifiers::NoteId::new(note_id_str);
        self.note_manager
            .get_linked_tab_id(&note_id)
            .map(|t| t.as_ref().to_string())
    }

    pub fn export_notes(&self, format_str: &str) -> String {
        let format = match format_str {
            "html" => crate::note_manager::NoteExportFormat::Html,
            "json" => crate::note_manager::NoteExportFormat::Json,
            _ => crate::note_manager::NoteExportFormat::Markdown,
        };
        self.note_manager.export_notes(format)
    }

    pub fn export_single_note(&self, note_id_str: &str, format_str: &str) -> Option<String> {
        let note_id = maho_types::identifiers::NoteId::new(note_id_str);
        let format = match format_str {
            "html" => crate::note_manager::NoteExportFormat::Html,
            "json" => crate::note_manager::NoteExportFormat::Json,
            _ => crate::note_manager::NoteExportFormat::Markdown,
        };
        self.note_manager.export_single_note(&note_id, format)
    }

    pub fn search_notes_fts(&self, query: &str) -> Vec<(String, f64)> {
        if let Some(storage) = &self.storage {
            storage.search_notes_fts(query).unwrap_or_default()
        } else {
            Vec::new()
        }
    }

    pub fn get_reader_settings_json(&self) -> String {
        serde_json::to_string(self.settings_manager.get_reader_settings())
            .unwrap_or_else(|_| "{}".to_string())
    }

    pub fn create_backup(&mut self, config_json: &str) -> Result<Vec<u8>, String> {
        let config: crate::backup_manager::BackupConfig =
            serde_json::from_str(config_json).map_err(|e| e.to_string())?;
        let data = crate::backup_manager::BackupData {
            metadata: crate::backup_manager::BackupMetadata {
                id: uuid::Uuid::new_v4().to_string(),
                created_at: maho_types::common::DateTime::now(),
                version: 1,
                encryption: config.encryption.clone(),
                size_bytes: 0,
                item_counts: crate::backup_manager::BackupItemCounts {
                    bookmarks: self.bookmark_manager.get_all_bookmarks().len(),
                    history_entries: 0,
                    notes: self.note_manager.get_all_view_models().len(),
                    boosts: self.boost_manager.get_all_boosts().len(),
                    spaces: self.space_manager.get_all_spaces().len(),
                    settings: config.include_settings,
                },
                description: config.description.clone(),
            },
            bookmarks: if config.include_bookmarks {
                Some(
                    self.bookmark_manager
                        .get_all_bookmarks()
                        .iter()
                        .map(|b| {
                            serde_json::json!({
                                "id": b.id.0,
                                "title": b.title,
                                "url": b.url,
                                "folder_id": b.folder_id,
                                "favicon": b.favicon,
                                "created_at": b.created_at.0,
                            })
                        })
                        .collect(),
                )
            } else {
                None
            },
            history: None,
            notes: if config.include_notes {
                Some(
                    self.note_manager
                        .get_all_view_models()
                        .iter()
                        .map(|n| {
                            serde_json::json!({
                                "id": n.id,
                                "content": n.content,
                                "linked_url": n.linked_url,
                            })
                        })
                        .collect(),
                )
            } else {
                None
            },
            boosts: if config.include_boosts {
                Some(
                    self.boost_manager
                        .get_all_boosts()
                        .iter()
                        .map(|b| {
                            serde_json::json!({
                                "domain": b.domain,
                                "name": b.name,
                                "custom_css": b.custom_css,
                                "enabled": self.boost_manager.get_active(&b.domain).map(|active| active.id == b.id).unwrap_or(false),
                            })
                        })
                        .collect(),
                )
            } else {
                None
            },
            spaces: if config.include_spaces {
                Some(
                    self.space_manager
                        .get_all_spaces()
                        .iter()
                        .map(|s| serde_json::to_value(s).unwrap_or_default())
                        .collect(),
                )
            } else {
                None
            },
            settings: if config.include_settings {
                serde_json::to_value(self.settings_manager.get_settings()).ok()
            } else {
                None
            },
        };
        self.backup_manager
            .create_backup(&config, data)
            .map_err(|e| e.to_string())
    }

    pub fn restore_backup(
        &mut self,
        data: &[u8],
        password: Option<&str>,
    ) -> Result<String, String> {
        let backup_data = self
            .backup_manager
            .restore_backup(data, password)
            .map_err(|e| e.to_string())?;

        if let Some(bookmarks) = &backup_data.bookmarks {
            for bm in bookmarks {
                if let (Some(title), Some(url)) = (
                    bm.get("title").and_then(|v| v.as_str()),
                    bm.get("url").and_then(|v| v.as_str()),
                ) {
                    let folder = bm
                        .get("folder_id")
                        .and_then(|v| v.as_str())
                        .map(String::from);
                    self.bookmark_manager.add_bookmark(
                        title.to_string(),
                        url.to_string(),
                        folder,
                        None,
                    );
                }
            }
        }

        if let Some(settings_val) = &backup_data.settings {
            if let Ok(update) =
                serde_json::from_value::<maho_types::settings::SettingsUpdate>(settings_val.clone())
            {
                self.update_settings(update);
            }
        }

        serde_json::to_string(&backup_data.metadata).map_err(|e| e.to_string())
    }

    pub fn validate_backup(data: &[u8]) -> Result<String, String> {
        let metadata = crate::backup_manager::BackupManager::validate_backup(data)
            .map_err(|e| e.to_string())?;
        serde_json::to_string(&crate::backup_manager::BackupManager::to_view_model(
            &metadata,
        ))
        .map_err(|e| e.to_string())
    }

    pub fn get_backup_history_json(&self) -> String {
        let history: Vec<serde_json::Value> = self
            .backup_manager
            .get_backup_history()
            .iter()
            .map(crate::backup_manager::BackupManager::to_view_model)
            .collect();
        serde_json::to_string(&history).unwrap_or_else(|_| "[]".to_string())
    }

    pub fn add_to_reading_list(&mut self, url: &str, title: &str) -> String {
        let item = self.reading_list_manager.add_item(url, title);
        serde_json::to_string(
            &crate::reading_list_manager::ReadingListManager::to_view_model(&item),
        )
        .unwrap_or_else(|_| "{}".to_string())
    }

    pub fn remove_from_reading_list(&mut self, id: &str) -> bool {
        self.reading_list_manager.remove_item(id).is_some()
    }

    pub fn toggle_reading_list_read(&mut self, id: &str) -> Option<bool> {
        self.reading_list_manager.toggle_read(id)
    }

    pub fn get_reading_list_json(&self) -> String {
        serde_json::to_string(&self.reading_list_manager.get_all_view_models())
            .unwrap_or_else(|_| "[]".to_string())
    }

    pub fn get_unread_reading_list_json(&self) -> String {
        let items: Vec<crate::reading_list_manager::ReadingListViewModel> = self
            .reading_list_manager
            .get_unread_items()
            .into_iter()
            .map(crate::reading_list_manager::ReadingListManager::to_view_model)
            .collect();
        serde_json::to_string(&items).unwrap_or_else(|_| "[]".to_string())
    }

    pub fn get_reading_list_count(&self) -> usize {
        self.reading_list_manager.get_item_count()
    }

    pub fn get_unread_count(&self) -> usize {
        self.reading_list_manager.get_unread_count()
    }

    pub fn detect_browser_profiles_json() -> String {
        let profiles = crate::import_export_manager::ImportExportManager::detect_browser_profiles();
        serde_json::to_string(&profiles).unwrap_or_else(|_| "[]".to_string())
    }

    pub fn import_chrome_bookmarks(&mut self, profile_path: &str) -> Result<String, String> {
        let path = std::path::Path::new(profile_path);
        let items =
            crate::import_export_manager::ImportExportManager::import_chrome_bookmarks(path)?;
        for item in &items {
            if !item.url.is_empty()
                && (item.url.starts_with("http://") || item.url.starts_with("https://"))
            {
                self.bookmark_manager.add_bookmark(
                    item.title.clone(),
                    item.url.clone(),
                    item.folder.clone(),
                    None,
                );
            }
        }
        let result = self
            .import_export
            .import_bookmarks(crate::import_export_manager::ImportSource::Chrome, items);
        serde_json::to_string(&result).map_err(|e| e.to_string())
    }

    pub fn import_firefox_bookmarks(&mut self, profile_path: &str) -> Result<String, String> {
        let path = std::path::Path::new(profile_path);
        let items =
            crate::import_export_manager::ImportExportManager::import_firefox_bookmarks(path)?;
        for item in &items {
            if !item.url.is_empty()
                && (item.url.starts_with("http://") || item.url.starts_with("https://"))
            {
                self.bookmark_manager.add_bookmark(
                    item.title.clone(),
                    item.url.clone(),
                    item.folder.clone(),
                    None,
                );
            }
        }
        let result = self
            .import_export
            .import_bookmarks(crate::import_export_manager::ImportSource::Firefox, items);
        serde_json::to_string(&result).map_err(|e| e.to_string())
    }

    pub fn export_history_csv(&self, entries_json: &str) -> Result<String, String> {
        let entries: Vec<crate::import_export_manager::HistoryEntry> =
            serde_json::from_str(entries_json).map_err(|e| e.to_string())?;
        Ok(self.import_export.export_history_csv(&entries))
    }

    pub fn get_all_shortcuts_json(&self) -> String {
        let mut list = Vec::new();
        for b in self.shortcut_manager.get_all_bindings() {
            let default_b = self.shortcut_manager.get_default_binding(&b.action);
            let default_combo = default_b.map(|d| d.key_combo.clone());
            list.push(serde_json::json!({
                "action": b.action,
                "label": b.label,
                "category": b.category,
                "keyCombo": b.key_combo,
                "isCustom": b.is_custom,
                "enabled": b.enabled,
                "updated_at": b.updated_at,
                "defaultKeyCombo": default_combo
            }));
        }
        serde_json::to_string(&list).unwrap_or_else(|_| "[]".to_string())
    }

    pub fn get_shortcuts_by_category_json(&self, category_json: &str) -> String {
        let category: maho_types::keyboard::ShortcutCategory =
            match serde_json::from_str(category_json) {
                Ok(c) => c,
                Err(_) => return "[]".to_string(),
            };
        let bindings: Vec<&maho_types::keyboard::ShortcutBinding> =
            self.shortcut_manager.get_bindings_by_category(&category);
        serde_json::to_string(&bindings).unwrap_or_else(|_| "[]".to_string())
    }

    pub fn set_shortcut_json(
        &mut self,
        action: &str,
        key_combo_json: &str,
    ) -> Result<String, String> {
        let key_combo: maho_types::keyboard::KeyCombo =
            serde_json::from_str(key_combo_json).map_err(|e| e.to_string())?;
        match self.shortcut_manager.set_shortcut(action, key_combo) {
            Ok(()) => Ok(r#"{"success":true}"#.to_string()),
            Err(conflict) => {
                let json = serde_json::to_string(&conflict).unwrap_or_else(|_| "{}".to_string());
                Err(json)
            }
        }
    }

    pub fn reset_shortcut(&mut self, action: &str) {
        self.shortcut_manager.reset_shortcut(action);
    }

    pub fn reset_all_shortcuts(&mut self) {
        self.shortcut_manager.reset_all_shortcuts();
    }

    fn push_shortcut_sync(&mut self, action: &str) {
        let binding_opt = self
            .shortcut_manager
            .get_all_bindings()
            .iter()
            .find(|b| b.action == action)
            .cloned();
        if let Some(binding) = binding_opt {
            let payload = serde_json::json!({
                "action": binding.action,
                "keyCombo": binding.key_combo,
                "enabled": binding.enabled,
            });
            match self.next_hlc_ts() {
                Ok(version) => {
                    let device_id = self.account_manager.device_id_hash();
                    let payload_str = payload.to_string();
                    let fields_hlc_json =
                        crate::sync_models::build_fields_hlc(&payload_str, version, device_id);
                    let sync_entity = crate::sync_models::SyncEntity {
                        entity_type: crate::sync_models::SyncEntityType::Shortcut,
                        entity_id: action.to_string(),
                        version,
                        device_id,
                        schema_version: 1,
                        modified_at: chrono::Utc::now().timestamp(),
                        payload_json: payload_str,
                        deleted: false,
                        queue_row_id: None,
                        fields_hlc_json,
                    };
                    self.sync_push_entity_persisted(sync_entity);
                }
                Err(e) => {
                    eprintln!(
                        "[sync] Skipping shortcut push for '{}': HLC failure: {}",
                        action, e
                    );
                }
            }
        }
    }

    fn push_shortcut_sync_deleted(&mut self, action: &str) {
        let payload = serde_json::json!({
            "action": action,
        });
        match self.next_hlc_ts() {
            Ok(version) => {
                let sync_entity = crate::sync_models::SyncEntity {
                    entity_type: crate::sync_models::SyncEntityType::Shortcut,
                    entity_id: action.to_string(),
                    version,
                    device_id: self.account_manager.device_id_hash(),
                    schema_version: 1,
                    modified_at: chrono::Utc::now().timestamp(),
                    payload_json: payload.to_string(),
                    deleted: true,
                    queue_row_id: None,
                    fields_hlc_json: None,
                };
                self.sync_push_entity_persisted(sync_entity);
            }
            Err(e) => {
                eprintln!(
                    "[sync] Skipping shortcut deletion push for '{}': HLC failure: {}",
                    action, e
                );
            }
        }
    }

    pub fn toggle_shortcut_persisted(&mut self, action: &str, enabled: bool) {
        self.shortcut_manager.toggle_shortcut(action, enabled);
        let updated_at = self
            .shortcut_manager
            .get_all_bindings()
            .iter()
            .find(|b| b.action == action)
            .map(|b| b.updated_at)
            .unwrap_or(0);
        if let Some(ref storage) = self.storage {
            let _ = storage.save_shortcut_enabled(action, enabled, updated_at);
        }
        self.push_shortcut_sync(action);
    }

    pub fn set_shortcut_persisted(
        &mut self,
        action: &str,
        key_combo: maho_types::keyboard::KeyCombo,
    ) -> Result<(), maho_types::keyboard::SetShortcutError> {
        let result = self
            .shortcut_manager
            .set_shortcut(action, key_combo.clone());
        if result.is_ok() {
            let updated_at = self
                .shortcut_manager
                .get_all_bindings()
                .iter()
                .find(|b| b.action == action)
                .map(|b| b.updated_at)
                .unwrap_or(0);
            if let Some(ref storage) = self.storage {
                if let Ok(json) = serde_json::to_string(&key_combo) {
                    let _ = storage.save_shortcut(action, &json, updated_at);
                }
            }
            self.push_shortcut_sync(action);
        }
        result
    }

    pub fn reset_shortcut_persisted(&mut self, action: &str) {
        self.shortcut_manager.reset_shortcut(action);
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_shortcut(action);
        }
        self.push_shortcut_sync_deleted(action);
    }

    pub fn reset_all_shortcuts_persisted(&mut self) {
        let actions: Vec<String> = self
            .shortcut_manager
            .get_all_bindings()
            .iter()
            .map(|b| b.action.clone())
            .collect();
        self.shortcut_manager.reset_all_shortcuts();
        if let Some(ref storage) = self.storage {
            let _ = storage.delete_all_shortcuts();
        }
        for action in actions {
            self.push_shortcut_sync_deleted(&action);
        }
    }

    pub fn resolve_shortcut_json(&self, key_combo_json: &str) -> Option<String> {
        let key_combo: maho_types::keyboard::KeyCombo =
            serde_json::from_str(key_combo_json).ok()?;
        self.shortcut_manager
            .find_binding_by_key_combo(&key_combo)
            .map(|b| b.action.clone())
    }

    pub fn check_shortcut_conflict_json(&self, key_combo_json: &str) -> Option<String> {
        let key_combo: maho_types::keyboard::KeyCombo =
            serde_json::from_str(key_combo_json).ok()?;
        self.shortcut_manager
            .check_conflict("", &key_combo)
            .map(|conflict| conflict.existing_action)
    }

    pub fn export_shortcuts_json(&self) -> String {
        let platform = if cfg!(target_os = "macos") {
            "macos"
        } else if cfg!(target_os = "windows") {
            "windows"
        } else {
            "linux"
        };
        let envelope = serde_json::json!({
            "version": 1,
            "platform": platform,
            "exportedAt": chrono::Utc::now().to_rfc3339(),
            "shortcuts": self.shortcut_manager.get_all_bindings()
        });
        serde_json::to_string(&envelope).unwrap_or_else(|_| "{}".to_string())
    }

    pub fn import_shortcuts_json(&mut self, json_str: &str) -> Result<(), String> {
        let value: serde_json::Value = serde_json::from_str(json_str).map_err(|e| e.to_string())?;
        let bindings: Vec<maho_types::keyboard::ShortcutBinding> = if value.is_array() {
            serde_json::from_value(value).map_err(|e| e.to_string())?
        } else if let Some(shortcuts_val) = value.get("shortcuts") {
            serde_json::from_value(shortcuts_val.clone()).map_err(|e| e.to_string())?
        } else {
            return Err("Invalid shortcuts import format: missing shortcuts array".to_string());
        };

        for binding in bindings {
            if binding.updated_at > 0 {
                let local_opt = self
                    .shortcut_manager
                    .get_all_bindings()
                    .iter()
                    .find(|b| b.action == binding.action)
                    .cloned();
                if let Some(local_b) = local_opt {
                    if binding.updated_at <= local_b.updated_at {
                        continue;
                    }
                }
            }
            if binding.is_custom {
                let _ = self.set_shortcut_persisted(&binding.action, binding.key_combo);
            } else {
                self.reset_shortcut_persisted(&binding.action);
            }
            self.toggle_shortcut_persisted(&binding.action, binding.enabled);
        }
        Ok(())
    }

    pub fn merge_shortcuts_json(&mut self, json_str: &str) -> Result<(), String> {
        let value: serde_json::Value = serde_json::from_str(json_str).map_err(|e| e.to_string())?;
        let bindings: Vec<maho_types::keyboard::ShortcutBinding> = if value.is_array() {
            serde_json::from_value(value).map_err(|e| e.to_string())?
        } else if let Some(shortcuts_val) = value.get("shortcuts") {
            serde_json::from_value(shortcuts_val.clone()).map_err(|e| e.to_string())?
        } else {
            return Err("Invalid shortcuts import format: missing shortcuts array".to_string());
        };

        for incoming_b in bindings {
            let local_opt = self
                .shortcut_manager
                .get_all_bindings()
                .iter()
                .find(|b| b.action == incoming_b.action)
                .cloned();
            if let Some(local_b) = local_opt {
                if incoming_b.updated_at > local_b.updated_at {
                    if incoming_b.is_custom {
                        let _ = self.set_shortcut_persisted(
                            &incoming_b.action,
                            incoming_b.key_combo.clone(),
                        );
                    } else {
                        self.reset_shortcut_persisted(&incoming_b.action);
                    }
                    self.toggle_shortcut_persisted(&incoming_b.action, incoming_b.enabled);
                    if let Some(ref storage) = self.storage {
                        if incoming_b.is_custom {
                            if let Ok(json) = serde_json::to_string(&incoming_b.key_combo) {
                                let _ = storage.save_shortcut(
                                    &incoming_b.action,
                                    &json,
                                    incoming_b.updated_at,
                                );
                            }
                        } else {
                            let _ = storage.save_shortcut_enabled(
                                &incoming_b.action,
                                incoming_b.enabled,
                                incoming_b.updated_at,
                            );
                        }
                    }
                    self.shortcut_manager.restore_shortcut_state(
                        &incoming_b.action,
                        incoming_b.key_combo,
                        incoming_b.enabled,
                        incoming_b.updated_at,
                    );
                }
            }
        }
        Ok(())
    }

    pub fn set_density(&mut self, density_str: &str) {
        let density: maho_types::settings::Density =
            match serde_json::from_str(&format!("\"{}\"", density_str)) {
                Ok(d) => d,
                Err(_) => {
                    self.emit_error("set_density", "INVALID_DENSITY", "Invalid density value");
                    return;
                }
            };
        self.update_settings(SettingsUpdate {
            appearance: Some(AppearanceSettingsUpdate {
                density: Some(density),
                ..Default::default()
            }),
            general: None,
            privacy: None,
            reader: None,
            keyboard_shortcuts: None,
            per_site_settings: None,
            toolbar_items: None,
            max: None,
            autofill: None,
            advanced: None,
            notifications: None,
        });
    }

    pub fn set_custom_chrome_css(&mut self, _css: Option<String>) {}

    pub fn set_window_transparency(&mut self, enabled: bool) {
        self.update_settings(SettingsUpdate {
            appearance: Some(AppearanceSettingsUpdate {
                window_transparency: Some(enabled),
                ..Default::default()
            }),
            general: None,
            privacy: None,
            reader: None,
            keyboard_shortcuts: None,
            per_site_settings: None,
            toolbar_items: None,
            max: None,
            autofill: None,
            advanced: None,
            notifications: None,
        });
    }

    pub fn get_toolbar_items_json(&self) -> String {
        serde_json::to_string(&self.settings_manager.get_settings().toolbar_items)
            .unwrap_or_else(|_| "[]".to_string())
    }

    pub fn set_toolbar_items_json(&mut self, items_json: &str) -> Result<(), String> {
        let items: Vec<maho_types::settings::ToolbarItem> =
            serde_json::from_str(items_json).map_err(|e| e.to_string())?;
        self.update_settings(SettingsUpdate {
            toolbar_items: Some(items),
            general: None,
            appearance: None,
            privacy: None,
            reader: None,
            keyboard_shortcuts: None,
            per_site_settings: None,
            max: None,
            autofill: None,
            advanced: None,
            notifications: None,
        });
        Ok(())
    }

    pub fn set_app_icon(&mut self, path: Option<String>) {
        self.update_settings(SettingsUpdate {
            appearance: Some(AppearanceSettingsUpdate {
                custom_icon_path: Some(path),
                ..Default::default()
            }),
            general: None,
            privacy: None,
            reader: None,
            keyboard_shortcuts: None,
            per_site_settings: None,
            toolbar_items: None,
            max: None,
            autofill: None,
            advanced: None,
            notifications: None,
        });
    }

    pub fn get_default_toolbar_items_json(&self) -> String {
        let defaults = vec![
            maho_types::settings::ToolbarItem {
                id: "back_forward".to_string(),
                kind: maho_types::settings::ToolbarItemKind::BackForward,
                visible: true,
                label: "Back/Forward".to_string(),
            },
            maho_types::settings::ToolbarItem {
                id: "reload".to_string(),
                kind: maho_types::settings::ToolbarItemKind::Reload,
                visible: true,
                label: "Reload".to_string(),
            },
            maho_types::settings::ToolbarItem {
                id: "address_bar".to_string(),
                kind: maho_types::settings::ToolbarItemKind::AddressBar,
                visible: true,
                label: "Address Bar".to_string(),
            },
            maho_types::settings::ToolbarItem {
                id: "share".to_string(),
                kind: maho_types::settings::ToolbarItemKind::Share,
                visible: true,
                label: "Share".to_string(),
            },
            maho_types::settings::ToolbarItem {
                id: "downloads".to_string(),
                kind: maho_types::settings::ToolbarItemKind::Downloads,
                visible: true,
                label: "Downloads".to_string(),
            },
            maho_types::settings::ToolbarItem {
                id: "extensions".to_string(),
                kind: maho_types::settings::ToolbarItemKind::Extensions,
                visible: true,
                label: "Extensions".to_string(),
            },
        ];
        serde_json::to_string(&defaults).unwrap_or_else(|_| "[]".to_string())
    }

    pub fn handle_memory_pressure(&mut self, level: MemoryPressureLevel) -> String {
        match level {
            MemoryPressureLevel::Normal => serde_json::json!({
                "level": "normal",
                "actions": [],
                "suspend_tab_ids": [],
                "count": 0
            })
            .to_string(),
            MemoryPressureLevel::Warning => {
                let all_tabs = self.tab_manager.get_all_tabs();
                let suspend_ids: Vec<String> = all_tabs
                    .iter()
                    .filter(|tab| {
                        let state = tab.state.kind_str();
                        state == "active" && !tab.role.is_pinned()
                    })
                    .map(|tab| tab.id.0.clone())
                    .collect();
                let count = suspend_ids.len();
                serde_json::json!({
                    "level": "warning",
                    "actions": ["suspend_idle_tabs"],
                    "suspend_tab_ids": suspend_ids,
                    "count": count
                })
                .to_string()
            }
            MemoryPressureLevel::Critical => {
                let all_tabs = self.tab_manager.get_all_tabs();
                let suspend_ids: Vec<String> = all_tabs
                    .iter()
                    .filter(|tab| {
                        let state = tab.state.kind_str();
                        state != "active" || !tab.role.is_pinned()
                    })
                    .map(|tab| tab.id.0.clone())
                    .collect();
                let count = suspend_ids.len();
                serde_json::json!({
                    "level": "critical",
                    "actions": ["suspend_non_active", "cleared_previews"],
                    "suspend_tab_ids": suspend_ids,
                    "count": count
                })
                .to_string()
            }
            MemoryPressureLevel::Extreme => {
                let all_tabs = self.tab_manager.get_all_tabs();
                let suspend_ids: Vec<String> = all_tabs
                    .iter()
                    .filter(|tab| {
                        let state = tab.state.kind_str();
                        state != "active" || !tab.role.is_pinned()
                    })
                    .map(|tab| tab.id.0.clone())
                    .collect();
                let count = suspend_ids.len();

                // Clear all history for extreme pressure
                self.clear_history();

                serde_json::json!({
                    "level": "extreme",
                    "actions": ["suspend_non_active", "cleared_previews", "purged_old_history"],
                    "suspend_tab_ids": suspend_ids,
                    "count": count
                })
                .to_string()
            }
        }
    }

    // === Sprint 11: System Integration ===

    /// Handle maho:// URL scheme - returns JSON response
    pub fn handle_url_scheme(&mut self, url: &str) -> String {
        let url = url.trim();

        let path = if let Some(stripped) = url.strip_prefix("maho://") {
            stripped
        } else {
            return serde_json::json!({"error": "invalid scheme"}).to_string();
        };

        let (command, query) = if let Some(idx) = path.find('?') {
            (&path[..idx], Some(&path[idx + 1..]))
        } else {
            (path, None)
        };

        fn parse_query_param<'a>(query: Option<&'a str>, key: &str) -> Option<&'a str> {
            query.and_then(|q| {
                q.split('&').find_map(|pair| {
                    let (k, v) = pair.split_once('=')?;
                    if k == key {
                        Some(v)
                    } else {
                        None
                    }
                })
            })
        }

        match command {
            "newtab" => {
                let url_param = parse_query_param(query, "url");
                match url_param {
                    None => serde_json::json!({"error": "missing url parameter"}).to_string(),
                    Some(raw_url) => {
                        let space_id = self.get_active_space_id().clone();
                        let url_value = Url(urlencoding_decode(raw_url));
                        self.handle_event(ShellEvent::CreateTab {
                            space_id,
                            url: Some(url_value),
                            parent_id: None,
                            tab_id: None,
                            window_id: None,
                            is_private: false,
                        });
                        serde_json::json!({"action": "newtab", "ok": true}).to_string()
                    }
                }
            }
            "settings" => serde_json::json!({"action": "settings", "ok": true}).to_string(),
            "space" => {
                if let Some(name) = parse_query_param(query, "name") {
                    let decoded = urlencoding_decode(name);
                    let color = SpaceColor {
                        hue: 220.0,
                        saturation: 0.8,
                        brightness: 0.9,
                        grain: 0.0,
                    };
                    let profile_id = self
                        .profile_manager
                        .get_active_profile_id()
                        .cloned()
                        .unwrap_or_else(|| maho_types::identifiers::ProfileId::new(""));
                    self.handle_event(ShellEvent::CreateSpace {
                        name: decoded,
                        color,
                        profile_id,
                    });
                    serde_json::json!({"action": "create_space", "ok": true}).to_string()
                } else {
                    serde_json::json!({"action": "space", "error": "missing name param"})
                        .to_string()
                }
            }
            "open" => {
                if let Some(url_param) = parse_query_param(query, "url") {
                    let decoded = urlencoding_decode(url_param);
                    let space_id = self.get_active_space_id().clone();
                    self.handle_event(ShellEvent::CreateTab {
                        space_id,
                        url: Some(Url(decoded)),
                        parent_id: None,
                        tab_id: None,
                        window_id: None,
                        is_private: false,
                    });
                    serde_json::json!({"action": "open", "ok": true}).to_string()
                } else {
                    serde_json::json!({"action": "open", "error": "missing url param"}).to_string()
                }
            }
            _ => serde_json::json!({"error": "unknown command", "command": command}).to_string(),
        }
    }

    /// Get searchable items for Spotlight indexing (tabs + bookmarks) as JSON
    pub fn get_searchable_items(&self) -> String {
        let mut items: Vec<serde_json::Value> = Vec::new();

        for tab in self.get_tab_view_models() {
            items.push(serde_json::json!({
                "kind": "tab",
                "id": format!("tab-{}", tab.id),
                "title": tab.title,
                "url": tab.url,
                "description": format!("Open tab: {}", tab.title),
            }));
        }

        for bookmark in self.get_all_bookmark_entries() {
            items.push(serde_json::json!({
                "kind": "bookmark",
                "id": format!("bookmark-{}", bookmark.id.0),
                "title": bookmark.title,
                "url": bookmark.url,
                "description": format!("Bookmark: {}", bookmark.title),
            }));
        }

        serde_json::to_string(&items).unwrap_or_else(|_| "[]".to_string())
    }

    pub fn push_tab_sync(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tab_manager.get_tab(tab_id) {
            if tab.is_private {
                return;
            }
            if let Ok(payload) = serde_json::to_string(tab) {
                self.push_sync_entity(SyncEntityType::Tab, tab.id.to_string(), payload, false);
            }
        }
    }

    pub fn push_space_sync(&mut self, space_id: &SpaceId) {
        if let Some(space) = self.space_manager.get_space(space_id) {
            let private_ids = private_tab_ids_from_tabs(self.tab_manager.get_all_tabs());
            let sanitized = sanitize_space_for_private_tabs(space, &private_ids);
            if let Ok(payload) = serde_json::to_string(&sanitized) {
                self.push_sync_entity(SyncEntityType::Space, space.id.to_string(), payload, false);
            }
        }
    }

    // === Sync Methods ===

    pub fn start_sync(&mut self, server_url: &str, room_id: &str) {
        use crate::sync_models::SyncConfig;
        let mut disabled = std::collections::HashSet::new();
        disabled.insert(SyncEntityType::AutofillAddress);
        disabled.insert(SyncEntityType::AutofillPayment);
        disabled.insert(SyncEntityType::Conversation);
        disabled.insert(SyncEntityType::ConversationTurn);
        disabled.insert(SyncEntityType::Settings);

        let config = SyncConfig {
            server_url: server_url.to_string(),
            sync_key: Some(room_id.to_string()),
            device_id: self.sync_device_id(),
            device_name: std::env::var("HOSTNAME").unwrap_or_else(|_| "Maho Device".to_string()),
            auto_sync: true,
            sync_interval_secs: 30,
            disabled_entity_types: disabled,
        };
        let mut sm = crate::sync_manager::SyncManager::new(config);
        if let Some(ref s) = self.storage {
            for entity_type in &[
                crate::sync_models::SyncEntityType::Space,
                crate::sync_models::SyncEntityType::Tab,
                crate::sync_models::SyncEntityType::Bookmark,
                crate::sync_models::SyncEntityType::BookmarkFolder,
                crate::sync_models::SyncEntityType::Note,
                crate::sync_models::SyncEntityType::Boost,
                crate::sync_models::SyncEntityType::ReadingListItem,
                crate::sync_models::SyncEntityType::SearchEngine,
                crate::sync_models::SyncEntityType::Shortcut,
                crate::sync_models::SyncEntityType::Memory,
                crate::sync_models::SyncEntityType::Extension,
                crate::sync_models::SyncEntityType::CssMod,
                crate::sync_models::SyncEntityType::Easel,
                crate::sync_models::SyncEntityType::Conversation,
                crate::sync_models::SyncEntityType::ConversationTurn,
                crate::sync_models::SyncEntityType::AutofillAddress,
                crate::sync_models::SyncEntityType::AutofillPayment,
                crate::sync_models::SyncEntityType::Settings,
                crate::sync_models::SyncEntityType::SharedCollection,
            ] {
                let entity_type_str = entity_type.as_str();
                if let Ok(version) = s.get_sync_last_pulled_version(entity_type_str) {
                    sm.set_last_pulled_version(entity_type.clone(), version);
                }
            }
        }
        self.sync_manager = Some(sm);
        if let Err(e) = self.load_sync_queue() {
            eprintln!("Failed to load sync queue: {}", e);
        }
    }

    pub fn stop_sync(&mut self) {
        self.trigger_async_snapshot_upload();
        self.sync_manager = None;
    }

    pub fn drain_received_tabs(&mut self) -> Vec<crate::sync_models::ReceivedTab> {
        match &mut self.sync_manager {
            Some(sm) => sm.drain_received_tabs(),
            None => Vec::new(),
        }
    }

    pub fn set_sync_status(&mut self, status: crate::sync_models::SyncStatus) {
        if let Some(ref mut sm) = self.sync_manager {
            let becoming_active = matches!(
                status,
                crate::sync_models::SyncStatus::Synced | crate::sync_models::SyncStatus::Syncing
            );
            sm.set_status(status);
            if becoming_active {
                sm.flush_offline_queue();
            }
        }
    }

    pub fn sync_device_id(&self) -> String {
        self.account_manager.device_id().to_string()
    }

    pub fn next_hlc_ts(&self) -> Result<u64, String> {
        let now_ms = chrono::Utc::now().timestamp_millis() as u64;
        let last_hlc = self.last_hlc_ts.get();
        let new_hlc = std::cmp::max(
            now_ms,
            last_hlc
                .checked_add(1)
                .ok_or("HLC logical clock overflow")?,
        );
        if let Some(ref s) = self.storage {
            s.set_last_hlc_ts(new_hlc).map_err(|e| e.to_string())?;
        }
        self.last_hlc_ts.set(new_hlc);
        Ok(new_hlc)
    }

    pub fn update_hlc_ts_on_receive(&self, received_hlc: u64) -> Result<(), String> {
        let now_ms = chrono::Utc::now().timestamp_millis() as u64;
        let now_ms = std::cmp::max(now_ms, 1577836800000); // 2020-01-01 floor
        let limit = now_ms.checked_add(3600000).ok_or("HLC limit overflow")?;
        if received_hlc > limit {
            return Err(format!(
                "Received HLC timestamp {} exceeds limit {}",
                received_hlc, limit
            ));
        }
        let local_hlc = self.last_hlc_ts.get();
        let new_hlc = std::cmp::max(std::cmp::max(local_hlc, received_hlc), now_ms)
            .checked_add(1)
            .ok_or("HLC logical clock overflow")?;
        if let Some(ref s) = self.storage {
            s.set_last_hlc_ts(new_hlc).map_err(|e| e.to_string())?;
        }
        self.last_hlc_ts.set(new_hlc);
        Ok(())
    }

    pub fn set_account_auth_error(&mut self, message: String) -> Option<AccountInfo> {
        let res = self.account_manager.mark_auth_error(message);
        self.persist_lmdb_now_partial(LMDB_DIRTY_ACCOUNT);
        res
    }

    pub fn refresh_account_token(&mut self) -> Option<AccountInfo> {
        let res = self.account_manager.refresh_token();
        self.persist_lmdb_now_partial(LMDB_DIRTY_ACCOUNT);
        res
    }

    pub fn set_account_auth_state(&mut self, auth_state: AuthState) -> Option<AccountInfo> {
        let res = self.account_manager.set_auth_state(auth_state);
        self.persist_lmdb_now_partial(LMDB_DIRTY_ACCOUNT);
        res
    }

    pub fn get_account_state_json(&self) -> String {
        serde_json::to_string(&self.account_manager.get_account())
            .unwrap_or_else(|_| "null".to_string())
    }

    pub fn get_auth_state_json(&self) -> String {
        serde_json::to_string(&self.account_manager.get_auth_state())
            .unwrap_or_else(|_| "null".to_string())
    }

    pub fn get_account_tier(&self) -> Option<maho_types::account::UserTier> {
        self.account_manager.get_tier()
    }

    pub fn get_sync_status(&self) -> String {
        match &self.sync_manager {
            Some(sm) => {
                serde_json::to_string(sm.status()).unwrap_or_else(|_| "\"idle\"".to_string())
            }
            None => "\"idle\"".to_string(),
        }
    }

    pub fn get_sync_server_url(&self) -> Option<String> {
        self.sync_manager
            .as_ref()
            .map(|sm| sm.config().server_url.clone())
    }

    pub fn get_sync_key(&self) -> Option<String> {
        self.sync_manager
            .as_ref()
            .and_then(|sm| sm.config().sync_key.clone())
    }

    pub fn handle_incoming_websocket_message(&mut self, message_json: &str) -> Result<(), String> {
        use crate::sync_models::SyncMessage;
        let msg: SyncMessage = serde_json::from_str(message_json)
            .map_err(|e| format!("Failed to parse SyncMessage: {}", e))?;

        let decrypted_msg = if let SyncMessage::Encrypted { data } = msg {
            if let Some(ref sm) = self.sync_manager {
                if let Some(ref key) = sm.encryption_key {
                    let decrypted = crate::sync_crypto::decrypt_update(&data, key)
                        .map_err(|e| format!("Decryption failed: {}", e))?;
                    serde_json::from_slice::<SyncMessage>(&decrypted)
                        .map_err(|e| format!("Failed to parse decrypted message: {}", e))?
                } else {
                    return Err("No encryption key configured".to_string());
                }
            } else {
                return Err("SyncManager not active".to_string());
            }
        } else {
            msg
        };

        match decrypted_msg {
            SyncMessage::EntityPush { entity } => {
                self.apply_sync_remote_entities(vec![entity]);
            }
            SyncMessage::EntityBatch { entities } => {
                self.apply_sync_remote_entities(entities);
            }
            other => {
                if let Some(ref mut sm) = self.sync_manager {
                    sm.handle_incoming_message(other);
                }
            }
        }
        Ok(())
    }

    pub fn get_sync_auth_token(&self) -> String {
        let token = if let Some(ref storage) = self.storage {
            storage
                .get_setting("auth:access_token")
                .ok()
                .flatten()
                .filter(|s| !s.is_empty())
        } else {
            None
        };
        token.unwrap_or_else(|| self.get_sync_key().unwrap_or_default())
    }

    pub fn get_connected_devices(&self) -> String {
        match &self.sync_manager {
            Some(sm) => {
                let devices: Vec<_> = sm.get_devices().into_iter().cloned().collect();
                serde_json::to_string(&devices).unwrap_or_else(|_| "[]".to_string())
            }
            None => "[]".to_string(),
        }
    }

    pub fn send_tab_to_device(&mut self, url: &str, title: &str, target_device_id: &str) {
        if let Some(ref mut sm) = self.sync_manager {
            sm.send_tab(url, title, target_device_id);
        }
    }

    pub fn generate_sync_key(&self) -> String {
        use crate::sync_crypto;
        let seed = sync_crypto::generate_sync_seed();
        let hex_key = seed
            .iter()
            .map(|b| format!("{:02x}", b))
            .collect::<String>();
        let room_id = sync_crypto::derive_room_id(&seed)
            .unwrap_or_else(|_| "error deriving room id".to_string());
        let recovery_phrase = sync_crypto::encode_recovery_phrase(&seed)
            .unwrap_or_else(|_| "error generating phrase".to_string());
        serde_json::json!({
            "syncKey": hex_key,
            "roomId": room_id,
            "recoveryPhrase": recovery_phrase
        })
        .to_string()
    }

    pub fn join_sync(&mut self, server_url: &str, recovery_phrase: &str) -> String {
        use crate::sync_crypto;
        // Decode recovery phrase to seed
        let seed = match sync_crypto::decode_recovery_phrase(recovery_phrase) {
            Ok(s) => s,
            Err(e) => {
                return serde_json::json!({
                    "success": false,
                    "error": format!("Invalid recovery phrase: {}", e)
                })
                .to_string();
            }
        };
        // Derive encryption key from seed
        let encryption_key = match sync_crypto::derive_encryption_key(&seed) {
            Ok(k) => k,
            Err(e) => {
                return serde_json::json!({
                    "success": false,
                    "error": format!("Key derivation failed: {}", e)
                })
                .to_string();
            }
        };
        let room_id = match sync_crypto::derive_room_id(&seed) {
            Ok(room_id) => room_id,
            Err(e) => {
                return serde_json::json!({
                    "success": false,
                    "error": format!("Room ID derivation failed: {}", e)
                })
                .to_string();
            }
        };
        self.start_sync(server_url, &room_id);
        if let Some(ref mut sm) = self.sync_manager {
            sm.set_encryption_key(encryption_key);
        }
        let _ = self.bootstrap_sync_join_flow(&room_id);
        serde_json::json!({
            "success": true,
            "deviceId": self.sync_manager.as_ref().map(|sm| sm.config().device_id.clone()).unwrap_or_default()
        }).to_string()
    }

    pub fn configure_sync_encryption_for_recovery_phrase(
        &mut self,
        server_url: &str,
        recovery_phrase: &str,
    ) -> Result<(), String> {
        use crate::sync_crypto;

        let seed = sync_crypto::decode_recovery_phrase(recovery_phrase)
            .map_err(|e| format!("Invalid recovery phrase: {}", e))?;
        let encryption_key = sync_crypto::derive_encryption_key(&seed)
            .map_err(|e| format!("Key derivation failed: {}", e))?;
        let room_id = sync_crypto::derive_room_id(&seed)
            .map_err(|e| format!("Room ID derivation failed: {}", e))?;
        self.start_sync(server_url, &room_id);
        if let Some(ref mut sm) = self.sync_manager {
            sm.set_encryption_key(encryption_key);
        }
        Ok(())
    }

    pub fn remove_sync_device(&mut self, device_id: &str) {
        if let Some(ref mut sm) = self.sync_manager {
            sm.remove_device(device_id);
        }
    }

    pub fn disconnect_sync_device(&mut self, device_id: &str) -> bool {
        if let Some(ref mut sm) = self.sync_manager {
            sm.disconnect_device(device_id)
        } else {
            false
        }
    }

    pub fn rename_sync_device(&mut self, device_id: &str, new_name: String) -> bool {
        if let Some(ref mut sm) = self.sync_manager {
            sm.rename_device(device_id, new_name)
        } else {
            false
        }
    }

    pub fn ack_sync_entity(&mut self, entity_type_str: &str, entity_id: &str, version: u64) {
        let type_lower = entity_type_str.to_lowercase();
        let device_id = self.account_manager.device_id_hash();
        if let Some(ref mut sm) = self.sync_manager {
            let key = format!("{}:{}", type_lower, entity_id);
            sm.set_entity_version_in_cache(&key, version, device_id, None, None);
        }
        if let Some(ref storage) = self.storage {
            if let Err(e) = storage.ack_sync_entity_by_key(&type_lower, entity_id, version) {
                eprintln!(
                    "[sync] Failed to ack sync entity {}:{} v{}: {}",
                    type_lower, entity_id, version, e
                );
            }
        }
    }

    fn create_sync_entity(
        &self,
        entity_type: SyncEntityType,
        entity_id: String,
        payload_json: String,
        modified_at: i64,
    ) -> crate::sync_models::SyncEntity {
        let type_str = entity_type.as_str();

        let (version, device_id, fields_hlc_json) = if let Some(ref s) = self.storage {
            match s.get_entity_version(type_str, &entity_id) {
                Ok(Some((hlc_ts, dev_id, fields_hlc, _payload))) => (hlc_ts, dev_id, fields_hlc),
                _ => {
                    let dev_id = self.account_manager.device_id_hash();
                    let ver = (modified_at * 1000) as u64;
                    let f_hlc = crate::sync_models::build_fields_hlc(&payload_json, ver, dev_id);
                    (ver, dev_id, f_hlc)
                }
            }
        } else {
            let dev_id = self.account_manager.device_id_hash();
            let ver = (modified_at * 1000) as u64;
            let f_hlc = crate::sync_models::build_fields_hlc(&payload_json, ver, dev_id);
            (ver, dev_id, f_hlc)
        };

        crate::sync_models::SyncEntity {
            entity_type,
            entity_id,
            version,
            device_id,
            schema_version: 1,
            modified_at,
            payload_json,
            deleted: false,
            queue_row_id: None,
            fields_hlc_json,
        }
    }

    pub fn build_snapshot(&self) -> Result<Vec<u8>, String> {
        let mut snap = crate::snapshot::Snapshot {
            schema_version: 1,
            hlc_ts_ceiling: 0,
            created_at: chrono::Utc::now().to_rfc3339(),
            creator_device_id: self.account_manager.device_id_hash(),
            entities: std::collections::HashMap::new(),
        };

        let parse_dt = |dt_str: &str| -> i64 {
            chrono::DateTime::parse_from_rfc3339(dt_str)
                .map(|d| d.timestamp())
                .unwrap_or_else(|_| dt_str.parse::<i64>().unwrap_or(0))
        };

        let is_disabled = |et: SyncEntityType| -> bool {
            if let Some(ref sm) = self.sync_manager {
                sm.config().disabled_entity_types.contains(&et)
            } else {
                false
            }
        };

        let mut hlc_ts_ceiling = 0u64;
        let mut entities = std::collections::HashMap::new();
        let private_ids = private_tab_ids_from_tabs(self.tab_manager.get_all_tabs());

        // Spaces
        let mut spaces = Vec::new();
        if !is_disabled(SyncEntityType::Space) {
            for space in
                sanitize_spaces_for_private_tabs(self.space_manager.get_all_spaces(), &private_ids)
            {
                let payload = serde_json::to_string(&space).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::Space,
                    space.id.to_string(),
                    payload,
                    chrono::Utc::now().timestamp(),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                spaces.push(sync_ent);
            }
        }
        entities.insert("Space".to_string(), spaces);

        // Tabs
        let mut tabs = Vec::new();
        if !is_disabled(SyncEntityType::Tab) {
            for tab in self.tab_manager.get_all_tabs() {
                if tab.is_private {
                    continue;
                }
                let payload = serde_json::to_string(tab).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::Tab,
                    tab.id.to_string(),
                    payload,
                    parse_dt(&tab.created_at.0),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                tabs.push(sync_ent);
            }
        }
        entities.insert("Tab".to_string(), tabs);

        // Bookmarks
        let mut bookmarks = Vec::new();
        if !is_disabled(SyncEntityType::Bookmark) {
            for bm in self.bookmark_manager.get_all_bookmarks() {
                let payload = serde_json::to_string(bm).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::Bookmark,
                    bm.id.0.clone(),
                    payload,
                    parse_dt(&bm.created_at.0),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                bookmarks.push(sync_ent);
            }
        }
        entities.insert("Bookmark".to_string(), bookmarks);

        // BookmarkFolders
        let mut bookmark_folders = Vec::new();
        if !is_disabled(SyncEntityType::BookmarkFolder) {
            for folder in self.bookmark_manager.get_folders() {
                let payload = serde_json::to_string(folder).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::BookmarkFolder,
                    folder.id.to_string(),
                    payload,
                    parse_dt(&folder.created_at.0),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                bookmark_folders.push(sync_ent);
            }
        }
        entities.insert("BookmarkFolder".to_string(), bookmark_folders);

        // Notes
        let mut notes = Vec::new();
        if !is_disabled(SyncEntityType::Note) {
            for note in self.note_manager.get_all_notes() {
                let payload = serde_json::to_string(note).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::Note,
                    note.id.to_string(),
                    payload,
                    parse_dt(&note.created_at.0),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                notes.push(sync_ent);
            }
        }
        entities.insert("Note".to_string(), notes);

        // Boosts
        let mut boosts = Vec::new();
        if !is_disabled(SyncEntityType::Boost) {
            for boost in self.boost_manager.get_all_boosts() {
                let payload = serde_json::to_string(boost).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::Boost,
                    boost.id.to_string(),
                    payload,
                    parse_dt(&boost.created_at.0),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                boosts.push(sync_ent);
            }
        }
        entities.insert("Boost".to_string(), boosts);

        // ReadingListItems
        let mut reading_list = Vec::new();
        if !is_disabled(SyncEntityType::ReadingListItem) {
            for item in self.reading_list_manager.get_all_items() {
                let payload = serde_json::to_string(item).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::ReadingListItem,
                    item.id.to_string(),
                    payload,
                    parse_dt(&item.added_at.0),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                reading_list.push(sync_ent);
            }
        }
        entities.insert("ReadingListItem".to_string(), reading_list);

        // SearchEngines
        let mut search_engines = Vec::new();
        if !is_disabled(SyncEntityType::SearchEngine) {
            if let Some(ref storage) = self.storage {
                if let Ok(engines_raw) = storage.load_search_engines() {
                    for (id, name, url_template, shortcut, icon_url, is_default) in engines_raw {
                        let se = maho_types::search_engine::SearchEngine {
                            id: id.clone(),
                            name,
                            url_template,
                            shortcut,
                            icon_url,
                            is_default,
                        };
                        let payload = serde_json::to_string(&se).map_err(|e| e.to_string())?;
                        let sync_ent = self.create_sync_entity(
                            SyncEntityType::SearchEngine,
                            id,
                            payload,
                            chrono::Utc::now().timestamp(),
                        );
                        hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                        search_engines.push(sync_ent);
                    }
                }
            }
        }
        entities.insert("SearchEngine".to_string(), search_engines);

        // Shortcuts
        let mut shortcuts = Vec::new();
        if !is_disabled(SyncEntityType::Shortcut) {
            if let Some(ref storage) = self.storage {
                if let Ok(shortcuts_raw) = storage.load_shortcuts() {
                    for (action, key_combo_json, enabled, _version) in shortcuts_raw {
                        let payload = serde_json::json!({
                            "action": action,
                            "keyCombo": serde_json::from_str::<serde_json::Value>(&key_combo_json).unwrap_or(serde_json::Value::Null),
                            "enabled": enabled
                        }).to_string();
                        let sync_ent = self.create_sync_entity(
                            SyncEntityType::Shortcut,
                            action,
                            payload,
                            chrono::Utc::now().timestamp(),
                        );
                        hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                        shortcuts.push(sync_ent);
                    }
                }
            }
        }
        entities.insert("Shortcut".to_string(), shortcuts);

        // Memories
        let mut memories = Vec::new();
        if !is_disabled(SyncEntityType::Memory) {
            if let Some(ref storage) = self.storage {
                if let Ok(memories_raw) = storage.load_all_memories() {
                    for (id, fact, source, session_id, categories, importance, metadata) in
                        memories_raw
                    {
                        let payload = serde_json::json!({
                            "id": id,
                            "fact": fact,
                            "source": source,
                            "session_id": session_id,
                            "categories": categories,
                            "importance": importance,
                            "metadata": metadata
                        })
                        .to_string();
                        let sync_ent = self.create_sync_entity(
                            SyncEntityType::Memory,
                            id,
                            payload,
                            chrono::Utc::now().timestamp(),
                        );
                        hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                        memories.push(sync_ent);
                    }
                }
            }
        }
        entities.insert("Memory".to_string(), memories);

        // Extensions
        let mut extensions = Vec::new();
        if !is_disabled(SyncEntityType::Extension) {
            for ext in self.get_installed_extensions() {
                let payload = serde_json::to_string(&ext).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::Extension,
                    ext.id.clone(),
                    payload,
                    chrono::Utc::now().timestamp(),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                extensions.push(sync_ent);
            }
        }
        entities.insert("Extension".to_string(), extensions);

        // CssMods
        let mut css_mods = Vec::new();
        if !is_disabled(SyncEntityType::CssMod) {
            for css_mod in self.css_mod_manager.get_all_mods() {
                let payload = serde_json::to_string(css_mod).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::CssMod,
                    css_mod.id.clone(),
                    payload,
                    css_mod.created_at.parse::<i64>().unwrap_or(0),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                css_mods.push(sync_ent);
            }
        }
        entities.insert("CssMod".to_string(), css_mods);

        // Easels
        let mut easels = Vec::new();
        if !is_disabled(SyncEntityType::Easel) {
            for easel in self.easel_manager.get_all_easels() {
                let payload = serde_json::to_string(easel).map_err(|e| e.to_string())?;
                let sync_ent = self.create_sync_entity(
                    SyncEntityType::Easel,
                    easel.id.to_string(),
                    payload,
                    chrono::Utc::now().timestamp(),
                );
                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                easels.push(sync_ent);
            }
        }
        entities.insert("Easel".to_string(), easels);

        // Conversations & ConversationTurns
        let mut conversations = Vec::new();
        let mut conversation_turns = Vec::new();
        if !is_disabled(SyncEntityType::Conversation) {
            if let Some(ref storage) = self.storage {
                if let Ok(convs) = storage.list_conversations(999999) {
                    for conv in convs {
                        let payload = serde_json::to_string(&conv).map_err(|e| e.to_string())?;
                        let sync_ent = self.create_sync_entity(
                            SyncEntityType::Conversation,
                            conv.id.clone(),
                            payload,
                            parse_dt(&conv.created_at),
                        );
                        hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                        conversations.push(sync_ent);

                        if let Ok(turns) = storage.get_conversation_messages(&conv.id) {
                            for turn in turns {
                                let payload =
                                    serde_json::to_string(&turn).map_err(|e| e.to_string())?;
                                let sync_ent = self.create_sync_entity(
                                    SyncEntityType::ConversationTurn,
                                    turn.id.clone(),
                                    payload,
                                    parse_dt(&turn.created_at),
                                );
                                hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                                conversation_turns.push(sync_ent);
                            }
                        }
                    }
                }
            }
        }
        entities.insert("Conversation".to_string(), conversations);
        entities.insert("ConversationTurn".to_string(), conversation_turns);

        // AutofillAddress
        let mut autofill_addresses = Vec::new();
        if !is_disabled(SyncEntityType::AutofillAddress) {
            if let Some(ref storage) = self.storage {
                if let Ok(addresses_raw) = storage.load_autofill_addresses() {
                    for (
                        id,
                        name,
                        street,
                        city,
                        state,
                        zip,
                        country,
                        phone,
                        email,
                        address_line2,
                    ) in addresses_raw
                    {
                        let addr = maho_types::autofill::AutofillAddress {
                            id: id.clone(),
                            name,
                            street,
                            city,
                            state,
                            zip,
                            country,
                            phone,
                            email,
                            address_line2,
                        };
                        let payload = serde_json::to_string(&addr).map_err(|e| e.to_string())?;
                        let sync_ent = self.create_sync_entity(
                            SyncEntityType::AutofillAddress,
                            id,
                            payload,
                            chrono::Utc::now().timestamp(),
                        );
                        hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                        autofill_addresses.push(sync_ent);
                    }
                }
            }
        }
        entities.insert("AutofillAddress".to_string(), autofill_addresses);

        // AutofillPayment
        let mut autofill_payments = Vec::new();
        if !is_disabled(SyncEntityType::AutofillPayment) {
            if let Some(ref storage) = self.storage {
                if let Ok(payments_raw) = storage.load_autofill_payments() {
                    for (id, card_name, last_four, expiry, card_network) in payments_raw {
                        let payment = maho_types::autofill::AutofillPayment {
                            id: id.clone(),
                            card_name,
                            last_four,
                            expiry,
                            card_network,
                        };
                        let payload = serde_json::to_string(&payment).map_err(|e| e.to_string())?;
                        let sync_ent = self.create_sync_entity(
                            SyncEntityType::AutofillPayment,
                            id,
                            payload,
                            chrono::Utc::now().timestamp(),
                        );
                        hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                        autofill_payments.push(sync_ent);
                    }
                }
            }
        }
        entities.insert("AutofillPayment".to_string(), autofill_payments);

        // Settings
        let mut settings_list = Vec::new();
        if !is_disabled(SyncEntityType::Settings) {
            let settings = self.settings_manager.get_settings();
            let payload = serde_json::to_string(settings).map_err(|e| e.to_string())?;
            let sync_ent = self.create_sync_entity(
                SyncEntityType::Settings,
                "global_settings".to_string(),
                payload,
                chrono::Utc::now().timestamp(),
            );
            hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
            settings_list.push(sync_ent);
        }
        entities.insert("Settings".to_string(), settings_list);

        // SharedCollection
        let mut shared_collections = Vec::new();
        if !is_disabled(SyncEntityType::SharedCollection) {
            if let Some(ref storage) = self.storage {
                if let Ok(collections_raw) = storage.get_all_shared_collections() {
                    for col in collections_raw {
                        let payload = serde_json::to_string(&col).map_err(|e| e.to_string())?;
                        let sync_ent = self.create_sync_entity(
                            SyncEntityType::SharedCollection,
                            col.id.clone(),
                            payload,
                            chrono::Utc::now().timestamp(),
                        );
                        hlc_ts_ceiling = hlc_ts_ceiling.max(sync_ent.version);
                        shared_collections.push(sync_ent);
                    }
                }
            }
        }
        entities.insert("SharedCollection".to_string(), shared_collections);

        snap.hlc_ts_ceiling = hlc_ts_ceiling;
        snap.entities = entities;

        let snap_json = serde_json::to_vec(&snap).map_err(|e| e.to_string())?;

        let sync_key_opt = self
            .sync_manager
            .as_ref()
            .and_then(|sm| sm.encryption_key.as_ref());
        match sync_key_opt {
            Some(key) => {
                let encrypted = crate::sync_crypto::encrypt_update(&snap_json, key)
                    .map_err(|e| e.to_string())?;
                Ok(encrypted)
            }
            None => Err("Encryption key is not set".to_string()),
        }
    }

    pub fn apply_snapshot(&mut self, encrypted_blob: &[u8]) -> Result<(), String> {
        let sync_key_opt = self
            .sync_manager
            .as_ref()
            .and_then(|sm| sm.encryption_key.as_ref());
        let decrypted_bytes = match sync_key_opt {
            Some(key) => crate::sync_crypto::decrypt_update(encrypted_blob, key)
                .map_err(|e| e.to_string())?,
            None => return Err("Encryption key is not set".to_string()),
        };

        let snap: crate::snapshot::Snapshot =
            serde_json::from_slice(&decrypted_bytes).map_err(|e| e.to_string())?;
        let mut ordered_entities = Vec::new();

        let type_keys = vec![
            "Space",
            "Tab",
            "BookmarkFolder",
            "Bookmark",
            "Note",
            "Boost",
            "ReadingListItem",
            "SearchEngine",
            "Shortcut",
            "Extension",
            "Memory",
            "CssMod",
            "Easel",
            "Conversation",
            "ConversationTurn",
            "AutofillAddress",
            "AutofillPayment",
            "Settings",
            "SharedCollection",
        ];

        for key in type_keys {
            if let Some(list) = snap.entities.get(key) {
                ordered_entities.extend(list.clone());
            }
        }

        let applied = self.apply_sync_remote_entities(ordered_entities);
        println!("[sync] Applied {} entities from snapshot", applied.len());

        if let Some(ref mut sm) = self.sync_manager {
            let ceiling = snap.hlc_ts_ceiling;
            for t in &[
                SyncEntityType::Space,
                SyncEntityType::Tab,
                SyncEntityType::Bookmark,
                SyncEntityType::BookmarkFolder,
                SyncEntityType::Note,
                SyncEntityType::Boost,
                SyncEntityType::ReadingListItem,
                SyncEntityType::SearchEngine,
                SyncEntityType::Shortcut,
                SyncEntityType::Memory,
                SyncEntityType::Extension,
                SyncEntityType::CssMod,
                SyncEntityType::Easel,
                SyncEntityType::Conversation,
                SyncEntityType::ConversationTurn,
                SyncEntityType::AutofillAddress,
                SyncEntityType::AutofillPayment,
                SyncEntityType::Settings,
                SyncEntityType::SharedCollection,
            ] {
                let last_pulled = sm.last_pulled_versions.entry(t.clone()).or_insert(0);
                *last_pulled = (*last_pulled).max(ceiling);
                if let Some(ref s) = self.storage {
                    if let Err(e) = s.set_sync_last_pulled_version(t.as_str(), *last_pulled) {
                        eprintln!(
                            "[sync] Failed to persist last_pulled_version for {}: {}",
                            t.as_str(),
                            e
                        );
                    }
                }
            }
        }

        Ok(())
    }

    fn push_sync_entity(
        &mut self,
        entity_type: SyncEntityType,
        id: String,
        payload_json: String,
        deleted: bool,
    ) {
        if let Some(ref sm) = self.sync_manager {
            if sm.config().disabled_entity_types.contains(&entity_type) {
                return; // Disabled entity type, skip sync push!
            }
        }

        match self.next_hlc_ts() {
            Ok(version) => {
                let device_id = self.account_manager.device_id_hash();
                let fields_hlc_json = if deleted {
                    None
                } else {
                    crate::sync_models::build_fields_hlc(&payload_json, version, device_id)
                };
                let sync_entity = crate::sync_models::SyncEntity {
                    entity_type,
                    entity_id: id,
                    version,
                    device_id,
                    schema_version: 1,
                    modified_at: chrono::Utc::now().timestamp(),
                    payload_json,
                    deleted,
                    queue_row_id: None,
                    fields_hlc_json,
                };
                self.sync_push_entity_persisted(sync_entity);

                // Increment push count and run scheduled check
                let count = self.sync_push_count.get() + 1;
                self.sync_push_count.set(count);

                let time_since_last = chrono::Utc::now() - self.sync_last_snapshot_time.get();
                let exceeds_time = time_since_last >= chrono::Duration::hours(6);
                let exceeds_count = count >= 500;

                if exceeds_time || exceeds_count {
                    let is_leader = self
                        .sync_manager
                        .as_ref()
                        .map(|sm| sm.is_leader())
                        .unwrap_or(false);
                    let startup_grace_passed = chrono::Utc::now() - self.sync_startup_time.get()
                        >= chrono::Duration::minutes(5);
                    let dedup_window_passed = chrono::Utc::now()
                        - self.sync_last_upload_attempt.get()
                        >= chrono::Duration::minutes(30);

                    if is_leader && startup_grace_passed && dedup_window_passed {
                        println!("[sync] Triggering scheduled snapshot upload (exceeds_time={}, exceeds_count={})", exceeds_time, exceeds_count);
                        self.trigger_async_snapshot_upload();
                        self.sync_push_count.set(0);
                        self.sync_last_snapshot_time.set(chrono::Utc::now());
                    }
                }
            }
            Err(e) => {
                eprintln!(
                    "[sync] Skipping push for {:?} id '{}': HLC failure: {}",
                    entity_type, id, e
                );
            }
        }
    }

    pub fn trigger_async_snapshot_upload(&self) {
        let Some(ref sm) = self.sync_manager else {
            return;
        };
        if !sm.is_leader() {
            return;
        }
        let room_id = match sm.config().sync_key.as_ref() {
            Some(key) => key.clone(),
            None => return,
        };
        let server_url = sm.config().server_url.clone();
        let rest_url = server_url
            .replace("wss://", "https://")
            .replace("ws://", "http://");
        let snapshot_endpoint = format!("{}/sync/snapshots", rest_url);
        let client = crate::http::shared_http_client();
        let token = if let Some(ref storage) = self.storage {
            storage
                .get_setting("auth:access_token")
                .ok()
                .flatten()
                .filter(|s| !s.is_empty())
                .unwrap_or_else(|| room_id.clone())
        } else {
            room_id.clone()
        };

        let encrypted_blob = match self.build_snapshot() {
            Ok(b) => b,
            Err(e) => {
                eprintln!("[sync] Failed to build snapshot for upload: {}", e);
                return;
            }
        };

        // Preflight size check: relay caps at 100 MB. Base64 inflates ~4/3× on the wire,
        // so a 75 MB encrypted blob becomes ~100 MB base64. Fail early with a clear log
        // instead of round-tripping to the relay for a 413.
        const MAX_ENCRYPTED_BLOB_BYTES: usize = 75 * 1024 * 1024;
        if encrypted_blob.len() > MAX_ENCRYPTED_BLOB_BYTES {
            eprintln!(
                "[sync] Snapshot blob {} bytes exceeds client preflight cap {} bytes; skipping upload",
                encrypted_blob.len(),
                MAX_ENCRYPTED_BLOB_BYTES
            );
            return;
        }

        use base64::Engine;
        let base64_blob = base64::engine::general_purpose::STANDARD.encode(&encrypted_blob);
        let hlc_ts_ceiling = self.last_hlc_ts.get() as i64;
        let creator_device_id = self.account_manager.device_id_hash() as i64;

        let json_body = serde_json::json!({
            "hlc_ts_ceiling": hlc_ts_ceiling,
            "creator_device_id": creator_device_id,
            "blob": base64_blob
        });

        self.sync_last_upload_attempt.set(chrono::Utc::now());

        std::thread::spawn(move || {
            let result = match tokio::runtime::Handle::try_current() {
                Ok(h) => h.block_on(async {
                    client
                        .post(&snapshot_endpoint)
                        .header("Authorization", format!("Bearer {}", token))
                        .json(&json_body)
                        .send()
                        .await
                }),
                Err(_) => crate::memory_manager::get_runtime().block_on(async {
                    client
                        .post(&snapshot_endpoint)
                        .header("Authorization", format!("Bearer {}", token))
                        .json(&json_body)
                        .send()
                        .await
                }),
            };
            match result {
                Ok(resp) if resp.status().is_success() => {
                    println!("[sync] Snapshot uploaded successfully to relay!");
                }
                Ok(resp) => eprintln!("[sync] Upload failed with status: {}", resp.status()),
                Err(e) => eprintln!("[sync] Upload HTTP request failed: {:?}", e),
            }
        });
    }

    pub fn bootstrap_sync_join_flow(&mut self, room_id: &str) -> Result<(), String> {
        let Some(ref sm) = self.sync_manager else {
            return Ok(());
        };
        let server_url = sm.config().server_url.clone();
        let rest_url = server_url
            .replace("wss://", "https://")
            .replace("ws://", "http://");
        let snapshot_endpoint = format!("{}/sync/snapshots", rest_url);
        let client = crate::http::shared_http_client();
        let room_id = room_id.to_string();
        let token = if let Some(ref storage) = self.storage {
            storage
                .get_setting("auth:access_token")
                .ok()
                .flatten()
                .filter(|s| !s.is_empty())
                .unwrap_or_else(|| room_id.clone())
        } else {
            room_id.clone()
        };

        let download_fut = async {
            match client
                .head(&snapshot_endpoint)
                .header("Authorization", format!("Bearer {}", token))
                .send()
                .await
            {
                Ok(resp) if resp.status().is_success() => {
                    match client
                        .get(&snapshot_endpoint)
                        .header("Authorization", format!("Bearer {}", token))
                        .send()
                        .await
                    {
                        Ok(resp) => resp
                            .json::<serde_json::Value>()
                            .await
                            .map_err(|e| format!("JSON download failed: {:?}", e)),
                        Err(e) => Err(format!("Download request failed: {:?}", e)),
                    }
                }
                Ok(resp) => Err(format!(
                    "No snapshot exists on relay (status: {})",
                    resp.status()
                )),
                Err(e) => Err(format!("Head request failed: {:?}", e)),
            }
        };

        let result = match tokio::runtime::Handle::try_current() {
            Ok(h) => h.block_on(download_fut),
            Err(_) => crate::memory_manager::get_runtime().block_on(download_fut),
        };

        match result {
            Ok(json_val) => {
                let base64_blob = json_val["blob"]
                    .as_str()
                    .ok_or_else(|| "Missing blob field in snapshot response".to_string())?;
                use base64::Engine;
                let encrypted_bytes = base64::engine::general_purpose::STANDARD
                    .decode(base64_blob)
                    .map_err(|e| format!("Base64 decode failed: {:?}", e))?;

                println!("[sync] Snapshot found on relay, applying...");
                if let Err(e) = self.apply_snapshot(&encrypted_bytes) {
                    eprintln!("[sync] Failed to apply snapshot: {:?}", e);
                    Err(e)
                } else {
                    println!("[sync] Snapshot applied successfully!");
                    Ok(())
                }
            }
            Err(e) => {
                println!("[sync] Join bootstrap: {}", e);
                Err(e)
            }
        }
    }

    /// Apply a batch of remote sync entities. Returns the list of actually-applied entities.
    pub fn apply_sync_remote_entities(
        &mut self,
        entities: Vec<crate::sync_models::SyncEntity>,
    ) -> Vec<crate::sync_models::SyncEntity> {
        let mut incoming_private_ids: HashSet<TabId> = HashSet::new();
        for entity in &entities {
            if entity.entity_type == SyncEntityType::Tab && !entity.deleted {
                if let Ok(tab) = serde_json::from_str::<maho_types::tab::Tab>(&entity.payload_json)
                {
                    if tab.is_private {
                        incoming_private_ids.insert(tab.id);
                    }
                }
            }
        }

        let local_private_ids = private_tab_ids_from_tabs(self.tab_manager.get_all_tabs());
        let mut blocked_private_ids = local_private_ids;
        blocked_private_ids.extend(incoming_private_ids);

        let mut valid_entities = Vec::new();
        for mut entity in entities {
            if entity.entity_type == SyncEntityType::Tab && !entity.deleted {
                if let Ok(tab) = serde_json::from_str::<maho_types::tab::Tab>(&entity.payload_json)
                {
                    if tab.is_private {
                        continue;
                    }
                }
            }
            if entity.entity_type == SyncEntityType::Space && !entity.deleted {
                if let Ok(space) =
                    serde_json::from_str::<maho_types::space::Space>(&entity.payload_json)
                {
                    let sanitized = sanitize_space_for_private_tabs(&space, &blocked_private_ids);
                    match serde_json::to_string(&sanitized) {
                        Ok(payload) => entity.payload_json = payload,
                        Err(e) => {
                            eprintln!(
                                "Rejected remote space {} due to sanitize serialization error: {}",
                                entity.entity_id, e
                            );
                            continue;
                        }
                    }
                }
            }
            if let Err(e) = self.update_hlc_ts_on_receive(entity.version) {
                eprintln!(
                    "Rejected remote entity {} due to invalid HLC: {}",
                    entity.entity_id, e
                );
                continue;
            }
            valid_entities.push(entity);
        }
        let applied = match &mut self.sync_manager {
            Some(sm) => sm.apply_remote_batch(valid_entities, &self.storage),
            None => return Vec::new(),
        };

        let mut updates = Vec::new();

        for entity in &applied {
            match entity.entity_type {
                SyncEntityType::Extension => {
                    if let Ok(payload) =
                        serde_json::from_str::<serde_json::Value>(&entity.payload_json)
                    {
                        if let Some(id) = payload.get("id").and_then(|v| v.as_str()) {
                            let enabled = payload
                                .get("enabled")
                                .and_then(|v| v.as_bool())
                                .unwrap_or(true);
                            let deleted = entity.deleted;

                            if let Some(ext) = self.extension_bridge.extensions.get_mut(id) {
                                if ext.enabled != enabled {
                                    ext.enabled = enabled;
                                }
                            }

                            if let Some(ref cb) = self.extension_bridge.sync_callback {
                                cb(id, enabled, deleted);
                            }
                        }
                    }
                }
                SyncEntityType::Shortcut => {
                    if let Ok(payload) =
                        serde_json::from_str::<serde_json::Value>(&entity.payload_json)
                    {
                        if let Some(action) = payload.get("action").and_then(|v| v.as_str()) {
                            if entity.deleted {
                                self.shortcut_manager.reset_shortcut(action);
                                if let Some(ref storage) = self.storage {
                                    let _ = storage.delete_shortcut(action);
                                }
                            } else {
                                if let Some(key_combo_val) = payload.get("keyCombo") {
                                    if let Ok(key_combo) =
                                        serde_json::from_value::<maho_types::keyboard::KeyCombo>(
                                            key_combo_val.clone(),
                                        )
                                    {
                                        let _ = self
                                            .shortcut_manager
                                            .set_shortcut(action, key_combo.clone());
                                        let enabled = self
                                            .shortcut_manager
                                            .get_all_bindings()
                                            .iter()
                                            .find(|b| b.action == action)
                                            .map(|b| b.enabled)
                                            .unwrap_or(true);
                                        self.shortcut_manager.restore_shortcut_state(
                                            action,
                                            key_combo.clone(),
                                            enabled,
                                            entity.version,
                                        );
                                        if let Some(ref storage) = self.storage {
                                            if let Ok(json) = serde_json::to_string(&key_combo) {
                                                let _ = storage.save_shortcut(
                                                    action,
                                                    &json,
                                                    entity.version,
                                                );
                                            }
                                        }
                                    }
                                }
                                if let Some(enabled) =
                                    payload.get("enabled").and_then(|v| v.as_bool())
                                {
                                    let key_combo = self
                                        .shortcut_manager
                                        .get_all_bindings()
                                        .iter()
                                        .find(|b| b.action == action)
                                        .map(|b| b.key_combo.clone())
                                        .unwrap_or_else(|| {
                                            maho_types::keyboard::KeyCombo::new("", vec![])
                                        });
                                    self.shortcut_manager.restore_shortcut_state(
                                        action,
                                        key_combo,
                                        enabled,
                                        entity.version,
                                    );
                                    if let Some(ref storage) = self.storage {
                                        let _ = storage.save_shortcut_enabled(
                                            action,
                                            enabled,
                                            entity.version,
                                        );
                                    }
                                }
                            }
                        }
                    }
                }
                SyncEntityType::Space => {
                    if entity.deleted {
                        let space_id = maho_types::identifiers::SpaceId::new(&entity.entity_id);
                        self.space_manager.delete_space(&space_id);
                    } else if let Ok(space) =
                        serde_json::from_str::<maho_types::space::Space>(&entity.payload_json)
                    {
                        self.space_manager.upsert_space(space);
                    }
                    let favorited_tab_ids: std::collections::HashSet<
                        maho_types::identifiers::TabId,
                    > = self
                        .tab_manager
                        .get_all_favorite_tabs()
                        .iter()
                        .map(|t| t.id.clone())
                        .collect();
                    self.space_manager
                        .strip_favorited_tabs_from_root_orders(&favorited_tab_ids);
                }
                SyncEntityType::Tab => {
                    if entity.deleted {
                        let tab_id = maho_types::identifiers::TabId::new(&entity.entity_id);
                        self.tab_manager.close_tab(&tab_id);
                    } else if let Ok(tab) =
                        serde_json::from_str::<maho_types::tab::Tab>(&entity.payload_json)
                    {
                        let is_favorite = tab.role.is_favorite(); // L3-EXEMPT: local query
                        let tab_id = tab.id.clone();
                        self.tab_manager.restore_tab(tab);
                        if is_favorite {
                            // L3-EXEMPT: local parameter
                            let mut favorited_tab_ids = std::collections::HashSet::new();
                            favorited_tab_ids.insert(tab_id);
                            self.space_manager
                                .strip_favorited_tabs_from_root_orders(&favorited_tab_ids);
                        }
                    }
                }
                SyncEntityType::Bookmark => {
                    if entity.deleted {
                        self.bookmark_manager.remove_bookmark(&entity.entity_id);
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_bookmark(&entity.entity_id);
                        }
                    } else if let Ok(bm) = serde_json::from_str::<crate::bookmark_manager::Bookmark>(
                        &entity.payload_json,
                    ) {
                        self.bookmark_manager.restore_bookmark(bm.clone());
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_bookmark(
                                &bm.id.0,
                                &bm.url,
                                &bm.title,
                                bm.folder_id.as_deref(),
                                &bm.created_at.0,
                            );
                        }
                    }
                }
                SyncEntityType::BookmarkFolder => {
                    if entity.deleted {
                        self.bookmark_manager.delete_folder(&entity.entity_id);
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_bookmark_folder(&entity.entity_id);
                        }
                    } else if let Ok(folder) = serde_json::from_str::<
                        crate::bookmark_manager::BookmarkFolder,
                    >(&entity.payload_json)
                    {
                        self.bookmark_manager.restore_folder(folder.clone());
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_bookmark_folder(
                                &folder.id,
                                &folder.name,
                                folder.parent_id.as_deref(),
                                &folder.created_at.0,
                            );
                        }
                    }
                }
                SyncEntityType::Note => {
                    if entity.deleted {
                        let note_id = maho_types::identifiers::NoteId::new(&entity.entity_id);
                        self.note_manager.delete_note(&note_id);
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_note(&entity.entity_id);
                        }
                    } else if let Ok(note) =
                        serde_json::from_str::<maho_types::note::Note>(&entity.payload_json)
                    {
                        self.note_manager.restore_note(note.clone());
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_note(
                                note.id.as_ref(),
                                note.linked_tab_id.as_ref().map(|tab_id| tab_id.as_ref()),
                                note.linked_url.as_ref().map(|url| url.as_ref()),
                                &note.content,
                                note.created_at.as_ref(),
                                note.updated_at.as_ref(),
                            );
                        }
                    }
                }
                SyncEntityType::Boost => {
                    if entity.deleted {
                        let boost_id = maho_types::identifiers::BoostId::new(&entity.entity_id);
                        self.boost_manager.delete(&boost_id);
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_boost(&entity.entity_id);
                        }
                    } else if let Ok(boost) =
                        serde_json::from_str::<maho_types::boost::Boost>(&entity.payload_json)
                    {
                        self.boost_manager.restore_boost(boost.clone());
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_boost(&boost);
                        }
                    }
                }
                SyncEntityType::ReadingListItem => {
                    if entity.deleted {
                        self.reading_list_manager.remove_item(&entity.entity_id);
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_reading_list_item(&entity.entity_id);
                        }
                    } else if let Ok(item) = serde_json::from_str::<
                        crate::reading_list_manager::ReadingListItem,
                    >(&entity.payload_json)
                    {
                        self.reading_list_manager.insert_item(item.clone());
                        if let Some(ref storage) = self.storage {
                            let tags_json = serde_json::to_string(&item.tags).unwrap_or_default();
                            let _ = storage.save_reading_list_item(
                                &item.id,
                                &item.url,
                                &item.title,
                                item.excerpt.as_deref(),
                                item.site_name.as_deref(),
                                item.favicon_url.as_deref(),
                                item.preview_image_url.as_deref(),
                                &item.added_at.0,
                                item.read_at.as_ref().map(|d| d.0.as_str()),
                                item.is_read,
                                item.estimated_read_minutes,
                                Some(&tags_json),
                            );
                        }
                    }
                }
                SyncEntityType::SearchEngine => {
                    if entity.deleted {
                        self.command_bar.remove_search_engine(&entity.entity_id);
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_search_engine(&entity.entity_id);
                        }
                    } else if let Ok(se) = serde_json::from_str::<
                        maho_types::search_engine::SearchEngine,
                    >(&entity.payload_json)
                    {
                        self.command_bar.add_search_engine(se.clone());
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_search_engine(
                                &se.id,
                                &se.name,
                                &se.url_template,
                                se.shortcut.as_deref(),
                                se.icon_url.as_deref(),
                                se.is_default,
                            );
                        }
                    }
                    updates.push(CoreUpdate::SearchEnginesUpdated {
                        engines: self.command_bar.get_search_engines(),
                    });
                }
                SyncEntityType::Memory => {
                    if entity.deleted {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_memory(&entity.entity_id);
                            let _ = self.memory_manager.rebuild_index(storage);
                        }
                    } else if let Ok(payload) =
                        serde_json::from_str::<serde_json::Value>(&entity.payload_json)
                    {
                        if let Some(fact) = payload.get("fact").and_then(|v| v.as_str()) {
                            let id = entity.entity_id.clone();
                            let source =
                                payload.get("source").and_then(|v| v.as_str()).unwrap_or("");
                            let session_id = payload.get("session_id").and_then(|v| v.as_str());
                            let categories = payload
                                .get("categories")
                                .and_then(|v| v.as_str())
                                .unwrap_or("");
                            let importance = payload
                                .get("importance")
                                .and_then(|v| v.as_f64())
                                .unwrap_or(0.5);
                            let metadata = payload.get("metadata").and_then(|v| v.as_str());

                            if let Some(ref storage) = self.storage {
                                let _ = storage.insert_memory(
                                    maho_storage::sqlite::MemoryInsertParams {
                                        id: &id,
                                        fact,
                                        source,
                                        session_id,
                                        categories,
                                        importance,
                                        metadata,
                                    },
                                );
                                self.memory_manager.embed_fact(id, fact.to_string());
                            }
                        }
                    }
                }
                SyncEntityType::CssMod => {
                    if entity.deleted {
                        self.css_mod_manager.uninstall_mod(&entity.entity_id);
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_css_mod(&entity.entity_id);
                        }
                    } else if let Ok(css_mod) =
                        serde_json::from_str::<maho_types::css_mod::CssMod>(&entity.payload_json)
                    {
                        self.css_mod_manager.install_mod(css_mod.clone());
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_css_mod(&css_mod);
                        }
                    }
                }
                SyncEntityType::Easel => {
                    if entity.deleted {
                        let easel_id = maho_types::identifiers::EaselId::new(&entity.entity_id);
                        self.easel_manager.delete_easel(&easel_id);
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_easel(&entity.entity_id);
                        }
                    } else if let Ok(easel) =
                        serde_json::from_str::<maho_types::easel::Easel>(&entity.payload_json)
                    {
                        self.easel_manager.restore_easel(easel.clone());
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_easel(&easel);
                        }
                    }
                }
                SyncEntityType::Conversation => {
                    if entity.deleted {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_conversation(&entity.entity_id);
                        }
                    } else if let Ok(conv) =
                        serde_json::from_str::<maho_types::chat::Conversation>(&entity.payload_json)
                    {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.create_conversation(
                                &conv.id,
                                conv.title.as_deref(),
                                conv.space_id.as_deref(),
                                conv.model.as_deref(),
                            );
                        }
                    }
                }
                SyncEntityType::ConversationTurn => {
                    if entity.deleted {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_conversation_turn(&entity.entity_id);
                        }
                    } else if let Ok(turn) = serde_json::from_str::<
                        maho_types::chat::ConversationTurn,
                    >(&entity.payload_json)
                    {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.insert_conversation_turn(
                                &turn.id,
                                &turn.session_id,
                                &turn.role,
                                &turn.content,
                                turn.url_context.as_deref(),
                            );
                        }
                    }
                }
                SyncEntityType::AutofillAddress => {
                    if entity.deleted {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_autofill_address(&entity.entity_id);
                        }
                    } else if let Ok(addr) = serde_json::from_str::<
                        maho_types::autofill::AutofillAddress,
                    >(&entity.payload_json)
                    {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_autofill_address(
                                maho_storage::sqlite::AutofillAddressParams {
                                    id: &addr.id,
                                    name: &addr.name,
                                    street: &addr.street,
                                    city: &addr.city,
                                    state: &addr.state,
                                    zip: &addr.zip,
                                    country: &addr.country,
                                    phone: addr.phone.as_deref(),
                                    email: addr.email.as_deref(),
                                    address_line2: addr.address_line2.as_deref(),
                                },
                            );
                        }
                    }
                }
                SyncEntityType::AutofillPayment => {
                    if entity.deleted {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_autofill_payment(&entity.entity_id);
                        }
                    } else if let Ok(pay) = serde_json::from_str::<
                        maho_types::autofill::AutofillPayment,
                    >(&entity.payload_json)
                    {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_autofill_payment(
                                &pay.id,
                                &pay.card_name,
                                &pay.last_four,
                                &pay.expiry,
                                pay.card_network.as_deref(),
                            );
                        }
                    }
                }
                SyncEntityType::Settings => {
                    if !entity.deleted {
                        if let Ok(settings) = serde_json::from_str::<maho_types::settings::Settings>(
                            &entity.payload_json,
                        ) {
                            self.settings_manager.restore_settings(settings);
                            updates.push(CoreUpdate::SettingsChanged {
                                settings: self.settings_manager.get_settings().clone(),
                            });
                        }
                    }
                }
                SyncEntityType::SharedCollection => {
                    if entity.deleted {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.delete_shared_collection(&entity.entity_id);
                        }
                    } else if let Ok(col) = serde_json::from_str::<
                        maho_types::sharing::SharedCollection,
                    >(&entity.payload_json)
                    {
                        if let Some(ref storage) = self.storage {
                            let _ = storage.save_shared_collection(&col);
                        }
                    }
                }
            }
        }

        self.emit_updates(updates);
        applied
    }

    /// Get and drain pending outgoing sync messages.
    pub fn drain_sync_outgoing(&mut self) -> Vec<crate::sync_models::SyncMessage> {
        let msgs = match &mut self.sync_manager {
            Some(sm) => sm.drain_outgoing_messages(),
            None => Vec::new(),
        };
        if let Some(ref mut sm) = self.sync_manager {
            let sent_ids = sm.drain_sent_entities();
            if !sent_ids.is_empty() {
                if let Some(ref storage) = self.storage {
                    if let Err(e) = storage.mark_sync_entities_sent(&sent_ids) {
                        eprintln!("Failed to mark sync entities sent in DB: {}", e);
                    }
                }
            }
        }
        msgs
    }

    /// Push an entity through sync, persisting to queue if storage available
    pub fn sync_push_entity_persisted(&mut self, mut entity: SyncEntity) {
        // Persist to SQLite queue first
        if let Some(ref storage) = self.storage {
            let entity_type_str = entity.entity_type.as_str();
            if let Ok(row_id) = storage.enqueue_sync_entity(
                entity_type_str,
                &entity.entity_id,
                entity.version,
                entity.modified_at,
                &entity.payload_json,
                entity.deleted,
            ) {
                entity.queue_row_id = Some(row_id);
            }
            // Also update entity_versions table for local change
            let _ = storage.save_entity_version(
                entity_type_str,
                &entity.entity_id,
                entity.version,
                entity.device_id,
                entity.fields_hlc_json.as_deref(),
                Some(&entity.payload_json),
            );
        }
        // Then push to in-memory sync manager
        if let Some(ref mut sm) = self.sync_manager {
            let cache_key = format!("{}:{}", entity.entity_type.as_str(), entity.entity_id);
            sm.set_entity_version_in_cache(
                &cache_key,
                entity.version,
                entity.device_id,
                entity.fields_hlc_json.clone(),
                Some(entity.payload_json.clone()),
            );
            sm.push_entity(entity);
        }
    }

    /// Load pending sync entities from persistent storage on startup
    pub fn load_sync_queue(&mut self) -> Result<(), PersistenceError> {
        let storage = match self.storage.as_ref() {
            Some(s) => s,
            None => return Ok(()),
        };
        let entries = storage
            .load_pending_sync_entities()
            .map_err(|e| PersistenceError::Storage(e.to_string()))?;
        for (id, entity_type_str, entity_id, version, modified_at, payload_json, deleted) in entries
        {
            let entity_type = match parse_sync_entity_type(&entity_type_str) {
                Some(et) => et,
                None => {
                    eprintln!(
                        "[sync] Skipping unknown pending entity type '{}'",
                        entity_type_str
                    );
                    continue;
                }
            };
            let device_id = self.account_manager.device_id_hash();
            let fields_hlc_json = if deleted {
                None
            } else {
                crate::sync_models::build_fields_hlc(&payload_json, version, device_id)
            };
            let entity = SyncEntity {
                entity_type,
                entity_id,
                version,
                device_id,
                schema_version: 1,
                modified_at,
                payload_json,
                deleted,
                queue_row_id: Some(id),
                fields_hlc_json,
            };
            if let Some(ref mut sm) = self.sync_manager {
                sm.push_entity(entity);
            }
        }
        Ok(())
    }

    fn handle_memory_result(&mut self, result: MemoryResult, updates: &mut Vec<CoreUpdate>) {
        match result {
            MemoryResult::FactsExtracted { session_id, facts } => {
                let mut facts_to_push = Vec::new();
                if let Some(ref storage) = self.storage {
                    let mut new_ids = Vec::new();
                    for fact in &facts {
                        let id = uuid::Uuid::new_v4().to_string();
                        let categories = serde_json::json!([fact.category]).to_string();
                        let _ = storage.insert_memory(maho_storage::sqlite::MemoryInsertParams {
                            id: &id,
                            fact: &fact.fact,
                            source: "chat",
                            session_id: Some(&session_id),
                            categories: &categories,
                            importance: fact.importance as f64,
                            metadata: None,
                        });
                        self.memory_manager
                            .embed_fact(id.clone(), fact.fact.clone());
                        new_ids.push(id.clone());

                        let payload = serde_json::json!({
                            "id": id,
                            "fact": fact.fact,
                            "source": "chat",
                            "session_id": session_id,
                            "categories": categories,
                            "importance": fact.importance as f64,
                            "metadata": serde_json::Value::Null,
                        })
                        .to_string();
                        facts_to_push.push((id, payload));
                    }
                    updates.push(CoreUpdate::MemoryFactsExtracted {
                        session_id,
                        fact_count: facts.len(),
                    });
                }
                for (id, payload) in facts_to_push {
                    self.push_sync_entity(SyncEntityType::Memory, id, payload, false);
                }
            }
            MemoryResult::EmbeddingsReady { fact_id, embedding } => {
                if let Some(ref storage) = self.storage {
                    let bytes = crate::embedding::vec_to_bytes(&embedding);
                    let _ = storage.update_memory_embedding(&fact_id, &bytes);
                    let _ = self.memory_manager.rebuild_index(storage);
                    updates.push(CoreUpdate::MemoryUpdated {
                        changed_ids: vec![fact_id],
                    });
                }
            }
            MemoryResult::BrowsingSummaryReady { content } => {
                if let Some(ref storage) = self.storage {
                    let token_count = (content.len() / 4) as i64;
                    let _ = storage.upsert_memory_block(
                        "browsing_summary",
                        &content,
                        "warm",
                        token_count,
                    );
                    let now = chrono::Utc::now();
                    let now_iso = now.to_rfc3339();
                    let _ = storage.upsert_memory_block(
                        "_meta:last_browsing_summary",
                        &now_iso,
                        "cold",
                        0,
                    );
                    self.memory_manager.note_summary_completed(now);
                }
            }
            MemoryResult::BrowsingSummaryDue => {
                if let Some(ref storage) = self.storage {
                    let mut last_summary_at = None;
                    if let Ok(Some(content)) =
                        storage.get_memory_block("_meta:last_browsing_summary")
                    {
                        if let Ok(dt) = chrono::DateTime::parse_from_rfc3339(&content) {
                            last_summary_at = Some(dt.with_timezone(&chrono::Utc));
                        }
                    }
                    let now = chrono::Utc::now();
                    let should_summarize = match last_summary_at {
                        Some(last) => now - last >= chrono::Duration::hours(24),
                        None => true,
                    };
                    if should_summarize {
                        let since_dt = now - chrono::Duration::hours(24);
                        let since_iso = since_dt.to_rfc3339();
                        if let Ok(sessions) = storage.list_browse_sessions_since(&since_iso) {
                            if !sessions.is_empty() {
                                let mut formatted_text = String::new();
                                for (_id, url, title) in sessions {
                                    let t = title.unwrap_or_else(|| "Untitled".to_string());
                                    formatted_text.push_str(&format!("- [{}] {}\n", t, url));
                                }
                                self.memory_manager.summarize_browsing(formatted_text);
                            }
                        }
                    }
                }
            }
            MemoryResult::Failed { reason } => {
                eprintln!("memory: job failed: {}", reason);
            }
        }
    }

    /// Set Memory auth from the shell (called when AI prefs change)
    pub fn set_memory_auth(&mut self, auth: Option<AuthSnapshot>) {
        self.memory_manager.set_auth(auth);
    }

    /// Get top-K relevant memory ids for a query embedding
    pub fn search_memories(&self, query_embedding: &[f32], top_k: usize) -> Vec<String> {
        self.memory_manager.search(query_embedding, top_k)
    }

    /// Set or replace a hot/warm memory block (used by Personalization UI)
    pub fn set_memory_block(
        &self,
        label: &str,
        content: &str,
        tier: &str,
    ) -> Result<(), crate::error::CoreError> {
        if let Some(ref storage) = self.storage {
            let token_count = (content.len() / 4) as i64;
            storage
                .upsert_memory_block(label, content, tier, token_count)
                .map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
        }
        Ok(())
    }

    pub fn get_memory_block(&self, label: &str) -> Option<String> {
        self.storage
            .as_ref()?
            .get_memory_block(label)
            .ok()
            .flatten()
    }

    pub fn delete_memory(&mut self, id: &str) -> bool {
        let Some(ref storage) = self.storage else {
            return false;
        };
        let deleted = storage.delete_memory(id).unwrap_or(false);
        if deleted {
            let _ = self.memory_manager.rebuild_index(storage);
            self.push_sync_entity(
                SyncEntityType::Memory,
                id.to_string(),
                "{}".to_string(),
                true,
            );
        }
        deleted
    }

    pub fn clear_all_memories(&mut self) -> bool {
        let Some(ref storage) = self.storage else {
            return false;
        };
        let memories_to_delete = storage.load_all_memories().unwrap_or_default();
        let ok = storage.delete_all_memories().is_ok();
        if ok {
            let _ = self.memory_manager.rebuild_index(storage);
            for (id, _, _, _, _, _, _) in memories_to_delete {
                self.push_sync_entity(SyncEntityType::Memory, id, "{}".to_string(), true);
            }
        }
        ok
    }

    pub fn create_conversation_persisted(
        &mut self,
        id: &str,
        title: Option<&str>,
        space_id: Option<&str>,
        model: Option<&str>,
    ) -> bool {
        let mut payload_opt = None;
        let mut ok = false;
        if let Some(ref storage) = self.storage {
            ok = storage
                .create_conversation(id, title, space_id, model)
                .is_ok();
            if ok {
                if let Ok(convs) = storage.list_conversations(999999) {
                    if let Some(conv) = convs.iter().find(|c| c.id == id) {
                        payload_opt = serde_json::to_string(conv).ok();
                    }
                }
            }
        }
        if let Some(payload) = payload_opt {
            self.push_sync_entity(SyncEntityType::Conversation, id.to_string(), payload, false);
        }
        ok
    }

    pub fn save_conversation_message_persisted(
        &mut self,
        session_id: &str,
        role: &str,
        content: &str,
        url_context: Option<&str>,
    ) -> bool {
        let mut payload_opt = None;
        let mut id_opt = None;
        let mut ok = false;
        if let Some(ref storage) = self.storage {
            let id = uuid::Uuid::new_v4().to_string();
            ok = storage
                .insert_conversation_turn(&id, session_id, role, content, url_context)
                .is_ok();
            if ok {
                if let Ok(turns) = storage.get_conversation_messages(session_id) {
                    if let Some(turn) = turns.iter().find(|t| t.id == id) {
                        payload_opt = serde_json::to_string(turn).ok();
                        id_opt = Some(id);
                    }
                }
            }
        }
        if let (Some(id), Some(payload)) = (id_opt, payload_opt) {
            self.push_sync_entity(SyncEntityType::ConversationTurn, id, payload, false);
        }
        ok
    }

    pub fn delete_conversation_persisted(&mut self, id: &str) -> bool {
        let mut turn_ids = Vec::new();
        if let Some(ref storage) = self.storage {
            if let Ok(turns) = storage.get_conversation_messages(id) {
                for turn in turns {
                    turn_ids.push(turn.id.clone());
                }
            }
        }
        for turn_id in &turn_ids {
            self.push_sync_entity(
                SyncEntityType::ConversationTurn,
                turn_id.clone(),
                "{}".to_string(),
                true,
            );
        }
        let mut ok = false;
        if let Some(ref storage) = self.storage {
            for turn_id in &turn_ids {
                let _ = storage.delete_conversation_turn(turn_id);
            }
            ok = storage.delete_conversation(id).unwrap_or(false);
        }
        if ok {
            self.push_sync_entity(
                SyncEntityType::Conversation,
                id.to_string(),
                "{}".to_string(),
                true,
            );
        }
        ok
    }

    pub fn rename_conversation_persisted(&mut self, id: &str, title: &str) -> bool {
        let mut payload_opt = None;
        let mut ok = false;
        if let Some(ref storage) = self.storage {
            ok = storage.rename_conversation(id, title).unwrap_or(false);
            if ok {
                if let Ok(convs) = storage.list_conversations(999999) {
                    if let Some(conv) = convs.iter().find(|c| c.id == id) {
                        payload_opt = serde_json::to_string(conv).ok();
                    }
                }
            }
        }
        if let Some(payload) = payload_opt {
            self.push_sync_entity(SyncEntityType::Conversation, id.to_string(), payload, false);
        }
        ok
    }

    /// Toggle Memory entity sync per opt-out UX
    pub fn set_entity_sync_enabled(&mut self, type_str: &str, enabled: bool) {
        let entity_type = match type_str {
            "Memory" => crate::sync_models::SyncEntityType::Memory,
            _ => return,
        };
        if let Some(ref mut sm) = self.sync_manager {
            sm.set_entity_sync_enabled(entity_type, enabled);
        }
    }

    /// Hot block compose for system prompt injection
    pub fn build_personalization_system_prompt(&self) -> Option<String> {
        const HOT_BUDGET_TOKENS: usize = 2000;
        const WARM_BUDGET_TOKENS: usize = 1000;
        const CHARS_PER_TOKEN: usize = 4;

        let storage = self.storage.as_ref()?;
        let mut out = String::new();

        let hot_budget_chars = HOT_BUDGET_TOKENS * CHARS_PER_TOKEN;
        let mut hot_used = 0usize;
        let hot_blocks = storage.list_memory_blocks_by_tier("hot").ok()?;
        for (label, content) in &hot_blocks {
            if content.trim().is_empty() {
                continue;
            }
            let entry_chars = label.len() + content.len() + 8;
            let truncated = if hot_used + entry_chars > hot_budget_chars {
                let remaining = hot_budget_chars.saturating_sub(hot_used + label.len() + 8);
                if remaining == 0 {
                    break;
                }
                let mut end = remaining.min(content.len());
                while !content.is_char_boundary(end) && end > 0 {
                    end -= 1;
                }
                &content[..end]
            } else {
                content.as_str()
            };
            out.push_str(&format!("## {}\n{}\n\n", label, truncated));
            hot_used += label.len() + truncated.len() + 8;
            if hot_used >= hot_budget_chars {
                break;
            }
        }

        let warm_budget_chars = WARM_BUDGET_TOKENS * CHARS_PER_TOKEN;
        let mut warm_used = 0usize;
        let warm_blocks = storage
            .list_memory_blocks_by_tier("warm")
            .ok()
            .unwrap_or_default();
        for (label, content) in &warm_blocks {
            if content.trim().is_empty() {
                continue;
            }
            let entry_chars = label.len() + content.len() + 8;
            let truncated = if warm_used + entry_chars > warm_budget_chars {
                let remaining = warm_budget_chars.saturating_sub(warm_used + label.len() + 8);
                if remaining == 0 {
                    break;
                }
                let mut end = remaining.min(content.len());
                while !content.is_char_boundary(end) && end > 0 {
                    end -= 1;
                }
                &content[..end]
            } else {
                content.as_str()
            };
            out.push_str(&format!("## {}\n{}\n\n", label, truncated));
            warm_used += label.len() + truncated.len() + 8;
            if warm_used >= warm_budget_chars {
                break;
            }
        }

        if out.is_empty() {
            None
        } else {
            Some(out)
        }
    }

    pub fn fts_relevant_memories(&self, query: &str, top_k: usize) -> Vec<(String, String, f64)> {
        let Some(storage) = self.storage.as_ref() else {
            return Vec::new();
        };
        const COLD_BUDGET_TOKENS: usize = 1000;
        const CHARS_PER_TOKEN: usize = 4;
        let budget_chars = COLD_BUDGET_TOKENS * CHARS_PER_TOKEN;
        let raw = storage
            .search_memories_fts(query, top_k)
            .unwrap_or_default();
        let mut out = Vec::new();
        let mut used = 0usize;
        for (id, content, importance) in raw {
            let next = used + content.len() + 4;
            if next > budget_chars && !out.is_empty() {
                break;
            }
            used = next;
            out.push((id, content, importance));
        }
        out
    }
}

fn parse_sync_entity_type(s: &str) -> Option<SyncEntityType> {
    match s.to_lowercase().as_str() {
        "space" => Some(SyncEntityType::Space),
        "tab" => Some(SyncEntityType::Tab),
        "bookmark" => Some(SyncEntityType::Bookmark),
        "bookmarkfolder" => Some(SyncEntityType::BookmarkFolder),
        "note" => Some(SyncEntityType::Note),
        "boost" => Some(SyncEntityType::Boost),
        "readinglistitem" => Some(SyncEntityType::ReadingListItem),
        "searchengine" => Some(SyncEntityType::SearchEngine),
        "shortcut" => Some(SyncEntityType::Shortcut),
        "memory" => Some(SyncEntityType::Memory),
        "extension" => Some(SyncEntityType::Extension),
        "cssmod" => Some(SyncEntityType::CssMod),
        "easel" => Some(SyncEntityType::Easel),
        "conversation" => Some(SyncEntityType::Conversation),
        "conversationturn" => Some(SyncEntityType::ConversationTurn),
        "autofilladdress" => Some(SyncEntityType::AutofillAddress),
        "autofillpayment" => Some(SyncEntityType::AutofillPayment),
        "settings" => Some(SyncEntityType::Settings),
        "sharedcollection" => Some(SyncEntityType::SharedCollection),
        _ => None,
    }
}

// === Trait implementations for EventDispatcher ===

impl TabManagerInterface for TabLifecycleManager {
    fn create_tab(
        &mut self,
        space_id: SpaceId,
        url: Option<Url>,
        parent_id: Option<TabId>,
        window_id: Option<i64>,
        is_private: bool,
    ) -> Tab {
        self.create_tab(space_id, url, parent_id, window_id, is_private)
    }

    fn create_tab_with_id(
        &mut self,
        tab_id: TabId,
        space_id: SpaceId,
        url: Option<Url>,
        parent_id: Option<TabId>,
        window_id: Option<i64>,
        is_private: bool,
    ) -> Tab {
        self.create_tab_with_id(tab_id, space_id, url, parent_id, window_id, is_private)
    }

    fn close_tab(&mut self, tab_id: &TabId) -> Option<Tab> {
        self.close_tab(tab_id)
    }

    fn activate_tab(&mut self, tab_id: &TabId) {
        self.activate_tab(tab_id)
    }

    fn restore_to_active(&mut self, tab_id: &TabId) {
        self.restore_to_active(tab_id)
    }

    fn duplicate_tab(&mut self, tab_id: &TabId) -> Option<Tab> {
        self.duplicate_tab(tab_id)
    }

    fn move_tab(
        &mut self,
        tab_id: &TabId,
        target_space: SpaceId,
        position: usize,
    ) -> Option<SpaceId> {
        self.move_tab(tab_id, target_space, position)
    }

    fn set_tab_parent(&mut self, tab_id: &TabId, new_parent_id: Option<TabId>) -> bool {
        self.set_tab_parent(tab_id, new_parent_id)
    }

    fn pin_tab(&mut self, tab_id: &TabId) {
        self.pin_tab(tab_id)
    }

    fn unpin_tab(&mut self, tab_id: &TabId) {
        self.unpin_tab(tab_id)
    }

    fn favorite_tab(&mut self, tab_id: &TabId) {
        self.favorite_tab(tab_id);
    }

    fn transition_tab_role(&mut self, tab_id: &TabId, new_role: TabRole) -> bool {
        self.transition_tab_role(tab_id, new_role)
    }

    fn count_favorite_tabs_in_spaces(
        &self,
        space_ids: &std::collections::HashSet<SpaceId>,
    ) -> usize {
        self.count_favorite_tabs_in_spaces(space_ids)
    }

    fn reorder_favorite(
        &mut self,
        tab_id: &TabId,
        new_index: usize,
        space_ids: &std::collections::HashSet<SpaceId>,
    ) {
        self.reorder_favorite(tab_id, new_index, space_ids)
    }

    fn mute_tab(&mut self, tab_id: &TabId) {
        self.mute_tab(tab_id)
    }

    fn unmute_tab(&mut self, tab_id: &TabId) {
        self.unmute_tab(tab_id)
    }

    fn freeze_tab(&mut self, tab_id: &TabId) {
        self.freeze_tab(tab_id)
    }

    fn suspend_tab(&mut self, tab_id: &TabId) {
        self.suspend_tab(tab_id)
    }

    fn archive_tab(&mut self, tab_id: &TabId) {
        self.archive_tab(tab_id)
    }

    fn reopen_last_closed(&mut self) -> Option<Tab> {
        self.reopen_last_closed()
    }

    fn handle_memory_pressure(&mut self, level: MemoryPressureLevel) -> MemoryAction {
        self.handle_memory_pressure(level)
    }

    fn update_tab_title(&mut self, tab_id: &TabId, title: String) {
        self.update_tab_title(tab_id, title)
    }

    fn update_tab_custom_title(&mut self, tab_id: &TabId, custom_title: Option<String>) {
        self.update_tab_custom_title(tab_id, custom_title)
    }

    fn update_tab_url(&mut self, tab_id: &TabId, url: Url) {
        self.update_tab_url(tab_id, url)
    }

    fn update_tab_favicon(
        &mut self,
        tab_id: &TabId,
        favicon: Option<maho_types::common::ImageData>,
    ) {
        TabLifecycleManager::update_tab_favicon(self, tab_id, favicon)
    }

    fn update_tab_loading(&mut self, tab_id: &TabId, is_loading: bool) {
        self.update_tab_loading(tab_id, is_loading)
    }

    fn restore_archived_tab(&mut self, tab_id: &TabId) {
        self.restore_archived_tab(tab_id)
    }

    fn delete_archived_tab(&mut self, tab_id: &TabId) -> bool {
        self.delete_archived_tab(tab_id)
    }

    fn get_tab(&self, tab_id: &TabId) -> Option<&maho_types::tab::Tab> {
        self.get_tab(tab_id)
    }

    fn to_tab_view_model(&self, tab: &Tab) -> TabViewModel {
        self.to_view_model(tab)
    }

    fn is_tab_loading(&self, tab_id: &TabId) -> bool {
        self.is_tab_loading(tab_id)
    }

    fn update_tab_security(&mut self, tab_id: &TabId, is_secure: bool) {
        self.update_tab_security(tab_id, is_secure)
    }

    fn update_tab_scroll_position(&mut self, tab_id: &TabId, x: f64, y: f64) {
        self.update_tab_scroll_position(tab_id, x, y)
    }

    fn is_tab_secure(&self, tab_id: &TabId) -> bool {
        self.is_tab_secure(tab_id)
    }

    fn close_other_tabs(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId> {
        self.close_other_tabs(space_id, tab_id)
    }

    fn close_tabs_to_right(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId> {
        self.close_tabs_to_right(space_id, tab_id)
    }

    fn close_tabs_to_left(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId> {
        self.close_tabs_to_left(space_id, tab_id)
    }

    fn toggle_freeze(&mut self, tab_id: &TabId) {
        self.toggle_freeze(tab_id)
    }

    fn release_window_tabs(&mut self, window_id: i64) -> Vec<TabId> {
        self.release_window_tabs(window_id)
    }

    fn collect_child_ids(&self, parent_id: &TabId) -> Vec<TabId> {
        self.collect_child_ids(parent_id)
    }
}

impl SpaceManagerInterface for SpaceManager {
    fn create_space(&mut self, name: &str, color: SpaceColor, profile_id: ProfileId) -> Space {
        self.create_space(name, color, profile_id)
    }

    fn delete_space(&mut self, space_id: &SpaceId) -> Option<SpaceId> {
        self.delete_space(space_id)
    }

    fn activate_space(&mut self, space_id: &SpaceId) {
        self.activate_space(space_id)
    }

    fn update_space_config(
        &mut self,
        changes: maho_types::space::SpaceConfigUpdate,
    ) -> Result<Option<maho_types::space::SpaceConfigUpdate>, crate::space_manager::SpaceUpdateError>
    {
        self.update_space_config(changes)
    }

    fn reorder_space(&mut self, space_id: &SpaceId, from: usize, to: usize) {
        self.reorder_space(space_id, from, to)
    }

    fn add_tab_to_space(&mut self, space_id: &SpaceId, tab_id: TabId, position: Option<usize>) {
        self.add_tab_to_space(space_id, tab_id, position)
    }

    fn remove_tab_from_space(&mut self, space_id: &SpaceId, tab_id: &TabId) {
        self.remove_tab_from_space(space_id, tab_id)
    }

    fn reorder_tab(&mut self, space_id: &SpaceId, tab_id: &TabId, before_tab_id: Option<&TabId>) {
        self.reorder_tab(space_id, tab_id, before_tab_id)
    }

    fn move_tab_to_root(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
        before_tab_id: Option<&TabId>,
    ) {
        self.move_tab_to_root(space_id, folder_id, tab_id, before_tab_id)
    }

    fn reorder_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        parent_folder_id: Option<&FolderId>,
        before_folder_id: Option<&FolderId>,
    ) -> Result<(), crate::space_manager::FolderError> {
        self.reorder_folder(space_id, folder_id, parent_folder_id, before_folder_id)
    }

    fn get_active_space_id(&self) -> &SpaceId {
        self.get_active_space_id()
    }

    fn get_space_order(&self) -> &[SpaceId] {
        SpaceManager::get_space_order(self)
    }

    fn get_space(&self, space_id: &SpaceId) -> Option<&maho_types::space::Space> {
        SpaceManager::get_space(self, space_id)
    }

    fn get_space_view_model(
        &self,
        space_id: &SpaceId,
    ) -> Option<maho_types::traits::shell_renderer::SpaceViewModel> {
        self.get_space(space_id)
            .map(|space| self.to_view_model(space))
    }

    fn get_space_ids_for_profile(
        &self,
        profile_id: &ProfileId,
    ) -> std::collections::HashSet<SpaceId> {
        self.get_space_ids_for_profile(profile_id)
    }

    fn create_folder(
        &mut self,
        space_id: &SpaceId,
        name: &str,
        is_pinned: bool,
        parent_folder_id: Option<FolderId>,
    ) -> Option<maho_types::traits::shell_renderer::FolderViewModel> {
        self.create_folder(space_id, name, is_pinned, parent_folder_id)
    }

    fn create_folder_with_provider(
        &mut self,
        space_id: &SpaceId,
        name: &str,
        is_pinned: bool,
        parent_folder_id: Option<FolderId>,
        provider_type: Option<String>,
        config_json: Option<String>,
    ) -> Option<maho_types::traits::shell_renderer::FolderViewModel> {
        self.create_folder_with_provider(
            space_id,
            name,
            is_pinned,
            parent_folder_id,
            provider_type,
            config_json,
        )
    }

    fn rename_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, name: &str) {
        self.rename_folder(space_id, folder_id, name)
    }

    fn delete_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        self.delete_folder(space_id, folder_id)
    }

    fn add_tab_to_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, tab_id: TabId) {
        self.add_tab_to_folder(space_id, folder_id, tab_id)
    }

    fn reorder_tab_in_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
        to: usize,
    ) {
        self.reorder_tab_in_folder(space_id, folder_id, tab_id, to)
    }

    fn remove_tab_from_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, tab_id: &TabId) {
        self.remove_tab_from_folder(space_id, folder_id, tab_id)
    }

    fn remove_empty_folders_in_space(&mut self, space_id: &SpaceId) -> Vec<FolderId> {
        self.remove_empty_folders_in_space(space_id)
    }

    fn toggle_folder_expanded(&mut self, space_id: &SpaceId, folder_id: &FolderId) -> bool {
        self.toggle_folder_expanded(space_id, folder_id)
    }

    fn find_folder_expanded(&self, space_id: &SpaceId, folder_id: &FolderId) -> Option<bool> {
        self.find_folder_expanded(space_id, folder_id)
    }

    fn set_folder_pinned(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        is_pinned: bool,
    ) -> bool {
        self.set_folder_pinned(space_id, folder_id, is_pinned)
    }

    fn move_folder(
        &mut self,
        folder_id: &FolderId,
        new_parent: Option<FolderId>,
    ) -> Result<(), crate::space_manager::FolderError> {
        self.move_folder(folder_id, new_parent)
    }

    fn move_folder_in_space(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        new_parent: Option<FolderId>,
    ) -> Result<(), crate::space_manager::FolderError> {
        self.move_folder_in_space(space_id, folder_id, new_parent)
    }

    fn move_folder_to_space(
        &mut self,
        folder_id: &FolderId,
        source_space_id: &SpaceId,
        target_space_id: &SpaceId,
    ) {
        self.move_folder_to_space(folder_id, source_space_id, target_space_id)
    }

    fn move_folder_to_root(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        before_folder_id: Option<&FolderId>,
    ) {
        self.move_folder_to_root(space_id, folder_id, before_folder_id)
    }

    fn get_folder_view_models(
        &self,
        space_id: &SpaceId,
    ) -> Vec<maho_types::traits::shell_renderer::FolderViewModel> {
        self.get_folder_view_models(space_id)
    }

    fn set_last_active_tab(&mut self, space_id: &SpaceId, tab_id: Option<TabId>) {
        self.set_last_active_tab(space_id, tab_id)
    }

    fn get_last_active_tab(&self, space_id: &SpaceId) -> Option<&TabId> {
        self.get_last_active_tab(space_id)
    }

    fn is_tab_in_folder(&self, space_id: &SpaceId, tab_id: &TabId) -> bool {
        self.is_tab_in_folder(space_id, tab_id)
    }

    fn apply_tab_residency(
        &mut self,
        space_id: &SpaceId,
        tab_id: &TabId,
        is_favorite: bool, // L3-EXEMPT: local parameter
        is_in_folder: bool,
    ) {
        self.apply_tab_residency(space_id, tab_id, is_favorite, is_in_folder) // L3-EXEMPT: local parameter
    }

    fn remove_tab_from_root_order(&mut self, space_id: &SpaceId, tab_id: &TabId) {
        self.remove_tab_from_root_order(space_id, tab_id)
    }

    fn reorder_root_item(
        &mut self,
        space_id: &SpaceId,
        item: &maho_types::space::RootItem,
        insertion_point: &maho_types::space::RootInsertionPoint,
    ) {
        self.reorder_root_item(space_id, item, insertion_point)
    }
}

impl CommandBarInterface for CommandBarEngine {
    fn search(
        &self,
        query: &str,
        mode: Option<&str>,
        ctx: &SearchContext,
    ) -> Vec<SuggestionViewModel> {
        self.search(query, mode, ctx)
    }
}

fn extract_domain_from_url(url: &str) -> Option<String> {
    let host = url
        .strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))
        .unwrap_or(url);
    let host = host.split('/').next()?.split(':').next()?;
    Some(host.to_string())
}
