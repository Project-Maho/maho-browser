package dev.maho.browser.ui.webview

import dev.maho.browser.MahoBridge
import dev.maho.browser.bridge.BridgeAi
import dev.maho.browser.bridge.BridgeChat
import dev.maho.browser.bridge.BridgeConversations
import dev.maho.browser.support.MahoJson
import kotlinx.serialization.encodeToString

/**
 * MahoNativeBridge — production [WebViewNativeBridge] that forwards each call to
 * the [MahoBridge] JNI singleton. List/config/offline results are serialized to
 * JSON here so the interface (and the dispatcher) stays free of native-coupled
 * domain types and can be unit-tested with a fake.
 *
 * Referencing [MahoBridge] triggers `System.loadLibrary("maho_jni")`, so this
 * object must only be constructed on-device / in instrumentation — never in JVM
 * unit tests.
 */
object MahoNativeBridge : WebViewNativeBridge {

    // --- BYOK ---
    override fun byokGetProviders(): String? = BridgeAi.byokGetProviders()
    override fun byokGetKey(provider: String): String? = BridgeAi.byokGetKey(provider)
    override fun byokSetKey(provider: String, key: String): Boolean = BridgeAi.byokSetKey(provider, key)
    override fun byokDeleteKey(provider: String): Boolean = BridgeAi.byokDeleteKey(provider)
    override fun byokValidateKey(provider: String, key: String): Boolean = BridgeAi.byokValidateKey(provider, key)
    override fun getAiSettingsJson(): String = BridgeAi.getAiSettings()
    override fun setAiProvider(provider: String): Boolean = BridgeAi.setAiProvider(provider)
    override fun setAiBaseUrl(url: String): Boolean = BridgeAi.setAiBaseUrl(url)
    override fun setAiApiKey(key: String): Boolean = BridgeAi.setAiApiKey(key)
    override fun setAiModel(model: String): Boolean = BridgeAi.setAiModel(model)
    override fun resolveChatCredential(provider: String): String? = BridgeAi.resolveChatCredential(provider)
    override fun resolveManagedChatConfig(): ManagedChatConfig? = BridgeAi.resolveManagedChatConfig()

    // --- Chat ---
    override fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long =
        BridgeChat.chatSessionNew(apiKey, endpoint, model, systemInstruction)
    override fun chatSessionFree(sessionPtr: Long) = BridgeChat.chatSessionFree(sessionPtr)
    override fun chatSendUserTurn(sessionPtr: Long, message: String): Boolean =
        BridgeChat.chatSendUserTurn(sessionPtr, message)
    override fun chatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean =
        BridgeChat.chatSendImage(sessionPtr, mime, data)
    override fun chatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean =
        BridgeChat.chatSendTextWithImage(sessionPtr, text, mime, data)
    override fun chatCancel(sessionPtr: Long): Boolean = BridgeChat.chatCancel(sessionPtr)
    override fun chatSessionPollEvent(sessionPtr: Long): String? = BridgeChat.chatSessionPollEvent(sessionPtr)
    override fun chatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean =
        BridgeChat.chatRegisterTool(sessionPtr, name, description, parametersJson)
    override fun chatSendToolResult(sessionPtr: Long, toolCallId: String, name: String, output: String, trigger: Boolean): Boolean =
        BridgeChat.chatSendToolResult(sessionPtr, toolCallId, name, output, trigger)
    override fun chatAppendUserMessage(sessionPtr: Long, content: String): Boolean =
        BridgeChat.chatAppendUserMessage(sessionPtr, content)
    override fun chatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean =
        BridgeChat.chatAppendAssistantMessage(sessionPtr, content, toolCallsJson)

    // --- Conversations ---
    override fun createConversation(id: String, title: String?, spaceId: String?, model: String?): Boolean =
        BridgeConversations.createConversation(id, title, spaceId, model)
    override fun listConversationsJson(limit: Long): String =
        MahoJson.instance.encodeToString(BridgeConversations.listConversations(limit))
    override fun listConversationsJson(state: String, limit: Long): String? =
        BridgeConversations.listConversations(state, limit)?.let { MahoJson.instance.encodeToString(it) }
    override fun archiveConversation(id: String): Boolean = BridgeConversations.archiveConversation(id)
    override fun unarchiveConversation(id: String): Boolean = BridgeConversations.unarchiveConversation(id)
    override fun applyConversationBulkOperationJson(operation: String, ids: List<String>): String? =
        BridgeConversations.applyBulkOperation(operation, ids)
    override fun getConversationAutoArchivePolicy(): Int = BridgeConversations.getAutoArchivePolicy()
    override fun setConversationAutoArchivePolicy(days: Int): Boolean = BridgeConversations.setAutoArchivePolicy(days)
    override fun getConversationMessagesJson(sessionId: String): String =
        MahoJson.instance.encodeToString(BridgeConversations.getConversationMessages(sessionId))
    override fun saveConversationMessage(sessionId: String, role: String, content: String, urlContext: String?): Boolean =
        BridgeConversations.saveConversationMessage(sessionId, role, content, urlContext)
    override fun deleteConversation(id: String): Boolean = BridgeConversations.deleteConversation(id)
    override fun renameConversation(id: String, title: String): Boolean = BridgeConversations.renameConversation(id, title)
    override fun listConversationProjectsJson(): String? = BridgeConversations.listConversationProjects()
    override fun createConversationProjectJson(name: String): String? = BridgeConversations.createConversationProject(name)
    override fun renameConversationProject(id: String, name: String): Boolean = BridgeConversations.renameConversationProject(id, name)
    override fun deleteConversationProject(id: String): Boolean = BridgeConversations.deleteConversationProject(id)
    override fun moveConversationsToProjectJson(ids: List<String>, projectId: String?): String? =
        BridgeConversations.moveConversationsToProject(ids, projectId)

    // --- Composer drafts ---
    override fun getComposerDraftJson(scopeJson: String): String? =
        BridgeConversations.getComposerDraft(scopeJson)
    override fun setComposerDraft(scopeJson: String, text: String): Boolean =
        BridgeConversations.setComposerDraft(scopeJson, text)
    override fun deleteComposerDraft(scopeJson: String): Boolean =
        BridgeConversations.deleteComposerDraft(scopeJson)

    // --- Space AI Config ---
    override fun getSpaceAIConfigJson(spaceId: String): String? =
        MahoBridge.getSpaceAIConfig(spaceId)?.let { MahoJson.instance.encodeToString(it) }
    override fun setSpaceAIConfigJson(spaceId: String, configJson: String): Boolean =
        MahoBridge.setSpaceAIConfig(spaceId, configJson)

    // --- Offline Model ---
    override fun offlineModelInfoJson(): String? =
        MahoBridge.offlineModelInfo()?.let { MahoJson.instance.encodeToString(it) }
    override fun offlineModelIsDownloaded(): Boolean = MahoBridge.offlineModelIsDownloaded()
    override fun offlineModelDelete(): Boolean = MahoBridge.offlineModelDelete()

    // --- Agent Session ---
    override fun agentCreateSession(sessionId: String): Long = MahoBridge.agentCreateSession(sessionId)
    override fun agentFreeSession(sessionPtr: Long) = MahoBridge.agentFreeSession(sessionPtr)
    override fun agentSendMessage(sessionPtr: Long, message: String): Boolean =
        MahoBridge.agentSendMessage(sessionPtr, message)
    override fun agentCancel(sessionPtr: Long): Boolean = MahoBridge.agentCancel(sessionPtr)
    override fun agentListTools(sessionPtr: Long): String? = MahoBridge.agentListTools(sessionPtr)
    override fun agentListArtifacts(sessionPtr: Long): String? = MahoBridge.agentListArtifacts(sessionPtr)
    override fun artifactShare(sessionPtr: Long, artifactId: String): Boolean =
        MahoBridge.artifactShare(sessionPtr, artifactId)
    override fun agentPollEvent(sessionPtr: Long): String? = MahoBridge.agentPollEvent(sessionPtr)
}
