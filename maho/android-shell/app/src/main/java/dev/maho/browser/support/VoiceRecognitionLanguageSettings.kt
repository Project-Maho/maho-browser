package dev.maho.browser.support

import android.content.Context
import java.util.Locale

internal data class VoiceRecognitionLanguageOption(
    val tag: String,
    val label: String,
)

internal object VoiceRecognitionLanguageSettings {
    const val SystemDefaultTag = ""

    val options = listOf(
        VoiceRecognitionLanguageOption(SystemDefaultTag, "System default"),
        VoiceRecognitionLanguageOption("en-US", "English (United States)"),
        VoiceRecognitionLanguageOption("ko-KR", "Korean (South Korea)"),
        VoiceRecognitionLanguageOption("ja-JP", "Japanese (Japan)"),
        VoiceRecognitionLanguageOption("zh-CN", "Chinese (Simplified, China)"),
        VoiceRecognitionLanguageOption("es-ES", "Spanish (Spain)"),
        VoiceRecognitionLanguageOption("fr-FR", "French (France)"),
        VoiceRecognitionLanguageOption("de-DE", "German (Germany)"),
    )

    fun load(context: Context): String {
        val saved = prefs(context).getString(VoiceRecognitionLanguageKey, SystemDefaultTag).orEmpty()
        return saved.takeIf { tag -> options.any { option -> option.tag == tag } } ?: SystemDefaultTag
    }

    fun save(context: Context, tag: String) {
        val normalizedTag = tag.takeIf { candidate -> options.any { option -> option.tag == candidate } } ?: SystemDefaultTag
        prefs(context).edit().putString(VoiceRecognitionLanguageKey, normalizedTag).apply()
    }

    fun labelFor(tag: String): String {
        return options.firstOrNull { option -> option.tag == tag }?.label ?: options.first().label
    }

    fun resolveLanguageTag(context: Context): String {
        return load(context).ifBlank { Locale.getDefault().toLanguageTag() }
    }

    private fun prefs(context: Context) = context.getSharedPreferences(
        BrowserUiPrefsName,
        Context.MODE_PRIVATE,
    )

    private const val BrowserUiPrefsName = "maho_browser_ui_prefs"
    private const val VoiceRecognitionLanguageKey = "settings.voiceRecognitionLanguage"
}
