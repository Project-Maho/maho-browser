package dev.maho.browser.ui

import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.painterResource
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellColors

@Composable
internal fun ArcBottomBarPageSection(
    shellColors: BrowserShellColors,
    onDismissMenu: () -> Unit,
    onBrowseForMe: () -> Unit,
    onFindInPage: () -> Unit,
    onShare: () -> Unit,
    onReload: () -> Unit,
) {
    ArcMenuSectionLabel(
        label = "Page",
        shellColors = shellColors,
    )
    DropdownMenuItem(
        text = {
            ArcMenuLabel(
                title = "Browse for Me",
                subtitle = "Generate a quicker read of this page",
                shellColors = shellColors,
            )
        },
        leadingIcon = {
            Icon(painter = painterResource(id = MahoIcon.SparklesAi.drawableRes), contentDescription = null)
        },
        onClick = {
            onDismissMenu()
            onBrowseForMe()
        },
    )
    DropdownMenuItem(
        text = {
            ArcMenuLabel(
                title = "Find in page",
                subtitle = "Jump to a word or phrase",
                shellColors = shellColors,
            )
        },
        leadingIcon = {
            Icon(painter = painterResource(id = MahoIcon.Search.drawableRes), contentDescription = null)
        },
        onClick = {
            onDismissMenu()
            onFindInPage()
        },
    )
    DropdownMenuItem(
        text = {
            ArcMenuLabel(
                title = "Share",
                subtitle = "Send this page elsewhere",
                shellColors = shellColors,
            )
        },
        leadingIcon = {
            Icon(painter = painterResource(id = MahoIcon.Share.drawableRes), contentDescription = null)
        },
        onClick = {
            onDismissMenu()
            onShare()
        },
    )
    DropdownMenuItem(
        text = {
            ArcMenuLabel(
                title = "Reload",
                subtitle = "Refresh the current page",
                shellColors = shellColors,
            )
        },
        leadingIcon = {
            Icon(painter = painterResource(id = MahoIcon.ReloadSync.drawableRes), contentDescription = null)
        },
        onClick = {
            onDismissMenu()
            onReload()
        },
    )
    ArcMenuDivider(shellColors = shellColors)
}
