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

    /**
     * Send a command-bar query event to core.
     *
     * @param text     The search text entered by the user.
     * @param mode     Optional mode string (null = default).
     * @param isIncognito Whether the query is issued from an incognito context.
     */
    fun query(text: String, mode: String? = null, isIncognito: Boolean = false): List<CoreUpdate> =
        MahoBridge.sendEvent(
            ShellEvent.CommandBarQuery(text = text, mode = mode, isIncognito = isIncognito)
        )

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

    /**
     * Convenience: flattens all [CoreUpdate.CommandBarResults] from a list of updates
     * into a single suggestions list. Returns the suggestions from the last
     * [CoreUpdate.CommandBarResults] found, or null if none present.
     */
    fun suggestions(updates: List<CoreUpdate>): List<SuggestionViewModel>? =
        updates.filterIsInstance<CoreUpdate.CommandBarResults>()
            .lastOrNull()
            ?.suggestions
}
