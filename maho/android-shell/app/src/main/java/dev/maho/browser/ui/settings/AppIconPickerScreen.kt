@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.settings

import android.content.ComponentName
import android.content.pm.PackageManager
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.selection.selectable
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

data class AppIconOption(
    val name: String,
    val icon: MahoIcon,
    val aliasClassName: String,
)

@Composable
fun AppIconPickerScreen(
    onBack: () -> Unit = {},
) {
    val context = LocalContext.current
    val pm = context.packageManager

    val options = remember {
        val pkg = context.packageName
        listOf(
            AppIconOption("Default", MahoIcon.Globe, "\$pkg.MainActivity"),
            AppIconOption("Dark", MahoIcon.Moon, "\$pkg.MainActivityDark"),
            AppIconOption("Light", MahoIcon.Sun, "\$pkg.MainActivityLight"),
            AppIconOption("Minimal", MahoIcon.Minus, "\$pkg.MainActivityMinimal"),
            AppIconOption("Neon", MahoIcon.SparklesAi, "\$pkg.MainActivityNeon"),
        )
    }

    var selected by remember {
        val current = options.firstOrNull { opt ->
            val component = ComponentName(context, opt.aliasClassName)
            pm.getComponentEnabledSetting(component) == PackageManager.COMPONENT_ENABLED_STATE_ENABLED
        }
        mutableStateOf(current ?: options.first())
    }

    fun selectIcon(option: AppIconOption) {
        selected = option
        options.forEach { opt ->
            val component = ComponentName(context, opt.aliasClassName)
            val state = if (opt == option) {
                PackageManager.COMPONENT_ENABLED_STATE_ENABLED
            } else {
                PackageManager.COMPONENT_ENABLED_STATE_DISABLED
            }
            pm.setComponentEnabledSetting(component, state, PackageManager.DONT_KILL_APP)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("App Icon") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(painter = painterResource(id = MahoIcon.NavBack.drawableRes), contentDescription = "Back")
                    }
                },
            )
        },
    ) { padding ->
        LazyVerticalGrid(
            columns = GridCells.Fixed(2),
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            contentPadding = PaddingValues(16.dp),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            items(options) { option ->
                val isSelected = selected == option
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .selectable(
                            selected = isSelected,
                            role = Role.RadioButton,
                            onClick = { selectIcon(option) },
                        ),
                    border = if (isSelected) {
                        BorderStroke(2.dp, MaterialTheme.colorScheme.primary)
                    } else {
                        null
                    },
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        Icon(
                            painter = painterResource(id = option.icon.drawableRes),
                            contentDescription = option.name,
                            modifier = Modifier.size(48.dp),
                            tint = MaterialTheme.colorScheme.primary,
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(text = option.name, style = MaterialTheme.typography.labelLarge)
                        if (isSelected) {
                            Spacer(modifier = Modifier.height(4.dp))
                            Icon(
                                painter = painterResource(id = MahoIcon.Check.drawableRes),
                                contentDescription = "Selected",
                                tint = MaterialTheme.colorScheme.primary,
                            )
                        }
                    }
                }
            }
        }
    }
}
