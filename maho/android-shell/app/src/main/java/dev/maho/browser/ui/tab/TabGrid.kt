package dev.maho.browser.ui.tab

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.maho.browser.bridge.BridgeTabs
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.graphics.Color

@Composable
fun TabGrid(
    tabs: List<TabViewModel>,
    spaceId: SpaceId?,
    activeTabId: TabId?,
    isIncognito: Boolean,
    onTabSelected: (TabId) -> Unit,
    onTabArchived: (TabId) -> Unit,
    onTabClosed: (TabId) -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val favorites = spaceId?.let { BridgeTabs.getFavoriteTabs(it) } ?: emptyList()
    val accentColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent
    val deckSurfaceColor = if (isIncognito) {
        shellColors.incognitoBackground.copy(alpha = 0.54f)
    } else {
        shellColors.overlaySurface.copy(alpha = 0.58f)
    }
    val deckBorderColor = if (isIncognito) {
        shellColors.incognitoAccent.copy(alpha = 0.18f)
    } else {
        shellColors.divider.copy(alpha = 0.42f)
    }
    if (tabs.isEmpty() && favorites.isEmpty()) {
        Box(
            modifier = modifier
                .fillMaxSize()
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 52.dp)
                .testTag("tabGridEmptyState")
                .semantics {
                    stateDescription = if (isIncognito) "no private tabs" else "no tabs"
                },
            contentAlignment = Alignment.Center,
        ) {
            Surface(
                shape = RoundedCornerShape(metrics.panelCorner),
                color = if (isIncognito) {
                    shellColors.incognitoSurface.copy(alpha = 0.72f)
                } else {
                    shellColors.overlaySurface.copy(alpha = 0.72f)
                },
            ) {
                Column(
                    modifier = Modifier.padding(horizontal = 22.dp, vertical = 22.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    Box(
                        modifier = Modifier
                            .size(46.dp)
                            .background(accentColor.copy(alpha = 0.16f), CircleShape),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            painter = painterResource(id = if (isIncognito) MahoIcon.VisibilityOff.drawableRes else MahoIcon.Tab.drawableRes),
                            contentDescription = null,
                            tint = accentColor,
                        )
                    }
                    Text(
                        text = if (isIncognito) "No private tabs" else "No tabs yet",
                        style = MaterialTheme.typography.titleMedium,
                        color = shellColors.textPrimary,
                    )
                    Text(
                        text = if (isIncognito) {
                            "Open one to browse privately."
                        } else {
                            "Open one to keep browsing."
                        },
                        style = MaterialTheme.typography.bodyMedium,
                        color = shellColors.textSecondary,
                    )
                }
            }
        }
        return
    }

    Column(
        modifier = modifier
            .testTag("tabGrid")
            .semantics {
                stateDescription = if (isIncognito) "private tab grid" else "tab grid"
            },
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .border(1.dp, deckBorderColor, RoundedCornerShape(metrics.panelCorner))
                .padding(0.5.dp)
                .testTag("tabGridDeckHeader"),
            shape = RoundedCornerShape(metrics.panelCorner),
            color = deckSurfaceColor,
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 14.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    Box(
                        modifier = Modifier
                            .size(34.dp)
                            .background(accentColor.copy(alpha = 0.14f), CircleShape),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            painter = painterResource(id = if (isIncognito) MahoIcon.VisibilityOff.drawableRes else MahoIcon.Tab.drawableRes),
                            contentDescription = null,
                            tint = accentColor,
                            modifier = Modifier.size(16.dp),
                        )
                    }

                    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Text(
                            text = if (isIncognito) "Private deck" else "Tab deck",
                            style = MaterialTheme.typography.titleSmall,
                            color = shellColors.textPrimary,
                        )
                        Text(
                            text = "Swipe through your latest pages and reopen context fast.",
                            style = MaterialTheme.typography.labelSmall,
                            color = shellColors.textSecondary,
                        )
                    }
                }

                Surface(
                    shape = RoundedCornerShape(999.dp),
                    color = shellColors.windowBackground.copy(alpha = 0.14f),
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = tabs.size.toString(),
                            style = MaterialTheme.typography.labelLarge,
                            color = shellColors.textPrimary,
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = if (tabs.size == 1) "tab" else "tabs",
                            style = MaterialTheme.typography.labelMedium,
                            color = shellColors.textSecondary,
                        )
                    }
                }
            }
        }

        LazyVerticalGrid(
            columns = GridCells.Fixed(2),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
            contentPadding = PaddingValues(top = 2.dp, bottom = 132.dp),
            modifier = Modifier.fillMaxSize(),
        ) {
            val pinned = tabs.filter { it.isPinned && !it.isFavorite }
            val today = tabs.filter { !it.isPinned && !it.isFavorite }

            if (favorites.isNotEmpty()) {
                item(span = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }) {
                    Column(
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                        modifier = Modifier.padding(vertical = 8.dp)
                    ) {
                        Text(
                            text = "Favorites",
                            style = MaterialTheme.typography.titleMedium,
                            color = shellColors.textSecondary,
                            modifier = Modifier.padding(horizontal = 8.dp),
                        )
                        LazyRow(
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                            contentPadding = PaddingValues(horizontal = 8.dp),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            items(
                                count = favorites.size,
                                key = { favorites[it].id }
                            ) { index ->
                                val tab = favorites[index]
                                val host = runCatching {
                                    java.net.URI(tab.url).host?.removePrefix("www.") ?: tab.url
                                }.getOrDefault(tab.url)
                                val letter = host.firstOrNull()?.uppercaseChar()?.toString() ?: "N"
                                val hash = host.hashCode()
                                val hues = listOf(0f, 30f, 60f, 120f, 200f, 270f)
                                val hue = hues[Math.abs(hash) % hues.size]
                                val monogramColor = Color.hsv(hue, 0.7f, 0.85f)
                                val isTabActive = tab.id == activeTabId

                                Box(
                                    contentAlignment = Alignment.Center,
                                    modifier = Modifier
                                        .size(44.dp)
                                        .clip(RoundedCornerShape(8.dp))
                                        .background(monogramColor)
                                        .border(
                                            width = if (isTabActive) 2.dp else 1.dp,
                                            color = if (isTabActive) accentColor else shellColors.divider.copy(alpha = 0.24f),
                                            shape = RoundedCornerShape(8.dp)
                                        )
                                        .clickable { onTabSelected(tab.id) }
                                ) {
                                    Text(
                                        text = letter,
                                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                                        color = Color.White
                                    )
                                }
                            }
                        }
                    }
                }
            }

            if (pinned.isNotEmpty()) {
                item(span = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }) {
                    Text(
                        text = "Pinned",
                        style = MaterialTheme.typography.titleMedium,
                        color = shellColors.textSecondary,
                        modifier = Modifier.padding(vertical = 4.dp)
                    )
                }
                itemsIndexed(
                    items = pinned,
                    key = { _, tab -> tab.id },
                ) { index, tab ->
                    TabCard(
                        tab = tab,
                        isActive = tab.id == activeTabId,
                        isIncognito = isIncognito,
                        visualIndex = index,
                        onTap = { onTabSelected(tab.id) },
                        onArchive = { onTabArchived(tab.id) },
                        onClose = { onTabClosed(tab.id) },
                    )
                }
            }

            if (today.isNotEmpty()) {
                item(span = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }) {
                    Text(
                        text = "Today",
                        style = MaterialTheme.typography.titleMedium,
                        color = shellColors.textSecondary,
                        modifier = Modifier.padding(vertical = 4.dp)
                    )
                }
                itemsIndexed(
                    items = today,
                    key = { _, tab -> tab.id },
                ) { index, tab ->
                    TabCard(
                        tab = tab,
                        isActive = tab.id == activeTabId,
                        isIncognito = isIncognito,
                        visualIndex = index + pinned.size,
                        onTap = { onTabSelected(tab.id) },
                        onArchive = { onTabArchived(tab.id) },
                        onClose = { onTabClosed(tab.id) },
                    )
                }
            }
        }
    }
}
