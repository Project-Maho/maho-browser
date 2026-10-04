package dev.maho.browser.sync

/**
 * Core-facing seam used by the bounded REST V2 sync cycle.
 *
 * Every method maps to one core operation. The transport never touches the
 * native bridge directly so the whole cycle stays unit-testable on the JVM.
 */
interface SyncBridgeAdapter {
    /**
     * The 32-hex sync room derived locally from the recovery seed.
     *
     * This is deliberately *not* the per-device relay `devices.room_id`: every
     * device of one account must address the same shared room.
     *
     * Returns null when the user has not joined sync with a recovery phrase.
     */
    fun getSyncRoomId(): String?

    /**
     * Leases at most [limit] durable outbox rows for one transport attempt.
     *
     * Leased rows stay pending in core until [acknowledge] confirms their exact
     * delivery ID, so a crash or socket failure re-sends instead of losing data.
     */
    fun leaseOutgoingEnvelopes(limit: Int): List<SyncEnvelopeV2>

    /** Completes exactly one leased outbox row by its relay-acknowledged delivery ID. */
    fun acknowledge(ack: RelayAckV2): Boolean

    /** Last relay sequence durably applied for [roomId]; 0 when nothing was applied. */
    fun getReceiveCursor(roomId: String): Long

    /**
     * Applies one envelope and advances the room cursor in the same core
     * transaction. Returning false must leave the cursor untouched so the
     * delivery is safely replayed on the next cycle.
     */
    fun applyEnvelopeAndAdvanceCursor(roomId: String, envelope: SyncEnvelopeV2): Boolean

    /** Publishes the transport-visible state so core status reflects real progress. */
    fun reportTransportState(report: SyncTransportStateReport)
}

/**
 * Raised when the core bridge cannot service a V2 sync operation.
 *
 * The transport treats this as a permanent failure for the current cycle: it
 * never silently degrades to a partial or lossy sync path.
 */
class SyncBridgeUnavailableException(message: String) : Exception(message)
