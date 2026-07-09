package dev.maho.browser.sync

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.util.Log
import dev.maho.browser.bridge.BridgeSync
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.math.min
import kotlin.math.pow

class SyncTransport(
    private val bridgeAdapter: SyncBridgeAdapter = DefaultSyncBridgeAdapter(),
    private val authClient: RelayAuthClient = RelayAuthClient(),
) {
    private val TAG = "SyncTransport"
    private val handler = Handler(Looper.getMainLooper())
    private val executor = Executors.newSingleThreadExecutor()
    private var pollRunnable: Runnable? = null
    private var retryRunnable: Runnable? = null
    private var runningCycle = false

    var isConnected = false
        private set
    private var serverUrl: String? = null
    private var session: RelaySession? = null
    var reconnectAttempts = 0
        private set
    var onError: ((String) -> Unit)? = null
    private var intentionalStop = false
    private val MAX_RECONNECT_ATTEMPTS = 10
    private val POLL_INTERVAL_MS = 30_000L
    private val SYNC_PULL_LIMIT = 100
    private val SEQ_KEY = "SyncTransport.lastServerSeq"
    var lastServerSeq: Long = 0
        private set
    private var appContext: Context? = null

    companion object {
        val shared by lazy { SyncTransport() }
    }

    fun initContext(context: Context) {
        appContext = context.applicationContext
        lastServerSeq = context.applicationContext
            .getSharedPreferences("maho_sync", Context.MODE_PRIVATE)
            .getLong(SEQ_KEY, 0)
        session = RelaySessionStore.load()
    }

    fun start(serverUrl: String, session: RelaySession? = RelaySessionStore.load()) {
        intentionalStop = false
        this.serverUrl = normalizeRelayBaseUrl(serverUrl)
        this.session = session
        reconnectAttempts = 0
        isConnected = true
        scheduleImmediatePoll()
    }

    fun disconnect() {
        intentionalStop = true
        isConnected = false
        pollRunnable?.let { handler.removeCallbacks(it) }
        pollRunnable = null
        retryRunnable?.let { handler.removeCallbacks(it) }
        retryRunnable = null
    }

    fun triggerSync() {
        if (!isConnected) return
        scheduleImmediatePoll()
    }

    private fun scheduleImmediatePoll() {
        if (!isConnected || runningCycle) return
        pollRunnable?.let { handler.removeCallbacks(it) }
        val runnable = Runnable { executor.execute { runSyncCycle() } }
        pollRunnable = runnable
        handler.post(runnable)
    }

    private fun scheduleNextPoll() {
        if (!isConnected) return
        pollRunnable?.let { handler.removeCallbacks(it) }
        val runnable = Runnable { executor.execute { runSyncCycle() } }
        pollRunnable = runnable
        handler.postDelayed(runnable, POLL_INTERVAL_MS)
    }

    private fun runSyncCycle() {
        if (!isConnected || runningCycle) return
        runningCycle = true
        try {
            val currentSession = loadSession() ?: run {
                onError?.invoke("Sign in to enable background sync")
                return
            }
            val baseUrl = resolveBaseUrl(currentSession)
            val outgoingBlob = drainOutgoingBlob()
            if (outgoingBlob != null) {
                performPushWithRefresh(baseUrl, currentSession, outgoingBlob)
            }
            performPullUntilDrained(baseUrl, currentSession)
            reconnectAttempts = 0
            SyncManager.recordSyncSuccess()
            scheduleNextPoll()
        } catch (e: RelayApiException) {
            handleFailure(e.message ?: "Sync request failed")
        } catch (e: Exception) {
            handleFailure(e.message ?: "Sync request failed")
        } finally {
            runningCycle = false
        }
    }

    private fun drainOutgoingBlob(): String? {
        val outgoing = bridgeAdapter.drainOutgoing()?.trim().orEmpty()
        if (outgoing.isBlank() || outgoing == "[]") return null
        return Base64.encodeToString(outgoing.toByteArray(Charsets.UTF_8), Base64.NO_WRAP)
    }

    private fun performPushWithRefresh(baseUrl: String, currentSession: RelaySession, blob: String) {
        try {
            authClient.pushSync(baseUrl, currentSession.accessToken, listOf(blob))
        } catch (e: RelayApiException) {
            if (e.code == 401 && refreshSession()) {
                val refreshedSession = loadSession() ?: throw e
                authClient.pushSync(baseUrl, refreshedSession.accessToken, listOf(blob))
            } else {
                throw e
            }
        }
    }

    private fun performPullUntilDrained(baseUrl: String, currentSession: RelaySession) {
        var sessionToUse = loadSession() ?: currentSession
        while (isConnected) {
            try {
                val response = authClient.pullSync(baseUrl, sessionToUse.accessToken, lastServerSeq, SYNC_PULL_LIMIT)
                applyPulledMessages(response)
                if (!response.hasMore) return
                sessionToUse = loadSession() ?: sessionToUse
            } catch (e: RelayApiException) {
                if (e.code == 401 && refreshSession()) {
                    sessionToUse = loadSession() ?: sessionToUse
                    continue
                }
                throw e
            }
        }
    }

    private fun loadSession(): RelaySession? {
        return session ?: RelaySessionStore.load()
    }

    private fun resolveBaseUrl(currentSession: RelaySession): String {
        return serverUrl
            ?: currentSession.serverUrl
            ?: RelaySessionStore.getServerUrl("https://sync.mahobrowser.com")
    }

    private fun refreshSession(): Boolean {
        val currentSession = loadSession() ?: return false
        val baseUrl = resolveBaseUrl(currentSession)
        return try {
            val refreshed = authClient.refresh(baseUrl, currentSession.refreshToken, currentSession.email)
            val saved = refreshed.copy(serverUrl = baseUrl)
            session = saved
            RelaySessionStore.save(saved)
            if (currentSession.accessToken != saved.accessToken) {
                BridgeSync.signIn(
                    email = saved.account?.email ?: saved.email,
                    displayName = saved.account?.displayName,
                    accessToken = saved.accessToken,
                    userId = saved.account?.id,
                    deviceId = saved.device?.id,
                )
            }
            true
        } catch (e: Exception) {
            Log.e(TAG, "Session refresh failed: ${e.message}")
            false
        }
    }

    private fun applyPulledMessages(response: RelaySyncPullResponse) {
        response.messages.forEach { message ->
            updateLastServerSeq(message.seq)
            val decoded = Base64.decode(message.blob, Base64.DEFAULT)
            val blobStr = String(decoded, Charsets.UTF_8)
            bridgeAdapter.applyRemoteEntities(blobStr)
        }
    }

    private fun handleFailure(message: String) {
        if (intentionalStop) {
            intentionalStop = false
            return
        }
        onError?.invoke(message)
        scheduleRetry()
    }

    private fun updateLastServerSeq(seq: Long) {
        if (seq < lastServerSeq) return
        lastServerSeq = seq
        appContext?.getSharedPreferences("maho_sync", Context.MODE_PRIVATE)
            ?.edit()?.putLong(SEQ_KEY, seq)?.apply()
    }

    private fun scheduleRetry() {
        if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) return
        reconnectAttempts++
        val baseDelay = min(2.0.pow(reconnectAttempts - 1), 60.0)
        val delay = (baseDelay * (0.5 + Math.random() * 0.5) * 1000).toLong()
        retryRunnable?.let { handler.removeCallbacks(it) }
        retryRunnable = Runnable { scheduleImmediatePoll() }
        handler.postDelayed(retryRunnable!!, delay)
    }
}
