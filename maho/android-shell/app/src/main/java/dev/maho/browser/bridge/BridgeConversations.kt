package dev.maho.browser.bridge

import dev.maho.browser.ConversationSession
import dev.maho.browser.ConversationTurn
import dev.maho.browser.MahoBridge
import dev.maho.browser.support.MahoJson

object BridgeConversations {
    fun createConversation(id: String, title: String?, spaceId: String?, model: String?): Boolean =
        MahoBridge.nativeCreateConversation(MahoBridge.corePtr, id, title ?: "", spaceId ?: "", model ?: "")

    fun listConversations(limit: Long = 100L): List<ConversationSession> {
        val json = MahoBridge.nativeListConversations(MahoBridge.corePtr, limit) ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<ConversationSession>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun getConversationMessages(sessionId: String): List<ConversationTurn> {
        val json = MahoBridge.nativeGetConversationMessages(MahoBridge.corePtr, sessionId) ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<ConversationTurn>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun saveConversationMessage(
        sessionId: String,
        role: String,
        content: String,
        urlContext: String? = null,
    ): Boolean = MahoBridge.nativeSaveConversationMessage(MahoBridge.corePtr, sessionId, role, content, urlContext ?: "")

    fun deleteConversation(id: String): Boolean = MahoBridge.nativeDeleteConversation(MahoBridge.corePtr, id)

    fun renameConversation(id: String, title: String): Boolean = MahoBridge.nativeRenameConversation(MahoBridge.corePtr, id, title)
}
