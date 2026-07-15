package dev.maho.browser.ui.webview

import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/**
 * HandleRegistry — thread-safe map from opaque string handles to native
 * session pointers (`Long`). The web bundle only ever sees the string handle;
 * the raw pointer never crosses the JS boundary.
 *
 * A fresh UUID is minted per [register] so handles are unguessable and never
 * collide across chat/agent sessions.
 */
class HandleRegistry {
    private val handles = ConcurrentHashMap<String, Long>()

    /** Registers a pointer and returns its fresh opaque handle. */
    fun register(pointer: Long): String {
        val handle = UUID.randomUUID().toString()
        handles[handle] = pointer
        return handle
    }

    /** Returns the pointer for [handle], or null if unknown. */
    fun resolve(handle: String): Long? = handles[handle]

    /** Removes [handle] and returns its pointer, or null if unknown. */
    fun unregister(handle: String): Long? = handles.remove(handle)
}
