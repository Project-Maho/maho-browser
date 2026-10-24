package dev.maho.browser.sync

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

const val SYNC_PROTOCOL_VERSION = 2

/**
 * `GET /sync/bootstrap`. An escrow account carries the seed; a client-held
 * account (`key_mode: "client_held"`) returns only `key_mode` + `room_id`
 * because the relay no longer holds any key material.
 */
@Serializable
data class RelaySyncBootstrap(
    val version: Int? = null,
    @SerialName("room_id") val roomId: String,
    val seed: String? = null,
    @SerialName("created_at") val createdAt: Long = 0,
    @SerialName("key_mode") val keyMode: String? = null,
) {
    /** The account holds its own keys: only the recovery phrase can join sync. */
    val isClientHeld: Boolean
        get() = keyMode == KEY_MODE_CLIENT_HELD

    companion object {
        const val KEY_MODE_CLIENT_HELD = "client_held"
    }
}

/** Why sync is paused until the user enters the 24-word recovery phrase. */
enum class SyncRecoveryRequirement(val message: String) {
    /** Bootstrap said `key_mode: client_held` and this device has not joined that room. */
    CLIENT_HELD_BOOTSTRAP(
        "This account uses your own keys \u2014 enter your 24-word recovery phrase to join sync.",
    ),
    /** A push or pull answered `410 room_retired`. */
    ROOM_RETIRED(
        "This account now uses your own keys - enter your recovery phrase to continue.",
    ),
    /** The entered phrase derived a different room than the account's client-held room. */
    RECOVERY_PHRASE_MISMATCH(
        "That recovery phrase does not match this account. Enter this account's 24-word recovery phrase to join sync.",
    ),
}

/** True for the relay's `410 {"code":"room_retired"}` answer to a push or pull. */
internal fun isRoomRetiredResponse(code: Int, body: String?): Boolean {
    if (code != 410 || body.isNullOrBlank()) return false
    return runCatching {
        val obj = dev.maho.browser.support.MahoJson.instance.parseToJsonElement(body)
            as? kotlinx.serialization.json.JsonObject ?: return false
        val codeValue = (obj["code"] as? kotlinx.serialization.json.JsonPrimitive)?.content
        val errorValue = (obj["error"] as? kotlinx.serialization.json.JsonPrimitive)?.content
        codeValue == "room_retired" || errorValue == "room_retired"
    }.getOrDefault(false)
}

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
