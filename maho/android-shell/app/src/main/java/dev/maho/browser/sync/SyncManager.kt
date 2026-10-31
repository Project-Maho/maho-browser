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
        // Interactive sign-in: drop any stored seed so the bootstrap is fetched fresh.
        RelaySessionStore.clearSyncSeed()
        updateServerUrl(session.serverUrl ?: getServerUrl())
        signInCore(session)
        if (lifecycleController?.onAuthenticated() != true) {
            syncErrorListener?.invoke("Unable to configure account Sync")
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
            syncErrorListener?.invoke("Unable to configure account Sync")
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
    private fun configureAccountBootstrap(stored: RelaySession): Boolean =
        configureSyncFromStoredSeed(
            storedSeed = { RelaySessionStore.loadSyncSeed(stored.account?.id) },
            applySeed = { seed ->
                BridgeSync.bootstrapConfigured(
                    BridgeSync.configureSyncBootstrap(stored.serverUrl ?: getServerUrl(), seed)
                )
            },
            clearSeed = { RelaySessionStore.clearSyncSeed() },
            fetchAndConfigure = { fetchAccountBootstrap(stored) },
        )

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
        val configured = BridgeSync.bootstrapConfigured(
            BridgeSync.configureSyncBootstrap(serverUrl, bootstrap.seed)
        )
        val persisted = configured && seedGuard.persistIfCurrent(fetchToken) {
            RelaySessionStore.saveSyncSeed(bootstrap.seed, session.account?.id)
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
