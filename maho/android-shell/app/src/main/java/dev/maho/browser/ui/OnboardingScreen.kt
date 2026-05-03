package dev.maho.browser.ui

import android.content.Context
import android.content.Intent
import android.provider.Settings
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.theme.BrowserShellTheme

@Composable
fun OnboardingScreen(
    onComplete: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val context = LocalContext.current
    val prefs = context.getSharedPreferences("maho_browser_ui_prefs", Context.MODE_PRIVATE)

    Scaffold(containerColor = shellColors.windowBackground) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = "Maho",
                style = MaterialTheme.typography.displayLarge,
                color = shellColors.accent,
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "Welcome to the future of browsing",
                style = MaterialTheme.typography.titleMedium,
                color = shellColors.textPrimary,
                textAlign = TextAlign.Center,
            )
            Spacer(modifier = Modifier.height(48.dp))
            Button(
                onClick = {
                    val intent = Intent(Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS)
                    context.startActivity(intent)
                    prefs.edit().putBoolean("onboardingCompleted", true).apply()
                    onComplete()
                },
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.buttonColors(
                    containerColor = shellColors.accent,
                    contentColor = shellColors.windowBackground,
                ),
            ) {
                Text("Set as Default Browser")
            }
            Spacer(modifier = Modifier.height(12.dp))
            OutlinedButton(
                onClick = {
                    prefs.edit().putBoolean("onboardingCompleted", true).apply()
                    onComplete()
                },
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = shellColors.textPrimary,
                ),
            ) {
                Text("Skip")
            }
        }
    }
}
