package dev.maho.browser.bridge

import android.os.Handler
import android.os.Looper
import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.support.MahoJson

fun MahoBridge.sendEvent(event: ShellEvent): List<CoreUpdate> {
    val eventJson = MahoJson.instance.encodeToString(ShellEvent.serializer(), event)
    val responseJson = handleEvent(eventJson) ?: return emptyList()
    return try {
        MahoJson.instance.decodeFromString<List<CoreUpdate>>(responseJson)
    } catch (_: Exception) {
        emptyList()
    }
}

/**
 * Dispatches a batch of events sequentially and aggregates all CoreUpdates.
 */
fun MahoBridge.sendEvents(events: List<ShellEvent>): List<CoreUpdate> {
    if (events.isEmpty()) return emptyList()
    val updates = mutableListOf<CoreUpdate>()
    for (event in events) {
        updates.addAll(sendEvent(event))
    }
    return updates
}

inline fun <reified T> MahoBridge.decodeResponse(json: String?): T? {
    if (json == null) return null
    return try {
        MahoJson.instance.decodeFromString<T>(json)
    } catch (_: Exception) {
        null
    }
}

/**
 * Thread-safe throttler / coalescer for high-frequency bridge event calls.
 */
class EventThrottler(
    private val intervalMs: Long = 16L,
    private val handler: Handler = Handler(Looper.getMainLooper()),
) {
    private var pendingRunnable: Runnable? = null
    private var lastRunTime = 0L

    fun throttle(action: () -> Unit) {
        pendingRunnable?.let { handler.removeCallbacks(it) }
        val now = System.currentTimeMillis()
        val timeSinceLast = now - lastRunTime

        if (timeSinceLast >= intervalMs) {
            lastRunTime = now
            action()
        } else {
            val delay = intervalMs - timeSinceLast
            val runnable = Runnable {
                lastRunTime = System.currentTimeMillis()
                action()
            }
            pendingRunnable = runnable
            handler.postDelayed(runnable, delay)
        }
    }

    fun cancel() {
        pendingRunnable?.let { handler.removeCallbacks(it) }
        pendingRunnable = null
    }
}
