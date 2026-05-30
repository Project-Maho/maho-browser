@file:OptIn(ExperimentalSerializationApi::class)

package dev.maho.browser.models

import dev.maho.browser.support.Patchable
import kotlinx.serialization.EncodeDefault
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class SpaceConfigUpdate(
    @SerialName("space_id") val spaceId: SpaceId,
    val name: String? = null,
    val color: SpaceColor? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) val icon: Patchable<String> = Patchable.Absent,
    @SerialName("profile_id") val profileId: ProfileId? = null,
)

@Serializable
data class SpaceUpdate(
    val name: String? = null,
    val color: SpaceColor? = null,
    @SerialName("tabCount") val tabCount: Int? = null,
    @SerialName("isActive") val isActive: Boolean? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) val icon: Patchable<String> = Patchable.Absent,
    @SerialName("profile_id") val profileId: ProfileId? = null,
    @SerialName("profile_name") val profileName: String? = null,
    @SerialName("order_index") val orderIndex: Int? = null,
)

@Serializable
data class FolderUpdate(
    val name: String? = null,
    @SerialName("isExpanded") val isExpanded: Boolean? = null,
    @SerialName("isPinned") val isPinned: Boolean? = null,
)

@Serializable
sealed class SyncStatus {
    @Serializable @SerialName("idle")
    data object Idle : SyncStatus()

    @Serializable @SerialName("syncing")
    data class Syncing(val progress: Double) : SyncStatus()

    @Serializable @SerialName("synced")
    data class Synced(@SerialName("last_sync_at") val lastSyncAt: String) : SyncStatus()

    @Serializable @SerialName("error")
    data class Error(val message: String) : SyncStatus()

    @Serializable @SerialName("offline")
    data object Offline : SyncStatus()
}

@Serializable
sealed class MemoryAction {
    @Serializable @SerialName("froze_tabs")
    data class FrozeTabs(val count: Int) : MemoryAction()

    @Serializable @SerialName("suspended_tabs")
    data class SuspendedTabs(val count: Int) : MemoryAction()

    @Serializable @SerialName("killed_tabs")
    data class KilledTabs(val count: Int) : MemoryAction()

    @Serializable @SerialName("released_webviews")
    data class ReleasedWebviews(val count: Int) : MemoryAction()
}

@Serializable
data class MahoError(
    val code: String,
    val message: String,
    val details: kotlinx.serialization.json.JsonElement? = null,
)

@Serializable
data class AppStateSnapshot(
    val spaces: List<SpaceViewModel>,
    @SerialName("activeSpaceId") val activeSpaceId: SpaceId,
    val tabs: Map<SpaceId, List<TabViewModel>>,
    @SerialName("syncStatus") val syncStatus: SyncStatus,
)

@Serializable
sealed class CoreUpdate {

    @Serializable @SerialName("full_state")
    data class FullState(val state: AppStateSnapshot) : CoreUpdate()

    @Serializable @SerialName("tab_created")
    data class TabCreated(val tab: TabViewModel) : CoreUpdate()

    @Serializable @SerialName("tab_updated")
    data class TabUpdated(
        @SerialName("tab_id") val tabId: TabId,
        val changes: TabStateUpdate,
    ) : CoreUpdate()

    @Serializable @SerialName("tab_closed")
    data class TabClosed(
        @SerialName("tab_id") val tabId: TabId,
        val animated: Boolean,
    ) : CoreUpdate()

    @Serializable @SerialName("tab_order_changed")
    data class TabOrderChanged(
        @SerialName("space_id") val spaceId: SpaceId,
        val order: List<TabId>,
    ) : CoreUpdate()

    @Serializable @SerialName("space_created")
    data class SpaceCreated(val space: SpaceViewModel) : CoreUpdate()

    @Serializable @SerialName("space_updated")
    data class SpaceUpdated(
        @SerialName("space_id") val spaceId: SpaceId,
        val changes: SpaceUpdate,
    ) : CoreUpdate()

    @Serializable @SerialName("space_deleted")
    data class SpaceDeleted(@SerialName("space_id") val spaceId: SpaceId) : CoreUpdate()

    @Serializable @SerialName("space_renamed")
    data class SpaceRenamed(
        @SerialName("space_id") val spaceId: SpaceId,
        val name: String,
    ) : CoreUpdate()

    @Serializable @SerialName("space_recolored")
    data class SpaceRecolored(
        @SerialName("space_id") val spaceId: SpaceId,
        val color: SpaceColor,
    ) : CoreUpdate()

    @Serializable @SerialName("space_reordered")
    data class SpaceReordered(
        @SerialName("space_id") val spaceId: SpaceId,
        val from: Int,
        val to: Int,
    ) : CoreUpdate()

    @Serializable @SerialName("space_order_changed")
    data class SpaceOrderChanged(val order: List<SpaceId>) : CoreUpdate()

    @Serializable @SerialName("folder_created")
    data class FolderCreated(val folder: FolderViewModel) : CoreUpdate()

    @Serializable @SerialName("folder_updated")
    data class FolderUpdated(
        @SerialName("folder_id") val folderId: FolderId,
        val changes: FolderUpdate,
    ) : CoreUpdate()

    @Serializable @SerialName("folder_deleted")
    data class FolderDeleted(@SerialName("folder_id") val folderId: FolderId) : CoreUpdate()

    @Serializable @SerialName("folder_order_changed")
    data class FolderOrderChanged(@SerialName("space_id") val spaceId: SpaceId) : CoreUpdate()

    @Serializable @SerialName("active_space_changed")
    data class ActiveSpaceChanged(@SerialName("space_id") val spaceId: SpaceId) : CoreUpdate()

    @Serializable @SerialName("command_bar_results")
    data class CommandBarResults(val suggestions: List<SuggestionViewModel>) : CoreUpdate()

    @Serializable @SerialName("recent_searches_updated")
    data class RecentSearchesUpdated(val searches: List<String>) : CoreUpdate()

    @Serializable @SerialName("search_engines_updated")
    data class SearchEnginesUpdated(val engines: List<SearchEngineViewModel>) : CoreUpdate()

    @Serializable @SerialName("navigation_state_changed")
    data class NavigationStateChanged(
        @SerialName("tab_id") val tabId: TabId,
        val url: Url,
        val title: String,
        @SerialName("can_go_back") val canGoBack: Boolean,
        @SerialName("can_go_forward") val canGoForward: Boolean,
        @SerialName("is_loading") val isLoading: Boolean,
        val progress: Double,
    ) : CoreUpdate()

    @Serializable @SerialName("tab_audio_state_changed")
    data class TabAudioStateChanged(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("is_playing") val isPlaying: Boolean,
    ) : CoreUpdate()

    @Serializable @SerialName("tab_favicon_changed")
    data class TabFaviconChanged(
        @SerialName("tab_id") val tabId: TabId,
        val favicon: ImageData? = null,
    ) : CoreUpdate()

    @Serializable @SerialName("download_started")
    data class DownloadStarted(val download: DownloadViewModel) : CoreUpdate()

    @Serializable @SerialName("download_progress")
    data class DownloadProgress(
        @SerialName("download_id") val downloadId: DownloadId,
        val progress: Double,
    ) : CoreUpdate()

    @Serializable @SerialName("download_completed")
    data class DownloadCompleted(@SerialName("download_id") val downloadId: DownloadId) : CoreUpdate()

    @Serializable @SerialName("show_notification")
    data class ShowNotification(val notification: NotificationViewModel) : CoreUpdate()

    @Serializable @SerialName("notification_dismissed")
    data class NotificationDismissed(@SerialName("notification_id") val notificationId: String) : CoreUpdate()

    @Serializable @SerialName("all_notifications_dismissed")
    data object AllNotificationsDismissed : CoreUpdate()

    @Serializable @SerialName("notification_list")
    data class NotificationList(val notifications: List<NotificationViewModel>) : CoreUpdate()

    @Serializable @SerialName("show_permission_request")
    data class ShowPermissionRequest(val request: PermissionRequest) : CoreUpdate()

    @Serializable @SerialName("show_find_bar")
    data class ShowFindBar(@SerialName("tab_id") val tabId: TabId) : CoreUpdate()

    @Serializable @SerialName("history_results")
    data class HistoryResults(val entries: List<HistoryEntry>) : CoreUpdate()

    @Serializable @SerialName("bookmark_results")
    data class BookmarkResults(val bookmarks: List<BookmarkEntry>) : CoreUpdate()

    @Serializable @SerialName("zoom_changed")
    data class ZoomChanged(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("zoom_level") val zoomLevel: Double,
    ) : CoreUpdate()

    @Serializable @SerialName("tab_preview_updated")
    data class TabPreviewUpdated(
        @SerialName("tab_id") val tabId: TabId,
        @SerialName("preview_data") val previewData: String,
    ) : CoreUpdate()

    @Serializable @SerialName("tab_preview_capture_requested")
    data class TabPreviewCaptureRequested(@SerialName("tab_id") val tabId: TabId) : CoreUpdate()

    @Serializable @SerialName("permission_response")
    data class PermissionResponse(
        val origin: String,
        val permission: String,
        val granted: Boolean,
    ) : CoreUpdate()

    @Serializable @SerialName("print_requested")
    data class PrintRequested(@SerialName("tab_id") val tabId: TabId) : CoreUpdate()

    @Serializable @SerialName("pip_toggled")
    data class PipToggled(
        @SerialName("tab_id") val tabId: TabId,
        val active: Boolean,
    ) : CoreUpdate()

    @Serializable @SerialName("content_rules_compiled")
    data class ContentRulesCompiled(@SerialName("rule_count") val ruleCount: Int) : CoreUpdate()

    @Serializable @SerialName("content_blocker_state_changed")
    data class ContentBlockerStateChanged(
        val enabled: Boolean,
        @SerialName("popup_blocking") val popupBlocking: Boolean,
    ) : CoreUpdate()

    @Serializable @SerialName("filter_list_updated")
    data class FilterListUpdated(
        val id: String,
        @SerialName("rule_count") val ruleCount: Int,
    ) : CoreUpdate()

    @Serializable @SerialName("tab_lifecycle_changed")
    data class TabLifecycleChanged(
        @SerialName("tab_id") val tabId: TabId,
        val state: TabLifecycleState,
    ) : CoreUpdate()

    @Serializable @SerialName("memory_pressure_response")
    data class MemoryPressureResponse(val action: MemoryAction) : CoreUpdate()

    @Serializable @SerialName("sync_state_changed")
    data class SyncStateChanged(val status: SyncStatus) : CoreUpdate()

    @Serializable @SerialName("settings_changed")
    data class SettingsChanged(val settings: Settings) : CoreUpdate()

    @Serializable @SerialName("space_config_updated")
    data class SpaceConfigUpdated(
        @SerialName("space_id") val spaceId: SpaceId,
        val changes: SpaceConfigUpdate,
    ) : CoreUpdate()



    @Serializable @SerialName("extension_toggled")
    data class ExtensionToggled(
        @SerialName("extension_id") val extensionId: String,
        val enabled: Boolean,
    ) : CoreUpdate()

    @Serializable @SerialName("extension_removed")
    data class ExtensionRemoved(@SerialName("extension_id") val extensionId: String) : CoreUpdate()

    @Serializable @SerialName("account_changed")
    data class AccountChanged(val account: AccountInfo? = null) : CoreUpdate()

    @Serializable @SerialName("account_sync_state_changed")
    data class AccountSyncStateChanged(val state: AccountSyncState) : CoreUpdate()

    @Serializable @SerialName("traffic_rule_created")
    data class TrafficRuleCreated(val rule: TrafficRule) : CoreUpdate()

    @Serializable @SerialName("traffic_rule_deleted")
    data class TrafficRuleDeleted(@SerialName("rule_id") val ruleId: String) : CoreUpdate()

    @Serializable @SerialName("profile_created")
    data class ProfileCreated(val profile: ProfileConfig) : CoreUpdate()

    @Serializable @SerialName("profile_deleted")
    data class ProfileDeleted(
        @SerialName("profile_id") val profileId: ProfileId,
        @SerialName("data_store_id") val dataStoreId: String? = null,
    ) : CoreUpdate()

    @Serializable @SerialName("profile_updated")
    data class ProfileUpdated(val profile: ProfileConfig) : CoreUpdate()

    @Serializable @SerialName("active_profile_changed")
    data class ActiveProfileChanged(@SerialName("profile_id") val profileId: ProfileId) : CoreUpdate()

    @Serializable @SerialName("error")
    data class ErrorUpdate(
        val context: String,
        val error: MahoError,
    ) : CoreUpdate()

    @Serializable @SerialName("password_deleted")
    data class PasswordDeleted(@SerialName("password_id") val passwordId: String) : CoreUpdate()

    @Serializable @SerialName("passwords_search_result")
    data class PasswordsSearchResult(val passwords: List<SavedPassword>) : CoreUpdate()

    @Serializable @SerialName("autofill_address_added")
    data class AutofillAddressAdded(val address: AutofillAddress) : CoreUpdate()

    @Serializable @SerialName("autofill_address_deleted")
    data class AutofillAddressDeleted(val id: String) : CoreUpdate()

    @Serializable @SerialName("autofill_payment_added")
    data class AutofillPaymentAdded(val payment: AutofillPayment) : CoreUpdate()

    @Serializable @SerialName("autofill_payment_deleted")
    data class AutofillPaymentDeleted(val id: String) : CoreUpdate()

    @Serializable @SerialName("open_peek_tab")
    data class OpenPeekTab(
        val url: Url,
        @SerialName("source_tab_id") val sourceTabId: TabId,
    ) : CoreUpdate()

    @Serializable @SerialName("favorite_limit_reached")
    data class FavoriteLimitReached(val max: Int) : CoreUpdate()

    @Serializable @SerialName("navigate_tab")
    data class NavigateTab(
        @SerialName("tab_id") val tabId: TabId,
        val url: Url,
    ) : CoreUpdate()

    @Serializable @SerialName("tabs_migrated")
    data class TabsMigrated(
        @SerialName("tab_ids") val tabIds: List<TabId>,
        @SerialName("from_space_id") val fromSpaceId: SpaceId,
        @SerialName("to_space_id") val toSpaceId: SpaceId,
    ) : CoreUpdate()
}
