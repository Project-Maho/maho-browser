package dev.maho.browser

import kotlinx.serialization.Serializable

@Serializable
data class ConversationSession(
    val id: String,
    val title: String? = null,
    val space_id: String? = null,
    val model: String? = null,
    val created_at: Long = 0L,
    val last_message_at: String = "",
    val message_count: Int = 0,
)

@Serializable
data class ConversationTurn(
    val id: String = "",
    val role: String,
    val content: String,
    val created_at: String? = null,
    val timestamp: Long = 0L,
)

@Serializable
data class SpaceAIConfig(
    val spaceId: String = "",
    val model: String? = null,
    val systemInstruction: String? = null,
    val temperature: Double? = null,
    val systemPrompt: String? = null,
    val tone: String? = null,
    val focusAreas: List<String> = emptyList(),
    val preferredModel: String? = null,
    val memoryEnabled: Boolean = true,
)

@Serializable
data class TabCluster(
    val name: String = "",
    val tabIds: List<String> = emptyList(),
    val tab_ids: List<String> = emptyList(),
    val group_name: String = "",
)
