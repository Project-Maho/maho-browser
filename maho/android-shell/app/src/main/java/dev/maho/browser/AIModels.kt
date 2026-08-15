package dev.maho.browser

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class ConversationSession(
    val id: String,
    val title: String? = null,
    @SerialName("spaceId") val spaceId: String? = null,
    val model: String? = null,
    @SerialName("createdAt") val createdAt: String = "",
    @SerialName("updatedAt") val updatedAt: String = "",
    @SerialName("archivedAt") val archivedAt: String? = null,
    @SerialName("projectId") val projectId: String? = null,
) {
    val space_id: String? get() = spaceId
    val created_at: String get() = createdAt
    val last_message_at: String get() = updatedAt
    val message_count: Int get() = 0
}

@Serializable
data class ConversationTurn(
    val id: String = "",
    val role: String,
    val content: String,
    @SerialName("urlContext") val urlContext: String? = null,
    @SerialName("createdAt") val createdAt: String = "",
) {
    val url_context: String? get() = urlContext
    val created_at: String get() = createdAt
}

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
