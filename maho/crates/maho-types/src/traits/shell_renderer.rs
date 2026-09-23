use serde::{Deserialize, Serialize};

use crate::animation::spring_config::SpringConfig;
use crate::common::{DateTime, DropTarget, ImageData, Orientation, Point};
use crate::identifiers::{BoostId, FolderId, PaneId, ProfileId, SpaceId, TabId};
use crate::space::{ActiveSpaceModel, SpaceColor, SpaceTheme};
use crate::split_view::SplitConfig;

use crate::tab::TabRole;

// === View Models ===

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TabViewModel {
    pub id: TabId,
    pub space_id: SpaceId,
    pub title: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub custom_title: Option<String>,
    /// User-chosen glyph rendered in place of the favicon (favorites tiles).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub custom_icon: Option<String>,
    /// Home (pinned) URL for pinned/favorite tabs; views use it for the
    /// "Edit Pinned Page" actions. Omitted when the tab has no home URL.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub pinned_url: Option<String>,
    pub url: String,
    pub favicon: Option<ImageData>,
    pub is_loading: bool,
    pub is_pinned: bool,
    pub is_favorite: bool, // L3-EXEMPT
    #[serde(skip_serializing_if = "Option::is_none")]
    pub favorite_order: Option<u32>, // L3-EXEMPT
    pub is_muted: bool,
    pub is_playing_audio: bool,
    pub lifecycle_state: String,
    pub children: Vec<TabId>,
    pub created_at: DateTime,
    pub last_active_at: DateTime,
    pub role: TabRole,
    #[serde(default)]
    pub is_private: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceViewModel {
    pub id: SpaceId,
    pub name: String,
    pub color: SpaceColor,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub theme: Option<SpaceTheme>,
    pub tab_count: usize,
    pub is_active: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub icon: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub profile_id: Option<ProfileId>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub profile_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub order_index: Option<usize>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderViewModel {
    pub id: FolderId,
    pub name: String,
    pub tab_count: usize,
    pub is_expanded: bool,
    pub tab_ids: Vec<TabId>,
    pub is_pinned: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub parent_folder_id: Option<FolderId>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub provider_type: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config_json: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", tag = "kind")]
pub enum SidebarNode {
    Tab {
        #[serde(flatten)]
        tab: TabViewModel,
    },
    Folder {
        id: FolderId,
        name: String,
        is_expanded: bool,
        is_pinned: bool,
        #[serde(skip_serializing_if = "Option::is_none")]
        parent_folder_id: Option<FolderId>,
        #[serde(skip_serializing_if = "Option::is_none")]
        provider_type: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        config_json: Option<String>,
        children: Vec<SidebarNode>,
    },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TabTreeViewModel {
    pub root_tabs: Vec<TabViewModel>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ArchivedTabViewModel {
    pub id: TabId,
    pub space_id: SpaceId,
    pub title: String,
    pub url: String,
    pub favicon: Option<ImageData>,
    pub archived_at: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DownloadViewModel {
    pub id: String,
    pub filename: String,
    pub url: String,
    pub total_bytes: u64,
    pub received_bytes: u64,
    pub state: DownloadState,
    pub file_path: Option<String>,
    pub mime_type: Option<String>,
    pub error: Option<String>,
    pub started_at: String,
    pub completed_at: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub original_filename: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum DownloadState {
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
#[non_exhaustive]
pub enum SuggestionType {
    Tab,
    Bookmark,
    History,
    Action,
    Navigation,
    Search,
    Calculator,
    UnitConversion,
    ArchivedTab,
    ClosedTab,
    Folder,
    #[serde(rename = "aiAnswer")]
    AiAnswer,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SuggestionViewModel {
    pub kind: SuggestionType,
    pub key: String,
    pub title: String,
    pub subtitle: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub execution_payload: Option<String>,
    pub icon: Option<ImageData>,
    pub relevance_score: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub match_ranges: Option<Vec<(usize, usize)>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub shortcut: Option<String>,
    /// Core-side tab identifier used by the C++ layer to reconcile a switch-to-tab
    /// suggestion with a suspended tab it needs to wake. Serialized as `tabCoreId`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tab_core_id: Option<String>,
    /// True when this suggestion refers to a suspended tab that lives only in the
    /// Rust core and must be woken by the shell. Serialized as `isSuspended`.
    #[serde(default)]
    pub is_suspended: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SearchContext {
    pub tabs: Vec<TabViewModel>,
    pub spaces: Vec<SpaceViewModel>,
    pub folders: Vec<FolderViewModel>,
    pub archived_tabs: Vec<TabViewModel>,
    pub bookmarks: Vec<(String, String, String)>,
    #[serde(default)]
    pub bookmark_favicons: Vec<(String, ImageData)>,
    pub closed_tabs: Vec<TabViewModel>,
    pub extensions: Vec<(String, String)>,
    pub is_incognito: bool,
    #[serde(default)]
    pub recent_tabs: Vec<TabViewModel>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ActionViewModel {
    pub id: String,
    pub label: String,
    pub icon: Option<ImageData>,
    pub shortcut: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TabStripViewModel {
    pub id: TabId,
    pub title: String,
    pub favicon: Option<ImageData>,
    pub is_active: bool,
    pub is_loading: bool,
    pub is_playing_audio: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TabStateUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub custom_title: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub url: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub favicon: Option<Option<ImageData>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub is_loading: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub is_pinned: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub is_muted: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub is_playing_audio: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub is_favorite: Option<bool>, // L3-EXEMPT
    #[serde(skip_serializing_if = "Option::is_none")]
    pub favorite_order: Option<Option<u32>>, // L3-EXEMPT
    #[serde(skip_serializing_if = "Option::is_none")]
    pub lifecycle_state: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub role: Option<TabRole>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct NotificationViewModel {
    pub id: String,
    pub title: String,
    pub message: String,
    pub icon: Option<ImageData>,
    pub actions: Vec<NotificationAction>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct NotificationAction {
    pub id: String,
    pub label: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct PermissionRequest {
    pub id: String,
    pub origin: String,
    pub permission: String,
    pub message: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FindBarState {
    pub query: String,
    pub match_count: u32,
    pub active_index: u32,
    pub is_visible: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoostViewModel {
    pub id: BoostId,
    pub domain: String,
    pub name: String,
    pub custom_css: Option<String>,
    pub enabled: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SettingsViewModel {
    pub sections: Vec<SettingsSection>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceCollectionViewModel {
    pub active_space_model: ActiveSpaceModel,
    pub active_space_id: SpaceId,
    pub spaces: Vec<SpaceViewModel>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SettingsSection {
    pub title: String,
    pub items: Vec<SettingsItem>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SettingsItem {
    pub key: String,
    pub label: String,
    #[serde(rename = "type")]
    pub item_type: SettingsItemType,
    pub value: serde_json::Value,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SettingsItemType {
    Toggle,
    Select,
    Text,
    Number,
    Color,
    Slider,
    KeyCapture,
    UrlPattern,
    DragList,
    PathSelector,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NoteViewModel {
    pub id: String,
    pub content: String,
    pub linked_url: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EaselViewModel {
    pub id: String,
    pub name: String,
    pub item_count: usize,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ExtensionViewModel {
    pub id: String,
    pub name: String,
    pub version: String,
    pub enabled: bool,
    pub icon: Option<ImageData>,
}

// === Renderer Traits ===

pub trait SidebarRenderer {
    fn render_space_list(&self, spaces: &[SpaceViewModel]);
    fn render_tab_tree(&self, space_id: &SpaceId, tabs: &TabTreeViewModel);
    fn render_pinned_tabs(&self, tabs: &[TabViewModel]);
    fn render_favorites(&self, tabs: &[TabViewModel]);
    fn update_tab_state(&self, tab_id: &TabId, state: &TabStateUpdate);
    fn animate_tab_reorder(&self, tab_id: &TabId, from: usize, to: usize);
    fn show_tab_context_menu(&self, tab_id: &TabId, position: &Point);
    fn handle_tab_drag_drop(&self, source: &TabId, target: &DropTarget);
    fn set_sidebar_width(&self, width: f64);
    fn toggle_sidebar(&self, visible: bool, animated: bool);
    fn render_archive(&self, tabs: &[ArchivedTabViewModel]);
    fn render_downloads(&self, items: &[DownloadViewModel]);
}

pub trait CommandBarRenderer {
    fn show_command_bar(&self, animated: bool);
    fn hide_command_bar(&self, animated: bool);
    fn update_suggestions(&self, results: &[SuggestionViewModel]);
    fn render_action_list(&self, actions: &[ActionViewModel]);
    fn set_search_text(&self, text: &str);
    fn highlight_result(&self, index: usize);
    fn animate_appear(&self, config: &SpringConfig);
    fn animate_dismiss(&self, config: &SpringConfig);
}

pub trait TabBarRenderer {
    fn render_tab_strip(&self, tabs: &[TabStripViewModel]);
    fn render_tab_preview(&self, tab_id: &TabId, thumbnail: &ImageData);
    fn update_tab_thumbnail(&self, tab_id: &TabId, thumbnail: &ImageData);
    fn animate_tab_close(&self, tab_id: &TabId);
    fn animate_tab_insert(&self, tab_id: &TabId, index: usize);
    fn render_loading_indicator(&self, tab_id: &TabId, progress: f64);
}

pub trait SplitViewRenderer {
    fn create_split(&self, config: &SplitConfig);
    fn remove_split(&self, pane_id: &PaneId);
    fn resize_split(&self, pane_id: &PaneId, ratio: f64);
    fn render_split_divider(&self, position: f64, orientation: &Orientation);
}

pub trait NotificationRenderer {
    fn show_notification(&self, notification: &NotificationViewModel);
    fn show_permission_request(&self, request: &PermissionRequest);
    fn show_download_progress(&self, download: &DownloadViewModel);
    fn show_find_bar(&self, state: &FindBarState);
}

pub trait FeatureRenderer {
    fn render_boost_editor(&self, boost: &BoostViewModel);
    fn render_settings_panel(&self, settings: &SettingsViewModel);
    fn render_note_editor(&self, note: &NoteViewModel);
    fn render_easel_canvas(&self, easel: &EaselViewModel);
    fn render_about_page(&self);
    fn render_extension_manager(&self, extensions: &[ExtensionViewModel]);
}

pub trait BrowserShellRenderer:
    SidebarRenderer
    + CommandBarRenderer
    + TabBarRenderer
    + SplitViewRenderer
    + NotificationRenderer
    + FeatureRenderer
{
}
