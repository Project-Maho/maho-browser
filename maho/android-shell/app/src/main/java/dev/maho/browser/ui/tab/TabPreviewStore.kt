package dev.maho.browser.ui.tab

object TabPreviewStore {

    const val MAX_ENTRIES = 24

    // Previews are bitmaps, so the map is access-ordered and bounded to evict the
    // least recently used entry instead of growing with the tab history.
    private val previews = object : LinkedHashMap<String, Any>(MAX_ENTRIES, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Any>?): Boolean =
            size > MAX_ENTRIES
    }

    fun put(tabId: String, preview: Any) {
        synchronized(previews) { previews[tabId] = preview }
    }

    fun get(tabId: String): Any? = synchronized(previews) { previews[tabId] }

    fun remove(tabId: String) {
        synchronized(previews) { previews.remove(tabId) }
    }

    fun size(): Int = synchronized(previews) { previews.size }

    fun clear() {
        synchronized(previews) { previews.clear() }
    }
}
