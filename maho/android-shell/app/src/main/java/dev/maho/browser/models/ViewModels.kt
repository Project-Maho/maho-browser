@file:OptIn(ExperimentalSerializationApi::class)

package dev.maho.browser.models

import dev.maho.browser.support.Patchable
import kotlinx.serialization.EncodeDefault
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class TabRole(
    val type: String, // "normal", "pinned", "favorite"
    val order: Int? = null
)

@Serializable
data class TabViewModel(
    val id: TabId,
    @SerialName("spaceId") val spaceId: SpaceId,
    val title: String,
    @SerialName("customTitle") val customTitle: String? = null,
    val url: String,
    val favicon: ImageData? = null,
    @SerialName("isLoading") val isLoading: Boolean,
    @SerialName("isMuted") val isMuted: Boolean,
    @SerialName("isPlayingAudio") val isPlayingAudio: Boolean,
    @SerialName("lifecycleState") val lifecycleState: String,
    val children: List<TabId>,
    @SerialName("createdAt") val createdAt: String,
    @SerialName("lastActiveAt") val lastActiveAt: String,
    val role: TabRole,
) {
    @Deprecated("Use role instead")
    val isPinned: Boolean get() = role.type == "pinned" || role.type == "favorite"

    @Deprecated("Use role instead")
    val isFavorite: Boolean get() = role.type == "favorite"

    @Deprecated("Use role instead")
    val favoriteOrder: Int? get() = if (role.type == "favorite") role.order else null
}

@Serializable
data class ArchivedTabViewModel(
    val id: TabId,
    @SerialName("spaceId") val spaceId: SpaceId,
    val title: String,
    val url: String,
    val favicon: ImageData? = null,
    @SerialName("archivedAt") val archivedAt: String,
)

@Serializable
data class SpaceViewModel(
    val id: SpaceId,
    val name: String,
    val color: SpaceColor,
    @SerialName("tabCount") val tabCount: Int,
    @SerialName("isActive") val isActive: Boolean,
    val icon: String? = null,
    @SerialName("profileId") val profileId: ProfileId? = null,
    @SerialName("profileName") val profileName: String? = null,
    @SerialName("orderIndex") val orderIndex: Int? = null,
)

@Serializable
data class FolderViewModel(
    val id: FolderId,
    val name: String,
    @SerialName("tabCount") val tabCount: Int,
    @SerialName("isExpanded") val isExpanded: Boolean,
    @SerialName("tabIds") val tabIds: List<TabId>,
    @SerialName("isPinned") val isPinned: Boolean,
    @SerialName("parentFolderId") val parentFolderId: FolderId? = null,
)

@Serializable
data class DownloadViewModel(
    val id: String,
    val filename: String,
    val url: String,
    @SerialName("totalBytes") val totalBytes: Long,
    @SerialName("receivedBytes") val receivedBytes: Long,
    val state: DownloadState,
    @SerialName("filePath") val filePath: String? = null,
    @SerialName("mimeType") val mimeType: String? = null,
    val error: String? = null,
    @SerialName("startedAt") val startedAt: String,
    @SerialName("completedAt") val completedAt: String? = null,
)

@Serializable
enum class DownloadState {
    @SerialName("downloading") Downloading,
    @SerialName("paused") Paused,
    @SerialName("completed") Completed,
    @SerialName("failed") Failed,
    @SerialName("cancelled") Cancelled,
}

@Serializable
enum class SuggestionType {
    @SerialName("tab") Tab,
    @SerialName("bookmark") Bookmark,
    @SerialName("history") History,
    @SerialName("action") Action,
    @SerialName("navigation") Navigation,
    @SerialName("search") Search,
    @SerialName("calculator") Calculator,
    @SerialName("unitconversion") UnitConversion,
    @SerialName("archivedtab") ArchivedTab,
    @SerialName("closedtab") ClosedTab,
    @SerialName("folder") Folder,
    @SerialName("aiAnswer") AiAnswer,
}

@Serializable
data class SuggestionViewModel(
    val kind: SuggestionType,
    val key: String,
    val title: String,
    val subtitle: String? = null,
    @SerialName("executionPayload") val executionPayload: String? = null,
    val icon: ImageData? = null,
    @SerialName("relevanceScore") val relevanceScore: Double,
    @SerialName("matchRanges") val matchRanges: List<List<Int>>? = null,
)

@Serializable
data class NotificationViewModel(
    val id: String,
    val title: String,
    val message: String,
    val icon: ImageData? = null,
    val actions: List<NotificationAction>,
)

@Serializable
data class NotificationAction(
    val id: String,
    val label: String,
)

@Serializable
data class PermissionRequest(
    val id: String,
    val origin: String,
    val permission: String,
    val message: String,
)

@Serializable
data class TabStateUpdate(
    val title: String? = null,
    @SerialName("customTitle") val customTitle: String? = null,
    val url: String? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) val favicon: Patchable<ImageData> = Patchable.Absent,
    @SerialName("isLoading") val isLoading: Boolean? = null,
    @SerialName("isPinned") val isPinned: Boolean? = null,
    @SerialName("isMuted") val isMuted: Boolean? = null,
    @SerialName("isPlayingAudio") val isPlayingAudio: Boolean? = null,
    @SerialName("isFavorite") val isFavorite: Boolean? = null,
    @SerialName("favoriteOrder") val favoriteOrder: Int? = null,
    @SerialName("lifecycleState") val lifecycleState: String? = null,
    val role: TabRole? = null,
)

@Serializable
data class SettingsViewModel(
    val sections: List<SettingsSection>,
)

@Serializable
data class SettingsSection(
    val title: String,
    val items: List<SettingsItem>,
)

@Serializable
data class SettingsItem(
    val key: String,
    val label: String,
    val type: SettingsItemType,
    val value: kotlinx.serialization.json.JsonElement,
)

@Serializable
enum class SettingsItemType {
    @SerialName("toggle") Toggle,
    @SerialName("select") Select,
    @SerialName("text") Text,
    @SerialName("number") Number,
    @SerialName("color") ColorPicker,
    @SerialName("slider") Slider,
    @SerialName("key_capture") KeyCapture,
    @SerialName("url_pattern") UrlPatternInput,
    @SerialName("drag_list") DragList,
    @SerialName("path_selector") PathSelector,
}

@Serializable
data class NoteViewModel(
    val id: String,
    val content: String,
    @SerialName("linkedUrl") val linkedUrl: String? = null,
)

@Serializable
data class BoostViewModel(
    val id: BoostId,
    val domain: String,
    @SerialName("customCss") val customCss: String? = null,
    val enabled: Boolean,
)

@Serializable
data class FindBarState(
    val query: String,
    @SerialName("matchCount") val matchCount: Int,
    @SerialName("activeIndex") val activeIndex: Int,
    @SerialName("isVisible") val isVisible: Boolean,
)
