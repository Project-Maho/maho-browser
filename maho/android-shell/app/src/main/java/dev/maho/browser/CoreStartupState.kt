package dev.maho.browser

/**
 * U06 startup phases: Uninitialized -> Loading -> Ready | Failed; any phase
 * -> Closing -> Closed.
 *
 * Ownership invariant: the hydration loader owns the native core pointer
 * privately until publication ([MahoBridge.corePtr] is set only on Ready).
 * A close requested during Loading defers the native reclaim to the loader
 * instead of freeing an executing core.
 */
object CoreStartupState {
    enum class Phase { Uninitialized, Loading, Ready, Failed, Closing, Closed }

    @Volatile
    private var phase: Phase = Phase.Uninitialized

    @Synchronized
    fun phase(): Phase = phase

    /** Single-flight admission; retry after failure is explicit. */
    @Synchronized
    fun beginLoading(): Boolean {
        if (phase != Phase.Uninitialized && phase != Phase.Failed) return false
        phase = Phase.Loading
        return true
    }

    @Synchronized
    fun isClosingOrClosed(): Boolean = phase == Phase.Closing || phase == Phase.Closed

    @Synchronized
    fun markReady() {
        if (phase == Phase.Loading) phase = Phase.Ready
    }

    @Synchronized
    fun markFailed() {
        if (phase == Phase.Loading) phase = Phase.Failed
    }

    /** Returns true when the caller must defer the reclaim to the loader. */
    @Synchronized
    fun requestClose(): Boolean {
        return when (phase) {
            Phase.Loading -> {
                phase = Phase.Closing
                true
            }
            else -> false
        }
    }

    @Synchronized
    fun markClosed() {
        phase = Phase.Closed
    }

    /** Robolectric reuses one sandbox per class; restore the pre-startup phase. */
    internal fun resetForTest() {
        synchronized(this) {
            phase = Phase.Uninitialized
        }
    }
}
