package dev.maho.browser.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.support.PageSummarizer
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellTheme

@Composable
internal fun MainBrowserSummaryShell(
    state: PageSummarizer.State,
    pageTitle: String,
    pageUrl: String,
    isIncognito: Boolean,
    onDismiss: () -> Unit,
) {
    val shellColors = BrowserShellTheme.colors
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(shellColors.overlayBackground.copy(alpha = if (isIncognito) 0.78f else 0.64f))
            .semantics {
                stateDescription = if (state is PageSummarizer.State.Result) {
                    "summary shell visible"
                } else {
                    "page summary shell visible"
                }
            },
    ) {
        SummaryOverlay(
            state = state,
            pageTitle = pageTitle,
            pageUrl = pageUrl,
            isIncognito = isIncognito,
            onDismiss = onDismiss,
            modifier = Modifier.align(Alignment.TopCenter).padding(top = 14.dp),
        )
    }
}

/** WebView's net error text ("net::ERR_...") is not meant for people; explain the common cases. */
internal fun pageLoadErrorMessage(errorCode: Int, description: String): String = when (errorCode) {
    android.webkit.WebViewClient.ERROR_HOST_LOOKUP -> "The site's address couldn't be found. Check the address or your connection."
    android.webkit.WebViewClient.ERROR_CONNECT -> "Couldn't connect to the site. Check your internet connection."
    android.webkit.WebViewClient.ERROR_TIMEOUT -> "The site took too long to respond."
    android.webkit.WebViewClient.ERROR_FAILED_SSL_HANDSHAKE -> "A secure connection to the site couldn't be made."
    else -> description
}

@Composable
internal fun MainBrowserPageErrorView(
    description: String,
    failingUrl: String,
    onReload: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(shellColors.browsingBackground)
            .semantics { stateDescription = "page load error" },
        contentAlignment = Alignment.Center,
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.padding(24.dp),
        ) {
            Icon(
                painter = painterResource(id = MahoIcon.CircleAlert.drawableRes),
                contentDescription = null,
                tint = shellColors.textSecondary,
                modifier = Modifier.size(32.dp),
            )
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                text = "This page couldn't be loaded",
                style = MaterialTheme.typography.titleMedium,
                color = shellColors.textPrimary,
                textAlign = TextAlign.Center,
            )
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = description,
                style = MaterialTheme.typography.bodyMedium,
                color = shellColors.textSecondary,
                textAlign = TextAlign.Center,
            )
            if (failingUrl.isNotBlank()) {
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = failingUrl,
                    style = MaterialTheme.typography.bodySmall,
                    color = shellColors.textSecondary,
                    textAlign = TextAlign.Center,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Spacer(modifier = Modifier.height(16.dp))
            Button(onClick = onReload) {
                Text("Reload")
            }
        }
    }
}

@Composable
internal fun MainBrowserIncognitoBadge(modifier: Modifier = Modifier) {
    val shellColors = BrowserShellTheme.colors
    Surface(
        modifier = modifier.semantics { stateDescription = "incognito shell active" },
        color = shellColors.incognitoSurface.copy(alpha = 0.76f),
        contentColor = shellColors.textPrimary,
        shape = RoundedCornerShape(999.dp),
        tonalElevation = 1.dp,
        shadowElevation = 4.dp,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                painter = painterResource(id = MahoIcon.VisibilityOff.drawableRes),
                contentDescription = null,
                tint = shellColors.incognitoAccent,
                modifier = Modifier.size(15.dp),
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                text = "Private",
                style = MaterialTheme.typography.labelMedium,
                color = shellColors.textPrimary,
            )
        }
    }
}
