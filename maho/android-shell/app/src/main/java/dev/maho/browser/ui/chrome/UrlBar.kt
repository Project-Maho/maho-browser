package dev.maho.browser.ui.chrome

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun UrlBar(
    currentUrl: String,
    currentTitle: String,
    isLoading: Boolean,
    loadProgress: Int,
    isSecure: Boolean,
    onNavigate: (String) -> Unit,
    onShare: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val focusManager = LocalFocusManager.current
    var isEditing by remember { mutableStateOf(false) }
    var editText by remember { mutableStateOf(currentUrl) }

    LaunchedEffect(currentUrl) {
        if (!isEditing) {
            editText = currentUrl
        }
    }

    Column(modifier = modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (isSecure && currentUrl.startsWith("https://")) {
                Icon(
                    painter = painterResource(id = MahoIcon.InfoLock.drawableRes),
                    contentDescription = "Secure",
                    modifier = Modifier.size(16.dp),
                    tint = MaterialTheme.colorScheme.primary,
                )
                Spacer(modifier = Modifier.width(4.dp))
            }

            OutlinedTextField(
                value = if (isEditing) editText else displayText(currentTitle, currentUrl),
                onValueChange = { editText = it },
                modifier = Modifier
                    .weight(1f)
                    .onFocusChanged { focusState ->
                        if (focusState.isFocused) {
                            isEditing = true
                            editText = currentUrl
                        } else {
                            isEditing = false
                        }
                    },
                singleLine = true,
                textStyle = MaterialTheme.typography.bodyMedium,
                keyboardOptions = KeyboardOptions(
                    keyboardType = KeyboardType.Uri,
                    imeAction = ImeAction.Go,
                ),
                keyboardActions = KeyboardActions(
                    onGo = {
                        onNavigate(normalizeInput(editText))
                        isEditing = false
                        focusManager.clearFocus()
                    }
                ),
                colors = OutlinedTextFieldDefaults.colors(
                    unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.5f),
                ),
                placeholder = {
                    Text(
                        "Search or enter URL",
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                },
            )

            Spacer(modifier = Modifier.width(4.dp))

            IconButton(onClick = onShare) {
                Icon(
                    painter = painterResource(id = MahoIcon.Share.drawableRes),
                    contentDescription = "Share",
                )
            }
        }

        if (isLoading && loadProgress in 1..99) {
            LinearProgressIndicator(
                progress = loadProgress / 100f,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(2.dp),
            )
        } else {
            Box(modifier = Modifier.height(2.dp))
        }
    }
}

private fun displayText(title: String, url: String): String =
    if (title.isNotBlank()) title else url

private fun normalizeInput(input: String): String {
    val trimmed = input.trim()
    if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) return trimmed
    if (trimmed.contains(".") && !trimmed.contains(" ")) return "https://$trimmed"
    return "https://www.google.com/search?q=${java.net.URLEncoder.encode(trimmed, "UTF-8")}"
}
