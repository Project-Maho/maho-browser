@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.settings

import android.content.Context
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import java.util.Locale
import dev.maho.browser.support.VoiceRecognitionLanguageSettings
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun LanguageSettingsScreen(
    onBack: () -> Unit = {},
) {
    val context = LocalContext.current
    val prefs = context.getSharedPreferences("maho_browser_ui_prefs", Context.MODE_PRIVATE)

    val locales = remember {
        listOf(
            Locale.ENGLISH,
            Locale.SIMPLIFIED_CHINESE,
            Locale.TRADITIONAL_CHINESE,
            Locale.JAPANESE,
            Locale.KOREAN,
            Locale.FRENCH,
            Locale.GERMAN,
            Locale("es"),
            Locale("pt"),
            Locale("ru"),
            Locale("ar"),
            Locale("hi"),
        )
    }

    var selectedLocale by remember {
        val saved = prefs.getString("settings.preferredLanguage", null)
        mutableStateOf(saved?.let { tag -> locales.firstOrNull { it.toLanguageTag() == tag } } ?: Locale.getDefault())
    }
    var selectedVoiceLanguageTag by remember {
        mutableStateOf(VoiceRecognitionLanguageSettings.load(context))
    }
    val voiceLanguageOptions = remember { VoiceRecognitionLanguageSettings.options }

    fun selectLocale(locale: Locale) {
        selectedLocale = locale
        prefs.edit().putString("settings.preferredLanguage", locale.toLanguageTag()).apply()
    }

    fun selectVoiceLanguage(tag: String) {
        selectedVoiceLanguageTag = tag
        VoiceRecognitionLanguageSettings.save(context, tag)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Language") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(painter = painterResource(id = MahoIcon.NavBack.drawableRes), contentDescription = "Back")
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState()),
        ) {
            SectionHeader("App language")
            locales.forEach { locale ->
                val isSelected = locale == selectedLocale
                ListItem(
                    headlineContent = { Text(locale.getDisplayName(locale).replaceFirstChar { it.uppercase(locale) }) },
                    trailingContent = {
                        if (isSelected) {
                            Icon(painter = painterResource(id = MahoIcon.Check.drawableRes), contentDescription = "Selected")
                        }
                    },
                    modifier = Modifier.selectable(
                        selected = isSelected,
                        role = Role.RadioButton,
                        onClick = { selectLocale(locale) },
                    ),
                )
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
            }

            SectionHeader("Voice")
            DropdownPreference(
                title = "Voice recognition language",
                value = VoiceRecognitionLanguageSettings.labelFor(selectedVoiceLanguageTag),
                options = voiceLanguageOptions.map { it.label },
                onSelect = { index -> selectVoiceLanguage(voiceLanguageOptions[index].tag) },
            )
            HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
        }
    }
}
