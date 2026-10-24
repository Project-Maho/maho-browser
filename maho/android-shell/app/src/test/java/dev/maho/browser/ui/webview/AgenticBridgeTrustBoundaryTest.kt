package dev.maho.browser.ui.webview

import dev.maho.browser.bridge.BridgeAgent
import dev.maho.browser.support.buildTaskSubmitJson
import dev.maho.browser.support.defaultAgentRelayUrl
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** Agentic browsing trust boundaries: D2 (JS bridge origin), D4 (tool allowlist), D8 (wire JSON / relay default). */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = android.app.Application::class)
class AgenticBridgeTrustBoundaryTest {
    @Test
    fun `D4 invokeAgentBrowserTool rejects tools outside the agentic browsing set`() {
        for (name in listOf("navigate", "close_tab", "search_history", "open_tab", "create_bookmark", "list_tabs", "nope")) {
            val result = JSONObject(requireNotNull(BridgeAgent.invokeAgentBrowserTool(name, "{}")))
            assertFalse(name, result.getBoolean("ok"))
            assertEquals(name, "unknown_tool", result.getString("error"))
        }
        assertTrue(BridgeAgent.isAdvertisedAgentBrowserTool("click_element"))
        assertTrue(BridgeAgent.isAdvertisedAgentBrowserTool("get_page_snapshot"))
        assertFalse(BridgeAgent.isAdvertisedAgentBrowserTool("navigate"))
    }

    @Test
    fun `D2 only the asset origin is trusted and dev server is debug-only`() {
        val asset = "https://appassets.androidplatform.net/assets/web-ai/index.html#agent"
        assertTrue(AiWebViewFactory.isTrustedBridgeUrl(asset, allowDev = false))
        assertFalse(AiWebViewFactory.isTrustedBridgeUrl("https://evil.example/assets/web-ai/index.html", false))
        assertFalse(AiWebViewFactory.isTrustedBridgeUrl("https://appassets.androidplatform.net.evil.example/", false))
        assertFalse(AiWebViewFactory.isTrustedBridgeUrl("http://appassets.androidplatform.net/assets/", false))
        assertFalse(AiWebViewFactory.isTrustedBridgeUrl("file:///android_asset/web-ai/index.html", false))
        assertFalse(AiWebViewFactory.isTrustedBridgeUrl(null, false))
        assertFalse(AiWebViewFactory.isTrustedBridgeUrl("http://10.0.2.2:5173/", allowDev = false))
        assertTrue(AiWebViewFactory.isTrustedBridgeUrl("http://10.0.2.2:5173/#agent", allowDev = true))
    }

    @Test
    fun `D2 gated bridge drops calls while the page is untrusted`() {
        var trusted = false
        val received = mutableListOf<String>()
        val bridge = OriginGatedBridge({ trusted }, { received += it })
        bridge.rpc("{}")
        assertTrue(received.isEmpty())
        trusted = true
        bridge.rpc("{}")
        assertEquals(1, received.size)
    }

    @Test
    fun `D8 task submit JSON escapes goal and url seed and relay default is TLS in release`() {
        val goal = "say \"hi\"\\ \n next"
        val seed = "https://a.example/?q=\"x\"\n"
        val parsed = JSONObject(buildTaskSubmitJson("t1", goal, seed))
        assertEquals(goal, parsed.getString("goal"))
        assertEquals(seed, parsed.getString("url_seed"))
        assertTrue(JSONObject(buildTaskSubmitJson("t1", "g", null)).isNull("url_seed"))
        assertEquals("wss://relay.mahobrowser.com/ws/agent", defaultAgentRelayUrl(debug = false))
        assertTrue(defaultAgentRelayUrl(debug = true).startsWith("ws://10.0.2.2"))
    }

    @Test
    fun `AR7 model supplied tab urls are limited to http and https`() {
        assertTrue(isAgentNavigableUrl("https://example.com/path"))
        assertTrue(isAgentNavigableUrl("  HTTP://example.com  "))
        for (url in listOf(
            "javascript:alert(1)",
            "file:///sdcard/secret.txt",
            "intent://scan/#Intent;scheme=zxing;end",
            "content://media/external",
            "data:text/html,hi",
            "https://",
            "https:///etc/passwd",
            "example.com",
        )) {
            assertFalse(url, isAgentNavigableUrl(url))
        }
    }
}
