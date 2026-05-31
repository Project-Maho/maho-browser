package dev.maho.browser.ui.tab

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun TabGridToolbar(
    tabCount: Int,
    activeSpaceName: String,
    onNewTab: () -> Unit,
    onCloseAll: () -> Unit,
    onDone: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val toolbarSurfaceColor = shellColors.overlaySurface.copy(alpha = 0.74f)
    val secondaryActionColor = shellColors.textSecondary

    Surface(
        modifier = modifier
            .testTag("tabGridToolbar")
            .semantics {
                stateDescription = "new tab opens in $activeSpaceName"
            },
        shape = RoundedCornerShape(metrics.barCorner),
        color = toolbarSurfaceColor,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            TextButton(
                onClick = onNewTab,
                modifier = Modifier.testTag("tabGridNewTabButton"),
            ) {
                Icon(painter = painterResource(id = MahoIcon.Add.drawableRes),
                    contentDescription = "New tab in $activeSpaceName",
                    modifier = Modifier,
                )
                Spacer(modifier = Modifier.width(4.dp))
                Text("New")
            }

            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Text(
                    text = "Tab overview",
                    style = MaterialTheme.typography.labelSmall,
                    color = secondaryActionColor,
                )
                Surface(
                    shape = CircleShape,
                    color = shellColors.windowBackground.copy(alpha = 0.12f),
                ) {
                    Text(
                        text = "$tabCount tab${if (tabCount != 1) "s" else ""}",
                        style = MaterialTheme.typography.labelMedium,
                        color = shellColors.textPrimary,
                        modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                    )
                }
            }

            Row(verticalAlignment = Alignment.CenterVertically) {
                if (tabCount > 1) {
                    TextButton(onClick = onCloseAll) {
                        Icon(painter = painterResource(id = MahoIcon.Eraser.drawableRes),
                            contentDescription = "Close All",
                            modifier = Modifier,
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("Close all")
                    }
                }
                TextButton(onClick = onDone) {
                    Text("Done")
                }
            }
        }
    }
}
