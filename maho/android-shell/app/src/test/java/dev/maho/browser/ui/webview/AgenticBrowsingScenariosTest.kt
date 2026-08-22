package dev.maho.browser.ui.webview

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
class AgenticBrowsingScenariosTest {

    @Test
    fun testScenario1_ExtractPageInteractiveSnapshot() {
        val fakeHtmlResponse = """
        {
            "ok": true,
            "result": {
                "url": "https://service.example.com/checkout",
                "title": "Checkout Order",
                "viewport": {"width": 412, "height": 915, "scrollX": 0, "scrollY": 0},
                "elements": [
                    {
                        "id": 1,
                        "tag": "input",
                        "type": "text",
                        "label": "Shipping Address",
                        "selector": "#shipping-addr",
                        "bounds": {"x": 16.0, "y": 120.0, "width": 380.0, "height": 48.0}
                    },
                    {
                        "id": 2,
                        "tag": "input",
                        "type": "tel",
                        "label": "Phone Number",
                        "selector": "#phone-number",
                        "bounds": {"x": 16.0, "y": 180.0, "width": 380.0, "height": 48.0}
                    },
                    {
                        "id": 3,
                        "tag": "button",
                        "type": "submit",
                        "label": "Pay Now",
                        "selector": "#pay-btn",
                        "bounds": {"x": 16.0, "y": 300.0, "width": 380.0, "height": 52.0}
                    }
                ]
            }
        }
        """.trimIndent()

        val result = invokeAgenticBrowsingTool(
            name = "get_page_elements",
            args = JSONObject(),
            evaluate = { script: String ->
                assertTrue(script.contains("querySelectorAll"))
                AgenticPageEvaluation.Success(fakeHtmlResponse)
            }
        )

        assertTrue(result.optBoolean("ok"))
        val payload = result.getJSONObject("result")
        assertEquals("Checkout Order", payload.getString("title"))
        val elements = payload.getJSONArray("elements")
        assertEquals(3, elements.length())
        assertEquals("Shipping Address", elements.getJSONObject(0).getString("label"))
        assertEquals("button", elements.getJSONObject(2).getString("tag"))
    }

    @Test
    fun testScenario2_FormFillingWithSyntheticEvents() {
        val fakeFillResponse = """
        {
            "ok": true,
            "result": {
                "id": null,
                "selector": "#shipping-addr",
                "filled": true,
                "length": 25
            }
        }
        """.trimIndent()

        val args = JSONObject().apply {
            put("selector", "#shipping-addr")
            put("text", "123 Gangnam-daero, Seoul")
        }

        val result = invokeAgenticBrowsingTool(
            name = "fill_input",
            args = args,
            evaluate = { script: String ->
                assertTrue(script.contains("InputEvent('input'"))
                assertTrue(script.contains("Event('change'"))
                assertTrue(script.contains("123 Gangnam-daero, Seoul"))
                AgenticPageEvaluation.Success(fakeFillResponse)
            }
        )

        assertTrue(result.optBoolean("ok"))
        val payload = result.getJSONObject("result")
        assertTrue(payload.getBoolean("filled"))
        assertEquals(25, payload.getInt("length"))
        assertFalse(payload.has("text"))
    }

    @Test
    fun testScenario3_TouchClickAndPageScrollSequence() {
        val fakeClickResponse = """
        {"ok": true, "result": {"id": 3, "selector": null, "clicked": true}}
        """.trimIndent()

        val clickResult = invokeAgenticBrowsingTool(
            name = "click_element",
            args = JSONObject().put("id", 3),
            evaluate = { script: String ->
                assertTrue(script.contains("refAttr"))
                assertTrue(script.contains("touchstart"))
                assertTrue(script.contains("click"))
                AgenticPageEvaluation.Success(fakeClickResponse)
            }
        )

        assertTrue(clickResult.optBoolean("ok"))
        val clickPayload = clickResult.getJSONObject("result")
        assertTrue(clickPayload.getBoolean("clicked"))
        assertEquals(3, clickPayload.getInt("id"))

        val fakeScrollResponse = """
        {
            "ok": true,
            "result": {
                "direction": "down",
                "amount": 400.0,
                "from": {"x": 0.0, "y": 0.0},
                "target": {"x": 0.0, "y": 400.0}
            }
        }
        """.trimIndent()

        val scrollResult = invokeAgenticBrowsingTool(
            name = "scroll_page",
            args = JSONObject().apply {
                put("direction", "down")
                put("amount", 400)
            },
            evaluate = { script: String ->
                assertTrue(script.contains("scrollBy"))
                assertTrue(script.contains("400"))
                AgenticPageEvaluation.Success(fakeScrollResponse)
            }
        )

        assertTrue(scrollResult.optBoolean("ok"))
        val scrollPayload = scrollResult.getJSONObject("result")
        assertEquals("down", scrollPayload.getString("direction"))
        val target = scrollPayload.getJSONObject("target")
        assertEquals(400.0, target.getDouble("y"), 0.001)
    }
}
