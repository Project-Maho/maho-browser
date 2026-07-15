package dev.maho.browser.ui.webview

import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * Drives [WebViewBridge.handleRequest] directly (no WebView) to lock the
 * dual-mode JSON-RPC contract the shared web-ai bundle depends on:
 *  - `id` echoed verbatim (string UUID for new bundle, int for old bundle)
 *  - `params` accepted as a named object OR a positional array
 *  - agent methods round-trip through an opaque string handle registry
 *
 * Robolectric supplies the real org.json implementation; MahoBridge (and its
 * native library) is never touched — a [FakeNativeBridge] stands in.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = android.app.Application::class)
class WebViewBridgeRpcTest {

    private fun bridge(native: WebViewNativeBridge = FakeNativeBridge()): WebViewBridge =
        WebViewBridge(native = native, evaluateJs = {})

    private fun call(bridge: WebViewBridge, json: String): JSONObject =
        JSONObject(runBlocking { bridge.handleRequest(json) })

    @Test
    fun `string id is echoed verbatim`() {
        val resp = call(
            bridge(),
            """{"jsonrpc":"2.0","id":"abc-123-uuid","method":"byokGetProviders","params":{}}""",
        )
        assertEquals("abc-123-uuid", resp.get("id"))
        assertTrue(resp.has("result"))
    }

    @Test
    fun `int id from old bundle is echoed verbatim as int`() {
        val resp = call(
            bridge(),
            """{"jsonrpc":"2.0","id":7,"method":"byokGetProviders","params":[]}""",
        )
        assertEquals(7, resp.get("id"))
    }

    @Test
    fun `named object params dispatch`() {
        val resp = call(
            bridge(),
            """{"jsonrpc":"2.0","id":"n1","method":"byokGetKey","params":{"provider":"openai"}}""",
        )
        assertEquals("sk-openai-existing", resp.get("result"))
    }

    @Test
    fun `positional array params still dispatch (dual-mode)`() {
        val resp = call(
            bridge(),
            """{"jsonrpc":"2.0","id":"p1","method":"byokGetKey","params":["openai"]}""",
        )
        assertEquals("sk-openai-existing", resp.get("result"))
    }

    @Test
    fun `agentCreateSession returns an opaque string handle (not the raw pointer)`() {
        val resp = call(
            bridge(FakeNativeBridge(agentCreateSessionPtr = 42L)),
            """{"jsonrpc":"2.0","id":"a1","method":"agentCreateSession","params":{"sessionId":"s-1"}}""",
        )
        val handle = resp.get("result")
        assertTrue("result should be a String handle", handle is String)
        assertTrue((handle as String).isNotEmpty())
        assertFalse("handle must not leak the raw pointer", handle == "42")
    }

    @Test
    fun `agentCreateSession failure (0L) yields an error, id echoed`() {
        val resp = call(
            bridge(FakeNativeBridge(agentCreateSessionPtr = 0L)),
            """{"jsonrpc":"2.0","id":"a0","method":"agentCreateSession","params":{"sessionId":"s-1"}}""",
        )
        assertEquals("a0", resp.get("id"))
        assertFalse(resp.has("result"))
        assertTrue(resp.has("error"))
    }

    @Test
    fun `agent round-trip create-send-poll-list-free`() {
        val native = FakeNativeBridge(
            agentCreateSessionPtr = 99L,
            agentPollResult = """{"type":"token","data":"hi"}""",
            agentToolsJson = """[{"name":"web_search","description":"x","inputSchema":{},"provenance":"builtin_browser","sensitive":false,"permission":"auto_approve"}]""",
        )
        val b = bridge(native)

        val handle = call(b, """{"id":"c","method":"agentCreateSession","params":{"sessionId":"s"}}""")
            .get("result") as String

        val send = call(b, """{"id":"s","method":"agentSendMessage","params":{"handle":"$handle","message":"do it"}}""")
        assertEquals(true, send.get("result"))
        assertEquals(99L, native.lastAgentSendPtr)
        assertEquals("do it", native.lastAgentSendMessage)

        val poll = call(b, """{"id":"p","method":"agentPollEvent","params":{"handle":"$handle"}}""")
        assertEquals("""{"type":"token","data":"hi"}""", poll.get("result"))

        val tools = call(b, """{"id":"t","method":"agentListTools","params":{"handle":"$handle"}}""")
        assertTrue(tools.get("result") is org.json.JSONArray)
        assertEquals(1, (tools.get("result") as org.json.JSONArray).length())

        val free = call(b, """{"id":"f","method":"agentFreeSession","params":{"handle":"$handle"}}""")
        assertTrue(free.isNull("result"))
        assertTrue(native.freedAgentPtrs.contains(99L))

        // handle is now unregistered → subsequent use errors
        val afterFree = call(b, """{"id":"x","method":"agentSendMessage","params":{"handle":"$handle","message":"again"}}""")
        assertTrue(afterFree.has("error"))
    }

    @Test
    fun `agentPollEvent returns null when native queue empty`() {
        val b = bridge(FakeNativeBridge(agentCreateSessionPtr = 5L, agentPollResult = null))
        val h = call(b, """{"id":"c","method":"agentCreateSession","params":{"sessionId":"s"}}""").get("result")
        val poll = call(b, """{"id":"p","method":"agentPollEvent","params":{"handle":"$h"}}""")
        assertTrue(poll.isNull("result"))
    }

    @Test
    fun `unknown method yields error with id echoed`() {
        val resp = call(
            bridge(),
            """{"jsonrpc":"2.0","id":"u1","method":"totallyUnknown","params":{}}""",
        )
        assertEquals("u1", resp.get("id"))
        assertTrue(resp.has("error"))
    }

    @Test
    fun `agentListTools returns empty array when native returns null`() {
        val native = FakeNativeBridge(agentCreateSessionPtr = 3L, agentToolsJson = null)
        val b = bridge(native)
        val h = call(b, """{"id":"c","method":"agentCreateSession","params":{"sessionId":"s"}}""").get("result")
        val tools = call(b, """{"id":"t","method":"agentListTools","params":{"handle":"$h"}}""")
        assertTrue(tools.get("result") is org.json.JSONArray)
        assertEquals(0, (tools.get("result") as org.json.JSONArray).length())
    }

    @Test
    fun `old-name chatSessionNew alias still dispatches`() {
        val resp = call(
            bridge(),
            """{"jsonrpc":"2.0","id":9,"method":"chatSessionNew","params":["k","e","m","s"]}""",
        )
        // old bundle mapped chatSessionNew -> Long pointer; dual-mode keeps it working
        assertFalse(resp.has("error"))
        assertEquals(9, resp.get("id"))
    }
}
