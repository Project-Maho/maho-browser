package dev.maho.browser.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.webkit.CookieManager
import android.webkit.WebStorage
import android.webkit.WebView
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import androidx.core.view.WindowCompat
import dev.maho.browser.MahoBridge
import dev.maho.browser.bridge.BridgeCommandBar
import dev.maho.browser.bridge.BridgeNavigation
import dev.maho.browser.bridge.BridgeSpaces
import dev.maho.browser.bridge.BridgeTabs
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.SpaceColor
import dev.maho.browser.models.SpaceId
import dev.maho.browser.models.SpaceViewModel
import dev.maho.browser.models.SuggestionType
import dev.maho.browser.models.SuggestionViewModel
import dev.maho.browser.models.TabId
import dev.maho.browser.models.TabViewModel
import dev.maho.browser.ui.find.FindInPageSheet
import dev.maho.browser.ui.summary.PinchSummaryView
import dev.maho.browser.ui.webview.AgenticBrowsingWebViewRegistry
import dev.maho.browser.ui.theme.BrowserShellTheme
import dev.maho.browser.ui.theme.resolveBrowserChromeTheme

@Composable
fun MainBrowserScreen(externalUrl: String? = null) {
    val shellColors = BrowserShellTheme.colors
    val bridge = MahoBridge
    val context = LocalContext.current
    val view = LocalView.current
    val isSystemDarkTheme = isSystemInDarkTheme()
    var tabs by remember { mutableStateOf<List<TabViewModel>>(emptyList()) }
    var spaces by remember { mutableStateOf<List<SpaceViewModel>>(emptyList()) }
    var activeSpaceId by remember { mutableStateOf<SpaceId?>(null) }
    var activeTabId by remember { mutableStateOf<String?>(null) }
    var shellRoute by rememberSaveable { mutableStateOf(ShellRoute.Home) }
    var homeReturnTabId by rememberSaveable { mutableStateOf<TabId?>(null) }
    var showTabGrid by rememberSaveable { mutableStateOf(false) }
    fun captureActiveTabPreview() {
        val webView = AgenticBrowsingWebViewRegistry.current() ?: return
        activeTabId?.let { captureTabPreview(webView, it) }
    }
    var showFindSheet by rememberSaveable { mutableStateOf(false) }
    var showSearchSheet by rememberSaveable { mutableStateOf(false) }
    var showPinchSummary by rememberSaveable { mutableStateOf(false) }
    var pinchSummarySentences by remember { mutableStateOf<List<String>>(emptyList()) }
    var overlayStack by remember { mutableStateOf(emptyList<BrowserOverlay>()) }
    var isIncognito by rememberSaveable { mutableStateOf(false) }
    var currentUrl by remember { mutableStateOf("") }
    var currentTitle by remember { mutableStateOf("") }
    var isLoading by remember { mutableStateOf(false) }
    var homeQuery by rememberSaveable { mutableStateOf("") }
    var pendingHomeNavigationUrl by rememberSaveable { mutableStateOf<String?>(null) }
    var requestedNavigationUrl by rememberSaveable { mutableStateOf<String?>(null) }
    var pendingBrowsingTabId by rememberSaveable { mutableStateOf<TabId?>(null) }
    var webAgentInitialGoal by rememberSaveable { mutableStateOf<String?>(null) }

    var loadProgress by remember { mutableStateOf(0) }
    var webViewRef by remember { mutableStateOf<WebView?>(null) }
    var bottomBarVisible by rememberSaveable { mutableStateOf(true) }
    var isReaderMode by rememberSaveable { mutableStateOf(false) }
    var isDesktopMode by rememberSaveable { mutableStateOf(false) }
    var zoomLevel by rememberSaveable { mutableStateOf(1.0f) }
    var predictiveBackProgress by remember { mutableStateOf(0f) }
    var summaryState by remember { mutableStateOf<dev.maho.browser.support.PageSummarizer.State>(dev.maho.browser.support.PageSummarizer.State.Idle) }
    var showSummaryOverlay by rememberSaveable { mutableStateOf(false) }
    var chromeScrollAccumulator by remember { mutableStateOf(0f) }
    var chromeRevealLockUntilMs by remember { mutableStateOf(0L) }
    var prevIncognitoTabsCount by remember { mutableStateOf(0) }
    var urlBeforeReaderMode by rememberSaveable { mutableStateOf<String?>(null) }
    var showReaderSettingsSheet by rememberSaveable { mutableStateOf(false) }

    val browserPrefs = remember(context) {
        context.getSharedPreferences("maho_browser_ui_prefs", Context.MODE_PRIVATE)
    }
    val selectedSearchEngine = remember {
        mutableStateOf(browserPrefs.getString("search_engine", "Google") ?: "Google")
    }

    fun refreshBrowserState() {
        val latestTabs = BridgeTabs.getTabViewModels()
        val latestSpaces = BridgeSpaces.getSpaceViewModels()
        val bridgeActiveSpaceId = bridge.getActiveSpaceId() ?: BridgeSpaces.getActiveSpaceId()
        val resolvedSpaceId = when {
            bridgeActiveSpaceId != null -> bridgeActiveSpaceId
            activeSpaceId != null && latestSpaces.any { it.id == activeSpaceId } -> activeSpaceId
            latestSpaces.any { it.isActive } -> latestSpaces.first { it.isActive }.id
            latestTabs.isNotEmpty() -> latestTabs.first().spaceId
            else -> null
        }
        val bridgeActiveTabId = BridgeTabs.getActiveTabId()
        val resolvedTabId = when {
            bridgeActiveTabId != null -> bridgeActiveTabId
            activeTabId != null && latestTabs.any { it.id == activeTabId } -> activeTabId
            resolvedSpaceId != null -> latestTabs
                .filter { it.spaceId == resolvedSpaceId }
                .maxByOrNull { it.lastActiveAt }
                ?.id
            else -> latestTabs.maxByOrNull { it.lastActiveAt }?.id
        }

        tabs = latestTabs
        spaces = latestSpaces
        activeSpaceId = resolvedSpaceId
        activeTabId = resolvedTabId
    }

    fun saveSearchEngine(engine: String) {
        selectedSearchEngine.value = engine
        browserPrefs.edit().putString("search_engine", engine).apply()
    }

    fun clearBrowsingData() {
        CookieManager.getInstance().removeAllCookies(null)
        CookieManager.getInstance().flush()
        WebStorage.getInstance().deleteAllData()
        WebView(context).apply {
            clearCache(true)
            clearHistory()
            clearFormData()
            destroy()
        }
    }

    fun applyDisplayOptions() {
        webViewRef?.applyDisplayOptions(isReaderMode = isReaderMode, zoomLevel = zoomLevel)
    }

    fun runReadabilityExtraction() {
        val webView = webViewRef ?: return
        runMainBrowserReadabilityExtraction(
            webView = webView,
            onReaderUrlCaptured = { readerUrl ->
                if (urlBeforeReaderMode == null) urlBeforeReaderMode = readerUrl
            },
            onReaderContentLoaded = { isReaderMode = true },
        )
    }

    fun toggleDesktopMode() {
        isDesktopMode = !isDesktopMode
        webViewRef?.applyDesktopMode(isDesktopMode)
        webViewRef?.reload()
    }

    fun adjustZoom(delta: Float) {
        zoomLevel = (zoomLevel + delta).coerceIn(0.5f, 2.0f)
        applyDisplayOptions()
    }

    fun pushOverlay(route: BrowserOverlay) {
        overlayStack = overlayStack + route
    }

    fun popOverlay() {
        if (overlayStack.lastOrNull() == BrowserOverlay.WebAgent) {
            webAgentInitialGoal = null
        }
        overlayStack = overlayStack.dropLast(1)
    }

    fun clearOverlays() {
        webAgentInitialGoal = null
        overlayStack = emptyList()
    }

    fun presentWebAgent(initialGoal: String? = null) {
        webAgentInitialGoal = initialGoal
            ?.trim()
            ?.takeIf { it.isNotEmpty() }
        pushOverlay(BrowserOverlay.WebAgent)
    }

    fun revealBottomBar(lockMillis: Long = 0L) {
        bottomBarVisible = true
        chromeScrollAccumulator = 0f
        if (lockMillis > 0L) {
            chromeRevealLockUntilMs = SystemClock.elapsedRealtime() + lockMillis
        }
    }

    fun updateBottomBarVisibilityFromScroll(scrollY: Int, oldScrollY: Int) {
        if (shellRoute != ShellRoute.Browsing || activeTabId == null || showFindSheet || showTabGrid || overlayStack.isNotEmpty()) {
            revealBottomBar()
            return
        }

        val delta = (scrollY - oldScrollY).toFloat()
        if (scrollY <= 24) {
            revealBottomBar(lockMillis = 220)
            return
        }
        if (delta == 0f || SystemClock.elapsedRealtime() < chromeRevealLockUntilMs) return

        chromeScrollAccumulator = if (
            chromeScrollAccumulator == 0f ||
            (chromeScrollAccumulator > 0f && delta > 0f) ||
            (chromeScrollAccumulator < 0f && delta < 0f)
        ) {
            chromeScrollAccumulator + delta
        } else {
            delta
        }

        when {
            chromeScrollAccumulator >= 44f && bottomBarVisible -> {
                bottomBarVisible = false
                chromeScrollAccumulator = 0f
            }

            chromeScrollAccumulator <= -28f && !bottomBarVisible -> {
                revealBottomBar(lockMillis = 320)
            }
        }
    }

    fun transitionToHome(returnTabId: TabId? = activeTabId) {
        shellRoute = ShellRoute.Home
        homeReturnTabId = returnTabId
        homeQuery = ""
        showFindSheet = false
        showSummaryOverlay = false
        summaryState = dev.maho.browser.support.PageSummarizer.State.Idle
        pendingBrowsingTabId = null
        revealBottomBar(lockMillis = 500)
    }

    fun transitionToBrowsing(tabId: TabId?) {
        if (tabId == null) {
            transitionToHome()
            return
        }

        activeTabId = tabId
        shellRoute = ShellRoute.Browsing
        homeReturnTabId = null
        pendingBrowsingTabId = null
        showTabGrid = false
        revealBottomBar(lockMillis = 700)
    }

    fun completeTabGridNewTabFlow(spaceId: SpaceId, tabId: TabId) {
        BridgeSpaces.activateSpace(spaceId)
        BridgeTabs.activateTab(tabId)
        refreshBrowserState()
        activeSpaceId = spaceId
        activeTabId = tabId
        pendingHomeNavigationUrl = null
        pendingBrowsingTabId = null
        clearOverlays()
        showFindSheet = false
        showSummaryOverlay = false
        summaryState = dev.maho.browser.support.PageSummarizer.State.Idle
        transitionToBrowsing(tabId)
    }

    fun activateTabAt(index: Int, visibleTabs: List<TabViewModel>) {
        val targetTab = visibleTabs.getOrNull(index) ?: return
        BridgeTabs.activateTab(targetTab.id)
        refreshBrowserState()
        activeTabId = targetTab.id
        transitionToBrowsing(targetTab.id)
    }

    fun shareCurrentPage() {
        val urlToShare: String = currentUrl.ifBlank { tabs.firstOrNull { it.id == activeTabId }?.url.orEmpty() }
        if (urlToShare.isBlank()) return
        val titleToShare: String = currentTitle.ifBlank { urlToShare }
        val sendIntent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_TEXT, urlToShare)
            putExtra(Intent.EXTRA_TITLE, titleToShare)
        }
        context.startActivity(Intent.createChooser(sendIntent, null))
    }

    fun handleNewTab(spaceId: SpaceId, url: String? = null): TabId? {
        val previousTabIds = tabs.map { it.id }.toSet()
        val updates = BridgeTabs.createTab(spaceId, url)
        val createdTab = updates.filterIsInstance<CoreUpdate.TabCreated>().firstOrNull()?.tab
        val createdTabId = createdTab?.id
            ?: BridgeTabs.getActiveTabId()
        if (createdTabId != null) {
            BridgeTabs.activateTab(createdTabId)
            activeTabId = createdTabId
        }
        refreshBrowserState()
        if (createdTab != null && tabs.none { it.id == createdTab.id }) {
            tabs = tabs + createdTab
        }
        activeSpaceId = activeSpaceId ?: spaceId
        val resolvedCreatedTabId = createdTabId
            ?: tabs.firstOrNull { it.spaceId == spaceId && it.id !in previousTabIds }?.id
            ?: tabs.filter { it.spaceId == spaceId }.maxByOrNull { it.lastActiveAt }?.id

        if (resolvedCreatedTabId != null && activeTabId != resolvedCreatedTabId) {
            BridgeTabs.activateTab(resolvedCreatedTabId)
            refreshBrowserState()
            activeTabId = resolvedCreatedTabId
        }

        return activeTabId ?: resolvedCreatedTabId
    }

    fun resolveTargetSpaceId(): SpaceId? {
        val existingSpaceId = activeSpaceId ?: spaces.firstOrNull()?.id
        if (existingSpaceId != null) {
            return existingSpaceId
        }

        val createdSpaceId = BridgeSpaces.createSpace(
            name = "Space",
            color = SpaceColor(hue = 210.0, saturation = 0.72, brightness = 0.82),
            profileId = "",
        ).filterIsInstance<CoreUpdate.SpaceCreated>()
            .firstOrNull()
            ?.space
            ?.id

        if (createdSpaceId != null) {
            BridgeSpaces.activateSpace(createdSpaceId)
            refreshBrowserState()
            return createdSpaceId
        }

        refreshBrowserState()
        return activeSpaceId ?: spaces.firstOrNull()?.id
    }

    fun submitSearch(
        rawInput: String,
        reuseActiveTab: Boolean = shellRoute == ShellRoute.Browsing && activeTabId != null,
    ) {
        val target = normalizeBrowserInput(rawInput, selectedSearchEngine.value)
        if (target.isBlank()) return

        homeQuery = ""

        val targetTabId = if (reuseActiveTab) {
            activeTabId
        } else {
            null
        }

        requestedNavigationUrl = target

        if (targetTabId != null) {
            pendingHomeNavigationUrl = null
            pendingBrowsingTabId = targetTabId
            BridgeNavigation.navigate(targetTabId, target)
            webViewRef?.loadUrl(target)
            currentUrl = target
            currentTitle = ""
            transitionToBrowsing(targetTabId)
        } else {
            pendingHomeNavigationUrl = target
            pendingBrowsingTabId = null
            shellRoute = ShellRoute.Browsing
            homeReturnTabId = null
            val fallbackSpaceId = resolveTargetSpaceId() ?: return
            val createdTabId = handleNewTab(fallbackSpaceId, target)
            val resolvedTabId = createdTabId ?: activeTabId
            if (resolvedTabId != null) {
                pendingHomeNavigationUrl = null
                pendingBrowsingTabId = resolvedTabId
                shellRoute = ShellRoute.Browsing
                homeReturnTabId = null
                transitionToBrowsing(resolvedTabId)
            }
            currentUrl = target
            currentTitle = ""
        }

        clearOverlays()
        revealBottomBar(lockMillis = 800)
    }

    fun handleSearchSuggestionSelection(suggestion: SuggestionViewModel) {
        showSearchSheet = false
        homeQuery = ""

        if (suggestion.kind == SuggestionType.AiAnswer) {
            presentWebAgent(aiSearchQueryText(suggestion))
            return
        }

        val target = suggestionNavigationTarget(suggestion)
        if (target.isNotBlank()) {
            submitSearch(
                target,
                reuseActiveTab = shouldReuseActiveTab(
                    isBrowsingRoute = shellRoute == ShellRoute.Browsing,
                    hasActiveTab = activeTabId != null,
                ),
            )
        }
    }

    LaunchedEffect(Unit) {
        refreshBrowserState()
        transitionToHome(returnTabId = activeTabId)
    }

    LaunchedEffect(externalUrl) {
        val url = externalUrl
        if (!url.isNullOrBlank()) {
            when {
                url.startsWith("maho:search?q=") -> {
                    val query = java.net.URLDecoder.decode(url.removePrefix("maho:search?q="), "UTF-8")
                    submitSearch(query)
                }
                url == "maho:newtab" -> transitionToHome()
                else -> submitSearch(url)
            }
        }
    }

    val activeTab = tabs.firstOrNull { it.id == activeTabId }
    val activeSpace = spaces.firstOrNull { it.id == activeSpaceId }
    val currentSpaceTabs = tabs.filter { tab ->
        activeSpaceId == null || tab.spaceId == activeSpaceId
    }
    val homeActivityTabs = currentSpaceTabs.filter { !isSystemSurfaceUrl(it.url) }
    val recentTabs = homeActivityTabs
        .sortedByDescending { it.lastActiveAt }
        .take(3)
    val topSites = buildList {
        val seenUrls = linkedSetOf<String>()
        homeActivityTabs
            .sortedByDescending { it.lastActiveAt }
            .forEach { tab ->
                if (tab.url.isNotBlank() && seenUrls.add(tab.url)) {
                    add(tab)
                }
            }
    }.take(8)
    val activeSpaceName = activeSpace?.name ?: spaces.firstOrNull { it.isActive }?.name ?: "Space"
    val browsingTabId = activeTabId ?: pendingBrowsingTabId
    val browsingInitialUrl = resolveBrowsingUrl(
        requestedUrl = requestedNavigationUrl ?: pendingHomeNavigationUrl,
        activeTabUrl = activeTab?.url,
        currentUrl = currentUrl,
    )
    val isBrowsingRoute = shellRoute == ShellRoute.Browsing
    val isCleanHomeChrome = shellRoute == ShellRoute.Home &&
        overlayStack.isEmpty() &&
        !showTabGrid &&
        !showFindSheet &&
        !showSearchSheet &&
        !showSummaryOverlay
    val shellRouteStateDescription = mainBrowserStateDescription(
        showTabGrid = showTabGrid,
        showSummaryOverlay = showSummaryOverlay,
        summaryState = summaryState,
        isBrowsingRoute = isBrowsingRoute,
        isIncognito = isIncognito,
    )
    val resolvedHomeReturnTabId = homeReturnTabId?.takeIf { returnTabId ->
        tabs.any { it.id == returnTabId }
    }
    val chromeTheme = resolveBrowserChromeTheme(
        shellColors = shellColors,
        inverseSurface = MaterialTheme.colorScheme.inverseSurface,
        activeSpaceColor = activeSpace?.color?.toComposeColor(),
        isIncognito = isIncognito,
        isSystemDarkTheme = isSystemDarkTheme,
        showSearchSheet = showSearchSheet,
        isCleanHomeChrome = isCleanHomeChrome,
    )
    val reduceMotion = remember(context) {
        runCatching {
            android.provider.Settings.Global.getFloat(
                context.contentResolver,
                android.provider.Settings.Global.ANIMATOR_DURATION_SCALE,
            ) == 0f
        }.getOrDefault(false)
    }

    SideEffect {
        val activity = context as? Activity
        val actionBar = activity?.actionBar
        if (shellRoute == ShellRoute.Home) {
            actionBar?.hide()
        } else {
            actionBar?.show()
        }

        val window = activity?.window ?: return@SideEffect
        window.statusBarColor = chromeTheme.statusBarColor.toArgb()
        WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = chromeTheme.useDarkStatusIcons
    }

    LaunchedEffect(activeTabId) {
        currentUrl = activeTab?.url.orEmpty()
        currentTitle = activeTab?.title.orEmpty()
        isLoading = activeTab?.isLoading == true
        loadProgress = if (activeTab?.isLoading == true) 15 else 100

        if (activeTabId != null && pendingBrowsingTabId == activeTabId) {
            pendingBrowsingTabId = null
        }

        isReaderMode = false
        zoomLevel = 1.0f
        if (showSummaryOverlay) {
            showSummaryOverlay = false
            summaryState = dev.maho.browser.support.PageSummarizer.State.Idle
        }
        if (activeTabId == null) {
            showFindSheet = false
            revealBottomBar(lockMillis = 300)
        }
    }

    LaunchedEffect(tabs, isIncognito) {
        val incognitoTabsCount = if (isIncognito) currentSpaceTabs.count() else 0
        if (isIncognito && prevIncognitoTabsCount > 0 && incognitoTabsCount == 0) {
            clearBrowsingData()
        }
        prevIncognitoTabsCount = incognitoTabsCount
    }

    LaunchedEffect(tabs, pendingHomeNavigationUrl, shellRoute) {
        val pendingUrl = pendingHomeNavigationUrl ?: return@LaunchedEffect
        if (shellRoute != ShellRoute.Home) return@LaunchedEffect

        val matchingTab = tabs
            .sortedByDescending { it.lastActiveAt }
            .firstOrNull { it.url == pendingUrl }
            ?: return@LaunchedEffect

        BridgeTabs.activateTab(matchingTab.id)
        refreshBrowserState()
        pendingHomeNavigationUrl = null
        transitionToBrowsing(matchingTab.id)
    }

    MainBrowserBackHandlers(
        showTabGrid = showTabGrid,
        showFindSheet = showFindSheet,
        showSummaryOverlay = showSummaryOverlay,
        showSearchSheet = showSearchSheet,
        hasOverlay = overlayStack.isNotEmpty(),
        isHomeRoute = shellRoute == ShellRoute.Home,
        homeReturnTabId = resolvedHomeReturnTabId,
        onDismissTabGrid = { showTabGrid = false },
        onDismissFind = { showFindSheet = false },
        onDismissSummary = {
            showSummaryOverlay = false
            summaryState = dev.maho.browser.support.PageSummarizer.State.Idle
        },
        onPopOverlay = ::popOverlay,
        onReturnToTab = ::transitionToBrowsing,
        onDismissSearch = { showSearchSheet = false },
    )

    val predictiveBackEnabled =
        isBrowsingRoute && activeTabId != null && !showTabGrid && !showFindSheet && !showSearchSheet && overlayStack.isEmpty()
    androidx.activity.compose.PredictiveBackHandler(enabled = predictiveBackEnabled) { progress ->
        try {
            progress.collect { event ->
                predictiveBackProgress = event.progress
            }
        } catch (_: Throwable) {
            predictiveBackProgress = 0f
        }
        predictiveBackProgress = 0f
        if (webViewRef?.canGoBack() == true) {
            requestedNavigationUrl = null
            webViewRef?.goBack()
        } else {
            activeTabId?.let { tabId ->
                BridgeTabs.closeTab(tabId)
            }
            refreshBrowserState()
            if (activeTabId != null) {
                transitionToBrowsing(activeTabId)
            } else {
                transitionToHome()
            }
        }
    }

    val backScale = 1f - (predictiveBackProgress * 0.08f).coerceIn(0f, 0.08f)
    val backAlpha = 1f - (predictiveBackProgress * 0.3f).coerceIn(0f, 0.3f)

    Scaffold(
        modifier = Modifier
            .fillMaxSize()
            .graphicsLayer {
                scaleX = backScale
                scaleY = backScale
                alpha = backAlpha
            },
        contentWindowInsets = WindowInsets.safeDrawing,
        bottomBar = {
            if (overlayStack.isEmpty() && !showSearchSheet) {
                if (isBrowsingRoute) {
                    AnimatedVisibility(
                        visible = bottomBarVisible,
                        enter = slideInVertically(
                            animationSpec = tween(durationMillis = 320, easing = FastOutSlowInEasing),
                            initialOffsetY = { it / 3 },
                        ) + fadeIn(animationSpec = tween(durationMillis = 220)),
                        exit = slideOutVertically(
                            animationSpec = tween(durationMillis = 220, easing = LinearOutSlowInEasing),
                            targetOffsetY = { it / 2 },
                        ) + fadeOut(animationSpec = tween(durationMillis = 160)),
                    ) {
                        Box(modifier = Modifier.testTag("mainBrowserBottomBar")) {
                            ArcBottomBar(
                                tabCount = currentSpaceTabs.count(),
                                spaces = spaces,
                                isIncognito = isIncognito,
                                isHomeMode = false,
                                homeQuery = homeQuery,
                                isReaderMode = isReaderMode || urlBeforeReaderMode != null,
                                isDesktopMode = isDesktopMode,
                                currentTitle = currentTitle,
                                currentUrl = currentUrl,
                                isLoading = isLoading,
                                zoomLevel = zoomLevel,
                                tintColor = chromeTheme.tintColor,
                                onHomeQueryChange = { homeQuery = it },
                                onHomeSubmit = {
                                    submitSearch(
                                        homeQuery,
                                        reuseActiveTab = shouldReuseActiveTab(
                                            isBrowsingRoute = true,
                                            hasActiveTab = activeTabId != null,
                                        ),
                                    )
                                },
                                onTabsClick = {
                                    captureActiveTabPreview()
                                    showTabGrid = true
                                    revealBottomBar(lockMillis = 900)
                                },
                                onSelectSpace = { spaceId ->
                                    BridgeSpaces.activateSpace(spaceId)
                                    refreshBrowserState()
                                    val visibleTabs = tabs.filter { it.spaceId == spaceId }
                                    if (visibleTabs.isNotEmpty()) {
                                        activateTabAt(0, visibleTabs)
                                    } else {
                                        transitionToHome(returnTabId = null)
                                    }
                                },
                                onSearchClick = {
                                    homeQuery = resolveAddressBarPrefill(currentUrl, isHomeMode = false)
                                    showSearchSheet = true
                                },
                                onGoBack = {
                                    activeTabId?.let { tabId ->
                                        requestedNavigationUrl = null
                                        BridgeNavigation.goBack(tabId)
                                        webViewRef?.goBack()
                                    }
                                },
                                onGoForward = {
                                    activeTabId?.let { tabId ->
                                        BridgeNavigation.goForward(tabId)
                                        webViewRef?.goForward()
                                    }
                                },
                                onPreviousTab = {
                                    val activeIndex = currentSpaceTabs.indexOfFirst { it.id == activeTabId }
                                    val previousTabId = currentSpaceTabs.getOrNull(activeIndex - 1)?.id
                                    if (previousTabId != null) {
                                        BridgeTabs.activateTab(previousTabId)
                                        refreshBrowserState()
                                        transitionToBrowsing(previousTabId)
                                    }
                                },
                                onNextTab = {
                                    val activeIndex = currentSpaceTabs.indexOfFirst { it.id == activeTabId }
                                    val nextTabId = currentSpaceTabs.getOrNull(activeIndex + 1)?.id
                                    if (nextTabId != null) {
                                        BridgeTabs.activateTab(nextTabId)
                                        refreshBrowserState()
                                        transitionToBrowsing(nextTabId)
                                    }
                                },
                                onToggleIncognito = { isIncognito = it },
                                onFindInPage = {
                                    if (activeTabId != null) {
                                        showFindSheet = true
                                        revealBottomBar(lockMillis = 1200)
                                    }
                                },
                                onArchive = {
                                    pushOverlay(BrowserOverlay.Archive)
                                },
                                onOpenSettings = {
                                    pushOverlay(BrowserOverlay.Settings)
                                    showFindSheet = false
                                },
                                onOpenConversations = {
                                    pushOverlay(BrowserOverlay.Conversations)
                                    showFindSheet = false
                                },
                                onShare = { shareCurrentPage() },
                                onReload = {
                                    activeTabId?.let { tabId ->
                                        BridgeNavigation.reload(tabId)
                                        webViewRef?.reload()
                                    }
                                },
                                onToggleReaderMode = {
                                    val originalUrl = urlBeforeReaderMode
                                    if (originalUrl == null) {
                                        runReadabilityExtraction()
                                    } else {
                                        isReaderMode = false
                                        urlBeforeReaderMode = null
                                        webViewRef?.loadUrl(originalUrl)
                                    }
                                },
                                onReaderSettingsClick = {
                                    showReaderSettingsSheet = true
                                },
                                onZoomIn = {
                                    adjustZoom(0.1f)
                                },
                                onZoomOut = {
                                    adjustZoom(-0.1f)
                                },
                                onToggleDesktopMode = {
                                    toggleDesktopMode()
                                },
                                onPiP = {
                                    dev.maho.browser.PiPHelper.isVideoPlaying(webViewRef) { playing ->
                                        if (playing) {
                                            (context as? android.app.Activity)?.let { activity ->
                                                dev.maho.browser.PiPHelper.enterPiP(activity)
                                            }
                                        }
                                    }
                                },
                                onBrowseForMe = {
                                    if (activeTabId != null) {
                                        showSummaryOverlay = true
                                        summaryState = dev.maho.browser.support.PageSummarizer.State.Loading
                                        revealBottomBar(lockMillis = 1400)
                                        dev.maho.browser.support.PageSummarizer.extractAndSummarize(webViewRef) { newState ->
                                            summaryState = newState
                                        }
                                    }
                                },
                                canSwipeToPreviousTab = currentSpaceTabs.indexOfFirst { it.id == activeTabId } > 0,
                                canSwipeToNextTab = run {
                                    val activeIndex = currentSpaceTabs.indexOfFirst { it.id == activeTabId }
                                    activeIndex >= 0 && activeIndex < currentSpaceTabs.lastIndex
                                },
                            )
                        }
                    }
                } else {
                    Box(modifier = Modifier.testTag("mainBrowserBottomBar")) {
                        ArcBottomBar(
                            tabCount = currentSpaceTabs.count(),
                            spaces = spaces,
                            isIncognito = isIncognito,
                            isHomeMode = true,
                            homeQuery = homeQuery,
                            isReaderMode = isReaderMode,
                            isDesktopMode = isDesktopMode,
                            currentTitle = currentTitle,
                            currentUrl = currentUrl,
                            isLoading = false,
                            zoomLevel = zoomLevel,
                            tintColor = chromeTheme.tintColor,
                            onHomeQueryChange = { homeQuery = it },
                            onHomeSubmit = {
                                submitSearch(homeQuery, reuseActiveTab = false)
                            },
                            onTabsClick = {
                                captureActiveTabPreview()
                                    showTabGrid = true
                                revealBottomBar(lockMillis = 900)
                            },
                            onSelectSpace = { spaceId ->
                                BridgeSpaces.activateSpace(spaceId)
                                refreshBrowserState()
                                val visibleTabs = tabs.filter { it.spaceId == spaceId }
                                if (visibleTabs.isNotEmpty()) {
                                    activateTabAt(0, visibleTabs)
                                } else {
                                    transitionToHome(returnTabId = null)
                                }
                            },
                            onSearchClick = {
                                homeQuery = resolveAddressBarPrefill(currentUrl, isHomeMode = true)
                                showSearchSheet = true
                            },
                            onGoBack = {},
                            onGoForward = {},
                            onPreviousTab = {},
                            onNextTab = {},
                            onToggleIncognito = { isIncognito = it },
                            onFindInPage = {},
                            onArchive = {
                                pushOverlay(BrowserOverlay.Archive)
                            },
                            onOpenSettings = {
                                pushOverlay(BrowserOverlay.Settings)
                            },
                            onOpenAiChat = {
                                presentWebAgent()
                            },
                            onOpenConversations = {
                                pushOverlay(BrowserOverlay.Conversations)
                            },
                            onShare = {},
                            onReload = {},
                            onToggleReaderMode = {},
                            onReaderSettingsClick = {},
                            onZoomIn = {},
                            onZoomOut = {},
                            onToggleDesktopMode = {},
                        )
                    }
                }
            }
        },
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .testTag("mainBrowserShell")
                .semantics {
                    stateDescription = buildString {
                        append(shellRouteStateDescription)
                        activeSpaceId?.let { append(", active space $it") }
                        activeTabId?.let { append(", active tab $it") }
                    }
                },
        ) {
            if (isBrowsingRoute) {
                Surface(
                    modifier = Modifier
                        .fillMaxSize()
                        .testTag("mainBrowserBrowsingSurface"),
                    color = if (isIncognito) shellColors.incognitoBackground else shellColors.browsingBackground,
                ) {
                    when (val tabId = browsingTabId) {
                        null -> Box(modifier = Modifier.fillMaxSize())
                        else -> WebViewHost(
                            tabId = tabId,
                            initialUrl = browsingInitialUrl,
                            isDesktopMode = isDesktopMode,
                            isIncognito = isIncognito,
                            onUrlChanged = { url ->
                                currentUrl = url
                                if (requestedNavigationUrl != null && url.isNotBlank()) {
                                    requestedNavigationUrl = null
                                }
                            },
                            onTitleChanged = { title ->
                                currentTitle = title
                                refreshBrowserState()
                            },
                            onLoadingChanged = { loading ->
                                isLoading = loading
                                if (loading && shouldResetReaderModeOnNavigation(urlBeforeReaderMode, currentUrl)) {
                                    isReaderMode = false
                                    urlBeforeReaderMode = null
                                    revealBottomBar(lockMillis = 700)
                                }
                                if (!loading) {
                                    loadProgress = 100
                                    refreshBrowserState()
                                }
                            },
                            onProgressChanged = { progress ->
                                loadProgress = progress
                            },
                            onNavigationStateChanged = { _, _ -> },
                            onError = { _, _, _ ->
                                isLoading = false
                            },
                            onWebViewReady = { webView ->
                                webViewRef = webView
                                webView.applyDesktopMode(isDesktopMode)
                                webView.applyDisplayOptions(
                                    isReaderMode = isReaderMode,
                                    zoomLevel = zoomLevel,
                                )
                            },
                            onNewTabRequested = { url ->
                                val spaceId = activeSpaceId ?: spaces.firstOrNull()?.id
                                if (spaceId != null) {
                                    handleNewTab(spaceId, url)
                                }
                            },
                            onSingleTap = {
                                if (!bottomBarVisible) {
                                    revealBottomBar(lockMillis = 1200)
                                }
                            },
                            onPinchSummarize = {
                                val webView = webViewRef
                                if (webView != null) {
                                    dev.maho.browser.support.PageSummarizer.extractAndSummarize(webView) { state ->
                                        if (state is dev.maho.browser.support.PageSummarizer.State.Result) {
                                            pinchSummarySentences = state.bullets
                                            showPinchSummary = true
                                        }
                                    }
                                }
                            },
                            onScrollChanged = { scrollY, oldScrollY ->
                                updateBottomBarVisibilityFromScroll(
                                    scrollY = scrollY,
                                    oldScrollY = oldScrollY,
                                )
                            },
                            modifier = Modifier.fillMaxSize(),
                        )
                    }
                }
            } else {
                HomeSearchScreen(
                    isIncognito = isIncognito,
                    activeSpaceName = activeSpaceName,
                    activeSpaceTabCount = currentSpaceTabs.count(),
                    recentTabs = recentTabs,
                    topSites = topSites,
                    onSelectSite = { url ->
                        submitSearch(url, reuseActiveTab = false)
                    },
                    onResumeTab = { tabId ->
                        BridgeTabs.activateTab(tabId)
                        refreshBrowserState()
                        transitionToBrowsing(tabId)
                    },
                    modifier = Modifier
                        .fillMaxSize()
                        .testTag("mainBrowserHomeSurface"),
                )
            }

            if (showFindSheet && activeTabId != null && overlayStack.isEmpty()) {
                Box(
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .padding(
                            horizontal = 12.dp,
                            vertical = if (bottomBarVisible) 96.dp else 8.dp,
                        ),
                ) {
                    FindInPageSheet(
                        visibleTabId = activeTabId,
                        onDismiss = { showFindSheet = false },
                        isIncognito = isIncognito,
                    )
                }
            }

            MainBrowserSearchOverlay(
                visible = showSearchSheet && overlayStack.isEmpty(),
                reduceMotion = reduceMotion,
                initialQuery = homeQuery,
                isIncognito = isIncognito,
                onDismiss = {
                    showSearchSheet = false
                    homeQuery = ""
                },
                onSubmit = { entered ->
                    showSearchSheet = false
                    homeQuery = ""
                    submitSearch(
                        entered,
                        reuseActiveTab = shouldReuseActiveTab(
                            isBrowsingRoute = isBrowsingRoute,
                            hasActiveTab = activeTabId != null,
                        ),
                    )
                },
                onToggleIncognito = { isIncognito = !isIncognito },
                onSelectSuggestion = ::handleSearchSuggestionSelection,
                onBrowseForMe = { entered ->
                    showSearchSheet = false
                    homeQuery = ""
                    presentWebAgent(entered)
                },
                onRecordSuggestionSelection = { index, suggestion ->
                    BridgeCommandBar.selectItem(index = index, key = suggestion.key)
                },
            )

            if (showPinchSummary && overlayStack.isEmpty()) {
                PinchSummaryView(
                    title = currentTitle,
                    sentences = pinchSummarySentences,
                    onDismiss = { showPinchSummary = false },
                    isIncognito = isIncognito
                )
            }

            if (showSummaryOverlay && overlayStack.isEmpty()) {
                MainBrowserSummaryShell(
                    state = summaryState,
                    pageTitle = currentTitle,
                    pageUrl = currentUrl,
                    isIncognito = isIncognito,
                    onDismiss = {
                        showSummaryOverlay = false
                        summaryState = dev.maho.browser.support.PageSummarizer.State.Idle
                    },
                )
            }

            if (isBrowsingRoute && isIncognito && !showSummaryOverlay && !showTabGrid && overlayStack.isEmpty()) {
                MainBrowserIncognitoBadge(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(top = 14.dp, end = 14.dp)
                        .testTag("incognitoShellBadge"),
                )
            }

            overlayStack.lastOrNull()?.let { overlay ->
                MainBrowserOverlay(
                    overlay = overlay,
                    webAgentInitialGoal = webAgentInitialGoal,
                    selectedSearchEngine = selectedSearchEngine.value,
                    onPush = ::pushOverlay,
                    onPop = ::popOverlay,
                    onClear = ::clearOverlays,
                    onRefresh = ::refreshBrowserState,
                    onSubmitSearch = { submitSearch(it) },
                    onOpenArchivedTab = ::transitionToBrowsing,
                    onSelectSearchEngine = ::saveSearchEngine,
                    onClearBrowsingData = ::clearBrowsingData,
                )
            }
        }
    }

    if (showTabGrid && overlayStack.isEmpty()) {
        TabGridScreen(
            activeTabId = activeTabId,
            isIncognito = isIncognito,
            onDismiss = { showTabGrid = false },
            onTabSelected = { tabId ->
                refreshBrowserState()
                if (tabId != null) {
                    transitionToBrowsing(tabId)
                } else {
                    transitionToHome(returnTabId = null)
                }
                showTabGrid = false
            },
            onNewTabCreated = { spaceId, tabId ->
                completeTabGridNewTabFlow(spaceId, tabId)
            },
        )
    }

    if (showReaderSettingsSheet) {
        dev.maho.browser.ui.settings.ReaderSettingsSheet(
            onDismiss = { showReaderSettingsSheet = false }
        )
    }
}

private fun normalizeBrowserInput(input: String, searchEngine: String): String =
    normalizeBrowserInputValue(input, searchEngine)
