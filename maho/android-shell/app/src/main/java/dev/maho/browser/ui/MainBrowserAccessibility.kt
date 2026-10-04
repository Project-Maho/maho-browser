package dev.maho.browser.ui

import dev.maho.browser.support.PageSummarizer

internal fun mainBrowserStateDescription(
    showTabGrid: Boolean,
    showSummaryOverlay: Boolean,
    summaryState: PageSummarizer.State,
    isBrowsingRoute: Boolean,
    isIncognito: Boolean,
): String = when {
    showTabGrid && isIncognito -> "incognito tab switcher"
    showTabGrid -> "tab switcher"
    showSummaryOverlay && summaryState is PageSummarizer.State.Loading && isIncognito -> "incognito browse for me loading"
    showSummaryOverlay && summaryState is PageSummarizer.State.Loading -> "browse for me loading"
    showSummaryOverlay && summaryState is PageSummarizer.State.Result && isIncognito -> "incognito summary ready"
    showSummaryOverlay && summaryState is PageSummarizer.State.Result -> "summary ready"
    showSummaryOverlay && summaryState is PageSummarizer.State.Error && isIncognito -> "incognito summary error"
    showSummaryOverlay && summaryState is PageSummarizer.State.Error -> "summary error"
    isBrowsingRoute && isIncognito -> "incognito browsing"
    isBrowsingRoute -> "browsing"
    isIncognito -> "incognito home"
    else -> "home"
}
