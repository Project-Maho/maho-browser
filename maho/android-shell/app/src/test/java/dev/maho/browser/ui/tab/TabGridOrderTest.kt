package dev.maho.browser.ui.tab

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The deck only composes the cards near the top of the grid, so the tab the user
 * is looking at has to lead. It was last of nine on device, which is why its
 * freshly captured preview was never drawn.
 */
class TabGridOrderTest {

    @Test
    fun putsTheActiveTabFirstSoItsPreviewIsOnScreen() {
        val ordered = orderTabsForDeck(
            tabIds = listOf("a", "b", "c", "d"),
            activeTabId = "d",
        )

        assertEquals(listOf("d", "a", "b", "c"), ordered)
    }

    @Test
    fun keepsTheExistingOrderWhenNoTabIsActive() {
        val ordered = orderTabsForDeck(
            tabIds = listOf("a", "b", "c"),
            activeTabId = null,
        )

        assertEquals(listOf("a", "b", "c"), ordered)
    }

    @Test
    fun keepsTheExistingOrderWhenTheActiveTabIsNotListed() {
        val ordered = orderTabsForDeck(
            tabIds = listOf("a", "b"),
            activeTabId = "missing",
        )

        assertEquals(listOf("a", "b"), ordered)
    }
}
