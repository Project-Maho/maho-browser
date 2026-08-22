package dev.maho.browser.ui.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import dev.maho.browser.MahoBridge
import dev.maho.browser.bridge.BridgeSettings
import dev.maho.browser.bridge.sendEvent
import dev.maho.browser.models.AutofillSettingsUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.ui.icons.MahoIcon

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AutofillSettingsScreen(
    onBack: () -> Unit = {},
) {
    var addressesEnabled by remember { mutableStateOf(true) }
    var paymentsEnabled by remember { mutableStateOf(true) }
    var passwordSearchQuery by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        val settings = BridgeSettings.getSettingsTyped() ?: return@LaunchedEffect
        addressesEnabled = settings.autofill.addressesEnabled
        paymentsEnabled = settings.autofill.paymentsEnabled
    }

    fun searchPasswords() {
        MahoBridge.sendEvent(ShellEvent.SearchPasswords(passwordSearchQuery.trim()))
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Autofill & Passwords") },
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
                .verticalScroll(rememberScrollState()),
        ) {
            SectionHeader("Autofill")
            SwitchPreference("Addresses", addressesEnabled) {
                addressesEnabled = it
                BridgeSettings.updateAutofillSettings(AutofillSettingsUpdate(addressesEnabled = it))
            }
            SwitchPreference("Payment Methods", paymentsEnabled) {
                paymentsEnabled = it
                BridgeSettings.updateAutofillSettings(AutofillSettingsUpdate(paymentsEnabled = it))
            }
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Saved Passwords")
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
            ) {
                OutlinedTextField(
                    value = passwordSearchQuery,
                    onValueChange = { passwordSearchQuery = it },
                    label = { Text("Search passwords") },
                    leadingIcon = { Icon(painter = painterResource(id = MahoIcon.Search.drawableRes), contentDescription = null) },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
                TextButton(
                    onClick = ::searchPasswords,
                    enabled = passwordSearchQuery.isNotBlank(),
                ) { Text("Search") }
            }
            Text(
                "Passwords are securely stored and encrypted.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Saved Addresses")
            ListItem(
                headlineContent = { Text("Manage Addresses") },
                supportingContent = { Text("Address management is available in the browser.") },
            )
            HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

            SectionHeader("Payment Methods")
            ListItem(
                headlineContent = { Text("Manage Payment Methods") },
                supportingContent = { Text("Payment method management is available in the browser.") },
            )
        }
    }
}
