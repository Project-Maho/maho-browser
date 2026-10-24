package dev.maho.browser.sync

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import dev.maho.browser.bridge.BridgeSync
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

internal fun completeLocalLogout(
    clearSession: () -> Boolean,
    stopSync: () -> Unit,
    signOutCore: () -> Unit,
): Boolean {
    if (!clearSession()) return false
    stopSync()
    signOutCore()
    return true
}

internal fun handleLegacyRelayMigration(
    migrationOutcome: LegacyRelayMigrationOutcome,
    stopSync: () -> Unit,
    signOutCore: () -> Unit,
) {
    if (migrationOutcome == LegacyRelayMigrationOutcome.INVALIDATED_SESSION) {
        stopSync()
        signOutCore()
    }
}



internal fun restorePersistedCoreSession(
    loadSession: () -> RelaySession?,
    signInCore: (RelaySession) -> Unit,
) {
    loadSession()?.let(signInCore)
}

/**
 * Configures core Sync from the device-local seed when one is stored for this
 * account; otherwise (or when core rejects it) falls back to [fetchAndConfigure],
 * which retrieves the relay bootstrap. The relay bootstrap is therefore fetched
 * only after an interactive sign-in (which clears the stored seed first) or when
 * the stored seed is missing.
 */
internal fun configureSyncFromStoredSeed(
    storedSeed: () -> String?,
    applySeed: (String) -> Boolean,
    clearSeed: () -> Unit,
    fetchAndConfigure: () -> Boolean,
): Boolean {
    val seed = storedSeed()
    if (seed != null) {
        if (applySeed(seed)) return true
        clearSeed()
    }
    return fetchAndConfigure()
}

/**
 * Guards persistence of a fetched Sync seed against sign-out. A fetch takes a
 * token before going to the relay; sign-out invalidates every outstanding token
 * and clears storage under the same lock, so a fetch still in flight when the
 * user signs out can never re-save the seed afterwards.
 */
internal class SyncSeedPersistGuard {
    private val lock = Any()
    private var generation = 0L

    fun begin(): Long = synchronized(lock) { generation }

    fun isCurrent(token: Long): Boolean = synchronized(lock) { token == generation }

    /** Runs [save] only if no sign-out happened since [token] was taken. */
    fun persistIfCurrent(token: Long, save: () -> Boolean): Boolean = synchronized(lock) {
        token == generation && save()
    }

    fun <T> invalidate(clear: () -> T): T = synchronized(lock) {
        generation += 1
        clear()
    }
}

/** What a fetched `GET /sync/bootstrap` asks this device to do. */
internal sealed class AccountBootstrapDecision {
    /** Escrow account: configure core from the relay-held seed. */
    data class ConfigureSeed(val seed: String) : AccountBootstrapDecision()

    /** Client-held account this device already joined with the recovery phrase. */
    object JoinedClientHeldRoom : AccountBootstrapDecision()

    /** Client-held account not joined (or joined to another room): ask for the phrase. */
    data class RecoveryPhraseRequired(val requirement: SyncRecoveryRequirement) : AccountBootstrapDecision()

    /** Escrow bootstrap without a seed: protocol error, configure nothing. */
    object Invalid : AccountBootstrapDecision()
}

/**
 * Decides how to configure Sync from a relay bootstrap. A client-held account
 * never gets a generated or uploaded seed: only a recovery-phrase join that
 * derived exactly the account's room ([joinedRoomId]) may sync.
 * [awaitingJoinVerification] is true right after a phrase join, so a room
 * mismatch is reported as a wrong phrase rather than a missing one.
 */
internal fun decideAccountBootstrap(
    bootstrap: RelaySyncBootstrap,
    joinedRoomId: String?,
    awaitingJoinVerification: Boolean,
): AccountBootstrapDecision {
    if (bootstrap.isClientHeld) {
        if (joinedRoomId != null && joinedRoomId == bootstrap.roomId) {
            return AccountBootstrapDecision.JoinedClientHeldRoom
        }
        return AccountBootstrapDecision.RecoveryPhraseRequired(
            if (awaitingJoinVerification) {
                SyncRecoveryRequirement.RECOVERY_PHRASE_MISMATCH
            } else {
                SyncRecoveryRequirement.CLIENT_HELD_BOOTSTRAP
            },
        )
    }
    val seed = bootstrap.seed?.takeIf(String::isNotBlank) ?: return AccountBootstrapDecision.Invalid
    return AccountBootstrapDecision.ConfigureSeed(seed)
}

/** What account-bootstrap configuration may do while a recovery pause exists. */
internal enum class RecoveryGateAction {
    /** No pause: normal stored-seed / bootstrap configuration. */
    PROCEED,
    /** Paused and no phrase join to verify: configure nothing. */
    BLOCKED,
    /** A phrase join awaits verification: re-fetch the bootstrap (never the stored seed), pause kept. */
    VERIFY_JOIN,
}

internal fun recoveryGateAction(
    pause: SyncRecoveryRequirement?,
    awaitingJoinVerification: Boolean,
): RecoveryGateAction = when {
    pause == null -> RecoveryGateAction.PROCEED
    awaitingJoinVerification -> RecoveryGateAction.VERIFY_JOIN
    else -> RecoveryGateAction.BLOCKED
}

/**
 * The persisted recovery pause after a bootstrap decision. Only a verified
 * client-held room match ([AccountBootstrapDecision.JoinedClientHeldRoom])
 * lifts it; a mismatch replaces it, and anything else (fetch failure, escrow
 * seed, invalid response) leaves it untouched, so no sync cycle can run
 * against an unverified phrase-derived room.
 */
internal fun recoveryPauseAfterBootstrap(
    current: SyncRecoveryRequirement?,
    decision: AccountBootstrapDecision,
): SyncRecoveryRequirement? = when (decision) {
    AccountBootstrapDecision.JoinedClientHeldRoom -> null
    is AccountBootstrapDecision.RecoveryPhraseRequired -> decision.requirement
    else -> current
}

internal fun signInCore(session: RelaySession) {
    BridgeSync.signIn(
        email = session.account?.email ?: session.email,
        displayName = session.account?.displayName,
        accessToken = session.accessToken,
        userId = session.account?.id,
        deviceId = session.device?.id,
    )
}

object SyncManager : DefaultLifecycleObserver {
    private const val TAG = "SyncManager"
    private const val DEFAULT_SERVER_URL = "https://relay.mahobrowser.com"

    val isSyncing: Boolean
        get() = lifecycleController?.isRunning == true
    val isSyncEnabled: Boolean
        get() = RelaySessionStore.isSyncEnabled()
    var lastSyncTimestamp: Long? = null
        private set

    @Volatile
    private var appContext: Context? = null
    @Volatile
    private var lifecycleController: SyncLifecycleController? = null
    // Relay cycles are blocking network I/O; they never run on the caller
    // (main) thread, whether triggered by lifecycle, sign-in, or Sync Now.
    private val syncScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val authClient = RelayAuthClient()
    private val seedGuard = SyncSeedPersistGuard()
    var syncErrorListener: ((String) -> Unit)? = null
    /** Set by [recoveryPhraseJoined] until the next bootstrap check verifies the room. */
    @Volatile
    private var awaitingRecoveryJoinVerification = false

    /** Non-null while sync is paused waiting for the user's recovery phrase. */
    val recoveryRequirement: SyncRecoveryRequirement?
        get() = RelaySessionStore.loadSyncRecoveryRequirement()

    fun initialize(
        context: Context,
        stopSync: () -> Unit = { SyncManager.stopSync() },
        signOutCore: () -> Unit = { BridgeSync.signOut() },
    ) {
        appContext = context.applicationContext
        RelaySessionStore.init(context)
        val migrationOutcome = RelaySessionStore.migrateRetiredRelaySession()
        handleLegacyRelayMigration(
            migrationOutcome = migrationOutcome,
            stopSync = stopSync,
            signOutCore = signOutCore,
        )
        lifecycleController = SyncLifecycleController(
            hasSession = RelaySessionStore::hasSession,
            isEnabled = RelaySessionStore::isSyncEnabled,
            persistEnabled = RelaySessionStore::setSyncEnabled,
            restoreCoreAuth = { restorePersistedCoreSession(RelaySessionStore::load, ::signInCore) },
            configureBootstrap = { RelaySessionStore.load()?.let(::configureAccountBootstrap) == true },
            toggleCoreSync = BridgeSync::toggleSync,
            runCycle = ::launchCycle,
            enqueueWork = { ReliableSyncWorker.enqueue(context.applicationContext) },
            cancelTransport = SyncTransport.shared::cancel,
            cancelWork = { ReliableSyncWorker.cancel(context.applicationContext) },
        )
        lifecycleController?.restore()
        // LifecycleRegistry enforces main-thread observer registration;
        // initialize() runs on the hydration (IO) coroutine.
        Handler(Looper.getMainLooper()).post {
            ProcessLifecycleOwner.get().lifecycle.addObserver(this)
        }
    }

    private fun launchCycle() {
        syncScope.launch {
            if (isSyncing) SyncTransport.shared.runBoundedCycle()
        }
    }

    fun getServerUrl(): String {
        return RelaySessionStore.getServerUrl(DEFAULT_SERVER_URL)
    }

    fun updateServerUrl(url: String) {
        RelaySessionStore.updateServerUrl(normalizeRelayBaseUrl(url))
    }

    fun currentSession(): RelaySession? = RelaySessionStore.load()

    fun signUp(serverUrl: String, email: String, password: String): RelaySession {
        val normalized = normalizeRelayBaseUrl(serverUrl)
        val session = authClient.signUp(normalized, email, password).copy(serverUrl = normalized)
        activateSession(session)
        return session
    }

    fun logIn(serverUrl: String, email: String, password: String): RelaySession {
        val normalized = normalizeRelayBaseUrl(serverUrl)
        val session = authClient.logIn(normalized, email, password).copy(serverUrl = normalized)
        activateSession(session)
        return session
    }

    suspend fun signInWithGoogle(
        serverUrl: String,
        serverClientId: String,
        credentials: GoogleCredentialSource,
    ): GoogleSignInOutcome {
        val normalized = normalizeRelayBaseUrl(serverUrl)
        return GoogleSignInCoordinator(
            credentials = credentials,
            exchange = GoogleRelayExchange { _, idToken, nonce, fallbackEmail ->
                authClient.signInWithGoogle(normalized, idToken, nonce, fallbackEmail)
                    .copy(serverUrl = normalized)
            },
            commit = GoogleSessionCommit(::activateSession),
        ).signIn(normalized, serverClientId)
    }

    private fun activateSession(session: RelaySession) {
        check(RelaySessionStore.save(session)) { "Unable to securely save relay session" }
        // Interactive sign-in: drop any stored seed (and any stale recovery
        // pause) so the bootstrap is fetched and evaluated fresh.
        RelaySessionStore.clearSyncSeed()
        RelaySessionStore.saveSyncRecoveryRequirement(null)
        awaitingRecoveryJoinVerification = false
        updateServerUrl(session.serverUrl ?: getServerUrl())
        signInCore(session)
        if (lifecycleController?.onAuthenticated() != true) {
            syncErrorListener?.invoke(recoveryRequirement?.message ?: "Unable to configure account Sync")
        }
    }

    /**
     * Pauses sync until the user enters the account recovery phrase (bootstrap
     * `key_mode: client_held`, or a `410 room_retired` push/pull). Persisted so
     * a relaunch stays paused; the escrow seed is dropped with it.
     */
    fun requireRecoveryPhrase(requirement: SyncRecoveryRequirement) {
        if (!RelaySessionStore.saveSyncRecoveryRequirement(requirement)) {
            Log.w(TAG, "Unable to persist the recovery-phrase requirement")
        }
        syncErrorListener?.invoke(requirement.message)
    }

    /**
     * Call after core joined sync with the user's recovery phrase. The
     * persisted pause stays set (so no worker / foreground cycle can push or
     * pull the phrase-derived room) while the relay bootstrap is re-checked;
     * [configureAccountBootstrapWith] lifts it only when the joined room is the
     * account's client-held room.
     */
    fun recoveryPhraseJoined() {
        awaitingRecoveryJoinVerification = true
        syncScope.launch {
            val controller = lifecycleController ?: return@launch
            if (!controller.isRunning) {
                startSync()
                return@launch
            }
            val verified = RelaySessionStore.load()?.let(::configureAccountBootstrap) == true
            if (verified) {
                SyncTransport.shared.runBoundedCycle()
            }
        }
    }

    fun logout() {
        val session = RelaySessionStore.load()
        if (session != null) {
            try {
                val serverUrl = session.serverUrl ?: getServerUrl()
                authClient.logout(serverUrl, session.accessToken, session.refreshToken)
            } catch (e: Exception) {
                Log.w(TAG, "Relay logout failed: ${e.message}")
            }
        }
        check(completeLocalLogout(
            clearSession = { seedGuard.invalidate(RelaySessionStore::clear) },
            stopSync = ::stopSync,
            signOutCore = { BridgeSync.signOut() },
        )) { "Unable to clear saved relay session" }
        lastSyncTimestamp = null
    }

    fun startSync() {
        if (!RelaySessionStore.hasSession()) {
            syncErrorListener?.invoke("Sign in to enable background sync")
            return
        }
        if (isSyncing) return
        if (lifecycleController?.onAuthenticated() != true) {
            syncErrorListener?.invoke(recoveryRequirement?.message ?: "Unable to configure account Sync")
            return
        }
        Log.i(TAG, "Background sync started")
    }

    /**
     * Restores sync with the stored session. The access token is short-lived and
     * is normally expired by the next app launch, so a 401 refreshes the session
     * once (persisting it and re-authenticating core) and retries; without this the
     * relay rejected every restart and sync silently stayed stopped.
     */
    private fun configureAccountBootstrap(stored: RelaySession): Boolean {
        // Paused for the recovery phrase: never fall through to a bootstrap
        // fetch that could generate and upload a new seed.
        when (recoveryGateAction(recoveryRequirement, awaitingRecoveryJoinVerification)) {
            RecoveryGateAction.BLOCKED -> {
                recoveryRequirement?.let { syncErrorListener?.invoke(it.message) }
                return false
            }
            // Verify the phrase join against the relay; the pause stays set
            // until the decision below confirms the client-held room.
            RecoveryGateAction.VERIFY_JOIN -> return fetchAccountBootstrap(stored)
            RecoveryGateAction.PROCEED -> Unit
        }
        return configureSyncFromStoredSeed(
            storedSeed = { RelaySessionStore.loadSyncSeed(stored.account?.id) },
            applySeed = { seed ->
                BridgeSync.bootstrapConfigured(
                    BridgeSync.configureSyncBootstrap(stored.serverUrl ?: getServerUrl(), seed)
                )
            },
            clearSeed = { RelaySessionStore.clearSyncSeed() },
            fetchAndConfigure = { fetchAccountBootstrap(stored) },
        )
    }

    private fun fetchAccountBootstrap(stored: RelaySession): Boolean {
        val first = configureAccountBootstrapOnce(stored)
        if (first != BootstrapAttempt.UNAUTHORIZED) return first == BootstrapAttempt.CONFIGURED
        val serverUrl = stored.serverUrl ?: getServerUrl()
        val refreshed = try {
            val response = authClient.refresh(serverUrl, stored.refreshToken, stored.email)
            when (val merged = mergeRelayRefreshSession(stored, response)) {
                is RelayRefreshMergeResult.Accepted -> merged.session.copy(serverUrl = serverUrl)
                is RelayRefreshMergeResult.Rejected -> {
                    Log.w(TAG, "Relay session refresh rejected: ${merged.reason}")
                    return false
                }
            }
        } catch (error: Exception) {
            Log.w(TAG, "Relay session refresh failed: ${error.message}")
            return false
        }
        if (!RelaySessionStore.save(refreshed)) {
            Log.w(TAG, "Unable to securely save refreshed relay session")
            return false
        }
        signInCore(refreshed)
        return configureAccountBootstrapOnce(refreshed) == BootstrapAttempt.CONFIGURED
    }

    private enum class BootstrapAttempt { CONFIGURED, UNAUTHORIZED, FAILED }

    private fun configureAccountBootstrapOnce(session: RelaySession): BootstrapAttempt = try {
        if (configureAccountBootstrapWith(session)) BootstrapAttempt.CONFIGURED else BootstrapAttempt.FAILED
    } catch (_: BootstrapUnauthorized) {
        BootstrapAttempt.UNAUTHORIZED
    }

    private class BootstrapUnauthorized : Exception()

    private fun configureAccountBootstrapWith(session: RelaySession): Boolean {
        val fetchToken = seedGuard.begin()
        val serverUrl = session.serverUrl ?: getServerUrl()
        val legacyRoomId = BridgeSync.getSyncRoomId()?.takeIf(String::isNotBlank)
        val bootstrap = try {
            authClient.getSyncBootstrap(serverUrl, session.accessToken)
        } catch (error: RelayApiException) {
            if (error.code == 401) throw BootstrapUnauthorized()
            if (error.code != 404) {
                Log.w(TAG, "Relay Sync bootstrap retrieval failed: ${error.message}")
                return false
            }
            if (legacyRoomId != null) {
                Log.w(TAG, "Existing Recovery Phrase Sync needs explicit migration")
                return false
            }
            val generated = BridgeSync.parseGeneratedSyncBootstrap(BridgeSync.generateSyncBootstrap())
                ?: return false
            try {
                authClient.putSyncBootstrap(serverUrl, session.accessToken, generated)
            } catch (createError: RelayApiException) {
                if (createError.code == 409) {
                    try {
                        authClient.getSyncBootstrap(serverUrl, session.accessToken)
                    } catch (reloadError: Exception) {
                        Log.w(TAG, "Relay Sync bootstrap reload failed: ${reloadError.message}")
                        return false
                    }
                } else {
                    Log.w(TAG, "Relay Sync bootstrap creation failed: ${createError.message}")
                    return false
                }
            } catch (createError: Exception) {
                Log.w(TAG, "Relay Sync bootstrap creation failed: ${createError.message}")
                return false
            }
        } catch (error: Exception) {
            Log.w(TAG, "Relay Sync bootstrap retrieval failed: ${error.message}")
            return false
        }
        if (!seedGuard.isCurrent(fetchToken)) {
            Log.w(TAG, "Signed out during Sync bootstrap fetch; discarding the fetched seed")
            return false
        }
        val decision = decideAccountBootstrap(
            bootstrap = bootstrap,
            joinedRoomId = BridgeSync.getSyncRoomId()?.takeIf(String::isNotBlank),
            awaitingJoinVerification = awaitingRecoveryJoinVerification,
        )
        val verifyingJoin = awaitingRecoveryJoinVerification
        awaitingRecoveryJoinVerification = false
        val pause = recoveryRequirement
        val nextPause = recoveryPauseAfterBootstrap(pause, decision)
        if (nextPause != pause && nextPause == null) {
            if (!RelaySessionStore.saveSyncRecoveryRequirement(null)) {
                Log.w(TAG, "Unable to clear the recovery-phrase requirement")
                return false
            }
        }
        if (pause != null && nextPause != null && decision !is AccountBootstrapDecision.RecoveryPhraseRequired) {
            // Still paused (escrow / invalid bootstrap while verifying a join):
            // configure nothing; a later retry re-verifies.
            awaitingRecoveryJoinVerification = verifyingJoin
            return false
        }
        val seed = when (decision) {
            is AccountBootstrapDecision.ConfigureSeed -> decision.seed
            AccountBootstrapDecision.JoinedClientHeldRoom -> {
                // Core already holds the phrase-derived keys; there is no seed.
                RelaySessionStore.clearSyncSeed()
                return true
            }
            is AccountBootstrapDecision.RecoveryPhraseRequired -> {
                requireRecoveryPhrase(decision.requirement)
                return false
            }
            AccountBootstrapDecision.Invalid -> {
                Log.w(TAG, "Relay Sync bootstrap omitted its seed")
                return false
            }
        }
        val configured = BridgeSync.bootstrapConfigured(
            BridgeSync.configureSyncBootstrap(serverUrl, seed)
        )
        val persisted = configured && seedGuard.persistIfCurrent(fetchToken) {
            RelaySessionStore.saveSyncSeed(seed, session.account?.id)
        }
        if (configured && !persisted) {
            Log.w(TAG, "Unable to securely store the Sync seed; it will be fetched again")
        }
        return configured
    }

    fun stopSync() {
        lifecycleController?.disable()
        Log.i(TAG, "Background sync stopped")
    }

    fun syncNow() {
        if (!isSyncing) return
        launchCycle()
        appContext?.let(ReliableSyncWorker::enqueue)
    }

    internal fun recordSyncSuccess() {
        lastSyncTimestamp = System.currentTimeMillis()
    }

    override fun onStart(owner: LifecycleOwner) {
        lifecycleController?.onForeground()
    }

    override fun onStop(owner: LifecycleOwner) {
        lifecycleController?.onBackground()
    }
}
