use serde::{Deserialize, Serialize};

use crate::autofill::{AutofillAddress, AutofillPayment};
use crate::boost::BoostUpdate;
use crate::chat::{ChatRequestContext, ChatRequestMode, PageContext};
use crate::common::{ImageData, MemoryPressureLevel, Size, Url};
use crate::content_blocking::ContentBlockingMode;
use crate::identifiers::{BoostId, FolderId, NoteId, ProfileId, SpaceId, TabId, WindowId};
use crate::settings::SettingsUpdate;
use crate::space::{SpaceColor, SpaceConfigUpdate};
use crate::tab::TabRole;

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct MemoryAuthSnapshot {
    pub provider: String,
    pub base_url: String,
    pub api_key: String,
    pub model: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum QuickAction {
    NewTab,
    NewSpace,
    CloseTab,
    CloseOtherTabs,
    CloseAllTabs,
    DuplicateTab,
    PinTab,
    UnpinTab,
    MuteTab,
    UnmuteTab,
    MuteAllTabs,
    FreezeTab,
    UnfreezeTab,
    ReloadTab,
    HardReload,
    CopyUrl,
    ClearHistory,
    ClearCookies,
    OpenSettings,
    OpenDownloads,
    OpenBookmarks,
    OpenHistory,
    ToggleBoost,
    ToggleSidebar,
    ToggleContentBlocker,
    NewFolder,
    ArchiveTab,
    RestoreLastClosed,
    ToggleFullScreen,
    ZoomIn,
    ZoomOut,
    ResetZoom,
    FindInPage,
    PrintPage,
    ViewSource,
    ToggleDevTools,
    NextSpace,
    PrevSpace,
    SharePage,
    NewIncognito,
    ScrollToTop,
    ScrollToBottom,
    ClearBrowsingData,
    Custom { action_id: String },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ShellEvent {
    // Navigation (5)
    NavigateTo {
        tab_id: TabId,
        url: Url,
    },
    GoBack {
        tab_id: TabId,
    },
    GoForward {
        tab_id: TabId,
    },
    Reload {
        tab_id: TabId,
    },
    Stop {
        tab_id: TabId,
    },

    // Tab management (12)
    CreateTab {
        space_id: SpaceId,
        url: Option<Url>,
        parent_id: Option<TabId>,
        tab_id: Option<TabId>,
        /// Browser window session ID that is creating this tab. Used to assign
        /// window ownership for per-window tab isolation.
        #[serde(default)]
        window_id: Option<i64>,
        #[serde(default)]
        is_private: bool,
    },
    CreateSplit {
        window_id: WindowId,
        tab_ids: Vec<TabId>,
        orientation: crate::common::Orientation,
        #[serde(default)]
        layout: Option<crate::split_view::SplitLayoutNode>,
    },
    ClearSplitView {
        window_id: WindowId,
    },
    RemoveSplit {
        window_id: WindowId,
        pane_id: String,
    },
    ResizeSplit {
        window_id: WindowId,
        pane_id: String,
        ratio: f64,
    },
    CloseTab {
        tab_id: TabId,
        #[serde(default)]
        expected_space_id: Option<SpaceId>,
    },
    ActivateTab {
        tab_id: TabId,
    },
    DuplicateTab {
        tab_id: TabId,
    },
    PinTab {
        tab_id: TabId,
    },
    UnpinTab {
        tab_id: TabId,
    },
    FavoriteTab {
        tab_id: TabId,
    },
    ChangeTabRole {
        tab_id: TabId,
        new_role: TabRole,
    },
    ReorderFavorite {
        tab_id: TabId,
        new_index: usize,
    },
    MuteTab {
        tab_id: TabId,
    },
    UnmuteTab {
        tab_id: TabId,
    },
    FreezeTab {
        tab_id: TabId,
    },
    UnfreezeTab {
        tab_id: TabId,
    },
    SuspendTab {
        tab_id: TabId,
    },
    MoveTab {
        tab_id: TabId,
        target_space: SpaceId,
        position: usize,
    },
    MoveTabToSpace {
        tab_id: TabId,
        target_space_id: SpaceId,
        #[serde(default)]
        section: String,
    },
    SetTabParent {
        tab_id: TabId,
        new_parent_id: Option<TabId>,
    },
    SetTabCustomTitle {
        tab_id: TabId,
        custom_title: Option<String>,
    },
    /// Sets or clears the user-chosen glyph rendered in place of the favicon
    /// on favorites tiles / tab rows (empty string clears).
    SetTabCustomIcon {
        tab_id: TabId,
        custom_icon: Option<String>,
    },
    /// Replaces the tab's home (pinned) URL - the "Edit Pinned Page" target.
    SetTabPinnedUrl {
        tab_id: TabId,
        url: Url,
    },
    TabFaviconUpdated {
        tab_id: TabId,
        favicon: Option<ImageData>,
    },
    ReorderTab {
        tab_id: TabId,
        #[serde(default)]
        before_tab_id: Option<TabId>,
    },
    CloseOtherTabs {
        space_id: SpaceId,
        tab_id: TabId,
    },
    CloseTabsToRight {
        space_id: SpaceId,
        tab_id: TabId,
    },
    CloseTabsToLeft {
        space_id: SpaceId,
        tab_id: TabId,
    },
    ReopenLastClosed,
    ArchiveTabById {
        tab_id: TabId,
    },
    RestoreArchivedTab {
        tab_id: TabId,
    },
    DeleteArchivedTab {
        tab_id: TabId,
    },
    ResetPinnedTab {
        tab_id: TabId,
    },

    // Space management (5)
    CreateSpace {
        name: String,
        color: SpaceColor,
        profile_id: ProfileId,
    },
    DeleteSpace {
        space_id: SpaceId,
    },
    ExportSpaceIntoFolder {
        space_id: SpaceId,
        target_space_id: SpaceId,
    },
    ActivateSpace {
        space_id: SpaceId,
    },
    RenameSpace {
        space_id: SpaceId,
        name: String,
    },
    RecolorSpace {
        space_id: SpaceId,
        color: SpaceColor,
    },
    ReorderSpace {
        space_id: SpaceId,
        from: usize,
        to: usize,
    },

    // Folder management (7)
    CreateFolder {
        space_id: SpaceId,
        name: String,
        #[serde(default)]
        is_pinned: bool,
        #[serde(default)]
        parent_folder_id: Option<FolderId>,
        #[serde(default)]
        provider_type: Option<String>,
        #[serde(default)]
        config_json: Option<String>,
    },
    CreateFolderWithTabs {
        space_id: SpaceId,
        name: String,
        tab_ids: Vec<TabId>,
    },
    RenameFolder {
        space_id: SpaceId,
        folder_id: FolderId,
        name: String,
    },
    DeleteFolder {
        space_id: SpaceId,
        folder_id: FolderId,
    },
    ConvertFolderToSpace {
        space_id: SpaceId,
        folder_id: FolderId,
    },
    MoveFolderToSpace {
        source_space_id: SpaceId,
        folder_id: FolderId,
        target_space_id: SpaceId,
    },
    MoveFolderIntoFolder {
        space_id: SpaceId,
        folder_id: FolderId,
        target_folder_id: FolderId,
    },
    MoveFolderToRoot {
        space_id: SpaceId,
        folder_id: FolderId,
        #[serde(default)]
        before_folder_id: Option<FolderId>,
    },
    ReorderFolder {
        space_id: SpaceId,
        folder_id: FolderId,
        #[serde(default)]
        parent_folder_id: Option<FolderId>,
        #[serde(default)]
        before_folder_id: Option<FolderId>,
    },
    ReorderTabInFolder {
        space_id: SpaceId,
        folder_id: FolderId,
        tab_id: TabId,
        to: usize,
    },
    MoveTabToRoot {
        space_id: SpaceId,
        folder_id: FolderId,
        tab_id: TabId,
        #[serde(default)]
        before_tab_id: Option<TabId>,
    },
    MoveTabToFolder {
        space_id: SpaceId,
        folder_id: FolderId,
        tab_id: TabId,
    },
    RemoveTabFromFolder {
        space_id: SpaceId,
        folder_id: FolderId,
        tab_id: TabId,
    },
    ToggleFolderExpanded {
        space_id: SpaceId,
        folder_id: FolderId,
    },
    SetFolderPinned {
        space_id: SpaceId,
        folder_id: FolderId,
        is_pinned: bool,
    },

    // Root ordering (unified mixed-item contract)
    ReorderRootItem {
        space_id: SpaceId,
        item: crate::space::RootItem,
        insertion_point: crate::space::RootInsertionPoint,
    },

    // Command bar (5)
    CommandBarOpened,
    CommandBarClosed,
    CommandBarQuery {
        text: String,
        #[serde(default)]
        mode: Option<String>,
        #[serde(default)]
        is_incognito: bool,
    },
    CommandBarSelect {
        index: usize,
        key: String,
    },
    CommandBarAction {
        action: QuickAction,
    },
    SaveSearch {
        query: String,
    },
    AddSearchEngine {
        engine: crate::search_engine::SearchEngine,
    },
    RemoveSearchEngine {
        id: String,
    },
    SetDefaultSearchEngine {
        id: String,
    },

    // Notification management
    DismissNotification {
        notification_id: String,
    },
    DismissAllNotifications,
    NotificationAction {
        notification_id: String,
        action_id: String,
    },
    SetNotificationFilter {
        origin: String,
        allowed: bool,
    },

    // Boosts (4)
    CreateBoost {
        domain: String,
    },
    UpdateBoost {
        boost_id: BoostId,
        changes: BoostUpdate,
    },
    SetActiveBoost {
        domain: String,
        boost_id: Option<BoostId>,
    },
    DeleteBoost {
        boost_id: BoostId,
    },

    // CSS Mods (3)
    InstallCssMod {
        name: String,
        css: String,
        description: Option<String>,
        author: Option<String>,
        version: Option<String>,
        homepage: Option<String>,
        source_url: Option<String>,
    },
    ToggleCssMod {
        mod_id: String,
    },
    UninstallCssMod {
        mod_id: String,
    },
    UpdateCssMod {
        mod_id: String,
        css: String,
    },

    // Content Blocker
    SetContentBlockingMode {
        mode: ContentBlockingMode,
    },
    ToggleContentBlocker {
        enabled: bool,
    },
    TogglePopupBlocking {
        enabled: bool,
    },
    AddFilterList {
        id: String,
        name: String,
        url: String,
    },
    RemoveFilterList {
        id: String,
    },
    ToggleFilterList {
        id: String,
        enabled: bool,
    },
    AddSiteException {
        exception: String,
    },
    RemoveSiteException {
        exception: String,
    },
    TriggerFilterUpdate {
        list_id: Option<String>,
    },

    // Notes (3)
    CreateNote {
        linked_tab: Option<TabId>,
        content: String,
    },
    UpdateNote {
        note_id: NoteId,
        content: String,
    },
    DeleteNote {
        note_id: NoteId,
    },

    // Settings (1)
    UpdateSettings {
        changes: SettingsUpdate,
    },

    // Window/sidebar (4)
    SidebarToggled {
        visible: bool,
    },
    SidebarResized {
        width: f64,
    },
    WindowResized {
        size: Size,
    },
    WindowFocusChanged {
        focused: bool,
    },

    // Tab lifecycle from webview (4)
    TabTitleUpdated {
        tab_id: TabId,
        title: String,
    },
    TabUrlUpdated {
        tab_id: TabId,
        url: Url,
    },
    TabLoadingChanged {
        tab_id: TabId,
        is_loading: bool,
    },
    TabNavigationStateChanged {
        tab_id: TabId,
        can_go_back: bool,
        can_go_forward: bool,
    },
    TabSecurityChanged {
        tab_id: TabId,
        is_secure: bool,
    },
    UpdateTabScrollPosition {
        tab_id: TabId,
        x: f64,
        y: f64,
    },

    // History/Bookmarks/Permissions/Zoom/Download/Print/PiP/DevTools/Reader (8)
    SearchHistory {
        query: String,
        limit: usize,
        #[serde(default)]
        is_incognito: bool,
    },
    ClearHistory,
    DeleteHistoryEntry {
        entry_id: String,
    },
    AddBookmark {
        url: String,
        title: String,
        folder_id: Option<String>,
    },
    RemoveBookmark {
        bookmark_id: String,
    },
    MoveBookmark {
        bookmark_id: String,
        folder_id: Option<String>,
    },
    SearchBookmarks {
        query: String,
    },
    GrantPermission {
        origin: String,
        permission: String,
    },
    RevokePermission {
        origin: String,
        permission: String,
    },
    QueryPermission {
        origin: String,
        permission: String,
    },
    SetZoom {
        tab_id: TabId,
        zoom_level: f64,
    },
    ResetZoom {
        tab_id: TabId,
    },
    PauseDownload {
        download_id: String,
    },
    ResumeDownload {
        download_id: String,
    },
    CancelDownload {
        download_id: String,
    },
    RemoveDownload {
        download_id: String,
    },
    RestoreDownloadName {
        download_id: String,
    },
    TogglePiP {
        tab_id: TabId,
    },
    ToggleDevTools {
        tab_id: TabId,
    },
    PrintPage {
        tab_id: TabId,
    },
    ViewSource {
        tab_id: TabId,
    },

    // App lifecycle / boosts / extensions (8)
    AppLaunched,
    AppWillTerminate,
    MemoryWarning {
        level: MemoryPressureLevel,
    },
    ToggleExtension {
        extension_id: String,
    },
    RemoveExtension {
        extension_id: String,
    },

    // Account & Sync (3)
    SignIn {
        email: String,
        password: String,
        #[serde(default)]
        display_name: Option<String>,
        #[serde(default)]
        access_token: Option<String>,
        #[serde(default)]
        user_id: Option<String>,
        #[serde(default)]
        device_id: Option<String>,
    },
    SignOut,
    ToggleSync,

    // Air traffic control (4)
    CreateTrafficRule {
        rule: crate::air_traffic::TrafficRule,
    },
    DeleteTrafficRule {
        rule_id: String,
    },
    UpdateTrafficRule {
        rule: crate::air_traffic::TrafficRule,
    },
    SetDefaultLinkBehavior {
        behavior: crate::air_traffic::DefaultLinkBehavior,
    },

    // Profile management (4)
    UpdateSpaceConfig {
        changes: SpaceConfigUpdate,
    },
    CreateProfile {
        name: String,
    },
    DeleteProfile {
        profile_id: ProfileId,
    },
    UpdateProfile {
        profile_id: ProfileId,
        name: Option<String>,
        avatar_color: Option<String>,
        download_path: Option<String>,
        archive_timeout_hours: Option<u32>,
    },
    SwitchProfile {
        profile_id: ProfileId,
    },

    // Passwords & Autofill (6)
    SearchPasswords {
        query: String,
    },
    DeletePassword {
        password_id: String,
    },
    AddPassword {
        domain: String,
        username: String,
        password: String,
    },
    AddAutofillAddress {
        address: AutofillAddress,
    },
    DeleteAutofillAddress {
        id: String,
    },
    AddAutofillPayment {
        payment: AutofillPayment,
    },
    DeleteAutofillPayment {
        id: String,
    },

    // Settings reset (1)
    ResetSettings,

    // Reading list (4)
    AddToReadingList {
        url: String,
        title: String,
    },
    RemoveFromReadingList {
        item_id: String,
    },
    MarkReadingListItemRead {
        item_id: String,
    },
    MarkReadingListItemUnread {
        item_id: String,
    },

    // LLM / AI helpers (7)
    LlmResult {
        request_id: String,
        result: String,
    },
    LlmError {
        request_id: String,
        error: String,
    },
    SetMemoryAuth {
        auth: Option<MemoryAuthSnapshot>,
    },
    RequestPagePreview {
        url: String,
    },
    RequestTidyTabs {
        space_id: SpaceId,
    },
    ApplyTidyTabs {
        space_id: SpaceId,
    },
    ChatRequestModeChanged {
        mode: ChatRequestMode,
    },
    ChatMessage {
        message: String,
        #[serde(default)]
        context: ChatRequestContext,
    },
    ToolPermissionResponse {
        call_id: String,
        granted: bool,
    },
    ToolActionResult {
        call_id: String,
        success: bool,
        result: String,
    },
    RequestSkillsList,
    UseSkill {
        skill_id: String,
        user_input: String,
        #[serde(default)]
        page_context: Option<PageContext>,
    },
    ChatSessionEnded {
        session_id: String,
    },
}

impl ShellEvent {
    pub fn is_navigation_event(&self) -> bool {
        matches!(
            self,
            Self::NavigateTo { .. }
                | Self::GoBack { .. }
                | Self::GoForward { .. }
                | Self::Reload { .. }
                | Self::Stop { .. }
        )
    }

    pub fn is_tab_management_event(&self) -> bool {
        matches!(
            self,
            Self::CreateTab { .. }
                | Self::CreateSplit { .. }
                | Self::CloseTab { .. }
                | Self::ActivateTab { .. }
                | Self::DuplicateTab { .. }
                | Self::PinTab { .. }
                | Self::UnpinTab { .. }
                | Self::FavoriteTab { .. }
                | Self::ChangeTabRole { .. }
                | Self::ReorderFavorite { .. }
                | Self::MuteTab { .. }
                | Self::UnmuteTab { .. }
                | Self::FreezeTab { .. }
                | Self::UnfreezeTab { .. }
                | Self::SuspendTab { .. }
                | Self::MoveTab { .. }
                | Self::MoveTabToSpace { .. }
                | Self::SetTabParent { .. }
                | Self::SetTabCustomTitle { .. }
                | Self::SetTabCustomIcon { .. }
                | Self::SetTabPinnedUrl { .. }
                | Self::TabFaviconUpdated { .. }
                | Self::ReorderTab { .. }
                | Self::CloseOtherTabs { .. }
                | Self::CloseTabsToRight { .. }
                | Self::CloseTabsToLeft { .. }
                | Self::ReopenLastClosed
                | Self::RestoreArchivedTab { .. }
                | Self::DeleteArchivedTab { .. }
                | Self::ResetPinnedTab { .. }
        )
    }

    pub fn is_space_management_event(&self) -> bool {
        matches!(
            self,
            Self::CreateSpace { .. }
                | Self::DeleteSpace { .. }
                | Self::ExportSpaceIntoFolder { .. }
                | Self::ActivateSpace { .. }
                | Self::RenameSpace { .. }
                | Self::RecolorSpace { .. }
                | Self::ReorderSpace { .. }
        )
    }

    pub fn is_folder_management_event(&self) -> bool {
        matches!(
            self,
            Self::CreateFolder { .. }
                | Self::RenameFolder { .. }
                | Self::DeleteFolder { .. }
                | Self::ConvertFolderToSpace { .. }
                | Self::MoveFolderToSpace { .. }
                | Self::MoveFolderIntoFolder { .. }
                | Self::MoveFolderToRoot { .. }
                | Self::ReorderFolder { .. }
                | Self::ReorderTabInFolder { .. }
                | Self::MoveTabToRoot { .. }
                | Self::MoveTabToFolder { .. }
                | Self::RemoveTabFromFolder { .. }
                | Self::ToggleFolderExpanded { .. }
                | Self::SetFolderPinned { .. }
                | Self::ReorderRootItem { .. }
        )
    }

    pub fn is_command_bar_event(&self) -> bool {
        matches!(
            self,
            Self::CommandBarOpened
                | Self::CommandBarClosed
                | Self::CommandBarQuery { .. }
                | Self::CommandBarSelect { .. }
                | Self::CommandBarAction { .. }
                | Self::SaveSearch { .. }
                | Self::AddSearchEngine { .. }
                | Self::RemoveSearchEngine { .. }
                | Self::SetDefaultSearchEngine { .. }
        )
    }

    pub fn is_notification_event(&self) -> bool {
        matches!(
            self,
            Self::DismissNotification { .. }
                | Self::DismissAllNotifications
                | Self::NotificationAction { .. }
                | Self::SetNotificationFilter { .. }
        )
    }

    pub fn is_history_event(&self) -> bool {
        matches!(
            self,
            Self::SearchHistory { .. } | Self::ClearHistory | Self::DeleteHistoryEntry { .. }
        )
    }

    pub fn is_bookmark_event(&self) -> bool {
        matches!(
            self,
            Self::AddBookmark { .. }
                | Self::RemoveBookmark { .. }
                | Self::MoveBookmark { .. }
                | Self::SearchBookmarks { .. }
        )
    }

    pub fn is_permission_event(&self) -> bool {
        matches!(
            self,
            Self::GrantPermission { .. }
                | Self::RevokePermission { .. }
                | Self::QueryPermission { .. }
        )
    }

    pub fn is_zoom_event(&self) -> bool {
        matches!(self, Self::SetZoom { .. } | Self::ResetZoom { .. })
    }

    pub fn is_download_event(&self) -> bool {
        matches!(
            self,
            Self::PauseDownload { .. }
                | Self::ResumeDownload { .. }
                | Self::CancelDownload { .. }
                | Self::RemoveDownload { .. }
                | Self::RestoreDownloadName { .. }
        )
    }

    pub fn is_browsing_event(&self) -> bool {
        matches!(
            self,
            Self::TabTitleUpdated { .. }
                | Self::TabUrlUpdated { .. }
                | Self::TabLoadingChanged { .. }
                | Self::TabNavigationStateChanged { .. }
                | Self::TabSecurityChanged { .. }
                | Self::UpdateTabScrollPosition { .. }
        )
    }

    pub fn is_lifecycle_event(&self) -> bool {
        matches!(
            self,
            Self::AppLaunched | Self::AppWillTerminate | Self::MemoryWarning { .. }
        )
    }
}
