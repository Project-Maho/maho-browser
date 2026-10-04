package dev.maho.browser.sync

internal class SyncLifecycleController(
    private val hasSession: () -> Boolean,
    private val isEnabled: () -> Boolean,
    private val persistEnabled: (Boolean) -> Boolean,
    private val restoreCoreAuth: () -> Unit = {},
    private val configureBootstrap: () -> Boolean,
    private val toggleCoreSync: () -> Unit,
    private val runCycle: () -> Unit,
    private val enqueueWork: () -> Unit,
    private val cancelTransport: () -> Unit,
    private val cancelWork: () -> Unit,
) {
    var isRunning: Boolean = false
        private set

    fun onAuthenticated(): Boolean = start(persistIntent = true, toggleCore = true)

    fun restore(): Boolean {
        if (!hasSession() || !isEnabled()) return false
        restoreCoreAuth()
        return start(persistIntent = false, toggleCore = true)
    }

    fun disable() {
        val wasRunning = isRunning
        check(persistEnabled(false)) { "Unable to persist disabled sync state" }
        if (wasRunning) toggleCoreSync()
        isRunning = false
        cancelTransport()
        cancelWork()
    }

    fun onBackground() {
        if (!isRunning) return
        cancelTransport()
        enqueueWork()
    }

    fun onForeground() {
        if (isRunning) runCycle()
    }

    private fun start(persistIntent: Boolean, toggleCore: Boolean): Boolean {
        if (!hasSession() || isRunning || !configureBootstrap()) return false
        if (persistIntent && !persistEnabled(true)) return false
        if (toggleCore) toggleCoreSync()
        isRunning = true
        runCycle()
        enqueueWork()
        return true
    }
}
