package dev.maho.browser.ui.find

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeFind
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.TabId
import dev.maho.browser.ui.theme.BrowserShellTheme
import kotlinx.coroutines.delay
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun FindInPageSheet(
    visibleTabId: TabId?,
    onDismiss: () -> Unit = {},
    isIncognito: Boolean = false,
) {
    val shellColors = BrowserShellTheme.colors
    var query by remember { mutableStateOf("") }
    var matchCount by remember { mutableIntStateOf(0) }
    var activeIndex by remember { mutableIntStateOf(0) }

    val isVisible = visibleTabId != null

    LaunchedEffect(visibleTabId) {
        if (visibleTabId == null) {
            query = ""
            matchCount = 0
            activeIndex = 0
        }
    }

    LaunchedEffect(query, visibleTabId) {
        if (visibleTabId == null) return@LaunchedEffect
        if (query.isBlank()) {
            matchCount = 0
            activeIndex = 0
            BridgeFind.dismissFind()
            return@LaunchedEffect
        }
        delay(300L)
        val result = BridgeFind.startFind(visibleTabId, query)
        matchCount = result?.matchCount ?: 0
        activeIndex = result?.activeMatchIndex ?: 0
    }

    if (!isVisible) return

    Surface(
        tonalElevation = 3.dp,
        shadowElevation = 3.dp,
        color = if (isIncognito) shellColors.incognitoSurface else MaterialTheme.colorScheme.surface,
        contentColor = if (isIncognito) shellColors.textPrimary else MaterialTheme.colorScheme.onSurface,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TextField(
                value = query,
                onValueChange = { query = it },
                singleLine = true,
                placeholder = { Text("Find in page") },
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                keyboardActions = KeyboardActions(
                    onSearch = {
                        visibleTabId?.let {
                            val result = BridgeFind.startFind(it, query)
                            matchCount = result?.matchCount ?: 0
                            activeIndex = result?.activeMatchIndex ?: 0
                        }
                    },
                ),
                modifier = Modifier.weight(1f),
            )

            Text(
                text = if (matchCount > 0) "${activeIndex + 1} of $matchCount" else "0 of 0",
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(horizontal = 8.dp),
            )

            IconButton(
                onClick = {
                    val result = BridgeFind.findPrevious()
                    if (result != null) {
                        activeIndex = result.activeMatchIndex
                        matchCount = result.matchCount
                    }
                },
                enabled = matchCount > 0,
            ) {
                Icon(painter = painterResource(id = MahoIcon.NavBack.drawableRes), contentDescription = "Previous")
            }

            IconButton(
                onClick = {
                    val result = BridgeFind.findNext()
                    if (result != null) {
                        activeIndex = result.activeMatchIndex
                        matchCount = result.matchCount
                    }
                },
                enabled = matchCount > 0,
            ) {
                Icon(painter = painterResource(id = MahoIcon.NavForward.drawableRes), contentDescription = "Next")
            }

            IconButton(
                onClick = {
                    BridgeFind.dismissFind()
                    onDismiss()
                },
            ) {
                Icon(painter = painterResource(id = MahoIcon.Close.drawableRes), contentDescription = "Close")
            }
        }
    }
}

fun handleShowFindBar(update: CoreUpdate.ShowFindBar): TabId = update.tabId
