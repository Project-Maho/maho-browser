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

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], application = android.app.Application::class)
class AgenticBrowsingBridgeTest {
    private fun bridge(executor: BrowserToolExecutor): WebViewBridge = WebViewBridge(
        native = FakeNativeBridge(),
        evaluateJs = {},
        browserToolExecutor = executor,
    )

    private fun call(bridge: WebViewBridge, json: String): JSONObject =
        JSONObject(runBlocking { bridge.handleRequest(json) })

    @Test
    fun `browserToolInvoke preserves exact agentic browsing result formats`() {
        val executor = BrowserToolExecutor { name, args ->
            when (name) {
                "get_page_elements" -> JSONObject()
                    .put("ok", true)
                    .put("result", JSONObject()
                        .put("url", "https://example.com/form")
                        .put("title", "Example form")
                        .put("viewport", JSONObject()
                            .put("width", 390)
                            .put("height", 844)
                            .put("scrollX", 0)
                            .put("scrollY", 120))
                        .put("elements", org.json.JSONArray().put(JSONObject()
                            .put("id", 1)
                            .put("tag", "button")
                            .put("label", "Continue")
                            .put("selector", "[data-maho-agent-ref=\"1\"]")
                            .put("bounds", JSONObject()
                                .put("x", 12.0)
                                .put("y", 20.0)
                                .put("width", 80.0)
                                .put("height", 44.0)))))
                "click_element" -> {
                    assertEquals(1, args.getInt("id"))
                    JSONObject().put("ok", true).put(
                        "result",
                        JSONObject().put("id", 1).put("selector", JSONObject.NULL).put("clicked", true),
                    )
                }
                "fill_input" -> {
                    assertEquals("#email", args.getString("selector"))
                    assertEquals("hello@example.com", args.getString("text"))
                    JSONObject().put("ok", true).put(
                        "result",
                        JSONObject()
                            .put("id", JSONObject.NULL)
                            .put("selector", "#email")
                            .put("filled", true)
                            .put("length", 17),
                    )
                }
                "scroll_page" -> {
                    assertEquals("down", args.getString("direction"))
                    assertEquals(320, args.getInt("amount"))
                    JSONObject().put("ok", true).put(
                        "result",
                        JSONObject()
                            .put("direction", "down")
                            .put("amount", 320.0)
                            .put("from", JSONObject().put("x", 0.0).put("y", 100.0))
                            .put("target", JSONObject().put("x", 0.0).put("y", 420.0)),
                    )
                }
                else -> JSONObject().put("ok", false).put("error", "unexpected_tool")
            }
        }
        val bridge = bridge(executor)

        val snapshot = call(
            bridge,
            """{"id":"snapshot","method":"browserToolInvoke","params":{"name":"get_page_elements","args":{}}}""",
        ).getJSONObject("result")
        assertTrue(snapshot.getBoolean("ok"))
        val snapshotPayload = snapshot.getJSONObject("result")
        assertEquals("https://example.com/form", snapshotPayload.getString("url"))
        assertEquals(120, snapshotPayload.getJSONObject("viewport").getInt("scrollY"))
        val element = snapshotPayload.getJSONArray("elements").getJSONObject(0)
        assertEquals(1, element.getInt("id"))
        assertEquals("Continue", element.getString("label"))
        assertEquals(44.0, element.getJSONObject("bounds").getDouble("height"), 0.0)

        val click = call(
            bridge,
            """{"id":"click","method":"browserToolInvoke","params":{"name":"click_element","args":{"id":1}}}""",
        ).getJSONObject("result").getJSONObject("result")
        assertTrue(click.getBoolean("clicked"))
        assertEquals(1, click.getInt("id"))

        val fill = call(
            bridge,
            """{"id":"fill","method":"browserToolInvoke","params":{"name":"fill_input","args":{"selector":"#email","text":"hello@example.com"}}}""",
        ).getJSONObject("result").getJSONObject("result")
        assertTrue(fill.getBoolean("filled"))
        assertEquals(17, fill.getInt("length"))
        assertFalse("fill result must not echo typed text", fill.has("text"))

        val scroll = call(
            bridge,
            """{"id":"scroll","method":"browserToolInvoke","params":{"name":"scroll_page","args":{"direction":"down","amount":320}}}""",
        ).getJSONObject("result").getJSONObject("result")
        assertEquals("down", scroll.getString("direction"))
        assertEquals(420.0, scroll.getJSONObject("target").getDouble("y"), 0.0)
    }

    @Test
    fun `agentic evaluator maps native errors and generates required DOM events`() {
        val click = invokeAgenticBrowsingTool(
            "click_element",
            JSONObject().put("id", 2),
        ) { script ->
            assertTrue(script.contains("data-maho-agent-ref"))
            assertTrue(script.contains("touchstart"))
            assertTrue(script.contains("PointerEvent('pointerdown'"))
            assertTrue(script.contains("MouseEvent('mousedown'"))
            AgenticPageEvaluation.Failure("page_script_timeout")
        }
        assertFalse(click.getBoolean("ok"))
        assertEquals("page_script_timeout", click.getString("error"))

        val fill = invokeAgenticBrowsingTool(
            "fill_input",
            JSONObject().put("selector", "#name").put("text", "Ada"),
        ) { script ->
            assertTrue(script.contains("InputEvent('input'"))
            assertTrue(script.contains("new Event('change'"))
            AgenticPageEvaluation.Success(
                """{"ok":true,"result":{"id":null,"selector":"#name","filled":true,"length":3}}""",
            )
        }
        assertTrue(fill.getBoolean("ok"))
        assertEquals(3, fill.getJSONObject("result").getInt("length"))

        val scroll = invokeAgenticBrowsingTool(
            "scroll_page",
            JSONObject().put("direction", "down"),
        ) { script ->
            assertTrue(script.contains("behavior: 'smooth'"))
            AgenticPageEvaluation.Success(
                """{"ok":true,"result":{"direction":"down","amount":600,"from":{"x":0,"y":0},"target":{"x":0,"y":600}}}""",
            )
        }
        assertEquals("down", scroll.getJSONObject("result").getString("direction"))
    }

    @Test
    fun `agentic tool descriptors separate autorun reads from mutating actions`() {
        val tools = JSONObject(agenticBrowsingToolDescriptorsJson()).getJSONArray("tools")
        val policies = (0 until tools.length()).associate { index ->
            val tool = tools.getJSONObject(index)
            tool.getString("name") to tool.getJSONObject("policy")
        }

        assertEquals("auto_approve", policies.getValue("get_page_elements").getString("permission"))
        assertEquals("auto_approve", policies.getValue("get_page_snapshot").getString("permission"))
        assertEquals("auto_approve", policies.getValue("scroll_page").getString("permission"))
        assertEquals("always_ask", policies.getValue("click_element").getString("permission"))
        assertEquals("always_ask", policies.getValue("fill_input").getString("permission"))
        assertTrue(policies.getValue("click_element").getBoolean("sensitive"))
        assertTrue(policies.getValue("fill_input").getBoolean("sensitive"))
    }
}
