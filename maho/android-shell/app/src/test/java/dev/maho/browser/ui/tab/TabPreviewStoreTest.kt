package dev.maho.browser.ui.tab

import androidx.compose.runtime.snapshots.Snapshot
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
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
    fun readingAPreviewIsObservableSoTheTabCardRedrawsWhenOneArrives() {
        TabPreviewStore.put("tab-1", 11)

        val observedReads = mutableListOf<Any>()
        val snapshot = Snapshot.takeMutableSnapshot(readObserver = { observedReads += it })
        try {
            snapshot.enter { TabPreviewStore.get("tab-1") }
            snapshot.apply()
        } finally {
            snapshot.dispose()
        }

        assertTrue(
            "a tab card that reads a preview must register a snapshot read, " +
                "otherwise a later put() never recomposes it",
            observedReads.isNotEmpty(),
        )
    }

    @Test
    fun keepsOnlyTheMostRecentPreviewsSoBitmapsDoNotAccumulate() {
        repeat(TabPreviewStore.MAX_ENTRIES + 3) { index -> TabPreviewStore.put("tab-$index", index) }

        assertEquals(TabPreviewStore.MAX_ENTRIES, TabPreviewStore.size())
        assertNull("the oldest preview should have been evicted", TabPreviewStore.get("tab-0"))
    }
}
