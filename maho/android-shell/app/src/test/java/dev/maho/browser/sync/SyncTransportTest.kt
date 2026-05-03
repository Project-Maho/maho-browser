package dev.maho.browser.sync

import android.content.Context
import android.util.Base64
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
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
import org.robolectric.shadows.ShadowLooper
import java.nio.charset.StandardCharsets
import java.util.concurrent.TimeUnit

class FakeBridgeAdapter : SyncBridgeAdapter {
    val appliedEntities = mutableListOf<String>()
    var drainResult: String? = null

    override fun applyRemoteEntities(json: String) {
        appliedEntities.add(json)
    }

    override fun drainOutgoing(): String? = drainResult
}

@RunWith(RobolectricTestRunner::class)
@Config(application = android.app.Application::class)
class SyncTransportTest {
    private lateinit var mockServer: MockWebServer
    private lateinit var fakeAdapter: FakeBridgeAdapter
    private lateinit var transport: SyncTransport

    @Before
    fun setUp() {
        mockServer = MockWebServer()
        fakeAdapter = FakeBridgeAdapter()
        transport = SyncTransport(fakeAdapter)
        mockServer.start()
        transport.initContext(RuntimeEnvironment.getApplication())
    }

    @After
    fun tearDown() {
        try { transport.disconnect() } catch (_: Exception) {}
        try { mockServer.shutdown() } catch (_: Exception) {}
        val context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences("maho_sync", Context.MODE_PRIVATE)
            .edit().remove("SyncTransport.lastServerSeq").commit()
    }

    private fun startSession(): RelaySession {
        return RelaySession(
            email = "test@example.com",
            accessToken = "access-token",
            refreshToken = "refresh-token",
            serverUrl = mockServer.url("/").toString(),
        )
    }

    private fun waitFor(timeoutMs: Long = 2_000L, predicate: () -> Boolean) {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (predicate()) return
            ShadowLooper.idleMainLooper()
            Thread.sleep(20)
        }
        if (!predicate()) {
            throw AssertionError("Condition was not met within ${timeoutMs}ms")
        }
    }

    @Test
    fun polling_pushes_base64_blob_and_pulls_paginated_messages() {
        val drained = "[{\"id\":\"1\"}]"
        fakeAdapter.drainResult = drained
        val expectedBlob = Base64.encodeToString(drained.toByteArray(StandardCharsets.UTF_8), Base64.NO_WRAP)
        mockServer.enqueue(MockResponse().setResponseCode(200).setBody("{}"))
        mockServer.enqueue(MockResponse().setResponseCode(200).setBody("{\"messages\":[{\"seq\":0,\"blob\":\"aGVsbG8=\"},{\"seq\":5,\"blob\":\"d29ybGQ=\"}],\"has_more\":true}"))
        mockServer.enqueue(MockResponse().setResponseCode(200).setBody("{\"messages\":[{\"seq\":7,\"blob\":\"eyJmb28iOiJiYXIifQ==\"}],\"has_more\":false}"))

        transport.start(mockServer.url("/").toString(), startSession())
        transport.triggerSync()

        waitFor { mockServer.requestCount >= 3 }
        val pushRequest = mockServer.takeRequest(5, TimeUnit.SECONDS)!!
        assertEquals("POST", pushRequest.method)
        assertTrue(pushRequest.path!!.contains("/sync/push"))
        assertFalse(pushRequest.path!!.contains("since_seq"))
        val pushBody = pushRequest.body.readUtf8()
        assertTrue(pushBody.contains("\"blobs\""))
        assertTrue(pushBody.contains(expectedBlob))

        val firstPull = mockServer.takeRequest(5, TimeUnit.SECONDS)!!
        assertEquals("GET", firstPull.method)
        assertTrue(firstPull.path!!.contains("/sync/pull"))
        assertTrue(firstPull.path!!.contains("since_seq=0"))
        assertTrue(firstPull.path!!.contains("limit=100"))

        val secondPull = mockServer.takeRequest(5, TimeUnit.SECONDS)!!
        assertEquals("GET", secondPull.method)
        assertTrue(secondPull.path!!.contains("/sync/pull"))
        assertTrue(secondPull.path!!.contains("since_seq=5"))
        assertTrue(secondPull.path!!.contains("limit=100"))

        waitFor { transport.lastServerSeq == 7L }
        assertEquals(listOf("hello", "world", "{\"foo\":\"bar\"}"), fakeAdapter.appliedEntities)
    }

    @Test
    fun polling_refreshes_session_after_unauthorized_response() {
        val drained = "[{\"id\":\"1\"}]"
        fakeAdapter.drainResult = drained
        mockServer.enqueue(MockResponse().setResponseCode(401).setBody("unauthorized"))
        mockServer.enqueue(MockResponse().setResponseCode(200).setBody("{\"access_token\":\"new-access\",\"refresh_token\":\"new-refresh\",\"email\":\"test@example.com\"}"))
        mockServer.enqueue(MockResponse().setResponseCode(200).setBody("{}"))
        mockServer.enqueue(MockResponse().setResponseCode(200).setBody("{\"messages\":[],\"has_more\":false}"))

        transport.start(mockServer.url("/").toString(), startSession())
        transport.triggerSync()

        waitFor { mockServer.requestCount >= 4 }
        val firstPush = mockServer.takeRequest(5, TimeUnit.SECONDS)!!
        assertTrue(firstPush.path!!.contains("/sync/push"))
        val refresh = mockServer.takeRequest(5, TimeUnit.SECONDS)!!
        assertTrue(refresh.path!!.contains("/auth/refresh"))
        val retriedPush = mockServer.takeRequest(5, TimeUnit.SECONDS)!!
        assertTrue(retriedPush.path!!.contains("/sync/push"))
        assertTrue(retriedPush.getHeader("Authorization")!!.contains("new-access"))
    }

    @Test
    fun disconnect_stops_future_polling() {
        mockServer.enqueue(MockResponse().setResponseCode(200).setBody("{}"))
        mockServer.enqueue(MockResponse().setResponseCode(200).setBody("{\"messages\":[],\"has_more\":false}"))

        transport.start(mockServer.url("/").toString(), startSession())
        transport.disconnect()
        transport.triggerSync()
        ShadowLooper.idleMainLooper()

        assertFalse(transport.isConnected)
        assertEquals(0, mockServer.requestCount)
    }
}
