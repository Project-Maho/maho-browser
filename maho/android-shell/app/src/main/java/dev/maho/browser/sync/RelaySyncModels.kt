package dev.maho.browser.sync

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

const val SYNC_PROTOCOL_VERSION = 2

@Serializable
data class RelaySyncBootstrap(
    val version: Int,
    @SerialName("room_id") val roomId: String,
    val seed: String,
    @SerialName("created_at") val createdAt: Long,
)

@Serializable
data class RelaySyncBootstrapRequest(
    val version: Int,
    @SerialName("room_id") val roomId: String,
    val seed: String,
)

@Serializable
data class SyncEnvelopeV2(
    @SerialName("protocol_version") val protocolVersion: Int,
    @SerialName("delivery_id") val deliveryId: String,
    val payload: String,
    @SerialName("relay_seq") val relaySeq: Long? = null,
)

@Serializable
data class RelayAckV2(
    @SerialName("delivery_id") val deliveryId: String,
    val seq: Long,
)

@Serializable
data class RelaySyncPushRequest(
    @SerialName("room_id") val roomId: String,
    val messages: List<SyncEnvelopeV2>,
)

@Serializable
data class RelaySyncPushResponse(
    val acks: List<RelayAckV2> = emptyList(),
)

@Serializable
data class RelaySyncPullResponse(
    val messages: List<SyncEnvelopeV2> = emptyList(),
    @SerialName("has_more") val hasMore: Boolean = false,
)

@Serializable
data class SyncTransportStateReport(
    val kind: String,
    @SerialName("last_error") val lastError: String? = null,
)

/**
 * Mirrors `maho_core::sync_models::SyncStatus`. Core serializes this enum in
 * lowercase, so every shell renders the same set of transport states.
 */
@Serializable
enum class SyncStateKind {
    @SerialName("idle") IDLE,
    @SerialName("connecting") CONNECTING,
    @SerialName("syncing") SYNCING,
    @SerialName("synced") SYNCED,
    @SerialName("error") ERROR,
    @SerialName("offline") OFFLINE,
}

/**
 * Structured counterpart of `maho_core::sync_models::SyncStateResponse`.
 *
 * Decoding is strict on purpose: a schema mismatch is a real defect that must
 * surface as a sync error instead of silently collapsing into an idle state.
 */
@Serializable
data class SyncStateResponse(
    val kind: SyncStateKind,
    @SerialName("last_success_at") val lastSuccessAt: Long? = null,
    @SerialName("pending_outbox_count") val pendingOutboxCount: Int = 0,
    @SerialName("last_error") val lastError: String? = null,
)

/** Result of reading core's sync state, keeping decode failures observable. */
sealed interface SyncStateResult {
    data class Available(val state: SyncStateResponse) : SyncStateResult

    /** Core returned no status at all (for example before initialization). */
    data object Unavailable : SyncStateResult

    /** Core returned a payload that does not match the shared schema. */
    data class Invalid(val reason: String) : SyncStateResult
}
