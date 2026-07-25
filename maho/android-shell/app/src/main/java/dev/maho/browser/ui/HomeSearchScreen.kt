package dev.maho.browser.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.R
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellColors
import dev.maho.browser.ui.theme.BrowserShellTheme

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
    val isDarkMode = isSystemInDarkTheme()
    val homeColors = homeColors(
        isIncognito = isIncognito,
        isDarkMode = isDarkMode,
        shellColors = shellColors,
    )
    val visibleRecentTabs = if (isIncognito) emptyList() else recentTabs.take(3)
    val visibleTopSites = if (isIncognito) emptyList() else topSites.take(8)
    val isEmptyHome = visibleRecentTabs.isEmpty() && visibleTopSites.isEmpty()

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(homeColors.backgroundBrush)
            .semantics {
                stateDescription = buildHomeStateDescription(
                    activeSpaceName = activeSpaceName,
                    activeSpaceTabCount = activeSpaceTabCount,
                    isIncognito = isIncognito,
                )
            }
            .testTag("homeSearchView"),
        contentAlignment = if (isEmptyHome) Alignment.Center else Alignment.TopCenter,
    ) {
        if (isEmptyHome) {
            MahoHomeLogo()
        } else {
            ReturningHomeContent(
                recentTabs = visibleRecentTabs,
                topSites = visibleTopSites,
                colors = homeColors,
                onSelectSite = onSelectSite,
                onResumeTab = onResumeTab,
            )
        }
    }
}

@Composable
private fun MahoHomeLogo() {
    val logoPainter = painterResource(id = R.drawable.ic_maho_star)

    Image(
        painter = logoPainter,
        contentDescription = "Maho",
        contentScale = ContentScale.Fit,
        modifier = Modifier
            .size(120.dp)
            .testTag("homeHeroPanel"),
    )
}

@Composable
private fun ReturningHomeContent(
    recentTabs: List<TabViewModel>,
    topSites: List<TabViewModel>,
    colors: HomeColors,
    onSelectSite: (String) -> Unit,
    onResumeTab: (TabId) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(modifier = Modifier.height(16.dp))

        MahoHomeLogo()

        if (recentTabs.isNotEmpty()) {
            Spacer(modifier = Modifier.height(16.dp))
            ContinueSection(
                tabs = recentTabs,
                colors = colors,
                onResumeTab = onResumeTab,
                modifier = Modifier.fillMaxWidth(),
            )
        }

        if (topSites.isNotEmpty()) {
            Spacer(modifier = Modifier.height(16.dp))
            TopSitesSection(
                tabs = topSites,
                colors = colors,
                onSelectSite = onSelectSite,
                modifier = Modifier.fillMaxWidth(),
            )
        }

        Spacer(modifier = Modifier.height(52.dp))
    }
}

@Composable
private fun ContinueSection(
    tabs: List<TabViewModel>,
    colors: HomeColors,
    onResumeTab: (TabId) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.testTag("homeRecentTabsSection"),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        SectionHeader(
            title = "Continue",
            subtitle = "Pick up where you left off",
            colors = colors,
        )

        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            color = colors.cardSurface,
            border = BorderStroke(1.dp, colors.cardBorder),
            tonalElevation = 0.dp,
            shadowElevation = 0.dp,
        ) {
            Column(modifier = Modifier.fillMaxWidth()) {
                tabs.forEachIndexed { index, tab ->
                    ContinueRow(
                        tab = tab,
                        colors = colors,
                        onClick = { onResumeTab(tab.id) },
                    )

                    if (index < tabs.lastIndex) {
                        HorizontalDivider(
                            modifier = Modifier.padding(start = 16.dp),
                            thickness = 1.dp,
                            color = colors.cardBorder,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ContinueRow(
    tab: TabViewModel,
    colors: HomeColors,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Icon(
            painter = painterResource(id = MahoIcon.Globe.drawableRes),
            contentDescription = null,
            modifier = Modifier.size(20.dp),
            tint = colors.accent,
        )

        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text(
                text = tab.title.ifBlank { "New tab" },
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = colors.primaryText,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = displayHost(tab.url),
                style = MaterialTheme.typography.bodySmall,
                color = colors.secondaryText,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        Icon(
            painter = painterResource(id = MahoIcon.ChevronRight.drawableRes),
            contentDescription = null,
            modifier = Modifier.size(16.dp),
            tint = colors.secondaryText,
        )
    }
}

@Composable
private fun TopSitesSection(
    tabs: List<TabViewModel>,
    colors: HomeColors,
    onSelectSite: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.testTag("homeTopSitesSection"),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        SectionHeader(
            title = "Top sites",
            subtitle = "Fast ways back in",
            colors = colors,
        )

        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            tabs.chunked(2).forEach { rowTabs ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    rowTabs.forEach { tab ->
                        TopSiteCard(
                            tab = tab,
                            colors = colors,
                            onClick = { onSelectSite(tab.url) },
                            modifier = Modifier.weight(1f),
                        )
                    }

                    if (rowTabs.size == 1) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@Composable
private fun TopSiteCard(
    tab: TabViewModel,
    colors: HomeColors,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier = modifier.clickable(onClick = onClick),
        shape = RoundedCornerShape(16.dp),
        color = colors.cardSurface,
        border = BorderStroke(1.dp, colors.cardBorder),
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Icon(
                painter = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                contentDescription = null,
                modifier = Modifier.size(16.dp),
                tint = colors.accent,
            )

            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                val host = displayHost(tab.url)
                Text(
                    text = tab.title.ifBlank { host },
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = FontWeight.SemiBold,
                    color = colors.primaryText,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = host,
                    style = MaterialTheme.typography.labelSmall,
                    color = colors.secondaryText,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

@Composable
private fun SectionHeader(
    title: String,
    subtitle: String,
    colors: HomeColors,
) {
    Column(
        modifier = Modifier.padding(start = 8.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            color = colors.primaryText,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = subtitle,
            style = MaterialTheme.typography.bodySmall,
            color = colors.secondaryText,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

private data class HomeColors(
    val backgroundBrush: Brush,
    val cardSurface: Color,
    val cardBorder: Color,
    val primaryText: Color,
    val secondaryText: Color,
    val accent: Color,
)

private fun homeColors(
    isIncognito: Boolean,
    isDarkMode: Boolean,
    shellColors: BrowserShellColors,
): HomeColors {
    if (isIncognito) {
        val incognitoBase = Color(0xFF1A2352)
        return HomeColors(
            backgroundBrush = Brush.linearGradient(
                colors = listOf(
                    incognitoBase,
                    incognitoBase.copy(alpha = 0.82f),
                    incognitoBase,
                ),
            ),
            cardSurface = Color.White.copy(alpha = 0.08f),
            cardBorder = Color.White.copy(alpha = 0.14f),
            primaryText = Color.White,
            secondaryText = Color.White.copy(alpha = 0.7f),
            accent = shellColors.accent,
        )
    }

    return if (isDarkMode) {
        HomeColors(
            backgroundBrush = Brush.verticalGradient(
                colors = listOf(shellColors.homeBackgroundStart, shellColors.homeBackgroundEnd),
            ),
            cardSurface = shellColors.overlaySurface.copy(alpha = 0.85f),
            cardBorder = shellColors.textPrimary.copy(alpha = 0.08f),
            primaryText = shellColors.textPrimary,
            secondaryText = shellColors.textSecondary,
            accent = shellColors.accent,
        )
    } else {
        HomeColors(
            backgroundBrush = Brush.verticalGradient(
                colors = listOf(Color(0xFFF2F2F7), Color.White),
            ),
            cardSurface = Color.White.copy(alpha = 0.85f),
            cardBorder = Color.Black.copy(alpha = 0.08f),
            primaryText = Color.Black,
            secondaryText = Color.Black.copy(alpha = 0.6f),
            accent = shellColors.accent,
        )
    }
}

private fun buildHomeStateDescription(
    activeSpaceName: String,
    activeSpaceTabCount: Int,
    isIncognito: Boolean,
): String {
    val spaceName = activeSpaceName.ifBlank { "Space" }
    val tabLabel = "$activeSpaceTabCount tab${if (activeSpaceTabCount == 1) "" else "s"}"
    return buildString {
        if (isIncognito) append("incognito ")
        append("home, ")
        append(spaceName)
        append(", ")
        append(tabLabel)
    }
}

private fun displayHost(url: String): String {
    val trimmedUrl = url.trim()
    if (trimmedUrl.isEmpty()) return "New tab"

    return runCatching {
        java.net.URI(trimmedUrl).host
    }.getOrNull()
        ?.removePrefix("www.")
        ?: "New tab"
}
