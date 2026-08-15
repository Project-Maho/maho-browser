package dev.maho.browser

import android.app.Application
import android.os.Trace
import dev.maho.browser.support.AssetProvisioner
import dev.maho.browser.sync.SyncManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import java.io.File

class MahoApplication : Application() {
    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    override fun onCreate() {
        super.onCreate()
        val storagePath = File(filesDir, "MahoData").also { it.mkdirs() }.absolutePath
        MahoBridge.appContext = this
        // Inject the SQLCipher key into the process-wide storage key slot BEFORE any
        // SqliteStorage::open (both MahoBridge.initialize -> with_storage and, later,
        // nativeAgentCreateSession). Without this the agent fails with native_error.
        Trace.beginSection("provisionSqlcipherKey")
        MahoBridge.provisionSqlcipherKey()
        Trace.endSection()

        applicationScope.launch {
            Trace.beginSection("provisionAssets")
            try {
                AssetProvisioner.provisionAssetsIfNeeded(this@MahoApplication)
            } finally {
                Trace.endSection()
            }
        }

        Trace.beginSection("initialize")
        MahoBridge.initialize(storagePath)
        Trace.endSection()

        Trace.beginSection("loadState")
        MahoBridge.loadState()
        Trace.endSection()
        SyncManager.initialize(this)
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (level >= TRIM_MEMORY_BACKGROUND) {
            MahoBridge.saveState()
        }
    }
}
