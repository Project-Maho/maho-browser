package dev.maho.browser.sync

import android.content.Context
import androidx.work.CoroutineWorker
import androidx.work.ExistingWorkPolicy
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.concurrent.TimeUnit

internal enum class SyncWorkDisposition {
    SUCCESS,
    RETRY,
    FAILURE,
}

internal fun workDisposition(outcome: SyncCycleOutcome): SyncWorkDisposition = when (outcome) {
    SyncCycleOutcome.SUCCESS -> SyncWorkDisposition.SUCCESS
    SyncCycleOutcome.TRANSIENT_FAILURE -> SyncWorkDisposition.RETRY
    SyncCycleOutcome.AUTH_FAILURE,
    SyncCycleOutcome.PERMANENT_FAILURE,
    -> SyncWorkDisposition.FAILURE
}

internal inline fun runEnabledSyncWork(
    enabled: Boolean,
    runCycle: () -> SyncCycleOutcome,
): SyncWorkDisposition = if (enabled) workDisposition(runCycle()) else SyncWorkDisposition.SUCCESS

class ReliableSyncWorker(
    appContext: Context,
    params: WorkerParameters,
) : CoroutineWorker(appContext, params) {
    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        when (runEnabledSyncWork(RelaySessionStore.isSyncEnabled()) {
            SyncTransport.shared.runBoundedCycle().outcome
        }) {
            SyncWorkDisposition.SUCCESS -> Result.success()
            SyncWorkDisposition.RETRY -> Result.retry()
            SyncWorkDisposition.FAILURE -> Result.failure()
        }
    }

    companion object {
        const val UNIQUE_WORK_NAME = "maho-reliable-sync"

        fun enqueue(context: Context) {
            val request = OneTimeWorkRequestBuilder<ReliableSyncWorker>()
                .setInitialDelay(15, TimeUnit.MINUTES)
                .build()
            WorkManager.getInstance(context).enqueueUniqueWork(
                UNIQUE_WORK_NAME,
                ExistingWorkPolicy.REPLACE,
                request,
            )
        }

        fun cancel(context: Context) {
            WorkManager.getInstance(context).cancelUniqueWork(UNIQUE_WORK_NAME)
        }
    }
}
