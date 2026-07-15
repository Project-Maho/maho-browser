package dev.maho.browser.ui.webview

import dev.maho.browser.MahoBridge
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
    override fun byokGetProviders(): String? = MahoBridge.byokGetProviders()
    override fun byokGetKey(provider: String): String? = MahoBridge.byokGetKey(provider)
    override fun byokSetKey(provider: String, key: String): Boolean = MahoBridge.byokSetKey(provider, key)
    override fun byokDeleteKey(provider: String): Boolean = MahoBridge.byokDeleteKey(provider)
    override fun byokValidateKey(provider: String, key: String): Boolean = MahoBridge.byokValidateKey(provider, key)

    // --- Chat ---
    override fun chatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long =
        MahoBridge.chatSessionNew(apiKey, endpoint, model, systemInstruction)
    override fun chatSessionFree(sessionPtr: Long) = MahoBridge.chatSessionFree(sessionPtr)
    override fun chatSendUserTurn(sessionPtr: Long, message: String): Boolean =
        MahoBridge.chatSendUserTurn(sessionPtr, message)
    override fun chatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean =
        MahoBridge.chatSendImage(sessionPtr, mime, data)
    override fun chatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean =
        MahoBridge.chatSendTextWithImage(sessionPtr, text, mime, data)
    override fun chatCancel(sessionPtr: Long): Boolean = MahoBridge.chatCancel(sessionPtr)
    override fun chatSessionPollEvent(sessionPtr: Long): String? = MahoBridge.chatSessionPollEvent(sessionPtr)
    override fun chatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean =
        MahoBridge.chatRegisterTool(sessionPtr, name, description, parametersJson)
    override fun chatSendToolResult(sessionPtr: Long, toolCallId: String, name: String, output: String, isSuccess: Boolean): Boolean =
        MahoBridge.chatSendToolResult(sessionPtr, toolCallId, name, output, isSuccess)
    override fun chatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean =
        MahoBridge.chatAppendAssistantMessage(sessionPtr, content, toolCallsJson)

    // --- Conversations ---
    override fun createConversation(id: String, title: String?, spaceId: String?, model: String?): Boolean =
        MahoBridge.createConversation(id, title, spaceId, model)
    override fun listConversationsJson(limit: Long): String =
        MahoJson.instance.encodeToString(MahoBridge.listConversations(limit))
    override fun getConversationMessagesJson(sessionId: String): String =
        MahoJson.instance.encodeToString(MahoBridge.getConversationMessages(sessionId))
    override fun saveConversationMessage(sessionId: String, role: String, content: String, urlContext: String?): Boolean =
        MahoBridge.saveConversationMessage(sessionId, role, content, urlContext)
    override fun deleteConversation(id: String): Boolean = MahoBridge.deleteConversation(id)
    override fun renameConversation(id: String, title: String): Boolean = MahoBridge.renameConversation(id, title)

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
    override fun agentPollEvent(sessionPtr: Long): String? = MahoBridge.agentPollEvent(sessionPtr)
}
