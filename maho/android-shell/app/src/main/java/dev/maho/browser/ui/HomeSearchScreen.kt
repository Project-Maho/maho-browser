package dev.maho.browser.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.maho.browser.R
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.icons.MahoIcon
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
    val visibleRecentTabs = if (isIncognito) emptyList() else recentTabs.take(3)
    val visibleTopSites = if (isIncognito) emptyList() else topSites.take(8)
    val isEmptyHome = visibleRecentTabs.isEmpty() && visibleTopSites.isEmpty()

    val backgroundBrush = if (isIncognito) {
        Brush.verticalGradient(
            colors = listOf(
                Color(0xFF1A2352),
                Color(0xFF121838),
                Color(0xFF0D1124),
            )
        )
    } else {
        Brush.verticalGradient(
            colors = listOf(
                shellColors.homeBackgroundStart,
                shellColors.homeBackgroundMid,
                shellColors.homeBackgroundEnd,
            )
        )
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(backgroundBrush)
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
            MahoHomeHeroLogo(isIncognito = isIncognito)
        } else {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 20.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Spacer(modifier = Modifier.height(48.dp))
                MahoHomeHeroLogo(isIncognito = isIncognito, compact = true)
                Spacer(modifier = Modifier.height(32.dp))

                if (visibleRecentTabs.isNotEmpty()) {
                    Column(modifier = Modifier.testTag("homeRecentTabsSection")) {
                        HomeSectionHeader(title = "Continue", isIncognito = isIncognito)
                        Spacer(modifier = Modifier.height(10.dp))
                        visibleRecentTabs.forEach { tab ->
                            HomeRecentTabCard(
                                tab = tab,
                                isIncognito = isIncognito,
                                onClick = { onResumeTab(tab.id) },
                            )
                            Spacer(modifier = Modifier.height(8.dp))
                        }
                    }
                    Spacer(modifier = Modifier.height(20.dp))
                }

                if (visibleTopSites.isNotEmpty()) {
                    Column(modifier = Modifier.testTag("homeTopSitesSection")) {
                        HomeSectionHeader(title = "Top Sites", isIncognito = isIncognito)
                        Spacer(modifier = Modifier.height(12.dp))
                        HomeTopSitesGrid(
                            sites = visibleTopSites,
                            isIncognito = isIncognito,
                            onSelectSite = onSelectSite,
                        )
                    }
                }

                Spacer(modifier = Modifier.height(120.dp))
            }
        }
    }
}

@Composable
private fun MahoHomeHeroLogo(isIncognito: Boolean, compact: Boolean = false) {
    val shellColors = BrowserShellTheme.colors
    val starColor = if (isIncognito) Color(0xFFA5B4FC) else Color(0xFF818CF8)

    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
        modifier = Modifier
            .padding(horizontal = 24.dp)
            .testTag("homeHeroPanel"),
    ) {
        Surface(
            shape = CircleShape,
            color = starColor.copy(alpha = 0.12f),
            border = BorderStroke(1.dp, starColor.copy(alpha = 0.25f)),
            modifier = Modifier.size(if (compact) 64.dp else 96.dp),
        ) {
            Box(contentAlignment = Alignment.Center) {
                Image(
                    painter = painterResource(id = R.drawable.ic_maho_star),
                    contentDescription = "Maho Star",
                    modifier = Modifier.size(if (compact) 36.dp else 56.dp),
                )
            }
        }

        if (!compact) {
            Spacer(modifier = Modifier.height(24.dp))
            Text(
                text = "Maho",
                style = MaterialTheme.typography.headlineLarge.copy(
                    fontSize = 32.sp,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = (-0.5).sp,
                ),
                color = if (isIncognito) Color.White else shellColors.textPrimary,
            )
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = if (isIncognito) "Private Browsing" else "Built for people who take browsers seriously.",
                style = MaterialTheme.typography.bodyMedium.copy(fontSize = 14.sp),
                color = if (isIncognito) Color.White.copy(alpha = 0.7f) else shellColors.textSecondary,
                textAlign = TextAlign.Center,
            )
        }
    }
}

@Composable
private fun HomeSectionHeader(title: String, isIncognito: Boolean) {
    val shellColors = BrowserShellTheme.colors
    Text(
        text = title.uppercase(),
        style = MaterialTheme.typography.labelSmall.copy(
            fontSize = 11.5.sp,
            fontWeight = FontWeight.SemiBold,
            letterSpacing = 0.8.sp,
        ),
        color = if (isIncognito) Color.White.copy(alpha = 0.6f) else shellColors.textSecondary,
        modifier = Modifier.fillMaxWidth().padding(start = 4.dp),
    )
}

@Composable
private fun HomeRecentTabCard(
    tab: TabViewModel,
    isIncognito: Boolean,
    onClick: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val cardBg = if (isIncognito) Color.White.copy(alpha = 0.08f) else shellColors.cardBackground
    val cardBorder = if (isIncognito) Color.White.copy(alpha = 0.12f) else shellColors.cardBorder

    Surface(
        shape = RoundedCornerShape(14.dp),
        color = cardBg,
        border = BorderStroke(0.5.dp, cardBorder),
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Surface(
                shape = RoundedCornerShape(8.dp),
                color = Color(0xFF0A84FF).copy(alpha = 0.15f),
                modifier = Modifier.size(32.dp),
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        painter = painterResource(id = MahoIcon.Globe.drawableRes),
                        contentDescription = null,
                        tint = Color(0xFF0A84FF),
                        modifier = Modifier.size(16.dp),
                    )
                }
            }

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = tab.title.ifBlank { tab.url },
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontSize = 14.5.sp,
                        fontWeight = FontWeight.Medium,
                    ),
                    color = if (isIncognito) Color.White else shellColors.textPrimary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = tab.url,
                    style = MaterialTheme.typography.bodySmall.copy(fontSize = 12.sp),
                    color = if (isIncognito) Color.White.copy(alpha = 0.6f) else shellColors.textSecondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

@Composable
private fun HomeTopSitesGrid(
    sites: List<TabViewModel>,
    isIncognito: Boolean,
    onSelectSite: (String) -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    val itemBg = if (isIncognito) Color.White.copy(alpha = 0.08f) else shellColors.cardBackground
    val itemBorder = if (isIncognito) Color.White.copy(alpha = 0.12f) else shellColors.cardBorder

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        sites.take(4).forEach { site ->
            Column(
                modifier = Modifier
                    .weight(1f)
                    .clip(RoundedCornerShape(12.dp))
                    .clickable { onSelectSite(site.url) }
                    .padding(vertical = 6.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Surface(
                    shape = RoundedCornerShape(14.dp),
                    color = itemBg,
                    border = BorderStroke(0.5.dp, itemBorder),
                    modifier = Modifier.size(54.dp),
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Text(
                            text = site.title.take(1).uppercase().ifBlank { "W" },
                            fontSize = 18.sp,
                            fontWeight = FontWeight.Bold,
                            color = if (isIncognito) Color.White else shellColors.accent,
                        )
                    }
                }

                Text(
                    text = site.title.ifBlank { site.url },
                    style = MaterialTheme.typography.labelSmall.copy(fontSize = 11.5.sp),
                    color = if (isIncognito) Color.White.copy(alpha = 0.8f) else shellColors.textPrimary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    textAlign = TextAlign.Center,
                )
            }
        }
    }
}

private fun buildHomeStateDescription(
    activeSpaceName: String,
    activeSpaceTabCount: Int,
    isIncognito: Boolean,
): String {
    return buildString {
        append(if (isIncognito) "private home" else "home")
        if (activeSpaceName.isNotBlank()) {
            append(", space $activeSpaceName")
        }
        append(", $activeSpaceTabCount tabs")
    }
}
