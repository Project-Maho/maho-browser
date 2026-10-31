package dev.maho.browser.ui.webview

import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger

/**
 * HandleRegistry — thread-safe map from opaque string handles to native
 * session pointers (`Long`). The web bundle only ever sees the string handle;
 * the raw pointer never crosses the JS boundary.
 *
 * Provides safe borrowing (`withPointer`) with active lease reference counting
 * so `destroy()` waits for in-flight native calls to complete before freeing pointers,
 * eliminating use-after-free races.
 */
class HandleRegistry {
    internal enum class WaitOperation { Unregister, Close }

    // Observation only: called under the monitor immediately before an actual wait.
    internal var onWaitForTesting: ((WaitOperation) -> Unit)? = null

    private val handles = ConcurrentHashMap<String, Long>()
    private val activeLeases = ConcurrentHashMap<String, AtomicInteger>()
    private val lock = Any()
    @Volatile
    private var closed = false

    /** Registers a pointer and returns its fresh opaque handle, or null if closed. */
    fun register(pointer: Long): String? = synchronized(lock) {
        if (closed) return null
        val handle = UUID.randomUUID().toString()
        handles[handle] = pointer
        activeLeases[handle] = AtomicInteger(0)
        handle
    }

    /**
     * Executes [block] with the resolved native pointer while holding an active lease.
     * Prevents [closeAndDrain] from returning/freeing the pointer until [block] completes.
     * Throws [IllegalArgumentException] if [handle] is unknown or registry is closed.
     */
    fun <T> withPointer(handle: String, block: (Long) -> T): T {
        val ptr: Long
        val counter: AtomicInteger
        synchronized(lock) {
            if (closed) throw IllegalStateException("Registry is closed")
            ptr = handles[handle] ?: throw IllegalArgumentException("Unknown handle: $handle")
            counter = activeLeases[handle] ?: throw IllegalArgumentException("Unknown handle: $handle")
            counter.incrementAndGet()
        }
        try {
            return block(ptr)
        } finally {
            counter.decrementAndGet()
            synchronized(lock) {
                (lock as java.lang.Object).notifyAll()
            }
        }
    }

    /** Removes [handle] and returns its pointer, or null if unknown. */
    fun unregister(handle: String): Long? = synchronized(lock) {
        val counter = activeLeases[handle]
        var interrupted = false
        if (counter != null) {
            while (counter.get() > 0) {
                onWaitForTesting?.invoke(WaitOperation.Unregister)
                try {
                    (lock as java.lang.Object).wait(100)
                } catch (_: InterruptedException) {
                    interrupted = true
                }
            }
        }
        if (interrupted) {
            Thread.currentThread().interrupt()
        }
        activeLeases.remove(handle)
        handles.remove(handle)
    }

    /**
     * Atomically closes the registry, blocks new registrations/borrows,
     * waits for any in-flight borrows to finish, and returns all drained handles.
     */
    fun closeAndDrain(): Map<String, Long> = synchronized(lock) {
        closed = true
        var interrupted = false
        for (counter in activeLeases.values) {
            while (counter.get() > 0) {
                onWaitForTesting?.invoke(WaitOperation.Close)
                try {
                    (lock as java.lang.Object).wait(100)
                } catch (_: InterruptedException) {
                    interrupted = true
                }
            }
        }
        if (interrupted) {
            Thread.currentThread().interrupt()
        }
        activeLeases.clear()
        val snapshot = HashMap(handles)
        handles.clear()
        snapshot
    }
}
