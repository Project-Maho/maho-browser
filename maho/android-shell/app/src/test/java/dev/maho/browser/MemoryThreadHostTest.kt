package dev.maho.browser

import android.app.Application
import android.os.Looper
import dev.maho.browser.ui.webview.HandleRegistry
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.LooperMode
import java.util.concurrent.CompletableFuture

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = Application::class)
@LooperMode(LooperMode.Mode.INSTRUMENTATION_TEST)
class MemoryThreadHostTest {
    @Test
    fun closeReclaimsAgentAndChatOnce() {
        // Given: both native kinds are acquired through the real RPC/registry path.
        MemoryThreadHostFixture().use { fixture ->
            fixture.request("agentCreateSession", JSONObject().put("sessionId", "memory-thread-owner-A"))
                .getString("result")
            fixture.request("chatSessionStart").getString("result")
            assertEquals(2, fixture.native.liveCount())

            // When: native host disposal repeats without JavaScript free.
            memoryThreadMain { repeat(2) { fixture.host.destroy() } }.await()

            // Then: both allocations have exactly one free owner.
            fixture.native.awaitFrees(2)
            fixture.native.assertExactlyFreed(2)
        }
    }

    @Test
    fun closeReturnsOnMainWhileBorrowIsParked() {
        // Given: the native call itself signals entry while the real registry leases it.
        MemoryThreadHostFixture().use { fixture ->
            val handle = fixture.request("agentCreateSession", JSONObject().put("sessionId", "borrow"))
                .getString("result")
            val gate = MemoryThreadGate().also { fixture.native.borrowGate = it }
            val drainWait = CompletableFuture<Unit>()
            fixture.bridge.agentSessions.onWaitForTesting = {
                when (it) {
                    HandleRegistry.WaitOperation.Close -> drainWait.complete(Unit)
                    HandleRegistry.WaitOperation.Unregister -> Unit
                }
            }
            val borrowing = fixture.requestAsync("agentSendMessage", JSONObject()
                .put("handle", handle).put("message", "memory-thread-owner-A"))
            gate.entered.await()

            // When: host close and a subsequent Main marker execute before gate release.
            memoryThreadMain {
                check(Looper.myLooper() == Looper.getMainLooper())
                fixture.host.destroy()
            }.await()
            memoryThreadMain { Unit }.await()

            // Then: actual drain wait is observed; the lease remains live until released.
            drainWait.await()
            assertEquals(1, fixture.native.liveCount())
            assertTrue(fixture.native.freed.isEmpty())
            gate.release.complete(Unit)
            assertTrue(borrowing.await().getBoolean("result"))
            fixture.native.awaitFrees(1)
            fixture.native.assertExactlyFreed(1)
        }
    }

    @Test
    fun lateCreateIsFreedWithoutDelivery() {
        // Given: subscribe to completion of the actual bridge-owned RPC job before close.
        MemoryThreadHostFixture().use { fixture ->
            val gate = MemoryThreadGate().also { fixture.native.createGate = it }
            val settled = fixture.rpc("agentCreateSession", JSONObject().put("sessionId", "late-create"))
            gate.entered.await()

            // When: native host disappears while synchronous native creation is parked.
            memoryThreadMain { fixture.host.destroy() }.await()
            gate.release.complete(Unit)

            // Then: cancelled delivery does not abandon a late native allocation.
            settled.await()
            assertEquals(emptyList<String>(), fixture.delivered.toList())
            fixture.native.awaitFrees(1)
            fixture.native.assertExactlyFreed(1)
        }
    }

    @Test
    fun closingOneOwnerLeavesOtherUsable() {
        // Given: two independent hosts share the same native allocation domain.
        val native = MemoryThreadNative()
        MemoryThreadHostFixture(native).use { ownerA ->
            MemoryThreadHostFixture(native).use { ownerB ->
                ownerA.request("agentCreateSession", JSONObject().put("sessionId", "A")).getString("result")
                val handleB = ownerB.request("agentCreateSession", JSONObject().put("sessionId", "B"))
                    .getString("result")

                // When: only A is destroyed.
                memoryThreadMain { ownerA.host.destroy() }.await()

                // Then: A is reclaimed and B still executes a native operation.
                native.awaitFrees(1)
                assertEquals(1, native.liveCount())
                assertTrue(ownerB.request("agentSendMessage", JSONObject().put("handle", handleB)
                    .put("message", "memory-thread-owner-B-alive")).getBoolean("result"))
                assertTrue(native.failures.isEmpty())
            }
        }
    }

    @Test
    fun concurrentFreeAndCloseHaveOneOwner() {
        // Given: explicit free is truly parked inside unregister, not just scheduled.
        MemoryThreadHostFixture().use { fixture ->
            val handle = fixture.request("agentCreateSession", JSONObject().put("sessionId", "race"))
                .getString("result")
            fixture.request("chatSessionStart").getString("result")
            val gate = MemoryThreadGate().also { fixture.native.borrowGate = it }
            val unregisterWait = CompletableFuture<Unit>()
            fixture.bridge.agentSessions.onWaitForTesting = {
                when (it) {
                    HandleRegistry.WaitOperation.Unregister -> unregisterWait.complete(Unit)
                    HandleRegistry.WaitOperation.Close -> Unit
                }
            }
            val borrowing = fixture.requestAsync("agentSendMessage", JSONObject()
                .put("handle", handle).put("message", "race"))
            gate.entered.await()
            val freeing = fixture.requestAsync("agentFreeSession", JSONObject().put("handle", handle))
            unregisterWait.await()

            // When: host close races the already admitted explicit free.
            memoryThreadMain { fixture.host.destroy() }.await()
            gate.release.complete(Unit)

            // Then: lease completes; agent and chat each get exactly one free owner.
            assertTrue(borrowing.await().getBoolean("result"))
            assertFalse(freeing.await().has("error"))
            fixture.native.awaitFrees(2)
            fixture.native.assertExactlyFreed(2)
        }
    }
}
