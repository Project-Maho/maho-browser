package dev.maho.browser

import android.app.Application
import dev.maho.browser.sync.SyncManager
import java.io.File

class MahoApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        val storagePath = File(filesDir, "MahoData").also { it.mkdirs() }.absolutePath
        MahoBridge.appContext = this
        // Inject the SQLCipher key into the process-wide storage key slot BEFORE any
        // SqliteStorage::open (both MahoBridge.initialize -> with_storage and, later,
        // nativeAgentCreateSession). Without this the agent fails with native_error.
        MahoBridge.provisionSqlcipherKey()
        MahoBridge.initialize(storagePath)
        MahoBridge.loadState()
        SyncManager.initialize(this)
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (level >= TRIM_MEMORY_BACKGROUND) {
            MahoBridge.saveState()
        }
    }
}
