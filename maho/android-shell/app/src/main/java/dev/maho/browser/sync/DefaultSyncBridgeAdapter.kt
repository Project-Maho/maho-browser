package dev.maho.browser.sync

import dev.maho.browser.MahoBridge
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.encodeToString

class DefaultSyncBridgeAdapter : SyncBridgeAdapter {
    private val json = MahoJson.instance

    override fun getSyncRoomId(): String? = MahoBridge.getSyncRoomId()

    override fun leaseOutgoingEnvelopes(limit: Int): List<SyncEnvelopeV2> {
        val response = MahoBridge.leaseSyncOutgoingEnvelopes(limit) ?: return emptyList()
        return json.decodeFromString(response)
    }

    override fun acknowledge(ack: RelayAckV2): Boolean =
        MahoBridge.acknowledgeSyncEnvelope(json.encodeToString(ack))

    override fun getReceiveCursor(roomId: String): Long = MahoBridge.getSyncReceiveCursor(roomId)

    override fun applyEnvelopeAndAdvanceCursor(roomId: String, envelope: SyncEnvelopeV2): Boolean =
        MahoBridge.applySyncEnvelopeAndAdvanceCursor(roomId, json.encodeToString(envelope))

    override fun reportTransportState(report: SyncTransportStateReport) {
        MahoBridge.reportSyncTransportState(json.encodeToString(report))
    }
}
