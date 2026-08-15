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
data class ManagedChatConfig(
    val apiKey: String,
    val endpoint: String,
    val model: String,
)

interface WebViewNativeBridge {
    // --- BYOK ---
    fun byokGetProviders(): String?
    fun byokGetKey(provider: String): String?
    fun byokSetKey(provider: String, key: String): Boolean
    fun byokDeleteKey(provider: String): Boolean
    fun byokValidateKey(provider: String, key: String): Boolean

    fun getAiSettingsJson(): String
    fun setAiProvider(provider: String): Boolean
    fun setAiBaseUrl(url: String): Boolean
    fun setAiApiKey(key: String): Boolean
    fun setAiModel(model: String): Boolean
    fun resolveChatCredential(provider: String): String?
    fun resolveManagedChatConfig(): ManagedChatConfig?

    // --- Chat (pointer-based; MahoBridge chat is currently a stub) ---
    fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long
    fun chatSessionFree(sessionPtr: Long)
    fun chatSendUserTurn(sessionPtr: Long, message: String): Boolean
    fun chatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean
    fun chatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean
    fun chatCancel(sessionPtr: Long): Boolean
    fun chatSessionPollEvent(sessionPtr: Long): String?
    fun chatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean
    fun chatSendToolResult(sessionPtr: Long, toolCallId: String, name: String, output: String, trigger: Boolean): Boolean
    // Append-only replay: records a persisted user message without running the model (mirrors FFI maho_core_chat_append_user_message). chatSendUserTurn is for live turns only.
    fun chatAppendUserMessage(sessionPtr: Long, content: String): Boolean
    fun chatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean

    // --- Conversations ---
    fun createConversation(id: String, title: String?, spaceId: String?, model: String?): Boolean
    fun listConversationsJson(limit: Long): String?
    fun listConversationsJson(state: String, limit: Long): String?
    fun archiveConversation(id: String): Boolean
    fun unarchiveConversation(id: String): Boolean
    fun applyConversationBulkOperationJson(operation: String, ids: List<String>): String?
    fun getConversationAutoArchivePolicy(): Int
    fun setConversationAutoArchivePolicy(days: Int): Boolean
    fun getConversationMessagesJson(sessionId: String): String?
    fun saveConversationMessage(sessionId: String, role: String, content: String, urlContext: String?): Boolean
    fun deleteConversation(id: String): Boolean
    fun renameConversation(id: String, title: String): Boolean
    fun listConversationProjectsJson(): String?
    fun createConversationProjectJson(name: String): String?
    fun renameConversationProject(id: String, name: String): Boolean
    fun deleteConversationProject(id: String): Boolean
    fun moveConversationsToProjectJson(ids: List<String>, projectId: String?): String?

    // --- Composer drafts (device/profile-local; never synced) ---
    // `scopeJson` is the exact core scope payload:
    //   {"kind":"new_task"} | {"kind":"conversation","conversationId":"<id>"}
    fun getComposerDraftJson(scopeJson: String): String?
    fun setComposerDraft(scopeJson: String, text: String): Boolean
    fun deleteComposerDraft(scopeJson: String): Boolean

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
    fun agentListArtifacts(sessionPtr: Long): String?
    fun artifactShare(sessionPtr: Long, artifactId: String): Boolean
    fun agentPollEvent(sessionPtr: Long): String?
}
