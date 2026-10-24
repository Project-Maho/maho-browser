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
    /** Non-null when sync is paused until the user enters the recovery phrase. */
    val recoveryRequirement: SyncRecoveryRequirement? = null,
)

/**
 * Persisted "enter your recovery phrase" pause. While a requirement is set the
 * transport never reaches the relay, and nothing generates a new seed.
 */
interface SyncRecoveryGate {
    fun current(): SyncRecoveryRequirement?
    fun require(requirement: SyncRecoveryRequirement)
}

private object DefaultSyncRecoveryGate : SyncRecoveryGate {
    override fun current(): SyncRecoveryRequirement? = RelaySessionStore.loadSyncRecoveryRequirement()
    override fun require(requirement: SyncRecoveryRequirement) = SyncManager.requireRecoveryPhrase(requirement)
}

/**
 * Hands a refreshed access token to core. Core's SignIn handler rebuilds the
 * account via `AccountManager::begin_sign_in`, which hard-resets
 * `sync_enabled` to false, so a running sync must be toggled back on to keep
 * core consistent with Android's running state.
 */
internal fun reauthenticateCoreAfterRefresh(
    session: RelaySession,
    syncRunning: Boolean,
    signInCore: (RelaySession) -> Unit,
    toggleCoreSync: () -> Unit,
) {
    signInCore(session)
    if (syncRunning) toggleCoreSync()
}

class SyncTransport(
    private val bridgeAdapter: SyncBridgeAdapter = DefaultSyncBridgeAdapter(),
    private val authClient: RelayAuthClient = RelayAuthClient(),
    private val sessionPersistence: RelaySessionPersistence = DefaultRelaySessionPersistence,
    private val recoveryGate: SyncRecoveryGate = DefaultSyncRecoveryGate,
    private val reauthenticateCore: (RelaySession) -> Unit = { session ->
        reauthenticateCoreAfterRefresh(
            session = session,
            syncRunning = SyncManager.isSyncing,
            signInCore = ::signInCore,
            toggleCoreSync = { BridgeSync.toggleSync() },
        )
    },
) {
    companion object {
        private const val TAG = "SyncTransport"
        private const val DEFAULT_SERVER_URL = "https://relay.mahobrowser.com"
        private const val PUSH_LIMIT = 50
        // A cycle drains the whole outbox (bounded) instead of one batch, so a
        // burst of offline edits is not left pending behind a "Synced" status.
        private const val MAX_PUSH_BATCHES = 20
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
        recoveryGate.current()?.let { requirement ->
            return finish(SyncCycleOutcome.PERMANENT_FAILURE, requirement.message, requirement)
        }
        val roomId = bridgeAdapter.getSyncRoomId()?.takeIf(::isValidRoomId)
            ?: return finish(SyncCycleOutcome.PERMANENT_FAILURE, "Join sync with a recovery phrase first")
        val baseUrl = initialSession.serverUrl
            ?: sessionPersistence.getServerUrl(DEFAULT_SERVER_URL)

        var appliedEnvelopes = 0
        return try {
            bridgeAdapter.reportTransportState(SyncTransportStateReport("syncing"))
            var batches = 0
            while (batches < MAX_PUSH_BATCHES) {
                ensureNotCancelled()
                batches += 1
                val pushed = pushOutbox(sessionPersistence.load() ?: initialSession, baseUrl, roomId)
                if (pushed < PUSH_LIMIT) break
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
                    appliedEnvelopes += 1
                }
                if (!response.hasMore) return finish(SyncCycleOutcome.SUCCESS)
                if (response.messages.isEmpty()) {
                    throw SyncProtocolException("Relay returned an empty page with has_more=true")
                }
            }
            finish(SyncCycleOutcome.TRANSIENT_FAILURE, "Sync pull exceeded the bounded page budget")
        } catch (error: RelayApiException) {
            if (error.isRoomRetired) {
                // The account switched to client-held keys: pause until the
                // user enters the recovery phrase. The requeued lease stays
                // durable in core for the account's new room.
                val requirement = SyncRecoveryRequirement.ROOM_RETIRED
                runCatching { recoveryGate.require(requirement) }
                    .onFailure { Log.w(TAG, "Unable to persist recovery requirement: ${it.message}") }
                return finish(SyncCycleOutcome.PERMANENT_FAILURE, requirement.message, requirement)
            }
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
        } finally {
            // Every exit, including a failure after some envelopes applied.
            signalShownTabResync(appliedEnvelopes)
        }
    }

    /**
     * Leases one outbox batch, pushes it and returns how many were acknowledged
     * (0 when the outbox is empty). Any failure before every ACK is
     * persisted hands the lease back to core so the next cycle re-sends the
     * same delivery IDs, then rethrows for outcome mapping.
     */
    private fun pushOutbox(initialSession: RelaySession, baseUrl: String, roomId: String): Int {
        val envelopes = bridgeAdapter.leaseOutgoingEnvelopes(PUSH_LIMIT)
        if (envelopes.isEmpty()) return 0
        try {
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
            return envelopes.size
        } catch (error: Throwable) {
            val requeued = runCatching { bridgeAdapter.requeueLeasedEnvelopes() }
            if (!requeued.getOrDefault(false)) {
                Log.w(TAG, "Unable to requeue sync lease: ${requeued.exceptionOrNull()?.message}")
            }
            throw error
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
            reauthenticateCore(merged)
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

    /**
     * Applied envelopes can close or move the tab on screen (a remote tab
     * tombstone, for example), so the browser screen must resync; nothing else
     * tells it about a core-side change.
     */
    private fun signalShownTabResync(appliedEnvelopes: Int) {
        if (dev.maho.browser.ui.shouldResyncAfterSyncPull(appliedEnvelopes)) {
            dev.maho.browser.ui.ShownTab.requestResync()
        }
    }

    private fun ensureNotCancelled() {
        if (cancelled) throw IOException("Sync cycle cancelled")
    }

    private fun finish(
        outcome: SyncCycleOutcome,
        error: String? = null,
        recoveryRequirement: SyncRecoveryRequirement? = null,
    ): SyncCycleResult {
        val kind = if (outcome == SyncCycleOutcome.SUCCESS) "synced" else "error"
        runCatching { bridgeAdapter.reportTransportState(SyncTransportStateReport(kind, error)) }
            .onFailure { Log.w(TAG, "Unable to report transport state: ${it.message}") }
        return SyncCycleResult(outcome, error, recoveryRequirement)
    }

    private fun isValidRoomId(roomId: String): Boolean =
        roomId.length == 32 && roomId.all { it in '0'..'9' || it in 'a'..'f' }
}

private class SyncProtocolException(message: String) : Exception(message)
