package dev.maho.browser.sync

import android.util.Log
import dev.maho.browser.bridge.BridgeSync
import java.io.IOException
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

interface RelaySessionPersistence {
    fun load(): RelaySession?
    fun save(session: RelaySession): Boolean
    fun getServerUrl(defaultUrl: String): String
}

private object DefaultRelaySessionPersistence : RelaySessionPersistence {
    override fun load(): RelaySession? = RelaySessionStore.load()
    override fun save(session: RelaySession): Boolean = RelaySessionStore.save(session)
    override fun getServerUrl(defaultUrl: String): String = RelaySessionStore.getServerUrl(defaultUrl)
}

enum class SyncCycleOutcome {
    SUCCESS,
    TRANSIENT_FAILURE,
    AUTH_FAILURE,
    PERMANENT_FAILURE,
}

data class SyncCycleResult(
    val outcome: SyncCycleOutcome,
    val error: String? = null,
)

class SyncTransport(
    private val bridgeAdapter: SyncBridgeAdapter = DefaultSyncBridgeAdapter(),
    private val authClient: RelayAuthClient = RelayAuthClient(),
    private val sessionPersistence: RelaySessionPersistence = DefaultRelaySessionPersistence,
) {
    companion object {
        private const val TAG = "SyncTransport"
        private const val DEFAULT_SERVER_URL = "https://relay.mahobrowser.com"
        private const val PUSH_LIMIT = 50
        private const val PULL_LIMIT = 100
        private const val MAX_PULL_PAGES = 10

        private val cycleLock = ReentrantLock()
        val shared by lazy { SyncTransport() }
    }

    @Volatile
    private var cancelled = false

    fun cancel() {
        cancelled = true
        authClient.cancelActiveCalls()
    }

    fun runBoundedCycle(): SyncCycleResult {
        if (!cycleLock.tryLock()) return SyncCycleResult(SyncCycleOutcome.SUCCESS)
        return try {
            cancelled = false
            runCycleLocked()
        } finally {
            cycleLock.unlock()
        }
    }

    private fun runCycleLocked(): SyncCycleResult {
        val initialSession = sessionPersistence.load()
            ?: return finish(SyncCycleOutcome.AUTH_FAILURE, "Sign in to enable sync")
        val roomId = bridgeAdapter.getSyncRoomId()?.takeIf(::isValidRoomId)
            ?: return finish(SyncCycleOutcome.PERMANENT_FAILURE, "Join sync with a recovery phrase first")
        val baseUrl = initialSession.serverUrl
            ?: sessionPersistence.getServerUrl(DEFAULT_SERVER_URL)

        return try {
            bridgeAdapter.reportTransportState(SyncTransportStateReport("syncing"))
            val envelopes = bridgeAdapter.leaseOutgoingEnvelopes(PUSH_LIMIT)
            if (envelopes.isNotEmpty()) {
                val pushResponse = executeWithRefresh(initialSession, baseUrl) { session ->
                    authClient.pushSync(baseUrl, session.accessToken, roomId, envelopes)
                }
                val expectedIds = envelopes.mapTo(linkedSetOf()) { it.deliveryId }
                val acknowledgedIds = pushResponse.acks.mapTo(linkedSetOf()) { it.deliveryId }
                if (acknowledgedIds != expectedIds) {
                    throw SyncProtocolException("Relay ACK set did not match the leased delivery IDs")
                }
                pushResponse.acks.forEach { ack ->
                    if (!bridgeAdapter.acknowledge(ack)) {
                        throw SyncProtocolException("Core rejected ACK for ${ack.deliveryId}")
                    }
                }
            }

            var cursor = bridgeAdapter.getReceiveCursor(roomId)
            repeat(MAX_PULL_PAGES) {
                ensureNotCancelled()
                val response = executeWithRefresh(sessionPersistence.load() ?: initialSession, baseUrl) { session ->
                    authClient.pullSync(baseUrl, session.accessToken, roomId, cursor, PULL_LIMIT)
                }
                response.messages.forEach { envelope ->
                    ensureValidEnvelope(envelope)
                    val seq = envelope.relaySeq
                        ?: throw SyncProtocolException("Pulled envelope omitted relay_seq")
                    if (seq <= cursor) return@forEach
                    if (!bridgeAdapter.applyEnvelopeAndAdvanceCursor(roomId, envelope)) {
                        throw SyncProtocolException("Core failed to apply delivery ${envelope.deliveryId}")
                    }
                    cursor = seq
                }
                if (!response.hasMore) return finish(SyncCycleOutcome.SUCCESS)
                if (response.messages.isEmpty()) {
                    throw SyncProtocolException("Relay returned an empty page with has_more=true")
                }
            }
            finish(SyncCycleOutcome.TRANSIENT_FAILURE, "Sync pull exceeded the bounded page budget")
        } catch (error: RelayApiException) {
            val outcome = when {
                error.code == 401 || error.code == 403 -> SyncCycleOutcome.AUTH_FAILURE
                error.code == 408 || error.code == 429 || error.code >= 500 -> SyncCycleOutcome.TRANSIENT_FAILURE
                else -> SyncCycleOutcome.PERMANENT_FAILURE
            }
            finish(outcome, error.message ?: "Relay sync request failed")
        } catch (error: IOException) {
            finish(SyncCycleOutcome.TRANSIENT_FAILURE, error.message ?: "Relay sync request failed")
        } catch (error: SyncProtocolException) {
            finish(SyncCycleOutcome.PERMANENT_FAILURE, error.message)
        } catch (error: Exception) {
            finish(SyncCycleOutcome.PERMANENT_FAILURE, error.message ?: "Sync failed")
        }
    }

    private fun <T> executeWithRefresh(
        initialSession: RelaySession,
        baseUrl: String,
        operation: (RelaySession) -> T,
    ): T {
        ensureNotCancelled()
        return try {
            operation(initialSession)
        } catch (error: RelayApiException) {
            if (error.code != 401) throw error
            val refreshed = refreshSession(initialSession, baseUrl)
            ensureNotCancelled()
            operation(refreshed)
        }
    }

    private fun refreshSession(currentSession: RelaySession, baseUrl: String): RelaySession {
        val refreshed = authClient.refresh(baseUrl, currentSession.refreshToken, currentSession.email)
        val merged = when (val result = mergeRelayRefreshSession(currentSession, refreshed)) {
            is RelayRefreshMergeResult.Accepted -> result.session.copy(serverUrl = baseUrl)
            is RelayRefreshMergeResult.Rejected -> throw SyncProtocolException(result.reason)
        }
        if (!sessionPersistence.save(merged)) {
            throw SyncProtocolException("Unable to securely save refreshed relay session")
        }
        if (currentSession.accessToken != merged.accessToken) {
            BridgeSync.signIn(
                email = merged.account?.email ?: merged.email,
                displayName = merged.account?.displayName,
                accessToken = merged.accessToken,
                userId = merged.account?.id,
                deviceId = merged.device?.id,
            )
        }
        return merged
    }

    private fun ensureValidEnvelope(envelope: SyncEnvelopeV2) {
        if (envelope.protocolVersion != SYNC_PROTOCOL_VERSION) {
            throw SyncProtocolException("Unsupported sync protocol version ${envelope.protocolVersion}")
        }
        if (envelope.deliveryId.isBlank() || envelope.payload.isBlank()) {
            throw SyncProtocolException("Relay returned a malformed V2 envelope")
        }
    }

    private fun ensureNotCancelled() {
        if (cancelled) throw IOException("Sync cycle cancelled")
    }

    private fun finish(outcome: SyncCycleOutcome, error: String? = null): SyncCycleResult {
        val kind = if (outcome == SyncCycleOutcome.SUCCESS) "synced" else "error"
        runCatching { bridgeAdapter.reportTransportState(SyncTransportStateReport(kind, error)) }
            .onFailure { Log.w(TAG, "Unable to report transport state: ${it.message}") }
        return SyncCycleResult(outcome, error)
    }

    private fun isValidRoomId(roomId: String): Boolean =
        roomId.length == 32 && roomId.all { it in '0'..'9' || it in 'a'..'f' }
}

private class SyncProtocolException(message: String) : Exception(message)
