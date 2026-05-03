package dev.maho.browser.sync

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class RelaySyncPushRequest(
    val blobs: List<String>,
)

@Serializable
data class RelaySyncMessage(
    val seq: Long,
    val blob: String,
)

@Serializable
data class RelaySyncPullResponse(
    val messages: List<RelaySyncMessage> = emptyList(),
    @SerialName("has_more") val hasMore: Boolean = false,
)
