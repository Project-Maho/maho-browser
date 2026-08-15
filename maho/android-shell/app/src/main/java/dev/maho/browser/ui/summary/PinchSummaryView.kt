package dev.maho.browser.ui.summary

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme

@Composable
fun PinchSummaryView(
    title: String,
    sentences: List<String>,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    isIncognito: Boolean = false,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val displayTitle = title.trim().ifEmpty { "This page" }
    val accentColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent
    val cardColor = if (isIncognito) shellColors.incognitoSurface else shellColors.tabCard

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.45f))
            .clickable(onClick = onDismiss),
        contentAlignment = Alignment.Center
    ) {
        // Card Container
        Card(
            shape = RoundedCornerShape(24.dp),
            colors = CardDefaults.cardColors(containerColor = cardColor),
            border = androidx.compose.foundation.BorderStroke(1.dp, shellColors.divider.copy(alpha = 0.24f)),
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp)
                .clickable(enabled = false) {}
                .testTag("pinchSummaryView")
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(20.dp)
            ) {
                // Header Row
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.Top,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    // Summary Icon
                    Box(
                        contentAlignment = Alignment.Center,
                        modifier = Modifier
                            .size(40.dp)
                            .clip(RoundedCornerShape(12.dp))
                            .background(accentColor.copy(alpha = 0.12f))
                    ) {
                        Icon(
                            painter = painterResource(id = MahoIcon.SparklesAi.drawableRes),
                            contentDescription = null,
                            tint = accentColor,
                            modifier = Modifier.size(20.dp)
                        )
                    }

                    VStack(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "PINCH SUMMARY",
                            style = MaterialTheme.typography.labelSmall.copy(
                                fontWeight = FontWeight.Bold,
                                letterSpacing = androidx.compose.ui.unit.TextUnit.Unspecified
                            ),
                            color = shellColors.textSecondary
                        )
                        Text(
                            text = displayTitle,
                            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                            color = shellColors.textPrimary,
                            maxLines = 3,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = "A focused read of the visible page.",
                            style = MaterialTheme.typography.bodySmall,
                            color = shellColors.textSecondary
                        )
                    }

                    // Close Button
                    IconButton(
                        onClick = onDismiss
                    ) {
                        Icon(
                            painter = painterResource(id = MahoIcon.Close.drawableRes),
                            contentDescription = "Close",
                            tint = shellColors.textSecondary,
                            modifier = Modifier.size(24.dp)
                        )
                    }
                }

                // Key Points Section
                Column(
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        text = "Key points",
                        style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                        color = shellColors.textPrimary
                    )

                    Column(
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        sentences.forEachIndexed { index, sentence ->
                            Row(
                                horizontalArrangement = Arrangement.spacedBy(12.dp),
                                verticalAlignment = Alignment.Top,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(vertical = 4.dp)
                            ) {
                                // Bullet Point Indicator
                                Box(
                                    contentAlignment = Alignment.Center,
                                    modifier = Modifier
                                        .padding(top = 4.dp)
                                        .size(20.dp)
                                        .clip(CircleShape)
                                        .background(if (index == 0) accentColor.copy(alpha = 0.16f) else shellColors.divider.copy(alpha = 0.42f))
                                ) {
                                    Box(
                                        modifier = Modifier
                                            .size(5.dp)
                                            .clip(CircleShape)
                                            .background(if (index == 0) accentColor else shellColors.textSecondary)
                                    )
                                }

                                Text(
                                    text = sentence,
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = shellColors.textPrimary,
                                    modifier = Modifier.weight(1f)
                                )
                            }

                            if (index < sentences.size - 1) {
                                HorizontalDivider(color = shellColors.divider.copy(alpha = 0.12f))
                            }
                        }
                    }
                }

                // Footer Row
                Row(
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    // Page context Tag
                    Box(
                        modifier = Modifier
                            .clip(CircleShape)
                            .background(shellColors.divider.copy(alpha = 0.42f))
                            .padding(horizontal = 12.dp, vertical = 6.dp)
                    ) {
                        Text(
                            text = "Page context",
                            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold),
                            color = shellColors.textSecondary
                        )
                    }

                    // Done Button
                    Button(
                        onClick = onDismiss,
                        shape = RoundedCornerShape(metrics.compactCorner),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = accentColor.copy(alpha = 0.12f),
                            contentColor = accentColor
                        ),
                        modifier = Modifier.testTag("pinchSummaryBackButton")
                    ) {
                        Text(
                            text = "Done",
                            style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium)
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun VStack(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(modifier = modifier, content = content)
}
