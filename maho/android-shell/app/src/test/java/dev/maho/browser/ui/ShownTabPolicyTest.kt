package dev.maho.browser.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Tapping a favorite owned by another space opens it while the user stays in
 * their space, so the browser must keep showing it instead of falling back to
 * the active space's newest tab.
 */
class ShownTabPolicyTest {

    private val newestInA = ShownTabFacts(id = "T", spaceId = "A", roleType = "normal", lastActiveAt = "2026-10-24T10:00:00Z")
    private val olderInA = ShownTabFacts(id = "U", spaceId = "A", roleType = "normal", lastActiveAt = "2026-10-24T09:00:00Z")
    private val favoriteInB = ShownTabFacts(id = "F", spaceId = "B", roleType = "favorite", lastActiveAt = "2026-10-24T11:00:00Z")
    private val normalInB = ShownTabFacts(id = "N", spaceId = "B", roleType = "normal", lastActiveAt = "2026-10-24T08:00:00Z")
    private val allTabs = listOf(newestInA, olderInA, favoriteInB, normalInB)

    @Test
    fun keepsAFavoriteOwnedByAnotherSpace() {
        assertTrue(canKeepShowingTab(tabSpaceId = "B", tabRoleType = "favorite", activeSpaceId = "A"))
    }

    @Test
    fun dropsARegularTabOwnedByAnotherSpace() {
        assertFalse(canKeepShowingTab(tabSpaceId = "B", tabRoleType = "normal", activeSpaceId = "A"))
        assertFalse(canKeepShowingTab(tabSpaceId = "B", tabRoleType = "pinned", activeSpaceId = "A"))
    }

    @Test
    fun keepsAnyTabInTheActiveSpace() {
        assertTrue(canKeepShowingTab(tabSpaceId = "A", tabRoleType = "normal", activeSpaceId = "A"))
    }

    @Test
    fun dropsARegularTabWhenNoSpaceIsActive() {
        assertFalse(canKeepShowingTab(tabSpaceId = "A", tabRoleType = "normal", activeSpaceId = null))
    }

    @Test
    fun refreshKeepsAFavoriteFromAnotherSpace() {
        assertEquals("F", resolveShownTabId(allTabs, shownTabId = "F", previousSpaceId = "A", activeSpaceId = "A"))
    }

    @Test
    fun refreshDropsANormalTabFromAnotherSpace() {
        assertEquals("T", resolveShownTabId(allTabs, shownTabId = "N", previousSpaceId = "A", activeSpaceId = "A"))
    }

    @Test
    fun refreshKeepsTheShownTabOverANewerTabInTheSameSpace() {
        assertEquals("U", resolveShownTabId(allTabs, shownTabId = "U", previousSpaceId = "A", activeSpaceId = "A"))
    }

    @Test
    fun explicitSpaceSwitchPicksThatSpacesTab() {
        assertEquals("T", resolveShownTabId(allTabs, shownTabId = "F", previousSpaceId = "B", activeSpaceId = "A"))
        assertEquals("T", resolveShownTabId(allTabs, shownTabId = "U", previousSpaceId = "B", activeSpaceId = "A"))
    }

    @Test
    fun closedShownTabFallsBackToTheActiveSpacesNewestTab() {
        assertEquals("T", resolveShownTabId(allTabs, shownTabId = "gone", previousSpaceId = "A", activeSpaceId = "A"))
        assertEquals("T", resolveShownTabId(allTabs, shownTabId = null, previousSpaceId = null, activeSpaceId = "A"))
    }

    @Test
    fun emptySpaceResolvesToNoTab() {
        assertNull(resolveShownTabId(allTabs, shownTabId = "F", previousSpaceId = "A", activeSpaceId = "C"))
    }

    @Test
    fun explicitShowRequestWinsOverTheShownTab() {
        assertEquals("U", resolveShownTabId(allTabs, shownTabId = "F", previousSpaceId = "A", activeSpaceId = "A", requestedTabId = "U"))
        assertEquals("F", resolveShownTabId(allTabs, shownTabId = "T", previousSpaceId = "A", activeSpaceId = "A", requestedTabId = "F"))
    }

    @Test
    fun showRequestForAMissingOrOutOfSpaceTabIsIgnored() {
        assertEquals("F", resolveShownTabId(allTabs, shownTabId = "F", previousSpaceId = "A", activeSpaceId = "A", requestedTabId = "gone"))
        assertEquals("F", resolveShownTabId(allTabs, shownTabId = "F", previousSpaceId = "A", activeSpaceId = "A", requestedTabId = "N"))
    }

    @Test
    fun showRequestIsConsumedOnce() {
        ShownTab.request("U")
        assertEquals("U", ShownTab.takeRequest())
        assertNull(ShownTab.takeRequest())
    }

    @Test
    fun showRequestNotifiesTheBrowserScreen() {
        val signals = mutableListOf<String?>()
        val owner = ShownTab.setSignalListener { signals.add(it) }
        ShownTab.request("T")
        ShownTab.clearSignalListener(owner)
        ShownTab.takeRequest()
        assertEquals(listOf("T"), signals)
    }

    @Test
    fun acceptedRequestWhileHomeSwitchesToBrowsingThatTab() {
        assertEquals(
            ShownTabSignalAction.ShowRequested,
            resolveSignalAction(isBrowsingRoute = false, requestedTabId = "U", resolvedTabId = "U", previousTabId = "T"),
        )
    }

    @Test
    fun rejectedRequestWhileHomeOnlyRefreshes() {
        // The resolver kept the shown favorite instead of the out-of-space request.
        assertEquals(
            ShownTabSignalAction.RefreshOnly,
            resolveSignalAction(isBrowsingRoute = false, requestedTabId = "N", resolvedTabId = "F", previousTabId = "F"),
        )
    }

    @Test
    fun homePublishesNoShownTab() {
        assertNull(publishedShownTabId(isBrowsingRoute = false, shownTabId = "T"))
        assertNull(publishedShownTabId(isBrowsingRoute = false, shownTabId = null))
        assertEquals("T", publishedShownTabId(isBrowsingRoute = true, shownTabId = "T"))
        assertNull(publishedShownTabId(isBrowsingRoute = true, shownTabId = null))
    }

    @Test
    fun externallyClosedShownTabKeepsBrowsingTheResolversChoice() {
        assertEquals(
            ShownTabSignalAction.KeepBrowsing,
            resolveSignalAction(isBrowsingRoute = true, requestedTabId = null, resolvedTabId = "T", previousTabId = "U"),
        )
    }

    @Test
    fun externallyClosedShownTabDropsToHomeWhenNoTabIsLeft() {
        assertEquals(
            ShownTabSignalAction.GoHome,
            resolveSignalAction(isBrowsingRoute = true, requestedTabId = null, resolvedTabId = null, previousTabId = "U"),
        )
    }

    @Test
    fun externalMutationWhileHomeOnlyRefreshes() {
        assertEquals(
            ShownTabSignalAction.RefreshOnly,
            resolveSignalAction(isBrowsingRoute = false, requestedTabId = null, resolvedTabId = "T", previousTabId = null),
        )
        assertEquals(
            ShownTabSignalAction.RefreshOnly,
            resolveSignalAction(isBrowsingRoute = false, requestedTabId = null, resolvedTabId = null, previousTabId = null),
        )
    }

    @Test
    fun resyncSignalNotifiesTheBrowserScreenWithoutAShowIntent() {
        val signals = mutableListOf<String?>()
        val owner = ShownTab.setSignalListener { signals.add(it) }
        ShownTab.requestResync()
        ShownTab.clearSignalListener(owner)
        assertNull(ShownTab.takeRequest())
        assertEquals(listOf<String?>(null), signals)
    }

    @Test
    fun firstOwnerDisposingDoesNotUnsubscribeTheSecond() {
        // A link from another app stacks a second screen over the first.
        val first = mutableListOf<String?>()
        val second = mutableListOf<String?>()
        val a = ShownTab.setSignalListener { first.add(it) }
        val b = ShownTab.setSignalListener { second.add(it) }
        ShownTab.publish(b, "T")
        ShownTab.clearSignalListener(a)
        ShownTab.requestResync()
        assertEquals(listOf<String?>(null), second)
        assertEquals(emptyList<String?>(), first)
        assertEquals("T", ShownTab.current())
        ShownTab.clearSignalListener(b)
    }

    @Test
    fun backFromTheSecondOwnerRestoresTheFirst() {
        val first = mutableListOf<String?>()
        val a = ShownTab.setSignalListener { first.add(it) }
        ShownTab.publish(a, "U")
        val b = ShownTab.setSignalListener { }
        ShownTab.publish(b, "T")
        ShownTab.clearSignalListener(b)
        assertEquals("U", ShownTab.current())
        // The restored screen is told to resync, then receives later signals.
        ShownTab.request("T")
        assertEquals(listOf<String?>(null, "T"), first)
        ShownTab.clearSignalListener(a)
    }

    @Test
    fun lateDisposeFromANonOwnerLeavesTheActiveOwnerIntact() {
        val active = mutableListOf<String?>()
        val stale = ShownTab.setSignalListener { }
        ShownTab.clearSignalListener(stale)
        val owner = ShownTab.setSignalListener { active.add(it) }
        ShownTab.publish(owner, "T")
        ShownTab.clearSignalListener(stale)
        ShownTab.publish(stale, "U")
        ShownTab.requestResync()
        assertEquals(listOf<String?>(null), active)
        assertEquals("T", ShownTab.current())
        ShownTab.clearSignalListener(owner)
    }

    @Test
    fun lastOwnerLeavingDropsThePendingRequestAndShownTab() {
        val owner = ShownTab.setSignalListener { }
        ShownTab.publish(owner, "T")
        ShownTab.request("U")
        ShownTab.clearSignalListener(owner)
        assertNull(ShownTab.takeRequest())
        assertNull(ShownTab.current())
    }

    @Test
    fun forgottenClosedTabIsNotRestoredToAnyOwner() {
        val a = ShownTab.setSignalListener { }
        ShownTab.publish(a, "T")
        val b = ShownTab.setSignalListener { }
        ShownTab.publish(b, "T")
        ShownTab.forget("T")
        assertNull(ShownTab.current())
        ShownTab.clearSignalListener(b)
        assertNull(ShownTab.current())
        ShownTab.clearSignalListener(a)
    }

    @Test
    fun syncPullSignalsResyncOnlyWhenItAppliedChanges() {
        assertTrue(shouldResyncAfterSyncPull(appliedEnvelopeCount = 1))
        assertTrue(shouldResyncAfterSyncPull(appliedEnvelopeCount = 37))
        assertFalse(shouldResyncAfterSyncPull(appliedEnvelopeCount = 0))
    }

    @Test
    fun syncClosedShownTabResyncKeepsBrowsingOrGoesHome() {
        // After a sync pull removed the shown tab, the resync resolves away from it.
        val withoutShown = allTabs.filterNot { it.id == "U" }
        val resolved = resolveShownTabId(withoutShown, shownTabId = "U", previousSpaceId = "A", activeSpaceId = "A")
        assertEquals("T", resolved)
        assertEquals(
            ShownTabSignalAction.KeepBrowsing,
            resolveSignalAction(isBrowsingRoute = true, requestedTabId = null, resolvedTabId = resolved, previousTabId = "U"),
        )
        assertEquals(
            ShownTabSignalAction.GoHome,
            resolveSignalAction(
                isBrowsingRoute = true,
                requestedTabId = null,
                resolvedTabId = resolveShownTabId(emptyList(), "U", "A", "A"),
                previousTabId = "U",
            ),
        )
    }

    @Test
    fun activateMakesThatScreenTheSignalTarget() {
        val first = mutableListOf<String?>()
        val second = mutableListOf<String?>()
        val a = ShownTab.setSignalListener { first.add(it) }
        ShownTab.publish(a, "U")
        val b = ShownTab.setSignalListener { second.add(it) }
        ShownTab.publish(b, "T")
        assertTrue(ShownTab.activate(a))
        assertEquals("U", ShownTab.current())
        ShownTab.request("T")
        assertEquals(listOf<String?>("T"), first)
        assertEquals(emptyList<String?>(), second)
        ShownTab.takeRequest()
        ShownTab.clearSignalListener(b)
        ShownTab.clearSignalListener(a)
    }

    @Test
    fun aBackgroundScreenComingToTheForegroundTakesOver() {
        // Instance B opened from another app; the user returns to A from Recents.
        val a = ShownTab.setSignalListener { }
        ShownTab.publish(a, "U")
        val b = ShownTab.setSignalListener { }
        ShownTab.publish(b, "T")
        assertTrue(ShownTab.activate(a))
        // Publishes from the now-hidden B no longer change the shown tab.
        ShownTab.publish(b, "F")
        assertEquals("U", ShownTab.current())
        // Resuming the screen already on top is not a takeover, and neither is a
        // token that left.
        assertFalse(ShownTab.activate(a))
        ShownTab.clearSignalListener(b)
        assertFalse(ShownTab.activate(b))
        ShownTab.clearSignalListener(a)
    }

    @Test
    fun aSyncPullThatKeptTheShownTabDoesNotSwitchScreens() {
        // A pull that applied only history or bookmarks: the shown tab survives.
        val resolved = resolveShownTabId(allTabs, shownTabId = "U", previousSpaceId = "A", activeSpaceId = "A")
        assertEquals("U", resolved)
        assertEquals(
            ShownTabSignalAction.RefreshOnly,
            resolveSignalAction(isBrowsingRoute = true, requestedTabId = null, resolvedTabId = resolved, previousTabId = "U"),
        )
    }

    @Test
    fun anUnchangedShownTabYieldsNoTransitionAndAChangedOneDoes() {
        assertEquals(
            ShownTabSignalAction.RefreshOnly,
            resolveSignalAction(isBrowsingRoute = true, requestedTabId = null, resolvedTabId = "T", previousTabId = "T"),
        )
        assertEquals(
            ShownTabSignalAction.KeepBrowsing,
            resolveSignalAction(isBrowsingRoute = true, requestedTabId = null, resolvedTabId = "T", previousTabId = "U"),
        )
        assertEquals(
            ShownTabSignalAction.GoHome,
            resolveSignalAction(isBrowsingRoute = true, requestedTabId = null, resolvedTabId = null, previousTabId = "T"),
        )
        // An accepted request still switches even when that tab was already shown.
        assertEquals(
            ShownTabSignalAction.ShowRequested,
            resolveSignalAction(isBrowsingRoute = false, requestedTabId = "T", resolvedTabId = "T", previousTabId = "T"),
        )
    }

    @Test
    fun aUserClosedTabIsForgottenForEveryScreen() {
        // A publishes X and goes to the background; the user closes X on B.
        val restored = mutableListOf<String?>()
        val a = ShownTab.setSignalListener { restored.add(it) }
        ShownTab.publish(a, "X")
        val b = ShownTab.setSignalListener { }
        ShownTab.publish(b, "T")
        val closed = mutableListOf<String>()
        assertEquals("ok", closeTabAndForget("X") { closed.add(it); "ok" })
        assertEquals(listOf("X"), closed)
        assertEquals("T", ShownTab.current())
        // Back from B: A is active again, shows nothing dead, and is told to resync.
        ShownTab.clearSignalListener(b)
        assertNull(ShownTab.current())
        assertEquals(listOf<String?>(null), restored)
        ShownTab.clearSignalListener(a)
    }

    @Test
    fun removingAHiddenScreenDoesNotSignalTheActiveOne() {
        val active = mutableListOf<String?>()
        val a = ShownTab.setSignalListener { }
        val b = ShownTab.setSignalListener { active.add(it) }
        ShownTab.clearSignalListener(a)
        assertEquals(emptyList<String?>(), active)
        ShownTab.clearSignalListener(b)
    }

    @Test
    fun topResumedFocusTakesOverFromApi29AndResumeDoesNot() {
        assertTrue(shouldTakeOverOnForegroundSignal(ForegroundSignal.TopResumedGained, sdkInt = 29))
        assertTrue(shouldTakeOverOnForegroundSignal(ForegroundSignal.TopResumedGained, sdkInt = 35))
        // Both split-screen panes stay resumed: ON_RESUME must not pick the slot owner.
        assertFalse(shouldTakeOverOnForegroundSignal(ForegroundSignal.Resumed, sdkInt = 29))
        assertFalse(shouldTakeOverOnForegroundSignal(ForegroundSignal.Resumed, sdkInt = 35))
    }

    @Test
    fun resumeStillTakesOverBeforeApi29() {
        assertTrue(shouldTakeOverOnForegroundSignal(ForegroundSignal.Resumed, sdkInt = 26))
        assertTrue(shouldTakeOverOnForegroundSignal(ForegroundSignal.Resumed, sdkInt = 28))
        assertFalse(shouldTakeOverOnForegroundSignal(ForegroundSignal.TopResumedGained, sdkInt = 28))
    }

    @Test
    fun focusingTheOlderSplitPaneTakesTheSlotBack() {
        val signals = mutableListOf<String?>()
        val a = ShownTab.setSignalListener { signals.add(it) }
        ShownTab.publish(a, "U")
        val b = ShownTab.setSignalListener { }
        ShownTab.publish(b, "T")
        // The user taps into pane A: the top-resumed signal runs A's takeover.
        if (shouldTakeOverOnForegroundSignal(ForegroundSignal.TopResumedGained, sdkInt = 35) && ShownTab.activate(a)) {
            ShownTab.publish(a, "U")
        }
        assertEquals("U", ShownTab.current())
        ShownTab.requestResync()
        assertEquals(listOf<String?>(null), signals)
        ShownTab.clearSignalListener(b)
        ShownTab.clearSignalListener(a)
    }

    @Test
    fun noOwnWebViewClearsTheAgentPageInsteadOfKeepingAHiddenOne() {
        val calls = mutableListOf<String>()
        takeOverAgentPage<String>(null, attach = { calls.add("attach:$it") }, clear = { calls.add("clear") })
        assertEquals(listOf("clear"), calls)
    }

    @Test
    fun anOwnWebViewIsAttachedOnTakeover() {
        val calls = mutableListOf<String>()
        takeOverAgentPage("own", attach = { calls.add("attach:$it") }, clear = { calls.add("clear") })
        assertEquals(listOf("attach:own"), calls)
    }

    @Test
    fun tabGridReloadsOnARevisionChangeOnly() {
        assertTrue(tabGridNeedsReload(loadedRevision = null, currentRevision = 0))
        assertTrue(tabGridNeedsReload(loadedRevision = 3, currentRevision = 4))
        assertFalse(tabGridNeedsReload(loadedRevision = 4, currentRevision = 4))
    }

    @Test
    fun aTapOnATabThatNoLongerExistsIsIgnored() {
        val live = listOf("T", "U")
        assertTrue(isSelectableTab(live, "T"))
        assertFalse(isSelectableTab(live, "X"))
    }
}
