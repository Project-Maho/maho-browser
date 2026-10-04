@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeSettings
import dev.maho.browser.models.AppearanceSettingsUpdate
import dev.maho.browser.models.Density
import dev.maho.browser.models.Theme
import dev.maho.browser.support.Patchable
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserThemeController

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppearanceSettingsScreen(
    onBack: () -> Unit = {},
) {
    var themeIndex by remember { mutableIntStateOf(0) }
    var densityIndex by remember { mutableIntStateOf(1) }
    var showTabBar by remember { mutableStateOf(true) }
    var windowTransparency by remember { mutableStateOf(false) }
    var customCss by remember { mutableStateOf("") }

    val themes = listOf("System", "Dark", "Light")
    val themeValues = listOf(Theme.System, Theme.Dark, Theme.Light)
    val densities = listOf("Compact", "Comfortable")
    val densityValues = listOf(Density.Compact, Density.Comfortable)

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        themeIndex = themeValues.indexOf(settings.appearance.theme).coerceAtLeast(0)
        densityIndex = densityValues.indexOf(settings.appearance.density).coerceAtLeast(0)
        showTabBar = settings.appearance.showTabBar
        windowTransparency = settings.appearance.windowTransparency
        customCss = settings.appearance.customChromeCss ?: ""
        BrowserThemeController.setTheme(settings.appearance.theme)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Appearance") },
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
            SectionHeader("Theme")
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                themes.forEachIndexed { index, label ->
                    SegmentedButton(
                        selected = themeIndex == index,
                        onClick = {
                            val theme = themeValues[index]
                            themeIndex = index
                            BridgeSettings.updateAppearanceSettings(
                                AppearanceSettingsUpdate(theme = theme)
                            )
                            BrowserThemeController.setTheme(theme)
                        },
                        shape = SegmentedButtonDefaults.itemShape(index, themes.size),
                    ) { Text(label) }
                }
            }
            Spacer(modifier = Modifier.height(16.dp))

            SectionHeader("Layout")
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                densities.forEachIndexed { index, label ->
                    SegmentedButton(
                        selected = densityIndex == index,
                        onClick = {
                            densityIndex = index
                            BridgeSettings.updateAppearanceSettings(
                                AppearanceSettingsUpdate(density = densityValues[index])
                            )
                        },
                        shape = SegmentedButtonDefaults.itemShape(index, densities.size),
                    ) { Text(label) }
                }
            }
            Spacer(modifier = Modifier.height(8.dp))

            SwitchPreference(
                title = "Show Tab Bar",
                checked = showTabBar,
                onCheckedChange = {
                    showTabBar = it
                    BridgeSettings.updateAppearanceSettings(
                        AppearanceSettingsUpdate(showTabBar = it)
                    )
                },
            )
            SwitchPreference(
                title = "Window Transparency",
                checked = windowTransparency,
                onCheckedChange = {
                    windowTransparency = it
                    BridgeSettings.updateAppearanceSettings(
                        AppearanceSettingsUpdate(windowTransparency = it)
                    )
                },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

            SectionHeader("Custom CSS")
            OutlinedTextField(
                value = customCss,
                onValueChange = { customCss = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(120.dp),
                label = { Text("Chrome CSS") },
            )
            Spacer(modifier = Modifier.height(8.dp))
            Button(onClick = {
                BridgeSettings.updateAppearanceSettings(
                    AppearanceSettingsUpdate(customChromeCss = Patchable.Set(customCss))
                )
            }) { Text("Apply CSS") }
            TextButton(onClick = {
                customCss = ""
                BridgeSettings.updateAppearanceSettings(
                    AppearanceSettingsUpdate(customChromeCss = Patchable.SetNull)
                )
            }) { Text("Clear CSS") }
        }
    }
}
