package dev.maho.browser.bridge

import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/**
 * Thread-safe registry that maps UUID string handles to native session pointers.
 *
 * The [nativeFree] callback is invoked OUTSIDE any internal state mutation — ConcurrentHashMap's
 * [remove] is atomic, so the pointer is extracted from the map before the free call.  This means
 * the lock is never held across an FFI boundary, eliminating the risk of a deadlock if Rust's
 * drop logic re-enters the JVM (e.g. via a callback or another JNI frame).
 *
 * Constructor-injected [nativeFree] keeps the class testable without loading the JNI library.
 * At the production wiring site, pass [MahoBridge::chatSessionFree].
 */
class SessionRegistry(
    private val nativeFree: (Long) -> Unit,
) {
    private val sessions = ConcurrentHashMap<String, Long>()

    init {
        ActiveRegistries.add(this)
    }

    /** Stores [ptr] in the registry and returns the newly allocated UUID handle. */
    fun register(ptr: Long): String {
        val id = UUID.randomUUID().toString()
        sessions[id] = ptr
        return id
    }

    /** Returns the native pointer for [id], or `null` if [id] is not registered. */
    fun get(id: String): Long? = sessions[id]

    /**
     * Removes the entry for [id] and frees the underlying native session.
     *
     * Idempotent: a call with an unknown or already-released [id] is a no-op.
     *
     * SAFETY: [ConcurrentHashMap.remove] atomically extracts the pointer before
     * [nativeFree] is called, so the free happens with no lock held.
     */
    fun release(id: String) {
        sessions.remove(id)?.let { ptr -> nativeFree(ptr) }
    }

    /**
     * Releases all registered sessions.
     *
     * Snapshot-and-clear ensures that any concurrent [register] calls that arrive
     * after [clear] will not have their pointers silently leaked: they remain in
     * the map and must be released independently.
     */
    fun releaseAll() {
        val snapshot = sessions.toMap()
        sessions.clear()
        snapshot.values.forEach { ptr -> nativeFree(ptr) }
    }

    /**
     * Finds and removes the entry whose stored pointer equals [ptr], then frees it.
     *
     * Use this when only the raw pointer is available (e.g. a normal JS-initiated
     * `chatSessionFree` call).  The entry is removed atomically before [nativeFree]
     * is invoked, so a concurrent [releaseAll] cannot double-free the same session.
     *
     * Idempotent: if [ptr] is not in the registry this is a no-op.
     */
    fun releaseByPointer(ptr: Long) {
        val id = sessions.entries.firstOrNull { it.value == ptr }?.key ?: return
        sessions.remove(id)?.let { nativeFree(it) }
    }

    /** Removes this instance from the global tracking set. Call when the owning bridge is torn down. */
    fun untrack() {
        ActiveRegistries.remove(this)
    }

    /**
     * Process-wide set of live [SessionRegistry] instances.
     *
     * Populated automatically on construction; trimmed on [untrack].
     * [drainAll] iterates a snapshot so concurrent registration/removal is safe.
     */
    companion object ActiveRegistries {
        private val active = ConcurrentHashMap.newKeySet<SessionRegistry>()

        internal fun add(registry: SessionRegistry) {
            active.add(registry)
        }

        internal fun remove(registry: SessionRegistry) {
            active.remove(registry)
        }

        fun drainAll() {
            val snapshot = active.toSet()
            snapshot.forEach { it.releaseAll() }
        }
    }
}
