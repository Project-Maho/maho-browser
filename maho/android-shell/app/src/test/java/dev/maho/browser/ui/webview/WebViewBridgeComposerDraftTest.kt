package dev.maho.browser.ui.webview

import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * Locks the composer-draft JSON-RPC surface the shared web-ai bundle calls.
 *
 * The scope object crossing the bridge must be normalized into exactly the JSON
 * maho-core accepts (`{"kind":"new_task"}` /
 * `{"kind":"conversation","conversationId":"<id>"}`); anything else must be
 * rejected here rather than written as a malformed key.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = android.app.Application::class)
class WebViewBridgeComposerDraftTest {

    private fun bridge(native: WebViewNativeBridge): WebViewBridge = WebViewBridge(
        native = native,
        evaluateJs = {},
    )

    private fun call(bridge: WebViewBridge, json: String): JSONObject =
        JSONObject(runBlocking { bridge.handleRequest(json) })

    @Test
    fun `set forwards the normalized new_task scope and text`() {
        val native = FakeNativeBridge()
        val resp = call(
            bridge(native),
            """{"id":"d1","method":"composerDraftSet","params":{"scope":{"kind":"new_task"},"text":"unsent"}}""",
        )

        assertTrue(resp.getBoolean("result"))
        assertEquals("""{"kind":"new_task"}""", native.composerDraftSets.single().first)
        assertEquals("unsent", native.composerDraftSets.single().second)
    }

    @Test
    fun `set forwards the normalized conversation scope`() {
        val native = FakeNativeBridge()
        call(
            bridge(native),
            """{"id":"d2","method":"composerDraftSet","params":{"scope":{"kind":"conversation","conversationId":"conv-9"},"text":"body"}}""",
        )

        assertEquals(
            """{"kind":"conversation","conversationId":"conv-9"}""",
            native.composerDraftSets.single().first,
        )
    }

    @Test
    fun `get returns the parsed draft object`() {
        val native = FakeNativeBridge(
            composerDraftJson = """{"version":1,"text":"restored","updatedAt":"2026-08-11T00:00:00Z"}""",
        )
        val resp = call(
            bridge(native),
            """{"id":"d3","method":"composerDraftGet","params":{"scope":{"kind":"new_task"}}}""",
        )

        val result = resp.getJSONObject("result")
        assertEquals("restored", result.getString("text"))
        assertEquals(1, result.getInt("version"))
    }

    @Test
    fun `get returns null when no draft is stored`() {
        val resp = call(
            bridge(FakeNativeBridge()),
            """{"id":"d4","method":"composerDraftGet","params":{"scope":{"kind":"new_task"}}}""",
        )

        assertTrue(resp.isNull("result"))
    }

    @Test
    fun `delete forwards the scope`() {
        val native = FakeNativeBridge()
        val resp = call(
            bridge(native),
            """{"id":"d5","method":"composerDraftDelete","params":{"scope":{"kind":"conversation","conversationId":"conv-9"}}}""",
        )

        assertTrue(resp.getBoolean("result"))
        assertEquals(
            """{"kind":"conversation","conversationId":"conv-9"}""",
            native.composerDraftDeletes.single(),
        )
    }

    @Test
    fun `malformed scope is rejected without touching native`() {
        val native = FakeNativeBridge()
        val b = bridge(native)

        val missingKind = call(b, """{"id":"e1","method":"composerDraftSet","params":{"scope":{},"text":"x"}}""")
        val unknownKind = call(b, """{"id":"e2","method":"composerDraftSet","params":{"scope":{"kind":"nope"},"text":"x"}}""")
        val missingId = call(b, """{"id":"e3","method":"composerDraftSet","params":{"scope":{"kind":"conversation"},"text":"x"}}""")
        val blankId = call(
            b,
            """{"id":"e4","method":"composerDraftSet","params":{"scope":{"kind":"conversation","conversationId":""},"text":"x"}}""",
        )
        val notAnObject = call(b, """{"id":"e5","method":"composerDraftGet","params":{"scope":"new_task"}}""")

        for (resp in listOf(missingKind, unknownKind, missingId, blankId, notAnObject)) {
            assertTrue(resp.has("error"))
            assertFalse(resp.has("result"))
        }
        assertTrue(native.composerDraftSets.isEmpty())
        assertTrue(native.composerDraftGets.isEmpty())
    }

    @Test
    fun `empty text is forwarded verbatim so core performs the delete`() {
        val native = FakeNativeBridge()
        call(
            bridge(native),
            """{"id":"d6","method":"composerDraftSet","params":{"scope":{"kind":"new_task"},"text":""}}""",
        )

        assertEquals("", native.composerDraftSets.single().second)
    }

    @Test
    fun `a native failure surfaces an rpc error instead of crashing the webview`() {
        val native = FakeNativeBridge(composerDraftSupported = false)
        val resp = call(
            bridge(native),
            """{"id":"d7","method":"composerDraftSet","params":{"scope":{"kind":"new_task"},"text":"x"}}""",
        )

        assertTrue(resp.has("error"))
        assertNull(resp.optJSONObject("result"))
    }

    @Test
    fun `positional array params dispatch like the named object form`() {
        val native = FakeNativeBridge()
        call(
            bridge(native),
            """{"id":"d8","method":"composerDraftSet","params":[{"kind":"new_task"},"positional"]}""",
        )

        assertEquals("""{"kind":"new_task"}""", native.composerDraftSets.single().first)
        assertEquals("positional", native.composerDraftSets.single().second)
    }
}
