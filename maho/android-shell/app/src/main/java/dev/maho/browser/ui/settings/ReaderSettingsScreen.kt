@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.maho.browser.bridge.BridgeSettings
import dev.maho.browser.MahoBridge
import dev.maho.browser.models.ReaderSettingsUpdate
import dev.maho.browser.models.ReaderTheme
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ReaderSettingsScreen(
    onBack: () -> Unit = {},
) {
    var fontFamily by remember { mutableStateOf("System") }
    var fontSize by remember { mutableDoubleStateOf(18.0) }
    var readerThemeIndex by remember { mutableIntStateOf(0) }

    val availableFonts = listOf("System", "Georgia", "Palatino", "Times New Roman", "Helvetica", "Arial", "Verdana")
    val readerThemes = listOf("Light", "Sepia", "Dark")
    val readerThemeValues = listOf(ReaderTheme.Light, ReaderTheme.Sepia, ReaderTheme.Dark)

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        fontFamily = settings.reader.fontFamily
        fontSize = settings.reader.fontSize
        readerThemeIndex = readerThemeValues.indexOf(settings.reader.theme).coerceAtLeast(0)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Reader Mode") },
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
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp),
        ) {
            SectionHeader("Font")
            DropdownPreference(
                title = "Font Family",
                value = fontFamily,
                options = availableFonts,
                onSelect = { idx ->
                    fontFamily = availableFonts[idx]
                    BridgeSettings.updateReaderSettings(ReaderSettingsUpdate(fontFamily = fontFamily))
                },
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text("Font Size: ${fontSize.toInt()}pt", style = MaterialTheme.typography.bodyMedium)
            Slider(
                value = fontSize.toFloat(),
                onValueChange = { fontSize = it.toDouble() },
                onValueChangeFinished = {
                    BridgeSettings.updateReaderSettings(ReaderSettingsUpdate(fontSize = fontSize))
                },
                valueRange = 12f..32f,
                steps = 19,
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

            SectionHeader("Theme")
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                readerThemes.forEachIndexed { index, label ->
                    SegmentedButton(
                        selected = readerThemeIndex == index,
                        onClick = {
                            readerThemeIndex = index
                            BridgeSettings.updateReaderSettings(
                                ReaderSettingsUpdate(theme = readerThemeValues[index])
                            )
                        },
                        shape = SegmentedButtonDefaults.itemShape(index, readerThemes.size),
                    ) { Text(label) }
                }
            }
            Spacer(modifier = Modifier.height(16.dp))

            SectionHeader("Preview")
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = MaterialTheme.shapes.medium,
                tonalElevation = 2.dp,
            ) {
                Text(
                    text = "The quick brown fox jumps over the lazy dog. This is a preview of how reader mode content will appear with the selected font and size settings.",
                    fontSize = fontSize.sp,
                    modifier = Modifier.padding(16.dp),
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ReaderSettingsSheet(
    onDismiss: () -> Unit,
) {
    var fontFamily by remember { mutableStateOf("System") }
    var fontSize by remember { mutableDoubleStateOf(18.0) }
    var readerThemeIndex by remember { mutableIntStateOf(0) }

    val availableFonts = listOf("System", "Georgia", "Palatino", "Times New Roman", "Helvetica", "Arial", "Verdana")
    val readerThemes = listOf("Light", "Sepia", "Dark")
    val readerThemeValues = listOf(ReaderTheme.Light, ReaderTheme.Sepia, ReaderTheme.Dark)

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        fontFamily = settings.reader.fontFamily
        fontSize = settings.reader.fontSize
        readerThemeIndex = readerThemeValues.indexOf(settings.reader.theme).coerceAtLeast(0)
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 8.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text(
                text = "Reader Settings",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(vertical = 8.dp)
            )

            SectionHeader("Font")
            DropdownPreference(
                title = "Font Family",
                value = fontFamily,
                options = availableFonts,
                onSelect = { idx ->
                    fontFamily = availableFonts[idx]
                    BridgeSettings.updateReaderSettings(ReaderSettingsUpdate(fontFamily = fontFamily))
                },
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text("Font Size: ${fontSize.toInt()}pt", style = MaterialTheme.typography.bodyMedium)
            Slider(
                value = fontSize.toFloat(),
                onValueChange = { fontSize = it.toDouble() },
                onValueChangeFinished = {
                    BridgeSettings.updateReaderSettings(ReaderSettingsUpdate(fontSize = fontSize))
                },
                valueRange = 12f..32f,
                steps = 19,
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

            SectionHeader("Theme")
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                readerThemes.forEachIndexed { index, label ->
                    SegmentedButton(
                        selected = readerThemeIndex == index,
                        onClick = {
                            readerThemeIndex = index
                            BridgeSettings.updateReaderSettings(
                                ReaderSettingsUpdate(theme = readerThemeValues[index])
                            )
                        },
                        shape = SegmentedButtonDefaults.itemShape(index, readerThemes.size),
                    ) { Text(label) }
                }
            }
            Spacer(modifier = Modifier.height(24.dp))
        }
    }
}

