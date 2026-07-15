package dev.maho.browser.ui.webview

/**
 * FakeNativeBridge — in-memory [WebViewNativeBridge] for JVM unit tests.
 *
 * Records agent/chat calls and returns configurable results so the JSON-RPC
 * dispatcher can be exercised without the `maho_jni` native library.
 *
 * `agentCreateSessionPtr` controls the pointer returned by [agentCreateSession]
 * (set to 0L to simulate native failure). Calls are recorded for assertions.
 */
class FakeNativeBridge(
    private val agentCreateSessionPtr: Long = 42L,
    private val agentPollResult: String? = null,
    private val agentToolsJson: String? = null,
) : WebViewNativeBridge {

    val savedKeys: MutableMap<String, String> = mutableMapOf("openai" to "sk-openai-existing")

    var lastAgentSendPtr: Long = -1L
    var lastAgentSendMessage: String? = null
    val freedAgentPtrs: MutableList<Long> = mutableListOf()

    // --- BYOK ---
    override fun byokGetProviders(): String = "[\"openai\",\"anthropic\"]"
    override fun byokGetKey(provider: String): String? = savedKeys[provider]
    override fun byokSetKey(provider: String, key: String): Boolean {
        savedKeys[provider] = key
        return true
    }
    override fun byokDeleteKey(provider: String): Boolean = savedKeys.remove(provider) != null
    override fun byokValidateKey(provider: String, key: String): Boolean = key.isNotBlank()

    // --- Chat ---
    override fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long = 7L
    override fun chatSessionFree(sessionPtr: Long) = Unit
    override fun chatSendUserTurn(sessionPtr: Long, message: String): Boolean = true
    override fun chatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean = true
    override fun chatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean = true
    override fun chatCancel(sessionPtr: Long): Boolean = true
    override fun chatSessionPollEvent(sessionPtr: Long): String? = null
    override fun chatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean = true
    override fun chatSendToolResult(sessionPtr: Long, toolCallId: String, name: String, output: String, isSuccess: Boolean): Boolean = true
    override fun chatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean = true

    // --- Conversations ---
    override fun createConversation(id: String, title: String?, spaceId: String?, model: String?): Boolean = true
    override fun listConversationsJson(limit: Long): String = "[]"
    override fun getConversationMessagesJson(sessionId: String): String = "[]"
    override fun saveConversationMessage(sessionId: String, role: String, content: String, urlContext: String?): Boolean = true
    override fun deleteConversation(id: String): Boolean = true
    override fun renameConversation(id: String, title: String): Boolean = true

    // --- Space AI Config ---
    override fun getSpaceAIConfigJson(spaceId: String): String? = null
    override fun setSpaceAIConfigJson(spaceId: String, configJson: String): Boolean = true

    // --- Offline Model ---
    override fun offlineModelInfoJson(): String? = null
    override fun offlineModelIsDownloaded(): Boolean = false
    override fun offlineModelDelete(): Boolean = true

    // --- Agent Session ---
    override fun agentCreateSession(sessionId: String): Long = agentCreateSessionPtr
    override fun agentFreeSession(sessionPtr: Long) { freedAgentPtrs.add(sessionPtr) }
    override fun agentSendMessage(sessionPtr: Long, message: String): Boolean {
        lastAgentSendPtr = sessionPtr
        lastAgentSendMessage = message
        return true
    }
    override fun agentCancel(sessionPtr: Long): Boolean = true
    override fun agentListTools(sessionPtr: Long): String? = agentToolsJson
    override fun agentPollEvent(sessionPtr: Long): String? = agentPollResult
}
