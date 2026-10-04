@file:OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)

package dev.maho.browser.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
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
import androidx.compose.foundation.layout.height
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
    onReaderSettingsClick: () -> Unit = {},
    onZoomIn: () -> Unit,
    onZoomOut: () -> Unit,
    onToggleDesktopMode: () -> Unit,
    onOpenAiChat: () -> Unit = {},
    onOpenConversations: () -> Unit = {},
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
        currentUrl.startsWith("https://") -> MahoIcon.InfoLock
        else -> MahoIcon.Globe
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

    if (isHomeMode) {
        val isDarkMode = isSystemInDarkTheme()
        val homeContentColor = if (isDarkMode) Color.White else Color.Black
        val homeBackgroundColor = if (isDarkMode) shellColors.homeBackgroundEnd else Color.White

        HomeModeBottomBar(
            tabCount = tabCount,
            contentColor = homeContentColor,
            badgeTextColor = if (isDarkMode) Color.Black else Color.White,
            backgroundColor = homeBackgroundColor,
            buttonColor = homeContentColor.copy(alpha = 0.035f),
            primaryButtonColor = if (isDarkMode) {
                shellColors.accent.copy(alpha = 0.14f)
            } else {
                Color(0xFFE5EEFB)
            },
            borderColor = homeContentColor.copy(alpha = 0.08f),
            onTabsClick = onTabsClick,
            onOpenAiChat = onOpenAiChat,
            onSearchClick = onSearchClick,
            onOpenSettings = onOpenSettings,
            modifier = modifier,
        )
        return
    }

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
                            isIncognito = isIncognito,
                            controlHighlightColor = controlHighlightColor,
                            contentColor = contentColor,
                            textSecondary = shellColors.textSecondary,
                            onClick = onSearchClick,
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
                                modifier = Modifier.padding(horizontal = 11.dp, vertical = 8.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(5.dp),
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
                                    fontWeight = FontWeight.SemiBold,
                                    color = contentColor,
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
                            ArcBottomBarSpacesSection(
                                spaces = spaces,
                                shellColors = shellColors,
                                onDismissMenu = { spacesExpanded = false },
                                onSelectSpace = onSelectSpace,
                            )
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
                                    isIncognito = isIncognito,
                                    controlHighlightColor = controlHighlightColor,
                                    contentColor = contentColor,
                                    textSecondary = shellColors.textSecondary,
                                    onClick = onSearchClick,
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
                                modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(7.dp),
                            ) {
                                Surface(
                                    shape = CircleShape,
                                    color = controlHighlightColor,
                                ) {
                                    Icon(
                                        painter = painterResource(id = pageGlyph.drawableRes),
                                        contentDescription = null,
                                        modifier = Modifier
                                            .padding(5.dp)
                                            .size(14.dp),
                                        tint = if (isIncognito) shellColors.incognitoAccent else contentColor,
                                    )
                                }

                                Column(
                                    modifier = Modifier.weight(1f),
                                    verticalArrangement = Arrangement.Center,
                                ) {
                                    if (currentTitle.isNotBlank() && currentUrl.isNotBlank() && pageContext != pageLabel) {
                                        Text(
                                            text = pageContext,
                                            style = MaterialTheme.typography.labelSmall,
                                            fontWeight = FontWeight.Medium,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis,
                                            color = shellColors.textSecondary,
                                        )
                                    }
                                    Text(
                                        text = pageLabel,
                                        style = MaterialTheme.typography.titleSmall,
                                        fontWeight = FontWeight.Medium,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                        color = contentColor,
                                    )
                                }

                                if (isLoading) {
                                    Text(
                                        text = "…",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = shellColors.textSecondary,
                                        modifier = Modifier.padding(end = 4.dp),
                                    )
                                }
                            }
                        }

                        BottomBarIconControl(
                            modifier = Modifier
                                .clickable(onClick = onGoForward)
                                .testTag("arcBottomBarForwardButton"),
                            color = controlColor,
                            icon = MahoIcon.NavForward,
                            contentDescription = "Forward",
                            tint = contentColor,
                        )
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
                                ArcBottomBarPageSection(
                                    shellColors = shellColors,
                                    onDismissMenu = { expanded = false },
                                    onBrowseForMe = onBrowseForMe,
                                    onFindInPage = onFindInPage,
                                    onShare = onShare,
                                    onReload = onReload,
                                )
                            }
                            ArcBottomBarBrowserSection(
                                isHomeMode = isHomeMode,
                                isReaderMode = isReaderMode,
                                isDesktopMode = isDesktopMode,
                                isIncognito = isIncognito,
                                zoomLabel = zoomLabel,
                                shellColors = shellColors,
                                onDismissMenu = { expanded = false },
                                onArchive = onArchive,
                                onToggleReaderMode = onToggleReaderMode,
                                onReaderSettingsClick = onReaderSettingsClick,
                                onToggleDesktopMode = onToggleDesktopMode,
                                onZoomIn = onZoomIn,
                                onZoomOut = onZoomOut,
                                onToggleIncognito = onToggleIncognito,
                                onOpenSettings = onOpenSettings,
                                onOpenConversations = onOpenConversations,
                                onPiP = onPiP,
                            )
                        }

                    }
                }
            }
        }
    }
}

@Composable
private fun HomeModeBottomBar(
    tabCount: Int,
    contentColor: Color,
    badgeTextColor: Color,
    backgroundColor: Color,
    buttonColor: Color,
    primaryButtonColor: Color,
    borderColor: Color,
    onTabsClick: () -> Unit,
    onOpenAiChat: () -> Unit,
    onSearchClick: () -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    BoxWithConstraints(
        modifier = modifier
            .fillMaxWidth()
            .navigationBarsPadding()
            .background(backgroundColor)
            .padding(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 6.dp)
            .testTag("arcBottomBarContainer"),
    ) {
        val rightGroupWidth = 44.dp + 12.dp + 44.dp
        val centerButtonWidth = minOf(112.dp, maxOf(80.dp, maxWidth - rightGroupWidth - rightGroupWidth))

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(44.dp),
        ) {
            Box(modifier = Modifier.align(Alignment.CenterStart)) {
                HomeTabsFooterButton(
                    tabCount = tabCount,
                    contentColor = contentColor,
                    badgeTextColor = badgeTextColor,
                    containerColor = buttonColor,
                    borderColor = borderColor,
                    onClick = onTabsClick,
                )
            }

            Box(modifier = Modifier.align(Alignment.Center)) {
                HomeNewTabFooterButton(
                    width = centerButtonWidth,
                    contentColor = contentColor,
                    containerColor = primaryButtonColor,
                    borderColor = borderColor,
                    onClick = onSearchClick,
                )
            }

            Row(
                modifier = Modifier.align(Alignment.CenterEnd),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                HomeFooterIconButton(
                    icon = MahoIcon.Bot,
                    contentDescription = "Agent",
                    testTag = "arcBottomBarAiButton",
                    shape = CircleShape,
                    contentColor = contentColor,
                    containerColor = buttonColor,
                    borderColor = borderColor,
                    onClick = onOpenAiChat,
                )

                HomeFooterIconButton(
                    icon = MahoIcon.Settings,
                    contentDescription = "Settings",
                    testTag = "arcBottomBarMoreButton",
                    shape = CircleShape,
                    contentColor = contentColor,
                    containerColor = buttonColor,
                    borderColor = borderColor,
                    onClick = onOpenSettings,
                )
            }
        }
    }
}

@Composable
private fun HomeTabsFooterButton(
    tabCount: Int,
    contentColor: Color,
    badgeTextColor: Color,
    containerColor: Color,
    borderColor: Color,
    onClick: () -> Unit,
) {
    Surface(
        modifier = Modifier
            .size(44.dp)
            .semantics {
                contentDescription = "Tabs"
                stateDescription = "$tabCount tab${if (tabCount == 1) "" else "s"}"
            }
            .clickable(onClick = onClick)
            .testTag("arcBottomBarTabsButton"),
        shape = RoundedCornerShape(16.dp),
        color = containerColor,
        border = BorderStroke(1.dp, borderColor),
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
    ) {
        Box(
            modifier = Modifier.size(44.dp),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(id = MahoIcon.Copy.drawableRes),
                contentDescription = null,
                modifier = Modifier.size(19.dp),
                tint = contentColor,
            )
            if (tabCount > 0) {
                Surface(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .offset(x = 4.dp, y = (-4).dp),
                    shape = CircleShape,
                    color = contentColor,
                    tonalElevation = 0.dp,
                    shadowElevation = 0.dp,
                ) {
                    Text(
                        text = if (tabCount > 99) "99+" else tabCount.toString(),
                        style = MaterialTheme.typography.labelSmall,
                        color = badgeTextColor,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.padding(horizontal = 5.dp, vertical = 1.dp),
                    )
                }
            }
        }
    }
}

@Composable
private fun HomeFooterIconButton(
    icon: MahoIcon,
    contentDescription: String,
    testTag: String,
    shape: Shape,
    contentColor: Color,
    containerColor: Color,
    borderColor: Color,
    onClick: () -> Unit,
) {
    Surface(
        modifier = Modifier
            .size(44.dp)
            .semantics { this.contentDescription = contentDescription }
            .clickable(onClick = onClick)
            .testTag(testTag),
        shape = shape,
        color = containerColor,
        border = BorderStroke(1.dp, borderColor),
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
    ) {
        Box(
            modifier = Modifier.size(44.dp),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(id = icon.drawableRes),
                contentDescription = null,
                modifier = Modifier.size(19.dp),
                tint = contentColor,
            )
        }
    }
}

@Composable
private fun HomeNewTabFooterButton(
    width: androidx.compose.ui.unit.Dp = 112.dp,
    contentColor: Color,
    containerColor: Color,
    borderColor: Color,
    onClick: () -> Unit,
) {
    Surface(
        modifier = Modifier
            .width(width)
            .height(44.dp)
            .semantics { contentDescription = "New Tab" }
            .clickable(onClick = onClick)
            .testTag("arcBottomBarPageButton"),
        shape = RoundedCornerShape(18.dp),
        color = containerColor,
        border = BorderStroke(1.dp, borderColor),
        tonalElevation = 0.dp,
        shadowElevation = 0.dp,
    ) {
        Box(
            modifier = Modifier
                .width(width)
                .height(44.dp),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(id = MahoIcon.Add.drawableRes),
                contentDescription = null,
                modifier = Modifier.size(20.dp),
                tint = contentColor,
            )
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
internal fun ArcMenuSectionLabel(
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
internal fun ArcMenuDivider(
    shellColors: dev.maho.browser.ui.theme.BrowserShellColors,
) {
    HorizontalDivider(
        thickness = 1.dp,
        color = shellColors.divider.copy(alpha = 0.44f),
        modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp),
    )
}

@Composable
internal fun ArcMenuLabel(
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
internal fun ArcMenuStateLabel(
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
    isIncognito: Boolean,
    controlHighlightColor: Color,
    contentColor: Color,
    textSecondary: Color,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("homeSearchBar")
            .border(width = 1.dp, color = controlHighlightColor.copy(alpha = 0.42f), shape = RoundedCornerShape(18.dp))
            .clickable { onClick() }
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
            Text(
                text = "Search or enter URL",
                style = MaterialTheme.typography.bodyLarge,
                color = textSecondary,
                modifier = Modifier.testTag("homeSearchField")
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

private fun displayHost(url: String): String {
    return runCatching {
        java.net.URI(url).host?.removePrefix("www.") ?: url
    }.getOrDefault(url)
}
