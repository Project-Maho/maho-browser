package dev.maho.browser.ui.tab

import androidx.compose.runtime.mutableIntStateOf

fun <T> orderTabsForDeck(tabs: List<T>, activeTabId: String?, idOf: (T) -> String): List<T> {
    val active = tabs.firstOrNull { idOf(it) == activeTabId } ?: return tabs
    return listOf(active) + tabs.filterNot { idOf(it) == idOf(active) }
}

fun orderTabsForDeck(tabIds: List<String>, activeTabId: String?): List<String> =
    orderTabsForDeck(tabIds, activeTabId) { it }

object TabPreviewStore {

    const val MAX_ENTRIES = 24

    // Compose only redraws a tab card when the read it made is a snapshot read,
    // so every lookup observes this revision and every write bumps it.
    private val revision = mutableIntStateOf(0)

    // Previews are bitmaps, so the map is access-ordered and bounded to evict the
    // least recently used entry instead of growing with the tab history.
    private val previews = object : LinkedHashMap<String, Any>(MAX_ENTRIES, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Any>?): Boolean =
            size > MAX_ENTRIES
    }

    fun put(tabId: String, preview: Any) {
        synchronized(previews) { previews[tabId] = preview }
        revision.intValue++
    }

    fun get(tabId: String): Any? {
        observeRevision()
        return synchronized(previews) { previews[tabId] }
    }

    private fun observeRevision(): Int = revision.intValue

    fun remove(tabId: String) {
        synchronized(previews) { previews.remove(tabId) }
        revision.intValue++
    }

    fun size(): Int = synchronized(previews) { previews.size }

    fun clear() {
        synchronized(previews) { previews.clear() }
        revision.intValue++
    }
}
