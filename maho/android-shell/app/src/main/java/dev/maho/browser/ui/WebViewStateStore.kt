package dev.maho.browser.ui

import android.os.Bundle
import android.webkit.WebView

/**
 * Per-tab WebView back/forward lists, kept while a tab is off screen because leaving
 * a tab destroys its WebView. Private tabs must never be stored here.
 */
internal object WebViewStateStore {
    const val MAX_ENTRIES = 16

    private val states = object : LinkedHashMap<String, Bundle>(MAX_ENTRIES, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, Bundle>?): Boolean =
            size > MAX_ENTRIES
    }

    fun save(tabId: String, webView: WebView) {
        val state = Bundle()
        if (webView.saveState(state) == null) return
        synchronized(states) { states[tabId] = state }
    }

    fun restore(tabId: String, webView: WebView): Boolean {
        val state = synchronized(states) { states[tabId] } ?: return false
        return webView.restoreState(state) != null
    }

    fun remove(tabId: String) {
        synchronized(states) { states.remove(tabId) }
    }

    fun clear() {
        synchronized(states) { states.clear() }
    }
}
