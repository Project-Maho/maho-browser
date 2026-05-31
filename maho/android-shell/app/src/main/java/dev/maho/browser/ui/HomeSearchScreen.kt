@file:OptIn(ExperimentalMaterial3Api::class)

package dev.maho.browser.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.theme.homeBackgroundBrush
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun HomeSearchScreen(
    isIncognito: Boolean,
    activeSpaceName: String,
    activeSpaceTabCount: Int,
    recentTabs: List<TabViewModel>,
    topSites: List<TabViewModel>,
    onSelectSite: (String) -> Unit,
    onResumeTab: (TabId) -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val scrollState = rememberScrollState()

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(
                brush = shellColors.homeBackgroundBrush(isIncognito),
            )
            .safeDrawingPadding()
            .testTag("homeSearchView"),
    ) {
        Surface(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(horizontal = 20.dp, vertical = 16.dp),
            shape = RoundedCornerShape(metrics.panelCorner),
            color = if (isIncognito) {
                shellColors.incognitoSurface.copy(alpha = 0.78f)
            } else {
                shellColors.overlaySurface.copy(alpha = 0.62f)
            },
            tonalElevation = 0.dp,
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Box(
                    modifier = Modifier
                        .size(34.dp)
                        .clip(CircleShape)
                        .background(
                            (if (isIncognito) shellColors.incognitoAccent else shellColors.accent).copy(alpha = 0.16f),
                        ),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        painter = painterResource(id = if (isIncognito) MahoIcon.VisibilityOff.drawableRes else MahoIcon.SparklesAi.drawableRes),
                        contentDescription = null,
                        tint = if (isIncognito) shellColors.incognitoAccent else shellColors.accent,
                        modifier = Modifier.size(18.dp),
                    )
                }

                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    Text(
                        text = if (isIncognito) "Private start" else activeSpaceName,
                        style = MaterialTheme.typography.titleMedium,
                        color = shellColors.textPrimary,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        text = if (activeSpaceTabCount > 0) {
                            "$activeSpaceTabCount open tab${if (activeSpaceTabCount != 1) "s" else ""}"
                        } else {
                            "Search from the bottom bar to start browsing"
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = shellColors.textSecondary,
                    )
                }
            }
        }

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(scrollState)
                .padding(horizontal = 20.dp)
                .padding(top = 104.dp, bottom = 120.dp)
                .navigationBarsPadding(),
            verticalArrangement = Arrangement.spacedBy(metrics.sectionSpacing),
        ) {
            HomeStartPanel(
                activeSpaceName = activeSpaceName,
                activeSpaceTabCount = activeSpaceTabCount,
                isIncognito = isIncognito,
            )

            if (recentTabs.isNotEmpty()) {
                HomeRecentTabsSection(
                    isIncognito = isIncognito,
                    recentTabs = recentTabs,
                    onResumeTab = onResumeTab,
                )
            }

            if (topSites.isNotEmpty()) {
                HomeTopSitesSection(
                    isIncognito = isIncognito,
                    topSites = topSites,
                    onSelectSite = onSelectSite,
                )
            }
        }
    }
}

@Composable
private fun HomeStartPanel(
    activeSpaceName: String,
    activeSpaceTabCount: Int,
    isIncognito: Boolean,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val accentColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 8.dp)
            .testTag("homeHeroPanel"),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Surface(
            shape = RoundedCornerShape(metrics.panelCorner),
            color = if (isIncognito) {
                shellColors.incognitoBackground.copy(alpha = 0.82f)
            } else {
                shellColors.overlaySurfaceHigh.copy(alpha = 0.54f)
            },
            tonalElevation = 0.dp,
        ) {
            Column(
                modifier = Modifier.padding(horizontal = 18.dp, vertical = 18.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                HomeBrandLockup(
                    activeSpaceName = activeSpaceName,
                    isIncognito = isIncognito,
                )

                Column(
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(
                        text = if (isIncognito) "Private browsing, anchored below." else "Your browser starts at the bottom.",
                        style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.SemiBold),
                        color = shellColors.textPrimary,
                        modifier = Modifier.semantics { heading() },
                    )
                    Text(
                        text = if (isIncognito) {
                            "Use the bottom search bar to open a private page, then keep tabs and menu controls within reach."
                        } else {
                            "Search from the anchored bar, reopen a tab below, or jump into a familiar site without leaving the browser shell."
                        },
                        style = MaterialTheme.typography.bodyMedium,
                        color = shellColors.textSecondary,
                    )
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    HomeMetaChip(
                        text = activeSpaceName,
                        icon = MahoIcon.SparklesAi,
                        tint = accentColor,
                        modifier = Modifier.weight(1f),
                        isIncognito = isIncognito,
                    )
                    HomeMetaChip(
                        text = if (activeSpaceTabCount > 0) {
                            "$activeSpaceTabCount ready"
                        } else {
                            "Search below"
                        },
                        icon = MahoIcon.ChevronRight,
                        tint = if (isIncognito) shellColors.incognitoAccent else shellColors.textSecondary,
                        modifier = Modifier.weight(1f),
                        isIncognito = isIncognito,
                    )
                }
            }
        }

        Surface(
            shape = RoundedCornerShape(metrics.cardCorner),
            color = if (isIncognito) {
                shellColors.incognitoSurface.copy(alpha = 0.74f)
            } else {
                shellColors.overlaySurface.copy(alpha = 0.48f)
            },
            tonalElevation = 0.dp,
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Box(
                    modifier = Modifier
                        .size(36.dp)
                        .clip(RoundedCornerShape(metrics.compactCorner))
                        .background(accentColor.copy(alpha = 0.16f)),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        painter = painterResource(id = MahoIcon.ChevronRight.drawableRes),
                        contentDescription = null,
                        tint = accentColor,
                    )
                }

                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    Text(
                        text = "Bottom chrome stays live on home",
                        style = MaterialTheme.typography.titleSmall,
                        color = shellColors.textPrimary,
                    )
                    Text(
                        text = "Tabs, search, and menu are anchored below for a browser-first start state.",
                        style = MaterialTheme.typography.bodySmall,
                        color = shellColors.textSecondary,
                    )
                }
            }
        }
    }
}

@Composable
private fun HomeBrandLockup(
    activeSpaceName: String,
    isIncognito: Boolean,
) {
    val shellColors = BrowserShellTheme.colors
    val accentColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent

    Surface(
        shape = RoundedCornerShape(999.dp),
        color = if (isIncognito) {
            shellColors.incognitoSurface.copy(alpha = 0.86f)
        } else {
            shellColors.overlaySurface.copy(alpha = 0.72f)
        },
        tonalElevation = 0.dp,
    ) {
        Row(
            modifier = Modifier
                .border(
                    width = 1.dp,
                    color = accentColor.copy(alpha = 0.18f),
                    shape = RoundedCornerShape(999.dp),
                )
                .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Box(
                modifier = Modifier
                    .size(34.dp)
                    .clip(CircleShape)
                    .background(accentColor.copy(alpha = 0.14f)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                    contentDescription = null,
                    tint = accentColor,
                    modifier = Modifier.size(18.dp),
                )
            }

            Column(verticalArrangement = Arrangement.spacedBy(1.dp)) {
                Text(
                    text = "Maho",
                    style = MaterialTheme.typography.labelLarge,
                    color = shellColors.textPrimary,
                    fontWeight = FontWeight.SemiBold,
                )
                Text(
                    text = if (isIncognito) "Private • $activeSpaceName" else activeSpaceName,
                    style = MaterialTheme.typography.bodySmall,
                    color = shellColors.textSecondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

@Composable
private fun HomeMetaChip(
    text: String,
    icon: MahoIcon,
    tint: Color,
    isIncognito: Boolean,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics

    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(metrics.cardCorner),
        color = if (isIncognito) {
            shellColors.incognitoSurface.copy(alpha = 0.76f)
        } else {
            shellColors.overlaySurface.copy(alpha = 0.52f)
        },
        tonalElevation = 0.dp,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Box(
                modifier = Modifier
                    .size(28.dp)
                    .clip(CircleShape)
                    .background(tint.copy(alpha = 0.14f)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(id = icon.drawableRes),
                    contentDescription = null,
                    tint = tint,
                    modifier = Modifier.size(15.dp),
                )
            }
            Text(
                text = text,
                style = MaterialTheme.typography.bodyMedium,
                color = shellColors.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun HomeRecentTabsSection(
    isIncognito: Boolean,
    recentTabs: List<TabViewModel>,
    onResumeTab: (TabId) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("homeRecentTabsSection"),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        HomeSectionHeader(
            title = "Continue",
            subtitle = "Pick up where you left off",
        )
        Column(
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            recentTabs.forEachIndexed { index, tab ->
                HomeRecentTabRow(
                    isIncognito = isIncognito,
                    tab = tab,
                    index = index,
                    onResumeTab = onResumeTab,
                )
                if (index != recentTabs.lastIndex) {
                    HorizontalDivider(
                        color = BrowserShellTheme.colors.divider.copy(alpha = 0.28f),
                    )
                }
            }
        }
    }
}

@Composable
private fun HomeRecentTabRow(
    isIncognito: Boolean,
    tab: TabViewModel,
    index: Int,
    onResumeTab: (TabId) -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("homeRecentTab_$index")
            .clip(RoundedCornerShape(metrics.cardCorner))
            .clickable { onResumeTab(tab.id) },
        color = if (isIncognito) {
            shellColors.incognitoBackground.copy(alpha = 0.74f)
        } else {
            shellColors.overlaySurfaceHigh.copy(alpha = 0.5f)
        },
        tonalElevation = 0.dp,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(RoundedCornerShape(metrics.compactCorner))
                    .background(shellColors.accentMuted),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(id = if (tab.isFavorite) MahoIcon.Star.drawableRes else MahoIcon.Clock.drawableRes),
                    contentDescription = null,
                    tint = shellColors.accent,
                )
            }

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = tab.title.ifBlank { displayTitle(tab.url) },
                    style = MaterialTheme.typography.titleSmall,
                    color = shellColors.textPrimary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Spacer(modifier = Modifier.height(2.dp))
                Text(
                    text = displayHost(tab.url),
                    style = MaterialTheme.typography.bodySmall,
                    color = shellColors.textSecondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }

            Icon(
                painter = painterResource(id = MahoIcon.NavForward.drawableRes),
                contentDescription = null,
                tint = shellColors.textSecondary,
            )
        }
    }
}

@Composable
private fun HomeTopSitesSection(
    isIncognito: Boolean,
    topSites: List<TabViewModel>,
    onSelectSite: (String) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("homeTopSitesSection"),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        HomeSectionHeader(
            title = "Top sites",
            subtitle = "Fast ways back in",
        )
        Column(
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            topSites.chunked(2).forEachIndexed { rowIndex, rowItems ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    rowItems.forEachIndexed { columnIndex, site ->
                        HomeTopSiteCard(
                            isIncognito = isIncognito,
                            tab = site,
                            index = (rowIndex * 2) + columnIndex,
                            onSelectSite = onSelectSite,
                            modifier = Modifier.weight(1f),
                        )
                    }

                    if (rowItems.size == 1) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@Composable
private fun HomeTopSiteCard(
    isIncognito: Boolean,
    tab: TabViewModel,
    index: Int,
    onSelectSite: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics

    Surface(
        modifier = modifier
            .testTag("homeTopSite_$index")
            .clip(RoundedCornerShape(metrics.cardCorner))
            .clickable { onSelectSite(tab.url) },
        color = if (isIncognito) {
            shellColors.incognitoBackground.copy(alpha = 0.78f)
        } else {
            shellColors.overlaySurfaceHigh.copy(alpha = 0.56f)
        },
        tonalElevation = 0.dp,
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(RoundedCornerShape(metrics.compactCorner))
                    .background(shellColors.accentMuted),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(id = if (tab.isFavorite) MahoIcon.Star.drawableRes else MahoIcon.Globe.drawableRes),
                    contentDescription = null,
                    tint = shellColors.textPrimary,
                )
            }

            Text(
                text = tab.title.ifBlank { displayTitle(tab.url) },
                style = MaterialTheme.typography.titleSmall,
                color = shellColors.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )

            Text(
                text = displayHost(tab.url),
                style = MaterialTheme.typography.bodySmall,
                color = shellColors.textSecondary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun HomeSectionHeader(
    title: String,
    subtitle: String? = null,
) {
    val shellColors = BrowserShellTheme.colors

    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.labelLarge,
            color = shellColors.textSecondary,
            fontWeight = FontWeight.SemiBold,
        )
        if (subtitle != null) {
            Text(
                text = subtitle,
                style = MaterialTheme.typography.titleSmall,
                color = shellColors.textPrimary,
            )
        }
    }
}

private fun displayHost(url: String): String {
    return runCatching {
        java.net.URI(url).host ?: url
    }.getOrDefault(url)
}

private fun displayTitle(url: String): String {
    return displayHost(url).removePrefix("www.")
}
