package dev.maho.browser

import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import dev.maho.browser.ui.webview.MahoNativeBridge
import dev.maho.browser.ui.webview.WebViewBridge
import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

@RunWith(AndroidJUnit4::class)
class AgentNativeLibraryTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    @Before
    fun setUp() {
        MahoBridge.appContext = context
        MahoBridge.provisionSqlcipherKey()
        MahoBridge.initialize(File(context.filesDir, "native-agent-instrumentation").also { it.mkdirs() }.absolutePath)
        File(context.filesDir, "agent-artifacts").mkdirs()
        File(context.cacheDir, "artifact_exports").deleteRecursively()
        MahoBridge.artifactShareLauncher = null
    }

    @After
    fun tearDown() {
        MahoBridge.artifactShareLauncher = null
        File(context.cacheDir, "artifact_exports").deleteRecursively()
    }

    @Test
    fun packagedAbiRunsRealFsWriteAndPollsOrderedDeduplicatedPathFreeEvents() {
        val handle = MahoBridge.agentCreateSession("instrumentation-fs-write")
        assertNotEquals(0L, handle)
        assertTrue(MahoBridge.agentInstallDeterministicFsWriteModel(handle))
        val generation = MahoBridge.agentTestCompletionGeneration(handle)

        assertTrue(MahoBridge.agentSendMessage(handle, "write the deterministic artifact"))
        assertTrue(MahoBridge.agentTestAwaitCompletion(handle, generation, 10_000L))

        val events = drainEvents(handle)
        val artifactIndex = events.indexOfFirst { it.getString("type") == "artifact_created" }
        val toolResultIndex = events.indexOfFirst { it.getString("type") == "tool_result" }
        assertTrue("artifact_created must precede tool_result: $events", artifactIndex >= 0 && artifactIndex < toolResultIndex)
        val artifactEvent = events[artifactIndex]
        val artifact = artifactEvent.getJSONObject("data")
        assertFalse(artifact.has("storage_rel_path"))
        assertFalse(artifact.has("storageRelPath"))
        assertFalse(artifact.has("bytes"))
        val artifactId = artifact.getString("artifact_id")

        assertTrue(MahoBridge.agentTestEnqueueArtifactCreatedTwice(handle, artifactId))
        val duplicateEvents = drainEvents(handle).filter { it.optString("type") == "artifact_created" }
        assertEquals(1, duplicateEvents.size)
        assertEquals(artifactId, duplicateEvents.single().getJSONObject("data").getString("artifact_id"))
        assertNull(MahoBridge.agentPollEvent(handle))

        val listed = JSONArray(MahoBridge.agentListArtifacts(handle))
        assertEquals(1, listed.length())
        assertEquals(artifactId, listed.getJSONObject(0).getString("artifactId"))
        assertArrayEquals("created through packaged JNI fs_write".toByteArray(), MahoBridge.agentReadArtifact(handle, artifactId))
        MahoBridge.agentFreeSession(handle)
    }

    @Test
    fun packagedJniRejectsMaliciousIndexedPathsAndSymlinkSwaps() {
        val handle = MahoBridge.agentCreateSession("instrumentation-malicious-index")
        assertNotEquals(0L, handle)
        val root = File(context.filesDir, "agent-artifacts").also { it.mkdirs() }
        val outside = File(context.filesDir, "outside-secret.txt").apply { writeText("outside") }
        val finalLink = File(root, "final-link.txt")
        val directoryLink = File(root, "directory-link")
        android.system.Os.symlink(outside.absolutePath, finalLink.absolutePath)
        android.system.Os.symlink(context.filesDir.absolutePath, directoryLink.absolutePath)

        val cases = listOf(
            "empty-storage" to "",
            "absolute-storage" to outside.absolutePath,
            "traversal-storage" to "../${outside.name}",
            "final-symlink" to finalLink.name,
            "directory-symlink" to "${directoryLink.name}/${outside.name}",
        )
        for ((artifactId, storagePath) in cases) {
            assertTrue(MahoBridge.agentTestSeedArtifact(handle, artifactId, storagePath, "secret.txt", "text/plain"))
            assertNull("malicious indexed path was readable: $storagePath", MahoBridge.agentReadArtifact(handle, artifactId))
        }
        MahoBridge.agentFreeSession(handle)
        finalLink.delete()
        directoryLink.delete()
        outside.delete()
    }

    @Test
    fun freeRacesWithEveryAgentOperationAndStaleHandleChurnStaysSafe() {
        repeat(20) { iteration ->
            val handle = MahoBridge.agentCreateSession("instrumentation-race-$iteration")
            assertNotEquals(0L, handle)
            assertTrue(MahoBridge.agentInstallDeterministicFsWriteModel(handle))
            val ready = CountDownLatch(6)
            val start = CountDownLatch(1)
            val done = CountDownLatch(6)
            val failures = Collections.synchronizedList(mutableListOf<Throwable>())
            val operations = listOf<() -> Unit>(
                { MahoBridge.agentSendMessage(handle, "race") },
                { MahoBridge.agentListTools(handle) },
                { MahoBridge.agentListArtifacts(handle) },
                { MahoBridge.agentReadArtifact(handle, "missing") },
                { MahoBridge.agentPollEvent(handle) },
                { MahoBridge.agentFreeSession(handle) },
            )
            operations.forEach { operation ->
                Thread {
                    ready.countDown()
                    try {
                        assertTrue(start.await(5, TimeUnit.SECONDS))
                        operation()
                    } catch (error: Throwable) {
                        failures += error
                    } finally {
                        done.countDown()
                    }
                }.start()
            }
            assertTrue(ready.await(5, TimeUnit.SECONDS))
            start.countDown()
            assertTrue("race operations timed out", done.await(10, TimeUnit.SECONDS))
            assertTrue("race failures: $failures", failures.isEmpty())
            MahoBridge.agentFreeSession(handle)
            assertFalse(MahoBridge.agentSendMessage(handle, "stale"))
            assertNull(MahoBridge.agentListTools(handle))
            assertNull(MahoBridge.agentListArtifacts(handle))
            assertNull(MahoBridge.agentReadArtifact(handle, "missing"))
            assertNull(MahoBridge.agentPollEvent(handle))
        }

        val handles = (0 until 100).map {
            MahoBridge.agentCreateSession("instrumentation-churn-$it").also(MahoBridge::agentFreeSession)
        }
        assertEquals(handles.size, handles.toSet().size)
        assertTrue(handles.all { it != 0L })
    }

    @Test
    fun productionWebViewBridgeListsAndSharesViaRealNativeBridgeWithExactChooserContract() {
        val nativeHandle = MahoBridge.agentCreateSession("instrumentation-rpc-share")
        assertNotEquals(0L, nativeHandle)
        assertTrue(MahoBridge.agentInstallDeterministicFsWriteModel(nativeHandle))
        val generation = MahoBridge.agentTestCompletionGeneration(nativeHandle)
        assertTrue(MahoBridge.agentSendMessage(nativeHandle, "write share artifact"))
        assertTrue(MahoBridge.agentTestAwaitCompletion(nativeHandle, generation, 10_000L))
        val artifactId = drainEvents(nativeHandle)
            .first { it.getString("type") == "artifact_created" }
            .getJSONObject("data")
            .getString("artifact_id")
        val bridge = WebViewBridge(native = MahoNativeBridge, evaluateJs = {})
        val create = rpc(bridge, "agentCreateSession", JSONObject().put("sessionId", "instrumentation-rpc-share"))
        val opaqueHandle = create.getString("result")
        val listResponse = rpc(bridge, "agentListArtifacts", JSONObject().put("handle", opaqueHandle))
        val listed = listResponse.getJSONArray("result")
        assertEquals(1, listed.length())
        assertFalse(listed.getJSONObject(0).has("path"))

        var chooser: Intent? = null
        MahoBridge.artifactShareLauncher = { _, intent -> chooser = intent; true }
        val shareResponse = rpc(
            bridge,
            "artifactShare",
            JSONObject().put("handle", opaqueHandle).put("artifactId", artifactId),
        )
        assertTrue(shareResponse.getBoolean("result"))
        assertNotNull(chooser)
        val capturedChooser = chooser!!
        assertEquals(Intent.ACTION_CHOOSER, capturedChooser.action)
        val send = capturedChooser.getParcelableExtra<Intent>(Intent.EXTRA_INTENT)
        assertNotNull(send)
        assertEquals(Intent.ACTION_SEND, send!!.action)
        assertEquals("text/plain", send.type)
        val stream = send.getParcelableExtra<android.net.Uri>(Intent.EXTRA_STREAM)
        assertNotNull(stream)
        assertEquals(stream, send.clipData!!.getItemAt(0).uri)
        assertTrue(send.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0)

        rpc(bridge, "agentFreeSession", JSONObject().put("handle", opaqueHandle))
        MahoBridge.agentFreeSession(nativeHandle)
    }

    @Test
    fun shareFailuresAndBadIdsLeaveNoCacheExports() {
        val handle = createArtifactSession("instrumentation-share-failures")
        val artifactId = drainEvents(handle)
            .first { it.getString("type") == "artifact_created" }
            .getJSONObject("data")
            .getString("artifact_id")
        val exportDir = File(context.cacheDir, "artifact_exports")

        assertFalse(MahoBridge.artifactShare(handle, "../escape"))
        assertFalse(exportDir.listFiles().orEmpty().isNotEmpty())

        MahoBridge.artifactShareLauncher = { _, _ -> false }
        assertFalse(MahoBridge.artifactShare(handle, artifactId))
        assertTrue(exportDir.listFiles().orEmpty().isEmpty())

        MahoBridge.artifactShareLauncher = { _, _ -> throw IllegalStateException("injected start failure") }
        assertFalse(MahoBridge.artifactShare(handle, artifactId))
        assertTrue(exportDir.listFiles().orEmpty().isEmpty())
        MahoBridge.agentFreeSession(handle)
    }

    private fun createArtifactSession(sessionId: String): Long {
        val handle = MahoBridge.agentCreateSession(sessionId)
        assertTrue(MahoBridge.agentInstallDeterministicFsWriteModel(handle))
        val generation = MahoBridge.agentTestCompletionGeneration(handle)
        assertTrue(MahoBridge.agentSendMessage(handle, "write"))
        assertTrue(MahoBridge.agentTestAwaitCompletion(handle, generation, 10_000L))
        return handle
    }

    private fun drainEvents(handle: Long): List<JSONObject> {
        val events = mutableListOf<JSONObject>()
        while (true) {
            val raw = MahoBridge.agentPollEvent(handle) ?: break
            events += JSONObject(raw)
        }
        return events
    }

    private fun rpc(bridge: WebViewBridge, method: String, params: JSONObject): JSONObject =
        JSONObject(runBlocking {
            bridge.handleRequest(
                JSONObject()
                    .put("jsonrpc", "2.0")
                    .put("id", method)
                    .put("method", method)
                    .put("params", params)
                    .toString(),
            )
        }).also { response -> assertFalse(response.toString(), response.has("error")) }
}
