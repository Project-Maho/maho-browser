package dev.maho.browser.ui

import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import dev.maho.browser.models.TabId

@Composable
internal fun MainBrowserBackHandlers(
    showTabGrid: Boolean,
    showFindSheet: Boolean,
    showSummaryOverlay: Boolean,
    showSearchSheet: Boolean,
    hasOverlay: Boolean,
    isHomeRoute: Boolean,
    homeReturnTabId: TabId?,
    onDismissTabGrid: () -> Unit,
    onDismissFind: () -> Unit,
    onDismissSummary: () -> Unit,
    onPopOverlay: () -> Unit,
    onReturnToTab: (TabId) -> Unit,
    onDismissSearch: () -> Unit,
) {
    BackHandler(enabled = showTabGrid, onBack = onDismissTabGrid)
    BackHandler(enabled = showFindSheet, onBack = onDismissFind)
    BackHandler(enabled = showSummaryOverlay && !hasOverlay, onBack = onDismissSummary)
    BackHandler(enabled = hasOverlay, onBack = onPopOverlay)
    BackHandler(
        enabled = isHomeRoute && homeReturnTabId != null &&
            !showTabGrid && !showFindSheet && !showSearchSheet && !hasOverlay,
    ) {
        homeReturnTabId?.let(onReturnToTab)
    }
    BackHandler(enabled = showSearchSheet && !hasOverlay, onBack = onDismissSearch)
}
