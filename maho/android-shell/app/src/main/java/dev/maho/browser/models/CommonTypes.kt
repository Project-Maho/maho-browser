package dev.maho.browser.models

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

typealias Url = String

@Serializable
enum class ImageFormat {
    @SerialName("png") Png,
    @SerialName("jpeg") Jpeg,
    @SerialName("webp") Webp,
}

@Serializable
data class ImageData(
    val data: List<Int>,
    val width: Int,
    val height: Int,
    val format: ImageFormat,
)

@Serializable
enum class MemoryPressureLevel {
    @SerialName("normal") Normal,
    @SerialName("warning") Warning,
    @SerialName("critical") Critical,
    @SerialName("extreme") Extreme,
}

@Serializable
data class Size(
    val width: Double,
    val height: Double,
)

@Serializable
data class Point(
    val x: Double,
    val y: Double,
)

@Serializable
data class ScrollPosition(
    val x: Double = 0.0,
    val y: Double = 0.0,
)

@Serializable
data class Color(
    val r: Int,
    val g: Int,
    val b: Int,
    val a: Double,
)

@Serializable
enum class Orientation {
    @SerialName("horizontal") Horizontal,
    @SerialName("vertical") Vertical,
}

@Serializable
data class UrlPattern(
    val pattern: String,
    val type: UrlPatternType,
)

@Serializable
enum class UrlPatternType {
    @SerialName("glob") Glob,
    @SerialName("regex") Regex,
}

@Serializable
data class TabSnapshot(
    val url: String,
    val title: String,
    @SerialName("scrollPosition") val scrollPosition: ScrollPosition,
    @SerialName("interactionState") val interactionState: List<Int>,
    @SerialName("capturedAt") val capturedAt: String,
)

@Serializable
data class FindResult(
    @SerialName("matchCount") val matchCount: Int,
    @SerialName("activeMatchIndex") val activeMatchIndex: Int,
)
