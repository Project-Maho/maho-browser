package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.models.SuggestionViewModel
import dev.maho.browser.support.MahoJson

/**
 * Command bar bridge — translates shell-side command bar actions into
 * [ShellEvent]s sent through [MahoBridge], and provides helpers for
 * processing command-bar-related [CoreUpdate]s coming back from the core.
 */
object BridgeCommandBar {

    // ── Lifecycle ──────────────────────────────────────────────────────

    fun notifyOpened(): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CommandBarOpened)

    fun notifyClosed(): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CommandBarClosed)

    // ── Queries ────────────────────────────────────────────────────────

    fun query(text: String, mode: String?): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CommandBarQuery(text = text, mode = mode))

    fun selectItem(index: Int, key: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CommandBarSelect(index = index, key = key))

    fun performAction(action: dev.maho.browser.models.QuickAction): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.CommandBarAction(action = action))

    // ── Search management ──────────────────────────────────────────────

    fun saveSearch(query: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.SaveSearch(query = query))

    fun getRecentSearches(): List<String> {
        val json = MahoBridge.getRecentSearches() ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<String>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun recordUsage(itemKey: String) {
        MahoBridge.recordUsage(itemKey)
    }

    // ── Search engines ─────────────────────────────────────────────────

    fun addSearchEngine(engine: dev.maho.browser.models.SearchEngine): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.AddSearchEngine(engine = engine))

    fun removeSearchEngine(id: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.RemoveSearchEngine(id = id))

    fun setDefaultSearchEngine(id: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.SetDefaultSearchEngine(id = id))

    // ── CoreUpdate helpers ─────────────────────────────────────────────

    /**
     * Returns `true` when the given [update] is related to the command bar.
     */
    fun isCommandBarUpdate(update: CoreUpdate): Boolean = when (update) {
        is CoreUpdate.CommandBarResults -> true
        is CoreUpdate.RecentSearchesUpdated -> true
        is CoreUpdate.SearchEnginesUpdated -> true
        else -> false
    }

    /**
     * Extracts suggestions from a [CoreUpdate.CommandBarResults].
     */
    fun suggestionsFrom(update: CoreUpdate.CommandBarResults): List<SuggestionViewModel> =
        update.suggestions
}
