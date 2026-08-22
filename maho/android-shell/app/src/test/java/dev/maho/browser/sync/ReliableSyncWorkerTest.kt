package dev.maho.browser.sync

import org.junit.Assert.assertEquals
import org.junit.Test

class ReliableSyncWorkerTest {
    @Test
    fun disabledSyncSkipsStaleQueuedWorker() {
        assertEquals(
            SyncWorkDisposition.SUCCESS,
            runEnabledSyncWork(enabled = false) {
                error("transport must not run")
            },
        )
    }

    @Test
    fun enabledSyncRunsQueuedWorker() {
        assertEquals(
            SyncWorkDisposition.RETRY,
            runEnabledSyncWork(enabled = true) { SyncCycleOutcome.TRANSIENT_FAILURE },
        )
    }

    @Test
    fun mapsTransportOutcomesToBoundedWorkManagerResults() {
        assertEquals(
            SyncWorkDisposition.SUCCESS,
            workDisposition(SyncCycleOutcome.SUCCESS),
        )
        assertEquals(
            SyncWorkDisposition.RETRY,
            workDisposition(SyncCycleOutcome.TRANSIENT_FAILURE),
        )
        assertEquals(
            SyncWorkDisposition.FAILURE,
            workDisposition(SyncCycleOutcome.AUTH_FAILURE),
        )
        assertEquals(
            SyncWorkDisposition.FAILURE,
            workDisposition(SyncCycleOutcome.PERMANENT_FAILURE),
        )
    }
}
