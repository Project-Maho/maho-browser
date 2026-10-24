package dev.maho.browser.bridge

import android.app.DownloadManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.Uri
import androidx.core.content.ContextCompat
import dev.maho.browser.MahoBridge
import java.util.concurrent.ConcurrentHashMap

/**
 * Records system DownloadManager transfers in core so the Downloads screen lists
 * them. DownloadManager runs in its own process; its completion broadcast is the
 * only signal this app gets, so the enqueue id is mapped to the core download id.
 */
object SystemDownloadTracker {
    private val coreIdBySystemId = ConcurrentHashMap<Long, String>()
    @Volatile private var registered = false

    fun enqueue(
        context: Context,
        request: DownloadManager.Request,
        url: String,
        filename: String,
        mimeType: String,
    ): Long? {
        val dm = context.getSystemService(Context.DOWNLOAD_SERVICE) as? DownloadManager ?: return null
        register(context.applicationContext)
        val systemId = dm.enqueue(request)
        MahoBridge.startDownload(url, filename, mimeType)?.let { coreIdBySystemId[systemId] = it }
        return systemId
    }

    /** Stops the system transfer backing [coreId], if it is still in flight. */
    fun cancel(coreId: String) {
        val systemId = coreIdBySystemId.entries.firstOrNull { it.value == coreId }?.key ?: return
        coreIdBySystemId.remove(systemId)
        val context = MahoBridge.appContext ?: return
        (context.getSystemService(Context.DOWNLOAD_SERVICE) as? DownloadManager)?.remove(systemId)
    }

    private fun register(context: Context) {
        if (registered) return
        synchronized(this) {
            if (registered) return
            ContextCompat.registerReceiver(
                context,
                object : BroadcastReceiver() {
                    override fun onReceive(ctx: Context, intent: Intent) {
                        val systemId = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1L)
                        val coreId = coreIdBySystemId.remove(systemId) ?: return
                        finish(ctx, systemId, coreId)
                    }
                },
                IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE),
                ContextCompat.RECEIVER_EXPORTED,
            )
            registered = true
        }
    }

    private fun finish(context: Context, systemId: Long, coreId: String) {
        val dm = context.getSystemService(Context.DOWNLOAD_SERVICE) as? DownloadManager ?: return
        dm.query(DownloadManager.Query().setFilterById(systemId)).use { cursor ->
            if (cursor == null || !cursor.moveToFirst()) {
                // The user removed it from the system list before it finished.
                MahoBridge.cancelDownload(coreId)
                return
            }
            val status = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS))
            if (status == DownloadManager.STATUS_SUCCESSFUL) {
                val localUri = cursor.getString(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_LOCAL_URI))
                val path = localUri?.let { Uri.parse(it).path }.orEmpty()
                val total = cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES))
                MahoBridge.completeDownload(coreId, path, total)
            } else {
                val reason = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON))
                MahoBridge.failDownload(coreId, "DownloadManager status=$status reason=$reason")
            }
        }
    }
}
