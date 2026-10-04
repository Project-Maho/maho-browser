package dev.maho.browser.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.support.PageSummarizer
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun SummaryOverlay(
    state: PageSummarizer.State,
    pageTitle: String,
    pageUrl: String,
    isIncognito: Boolean,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val phaseLabel = when (state) {
        is PageSummarizer.State.Loading -> "Preparing digest"
        is PageSummarizer.State.Result -> "Generated digest"
        is PageSummarizer.State.Error -> "Digest unavailable"
        PageSummarizer.State.Idle -> "Page digest"
    }
    val eyebrow = when (state) {
        is PageSummarizer.State.Loading -> "Summary in progress"
        is PageSummarizer.State.Result -> "Summary"
        is PageSummarizer.State.Error -> "Summary"
        PageSummarizer.State.Idle -> "Summary"
    }
    val headline = when (state) {
        is PageSummarizer.State.Loading -> "Reading this page"
        is PageSummarizer.State.Result -> "What matters here"
        is PageSummarizer.State.Error -> "This page couldn’t be summarized"
        PageSummarizer.State.Idle -> "Ready when you are"
    }
    val supportingLine = when {
        pageTitle.isNotBlank() -> pageTitle
        pageUrl.isNotBlank() -> displayHost(pageUrl)
        else -> "Current page"
    }
    val metadataLine = buildString {
        append(if (pageUrl.isNotBlank()) displayHost(pageUrl) else "Current page")
        if (isIncognito) {
            append(" · Private")
        }
    }
    val actionColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent
    val actionMutedColor = if (isIncognito) {
        shellColors.incognitoSurface.copy(alpha = 0.8f)
    } else {
        shellColors.overlaySurface.copy(alpha = 0.74f)
    }
    val panelColor = if (isIncognito) {
        shellColors.incognitoBackground.copy(alpha = 0.96f)
    } else {
        shellColors.overlaySurfaceHigh.copy(alpha = 0.96f)
    }
    val chipColor = if (isIncognito) {
        shellColors.incognitoSurface.copy(alpha = 0.86f)
    } else {
        shellColors.overlaySurface.copy(alpha = 0.8f)
    }
    val editorialBandColor = if (isIncognito) {
        shellColors.incognitoAccent.copy(alpha = 0.12f)
    } else {
        shellColors.accent.copy(alpha = 0.12f)
    }
    val panelBorderColor = if (isIncognito) {
        shellColors.incognitoAccent.copy(alpha = 0.18f)
    } else {
        shellColors.divider.copy(alpha = 0.46f)
    }
    val bulletSurfaceColor = if (isIncognito) {
        shellColors.incognitoSurface.copy(alpha = 0.32f)
    } else {
        panelColor.copy(alpha = 0.34f)
    }

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .border(1.dp, panelBorderColor, RoundedCornerShape(metrics.panelCorner))
            .testTag("summaryShellOverlay")
            .semantics {
                stateDescription = phaseLabel.lowercase()
            },
        shape = RoundedCornerShape(metrics.panelCorner),
        tonalElevation = 0.dp,
        shadowElevation = metrics.floatingShadow,
        color = panelColor,
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 18.dp, vertical = 18.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.Top,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Column(
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                    modifier = Modifier.weight(1f),
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Surface(
                            shape = RoundedCornerShape(999.dp),
                            color = chipColor,
                        ) {
                            Row(
                                modifier = Modifier
                                    .padding(horizontal = 10.dp, vertical = 7.dp)
                                    .testTag("summaryShellModeChip"),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                            ) {
                                Icon(
                                    painter = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                                    contentDescription = null,
                                    tint = actionColor,
                                    modifier = Modifier.size(16.dp),
                                )
                                Text(
                                    text = phaseLabel,
                                    style = MaterialTheme.typography.labelLarge,
                                    color = actionColor,
                                )
                            }
                        }

                        if (isIncognito) {
                            Surface(
                                shape = RoundedCornerShape(999.dp),
                                color = chipColor,
                            ) {
                                Row(
                                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                                ) {
                                    Icon(
                                        painter = painterResource(id = MahoIcon.VisibilityOff.drawableRes),
                                        contentDescription = null,
                                        tint = shellColors.incognitoAccent,
                                        modifier = Modifier.size(14.dp),
                                    )
                                    Text(
                                        text = "Private",
                                        style = MaterialTheme.typography.labelMedium,
                                        color = shellColors.textSecondary,
                                    )
                                }
                            }
                        }
                    }

                    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text(
                            text = eyebrow.uppercase(),
                            style = MaterialTheme.typography.labelSmall,
                            color = shellColors.textSecondary,
                            fontWeight = FontWeight.Medium,
                        )
                        Text(
                            text = headline,
                            style = MaterialTheme.typography.headlineSmall,
                            color = shellColors.textPrimary,
                        )
                        Text(
                            text = supportingLine,
                            style = MaterialTheme.typography.bodyMedium,
                            color = shellColors.textPrimary.copy(alpha = 0.82f),
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Text(
                            text = metadataLine,
                            style = MaterialTheme.typography.bodySmall,
                            color = shellColors.textSecondary,
                        )
                    }
                }

                Surface(
                    shape = CircleShape,
                    color = chipColor,
                ) {
                    IconButton(onClick = onDismiss) {
                        Icon(
                            painter = painterResource(id = MahoIcon.Close.drawableRes),
                            contentDescription = "Close",
                            tint = shellColors.textSecondary,
                        )
                    }
                }
            }

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(
                        color = actionMutedColor,
                        shape = RoundedCornerShape(metrics.cardCorner),
                    )
                    .padding(horizontal = 14.dp, vertical = 14.dp)
                    .testTag("summaryShellPhase"),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Box(
                        modifier = Modifier
                            .size(10.dp)
                            .background(actionColor, CircleShape),
                    )
                    Text(
                        text = phaseLabel,
                        style = MaterialTheme.typography.labelLarge,
                        color = actionColor,
                    )
                }

                HorizontalDivider(
                    thickness = 1.dp,
                    color = shellColors.divider.copy(alpha = 0.34f),
                )

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(
                            color = editorialBandColor,
                            shape = RoundedCornerShape(metrics.compactCorner),
                        )
                        .padding(horizontal = 12.dp, vertical = 10.dp),
                ) {
                    when (state) {
                        is PageSummarizer.State.Idle -> {
                            Text(
                                text = "Generate a tighter read on the current page, with the main ideas pulled forward.",
                                style = MaterialTheme.typography.bodyMedium,
                                color = shellColors.textSecondary,
                            )
                        }

                        is PageSummarizer.State.Loading -> {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                CircularProgressIndicator(
                                    modifier = Modifier.size(18.dp),
                                    strokeWidth = 2.dp,
                                    color = actionColor,
                                    trackColor = actionColor.copy(alpha = 0.18f),
                                )
                                Spacer(modifier = Modifier.width(10.dp))
                                Text(
                                    text = "Pulling out the main points from this page.",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = shellColors.textSecondary,
                                )
                            }
                        }

                        is PageSummarizer.State.Result -> {
                            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                state.bullets.forEachIndexed { index, bullet ->
                                    Surface(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .testTag("summaryBullet_$index"),
                                        shape = RoundedCornerShape(metrics.compactCorner),
                                        color = bulletSurfaceColor,
                                    ) {
                                        Row(
                                            modifier = Modifier.padding(horizontal = 12.dp, vertical = 11.dp),
                                            verticalAlignment = Alignment.Top,
                                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                                        ) {
                                            Surface(
                                                modifier = Modifier.padding(top = 2.dp),
                                                shape = CircleShape,
                                                color = actionColor.copy(alpha = 0.14f),
                                            ) {
                                                Text(
                                                    text = "${index + 1}",
                                                    style = MaterialTheme.typography.labelMedium,
                                                    color = actionColor,
                                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 5.dp),
                                                )
                                            }
                                            Text(
                                                text = bullet,
                                                style = MaterialTheme.typography.bodyMedium,
                                                color = shellColors.textPrimary,
                                                modifier = Modifier.weight(1f),
                                            )
                                        }
                                    }
                                }
                            }
                        }

                        is PageSummarizer.State.Error -> {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                            ) {
                                Icon(
                                    painter = painterResource(id = MahoIcon.Warning.drawableRes),
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.error,
                                    modifier = Modifier.size(18.dp),
                                )
                                Text(
                                    text = state.message,
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = shellColors.textSecondary,
                                )
                            }
                        }
                    }
                }
            }

            Button(
                onClick = onDismiss,
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(metrics.compactCorner),
                colors = ButtonDefaults.buttonColors(
                    containerColor = editorialBandColor,
                    contentColor = actionColor,
                ),
            ) {
                Text(
                    text = if (state is PageSummarizer.State.Result) "Done" else "Close",
                )
            }
        }
    }
}

private fun displayHost(url: String): String {
    return runCatching {
        java.net.URI(url).host?.removePrefix("www.") ?: url
    }.getOrDefault(url)
}
