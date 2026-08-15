@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.library

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeSpaces
import dev.maho.browser.bridge.BridgeTabs
import dev.maho.browser.models.ArchivedTabViewModel
import dev.maho.browser.models.SpaceId
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ArchiveScreen(
    onOpenTab: (String) -> Unit = {},
    onBack: () -> Unit = {},
) {
    var archivedTabs by remember { mutableStateOf<List<ArchivedTabViewModel>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }
    var activeSpaceId by remember { mutableStateOf<SpaceId?>(null) }

    LaunchedEffect(Unit) {
        isLoading = true
        activeSpaceId = BridgeSpaces.getActiveSpaceId()
        archivedTabs = activeSpaceId?.let { BridgeTabs.getArchivedTabs(it) }.orEmpty()
        isLoading = false
    }

    Scaffold(
        modifier = Modifier.testTag("archiveScreen"),
        topBar = {
            TopAppBar(
                title = { Text("Archive") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(painter = painterResource(id = MahoIcon.NavBack.drawableRes), contentDescription = "Back")
                    }
                },
            )
        },
    ) { padding ->
        when {
            isLoading -> {
                Box(
                    modifier = Modifier
                        .padding(padding)
                        .fillMaxSize(),
                    contentAlignment = Alignment.Center,
                ) {
                    CircularProgressIndicator(modifier = Modifier.testTag("archiveLoading"))
                }
            }

            archivedTabs.isEmpty() -> {
                Box(
                    modifier = Modifier
                        .padding(padding)
                        .fillMaxSize(),
                    contentAlignment = Alignment.Center,
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.testTag("archiveEmptyState")) {
                        Icon(painter = painterResource(id = MahoIcon.Archive.drawableRes),
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Text(
                            text = "No Archived Tabs",
                            style = MaterialTheme.typography.titleMedium,
                            modifier = Modifier.padding(top = 8.dp),
                        )
                        Text(
                            text = "Tabs archived in this space will appear here.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(top = 4.dp),
                        )
                    }
                }
            }

            else -> {
                LazyColumn(
                    modifier = Modifier
                        .padding(padding)
                        .fillMaxSize()
                        .testTag("archiveList"),
                ) {
                    items(
                        items = archivedTabs,
                        key = { it.id },
                    ) { tab ->
                        ArchiveItem(
                            tab = tab,
                            onTap = {
                                BridgeTabs.restoreArchivedTab(tab.id)
                                onOpenTab(tab.id)
                            },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ArchiveItem(
    tab: ArchivedTabViewModel,
    onTap: () -> Unit,
) {
    ListItem(
        headlineContent = {
            Text(
                text = tab.title.ifBlank { tab.url },
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        },
        supportingContent = {
            Column {
                Text(
                    text = tab.url,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
                Text(
                    text = "Archived ${tab.archivedAt}",
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 2.dp),
                )
            }
        },
        modifier = Modifier
            .fillMaxWidth()
            .testTag("archiveItem_${tab.id}")
            .clickable(onClick = onTap),
    )
}
