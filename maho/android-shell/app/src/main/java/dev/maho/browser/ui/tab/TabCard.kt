package dev.maho.browser.ui.tab

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun TabCard(
    tab: TabViewModel,
    isActive: Boolean,
    isIncognito: Boolean,
    visualIndex: Int,
    onTap: () -> Unit,
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val accentColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent
    val borderColor = when {
        isActive && isIncognito -> shellColors.incognitoAccent.copy(alpha = 0.85f)
        isActive -> shellColors.tabCardBorder.copy(alpha = 0.86f)
        isIncognito -> shellColors.incognitoSurface.copy(alpha = 0.84f)
        else -> shellColors.divider
    }
    val borderStroke = if (isActive) {
        BorderStroke(1.15.dp, borderColor)
    } else {
        BorderStroke(1.dp, borderColor.copy(alpha = 0.24f))
    }
    val cardColor = when {
        isIncognito && isActive -> shellColors.incognitoSurface.copy(alpha = 0.96f)
        isIncognito -> shellColors.incognitoBackground.copy(alpha = 0.96f)
        isActive -> shellColors.tabCardActive.copy(alpha = 0.96f)
        else -> shellColors.tabCard.copy(alpha = 0.94f)
    }
    val previewBrush = Brush.verticalGradient(
        colors = when {
            isIncognito -> listOf(
                shellColors.incognitoSurface.copy(alpha = 0.96f),
                shellColors.incognitoBackground.copy(alpha = 0.92f),
            )
            isActive -> listOf(
                shellColors.tabCardActive.copy(alpha = 0.96f),
                shellColors.overlaySurface.copy(alpha = 0.96f),
            )
            else -> listOf(
                shellColors.overlaySurfaceHigh.copy(alpha = 0.92f),
                shellColors.tabCard.copy(alpha = 0.94f),
            )
        },
    )
    val previewFrameColor = if (isActive) {
        accentColor.copy(alpha = 0.16f)
    } else {
        shellColors.divider.copy(alpha = 0.18f)
    }
    val statusSurfaceColor = if (isActive) {
        accentColor.copy(alpha = 0.14f)
    } else {
        shellColors.windowBackground.copy(alpha = 0.16f)
    }
    val hostChipColor = if (isIncognito) {
        shellColors.incognitoSurface.copy(alpha = 0.5f)
    } else {
        shellColors.windowBackground.copy(alpha = 0.16f)
    }
    val metaChipColor = if (isActive) {
        accentColor.copy(alpha = 0.14f)
    } else {
        shellColors.windowBackground.copy(alpha = 0.14f)
    }
    val title = tab.title.ifBlank { "New tab" }
    val host = displayHost(tab.url.ifBlank { "about:blank" })
    val statusLabel = when {
        tab.isLoading -> "Loading"
        isActive -> "Current"
        else -> tab.lifecycleState.replaceFirstChar { it.uppercase() }
    }
    val details = buildList {
        if (tab.children.isNotEmpty()) add("${tab.children.size} linked")
        if (tab.isFavorite) add("Saved")
    }

    Card(
        shape = RoundedCornerShape(metrics.cardCorner),
        border = borderStroke,
        colors = CardDefaults.cardColors(containerColor = cardColor),
        elevation = CardDefaults.cardElevation(
            defaultElevation = if (isActive) 4.dp else 1.dp,
        ),
        modifier = modifier
            .fillMaxWidth()
            .testTag("tabCard_$visualIndex")
            .semantics {
                stateDescription = buildTabStateDescription(tab = tab, isActive = isActive, isIncognito = isIncognito)
            }
            .clickable(onClick = onTap),
    ) {
        Column(modifier = Modifier.padding(11.dp)) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(144.dp)
                    .clip(RoundedCornerShape(metrics.cardCorner))
                    .background(previewBrush)
                    .border(1.dp, previewFrameColor, RoundedCornerShape(metrics.cardCorner))
                    .padding(14.dp),
                verticalArrangement = Arrangement.SpaceBetween,
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(5.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        repeat(3) { dotIndex ->
                            Box(
                                modifier = Modifier
                                    .size(6.dp)
                                    .clip(CircleShape)
                                    .background(
                                        when (dotIndex) {
                                            0 -> accentColor.copy(alpha = 0.88f)
                                            1 -> shellColors.textSecondary.copy(alpha = 0.28f)
                                            else -> shellColors.textSecondary.copy(alpha = 0.14f)
                                        },
                                    ),
                            )
                        }
                    }

                    Surface(
                        shape = RoundedCornerShape(999.dp),
                        color = statusSurfaceColor,
                    ) {
                        Text(
                            text = statusLabel,
                            style = MaterialTheme.typography.labelSmall,
                            color = if (tab.isLoading || isActive) accentColor else shellColors.textSecondary,
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 5.dp),
                        )
                    }
                }

                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Surface(
                        shape = RoundedCornerShape(999.dp),
                        color = hostChipColor,
                    ) {
                        Text(
                            text = host.uppercase(),
                            style = MaterialTheme.typography.labelSmall,
                            color = shellColors.textSecondary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.padding(horizontal = 9.dp, vertical = 5.dp),
                        )
                    }
                    Text(
                        text = title,
                        style = MaterialTheme.typography.titleMedium,
                        color = shellColors.textPrimary,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }

                Row(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    if (tab.isLoading) {
                        CircularProgressIndicator(
                            strokeWidth = 2.dp,
                            modifier = Modifier.size(14.dp),
                            color = accentColor,
                            trackColor = accentColor.copy(alpha = 0.18f),
                        )
                    }
                    tabCardLabels(tab = tab)
                        .take(2)
                        .forEach { label ->
                            TabCardChip(
                                label = label,
                                containerColor = metaChipColor,
                                contentColor = shellColors.textPrimary,
                            )
                        }
                }
            }

            Spacer(modifier = Modifier.height(11.dp))

            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Text(
                        text = host,
                        style = MaterialTheme.typography.bodySmall,
                        color = shellColors.textSecondary,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    if (details.isNotEmpty()) {
                        Text(
                            text = details.joinToString(" · "),
                            style = MaterialTheme.typography.labelSmall,
                            color = shellColors.textSecondary.copy(alpha = 0.88f),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }

                Surface(
                    shape = CircleShape,
                    color = shellColors.windowBackground.copy(alpha = 0.12f),
                ) {
                    IconButton(
                        onClick = onClose,
                        modifier = Modifier.size(28.dp),
                    ) {
                        Icon(
                            painter = painterResource(id = MahoIcon.Close.drawableRes),
                            contentDescription = "Close tab",
                            modifier = Modifier.size(14.dp),
                            tint = shellColors.textSecondary,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun TabCardChip(
    label: String,
    containerColor: Color,
    contentColor: Color,
) {
    Surface(
        shape = RoundedCornerShape(999.dp),
        color = containerColor,
        contentColor = contentColor,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
        )
    }
}

private fun tabCardLabels(
    tab: TabViewModel,
): List<String> = buildList {
    if (tab.isPinned) add("Pinned")
    if (tab.isPlayingAudio) add("Audio")
    if (tab.isMuted) add("Muted")
}

private fun buildTabStateDescription(
    tab: TabViewModel,
    isActive: Boolean,
    isIncognito: Boolean,
): String = buildString {
    append(if (isIncognito) "private tab" else "tab")
    append(", ")
    append(tab.title.ifBlank { "new tab" })
    if (isActive) append(", current")
    if (tab.isLoading) append(", loading")
    if (tab.isPinned) append(", pinned")
    if (tab.isFavorite) append(", saved")
    if (tab.isPlayingAudio) append(", playing audio")
    if (tab.isMuted) append(", muted")
    append(", state ${tab.lifecycleState}")
}

private fun displayHost(url: String): String {
    return runCatching {
        java.net.URI(url).host?.removePrefix("www.") ?: url
    }.getOrDefault(url)
}
