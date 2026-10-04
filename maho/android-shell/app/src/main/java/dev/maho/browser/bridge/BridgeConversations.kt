package dev.maho.browser.bridge

import dev.maho.browser.ConversationSession
import dev.maho.browser.ConversationTurn
import dev.maho.browser.MahoBridge
import dev.maho.browser.support.MahoJson
import org.json.JSONArray
import org.json.JSONObject

object BridgeConversations {
    fun createConversation(id: String, title: String?, spaceId: String?, model: String?): Boolean =
        MahoBridge.invokeCreateConversation(MahoBridge.corePtr, id, title ?: "", spaceId ?: "", model ?: "")

    fun listConversations(limit: Long = 100L): List<ConversationSession> {
        val json = MahoBridge.invokeListConversations(MahoBridge.corePtr, limit) ?: return emptyList()
        return try {
            MahoJson.instance.decodeFromString<List<ConversationSession>>(json)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun listConversations(state: String, limit: Long = 100L): List<ConversationSession>? {
        val query = JSONObject().put("state", state).put("limit", limit).toString()
        val json = MahoBridge.invokeListConversationsV2(MahoBridge.corePtr, query) ?: return null
        return try {
            MahoJson.instance.decodeFromString<List<ConversationSession>>(json)
        } catch (_: Exception) {
            null
        }
    }

    fun archiveConversation(id: String): Boolean =
        MahoBridge.invokeArchiveConversation(MahoBridge.corePtr, id)

    fun unarchiveConversation(id: String): Boolean =
        MahoBridge.invokeUnarchiveConversation(MahoBridge.corePtr, id)

    fun applyBulkOperation(operation: String, ids: List<String>): String? =
        MahoBridge.invokeApplyConversationBulkOperation(
            MahoBridge.corePtr,
            JSONObject().put("op", operation).put("ids", JSONArray(ids)).toString(),
        )

    fun getAutoArchivePolicy(): Int =
        MahoBridge.invokeGetConversationAutoArchivePolicy(MahoBridge.corePtr)

    fun setAutoArchivePolicy(days: Int): Boolean =
        MahoBridge.invokeSetConversationAutoArchivePolicy(MahoBridge.corePtr, days)

    fun getConversationMessages(sessionId: String): List<ConversationTurn> {
        val json = MahoBridge.invokeGetConversationMessages(MahoBridge.corePtr, sessionId) ?: return emptyList()
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
    ): Boolean = MahoBridge.invokeSaveConversationMessage(MahoBridge.corePtr, sessionId, role, content, urlContext ?: "")

    fun deleteConversation(id: String): Boolean = MahoBridge.invokeDeleteConversation(MahoBridge.corePtr, id)

    fun renameConversation(id: String, title: String): Boolean = MahoBridge.invokeRenameConversation(MahoBridge.corePtr, id, title)

    fun listConversationProjects(): String? = MahoBridge.invokeListConversationProjects(MahoBridge.corePtr)

    fun createConversationProject(name: String): String? =
        MahoBridge.invokeCreateConversationProject(MahoBridge.corePtr, name)

    fun renameConversationProject(id: String, name: String): Boolean =
        MahoBridge.invokeRenameConversationProject(MahoBridge.corePtr, id, name)

    fun deleteConversationProject(id: String): Boolean =
        MahoBridge.invokeDeleteConversationProject(MahoBridge.corePtr, id)

    fun moveConversationsToProject(ids: List<String>, projectId: String?): String? =
        MahoBridge.invokeMoveConversationsToProject(
            MahoBridge.corePtr,
            JSONObject().put("ids", JSONArray(ids)).put("projectId", projectId ?: JSONObject.NULL).toString(),
        )

    // ─── Composer drafts ──────────────────────────────────────────────────────
    //
    // Drafts live in maho-core's settings table (device/profile-local, never
    // synced). `scopeJson` crosses the JNI boundary verbatim — core owns scope
    // parsing, key mapping, empty-string deletes and orphan reaping.

    fun getComposerDraft(scopeJson: String): String? =
        MahoBridge.invokeGetComposerDraft(MahoBridge.corePtr, scopeJson)

    fun setComposerDraft(scopeJson: String, text: String): Boolean =
        MahoBridge.invokeSetComposerDraft(MahoBridge.corePtr, scopeJson, text)

    fun deleteComposerDraft(scopeJson: String): Boolean =
        MahoBridge.invokeDeleteComposerDraft(MahoBridge.corePtr, scopeJson)
}
