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

        // U06a: hydration runs OFF Main. The SQLCipher key must be injected in
        // the same serial chain BEFORE any storage open, and dependent sync
        // setup waits for a Ready publication (never a half-hydrated core).
        applicationScope.launch {
            Trace.beginSection("hydrate")
            try {
                Trace.beginSection("provisionSqlcipherKey")
                MahoBridge.provisionSqlcipherKey()
                Trace.endSection()

                Trace.beginSection("initialize")
                val ready = try {
                    MahoBridge.beginHydration(storagePath)
                } finally {
                    Trace.endSection()
                }
                if (ready) {
                    Trace.beginSection("sync")
                    try {
                        SyncManager.initialize(this@MahoApplication)
                    } finally {
                        Trace.endSection()
                    }
                }
            } finally {
                Trace.endSection()
            }
        }

        applicationScope.launch {
            Trace.beginSection("provisionAssets")
            try {
                AssetProvisioner.provisionAssetsIfNeeded(this@MahoApplication)
            } finally {
                Trace.endSection()
            }
        }
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (level >= TRIM_MEMORY_BACKGROUND) {
            MahoBridge.saveState()
        }
    }
}
