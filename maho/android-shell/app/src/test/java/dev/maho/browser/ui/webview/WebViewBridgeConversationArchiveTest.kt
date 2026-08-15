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
class WebViewBridgeConversationArchiveTest {
    private fun call(native: FakeNativeBridge, json: String): JSONObject = JSONObject(runBlocking {
        WebViewBridge(native = native, evaluateJs = {}).handleRequest(json)
    })

    private fun assertParamsInvalid(response: JSONObject, method: String) {
        assertFalse(response.has("result"))
        val error = response.getJSONObject("error")
        assertEquals("params_invalid", error.getString("kind"))
        assertEquals(method, error.getString("method"))
    }

    private fun assertOperationFailure(response: JSONObject, method: String) {
        assertFalse(response.has("result"))
        val error = response.getJSONObject("error")
        assertEquals("native_error", error.getString("kind"))
        assertEquals(method, error.getString("method"))
    }

    @Test
    fun `list forwards valid state and limit and accepts valid empty native list`() {
        val native = FakeNativeBridge(conversationListJson = "[]")
        val response = call(native, """{"id":"l","method":"conversationList","params":{"state":"archived","limit":75}}""")
        assertEquals(0, response.getJSONArray("result").length())
        assertEquals("archived" to 75L, native.conversationListRequests.single())
    }

    @Test
    fun `list rejects invalid or missing state and invalid limit as params invalid`() {
        val invalidRequests = listOf(
            """{"id":"missing-state","method":"conversationList","params":{"limit":100}}""",
            """{"id":"bad-state","method":"conversationList","params":{"state":"deleted","limit":100}}""",
            """{"id":"blank-state","method":"conversationList","params":{"state":" ","limit":100}}""",
            """{"id":"missing-limit","method":"conversationList","params":{"state":"active"}}""",
            """{"id":"zero-limit","method":"conversationList","params":{"state":"active","limit":0}}""",
            """{"id":"large-limit","method":"conversationList","params":{"state":"active","limit":501}}""",
            """{"id":"fraction-limit","method":"conversationList","params":{"state":"active","limit":3.5}}""",
            """{"id":"string-limit","method":"conversationList","params":{"state":"active","limit":"3"}}""",
        )
        val native = FakeNativeBridge()
        invalidRequests.forEach { request ->
            assertParamsInvalid(call(native, request), "conversationList")
        }
        assertTrue(native.conversationListRequests.isEmpty())
    }

    @Test
    fun `invalid native list output is operation failure rather than empty success`() {
        assertOperationFailure(
            call(FakeNativeBridge(conversationListJson = null), """{"id":"null","method":"conversationList","params":{"state":"active","limit":100}}"""),
            "conversationList",
        )
        assertOperationFailure(
            call(FakeNativeBridge(conversationListJson = "not-json"), """{"id":"bad","method":"conversationList","params":{"state":"active","limit":100}}"""),
            "conversationList",
        )
        assertOperationFailure(
            call(FakeNativeBridge(conversationListJson = "{}"), """{"id":"shape","method":"conversationList","params":{"state":"active","limit":100}}"""),
            "conversationList",
        )
    }

    @Test
    fun `archive and unarchive dispatch nonblank IDs to native wrapper`() {
        val native = FakeNativeBridge()
        assertTrue(call(native, """{"id":"a","method":"conversationArchive","params":{"id":"c-1"}}""").getBoolean("result"))
        assertTrue(call(native, """{"id":"u","method":"conversationUnarchive","params":{"id":"c-2"}}""").getBoolean("result"))
        assertEquals(listOf("c-1"), native.archivedConversationIds)
        assertEquals(listOf("c-2"), native.unarchivedConversationIds)
    }

    @Test
    fun `archive and unarchive reject blank IDs as params invalid`() {
        val native = FakeNativeBridge()
        listOf("conversationArchive", "conversationUnarchive").forEach { method ->
            listOf("", "   ").forEachIndexed { index, id ->
                assertParamsInvalid(
                    call(native, JSONObject().put("id", "$method-$index").put("method", method).put("params", JSONObject().put("id", id)).toString()),
                    method,
                )
            }
        }
        assertTrue(native.archivedConversationIds.isEmpty())
        assertTrue(native.unarchivedConversationIds.isEmpty())
    }

    @Test
    fun `bulk preserves active session raw result shape and makes one native call`() {
        val native = FakeNativeBridge(conversationBulkJson = """{"error":"active_sessions","ids":["live"]}""")
        val response = call(native, """{"id":"b","method":"conversationBulk","params":{"op":"delete","ids":["live","idle"]}}""")
        assertEquals("""{"error":"active_sessions","ids":["live"]}""", response.getJSONObject("result").toString())
        assertEquals(listOf("delete" to listOf("live", "idle")), native.conversationBulkRequests)
    }

    @Test
    fun `bulk accepts one through five hundred unique nonblank IDs in one call`() {
        listOf(1, 500).forEach { count ->
            val ids = (1..count).map { "conversation-$it" }
            val params = JSONObject().put("op", "archive").put("ids", org.json.JSONArray(ids))
            val native = FakeNativeBridge()
            val response = call(native, JSONObject().put("id", "bulk-$count").put("method", "conversationBulk").put("params", params).toString())
            assertTrue(response.has("result"))
            assertEquals(listOf("archive" to ids), native.conversationBulkRequests)
        }
    }

    @Test
    fun `bulk rejects invalid operation and IDs as params invalid without native call`() {
        val invalidParams = listOf(
            JSONObject().put("op", "move").put("ids", org.json.JSONArray().put("c-1")),
            JSONObject().put("op", "archive").put("ids", org.json.JSONArray()),
            JSONObject().put("op", "archive").put("ids", org.json.JSONArray().put("")),
            JSONObject().put("op", "archive").put("ids", org.json.JSONArray().put("   ")),
            JSONObject().put("op", "archive").put("ids", org.json.JSONArray().put("same").put("same")),
            JSONObject().put("op", "archive").put("ids", org.json.JSONArray((1..501).map { "c-$it" })),
            JSONObject().put("op", "archive").put("ids", org.json.JSONArray().put("c-1").put(7)),
        )
        val native = FakeNativeBridge()
        invalidParams.forEachIndexed { index, params ->
            assertParamsInvalid(
                call(native, JSONObject().put("id", "bad-bulk-$index").put("method", "conversationBulk").put("params", params).toString()),
                "conversationBulk",
            )
        }
        assertTrue(native.conversationBulkRequests.isEmpty())
    }

    @Test
    fun `invalid native bulk output is operation failure`() {
        listOf<String?>(null, "not-json", "[]").forEachIndexed { index, output ->
            val response = call(
                FakeNativeBridge(conversationBulkJson = output),
                """{"id":"bad-bulk-$index","method":"conversationBulk","params":{"op":"archive","ids":["c-1"]}}""",
            )
            assertOperationFailure(response, "conversationBulk")
        }
    }

    @Test
    fun `auto archive policy accepts only null minus one or integral three seven thirty`() {
        val cases = listOf(
            "null" to -1,
            "-1" to -1,
            "3" to 3,
            "7" to 7,
            "30" to 30,
        )
        cases.forEach { (jsonValue, expected) ->
            val native = FakeNativeBridge()
            val response = call(native, """{"id":"p-$expected","method":"conversationSetAutoArchivePolicy","params":{"afterDays":$jsonValue}}""")
            assertTrue(response.getBoolean("result"))
            assertEquals(expected, native.autoArchivePolicy)
        }
    }

    @Test
    fun `auto archive policy rejects unsupported missing or nonintegral values as params invalid`() {
        val invalidParams = listOf(
            "{}",
            """{"afterDays":0}""",
            """{"afterDays":1}""",
            """{"afterDays":-2}""",
            """{"afterDays":4}""",
            """{"afterDays":3.0}""",
            """{"afterDays":3.5}""",
            """{"afterDays":"3"}""",
            """{"afterDays":true}""",
        )
        invalidParams.forEachIndexed { index, params ->
            val native = FakeNativeBridge()
            assertParamsInvalid(
                call(native, """{"id":"bad-policy-$index","method":"conversationSetAutoArchivePolicy","params":$params}"""),
                "conversationSetAutoArchivePolicy",
            )
            assertEquals(-1, native.autoArchivePolicy)
        }
    }
}
