use serde::{Deserialize, Serialize};
use std::collections::HashMap;

use crate::autofill::{AutofillAddress, AutofillPayment};
use crate::boost::Boost;
use crate::chat::ChatRequestContext;
use crate::common::{ImageData, Url};
use crate::content_blocking::ContentBlockerStateChange;
use crate::identifiers::{BoostId, DownloadId, FolderId, ProfileId, SpaceId, TabId, WindowId};
use crate::passwords::SavedPassword;
use crate::profile::ProfileConfig;
use crate::search_engine::SearchEngineViewModel;
use crate::settings::Settings;
use crate::space::{ActiveSpaceModel, SpaceColor, SpaceConfigUpdate};
use crate::split_view::SplitViewConfig;
use crate::tab::{TabLifecycleState, TabRole};
use crate::traits::shell_renderer::{
    DownloadViewModel, FolderViewModel, NotificationViewModel, PermissionRequest, SpaceViewModel,
    SuggestionViewModel, TabStateUpdate, TabViewModel,
};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub color: Option<SpaceColor>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tab_count: Option<usize>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub is_active: Option<bool>,
    /// `Some(Some("🌐"))` = set icon, `Some(None)` = clear icon, `None` = unchanged
    #[serde(skip_serializing_if = "Option::is_none")]
    pub icon: Option<Option<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub profile_id: Option<ProfileId>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub profile_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub order_index: Option<usize>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub is_expanded: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub is_pinned: Option<bool>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum SyncStatus {
    Idle,
    Syncing { progress: f64 },
    Synced { last_sync_at: String },
    Error { message: String },
    Offline,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum MemoryAction {
    FrozeTabs { count: usize },
    SuspendedTabs { count: usize },
    KilledTabs { count: usize },
    ReleasedWebviews { count: usize },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct MahoError {
    pub code: String,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub details: Option<serde_json::Value>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppStateSnapshot {
    pub active_space_model: ActiveSpaceModel,
    pub active_space_id: SpaceId,
    pub spaces: Vec<SpaceViewModel>,
    pub tabs: HashMap<SpaceId, Vec<TabViewModel>>,
    pub sync_status: SyncStatus,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HistoryEntry {
    pub id: String,
    pub url: String,
    pub title: String,
    pub visited_at: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BookmarkEntry {
    pub id: String,
    pub url: String,
    pub title: String,
    pub folder_id: Option<String>,
    pub created_at: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum LLMRequestType {
    TidyTabTitle,
    TidyDownload,
    TidyTabs,
    PagePreview,
    ChatCompletion,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum LLMRequestContext {
    TidyTabTitle { tab_id: TabId },
    TidyDownload { download_id: DownloadId },
    PagePreview { url: Url },
    TidyTabs { space_id: SpaceId },
    Chat { chat: Box<ChatRequestContext> },
}

pub const SPLIT_VIEW_SCHEMA_VERSION: u32 = 1;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum CoreUpdate {
    FullState {
        state: AppStateSnapshot,
    },
    TabCreated {
        tab: TabViewModel,
    },
    TabUpdated {
        tab_id: TabId,
        changes: TabStateUpdate,
    },
    TabClosed {
        tab_id: TabId,
        animated: bool,
    },
    TabOrderChanged {
        space_id: SpaceId,
        order: Vec<TabId>,
    },
    SpaceCreated {
        space: SpaceViewModel,
    },
    SpaceUpdated {
        space_id: SpaceId,
        changes: SpaceUpdate,
    },
    SpaceDeleted {
        space_id: SpaceId,
    },
    SpaceOrderChanged {
        order: Vec<SpaceId>,
    },
    FolderCreated {
        folder: FolderViewModel,
    },
    FolderUpdated {
        folder_id: FolderId,
        changes: FolderUpdate,
    },
    FolderDeleted {
        folder_id: FolderId,
    },
    FolderOrderChanged {
        space_id: SpaceId,
    },
    ActiveSpaceChanged {
        space_id: SpaceId,
        active_tab_id: Option<TabId>,
    },
    CommandBarResults {
        suggestions: Vec<SuggestionViewModel>,
    },
    RecentSearchesUpdated {
        searches: Vec<String>,
    },
    SearchEnginesUpdated {
        engines: Vec<SearchEngineViewModel>,
    },
    NavigationStateChanged {
        tab_id: TabId,
        url: Url,
        title: String,
        can_go_back: bool,
        can_go_forward: bool,
        is_loading: bool,
        progress: f64,
        is_secure: bool,
    },
    TabAudioStateChanged {
        tab_id: TabId,
        is_playing: bool,
    },
    TabFaviconChanged {
        tab_id: TabId,
        favicon: Option<ImageData>,
    },
    DownloadStarted {
        download: DownloadViewModel,
    },
    DownloadProgress {
        download_id: DownloadId,
        progress: f64,
    },
    DownloadCompleted {
        download_id: DownloadId,
    },
    DownloadRenamed {
        download_id: DownloadId,
        old_name: String,
        new_name: String,
    },
    ShowNotification {
        notification: NotificationViewModel,
    },
    NotificationDismissed {
        notification_id: String,
    },
    AllNotificationsDismissed,
    NotificationList {
        notifications: Vec<NotificationViewModel>,
    },
    ShowPermissionRequest {
        request: PermissionRequest,
    },
    ShowFindBar {
        tab_id: TabId,
    },
    HistoryResults {
        entries: Vec<HistoryEntry>,
    },
    BookmarkResults {
        bookmarks: Vec<BookmarkEntry>,
    },
    ZoomChanged {
        tab_id: TabId,
        zoom_level: f64,
    },
    TabPreviewUpdated {
        tab_id: TabId,
        preview_data: String,
    },
    TabPreviewCaptureRequested {
        tab_id: TabId,
    },
    PermissionResponse {
        origin: String,
        permission: String,
        granted: bool,
    },
    PrintRequested {
        tab_id: TabId,
    },
    PipToggled {
        tab_id: TabId,
        active: bool,
    },
    ContentRulesCompiled {
        rule_count: usize,
    },
    ContentBlockerStateChanged(ContentBlockerStateChange),
    FilterListUpdated {
        id: String,
        rule_count: usize,
    },
    TabLifecycleChanged {
        tab_id: TabId,
        state: TabLifecycleState,
    },
    MemoryPressureResponse {
        action: MemoryAction,
    },
    SyncStateChanged {
        status: SyncStatus,
    },
    SettingsChanged {
        settings: Settings,
    },
    SpaceConfigUpdated {
        space_id: SpaceId,
        changes: SpaceConfigUpdate,
    },
    ExtensionToggled {
        extension_id: String,
        enabled: bool,
    },
    ExtensionRemoved {
        extension_id: String,
    },
    InstalledExtensionsUpdated,
    AccountChanged {
        account: Option<crate::account::AccountInfo>,
    },
    AccountAuthStateChanged {
        auth_state: crate::account::AuthState,
    },
    AccountTokenRefreshed {
        account: Option<crate::account::AccountInfo>,
    },
    AccountAuthRequired {
        reason: String,
    },
    AccountAuthError {
        message: String,
    },
    AccountSyncStateChanged {
        state: crate::account::SyncState,
    },
    TrafficRuleCreated {
        rule: crate::air_traffic::TrafficRule,
    },
    TrafficRuleDeleted {
        rule_id: String,
    },
    ProfileCreated {
        profile: ProfileConfig,
    },
    ProfileDeleted {
        profile_id: ProfileId,
        data_store_id: Option<String>,
    },
    ProfileUpdated {
        profile: ProfileConfig,
    },
    ActiveProfileChanged {
        profile_id: ProfileId,
    },
    Error {
        context: String,
        error: MahoError,
    },
    PasswordDeleted {
        password_id: String,
    },
    PasswordsSearchResult {
        passwords: Vec<SavedPassword>,
    },
    AutofillAddressAdded {
        address: AutofillAddress,
    },
    AutofillAddressDeleted {
        id: String,
    },
    AutofillPaymentAdded {
        payment: AutofillPayment,
    },
    AutofillPaymentDeleted {
        id: String,
    },
    OpenPeekTab {
        url: Url,
        source_tab_id: TabId,
    },
    FavoriteLimitReached {
        max: usize,
    },
    NavigateTab {
        tab_id: TabId,
        url: Url,
    },
    TabsMigrated {
        tab_ids: Vec<TabId>,
        from_space_id: SpaceId,
        to_space_id: SpaceId,
    },
    RequestLlmCompletion {
        request_id: String,
        request_type: LLMRequestType,
        prompt: String,
        context: LLMRequestContext,
    },
    PagePreviewReady {
        url: Url,
        title: String,
        summary: String,
    },
    TidyTabsReady {
        space_id: SpaceId,
        folders: Vec<TidyTabFolder>,
    },
    SkillsListReady {
        skills: Vec<SkillInfo>,
    },
    ChatCompletionReady {
        request_id: String,
        response: String,
    },
    SkillPromptReady {
        skill_id: String,
        skill_name: String,
        system_prompt: String,
    },
    RequestToolPermission {
        call_id: String,
        tool_name: String,
        description: String,
        args_summary: String,
    },
    ExecuteToolAction {
        call_id: String,
        tool_name: String,
        args: String,
    },
    BoostCreated {
        boost: Boost,
    },
    BoostUpdated {
        boost_id: BoostId,
        boost: Boost,
    },
    BoostActiveChanged {
        domain: String,
        active_boost_id: Option<BoostId>,
    },
    BoostDeleted {
        boost_id: BoostId,
        domain: String,
    },
    MemoryFactsExtracted {
        session_id: String,
        fact_count: usize,
    },
    MemoryUpdated {
        changed_ids: Vec<String>,
    },
    TabRoleChanged {
        tab_id: TabId,
        old_role: Option<TabRole>,
        new_role: TabRole,
    },
    SplitViewChanged {
        window_id: WindowId,
        config: Option<SplitViewConfig>,
        schema_version: u32,
    },
    ToolAvailabilityChanged {
        workspace_id: String,
        server_name: String,
        available: bool,
    },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TidyTabFolder {
    pub name: String,
    pub tab_ids: Vec<TabId>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SkillInfo {
    pub id: String,
    pub name: String,
    pub slash_command: String,
    pub description: String,
    pub icon: Option<String>,
}

impl CoreUpdate {
    pub fn is_tab_update(&self) -> bool {
        matches!(
            self,
            Self::TabCreated { .. }
                | Self::TabUpdated { .. }
                | Self::TabClosed { .. }
                | Self::TabOrderChanged { .. }
                | Self::NavigationStateChanged { .. }
                | Self::TabAudioStateChanged { .. }
                | Self::TabFaviconChanged { .. }
                | Self::TabLifecycleChanged { .. }
                | Self::TabPreviewUpdated { .. }
                | Self::TabPreviewCaptureRequested { .. }
                | Self::TabRoleChanged { .. }
        )
    }

    pub fn is_space_update(&self) -> bool {
        matches!(
            self,
            Self::SpaceCreated { .. }
                | Self::SpaceUpdated { .. }
                | Self::SpaceDeleted { .. }
                | Self::SpaceOrderChanged { .. }
                | Self::ActiveSpaceChanged { .. }
                | Self::SpaceConfigUpdated { .. }
                | Self::TabsMigrated { .. }
        )
    }

    pub fn is_download_update(&self) -> bool {
        matches!(
            self,
            Self::DownloadStarted { .. }
                | Self::DownloadProgress { .. }
                | Self::DownloadCompleted { .. }
                | Self::DownloadRenamed { .. }
        )
    }

    pub fn is_settings_update(&self) -> bool {
        matches!(
            self,
            Self::SettingsChanged { .. }
                | Self::ContentBlockerStateChanged(..)
                | Self::FilterListUpdated { .. }
                | Self::ExtensionToggled { .. }
                | Self::ExtensionRemoved { .. }
                | Self::InstalledExtensionsUpdated
                | Self::AccountChanged { .. }
                | Self::AccountAuthStateChanged { .. }
                | Self::AccountTokenRefreshed { .. }
                | Self::AccountAuthRequired { .. }
                | Self::AccountAuthError { .. }
                | Self::AccountSyncStateChanged { .. }
        )
    }

    pub fn is_profile_update(&self) -> bool {
        matches!(
            self,
            Self::ProfileCreated { .. }
                | Self::ProfileUpdated { .. }
                | Self::ProfileDeleted { .. }
                | Self::ActiveProfileChanged { .. }
        )
    }
}

use std::sync::{Mutex, OnceLock};

pub type CoreUpdateCallback = dyn Fn(CoreUpdate) + Send + Sync + 'static;
static CORE_UPDATE_CALLBACK: OnceLock<Mutex<Option<Box<CoreUpdateCallback>>>> = OnceLock::new();

pub fn set_core_update_callback<F>(callback: F)
where
    F: Fn(CoreUpdate) + Send + Sync + 'static,
{
    let mut lock = CORE_UPDATE_CALLBACK
        .get_or_init(|| Mutex::new(None))
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner());
    *lock = Some(Box::new(callback));
}

pub fn emit_core_update(update: CoreUpdate) {
    if let Some(lock) = CORE_UPDATE_CALLBACK.get() {
        if let Ok(guard) = lock.lock() {
            if let Some(ref cb) = *guard {
                cb(update);
            }
        }
    }
}
