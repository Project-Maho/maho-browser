package dev.maho.browser

import android.os.Handler
import android.os.Looper
import android.webkit.WebView
import dev.maho.browser.ui.webview.AiWebViewHost
import dev.maho.browser.ui.webview.FakeNativeBridge
import dev.maho.browser.ui.webview.WebViewBridge
import dev.maho.browser.ui.webview.WebViewNativeBridge
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.robolectric.RuntimeEnvironment
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executors
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

internal fun <T> CompletableFuture<T>.await(): T = get(10, TimeUnit.SECONDS)

internal fun <T> memoryThreadMain(action: () -> T): CompletableFuture<T> {
    val result = CompletableFuture<T>()
    Handler(Looper.getMainLooper()).post {
        try {
            result.complete(action())
        } catch (error: Throwable) {
            result.completeExceptionally(error)
        }
    }
    return result
}

internal class MemoryThreadGate {
    val entered = CompletableFuture<Unit>()
    val release = CompletableFuture<Unit>()

    fun park() {
        entered.complete(Unit)
        // Longer than any assertion guard, so an expected RED cannot release the gate.
        release.get(60, TimeUnit.SECONDS)
    }
}

/** Native-boundary fake owns live sessions, not merely a list of expected calls. */
internal class MemoryThreadNative : WebViewNativeBridge by FakeNativeBridge() {
    private enum class Kind { Agent, Chat }
    private val live = mutableMapOf<Long, Kind>()
    private val leases = mutableSetOf<Long>()
    private val freeCounts = mutableMapOf<Long, Int>()
    private var nextId = 100L
    val freed = LinkedBlockingQueue<Long>()
    val failures = CopyOnWriteArrayList<String>()
    var createGate: MemoryThreadGate? = null
    var borrowGate: MemoryThreadGate? = null

    override fun agentCreateSession(sessionId: String): Long {
        createGate?.park()
        return allocate(Kind.Agent)
    }

    override fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long =
        allocate(Kind.Chat)

    @Synchronized
    private fun allocate(kind: Kind): Long {
        val id = nextId++
        live[id] = kind
        return id
    }

    override fun agentFreeSession(sessionPtr: Long) = free(sessionPtr, Kind.Agent)
    override fun chatSessionFree(sessionPtr: Long) = free(sessionPtr, Kind.Chat)

    @Synchronized
    private fun free(id: Long, kind: Kind) {
        if (id in leases) failures += "free_during_borrow"
        if (live.remove(id) != kind) failures += "duplicate_or_wrong_kind_free"
        freeCounts[id] = (freeCounts[id] ?: 0) + 1
        freed.add(id)
    }

    override fun agentSendMessage(sessionPtr: Long, message: String): Boolean {
        synchronized(this) {
            check(live[sessionPtr] == Kind.Agent) { "stale_native_handle" }
            check(leases.add(sessionPtr))
        }
        try {
            borrowGate?.park()
            return synchronized(this) { live[sessionPtr] == Kind.Agent }
        } finally {
            synchronized(this) { leases.remove(sessionPtr) }
        }
    }

    @Synchronized
    fun liveCount(): Int = live.size

    fun awaitFrees(count: Int) {
        repeat(count) {
            assertTrue("owner did not free native session", freed.poll(10, TimeUnit.SECONDS) != null)
        }
    }

    @Synchronized
    fun assertExactlyFreed(count: Int) {
        assertEquals(emptyList<String>(), failures.toList())
        assertEquals(count, freeCounts.size)
        assertTrue(freeCounts.values.all { it == 1 })
        assertEquals(0, live.size)
    }
}

internal class MemoryThreadHostFixture(val native: MemoryThreadNative = MemoryThreadNative()) : AutoCloseable {
    val delivered = CopyOnWriteArrayList<String>()
    val bridge = WebViewBridge(native = native, evaluateJs = delivered::add)
    val host = memoryThreadMain {
        AiWebViewHost(WebView(RuntimeEnvironment.getApplication()), bridge)
    }.await()
    private val workers = Executors.newFixedThreadPool(2)

    fun request(method: String, params: JSONObject = JSONObject()): JSONObject =
        JSONObject(runBlocking { bridge.handleRequest(payload(method, params)) })

    fun requestAsync(method: String, params: JSONObject): CompletableFuture<JSONObject> =
        CompletableFuture.supplyAsync({ request(method, params) }, workers)

    fun rpc(method: String, params: JSONObject): CompletableFuture<Unit> = memoryThreadMain {
        bridge.rpc(payload(method, params))
        val jobs = bridge.requestsForTesting()
        check(jobs.size == 1) { "expected one real RPC coroutine" }
        val settled = CompletableFuture<Unit>()
        jobs.single().invokeOnCompletion { settled.complete(Unit) }
        settled
    }.await()

    private fun payload(method: String, params: JSONObject): String = JSONObject()
        .put("id", method).put("method", method).put("params", params).toString()

    override fun close() {
        native.createGate?.release?.complete(Unit)
        native.borrowGate?.release?.complete(Unit)
        // Cleanup deliberately calls canonical bridge cleanup, not the host under test.
        CompletableFuture.runAsync({ bridge.destroy() }, workers).await()
        workers.shutdown()
        check(workers.awaitTermination(10, TimeUnit.SECONDS)) { "fixture worker did not terminate" }
        memoryThreadMain { host.webView.destroy() }.await()
    }
}
