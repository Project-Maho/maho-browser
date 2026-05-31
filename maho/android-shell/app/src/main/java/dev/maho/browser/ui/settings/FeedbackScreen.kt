@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.settings

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun FeedbackScreen(
    onBack: () -> Unit = {},
) {
    val context = LocalContext.current
    var email by remember { mutableStateOf("") }
    var message by remember { mutableStateOf("") }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Feedback") },
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
                .padding(16.dp),
        ) {
            OutlinedTextField(
                value = email,
                onValueChange = { email = it },
                label = { Text("Email (optional)") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            Spacer(modifier = Modifier.height(16.dp))
            OutlinedTextField(
                value = message,
                onValueChange = { message = it },
                label = { Text("Message") },
                modifier = Modifier.fillMaxWidth().height(160.dp),
                minLines = 4,
            )
            Spacer(modifier = Modifier.height(24.dp))
            Button(
                onClick = {
                    val subject = "Maho Feedback"
                    val body = if (email.isBlank()) {
                        message
                    } else {
                        "From: \$email\n\n\$message"
                    }
                    val uri = Uri.parse(
                        "mailto:feedback@maho.dev?subject=" +
                            Uri.encode(subject) +
                            "\u0026body=" +
                            Uri.encode(body)
                    )
                    val intent = Intent(Intent.ACTION_SENDTO, uri)
                    context.startActivity(intent)
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = message.isNotBlank(),
            ) {
                Text("Send")
            }
        }
    }
}
