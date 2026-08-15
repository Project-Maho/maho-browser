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
    private val agentArtifactsJson: String? = null,
    private val artifactShareResult: Boolean = true,
    private val chatSessionNewPtr: Long = 7L,
    private val conversationHistoryJson: String = "[]",
    private val conversationListJson: String? = "[]",
    private val conversationBulkJson: String? = "{\"requestedCount\":0,\"affectedIds\":[],\"unchangedIds\":[],\"missingIds\":[]}",
    private val chatSessionNewError: Exception? = null,
    private val composerDraftJson: String? = null,
    private val composerDraftSupported: Boolean = true,
    private val managedChatConfig: ManagedChatConfig? = ManagedChatConfig(
        apiKey = "relay-access-token",
        endpoint = "https://proxy.maho.co/v1/chat/completions",
        model = "google/gemini-3-flash-lite:free",
    ),
) : WebViewNativeBridge {

    val savedKeys: MutableMap<String, String> = mutableMapOf("openai" to "sk-openai-existing")
    private var aiProvider: String = ""
    private var aiBaseUrl: String = ""
    private var aiApiKey: String = ""
    private var aiModel: String = ""

    var lastAgentSendPtr: Long = -1L
    var lastAgentSendMessage: String? = null
    var lastArtifactSharePtr: Long = -1L
    var lastArtifactShareId: String? = null
    val freedAgentPtrs: MutableList<Long> = mutableListOf()
    var lastChatSendPtr: Long = -1L
    var lastChatSendMessage: String? = null
    val chatSendUserTurns: MutableList<String> = mutableListOf()
    val chatAppendedUserMessages: MutableList<String> = mutableListOf()
    val chatAppendedAssistantMessages: MutableList<String> = mutableListOf()
    val chatAppendedAssistantToolCalls: MutableList<String> = mutableListOf()
    val chatSessionNewCalls: MutableList<ChatSessionNewCall> = mutableListOf()
    val resolvedCredentialProviders: MutableList<String> = mutableListOf()
    val conversationHistoryRequests: MutableList<String> = mutableListOf()
    val conversationListRequests: MutableList<Pair<String, Long>> = mutableListOf()
    val archivedConversationIds: MutableList<String> = mutableListOf()
    val unarchivedConversationIds: MutableList<String> = mutableListOf()
    val conversationBulkRequests: MutableList<Pair<String, List<String>>> = mutableListOf()
    val conversationProjectMoveRequests: MutableList<Pair<List<String>, String?>> = mutableListOf()
    var autoArchivePolicy: Int = -1
    val chatToolResults: MutableList<ChatToolResultCall> = mutableListOf()
    val composerDraftGets: MutableList<String> = mutableListOf()
    val composerDraftSets: MutableList<Pair<String, String>> = mutableListOf()
    val composerDraftDeletes: MutableList<String> = mutableListOf()

    data class ChatSessionNewCall(
        val apiKey: String,
        val endpoint: String,
        val model: String,
        val systemInstruction: String,
    )

    data class ChatToolResultCall(
        val sessionPtr: Long,
        val toolCallId: String,
        val name: String,
        val output: String,
        val trigger: Boolean,
    )

    // --- BYOK ---
    override fun byokGetProviders(): String = "[\"openai\",\"anthropic\"]"
    override fun byokGetKey(provider: String): String? = savedKeys[provider]
    override fun byokSetKey(provider: String, key: String): Boolean {
        savedKeys[provider] = key
        return true
    }
    override fun byokDeleteKey(provider: String): Boolean = savedKeys.remove(provider) != null
    override fun byokValidateKey(provider: String, key: String): Boolean = key.isNotBlank()

    override fun getAiSettingsJson(): String = org.json.JSONObject()
        .put("provider", aiProvider)
        .put("baseUrl", aiBaseUrl)
        .put("model", aiModel)
        .put("hasApiKey", aiApiKey.isNotEmpty())
        .put("hasByokOpenai", savedKeys["openai"].isNullOrEmpty().not())
        .put("hasByokAnthropic", savedKeys["anthropic"].isNullOrEmpty().not())
        .toString()
    override fun setAiProvider(provider: String): Boolean { aiProvider = provider; return true }
    override fun setAiBaseUrl(url: String): Boolean { aiBaseUrl = url; return true }
    override fun setAiApiKey(key: String): Boolean { aiApiKey = key; return true }
    override fun setAiModel(model: String): Boolean { aiModel = model; return true }
    override fun resolveChatCredential(provider: String): String? {
        resolvedCredentialProviders.add(provider)
        return if (provider == "openai-compatible") aiApiKey.takeIf { it.isNotEmpty() } else null
    }
    override fun resolveManagedChatConfig(): ManagedChatConfig? {
        resolvedCredentialProviders.add("maho-managed")
        return managedChatConfig
    }

    // --- Chat ---
    override fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long {
        chatSessionNewCalls.add(ChatSessionNewCall(apiKey, endpoint, model, systemInstruction))
        chatSessionNewError?.let { throw it }
        return chatSessionNewPtr
    }
    override fun chatSessionFree(sessionPtr: Long) = Unit
    override fun chatSendUserTurn(sessionPtr: Long, message: String): Boolean {
        lastChatSendPtr = sessionPtr
        lastChatSendMessage = message
        chatSendUserTurns.add(message)
        return true
    }
    override fun chatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean = true
    override fun chatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean = true
    override fun chatCancel(sessionPtr: Long): Boolean = true
    override fun chatSessionPollEvent(sessionPtr: Long): String? = null
    override fun chatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean = true
    override fun chatSendToolResult(sessionPtr: Long, toolCallId: String, name: String, output: String, trigger: Boolean): Boolean {
        chatToolResults.add(ChatToolResultCall(sessionPtr, toolCallId, name, output, trigger))
        return true
    }
    override fun chatAppendUserMessage(sessionPtr: Long, content: String): Boolean {
        chatAppendedUserMessages.add(content)
        return true
    }
    override fun chatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean {
        chatAppendedAssistantMessages.add(content)
        chatAppendedAssistantToolCalls.add(toolCallsJson)
        return true
    }

    // --- Conversations ---
    override fun createConversation(id: String, title: String?, spaceId: String?, model: String?): Boolean = true
    override fun listConversationsJson(limit: Long): String? = conversationListJson
    override fun listConversationsJson(state: String, limit: Long): String? {
        conversationListRequests.add(state to limit)
        return conversationListJson
    }
    override fun archiveConversation(id: String): Boolean { archivedConversationIds.add(id); return true }
    override fun unarchiveConversation(id: String): Boolean { unarchivedConversationIds.add(id); return true }
    override fun applyConversationBulkOperationJson(operation: String, ids: List<String>): String? {
        conversationBulkRequests.add(operation to ids)
        return conversationBulkJson
    }
    override fun getConversationAutoArchivePolicy(): Int = autoArchivePolicy
    override fun setConversationAutoArchivePolicy(days: Int): Boolean { autoArchivePolicy = days; return true }
    override fun getConversationMessagesJson(sessionId: String): String {
        conversationHistoryRequests.add(sessionId)
        return conversationHistoryJson
    }
    override fun saveConversationMessage(sessionId: String, role: String, content: String, urlContext: String?): Boolean = true
    override fun deleteConversation(id: String): Boolean = true
    override fun renameConversation(id: String, title: String): Boolean = true
    override fun listConversationProjectsJson(): String = """[{"id":"project-1","name":"Project","createdAt":"now","updatedAt":"now"}]"""
    override fun createConversationProjectJson(name: String): String = """{"id":"project-new","name":"$name","createdAt":"now","updatedAt":"now"}"""
    override fun renameConversationProject(id: String, name: String): Boolean = true
    override fun deleteConversationProject(id: String): Boolean = true
    override fun moveConversationsToProjectJson(ids: List<String>, projectId: String?): String {
        conversationProjectMoveRequests.add(ids to projectId)
        return org.json.JSONObject()
            .put("requestedCount", ids.size)
            .put("affectedIds", org.json.JSONArray(ids))
            .put("missingIds", org.json.JSONArray())
            .toString()
    }

    // --- Composer drafts ---
    override fun getComposerDraftJson(scopeJson: String): String? {
        requireComposerDraftSupport()
        composerDraftGets.add(scopeJson)
        return composerDraftJson
    }
    override fun setComposerDraft(scopeJson: String, text: String): Boolean {
        requireComposerDraftSupport()
        composerDraftSets.add(scopeJson to text)
        return true
    }
    override fun deleteComposerDraft(scopeJson: String): Boolean {
        requireComposerDraftSupport()
        composerDraftDeletes.add(scopeJson)
        return true
    }

    private fun requireComposerDraftSupport() {
        if (!composerDraftSupported) {
            throw UnsupportedOperationException("composer drafts unsupported in this fake")
        }
    }

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
    override fun agentListArtifacts(sessionPtr: Long): String? = agentArtifactsJson
    override fun artifactShare(sessionPtr: Long, artifactId: String): Boolean {
        lastArtifactSharePtr = sessionPtr
        lastArtifactShareId = artifactId
        return artifactShareResult
    }
    override fun agentPollEvent(sessionPtr: Long): String? = agentPollResult
}
