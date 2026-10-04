package dev.maho.browser

import android.app.Application
import android.content.Context
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode
import org.robolectric.util.ReflectionHelpers
import java.util.concurrent.CompletableFuture

@RunWith(RobolectricTestRunner::class)
@Config(
    sdk = [33],
    application = Application::class,
    shadows = [MemoryThreadCoreShadow::class, MemoryThreadSyncShadow::class, MemoryThreadAssetsShadow::class],
)
@LooperMode(LooperMode.Mode.INSTRUMENTATION_TEST)
class MemoryThreadStartupTest {
    private lateinit var fixture: MemoryThreadStartupFixture
    private var startup: CompletableFuture<MahoApplication>? = null

    @Before
    fun setUp() {
        fixture = MemoryThreadStartupFixture()
        MemoryThreadStartupFixture.current = fixture
        // Robolectric reuses one sandbox per test class; restore the pre-startup
        // phase so each method starts Uninitialized.
        CoreStartupState.resetForTest()
        assertFalse("test must begin without a published core", MahoBridge.isInitialized)
    }

    private fun start() {
        startup = memoryThreadMain {
            val application = MahoApplication()
            ReflectionHelpers.callInstanceMethod<Unit>(application, "attach",
                ReflectionHelpers.ClassParameter.from(Context::class.java,
                    RuntimeEnvironment.getApplication().baseContext))
            application.onCreate()
            application
        }
        fixture.load.entered.await()
    }

    @After
    fun tearDown() {
        fixture.load.release.complete(Unit)
        startup?.await()
        fixture.assetsFinished.await()
        memoryThreadMain { MahoBridge.destroy() }.await()
        MahoBridge.appContext = null
    }

    @Test
    fun loadingUiDoesNotCallCore() {
        // Given: native hydration signals from inside its boundary and remains parked.
        start()

        // When: a Main-loop marker reads public readiness while storage is unavailable.
        val readySeenByUi = memoryThreadMain { MahoBridge.isInitialized }

        // Then: loading UI progresses and sees no partially hydrated core.
        assertFalse(readySeenByUi.await())
        assertEquals(emptyList<String>(), fixture.mainNativeCalls.toList())
    }

    @Test
    fun readyFollowsKeyOpenLoadAndSnapshot() {
        // Given: seeded storage is held inside the real initialize/load call chain.
        start()

        // When: hydration finishes and dependent sync setup is admitted.
        fixture.load.release.complete(Unit)
        fixture.syncStarted.await()

        // Then: core ownership was private at native load and snapshot preceded publication.
        assertFalse("core was published before native load returned", fixture.publishedAtLoad)
        val events = fixture.events.toList()
        val expectedOrder = listOf("key", "open", "load", "snapshot", "sync")
        assertEquals(expectedOrder, events.filter { it in expectedOrder })
        assertTrue(fixture.publishedAtSync)
        assertEquals(emptyList<String>(), fixture.mainNativeCalls.toList())
    }

    @Test
    fun failureIsNotEmptySuccess() {
        // Given: storage opens, but loading its contents returns the native failure value.
        fixture.loadSucceeds = false
        start()

        // When: the native load failure is delivered to MahoApplication.
        fixture.load.release.complete(Unit)

        // Then: the failed native allocation is reclaimed, not published as an empty profile.
        fixture.destroyed.await()
        assertFalse(MahoBridge.isInitialized)
        assertFalse(fixture.syncStarted.isDone)
        assertEquals(1, fixture.freeCount)
    }

    @Test
    fun shutdownDuringLoadFreesUnpublishedCore() {
        // Given: the actual native core is inside hydration, with ownership not yet ready.
        start()

        // When: the public core teardown entry point is invoked before load completion.
        MahoBridge.destroy()

        // Then: shutdown must not free an executing core; completion reclaims it without Ready.
        assertFalse("native core was freed while load still owned it", fixture.freeWhileHydrating)
        assertFalse(MahoBridge.isInitialized)
        fixture.load.release.complete(Unit)
        fixture.destroyed.await()
        startup?.await()
        assertEquals(1, fixture.freeCount)
        assertFalse(fixture.syncStarted.isDone)
        assertFalse(MahoBridge.isInitialized)
    }
}
