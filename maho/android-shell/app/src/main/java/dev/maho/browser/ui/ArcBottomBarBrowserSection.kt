package dev.maho.browser.ui

import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.painterResource
import dev.maho.browser.ui.icons.MahoIcon
import dev.maho.browser.ui.theme.BrowserShellColors

@Composable
internal fun ArcBottomBarBrowserSection(
    isHomeMode: Boolean,
    isReaderMode: Boolean,
    isDesktopMode: Boolean,
    isIncognito: Boolean,
    zoomLabel: String,
    shellColors: BrowserShellColors,
    onDismissMenu: () -> Unit,
    onArchive: () -> Unit,
    onToggleReaderMode: () -> Unit,
    onReaderSettingsClick: () -> Unit,
    onToggleDesktopMode: () -> Unit,
    onZoomIn: () -> Unit,
    onZoomOut: () -> Unit,
    onToggleIncognito: (Boolean) -> Unit,
    onOpenSettings: () -> Unit,
    onOpenConversations: () -> Unit,
    onPiP: () -> Unit,
) {
    ArcMenuSectionLabel(
        label = "Browser",
        shellColors = shellColors,
    )
    DropdownMenuItem(
        text = {
            ArcMenuLabel(
                title = "Archive",
                subtitle = "Reopen archived tabs from this space",
                shellColors = shellColors,
            )
        },
        leadingIcon = {
            Icon(painter = painterResource(id = MahoIcon.Archive.drawableRes), contentDescription = null)
        },
        onClick = {
            onDismissMenu()
            onArchive()
        },
    )
    if (!isHomeMode) {
        DropdownMenuItem(
            text = {
                ArcMenuLabel(
                    title = if (isReaderMode) "Hide Reader" else "Reader",
                    subtitle = if (isReaderMode) {
                        "Return to the original page"
                    } else {
                        "Simplify the page typography"
                    },
                    shellColors = shellColors,
                )
            },
            leadingIcon = {
                Icon(painter = painterResource(id = MahoIcon.Book.drawableRes), contentDescription = null)
            },
            trailingIcon = {
                ArcMenuStateLabel(
                    label = if (isReaderMode) "On" else "Off",
                    isEmphasized = isReaderMode,
                    shellColors = shellColors,
                    actionColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent,
                )
            },
            onClick = {
                onDismissMenu()
                onToggleReaderMode()
            },
        )
        if (isReaderMode) {
            DropdownMenuItem(
                text = {
                    ArcMenuLabel(
                        title = "Reader Settings",
                        subtitle = "Adjust font, size, and theme",
                        shellColors = shellColors,
                    )
                },
                leadingIcon = {
                    Icon(painter = painterResource(id = MahoIcon.Tune.drawableRes), contentDescription = null)
                },
                onClick = {
                    onDismissMenu()
                    onReaderSettingsClick()
                },
            )
        }
        DropdownMenuItem(
            text = {
                ArcMenuLabel(
                    title = if (isDesktopMode) "Mobile Site" else "Desktop Site",
                    subtitle = if (isDesktopMode) {
                        "Request the mobile version of sites"
                    } else {
                        "Request the desktop version of sites"
                    },
                    shellColors = shellColors,
                )
            },
            leadingIcon = {
                Icon(painter = painterResource(id = MahoIcon.Monitor.drawableRes), contentDescription = null)
            },
            trailingIcon = {
                ArcMenuStateLabel(
                    label = if (isDesktopMode) "On" else "Off",
                    isEmphasized = isDesktopMode,
                    shellColors = shellColors,
                    actionColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent,
                )
            },
            onClick = {
                onDismissMenu()
                onToggleDesktopMode()
            },
        )
        DropdownMenuItem(
            text = {
                ArcMenuLabel(
                    title = "Zoom In",
                    subtitle = "Increase page scale from $zoomLabel",
                    shellColors = shellColors,
                )
            },
            leadingIcon = {
                Icon(painter = painterResource(id = MahoIcon.ZoomIn.drawableRes), contentDescription = null)
            },
            onClick = {
                onDismissMenu()
                onZoomIn()
            },
        )
        DropdownMenuItem(
            text = {
                ArcMenuLabel(
                    title = "Zoom Out",
                    subtitle = "Reduce page scale from $zoomLabel",
                    shellColors = shellColors,
                )
            },
            leadingIcon = {
                Icon(painter = painterResource(id = MahoIcon.ZoomOut.drawableRes), contentDescription = null)
            },
            onClick = {
                onDismissMenu()
                onZoomOut()
            },
        )
    }
    DropdownMenuItem(
        text = {
            ArcMenuLabel(
                title = "Private",
                subtitle = "Browse without keeping local history",
                shellColors = shellColors,
            )
        },
        leadingIcon = {
            Icon(painter = painterResource(id = MahoIcon.VisibilityOff.drawableRes), contentDescription = null)
        },
        trailingIcon = {
            ArcMenuStateLabel(
                label = if (isIncognito) "On" else "Off",
                isEmphasized = isIncognito,
                shellColors = shellColors,
                actionColor = if (isIncognito) shellColors.incognitoAccent else shellColors.accent,
            )
        },
        onClick = {
            onDismissMenu()
            onToggleIncognito(!isIncognito)
        },
    )
    DropdownMenuItem(
        text = {
            ArcMenuLabel(
                title = "Conversations",
                subtitle = "View saved AI conversation history",
                shellColors = shellColors,
            )
        },
        leadingIcon = {
            Icon(painter = painterResource(id = MahoIcon.History.drawableRes), contentDescription = null)
        },
        onClick = {
            onDismissMenu()
            onOpenConversations()
        },
    )
    DropdownMenuItem(
        text = {
            ArcMenuLabel(
                title = "Settings",
                subtitle = "Preferences, spaces, and app controls",
                shellColors = shellColors,
            )
        },
        leadingIcon = {
            Icon(painter = painterResource(id = MahoIcon.Settings.drawableRes), contentDescription = null)
        },
        onClick = {
            onDismissMenu()
            onOpenSettings()
        },
    )
    if (!isHomeMode) {
        DropdownMenuItem(
            text = {
                ArcMenuLabel(
                    title = "Picture in picture",
                    subtitle = "Keep the page visible while multitasking",
                    shellColors = shellColors,
                )
            },
            leadingIcon = {
                Icon(painter = painterResource(id = MahoIcon.PictureInPicture.drawableRes), contentDescription = null)
            },
            onClick = {
                onDismissMenu()
                onPiP()
            },
        )
    }
}
