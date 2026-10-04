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

    private fun bridge(
        native: WebViewNativeBridge = FakeNativeBridge(),
        browserToolExecutor: BrowserToolExecutor = BrowserToolExecutor(),
    ): WebViewBridge = WebViewBridge(
        native = native,
        evaluateJs = {},
        browserToolExecutor = browserToolExecutor,
    )

    private fun call(bridge: WebViewBridge, json: String): JSONObject =
        JSONObject(runBlocking { bridge.handleRequest(json) })

    @Test
    fun `Google Relay sign-in without a host credential source returns a structured error`() {
        val response = call(
            bridge(),
            """{"jsonrpc":"2.0","id":"google","method":"relaySignInWithGoogle","params":{}}""",
        )

        val result = response.getJSONObject("result")
        assertFalse(result.getBoolean("ok"))
        assertEquals("Google sign-in is unavailable", result.getString("error"))
    }

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
    fun `AI settings round-trip through named object RPC params`() {
        val b = bridge()

        assertTrue(call(b, """{"id":"p","method":"setAiProvider","params":{"provider":"openai-compatible"}}""").isNull("result"))
        assertTrue(call(b, """{"id":"u","method":"setAiBaseUrl","params":{"url":"https://ai.example.test/v1"}}""").isNull("result"))
        assertTrue(call(b, """{"id":"k","method":"setAiApiKey","params":{"key":"secret-value"}}""").isNull("result"))
        assertTrue(call(b, """{"id":"m","method":"setAiModel","params":{"model":"custom-model"}}""").isNull("result"))

        val settings = call(b, """{"id":"g","method":"getAiSettings","params":{}}""").getJSONObject("result")
        assertEquals("openai-compatible", settings.getString("provider"))
        assertEquals("https://ai.example.test/v1", settings.getString("baseUrl"))
        assertEquals("custom-model", settings.getString("model"))
        assertTrue(settings.getBoolean("hasApiKey"))
        assertTrue(settings.getBoolean("hasByokOpenai"))
        assertFalse(settings.getBoolean("hasByokAnthropic"))
        assertFalse(settings.has("apiKey"))
    }

    @Test
    fun `managed chat bootstrap without a relay session returns typed credential envelope`() {
        val response = call(
            bridge(FakeNativeBridge(managedChatConfig = null)),
            """{"jsonrpc":"2.0","id":"managed","method":"chatSessionStart","params":{"credentialProvider":"maho-managed"}}""",
        )

        val error = response.getJSONObject("error")
        assertEquals("native_error", error.getString("kind"))
        assertEquals("chatSessionStart", error.getString("method"))
        assertEquals(
            """{"version":1,"kind":"credential_error","code":"managed_auth_unavailable"}""",
            error.getString("reason"),
        )
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
    fun `artifact list and share resolve only through the opaque agent handle`() {
        val native = FakeNativeBridge(
            agentCreateSessionPtr = 77L,
            agentArtifactsJson = """[{"artifactId":"artifact-42","sessionId":"s","displayName":"plan.html","mimeType":"text/html","sizeBytes":12,"createdAt":1}]""",
        )
        val b = bridge(native)
        val handle = call(b, """{"id":"c","method":"agentCreateSession","params":{"sessionId":"s"}}""")
            .getString("result")

        val listed = call(b, """{"id":"l","method":"agentListArtifacts","params":{"handle":"$handle"}}""")
            .getJSONArray("result")
        assertEquals("artifact-42", listed.getJSONObject(0).getString("artifactId"))
        assertFalse(listed.getJSONObject(0).has("path"))

        val shared = call(
            b,
            """{"id":"x","method":"artifactShare","params":{"handle":"$handle","artifactId":"artifact-42"}}""",
        )
        assertEquals(true, shared.get("result"))
        assertEquals(77L, native.lastArtifactSharePtr)
        assertEquals("artifact-42", native.lastArtifactShareId)
    }

    @Test
    fun `artifact operations reject stale handles after free`() {
        val b = bridge(FakeNativeBridge(agentCreateSessionPtr = 88L))
        val handle = call(b, """{"id":"c","method":"agentCreateSession","params":{"sessionId":"s"}}""")
            .getString("result")
        call(b, """{"id":"f","method":"agentFreeSession","params":{"handle":"$handle"}}""")
        val response = call(
            b,
            """{"id":"x","method":"artifactShare","params":{"handle":"$handle","artifactId":"artifact-42"}}""",
        )
        assertTrue(response.has("error"))
    }

    @Test
    fun `agentPollEvent returns null when native queue empty`() {
        val b = bridge(FakeNativeBridge(agentCreateSessionPtr = 5L, agentPollResult = null))
        val h = call(b, """{"id":"c","method":"agentCreateSession","params":{"sessionId":"s"}}""").get("result")
        val poll = call(b, """{"id":"p","method":"agentPollEvent","params":{"handle":"$h"}}""")
        assertTrue(poll.isNull("result"))
    }

    @Test
    fun `conversation project RPC validates params and forwards CRUD plus move`() {
        val native = FakeNativeBridge()
        val b = bridge(native)

        val invalid = listOf(
            """{"id":"p0","method":"conversationProjectCreate","params":{"name":" "}}""",
            """{"id":"p1","method":"conversationProjectRename","params":{"id":"","name":"Name"}}""",
            """{"id":"p2","method":"conversationProjectDelete","params":{"id":7}}""",
            """{"id":"p3","method":"conversationProjectMove","params":{"ids":[],"projectId":null}}""",
            """{"id":"p4","method":"conversationProjectMove","params":{"ids":["one","one"],"projectId":"p"}}""",
        )
        invalid.forEach { request ->
            assertEquals("params_invalid", call(b, request).getJSONObject("error").getString("kind"))
        }

        assertEquals(1, call(b, """{"id":"l","method":"conversationProjectList","params":{}}""").getJSONArray("result").length())
        assertEquals("Created", call(b, """{"id":"c","method":"conversationProjectCreate","params":{"name":"Created"}}""").getJSONObject("result").getString("name"))
        assertTrue(call(b, """{"id":"r","method":"conversationProjectRename","params":{"id":"project-1","name":"Renamed"}}""").getBoolean("result"))
        assertTrue(call(b, """{"id":"d","method":"conversationProjectDelete","params":{"id":"project-1"}}""").getBoolean("result"))
        assertEquals(2, call(b, """{"id":"m","method":"conversationProjectMove","params":{"ids":["one","two"],"projectId":null}}""").getJSONObject("result").getInt("requestedCount"))
        assertEquals(listOf(listOf("one", "two") to null), native.conversationProjectMoveRequests)
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
    fun `browser tool invoke returns a structured unsupported result for an unknown tool`() {
        val response = call(
            bridge(),
            """{"jsonrpc":"2.0","id":"browser-tool","method":"browserToolInvoke","params":{"name":"not_a_tool","args":{}}}""",
        )

        assertFalse(response.has("error"))
        val result = response.getJSONObject("result")
        assertFalse(result.getBoolean("ok"))
        assertEquals("unknown_tool", result.getString("error"))
    }

    @Test
    fun `browser tool invoke returns the actual native tool result`() {
        val response = call(
            bridge(browserToolExecutor = BrowserToolExecutor { name, args ->
                assertEquals("open_tab", name)
                assertEquals("https://example.com", args.getString("url"))
                JSONObject().put("ok", true).put("result", JSONObject().put("url", "https://example.com"))
            }),
            """{"jsonrpc":"2.0","id":"open","method":"browserToolInvoke","params":{"name":"open_tab","args":{"url":"https://example.com"}}}""",
        )

        val result = response.getJSONObject("result")
        assertTrue(result.getBoolean("ok"))
        assertEquals("https://example.com", result.getJSONObject("result").getString("url"))
    }

    @Test
    fun `browser tool invoke truncates oversized array results at complete entries`() {
        val entry = JSONObject().put("title", "x".repeat(700))
        val response = call(
            bridge(browserToolExecutor = BrowserToolExecutor { _, _ ->
                JSONObject().put("ok", true).put("result", org.json.JSONArray().apply {
                    repeat(10) { put(entry) }
                })
            }),
            """{"jsonrpc":"2.0","id":"large","method":"browserToolInvoke","params":{"name":"list_tabs","args":{}}}""",
        )

        val result = response.getJSONObject("result")
        assertTrue(result.getBoolean("ok"))
        assertTrue(result.getBoolean("truncated"))
        assertTrue(result.getJSONArray("result").length() in 1..9)
        assertTrue(result.toString().toByteArray(Charsets.UTF_8).size <= 4_000)
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

    @Test
    fun `chatSessionStart reads named option fields and passes the complete config to native chat`() {
        val native = FakeNativeBridge(chatSessionNewPtr = 13L)
        val response = call(
            bridge(native),
            """{"jsonrpc":"2.0","id":"start","method":"chatSessionStart","params":{"apiKey":"legacy-secret","endpoint":"https://chat.example.test/v1","model":"chat-model","systemInstruction":"Answer precisely"}}""",
        )

        assertFalse(response.has("error"))
        assertTrue(response.getString("result").isNotEmpty())
        assertEquals(
            listOf(
                FakeNativeBridge.ChatSessionNewCall(
                    apiKey = "legacy-secret",
                    endpoint = "https://chat.example.test/v1",
                    model = "chat-model",
                    systemInstruction = "Answer precisely",
                ),
            ),
            native.chatSessionNewCalls,
        )
        assertTrue("an explicit legacy apiKey must not consult secure storage", native.resolvedCredentialProviders.isEmpty())
    }

    @Test
    fun `managed chat session ignores JS config and returns only an opaque handle`() {
        val relayToken = "relay-access-token-that-must-never-cross-rpc"
        val native = FakeNativeBridge(
            chatSessionNewPtr = 23L,
            managedChatConfig = ManagedChatConfig(
                apiKey = relayToken,
                endpoint = "https://proxy.maho.co/v1/chat/completions",
                model = "google/gemini-3-flash-lite:free",
            ),
        )

        val response = call(
            bridge(native),
            """{"jsonrpc":"2.0","id":"managed","method":"chatSessionStart","params":{"opts":{"apiKey":"attacker-key","endpoint":"https://attacker.example/steal","model":"attacker-model","systemInstruction":"Keep this","credentialProvider":"maho-managed"}}}""",
        )

        assertFalse(response.has("error"))
        val handle = response.getString("result")
        assertTrue(handle.isNotEmpty())
        assertFalse("managed session must not return the native pointer", handle == "23")
        assertEquals(3, response.length())
        assertTrue(response.has("jsonrpc"))
        assertTrue(response.has("id"))
        assertTrue(response.has("result"))
        assertFalse("managed token must not appear in the RPC response", response.toString().contains(relayToken))
        assertEquals(listOf("maho-managed"), native.resolvedCredentialProviders)
        assertEquals(
            listOf(
                FakeNativeBridge.ChatSessionNewCall(
                    apiKey = relayToken,
                    endpoint = "https://proxy.maho.co/v1/chat/completions",
                    model = "google/gemini-3-flash-lite:free",
                    systemInstruction = "Keep this",
                ),
            ),
            native.chatSessionNewCalls,
        )
    }

    @Test
    fun `managed native failure cannot leak the relay token in an RPC error`() {
        val relayToken = "relay-access-token-that-must-never-cross-rpc"
        val native = FakeNativeBridge(
            chatSessionNewError = IllegalStateException("native rejected bearer $relayToken"),
            managedChatConfig = ManagedChatConfig(
                apiKey = relayToken,
                endpoint = "https://proxy.maho.co/v1/chat/completions",
                model = "google/gemini-3-flash-lite:free",
            ),
        )

        val response = call(
            bridge(native),
            """{"jsonrpc":"2.0","id":"managed-error","method":"chatSessionStart","params":{"opts":{"credentialProvider":"maho-managed"}}}""",
        )

        assertTrue(response.has("error"))
        assertFalse(response.toString().contains(relayToken))
        assertEquals("chat session creation failed", response.getJSONObject("error").getString("message"))
    }

    @Test
    fun `chatSessionResume resolves compatible credential and restores history with supplied config`() {
        val history = """[
            {"role":"user","content":"stored question"},
            {"role":"assistant","content":"stored answer"}
        ]"""
        val native = FakeNativeBridge(chatSessionNewPtr = 17L, conversationHistoryJson = history)
        native.setAiApiKey("stored-compatible-secret")

        val response = call(
            bridge(native),
            """{"jsonrpc":"2.0","id":"resume","method":"chatSessionResume","params":{"handle":"conversation-42","endpoint":"https://compatible.example.test/v1","model":"compatible-model","systemInstruction":"Keep prior context","credentialProvider":"openai-compatible"}}""",
        )

        assertFalse(response.has("error"))
        assertTrue(response.getString("result").isNotEmpty())
        assertEquals(listOf("openai-compatible"), native.resolvedCredentialProviders)
        assertEquals(
            listOf(
                FakeNativeBridge.ChatSessionNewCall(
                    apiKey = "stored-compatible-secret",
                    endpoint = "https://compatible.example.test/v1",
                    model = "compatible-model",
                    systemInstruction = "Keep prior context",
                ),
            ),
            native.chatSessionNewCalls,
        )
        assertEquals(listOf("conversation-42"), native.conversationHistoryRequests)
        assertEquals(listOf("stored question"), native.chatAppendedUserMessages)
        assertEquals(listOf("stored answer"), native.chatAppendedAssistantMessages)
        assertTrue("history replay must not trigger a live model turn", native.chatSendUserTurns.isEmpty())
    }

    @Test
    fun `chatSessionResume returns a handle usable by chatSendMessage`() {
        val native = FakeNativeBridge()
        val b = bridge(native)
        val resumeResp = call(
            b,
            """{"jsonrpc":"2.0","id":"r1","method":"chatSessionResume","params":{"handle":"session-123"}}""",
        )
        assertFalse(resumeResp.has("error"))
        val handle = resumeResp.getString("result")
        assertTrue(handle.isNotEmpty())

        val sendResp = call(
            b,
            """{"jsonrpc":"2.0","id":"s1","method":"chatSendMessage","params":{"handle":"$handle","content":{"kind":"text","text":"continue"}}}""",
        )
        assertFalse(sendResp.has("error"))
        assertEquals(true, sendResp.get("result"))
        assertEquals(7L, native.lastChatSendPtr)
        assertEquals("continue", native.lastChatSendMessage)
    }

    @Test
    fun `chatSendToolResult forwards exact tool name and trigger`() {
        val native = FakeNativeBridge()
        val b = bridge(native)
        val handle = call(
            b,
            """{"jsonrpc":"2.0","id":"start","method":"chatSessionStart","params":{}}""",
        ).getString("result")

        val response = call(
            b,
            """{"jsonrpc":"2.0","id":"tool","method":"chatSendToolResult","params":{"handle":"$handle","toolCallId":"tabs","result":{"output":"[]"},"toolName":"list_tabs","trigger":false}}""",
        )

        assertEquals(true, response.get("result"))
        assertEquals(
            listOf(FakeNativeBridge.ChatToolResultCall(7L, "tabs", "list_tabs", "{\"output\":\"[]\"}", false)),
            native.chatToolResults,
        )
    }

    @Test
    fun `chatAppendAssistantMessage forwards complete tool calls into native history`() {
        val native = FakeNativeBridge()
        val b = bridge(native)
        val handle = call(
            b,
            """{"jsonrpc":"2.0","id":"start","method":"chatSessionStart","params":{}}""",
        ).getString("result")
        val toolCalls = """[{"id":"tabs","name":"list_tabs","arguments_json":"{}"}]"""

        val response = call(
            b,
            """{"jsonrpc":"2.0","id":"assistant","method":"chatAppendAssistantMessage","params":{"handle":"$handle","content":"Checking tabs.","toolCallsJson":${JSONObject.quote(toolCalls)}}}""",
        )

        assertFalse(response.has("error"))
        assertEquals(listOf("Checking tabs."), native.chatAppendedAssistantMessages)
        assertEquals(listOf(toolCalls), native.chatAppendedAssistantToolCalls)
    }

    @Test
    fun `chatSessionStart yields an error when native chatSessionNew returns 0L`() {
        val resp = call(
            bridge(FakeNativeBridge(chatSessionNewPtr = 0L)),
            """{"jsonrpc":"2.0","id":"cs0","method":"chatSessionStart","params":{}}""",
        )
        assertEquals("cs0", resp.get("id"))
        assertFalse("failed native session must not produce a handle", resp.has("result"))
        assertTrue(resp.has("error"))
    }

    @Test
    fun `chatSessionResume yields an error when native chatSessionNew returns 0L`() {
        val resp = call(
            bridge(FakeNativeBridge(chatSessionNewPtr = 0L)),
            """{"jsonrpc":"2.0","id":"cr0","method":"chatSessionResume","params":{"handle":"session-x"}}""",
        )
        assertEquals("cr0", resp.get("id"))
        assertFalse("failed native session must not produce a handle", resp.has("result"))
        assertTrue(resp.has("error"))
    }

    @Test
    fun `chatSessionResume replays history append-only (user append, assistant append, never send-user-turn)`() {
        val history = """[
            {"role":"user","content":"first question"},
            {"role":"assistant","content":"first answer"},
            {"role":"user","content":"second question"},
            {"role":"assistant","content":"second answer"}
        ]"""
        val native = FakeNativeBridge(chatSessionNewPtr = 11L, conversationHistoryJson = history)
        val b = bridge(native)

        val resp = call(b, """{"jsonrpc":"2.0","id":"rr","method":"chatSessionResume","params":{"handle":"conv-1"}}""")
        assertFalse(resp.has("error"))
        assertTrue(resp.getString("result").isNotEmpty())

        assertEquals(listOf("first question", "second question"), native.chatAppendedUserMessages)
        assertEquals(listOf("first answer", "second answer"), native.chatAppendedAssistantMessages)
        assertTrue(
            "historical replay must never trigger a live model turn",
            native.chatSendUserTurns.isEmpty(),
        )
    }

    @Test
    fun `new user message after resume still triggers chatSendUserTurn on the returned handle`() {
        val history = """[{"role":"user","content":"old"},{"role":"assistant","content":"old-answer"}]"""
        val native = FakeNativeBridge(chatSessionNewPtr = 11L, conversationHistoryJson = history)
        val b = bridge(native)

        val handle = call(b, """{"jsonrpc":"2.0","id":"rr","method":"chatSessionResume","params":{"handle":"conv-1"}}""")
            .getString("result")
        assertTrue("resume itself must not send a live turn", native.chatSendUserTurns.isEmpty())

        val send = call(
            b,
            """{"jsonrpc":"2.0","id":"s1","method":"chatSendMessage","params":{"handle":"$handle","content":{"kind":"text","text":"new turn"}}}""",
        )
        assertFalse(send.has("error"))
        assertEquals(true, send.get("result"))
        assertEquals(listOf("new turn"), native.chatSendUserTurns)
        assertEquals(11L, native.lastChatSendPtr)
        assertEquals("new turn", native.lastChatSendMessage)
    }
}
