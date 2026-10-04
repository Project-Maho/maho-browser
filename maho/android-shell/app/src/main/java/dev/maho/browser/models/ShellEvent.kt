@file:OptIn(ExperimentalSerializationApi::class)

package dev.maho.browser.models

import dev.maho.browser.support.Patchable
import kotlinx.serialization.EncodeDefault
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
sealed class QuickAction {
    @Serializable @SerialName("new_tab") data object NewTab : QuickAction()
    @Serializable @SerialName("new_space") data object NewSpace : QuickAction()
    @Serializable @SerialName("close_tab") data object CloseTab : QuickAction()
    @Serializable @SerialName("close_other_tabs") data object CloseOtherTabs : QuickAction()
    @Serializable @SerialName("close_all_tabs") data object CloseAllTabs : QuickAction()
    @Serializable @SerialName("duplicate_tab") data object DuplicateTab : QuickAction()
    @Serializable @SerialName("pin_tab") data object PinTab : QuickAction()
    @Serializable @SerialName("unpin_tab") data object UnpinTab : QuickAction()
    @Serializable @SerialName("mute_tab") data object MuteTab : QuickAction()
    @Serializable @SerialName("unmute_tab") data object UnmuteTab : QuickAction()
    @Serializable @SerialName("mute_all_tabs") data object MuteAllTabs : QuickAction()
    @Serializable @SerialName("freeze_tab") data object FreezeTab : QuickAction()
    @Serializable @SerialName("unfreeze_tab") data object UnfreezeTab : QuickAction()
    @Serializable @SerialName("reload_tab") data object ReloadTab : QuickAction()
    @Serializable @SerialName("hard_reload") data object HardReload : QuickAction()
    @Serializable @SerialName("copy_url") data object CopyUrl : QuickAction()
    @Serializable @SerialName("clear_history") data object ClearHistory : QuickAction()
    @Serializable @SerialName("clear_cookies") data object ClearCookies : QuickAction()
    @Serializable @SerialName("open_settings") data object OpenSettings : QuickAction()
    @Serializable @SerialName("open_downloads") data object OpenDownloads : QuickAction()
    @Serializable @SerialName("open_bookmarks") data object OpenBookmarks : QuickAction()
    @Serializable @SerialName("open_history") data object OpenHistory : QuickAction()
    @Serializable @SerialName("toggle_boost") data object ToggleBoost : QuickAction()
    @Serializable @SerialName("toggle_sidebar") data object ToggleSidebar : QuickAction()
    @Serializable @SerialName("toggle_content_blocker") data object ToggleContentBlocker : QuickAction()
    @Serializable @SerialName("new_folder") data object NewFolder : QuickAction()
    @Serializable @SerialName("archive_tab") data object ArchiveTab : QuickAction()
    @Serializable @SerialName("restore_last_closed") data object RestoreLastClosed : QuickAction()
    @Serializable @SerialName("toggle_full_screen") data object ToggleFullScreen : QuickAction()
    @Serializable @SerialName("zoom_in") data object ZoomIn : QuickAction()
    @Serializable @SerialName("zoom_out") data object ZoomOut : QuickAction()
    @Serializable @SerialName("reset_zoom") data object ResetZoom : QuickAction()
    @Serializable @SerialName("find_in_page") data object FindInPage : QuickAction()
    @Serializable @SerialName("print_page") data object PrintPage : QuickAction()
    @Serializable @SerialName("view_source") data object ViewSource : QuickAction()
    @Serializable @SerialName("toggle_dev_tools") data object ToggleDevTools : QuickAction()
    @Serializable @SerialName("next_space") data object NextSpace : QuickAction()
    @Serializable @SerialName("prev_space") data object PrevSpace : QuickAction()
    @Serializable @SerialName("custom") data class Custom(@SerialName("action_id") val actionId: String) : QuickAction()
}

@Serializable
sealed class ShellEvent {

    // Navigation
    @Serializable @SerialName("navigate_to")
    data class NavigateTo(
        @SerialName("tab_id") val tabId: TabId,
        val url: Url,
    ) : ShellEvent()

    @Serializable @SerialName("go_back")
    data class GoBack(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("go_forward")
    data class GoForward(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("reload")
    data class Reload(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("stop")
    data class Stop(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    // Tab management
    @Serializable @SerialName("create_tab")
    data class CreateTab(
        @SerialName("space_id") val spaceId: SpaceId,
        val url: Url? = null,
        @SerialName("parent_id") val parentId: TabId? = null,
    ) : ShellEvent()

    @Serializable @SerialName("close_tab")
    data class CloseTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("activate_tab")
    data class ActivateTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("duplicate_tab")
    data class DuplicateTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("pin_tab")
    data class PinTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("unpin_tab")
    data class UnpinTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("favorite_tab")
    data class FavoriteTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("change_tab_role")
    data class ChangeTabRole(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("new_role") val newRole: TabRole
    ) : ShellEvent()

    @Serializable @SerialName("reorder_favorite")
    data class ReorderFavorite(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("new_index") val newIndex: Int
    ) : ShellEvent()

    @Serializable @SerialName("mute_tab")
    data class MuteTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("unmute_tab")
    data class UnmuteTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("freeze_tab")
    data class FreezeTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("move_tab")
    data class MoveTab(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("target_space") val targetSpace: SpaceId,
        val position: Int,
    ) : ShellEvent()

    @Serializable @SerialName("set_tab_parent")
    data class SetTabParent(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("new_parent_id") val newParentId: TabId? = null,
    ) : ShellEvent()

    @Serializable @SerialName("reorder_tab")
    data class ReorderTab(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("before_tab_id") val beforeTabId: TabId? = null,
    ) : ShellEvent()

    @Serializable @SerialName("close_other_tabs")
    data class CloseOtherTabs(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("tab_id") val tabId: TabId,
    ) : ShellEvent()

    @Serializable @SerialName("close_tabs_to_right")
    data class CloseTabsToRight(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("tab_id") val tabId: TabId,
    ) : ShellEvent()

    @Serializable @SerialName("close_tabs_to_left")
    data class CloseTabsToLeft(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("tab_id") val tabId: TabId,
    ) : ShellEvent()

    @Serializable @SerialName("reopen_last_closed")
    data object ReopenLastClosed : ShellEvent()

    @Serializable @SerialName("archive_tab_by_id")
    data class ArchiveTabById(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("restore_archived_tab")
    data class RestoreArchivedTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("reset_pinned_tab")
    data class ResetPinnedTab(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    // Space management
    @Serializable @SerialName("create_space")
    data class CreateSpace(
        val name: String,
        val color: SpaceColor,
        @SerialName("profile_id") val profileId: ProfileId,
    ) : ShellEvent()

    @Serializable @SerialName("delete_space")
    data class DeleteSpace(@SerialName("space_id") val spaceId: SpaceId) : ShellEvent()

    @Serializable @SerialName("activate_space")
    data class ActivateSpace(@SerialName("space_id") val spaceId: SpaceId) : ShellEvent()

    @Serializable @SerialName("rename_space")
    data class RenameSpace(
        @SerialName("space_id") val spaceId: SpaceId,
        val name: String,
    ) : ShellEvent()

    @Serializable @SerialName("recolor_space")
    data class RecolorSpace(
        @SerialName("space_id") val spaceId: SpaceId,
        val color: SpaceColor,
    ) : ShellEvent()

    @Serializable @SerialName("reorder_space")
    data class ReorderSpace(
        @SerialName("space_id") val spaceId: SpaceId,
        val from: Int,
        val to: Int,
    ) : ShellEvent()

    // Folder management
    @Serializable @SerialName("create_folder")
    data class CreateFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        val name: String,
    ) : ShellEvent()

    @Serializable @SerialName("rename_folder")
    data class RenameFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
        val name: String,
    ) : ShellEvent()

    @Serializable @SerialName("delete_folder")
    data class DeleteFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
    ) : ShellEvent()

    @Serializable @SerialName("move_folder_into_folder")
    data class MoveFolderIntoFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
        @SerialName("target_folder_id") val targetFolderId: FolderId,
    ) : ShellEvent()

    @Serializable @SerialName("reorder_folder")
    data class ReorderFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
        val from: Int,
        val to: Int,
    ) : ShellEvent()

    @Serializable @SerialName("move_tab_to_folder")
    data class MoveTabToFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
        @SerialName("tab_id") val tabId: TabId,
    ) : ShellEvent()

    @Serializable @SerialName("remove_tab_from_folder")
    data class RemoveTabFromFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
        @SerialName("tab_id") val tabId: TabId,
    ) : ShellEvent()

    @Serializable @SerialName("toggle_folder_expanded")
    data class ToggleFolderExpanded(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
    ) : ShellEvent()

    @Serializable @SerialName("pin_folder")
    data class PinFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
    ) : ShellEvent()

    @Serializable @SerialName("unpin_folder")
    data class UnpinFolder(
        @SerialName("space_id") val spaceId: SpaceId,
        @SerialName("folder_id") val folderId: FolderId,
    ) : ShellEvent()

    // Command bar
    @Serializable @SerialName("command_bar_opened")
    data object CommandBarOpened : ShellEvent()

    @Serializable @SerialName("command_bar_closed")
    data object CommandBarClosed : ShellEvent()

    @Serializable @SerialName("command_bar_query")
    data class CommandBarQuery(
        val text: String,
        val mode: String? = null,
        @SerialName("is_incognito") val isIncognito: Boolean = false,
    ) : ShellEvent()

    @Serializable @SerialName("command_bar_select")
    data class CommandBarSelect(val index: Int, val key: String) : ShellEvent()

    @Serializable @SerialName("command_bar_action")
    data class CommandBarAction(val action: QuickAction) : ShellEvent()

    @Serializable @SerialName("save_search")
    data class SaveSearch(val query: String) : ShellEvent()

    @Serializable @SerialName("add_search_engine")
    data class AddSearchEngine(val engine: SearchEngine) : ShellEvent()

    @Serializable @SerialName("remove_search_engine")
    data class RemoveSearchEngine(val id: String) : ShellEvent()

    @Serializable @SerialName("set_default_search_engine")
    data class SetDefaultSearchEngine(val id: String) : ShellEvent()

    // Notification management
    @Serializable @SerialName("dismiss_notification")
    data class DismissNotification(@SerialName("notification_id") val notificationId: String) : ShellEvent()

    @Serializable @SerialName("dismiss_all_notifications")
    data object DismissAllNotifications : ShellEvent()

    @Serializable @SerialName("notification_action")
    data class NotificationActionEvent(
        @SerialName("notification_id") val notificationId: String,
        @SerialName("action_id") val actionId: String,
    ) : ShellEvent()

    @Serializable @SerialName("set_notification_filter")
    data class SetNotificationFilter(
        val origin: String,
        val allowed: Boolean,
    ) : ShellEvent()

    // Split view
    @Serializable @SerialName("create_split")
    data class CreateSplit(
        @SerialName("tab_ids") val tabIds: List<TabId>,
        val orientation: Orientation,
    ) : ShellEvent()

    @Serializable @SerialName("remove_split")
    data class RemoveSplit(@SerialName("pane_id") val paneId: PaneId) : ShellEvent()

    @Serializable @SerialName("resize_split")
    data class ResizeSplit(
        @SerialName("pane_id") val paneId: PaneId,
        val ratio: Double,
    ) : ShellEvent()

    // Boosts
    @Serializable @SerialName("create_boost")
    data class CreateBoost(val domain: String) : ShellEvent()

    @Serializable @SerialName("update_boost")
    data class UpdateBoostEvent(
        @SerialName("boost_id") val boostId: BoostId,
        val changes: BoostUpdate,
    ) : ShellEvent()

    @Serializable @SerialName("toggle_boost")
    data class ToggleBoost(
        @SerialName("boost_id") val boostId: BoostId,
        val enabled: Boolean,
    ) : ShellEvent()

    @Serializable @SerialName("delete_boost")
    data class DeleteBoost(@SerialName("boost_id") val boostId: BoostId) : ShellEvent()

    // Content Blocker
    @Serializable @SerialName("toggle_content_blocker")
    data class ToggleContentBlocker(val enabled: Boolean) : ShellEvent()

    @Serializable @SerialName("toggle_popup_blocking")
    data class TogglePopupBlocking(val enabled: Boolean) : ShellEvent()

    @Serializable @SerialName("add_filter_list")
    data class AddFilterList(
        val id: String,
        val name: String,
        val url: String,
    ) : ShellEvent()

    @Serializable @SerialName("remove_filter_list")
    data class RemoveFilterList(val id: String) : ShellEvent()

    @Serializable @SerialName("toggle_filter_list")
    data class ToggleFilterList(
        val id: String,
        val enabled: Boolean,
    ) : ShellEvent()

    // Notes
    @Serializable @SerialName("create_note")
    data class CreateNote(
        @SerialName("linked_tab") val linkedTab: TabId? = null,
        val content: String,
    ) : ShellEvent()

    @Serializable @SerialName("update_note")
    data class UpdateNote(
        @SerialName("note_id") val noteId: NoteId,
        val content: String,
    ) : ShellEvent()

    @Serializable @SerialName("delete_note")
    data class DeleteNote(@SerialName("note_id") val noteId: NoteId) : ShellEvent()

    // Settings
    @Serializable @SerialName("update_settings")
    data class UpdateSettings(val changes: SettingsUpdate) : ShellEvent()

    // Window/sidebar
    @Serializable @SerialName("sidebar_toggled")
    data class SidebarToggled(val visible: Boolean) : ShellEvent()

    @Serializable @SerialName("sidebar_resized")
    data class SidebarResized(val width: Double) : ShellEvent()

    @Serializable @SerialName("window_resized")
    data class WindowResized(val size: Size) : ShellEvent()

    @Serializable @SerialName("window_focus_changed")
    data class WindowFocusChanged(val focused: Boolean) : ShellEvent()

    // Web content state sync
    @Serializable @SerialName("tab_title_updated")
    data class TabTitleUpdated(
        @SerialName("tab_id") val tabId: TabId,
        val title: String,
    ) : ShellEvent()

    @Serializable @SerialName("tab_url_updated")
    data class TabUrlUpdated(
        @SerialName("tab_id") val tabId: TabId,
        val url: Url,
    ) : ShellEvent()

    @Serializable @SerialName("tab_loading_changed")
    data class TabLoadingChanged(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("is_loading") val isLoading: Boolean,
    ) : ShellEvent()

    @Serializable @SerialName("tab_navigation_state_changed")
    data class TabNavigationStateChanged(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("can_go_back") val canGoBack: Boolean,
        @SerialName("can_go_forward") val canGoForward: Boolean,
    ) : ShellEvent()

    // History
    @Serializable @SerialName("search_history")
    data class SearchHistory(
        val query: String,
        val limit: Int,
    ) : ShellEvent()

    @Serializable @SerialName("clear_history")
    data object ClearHistory : ShellEvent()

    @Serializable @SerialName("delete_history_entry")
    data class DeleteHistoryEntry(@SerialName("entry_id") val entryId: String) : ShellEvent()

    // Bookmarks
    @Serializable @SerialName("add_bookmark")
    data class AddBookmark(
        val url: Url,
        val title: String,
        @SerialName("folder_id") val folderId: String? = null,
    ) : ShellEvent()

    @Serializable @SerialName("remove_bookmark")
    data class RemoveBookmark(@SerialName("bookmark_id") val bookmarkId: String) : ShellEvent()

    @Serializable @SerialName("move_bookmark")
    data class MoveBookmark(
        @SerialName("bookmark_id") val bookmarkId: String,
        @SerialName("folder_id") val folderId: String? = null,
    ) : ShellEvent()

    @Serializable @SerialName("search_bookmarks")
    data class SearchBookmarks(val query: String) : ShellEvent()

    // Permissions
    @Serializable @SerialName("grant_permission")
    data class GrantPermission(
        val origin: String,
        val permission: String,
    ) : ShellEvent()

    @Serializable @SerialName("revoke_permission")
    data class RevokePermission(
        val origin: String,
        val permission: String,
    ) : ShellEvent()

    @Serializable @SerialName("query_permission")
    data class QueryPermission(
        val origin: String,
        val permission: String,
    ) : ShellEvent()

    // Zoom
    @Serializable @SerialName("set_zoom")
    data class SetZoom(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("zoom_level") val zoomLevel: Double,
    ) : ShellEvent()

    @Serializable @SerialName("reset_zoom")
    data class ResetZoom(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    // Downloads
    @Serializable @SerialName("pause_download")
    data class PauseDownload(@SerialName("download_id") val downloadId: String) : ShellEvent()

    @Serializable @SerialName("resume_download")
    data class ResumeDownload(@SerialName("download_id") val downloadId: String) : ShellEvent()

    @Serializable @SerialName("cancel_download")
    data class CancelDownload(@SerialName("download_id") val downloadId: String) : ShellEvent()

    @Serializable @SerialName("remove_download")
    data class RemoveDownload(@SerialName("download_id") val downloadId: String) : ShellEvent()

    // Browsing
    @Serializable @SerialName("toggle_pi_p")
    data class TogglePiP(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("toggle_dev_tools")
    data class ToggleDevTools(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("print_page")
    data class PrintPage(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    @Serializable @SerialName("view_source")
    data class ViewSource(@SerialName("tab_id") val tabId: TabId) : ShellEvent()

    // Lifecycle
    @Serializable @SerialName("app_launched")
    data object AppLaunched : ShellEvent()

    @Serializable @SerialName("app_will_terminate")
    data object AppWillTerminate : ShellEvent()

    @Serializable @SerialName("memory_warning")
    data class MemoryWarning(val level: MemoryPressureLevel) : ShellEvent()



    // Extensions management
    @Serializable @SerialName("toggle_extension")
    data class ToggleExtension(@SerialName("extension_id") val extensionId: String) : ShellEvent()

    @Serializable @SerialName("remove_extension")
    data class RemoveExtension(@SerialName("extension_id") val extensionId: String) : ShellEvent()

    // Account & Sync
    @Serializable @SerialName("sign_in")
    data class SignIn(
        val email: String,
        @SerialName("display_name") val displayName: String? = null,
        val password: String? = null,
        @SerialName("access_token") val accessToken: String? = null,
        @SerialName("user_id") val userId: String? = null,
        @SerialName("device_id") val deviceId: String? = null,
    ) : ShellEvent()

    @Serializable @SerialName("sign_out")
    data object SignOut : ShellEvent()

    @Serializable @SerialName("toggle_sync")
    data object ToggleSync : ShellEvent()

    // Air traffic control
    @Serializable @SerialName("create_traffic_rule")
    data class CreateTrafficRule(val rule: TrafficRule) : ShellEvent()

    @Serializable @SerialName("delete_traffic_rule")
    data class DeleteTrafficRule(@SerialName("rule_id") val ruleId: String) : ShellEvent()

    @Serializable @SerialName("update_traffic_rule")
    data class UpdateTrafficRule(val rule: TrafficRule) : ShellEvent()

    @Serializable @SerialName("set_default_link_behavior")
    data class SetDefaultLinkBehavior(val behavior: DefaultLinkBehavior) : ShellEvent()

    // Space settings
    @Serializable @SerialName("update_space_config")
    data class UpdateSpaceConfig(
        @SerialName("space_id") val spaceId: SpaceId,
        val name: String? = null,
        val color: SpaceColor? = null,
        @EncodeDefault(EncodeDefault.Mode.NEVER) val icon: Patchable<String> = Patchable.Absent,
    ) : ShellEvent()

    // Profile management
    @Serializable @SerialName("create_profile")
    data class CreateProfile(val name: String) : ShellEvent()

    @Serializable @SerialName("delete_profile")
    data class DeleteProfile(@SerialName("profile_id") val profileId: ProfileId) : ShellEvent()

    @Serializable @SerialName("update_profile")
    data class UpdateProfile(
        @SerialName("profile_id") val profileId: ProfileId,
        val name: String? = null,
        @SerialName("avatar_color") val avatarColor: String? = null,
        @SerialName("download_path") val downloadPath: String? = null,
        @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("archive_timeout_hours") val archiveTimeoutHours: Patchable<Double> = Patchable.Absent,
    ) : ShellEvent()

    @Serializable @SerialName("switch_profile")
    data class SwitchProfile(@SerialName("profile_id") val profileId: ProfileId) : ShellEvent()

    // Passwords
    @Serializable @SerialName("search_passwords")
    data class SearchPasswords(val query: String) : ShellEvent()

    @Serializable @SerialName("delete_password")
    data class DeletePassword(@SerialName("password_id") val passwordId: String) : ShellEvent()

    @Serializable @SerialName("add_password")
    data class AddPassword(
        val domain: String,
        val username: String,
    ) : ShellEvent()

    // Autofill
    @Serializable @SerialName("add_autofill_address")
    data class AddAutofillAddress(val address: AutofillAddress) : ShellEvent()

    @Serializable @SerialName("delete_autofill_address")
    data class DeleteAutofillAddress(val id: String) : ShellEvent()

    @Serializable @SerialName("add_autofill_payment")
    data class AddAutofillPayment(val payment: AutofillPayment) : ShellEvent()

    @Serializable @SerialName("delete_autofill_payment")
    data class DeleteAutofillPayment(val id: String) : ShellEvent()

    @Serializable @SerialName("reset_settings")
    data object ResetSettings : ShellEvent()

    // Reading List
    @Serializable @SerialName("add_to_reading_list")
    data class AddToReadingList(
        val url: String,
        val title: String,
    ) : ShellEvent()

    @Serializable @SerialName("remove_from_reading_list")
    data class RemoveFromReadingList(@SerialName("item_id") val itemId: String) : ShellEvent()

    @Serializable @SerialName("mark_reading_list_item_read")
    data class MarkReadingListItemRead(@SerialName("item_id") val itemId: String) : ShellEvent()

    @Serializable @SerialName("mark_reading_list_item_unread")
    data class MarkReadingListItemUnread(@SerialName("item_id") val itemId: String) : ShellEvent()
}
