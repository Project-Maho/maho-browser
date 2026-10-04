package dev.maho.browser.ui.tab

import android.content.Intent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun TabPreview(
    tab: TabViewModel,
    onClose: () -> Unit,
    onBookmark: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val shellColors = BrowserShellTheme.colors

    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(16.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (tab.isLoading) {
                        CircularProgressIndicator(
                            strokeWidth = 2.dp,
                            modifier = Modifier
                                .padding(end = 8.dp)
                                .height(16.dp),
                        )
                    }
                    Text(
                        text = tab.title.ifEmpty { "New Tab" },
                        style = MaterialTheme.typography.titleMedium,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = tab.url.ifEmpty { "about:blank" },
                    style = MaterialTheme.typography.bodySmall,
                    color = shellColors.textSecondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }

            Row {
                IconButton(onClick = {
                    val sendIntent = Intent(Intent.ACTION_SEND).apply {
                        type = "text/plain"
                        putExtra(Intent.EXTRA_TEXT, tab.url)
                        putExtra(Intent.EXTRA_TITLE, tab.title)
                    }
                    context.startActivity(Intent.createChooser(sendIntent, null))
                }) {
                    Icon(painter = painterResource(id = MahoIcon.Share.drawableRes), contentDescription = "Share")
                }
                IconButton(onClick = onBookmark) {
                    Icon(painter = painterResource(id = MahoIcon.BookmarkBorder.drawableRes), contentDescription = "Bookmark")
                }
                IconButton(onClick = onClose) {
                    Icon(painter = painterResource(id = MahoIcon.Close.drawableRes), contentDescription = "Close")
                }
            }
        }
    }
}
