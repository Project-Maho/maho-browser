@file:OptIn(ExperimentalSerializationApi::class)

package dev.maho.browser.models

import dev.maho.browser.support.Patchable
import kotlinx.serialization.EncodeDefault
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.KSerializer
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.descriptors.buildClassSerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder
import kotlinx.serialization.json.JsonDecoder
import kotlinx.serialization.json.JsonEncoder
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonObject

@Serializable
data class SpaceColor(
    val hue: Double,
    val saturation: Double,
    val brightness: Double,
)

@Serializable
data class Tab(
    val id: TabId,
    @SerialName("parentId") val parentId: TabId? = null,
    @SerialName("spaceId") val spaceId: SpaceId,
    val url: Url,
    val title: String,
    val favicon: ImageData? = null,
    val state: TabLifecycleState,
    @SerialName("isPinned") val isPinned: Boolean,
    @SerialName("isFavorite") val isFavorite: Boolean,
    @SerialName("isMuted") val isMuted: Boolean,
    @SerialName("zoomLevel") val zoomLevel: Double,
    @SerialName("createdAt") val createdAt: String,
    @SerialName("lastActiveAt") val lastActiveAt: String,
    @SerialName("scrollPosition") val scrollPosition: ScrollPosition,
    @SerialName("pinnedUrl") val pinnedUrl: Url? = null,
)

@Serializable
sealed class TabLifecycleState {
    @Serializable @SerialName("active")
    data object Active : TabLifecycleState()

    @Serializable @SerialName("frozen")
    data object Frozen : TabLifecycleState()

    @Serializable @SerialName("suspended")
    data class Suspended(val snapshot: TabSnapshot) : TabLifecycleState()

    @Serializable @SerialName("archived")
    data class Archived(@SerialName("metadata_only") val metadataOnly: Boolean) : TabLifecycleState()
}

@Serializable
data class Space(
    val id: SpaceId,
    @SerialName("profileId") val profileId: ProfileId = "",
    val name: String,
    val color: SpaceColor,
    val icon: String? = null,
    @SerialName("tabOrder") val tabOrder: List<TabId>,
    val folders: List<Folder>,
    @SerialName("atcRules") val atcRules: List<ATCRule>,
    @SerialName("isActive") val isActive: Boolean,
    @SerialName("createdAt") val createdAt: String,
)

@Serializable
data class ATCRule(
    val id: String,
    val condition: ATCCondition,
    val action: ATCAction,
    val enabled: Boolean,
)

@Serializable
sealed class ATCCondition {
    @Serializable @SerialName("inactive_duration")
    data class InactiveDuration(val hours: Double) : ATCCondition()

    @Serializable @SerialName("tab_count_exceeded")
    data class TabCountExceeded(@SerialName("max_tabs") val maxTabs: Int) : ATCCondition()

    @Serializable @SerialName("url_pattern")
    data class UrlPatternCondition(val pattern: String) : ATCCondition()

    @Serializable @SerialName("url_contains")
    data class UrlContains(val text: String) : ATCCondition()

    @Serializable @SerialName("url_equals")
    data class UrlEquals(val url: String) : ATCCondition()
}

object ATCActionSerializer : KSerializer<ATCAction> {
    override val descriptor = buildClassSerialDescriptor("ATCAction")

    override fun serialize(encoder: Encoder, value: ATCAction) {
        val jsonEncoder = encoder as JsonEncoder
        val element = when (value) {
            is ATCAction.Close -> JsonPrimitive("close")
            is ATCAction.Archive -> JsonPrimitive("archive")
            is ATCAction.Route -> buildJsonObject {
                putJsonObject("route") {
                    put("space_id", value.spaceId)
                }
            }
        }
        jsonEncoder.encodeJsonElement(element)
    }

    override fun deserialize(decoder: Decoder): ATCAction {
        val jsonDecoder = decoder as JsonDecoder
        val element = jsonDecoder.decodeJsonElement()
        return when {
            element is JsonPrimitive -> when (element.content) {
                "close" -> ATCAction.Close
                "archive" -> ATCAction.Archive
                else -> error("Unknown ATCAction: ${element.content}")
            }
            element is kotlinx.serialization.json.JsonObject -> {
                val routeObj = element["route"]?.let { it as? kotlinx.serialization.json.JsonObject }
                    ?: error("Unknown ATCAction object: $element")
                ATCAction.Route(spaceId = routeObj["space_id"]!!.jsonPrimitive.content)
            }
            else -> error("Cannot decode ATCAction from $element")
        }
    }
}

@Serializable(with = ATCActionSerializer::class)
sealed class ATCAction {
    data object Close : ATCAction()
    data object Archive : ATCAction()
    data class Route(@SerialName("space_id") val spaceId: SpaceId) : ATCAction()
}

@Serializable
data class Profile(
    val id: ProfileId,
    val name: String,
    val spaces: List<Space>,
    val boosts: List<Boost>,
    val notes: List<Note>,
)

@Serializable
data class ProfileConfig(
    val id: ProfileId,
    val name: String,
    @SerialName("avatarColor") val avatarColor: String,
    @SerialName("defaultSearchEngine") val defaultSearchEngine: SettingsSearchEngine,
    @SerialName("downloadPath") val downloadPath: String,
    @SerialName("archiveTimeoutHours") val archiveTimeoutHours: Double? = null,
    @SerialName("dataStoreId") val dataStoreId: String? = null,
)

@Serializable
data class Folder(
    val id: FolderId,
    val name: String,
    @SerialName("tabIds") val tabIds: List<TabId>,
    @SerialName("isExpanded") val isExpanded: Boolean,
    @SerialName("isPinned") val isPinned: Boolean = false,
    @SerialName("parentFolderId") val parentFolderId: FolderId? = null,
)

@Serializable
data class Note(
    val id: NoteId,
    @SerialName("linkedTabId") val linkedTabId: TabId? = null,
    @SerialName("linkedUrl") val linkedUrl: Url? = null,
    val content: String,
    @SerialName("createdAt") val createdAt: String,
    @SerialName("updatedAt") val updatedAt: String,
)

@Serializable
enum class TextCase {
    @SerialName("none") NONE,
    @SerialName("upper") UPPER,
    @SerialName("lower") LOWER,
    @SerialName("capitalize") CAPITALIZE
}

@Serializable
data class ColorBoost(
    @SerialName("enableColorBoost") val enableColorBoost: Boolean,
    @SerialName("dotAngleDeg") val dotAngleDeg: Double,
    @SerialName("secondaryDotAngleDegDelta") val secondaryDotAngleDegDelta: Double,
    val brightness: Double,
    val saturation: Double,
    val contrast: Double,
    @SerialName("autoTheme") val autoTheme: Boolean,
    @SerialName("smartInvert") val smartInvert: Boolean,
)

@Serializable
data class TypographyBoost(
    @SerialName("fontFamily") val fontFamily: String? = null,
    @SerialName("textCaseOverride") val textCaseOverride: TextCase,
    @SerialName("sizeOverride") val sizeOverride: Float? = null,
)

@Serializable
data class Boost(
    val id: BoostId,
    val domain: String,
    val name: String,
    val color: ColorBoost,
    val typography: TypographyBoost,
    @SerialName("zapSelectors") val zapSelectors: List<String>,
    @SerialName("customCss") val customCss: String,
    @SerialName("createdAt") val createdAt: String,
    @SerialName("updatedAt") val updatedAt: String,
)

@Serializable
data class ColorBoostUpdate(
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("enableColorBoost") val enableColorBoost: Boolean? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("dotAngleDeg") val dotAngleDeg: Double? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("secondaryDotAngleDegDelta") val secondaryDotAngleDegDelta: Double? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) val brightness: Double? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) val saturation: Double? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) val contrast: Double? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("autoTheme") val autoTheme: Boolean? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("smartInvert") val smartInvert: Boolean? = null,
)

@Serializable
data class TypographyBoostUpdate(
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("fontFamily") val fontFamily: Patchable<String?> = Patchable.Absent,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("textCaseOverride") val textCaseOverride: TextCase? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("sizeOverride") val sizeOverride: Patchable<Float?> = Patchable.Absent,
)

@Serializable
data class BoostUpdate(
    @EncodeDefault(EncodeDefault.Mode.NEVER) val name: String? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) val color: ColorBoostUpdate? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) val typography: TypographyBoostUpdate? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("zapSelectors") val zapSelectors: List<String>? = null,
    @EncodeDefault(EncodeDefault.Mode.NEVER) @SerialName("customCss") val customCss: String? = null,
)

@Serializable
data class BookmarkEntry(
    val id: String,
    val url: String,
    val title: String,
    @SerialName("folderId") val folderId: String? = null,
    @SerialName("createdAt") val createdAt: String,
)

@Serializable
data class HistoryEntry(
    val id: String,
    val url: String,
    val title: String,
    @SerialName("visitedAt") val visitedAt: String,
)

@Serializable
data class SearchEngine(
    val id: String,
    val name: String,
    @SerialName("urlTemplate") val urlTemplate: String,
    val shortcut: String? = null,
    @SerialName("iconUrl") val iconUrl: String? = null,
    @SerialName("isDefault") val isDefault: Boolean,
)

@Serializable
data class SearchEngineViewModel(
    val id: String,
    val name: String,
    val shortcut: String? = null,
    @SerialName("iconUrl") val iconUrl: String? = null,
    @SerialName("isDefault") val isDefault: Boolean,
)

@Serializable
data class TrafficRule(
    val id: String,
    @SerialName("urlPattern") val urlPattern: String,
    @SerialName("matchType") val matchType: MatchType,
    @SerialName("targetSpaceId") val targetSpaceId: SpaceId,
    val enabled: Boolean,
)

@Serializable
enum class MatchType {
    @SerialName("contains") Contains,
    @SerialName("equals") Equals,
    @SerialName("regex") Regex,
}

object DefaultLinkBehaviorSerializer : KSerializer<DefaultLinkBehavior> {
    override val descriptor = buildClassSerialDescriptor("DefaultLinkBehavior")

    override fun serialize(encoder: Encoder, value: DefaultLinkBehavior) {
        val jsonEncoder = encoder as JsonEncoder
        val element = when (value) {
            is DefaultLinkBehavior.CurrentSpace -> JsonPrimitive("current_space")
            is DefaultLinkBehavior.MostRecentSpace -> JsonPrimitive("most_recent_space")
            is DefaultLinkBehavior.SpecificSpace -> buildJsonObject {
                putJsonObject("specific_space") {
                    put("space_id", value.spaceId)
                }
            }
        }
        jsonEncoder.encodeJsonElement(element)
    }

    override fun deserialize(decoder: Decoder): DefaultLinkBehavior {
        val jsonDecoder = decoder as JsonDecoder
        val element = jsonDecoder.decodeJsonElement()
        return when {
            element is JsonPrimitive -> when (element.content) {
                "current_space" -> DefaultLinkBehavior.CurrentSpace
                "most_recent_space" -> DefaultLinkBehavior.MostRecentSpace
                else -> error("Unknown DefaultLinkBehavior: ${element.content}")
            }
            element is kotlinx.serialization.json.JsonObject -> {
                val specificObj = element["specific_space"]?.let { it as? kotlinx.serialization.json.JsonObject }
                    ?: error("Unknown DefaultLinkBehavior object: $element")
                DefaultLinkBehavior.SpecificSpace(spaceId = specificObj["space_id"]!!.jsonPrimitive.content)
            }
            else -> error("Cannot decode DefaultLinkBehavior from $element")
        }
    }
}

@Serializable(with = DefaultLinkBehaviorSerializer::class)
sealed class DefaultLinkBehavior {
    data object CurrentSpace : DefaultLinkBehavior()
    data object MostRecentSpace : DefaultLinkBehavior()
    data class SpecificSpace(@SerialName("space_id") val spaceId: SpaceId) : DefaultLinkBehavior()
}

@Serializable
data class AutofillAddress(
    val id: String,
    val name: String,
    val street: String,
    val city: String,
    val state: String,
    val zip: String,
    val country: String,
    val phone: String? = null,
    val email: String? = null,
)

@Serializable
data class AutofillPayment(
    val id: String,
    @SerialName("cardName") val cardName: String,
    @SerialName("lastFour") val lastFour: String,
    val expiry: String,
)

@Serializable
data class SavedPassword(
    val id: String,
    val domain: String,
    val username: String,
    @SerialName("createdAt") val createdAt: String,
    @SerialName("lastUsed") val lastUsed: String? = null,
)

@Serializable
data class AccountInfo(
    val email: String,
    @SerialName("displayName") val displayName: String? = null,
    @SerialName("avatarUrl") val avatarUrl: String? = null,
    @SerialName("syncEnabled") val syncEnabled: Boolean,
)

@Serializable
sealed class AccountSyncState {
    @Serializable @SerialName("idle")
    data object Idle : AccountSyncState()

    @Serializable @SerialName("syncing")
    data class Syncing(val progress: Double) : AccountSyncState()

    @Serializable @SerialName("synced")
    data class Synced(@SerialName("last_sync_at") val lastSyncAt: String) : AccountSyncState()

    @Serializable @SerialName("error")
    data class Error(val message: String) : AccountSyncState()

    @Serializable @SerialName("offline")
    data object Offline : AccountSyncState()
}

@Serializable
data class SpaceConfig(
    val color: SpaceColor,
    val icon: String? = null,
    val name: String,
    @SerialName("profileId") val profileId: ProfileId? = null,
)

@Serializable
data class ReadingListItem(
    val id: String,
    val url: String,
    val title: String,
    @SerialName("isRead") val isRead: Boolean,
    @SerialName("addedAt") val addedAt: String,
)

@Serializable
data class ConnectedDevice(
    val id: String,
    val name: String,
    @SerialName("deviceType") val deviceType: String,
    @SerialName("lastSeen") val lastSeen: String? = null,
    @SerialName("isOnline") val isOnline: Boolean? = null,
)

@Serializable
data class SyncStatusResponse(
    val state: AccountSyncState,
    val enabled: Boolean,
)
