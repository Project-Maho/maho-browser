package dev.maho.browser.ui.search

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeHistory
import dev.maho.browser.models.HistoryEntry
import dev.maho.browser.support.VoiceSearchButton
import dev.maho.browser.support.VoiceSearchManager
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme
import kotlinx.coroutines.delay

@Composable
fun SearchSheet(
    initialQuery: String,
    isIncognito: Boolean,
    onDismiss: () -> Unit = {},
    onSubmit: (String) -> Unit,
    onToggleIncognito: () -> Unit,
    onAskAI: (String) -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val context = LocalContext.current
    val keyboardController = LocalSoftwareKeyboardController.current
    val voiceSearchManager = remember(context) { VoiceSearchManager(context) }

    var query by remember { mutableStateOf(TextFieldValue(initialQuery)) }
    var historyEntries by remember { mutableStateOf<List<HistoryEntry>>(emptyList()) }
    val focusRequester = remember { FocusRequester() }
    val inputActionTint = if (isIncognito) Color.White.copy(alpha = 0.6f) else shellColors.textPrimary

    DisposableEffect(voiceSearchManager) {
        onDispose {
            voiceSearchManager.reset()
        }
    }

    LaunchedEffect(query.text) {
        delay(150)
        historyEntries = BridgeHistory.searchHistoryEntries(query.text, limit = 20)
    }

    LaunchedEffect(Unit) {
        delay(150)
        focusRequester.requestFocus()
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(if (isIncognito) Color(0xFF1E1E1E) else shellColors.overlayBackground.copy(alpha = 0.98f))
            .clickable { onDismiss() }
    ) {
        // Modal Container
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .clip(RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp))
                .background(if (isIncognito) Color(0xFF282828) else shellColors.tabCard)
                .clickable(enabled = false) {}
                .navigationBarsPadding()
                .imePadding()
                .padding(bottom = 20.dp)
        ) {
            // Search field capsule
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 20.dp)
                    .border(
                        width = 1.dp,
                        color = if (isIncognito) Color.White.copy(alpha = 0.24f) else shellColors.divider,
                        shape = CircleShape
                    )
                    .background(if (isIncognito) Color(0xFF333333) else shellColors.windowBackground, shape = CircleShape)
                    .padding(horizontal = 20.dp, vertical = 14.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                BasicTextField(
                    value = query,
                    onValueChange = { query = it },
                    modifier = Modifier
                        .weight(1f)
                        .focusRequester(focusRequester)
                        .testTag("homeSearchField"),
                    singleLine = true,
                    textStyle = MaterialTheme.typography.bodyLarge.copy(color = if (isIncognito) Color.White else shellColors.textPrimary),
                    keyboardOptions = KeyboardOptions(
                        keyboardType = KeyboardType.Uri,
                        imeAction = ImeAction.Search
                    ),
                    keyboardActions = KeyboardActions(
                        onSearch = {
                            keyboardController?.hide()
                            onSubmit(query.text)
                        }
                    ),
                    cursorBrush = androidx.compose.ui.graphics.SolidColor(if (isIncognito) Color.White else shellColors.textPrimary)
                )

                if (query.text.isNotEmpty()) {
                    Icon(
                        painter = painterResource(id = MahoIcon.Close.drawableRes),
                        contentDescription = "Clear",
                        tint = if (isIncognito) Color.White.copy(alpha = 0.85f) else shellColors.textSecondary,
                        modifier = Modifier
                            .size(17.dp)
                            .clickable { query = TextFieldValue("") }
                    )
                }

                VoiceSearchButton(
                    manager = voiceSearchManager,
                    onResult = { transcript ->
                        val trimmedTranscript = transcript.trim()
                        if (trimmedTranscript.isNotEmpty()) {
                            query = TextFieldValue(trimmedTranscript)
                            keyboardController?.hide()
                            onSubmit(trimmedTranscript)
                        }
                    },
                    modifier = Modifier
                        .size(24.dp)
                        .testTag("searchSheetVoiceButton"),
                    compact = true,
                    buttonModifier = Modifier.size(24.dp),
                    iconModifier = Modifier.size(17.dp),
                    idleTint = inputActionTint,
                    listeningTint = if (isIncognito) shellColors.incognitoAccent else shellColors.accent,
                    errorTint = MaterialTheme.colorScheme.error,
                )

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    modifier = Modifier.clickable { onToggleIncognito() }
                ) {
                    if (isIncognito) {
                        Text(
                            text = "Incognito",
                            style = MaterialTheme.typography.bodyMedium,
                            color = Color.White.copy(alpha = 0.6f)
                        )
                        Icon(
                            painter = painterResource(id = MahoIcon.VisibilityOff.drawableRes),
                            contentDescription = "Exit Private Mode",
                            tint = Color.White.copy(alpha = 0.6f),
                            modifier = Modifier.size(17.dp)
                        )
                    } else {
                        Icon(
                            painter = painterResource(id = MahoIcon.Eye.drawableRes),
                            contentDescription = "Enter Private Mode",
                            tint = shellColors.textPrimary,
                            modifier = Modifier.size(17.dp)
                        )
                    }
                }
            }

            // Suggestions List
            LazyColumn(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp)
            ) {
                if (query.text.trim().isNotEmpty()) {
                    item {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(14.dp),
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    onAskAI(query.text)
                                }
                                .padding(vertical = 18.dp)
                        ) {
                            Icon(
                                painter = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                                contentDescription = "Ask Maho AI",
                                tint = Color(0xFFA855F7),
                                modifier = Modifier.size(24.dp)
                            )
                            Text(
                                text = "Ask Maho AI \"${query.text}\"",
                                style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Medium),
                                color = Color(0xFFA855F7)
                            )
                        }
                        HorizontalDivider(color = if (isIncognito) Color.White.copy(alpha = 0.12f) else shellColors.divider)
                    }
                }

                items(historyEntries.take(6)) { entry ->
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(14.dp),
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                onSubmit(entry.url)
                            }
                            .padding(vertical = 18.dp)
                    ) {
                        val isURL = entry.url.startsWith("http://") || entry.url.startsWith("https://")
                        Icon(
                            painter = painterResource(id = if (isURL) MahoIcon.Globe.drawableRes else MahoIcon.Search.drawableRes),
                            contentDescription = null,
                            tint = if (isIncognito) Color.White.copy(alpha = 0.6f) else shellColors.textSecondary,
                            modifier = Modifier.size(24.dp)
                        )
                        Text(
                            text = if (entry.title.isEmpty()) entry.url else entry.title,
                            style = MaterialTheme.typography.bodyLarge,
                            color = if (isIncognito) Color.White else shellColors.textPrimary,
                            maxLines = 1
                        )
                    }
                }
            }
        }
    }
}
