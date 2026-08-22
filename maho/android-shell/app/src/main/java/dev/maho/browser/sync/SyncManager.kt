package dev.maho.browser.sync

import android.content.Context
import android.util.Log
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import dev.maho.browser.bridge.BridgeSync

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

    private var appContext: Context? = null
    private var lifecycleController: SyncLifecycleController? = null
    private val authClient = RelayAuthClient()
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
            runCycle = { SyncTransport.shared.runBoundedCycle() },
            enqueueWork = { ReliableSyncWorker.enqueue(context.applicationContext) },
            cancelTransport = SyncTransport.shared::cancel,
            cancelWork = { ReliableSyncWorker.cancel(context.applicationContext) },
        )
        lifecycleController?.restore()
        ProcessLifecycleOwner.get().lifecycle.addObserver(this)
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
            clearSession = RelaySessionStore::clear,
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

    private fun configureAccountBootstrap(session: RelaySession): Boolean {
        val serverUrl = session.serverUrl ?: getServerUrl()
        val legacyRoomId = BridgeSync.getSyncRoomId()?.takeIf(String::isNotBlank)
        val bootstrap = try {
            authClient.getSyncBootstrap(serverUrl, session.accessToken)
        } catch (error: RelayApiException) {
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
        return BridgeSync.bootstrapConfigured(
            BridgeSync.configureSyncBootstrap(serverUrl, bootstrap.seed)
        )
    }

    fun stopSync() {
        lifecycleController?.disable()
        Log.i(TAG, "Background sync stopped")
    }

    fun syncNow() {
        if (!isSyncing) return
        SyncTransport.shared.runBoundedCycle()
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
