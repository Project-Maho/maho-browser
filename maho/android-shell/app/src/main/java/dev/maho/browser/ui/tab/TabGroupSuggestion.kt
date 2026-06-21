@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui.tab

import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.MahoBridge
import dev.maho.browser.TabCluster
import dev.maho.browser.bridge.BridgeFolders
import dev.maho.browser.bridge.BridgeTabPreviews
import dev.maho.browser.bridge.BridgeTabs
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.support.MahoJson
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme

private data class SuggestedClusterCard(
    val id: String,
    val name: String,
    val tabs: List<TabViewModel>,
    val previews: List<SuggestedClusterPreview>,
)

private data class SuggestedClusterPreview(
    val id: TabId,
    val title: String,
    val host: String,
    val previewData: String?,
)

@Composable
fun TabGroupSuggestion(
    spaceId: SpaceId,
    activeSpaceName: String,
    tabs: List<TabViewModel>,
    onDismiss: () -> Unit,
    onFoldersCreated: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    var suggestions by remember { mutableStateOf<List<SuggestedClusterCard>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }
    var creatingClusterId by remember { mutableStateOf<String?>(null) }
    var errorMessage by remember { mutableStateOf<String?>(null) }

    fun loadSuggestions() {
        isLoading = true
        errorMessage = null

        val tabLookup = tabs.associateBy { it.id }
        val groupedTabIds = BridgeFolders.getFolderViewModels(spaceId)
            .flatMap { it.tabIds }
            .toSet()

        val clusters = runCatching {
            val json = MahoBridge.clusterTabs(0.75f)
            if (json == null) {
                emptyList()
            } else {
                MahoJson.instance.decodeFromString<List<TabCluster>>(json)
            }
        }.getOrDefault(emptyList())

        suggestions = clusters
            .mapNotNull { cluster ->
                val clusterTabs = cluster.tab_ids
                    .mapNotNull { tabLookup[it] }
                    .filterNot { groupedTabIds.contains(it.id) }

                if (clusterTabs.size < 2) {
                    null
                } else {
                    SuggestedClusterCard(
                        id = (listOf(cluster.group_name) + clusterTabs.map { it.id }).joinToString(separator = ":"),
                        name = suggestedName(cluster.group_name, clusterTabs),
                        tabs = clusterTabs,
                        previews = clusterTabs.take(3).map { tab ->
                            SuggestedClusterPreview(
                                id = tab.id,
                                title = displayTitle(tab),
                                host = displayHost(tab.url),
                                previewData = BridgeTabPreviews.getTabPreview(tab.id),
                            )
                        },
                    )
                }
            }
            .sortedByDescending { it.tabs.size }

        isLoading = false
    }

    fun createFolder(cluster: SuggestedClusterCard) {
        if (creatingClusterId != null) return
        creatingClusterId = cluster.id
        errorMessage = null

        val folder = BridgeFolders.createFolderAndReturnModel(name = cluster.name, spaceId = spaceId)
        if (folder == null) {
            errorMessage = "Maho couldn't create this folder right now. Please try again."
            creatingClusterId = null
            return
        }

        cluster.tabs.forEach { tab ->
            BridgeTabs.moveTabToFolder(spaceId = spaceId, folderId = folder.id, tabId = tab.id)
        }

        suggestions = suggestions.filterNot { it.id == cluster.id }
        creatingClusterId = null
        onFoldersCreated()

        if (suggestions.isEmpty()) {
            onDismiss()
        }
    }

    LaunchedEffect(spaceId, tabs) {
        loadSuggestions()
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        modifier = modifier,
        containerColor = shellColors.overlaySurface,
        contentColor = shellColors.textPrimary,
    ) {
        LazyColumn(
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Box(
                        modifier = Modifier
                            .size(44.dp)
                            .background(shellColors.accentMuted, CircleShape),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            painter = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                            contentDescription = null,
                            tint = shellColors.accent,
                            modifier = Modifier.size(18.dp),
                        )
                    }

                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(
                            text = "AI suggests grouping these tabs",
                            style = MaterialTheme.typography.titleMedium,
                            color = shellColors.textPrimary,
                            fontWeight = FontWeight.SemiBold,
                        )
                        Text(
                            text = "Review clusters in $activeSpaceName and create folders only for the groups you want.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = shellColors.textSecondary,
                        )
                    }
                }
            }

            if (errorMessage != null) {
                item {
                    Surface(
                        shape = RoundedCornerShape(metrics.compactCorner),
                        color = shellColors.overlaySurfaceHigh,
                    ) {
                        Text(
                            text = errorMessage.orEmpty(),
                            style = MaterialTheme.typography.bodySmall,
                            color = shellColors.textPrimary,
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
                        )
                    }
                }
            }

            when {
                isLoading -> item {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 24.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        CircularProgressIndicator(color = shellColors.accent)
                        Text(
                            text = "Finding related tabs…",
                            style = MaterialTheme.typography.titleSmall,
                            color = shellColors.textPrimary,
                        )
                    }
                }

                suggestions.isEmpty() -> item {
                    Card(
                        shape = RoundedCornerShape(metrics.cardCorner),
                        colors = CardDefaults.cardColors(containerColor = shellColors.overlaySurfaceHigh),
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 18.dp, vertical = 20.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(10.dp),
                        ) {
                            Icon(
                                painter = painterResource(id = MahoIcon.Folder.drawableRes),
                                contentDescription = null,
                                tint = shellColors.textSecondary,
                            )
                            Text(
                                text = "No suggested groups yet",
                                style = MaterialTheme.typography.titleSmall,
                                color = shellColors.textPrimary,
                            )
                            Text(
                                text = "Open a few related tabs and try again. Single tabs and already-filed tabs are skipped.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = shellColors.textSecondary,
                            )
                        }
                    }
                }

                else -> items(suggestions, key = { it.id }) { cluster ->
                    ClusterSuggestionCard(
                        cluster = cluster,
                        isCreating = creatingClusterId == cluster.id,
                        onCreateFolder = { createFolder(cluster) },
                    )
                }
            }

            item {
                TextButton(
                    onClick = onDismiss,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Not now")
                }
            }
        }
    }
}

@Composable
private fun ClusterSuggestionCard(
    cluster: SuggestedClusterCard,
    isCreating: Boolean,
    onCreateFolder: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics

    Card(
        shape = RoundedCornerShape(metrics.cardCorner),
        border = BorderStroke(1.dp, shellColors.divider.copy(alpha = 0.42f)),
        colors = CardDefaults.cardColors(containerColor = shellColors.overlaySurfaceHigh),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = cluster.name,
                        style = MaterialTheme.typography.titleSmall,
                        color = shellColors.textPrimary,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        text = "${cluster.tabs.size} tab${if (cluster.tabs.size == 1) "" else "s"}",
                        style = MaterialTheme.typography.labelMedium,
                        color = shellColors.textSecondary,
                    )
                }

                Button(
                    onClick = onCreateFolder,
                    enabled = !isCreating,
                ) {
                    if (isCreating) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(14.dp),
                            strokeWidth = 2.dp,
                            color = Color.White,
                        )
                    } else {
                        Text("Create Folder")
                    }
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                cluster.previews.forEach { preview ->
                    PreviewTile(
                        preview = preview,
                        modifier = Modifier.weight(1f),
                    )
                }
            }

            Text(
                text = cluster.previews.joinToString(separator = " · ") { it.host },
                style = MaterialTheme.typography.labelMedium,
                color = shellColors.textSecondary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun PreviewTile(
    preview: SuggestedClusterPreview,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val imageBitmap = remember(preview.previewData) {
        preview.previewData?.let { encoded ->
            runCatching {
                val bytes = Base64.decode(encoded, Base64.DEFAULT)
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
            }.getOrNull()
        }
    }

    Box(
        modifier = modifier
            .height(metrics.suggestionPreviewHeight)
            .clip(RoundedCornerShape(metrics.compactCorner))
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        shellColors.overlaySurface,
                        shellColors.overlaySurfaceHigh,
                    ),
                ),
            ),
    ) {
        if (imageBitmap != null) {
            Image(
                bitmap = imageBitmap,
                contentDescription = preview.title,
                contentScale = ContentScale.Crop,
                modifier = Modifier.matchParentSize(),
            )
        } else {
            Column(
                modifier = Modifier
                    .matchParentSize()
                    .padding(10.dp),
                verticalArrangement = Arrangement.SpaceBetween,
            ) {
                Icon(
                    painter = painterResource(id = MahoIcon.Globe.drawableRes),
                    contentDescription = null,
                    tint = shellColors.textSecondary,
                    modifier = Modifier.size(14.dp),
                )
                Text(
                    text = preview.host,
                    style = MaterialTheme.typography.labelSmall,
                    color = shellColors.textSecondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }

        Box(
            modifier = Modifier
                .matchParentSize()
                .background(
                    Brush.verticalGradient(
                        colors = listOf(
                            Color.Transparent,
                            Color.Black.copy(alpha = 0.14f),
                            Color.Black.copy(alpha = 0.26f),
                        ),
                    ),
                ),
        )

        Text(
            text = preview.title,
            style = MaterialTheme.typography.labelSmall,
            color = Color.White,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(horizontal = 10.dp, vertical = 8.dp),
        )
    }
}

private fun displayTitle(tab: TabViewModel): String {
    return tab.title.ifBlank { displayHost(tab.url) }
}

private fun displayHost(url: String): String {
    return runCatching {
        java.net.URI(url).host?.removePrefix("www.") ?: "New tab"
    }.getOrDefault("New tab")
}

private fun suggestedName(rawName: String, tabs: List<TabViewModel>): String {
    val trimmed = rawName.trim()
    if (trimmed.isNotEmpty()) {
        return trimmed
    }

    return tabs.firstOrNull()?.let { displayHost(it.url).replaceFirstChar { char -> char.titlecase() } }
        ?: "Suggested Group"
}
