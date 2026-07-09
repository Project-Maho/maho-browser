package dev.maho.browser.sync

import android.content.Context
import android.util.Log
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import dev.maho.browser.bridge.BridgeSync

object SyncManager : DefaultLifecycleObserver {
    private const val TAG = "SyncManager"
    private const val DEFAULT_SERVER_URL = "https://sync.mahobrowser.com"

    var isSyncing = false
        private set
    var lastSyncTimestamp: Long? = null
        private set

    private var appContext: Context? = null
    private val authClient = RelayAuthClient()
    var syncErrorListener: ((String) -> Unit)? = null

    fun initialize(context: Context) {
        appContext = context.applicationContext
        RelaySessionStore.init(context)
        SyncTransport.shared.initContext(context)
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
        RelaySessionStore.save(session)
        updateServerUrl(normalized)
        BridgeSync.signIn(
            email = session.account?.email ?: session.email,
            displayName = session.account?.displayName,
            accessToken = session.accessToken,
            userId = session.account?.id,
            deviceId = session.device?.id,
        )
        return session
    }

    fun logIn(serverUrl: String, email: String, password: String): RelaySession {
        val normalized = normalizeRelayBaseUrl(serverUrl)
        val session = authClient.logIn(normalized, email, password).copy(serverUrl = normalized)
        RelaySessionStore.save(session)
        updateServerUrl(normalized)
        BridgeSync.signIn(
            email = session.account?.email ?: session.email,
            displayName = session.account?.displayName,
            accessToken = session.accessToken,
            userId = session.account?.id,
            deviceId = session.device?.id,
        )
        return session
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
        if (isSyncing) {
            stopSync()
        }
        BridgeSync.signOut()
        RelaySessionStore.clear()
        lastSyncTimestamp = null
    }

    fun startSync() {
        val session = RelaySessionStore.load()
        if (session == null) {
            syncErrorListener?.invoke("Sign in to enable background sync")
            return
        }
        if (isSyncing) return
        BridgeSync.toggleSync()
        isSyncing = true
        SyncTransport.shared.onError = syncErrorListener
        SyncTransport.shared.start(session.serverUrl ?: getServerUrl(), session)
        Log.i(TAG, "Background sync started")
    }

    fun stopSync() {
        if (!isSyncing) return
        BridgeSync.toggleSync()
        isSyncing = false
        SyncTransport.shared.disconnect()
        SyncTransport.shared.onError = null
        Log.i(TAG, "Background sync stopped")
    }

    fun syncNow() {
        SyncTransport.shared.triggerSync()
    }

    internal fun recordSyncSuccess() {
        lastSyncTimestamp = System.currentTimeMillis()
    }

    override fun onStart(owner: LifecycleOwner) {
        if (isSyncing) {
            SyncTransport.shared.triggerSync()
        }
    }

    override fun onStop(owner: LifecycleOwner) {
        if (isSyncing) {
            SyncTransport.shared.disconnect()
        }
    }
}
