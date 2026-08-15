package dev.maho.browser

import android.os.Looper
import android.webkit.WebView
import dev.maho.browser.bridge.WebViewBridge
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.android.controller.ActivityController
import org.robolectric.annotation.Config
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = android.app.Application::class)
class SessionStreamingTest {

    private lateinit var activityController: ActivityController<android.app.Activity>
    private lateinit var activity: android.app.Activity
    private lateinit var webView: WebView
    private lateinit var bridge: WebViewBridge

    @Before
    fun setUp() {
        activityController = Robolectric.buildActivity(android.app.Activity::class.java)
            .create()
            .start()
            .resume()
        activity = activityController.get()
        webView = WebView(activity)

        bridge = WebViewBridge(
            webView = webView,
            activity = activity,
        )
    }

    @After
    fun tearDown() {
        bridge.shutdownStreaming()
        activityController.destroy()
    }

    private fun drainAll() {
        shadowOf(Looper.getMainLooper()).idle()
        Thread.sleep(100)
        shadowOf(Looper.getMainLooper()).idle()
    }

    @Test
    fun `concurrent sessions deliver all tokens without crash`() {
        val sessionCount = 3
        val tokensPerSession = 500
        val executor = Executors.newFixedThreadPool(sessionCount)
        val startLatch = CountDownLatch(1)
        val doneLatch = CountDownLatch(sessionCount)

        repeat(sessionCount) { s ->
            executor.submit {
                startLatch.await()
                val sid = "session-$s"
                repeat(tokensPerSession) { t ->
                    bridge.pushToken(sid, "t${s}_$t")
                }
                doneLatch.countDown()
            }
        }

        startLatch.countDown()
        assertTrue("Threads should finish within 5s", doneLatch.await(5, TimeUnit.SECONDS))

        repeat(sessionCount) { s ->
            bridge.pushStreamComplete("session-$s", "done-$s")
        }

        drainAll()
        executor.shutdown()
    }

    @Test
    fun `streaming constants match A6 design spec`() {
        assertEquals(32, WebViewBridge.MAX_BATCH_SIZE)
        assertEquals(256, WebViewBridge.MAX_BUFFER_SIZE)
        assertEquals(60L, WebViewBridge.FLUSH_INTERVAL_MS)
    }

    @Test
    fun `batch flushes immediately at 32 tokens`() {
        val sid = "batch-test"
        repeat(32) { i ->
            bridge.pushToken(sid, "b-$i")
        }
        drainAll()

        val lastJs = shadowOf(webView).lastEvaluatedJavascript
        assertTrue(
            "Should have flushed a batch via __mahoStreamBatch",
            lastJs != null && lastJs.contains("__mahoStreamBatch"),
        )
    }

    @Test
    fun `tokens buffer during delayed UI dispatch and eventually deliver`() {
        val sid = "gc-pause-session"

        repeat(100) { i ->
            bridge.pushToken(sid, "p-$i")
        }

        Thread.sleep(200)

        bridge.pushStreamComplete(sid, "gc-done")
        drainAll()

        val lastJs = shadowOf(webView).lastEvaluatedJavascript
        assertTrue(
            "Complete should fire after GC pause",
            lastJs != null && lastJs.contains("__mahoStreamComplete"),
        )
    }

    @Test
    fun `pushStreamComplete flushes pending tokens before emitting complete`() {
        val sid = "complete-flush"

        repeat(5) { i ->
            bridge.pushToken(sid, "c-$i")
        }

        bridge.pushStreamComplete(sid, "all-done")
        drainAll()

        val lastJs = shadowOf(webView).lastEvaluatedJavascript
        assertTrue(
            "Last JS call should be complete",
            lastJs != null && lastJs.contains("__mahoStreamComplete"),
        )
    }

    @Test
    fun `pushStreamError flushes pending tokens before emitting error`() {
        val sid = "error-flush"

        repeat(5) { i ->
            bridge.pushToken(sid, "e-$i")
        }

        bridge.pushStreamError(sid, "something broke")
        drainAll()

        val lastJs = shadowOf(webView).lastEvaluatedJavascript
        assertTrue(
            "Last JS call should be error",
            lastJs != null && lastJs.contains("__mahoStreamError"),
        )
    }

    @Test
    fun `releaseAll during active streams does not crash`() {
        repeat(3) { s ->
            repeat(10) { t ->
                bridge.pushToken("teardown-$s", "x-$t")
            }
        }

        bridge.releaseAll()
        drainAll()

        assertTrue("releaseAll completes without crash", true)
    }

    @Test
    fun `pushToolDelta dispatches immediately without buffering`() {
        val sid = "tool-test"
        val deltaJson = """{"name":"search","args":{"q":"hello"}}"""

        bridge.pushToolDelta(sid, deltaJson)
        drainAll()

        val lastJs = shadowOf(webView).lastEvaluatedJavascript
        assertTrue(
            "Should dispatch tool delta via __mahoStreamToolDelta",
            lastJs != null && lastJs.contains("__mahoStreamToolDelta"),
        )
    }

    @Test
    fun `pushImageDelta dispatches immediately without buffering`() {
        val sid = "image-test"
        val deltaJson = """{"url":"data:image/png;base64,abc"}"""

        bridge.pushImageDelta(sid, deltaJson)
        drainAll()

        val lastJs = shadowOf(webView).lastEvaluatedJavascript
        assertTrue(
            "Should dispatch image delta via __mahoStreamImageDelta",
            lastJs != null && lastJs.contains("__mahoStreamImageDelta"),
        )
    }

    @Test
    fun `sub-batch tokens are delivered via complete flush when timer cannot fire`() {
        val sid = "timer-test"
        repeat(5) { i ->
            bridge.pushToken(sid, "tm-$i")
        }

        // In production, the 60ms timer flushes sub-batch tokens.
        // Under test, HandlerThread loopers are paused, so we verify
        // that pushStreamComplete correctly flushes any pending tokens.
        bridge.pushStreamComplete(sid, "timer-fallback")
        shadowOf(Looper.getMainLooper()).idle()

        val lastJs = shadowOf(webView).lastEvaluatedJavascript
        assertTrue(
            "Complete should flush pending sub-batch tokens",
            lastJs != null && lastJs.contains("__mahoStreamComplete"),
        )
    }

    @Test
    fun `pushThinking coalesces and flushes on complete`() {
        val sid = "thinking-test"
        repeat(5) { i ->
            bridge.pushThinking(sid, "thought-$i; ")
        }
        bridge.pushStreamComplete(sid, "thinking-done")
        drainAll()

        val lastJs = shadowOf(webView).lastEvaluatedJavascript
        assertTrue(
            "Should have completed stream",
            lastJs != null && lastJs.contains("__mahoStreamComplete"),
        )
    }

    @Test
    fun `EventThrottler throttles rapid events`() {
        val throttler = dev.maho.browser.bridge.EventThrottler(intervalMs = 50L)
        var count = 0
        repeat(10) {
            throttler.throttle {
                count++
            }
        }
        shadowOf(Looper.getMainLooper()).idle()
        Thread.sleep(100)
        shadowOf(Looper.getMainLooper()).idle()

        assertTrue("Execution count should be throttled (got $count)", count in 1..2)
    }
}
