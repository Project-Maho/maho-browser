package dev.maho.browser

import android.webkit.WebView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import dev.maho.browser.ui.webview.AiWebViewHost
import dev.maho.browser.ui.webview.MahoNativeBridge
import dev.maho.browser.ui.webview.WebViewBridge
import dev.maho.browser.ui.webview.WebViewNativeBridge
import kotlinx.coroutines.runBlocking
import org.json.JSONObject

import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

/** Actual packaged JNI path. Run only on a dedicated physical-device fixture profile. */
@RunWith(AndroidJUnit4::class)
class MemoryThreadNativeHostTest {
    @Test
    fun nativeHostCloseReclaimsAgentAcrossChurnAndPreservesOtherOwner() {
        // Given: every native operation passes through MahoNativeBridge and packaged JNI.
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val nativeIds = ConcurrentHashMap<String, Long>()
        val freed = ConcurrentHashMap<Long, CompletableFuture<Unit>>()
        val native = object : WebViewNativeBridge by MahoNativeBridge {
            override fun agentCreateSession(sessionId: String): Long {
                val id = MahoNativeBridge.agentCreateSession(sessionId)
                nativeIds[sessionId] = id
                freed[id] = CompletableFuture()
                return id
            }

            override fun agentFreeSession(sessionPtr: Long) {
                MahoNativeBridge.agentFreeSession(sessionPtr)
                requireNotNull(freed[sessionPtr]).complete(Unit)
            }
        }
        val runId = UUID.randomUUID().toString()
        val ownerB = WebViewBridge(native = native, evaluateJs = {})
        val hostB = CompletableFuture<AiWebViewHost>()
        instrumentation.runOnMainSync {
            hostB.complete(AiWebViewHost(WebView(instrumentation.targetContext), ownerB))
        }
        val bSession = "$runId-B"
        try {
            create(ownerB, bSession)
            val bId = requireNotNull(nativeIds[bSession])
            assertNotEquals(0L, bId)
            repeat(100) { iteration ->
                val ownerA = WebViewBridge(native = native, evaluateJs = {})
                val hostA = CompletableFuture<AiWebViewHost>()
                instrumentation.runOnMainSync {
                    hostA.complete(AiWebViewHost(WebView(instrumentation.targetContext), ownerA))
                }
                val aSession = "$runId-A-$iteration"
                try {
                    create(ownerA, aSession)
                    val aId = requireNotNull(nativeIds[aSession])
                    assertNotEquals(0L, aId)
                    // When: native host closes; there is no JS free/unmount callback.
                    instrumentation.runOnMainSync { hostA.get().destroy() }

                    // Then: await native free completion, prove stale JNI rejection and B liveness.
                    requireNotNull(freed[aId]).get(10, TimeUnit.SECONDS)
                    assertNull(MahoBridge.agentListTools(aId))
                    assertNotNull(MahoBridge.agentListTools(bId))
                } finally {
                    ownerA.destroy()
                    nativeIds[aSession]?.takeIf { it != 0L }?.let {
                        requireNotNull(freed[it]).get(10, TimeUnit.SECONDS)
                    }
                    instrumentation.runOnMainSync { hostA.get().webView.destroy() }
                }
            }
        } finally {
            ownerB.destroy()
            nativeIds[bSession]?.takeIf { it != 0L }?.let {
                requireNotNull(freed[it]).get(10, TimeUnit.SECONDS)
            }
            instrumentation.runOnMainSync { hostB.get().webView.destroy() }
        }
    }

    private fun create(bridge: WebViewBridge, sessionId: String): String =
        request(bridge, "agentCreateSession", JSONObject().put("sessionId", sessionId)).getString("result")

    private fun request(bridge: WebViewBridge, method: String, params: JSONObject): JSONObject =
        JSONObject(runBlocking { bridge.handleRequest(JSONObject().put("id", method)
            .put("method", method).put("params", params).toString()) })
}
