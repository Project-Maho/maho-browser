package dev.maho.browser.models

import kotlinx.serialization.Serializable

@Serializable
enum class TextDirection {
    ltr, rtl, auto
}

@Serializable
data class ExtractedArticle(
    val id: String,
    val title: String,
    val content: String,
    val textContent: String,
    val byline: String,
    val textDirection: TextDirection,
    val excerpt: String? = null,
    val siteName: String? = null,
    val length: Int? = null,
    val publishedTime: String? = null
)
