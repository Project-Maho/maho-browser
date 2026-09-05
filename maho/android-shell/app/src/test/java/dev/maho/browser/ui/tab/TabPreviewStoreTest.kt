package dev.maho.browser.ui.tab

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TabPreviewStoreTest {

    @After
    fun tearDown() {
        TabPreviewStore.clear()
    }

    @Test
    fun returnsThePreviewRecordedForATab() {
        TabPreviewStore.put("tab-1", 11)

        assertEquals(11, TabPreviewStore.get("tab-1"))
    }

    @Test
    fun hasNoPreviewForATabThatWasNeverCaptured() {
        assertNull(TabPreviewStore.get("tab-unknown"))
    }

    @Test
    fun replacesAnEarlierPreviewForTheSameTab() {
        TabPreviewStore.put("tab-1", 11)
        TabPreviewStore.put("tab-1", 22)

        assertEquals(22, TabPreviewStore.get("tab-1"))
    }

    @Test
    fun dropsThePreviewWhenItsTabCloses() {
        TabPreviewStore.put("tab-1", 11)

        TabPreviewStore.remove("tab-1")

        assertNull(TabPreviewStore.get("tab-1"))
    }

    @Test
    fun keepsOnlyTheMostRecentPreviewsSoBitmapsDoNotAccumulate() {
        repeat(TabPreviewStore.MAX_ENTRIES + 3) { index -> TabPreviewStore.put("tab-$index", index) }

        assertEquals(TabPreviewStore.MAX_ENTRIES, TabPreviewStore.size())
        assertNull("the oldest preview should have been evicted", TabPreviewStore.get("tab-0"))
    }
}
