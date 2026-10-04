package dev.maho.browser

import android.content.Context
import android.os.Looper
import dev.maho.browser.support.AssetProvisioner
import dev.maho.browser.sync.SyncManager
import org.robolectric.annotation.Implementation
import org.robolectric.annotation.Implements
import java.io.File
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList

/** One native core with real open/load/free semantics, replacing only the JNI boundary. */
class MemoryThreadStartupFixture {
    internal val load = MemoryThreadGate()
    val destroyed = CompletableFuture<Unit>()
    val syncStarted = CompletableFuture<Unit>()
    val assetsFinished = CompletableFuture<Unit>()
    val events = CopyOnWriteArrayList<String>()
    val mainNativeCalls = CopyOnWriteArrayList<String>()
    @Volatile var loadSucceeds = true
    @Volatile var publishedAtLoad = false
    @Volatile var publishedAtSync = false
    @Volatile var freeWhileHydrating = false
    @Volatile var hydrated = false
    @Volatile var live = false
    @Volatile var freeCount = 0
    private var hydrating = false

    fun recordNative(event: String) {
        events += event
        if (Looper.myLooper() == Looper.getMainLooper()) mainNativeCalls += event
    }

    @Synchronized
    fun open(): Long {
        recordNative("open")
        check(!live) { "duplicate_native_open" }
        live = true
        return 701L
    }

    fun hydrate(ptr: Long): Boolean {
        synchronized(this) {
            check(ptr == 701L && live)
            hydrating = true
            publishedAtLoad = MahoBridge.isInitialized
        }
        recordNative("load")
        try {
            load.park()
            hydrated = loadSucceeds
            return loadSucceeds
        } finally {
            synchronized(this) { hydrating = false }
        }
    }

    @Synchronized
    fun free(ptr: Long) {
        check(ptr == 701L && live) { "invalid_native_free" }
        freeWhileHydrating = hydrating
        live = false
        freeCount++
        events += "free"
        destroyed.complete(Unit)
    }

    fun tabs(ptr: Long): String {
        check(ptr == 701L && live && hydrated) { "read_before_hydration_or_after_free" }
        recordNative("snapshot")
        return """[{"id":"memory-thread-seeded-tab","spaceId":"space-1","title":"Seeded","url":"https://example.com/?memory-thread=seeded","favicon":null,"isLoading":false,"isPinned":false,"isFavorite":false,"isMuted":false,"isPlayingAudio":false,"lifecycleState":"active","children":[],"createdAt":"2024-01-01T00:00:00Z","lastActiveAt":"2024-01-01T00:00:00Z","role":{"type":"normal"}}]"""
    }

    companion object {
        // Replaced before every test; Robolectric sandboxes isolate test classes/processes.
        var current = MemoryThreadStartupFixture()
    }
}

@Implements(value = MahoBridge::class, isInAndroidSdk = false)
class MemoryThreadCoreShadow {
    private val fixture get() = MemoryThreadStartupFixture.current

    @Implementation
    fun provisionSqlcipherKey(): Boolean {
        // Keychain/Keystore boundary: no real credentials are needed for this test.
        fixture.recordNative("key")
        return true
    }

    @Implementation
    fun nativeCreateWithStorage(storagePath: String): Long {
        check(File(storagePath).name == "MahoData")
        return fixture.open()
    }

    @Implementation
    fun nativeLoadState(ptr: Long): Boolean = fixture.hydrate(ptr)

    @Implementation
    fun nativeDestroy(ptr: Long) = fixture.free(ptr)

    @Implementation
    fun nativeGetTabViewModels(ptr: Long): String = fixture.tabs(ptr)

    @Implementation
    fun nativeGetSpaceViewModels(ptr: Long): String {
        check(ptr == 701L && fixture.live && fixture.hydrated)
        return "[]"
    }
}

@Implements(value = SyncManager::class, isInAndroidSdk = false)
class MemoryThreadSyncShadow {
    @Implementation
    fun initialize(context: Context, stopSync: () -> Unit, signOutCore: () -> Unit) {
        val fixture = MemoryThreadStartupFixture.current
        fixture.events += "sync"
        fixture.publishedAtSync = MahoBridge.isInitialized
        fixture.syncStarted.complete(Unit)
    }
}

@Implements(value = AssetProvisioner::class, isInAndroidSdk = false)
class MemoryThreadAssetsShadow {
    @Implementation
    fun provisionAssetsIfNeeded(context: Context, force: Boolean, destDir: File): Boolean {
        MemoryThreadStartupFixture.current.assetsFinished.complete(Unit)
        return false
    }
}
