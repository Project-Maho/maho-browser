package dev.maho.browser.ui

import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.TabId
import java.util.concurrent.atomic.AtomicReference

/**
 * Whether the browser may keep showing a tab after it resyncs with core.
 * A tab in the active space stays shown. A favorite stays shown whichever space
 * owns it, because favorites are profile-wide and opening one must not move the
 * user out of the space they are in.
 */
fun canKeepShowingTab(tabSpaceId: SpaceId, tabRoleType: String, activeSpaceId: SpaceId?): Boolean =
    tabSpaceId == activeSpaceId || tabRoleType == "favorite"

data class ShownTabFacts(
    val id: TabId,
    val spaceId: SpaceId,
    val roleType: String,
    val lastActiveAt: String,
)

/**
 * The single definition of "the tab currently shown" after a resync with core.
 *
 * An explicit show request (an agent `open_tab`, for example) wins when the
 * requested tab exists and [canKeepShowingTab] allows it. Otherwise the shown
 * tab survives the resync while it still exists, the user has not switched
 * space, and [canKeepShowingTab] allows it (so a favorite owned by another space
 * stays). Otherwise, for example after a space switch or when the shown tab was
 * closed, core's choice wins: the most recently active tab in the active space.
 */
fun resolveShownTabId(
    tabs: List<ShownTabFacts>,
    shownTabId: TabId?,
    previousSpaceId: SpaceId?,
    activeSpaceId: SpaceId?,
    requestedTabId: TabId? = null,
): TabId? {
    val requested = tabs.firstOrNull { it.id == requestedTabId }
    if (requested != null && canKeepShowingTab(requested.spaceId, requested.roleType, activeSpaceId)) {
        return requested.id
    }
    val shown = tabs.firstOrNull { it.id == shownTabId }
    if (shown != null &&
        previousSpaceId == activeSpaceId &&
        canKeepShowingTab(shown.spaceId, shown.roleType, activeSpaceId)
    ) {
        return shown.id
    }
    return newestTabIdInSpace(tabs, activeSpaceId)
}

fun newestTabIdInSpace(tabs: List<ShownTabFacts>, activeSpaceId: SpaceId?): TabId? =
    tabs.filter { activeSpaceId == null || it.spaceId == activeSpaceId }
        .maxByOrNull { it.lastActiveAt }
        ?.id

/**
 * What the shell must do after [ShownTab] signals a change from outside the UI.
 */
enum class ShownTabSignalAction {
    /** An accepted show request: switch to the browsing screen for that tab. */
    ShowRequested,
    /** The resolver picked a different tab than the screen showed: browse that tab. */
    KeepBrowsing,
    /** The shown tab vanished with no replacement: drop to Home, as dismissing the tab grid does. */
    GoHome,
    /** Nothing to navigate: the screen only refreshes its state. */
    RefreshOnly,
}

/**
 * The single definition of how the screen reacts to a [ShownTab] signal.
 *
 * [requestedTabId] is the show intent carried by the signal, if any, and
 * [resolvedTabId] is what [resolveShownTabId] picked on the refresh that the
 * signal triggered. A request is accepted exactly when the resolver picked the
 * requested tab; an accepted request always switches from Home to the browsing
 * screen for that tab. A rejected request never navigates. Without a request,
 * only the browsing route transitions, and only when the shown tab changed:
 * to the resolver's choice when it differs from [previousTabId] (the tab shown
 * before the refresh), or to Home when no tab is left (the same recovery as
 * dismissing the tab grid). A refresh that kept the shown tab (a sync pull that
 * only changed history or bookmarks, for example) leaves the screen alone, so
 * an open tab grid or a hidden bottom bar is never reset under the user.
 */
fun resolveSignalAction(
    isBrowsingRoute: Boolean,
    requestedTabId: TabId?,
    resolvedTabId: TabId?,
    previousTabId: TabId?,
): ShownTabSignalAction = when {
    requestedTabId != null && resolvedTabId == requestedTabId -> ShownTabSignalAction.ShowRequested
    requestedTabId != null -> ShownTabSignalAction.RefreshOnly
    isBrowsingRoute && resolvedTabId != null && resolvedTabId != previousTabId -> ShownTabSignalAction.KeepBrowsing
    isBrowsingRoute && resolvedTabId != null -> ShownTabSignalAction.RefreshOnly
    isBrowsingRoute -> ShownTabSignalAction.GoHome
    else -> ShownTabSignalAction.RefreshOnly
}

/**
 * What the process-wide handle publishes. While Home is on screen no tab is
 * shown, so agent page tools must see "no page" instead of a hidden tab.
 */
fun publishedShownTabId(isBrowsingRoute: Boolean, shownTabId: TabId?): TabId? =
    if (isBrowsingRoute) shownTabId else null

/** A foreground signal a browser screen can receive. */
enum class ForegroundSignal {
    /** Lifecycle ON_RESUME. */
    Resumed,
    /** Activity.onTopResumedActivityChanged(true): this window got the user's focus. */
    TopResumedGained,
}

/**
 * Whether [signal] means the user is now working in this screen, so it must
 * take the [ShownTab] slot and the agent page back. From API 29 every visible
 * split-screen or freeform pane stays RESUMED, so ON_RESUME does not mark the
 * focused pane (a pane that resumes beside the focused one must not steal the
 * slot); only the top-resumed change does. Before API 29 only the focused pane
 * is resumed and the top-resumed callback does not exist, so ON_RESUME does.
 */
fun shouldTakeOverOnForegroundSignal(signal: ForegroundSignal, sdkInt: Int): Boolean = when (signal) {
    ForegroundSignal.Resumed -> sdkInt < TOP_RESUMED_MIN_SDK
    ForegroundSignal.TopResumedGained -> sdkInt >= TOP_RESUMED_MIN_SDK
}

const val TOP_RESUMED_MIN_SDK = 29

/**
 * On takeover the agent page becomes this screen's own WebView. With none (Home
 * is shown, so no WebView is composed) the slot is cleared instead of being left
 * on another, hidden screen's live WebView, which DOM tools would act on.
 */
fun <W : Any> takeOverAgentPage(ownWebView: W?, attach: (W) -> Unit, clear: () -> Unit) {
    if (ownWebView != null) attach(ownWebView) else clear()
}

/**
 * Whether the open tab grid must reload its tabs: the browser bumps the revision
 * after every resync (a remote close or an agent `close_tab`), and the grid keeps
 * the revision it last loaded.
 */
fun tabGridNeedsReload(loadedRevision: Int?, currentRevision: Int): Boolean =
    loadedRevision != currentRevision

/**
 * Whether a tapped card may become the shown tab: a card can outlive its tab
 * (closed remotely or by an agent), and a dead id must never be shown.
 */
fun isSelectableTab(existingTabIds: Collection<TabId>, tabId: TabId): Boolean = tabId in existingTabIds

/**
 * Whether a sync pull must signal the browser screen to resync. Applied
 * envelopes can close or move the shown tab in core (a remote tab tombstone,
 * for example), and nothing else tells the screen; a pull that applied nothing
 * changed nothing.
 */
fun shouldResyncAfterSyncPull(appliedEnvelopeCount: Int): Boolean = appliedEnvelopeCount > 0

/**
 * The single close path for a tab, used by every caller (the tab grid, Close
 * all, the tab sheet and agent `close_tab`): [close] removes it from core and
 * [ShownTab.forget] then drops it from every owner, active or not, so no screen
 * restored later and no tool call resolves the dead id.
 */
fun <T> closeTabAndForget(tabId: TabId, close: (TabId) -> T): T {
    val result = close(tabId)
    ShownTab.forget(tabId)
    return result
}

/**
 * Process-wide handle on the shown tab for code outside the browser screen
 * (agent tools run on a non-UI thread). Each live browser screen registers as
 * an owner and gets an opaque token. Several screens can be alive at once (a
 * second activity instance opened from another app, split-screen, freeform),
 * so owners form a stack: the top owner receives signals and its published tab
 * is the shown tab. A new owner starts on top, and a screen the user returns to
 * (from Recents, the launcher or the other split-screen pane) moves back on top
 * through [activate]. When an owner leaves, only its own entry is removed and
 * the previous owner, with the tab it last published, becomes active again and
 * is told to resync; a late or repeated removal never touches another owner.
 */
object ShownTab {
    private class Owner(val listener: (TabId?) -> Unit) {
        var published: TabId? = null
    }

    private val lock = Any()
    private val owners = ArrayList<Owner>()

    @Volatile
    private var shown: TabId? = null
    private val requested = AtomicReference<TabId?>(null)

    fun current(): TabId? = shown

    /**
     * Records the tab [owner] shows. It becomes the process-wide shown tab only
     * while [owner] is the active owner; a token that no longer owns a slot is
     * ignored.
     */
    fun publish(owner: Any, tabId: TabId?) {
        synchronized(lock) {
            val entry = owners.firstOrNull { it === owner } ?: return
            entry.published = tabId
            if (owners.last() === entry) shown = tabId
        }
    }

    /**
     * The tab [tabId] was closed (by the user or an agent): no owner, active
     * or not, may keep reporting it, so later tool calls never resolve the
     * dead id and a restored owner never republishes it.
     */
    fun forget(tabId: TabId) {
        synchronized(lock) {
            owners.forEach { if (it.published == tabId) it.published = null }
            if (shown == tabId) shown = null
        }
    }

    private fun activeListener(): ((TabId?) -> Unit)? =
        synchronized(lock) { owners.lastOrNull()?.listener }

    /**
     * An explicit "show this tab" intent (an agent `open_tab`, for example).
     * The listener receives the requested id so the screen can switch from
     * Home to the browsing screen when the resolver accepts the request.
     */
    fun request(tabId: TabId) {
        requested.set(tabId)
        activeListener()?.invoke(tabId)
    }

    /**
     * A "state changed, refresh now" signal for any tab mutation from outside
     * the UI (an agent `close_tab`, for example). Carries no show intent.
     */
    fun requestResync() {
        activeListener()?.invoke(null)
    }

    fun takeRequest(): TabId? = requested.getAndSet(null)

    /**
     * Registers a browser screen as the active owner and returns its token.
     * Pass the token to [publish] and [clearSignalListener].
     */
    fun setSignalListener(listener: (TabId?) -> Unit): Any {
        val owner = Owner(listener)
        synchronized(lock) {
            owners.add(owner)
            shown = null
        }
        return owner
    }

    /**
     * The screen behind [token] came to the foreground. Moves it on top, so it
     * receives signals and its published tab is the shown tab, and returns
     * whether it took over from another owner (the caller then resyncs, since
     * signals went elsewhere while it was not on top). Returns false when it
     * was already on top or no longer owns a slot.
     */
    fun activate(token: Any): Boolean = synchronized(lock) {
        val entry = owners.firstOrNull { it === token } ?: return@synchronized false
        if (owners.last() === entry) return@synchronized false
        owners.remove(entry)
        owners.add(entry)
        shown = entry.published
        true
    }

    /**
     * Removes [token]'s registration only. If it was the active owner, the
     * previous owner becomes active again, its last published tab is the shown
     * tab, and it receives a resync, because signals reached the removed owner
     * meanwhile (its shown tab may have been closed). When the last owner
     * leaves, nothing is shown and a pending show request is dropped, so it can
     * never fire on an unrelated refresh.
     */
    fun clearSignalListener(token: Any) {
        val restored = synchronized(lock) {
            val wasActive = owners.lastOrNull() === token
            if (!owners.removeIf { it === token }) return
            val active = owners.lastOrNull()
            shown = active?.published
            if (active == null) requested.set(null)
            active?.listener?.takeIf { wasActive }
        }
        restored?.invoke(null)
    }
}
