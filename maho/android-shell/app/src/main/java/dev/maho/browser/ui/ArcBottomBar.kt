@file:OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)

package dev.maho.browser.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.theme.bottomBarContainerColor
import kotlin.math.roundToInt
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

@Composable
fun ArcBottomBar(
    tabCount: Int,
    spaces: List<SpaceViewModel>,
    isIncognito: Boolean,
    isHomeMode: Boolean,
    homeQuery: String,
    isReaderMode: Boolean,
    isDesktopMode: Boolean,
    currentTitle: String,
    currentUrl: String,
    isLoading: Boolean,
    zoomLevel: Float,
    tintColor: Color,
    onHomeQueryChange: (String) -> Unit,
    onHomeSubmit: () -> Unit,
    onTabsClick: () -> Unit,
    onSelectSpace: (String) -> Unit,
    onSearchClick: () -> Unit,
    onGoBack: () -> Unit,
    onGoForward: () -> Unit,
    onPreviousTab: () -> Unit,
    onNextTab: () -> Unit,
    onToggleIncognito: (Boolean) -> Unit,
    onFindInPage: () -> Unit,
    onArchive: () -> Unit,
    onOpenSettings: () -> Unit,
    onShare: () -> Unit,
    onReload: () -> Unit,
    onToggleReaderMode: () -> Unit,
    onZoomIn: () -> Unit,
    onZoomOut: () -> Unit,
    onToggleDesktopMode: () -> Unit,
    onPiP: () -> Unit = {},
    onBrowseForMe: () -> Unit = {},
    canSwipeToPreviousTab: Boolean = false,
    canSwipeToNextTab: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val shellColors = BrowserShellTheme.colors
    val metrics = BrowserShellTheme.metrics
    val keyboardController = LocalSoftwareKeyboardController.current
    var expanded by remember { mutableStateOf(false) }
    var spacesExpanded by remember { mutableStateOf(false) }
    val containerColor = shellColors.bottomBarContainerColor(
        tintColor = tintColor,
        isIncognito = isIncognito,
    ).copy(alpha = if (isIncognito) 0.96f else 0.9f)
    val contentColor = shellColors.textPrimary
    val controlColor = if (isIncognito) {
        shellColors.incognitoSurface.copy(alpha = 0.7f)
    } else {
        shellColors.overlaySurfaceHigh.copy(alpha = 0.68f)
    }
    val controlHighlightColor = if (isIncognito) {
        shellColors.incognitoAccent.copy(alpha = 0.22f)
    } else {
        shellColors.accent.copy(alpha = 0.18f)
    }
    val chromeOutlineColor = if (isIncognito) {
        shellColors.incognitoAccent.copy(alpha = 0.2f)
    } else {
        shellColors.divider.copy(alpha = 0.46f)
    }
    val menuContainerColor = if (isIncognito) {
        shellColors.incognitoBackground.copy(alpha = 0.98f)
    } else {
        shellColors.overlaySurfaceHigh.copy(alpha = 0.98f)
    }
    val menuBorderColor = if (isIncognito) {
        shellColors.incognitoAccent.copy(alpha = 0.24f)
    } else {
        shellColors.divider.copy(alpha = 0.5f)
    }
    val zoomLabel = "${(zoomLevel * 100).roundToInt()}%"
    val pageLabel = currentTitle.ifBlank {
        if (currentUrl.isBlank()) "Search or enter URL" else displayHost(currentUrl)
    }
    val pageContext = when {
        isLoading -> "Loading"
        isIncognito -> "Private"
        currentUrl.isBlank() -> "Search"
        else -> displayHost(currentUrl)
    }
    val pageGlyph = when {
        isIncognito -> MahoIcon.VisibilityOff
        currentUrl.isBlank() -> MahoIcon.Search
        else -> MahoIcon.Tab
    }
    val tabGestureDescription = buildString {
        append("$tabCount tab")
        if (tabCount != 1) append("s")
        when {
            canSwipeToPreviousTab && canSwipeToNextTab -> append(", swipe left or right to switch tabs")
            canSwipeToPreviousTab -> append(", swipe right for the previous tab")
            canSwipeToNextTab -> append(", swipe left for the next tab")
        }
    }
    val canSubmitHomeQuery = homeQuery.trim().isNotEmpty()

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .imePadding()
            .navigationBarsPadding()
            .padding(horizontal = 14.dp, vertical = 8.dp)
            .border(
                width = 1.dp,
                color = chromeOutlineColor,
                shape = RoundedCornerShape(metrics.barCorner),
            )
            .testTag("arcBottomBarContainer"),
        shape = RoundedCornerShape(metrics.barCorner),
        color = containerColor,
        contentColor = contentColor,
        tonalElevation = 0.dp,
        shadowElevation = 6.dp,
    ) {
        BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
            val compactHomeLayout = isHomeMode && maxWidth < 360.dp

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = 56.dp)
                    .padding(horizontal = 6.dp, vertical = 6.dp),
                verticalArrangement = Arrangement.spacedBy(if (compactHomeLayout) 8.dp else 6.dp),
            ) {
                if (compactHomeLayout) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .semantics {
                                stateDescription = if (isIncognito) "private search" else "search"
                            }
                            .testTag("arcBottomBarPageButton"),
                        color = controlColor,
                        shape = RoundedCornerShape(22.dp),
                    ) {
                        HomeSearchField(
                            homeQuery = homeQuery,
                            isIncognito = isIncognito,
                            controlHighlightColor = controlHighlightColor,
                            contentColor = contentColor,
                            textSecondary = shellColors.textSecondary,
                            canSubmitHomeQuery = canSubmitHomeQuery,
                            onHomeQueryChange = onHomeQueryChange,
                            onHomeSubmit = {
                                keyboardController?.hide()
                                onHomeSubmit()
                            },
                        )
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Box {
                        Surface(
                            modifier = Modifier
                                .semantics {
                                    stateDescription = tabGestureDescription
                                    contentDescription = "Tabs"
                                }
                                .pointerInput(canSwipeToPreviousTab, canSwipeToNextTab) {
                                    var accumulatedDrag = 0f
                                    detectHorizontalDragGestures(
                                        onHorizontalDrag = { change, dragAmount ->
                                            change.consume()
                                            accumulatedDrag += dragAmount
                                        },
                                        onDragCancel = {
                                            accumulatedDrag = 0f
                                        },
                                        onDragEnd = {
                                            when {
                                                accumulatedDrag >= 48f && canSwipeToPreviousTab -> onPreviousTab()
                                                accumulatedDrag <= -48f && canSwipeToNextTab -> onNextTab()
                                            }
                                            accumulatedDrag = 0f
                                        },
                                    )
                                }
                                .combinedClickable(
                                    onClick = onTabsClick,
                                    onLongClick = {
                                        if (spaces.isNotEmpty()) {
                                            spacesExpanded = true
                                        }
                                    },
                                )
                                .testTag("arcBottomBarTabsButton"),
                            shape = RoundedCornerShape(18.dp),
                            color = controlColor,
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 9.dp, vertical = 7.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                            ) {
                                SwipeCue(
                                    icon = MahoIcon.NavBack,
                                    enabled = canSwipeToPreviousTab,
                                    tint = contentColor,
                                )

                                Surface(
                                    shape = RoundedCornerShape(999.dp),
                                    color = controlHighlightColor,
                                ) {
                                    Row(
                                        modifier = Modifier.padding(horizontal = 9.dp, vertical = 5.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                                    ) {
                                        Icon(
                                            painter = painterResource(id = MahoIcon.Tab.drawableRes),
                                            contentDescription = "Tabs",
                                            modifier = Modifier.size(16.dp),
                                            tint = if (isIncognito) shellColors.incognitoAccent else contentColor,
                                        )
                                        Text(
                                            text = tabCount.toString(),
                                            style = MaterialTheme.typography.labelLarge,
                                            color = contentColor,
                                        )
                                    }
                                }

                                SwipeCue(
                                    icon = MahoIcon.NavForward,
                                    enabled = canSwipeToNextTab,
                                    tint = contentColor,
                                )
                            }
                        }

                        ArcDropdownMenu(
                            expanded = spacesExpanded,
                            onDismissRequest = { spacesExpanded = false },
                            metricsCorner = metrics.panelCorner,
                            containerColor = menuContainerColor,
                            borderColor = menuBorderColor,
                        ) {
                            ArcMenuSectionLabel(
                                label = "Spaces",
                                shellColors = shellColors,
                            )
                            spaces.forEach { space ->
                                DropdownMenuItem(
                                    text = {
                                        ArcMenuLabel(
                                            title = space.name,
                                            subtitle = "${space.tabCount} tab${if (space.tabCount == 1) "" else "s"}",
                                            shellColors = shellColors,
                                        )
                                    },
                                    onClick = {
                                        spacesExpanded = false
                                        onSelectSpace(space.id)
                                    },
                                )
                            }
                        }
                    }

                    if (isHomeMode) {
                        if (!compactHomeLayout) {
                            Surface(
                                modifier = Modifier
                                    .weight(1f)
                                    .semantics {
                                        stateDescription = if (isIncognito) "private search" else "search"
                                    }
                                    .testTag("arcBottomBarPageButton"),
                                color = controlColor,
                                shape = RoundedCornerShape(22.dp),
                            ) {
                                HomeSearchField(
                                    homeQuery = homeQuery,
                                    isIncognito = isIncognito,
                                    controlHighlightColor = controlHighlightColor,
                                    contentColor = contentColor,
                                    textSecondary = shellColors.textSecondary,
                                    canSubmitHomeQuery = canSubmitHomeQuery,
                                    onHomeQueryChange = onHomeQueryChange,
                                    onHomeSubmit = {
                                        keyboardController?.hide()
                                        onHomeSubmit()
                                    },
                                )
                            }
                        }
                    } else {
                        BottomBarIconControl(
                            modifier = Modifier
                                .clickable(onClick = onGoBack)
                                .testTag("arcBottomBarBackButton"),
                            color = controlColor,
                            icon = MahoIcon.NavBack,
                            contentDescription = "Back",
                            tint = contentColor,
                        )

                        BottomBarIconControl(
                            modifier = Modifier
                                .clickable(onClick = onGoForward)
                                .testTag("arcBottomBarForwardButton"),
                            color = controlColor,
                            icon = MahoIcon.NavForward,
                            contentDescription = "Forward",
                            tint = contentColor,
                        )

                        Surface(
                            modifier = Modifier
                                .weight(1f)
                                .semantics {
                                    stateDescription = pageContext
                                }
                                .combinedClickable(onClick = onSearchClick)
                                .testTag("arcBottomBarPageButton"),
                            color = controlColor,
                            shape = RoundedCornerShape(22.dp),
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 9.dp, vertical = 8.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(9.dp),
                            ) {
                                Surface(
                                    shape = CircleShape,
                                    color = controlHighlightColor,
                                ) {
                                    Icon(
                                        painter = painterResource(id = pageGlyph.drawableRes),
                                        contentDescription = null,
                                        modifier = Modifier
                                            .padding(7.dp)
                                            .size(15.dp),
                                        tint = if (isIncognito) shellColors.incognitoAccent else contentColor,
                                    )
                                }

                                Column(
                                    modifier = Modifier.weight(1f),
                                    verticalArrangement = Arrangement.spacedBy(2.dp),
                                ) {
                                    Text(
                                        text = pageContext,
                                        style = MaterialTheme.typography.labelSmall,
                                        fontWeight = FontWeight.Medium,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                        color = shellColors.textSecondary,
                                    )
                                    Text(
                                        text = pageLabel,
                                        style = MaterialTheme.typography.titleSmall,
                                        fontWeight = FontWeight.Medium,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                        color = contentColor,
                                    )
                                }

                                Spacer(modifier = Modifier.width(2.dp))
                                Surface(
                                    shape = RoundedCornerShape(999.dp),
                                    color = controlHighlightColor,
                                ) {
                                    Text(
                                        text = if (isLoading) "…" else "Open",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = shellColors.textSecondary.copy(alpha = 0.9f),
                                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 5.dp),
                                    )
                                }
                            }
                        }
                    }

                    Box {
                        Surface(
                            modifier = Modifier
                                .combinedClickable(
                                    onClick = {
                                        expanded = true
                                    },
                                )
                                .semantics {
                                    contentDescription = "More"
                                }
                                .testTag("arcBottomBarMoreButton"),
                            shape = RoundedCornerShape(18.dp),
                            color = controlColor,
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 11.dp, vertical = 9.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(5.dp),
                            ) {
                                repeat(3) {
                                    Box(
                                        modifier = Modifier
                                            .size(3.dp)
                                            .clip(CircleShape)
                                            .background(contentColor.copy(alpha = 0.78f)),
                                    )
                                }
                            }
                        }

                        ArcDropdownMenu(
                            expanded = expanded,
                            onDismissRequest = { expanded = false },
                            metricsCorner = metrics.panelCorner,
                            containerColor = menuContainerColor,
                            borderColor = menuBorderColor,
                        ) {
                            if (!isHomeMode) {
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
                                        expanded = false
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
                                        expanded = false
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
                                        expanded = false
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
                                        expanded = false
                                        onReload()
                                    },
                                )
                                ArcMenuDivider(shellColors = shellColors)
                            }
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
                                    expanded = false
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
                                        expanded = false
                                        onToggleReaderMode()
                                    },
                                )
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
                                        expanded = false
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
                                        expanded = false
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
                                        expanded = false
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
                                    expanded = false
                                    onToggleIncognito(!isIncognito)
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
                                    expanded = false
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
                                        expanded = false
                                        onPiP()
                                    },
                                )
                            }
                        }

                    }
                }
            }
        }
    }
}

@Composable
private fun ArcDropdownMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    metricsCorner: androidx.compose.ui.unit.Dp,
    containerColor: Color,
    borderColor: Color,
    content: @Composable ColumnScope.() -> Unit,
) {
    val menuShape = RoundedCornerShape(metricsCorner)
    DropdownMenu(
        expanded = expanded,
        onDismissRequest = onDismissRequest,
        modifier = Modifier
            .clip(menuShape)
            .background(containerColor, menuShape)
            .border(BorderStroke(1.dp, borderColor), menuShape),
        content = content,
    )
}

@Composable
private fun ArcMenuSectionLabel(
    label: String,
    shellColors: dev.maho.browser.ui.theme.BrowserShellColors,
) {
    Text(
        text = label.uppercase(),
        style = MaterialTheme.typography.labelSmall,
        color = shellColors.textSecondary.copy(alpha = 0.88f),
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
    )
}

@Composable
private fun ArcMenuDivider(
    shellColors: dev.maho.browser.ui.theme.BrowserShellColors,
) {
    HorizontalDivider(
        thickness = 1.dp,
        color = shellColors.divider.copy(alpha = 0.44f),
        modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp),
    )
}

@Composable
private fun ArcMenuLabel(
    title: String,
    subtitle: String,
    shellColors: dev.maho.browser.ui.theme.BrowserShellColors,
) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.bodyLarge,
            color = shellColors.textPrimary,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = subtitle,
            style = MaterialTheme.typography.labelSmall,
            color = shellColors.textSecondary,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun ArcMenuStateLabel(
    label: String,
    isEmphasized: Boolean,
    shellColors: dev.maho.browser.ui.theme.BrowserShellColors,
    actionColor: Color,
) {
    Text(
        text = label,
        style = MaterialTheme.typography.labelMedium,
        color = if (isEmphasized) actionColor else shellColors.textSecondary,
    )
}

@Composable
private fun HomeSearchField(
    homeQuery: String,
    isIncognito: Boolean,
    controlHighlightColor: Color,
    contentColor: Color,
    textSecondary: Color,
    canSubmitHomeQuery: Boolean,
    onHomeQueryChange: (String) -> Unit,
    onHomeSubmit: () -> Unit,
) {
    var fieldValue by remember(homeQuery) {
        mutableStateOf(
            TextFieldValue(
                text = homeQuery,
                selection = androidx.compose.ui.text.TextRange(homeQuery.length),
            ),
        )
    }

    if (fieldValue.text != homeQuery) {
        fieldValue = fieldValue.copy(
            text = homeQuery,
            selection = androidx.compose.ui.text.TextRange(homeQuery.length),
        )
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("homeSearchBar")
            .border(width = 1.dp, color = controlHighlightColor.copy(alpha = 0.42f), shape = RoundedCornerShape(18.dp))
            .padding(horizontal = 10.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Surface(
            shape = CircleShape,
            color = controlHighlightColor,
        ) {
            Icon(
                painter = painterResource(id = if (isIncognito) MahoIcon.VisibilityOff.drawableRes else MahoIcon.Search.drawableRes),
                contentDescription = null,
                tint = if (isIncognito) contentColor else textSecondary,
                modifier = Modifier
                    .padding(7.dp)
                    .size(15.dp),
            )
        }

        Box(
            modifier = Modifier.weight(1f),
            contentAlignment = Alignment.CenterStart,
        ) {
            if (fieldValue.text.isEmpty()) {
                Text(
                    text = "Search or enter URL",
                    style = MaterialTheme.typography.bodyLarge,
                    color = textSecondary,
                )
            }

            BasicTextField(
                value = fieldValue,
                onValueChange = { updated ->
                    fieldValue = updated
                    onHomeQueryChange(updated.text)
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag("homeSearchField")
                    .semantics {
                        contentDescription = "Search or enter URL"
                    },
                singleLine = true,
                textStyle = MaterialTheme.typography.bodyLarge.copy(color = contentColor),
                keyboardOptions = KeyboardOptions(
                    keyboardType = KeyboardType.Uri,
                    imeAction = ImeAction.Search,
                ),
                keyboardActions = KeyboardActions(
                    onSearch = {
                        if (canSubmitHomeQuery) {
                            onHomeSubmit()
                        }
                    },
                ),
                cursorBrush = androidx.compose.ui.graphics.SolidColor(if (isIncognito) contentColor else textSecondary),
            )
        }

        if (fieldValue.text.isNotEmpty()) {
            Surface(
                modifier = Modifier
                    .testTag("homeSearchClearButton")
                    .clickable {
                        fieldValue = TextFieldValue("")
                        onHomeQueryChange("")
                    }
                    .semantics {
                        contentDescription = "Clear search"
                    },
                shape = CircleShape,
                color = controlHighlightColor,
            ) {
                Icon(
                    painter = painterResource(id = MahoIcon.Close.drawableRes),
                    contentDescription = null,
                    tint = textSecondary,
                    modifier = Modifier
                        .padding(9.dp)
                        .size(16.dp),
                )
            }
        }

        Surface(
            modifier = Modifier
                .testTag("homeSearchSubmitButton")
                .clickable(enabled = canSubmitHomeQuery, onClick = onHomeSubmit)
                .semantics {
                    contentDescription = "Submit search"
                },
            shape = CircleShape,
            color = if (canSubmitHomeQuery) controlHighlightColor else controlHighlightColor.copy(alpha = 0.42f),
        ) {
            Icon(
                painter = painterResource(id = MahoIcon.NavForward.drawableRes),
                contentDescription = null,
                tint = if (canSubmitHomeQuery) contentColor else textSecondary.copy(alpha = 0.52f),
                modifier = Modifier
                    .padding(9.dp)
                    .size(16.dp),
            )
        }
    }
}

@Composable
private fun BottomBarIconControl(
    modifier: Modifier,
    color: Color,
    icon: MahoIcon,
    contentDescription: String,
    tint: Color,
    shape: Shape = RoundedCornerShape(18.dp),
) {
    Surface(
        modifier = modifier,
        color = color,
        shape = shape,
    ) {
        Box(
            modifier = Modifier.padding(9.dp),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(id = icon.drawableRes),
                contentDescription = contentDescription,
                modifier = Modifier.size(17.dp),
                tint = tint,
            )
        }
    }
}

@Composable
private fun SwipeCue(
    icon: MahoIcon,
    enabled: Boolean,
    tint: Color,
) {
    Icon(
        painter = painterResource(id = icon.drawableRes),
        contentDescription = null,
        modifier = Modifier.size(14.dp),
        tint = if (enabled) tint.copy(alpha = 0.72f) else tint.copy(alpha = 0.22f),
    )
}

private fun displayHost(url: String): String {
    return runCatching {
        java.net.URI(url).host?.removePrefix("www.") ?: url
    }.getOrDefault(url)
}
