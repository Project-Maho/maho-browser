package dev.maho.browser.ui.webview

/**
 * WebViewNativeBridge — the exact native surface [WebViewBridge] depends on.
 *
 * This interface exists so the JSON-RPC dispatcher can be exercised by JVM unit
 * tests with a fake, without loading the `maho_jni` native library that the real
 * [dev.maho.browser.MahoBridge] object pulls in at class-init time.
 *
 * Production wiring: [MahoNativeBridge] forwards each call to `MahoBridge`.
 * List/config/offline results are surfaced as JSON strings so this contract stays
 * free of native-coupled domain types.
 *
 * Native session pointers are opaque `Long`s (0L means failure). WebViewBridge
 * never leaks a pointer to JS — it maps each pointer to an opaque string handle.
 */
interface WebViewNativeBridge {
    // --- BYOK ---
    fun byokGetProviders(): String?
    fun byokGetKey(provider: String): String?
    fun byokSetKey(provider: String, key: String): Boolean
    fun byokDeleteKey(provider: String): Boolean
    fun byokValidateKey(provider: String, key: String): Boolean

    // --- Chat (pointer-based; MahoBridge chat is currently a stub) ---
    fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long
    fun chatSessionFree(sessionPtr: Long)
    fun chatSendUserTurn(sessionPtr: Long, message: String): Boolean
    fun chatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean
    fun chatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean
    fun chatCancel(sessionPtr: Long): Boolean
    fun chatSessionPollEvent(sessionPtr: Long): String?
    fun chatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean
    fun chatSendToolResult(sessionPtr: Long, toolCallId: String, name: String, output: String, isSuccess: Boolean): Boolean
    fun chatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean

    // --- Conversations ---
    fun createConversation(id: String, title: String?, spaceId: String?, model: String?): Boolean
    fun listConversationsJson(limit: Long): String?
    fun getConversationMessagesJson(sessionId: String): String?
    fun saveConversationMessage(sessionId: String, role: String, content: String, urlContext: String?): Boolean
    fun deleteConversation(id: String): Boolean
    fun renameConversation(id: String, title: String): Boolean

    // --- Space AI Config ---
    fun getSpaceAIConfigJson(spaceId: String): String?
    fun setSpaceAIConfigJson(spaceId: String, configJson: String): Boolean

    // --- Offline Model ---
    fun offlineModelInfoJson(): String?
    fun offlineModelIsDownloaded(): Boolean
    fun offlineModelDelete(): Boolean

    // --- Agent Session (pointer-based; 0L == failure) ---
    fun agentCreateSession(sessionId: String): Long
    fun agentFreeSession(sessionPtr: Long)
    fun agentSendMessage(sessionPtr: Long, message: String): Boolean
    fun agentCancel(sessionPtr: Long): Boolean
    fun agentListTools(sessionPtr: Long): String?
    fun agentPollEvent(sessionPtr: Long): String?
}
