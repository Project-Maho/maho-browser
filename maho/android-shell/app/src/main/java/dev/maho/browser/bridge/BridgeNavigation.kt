package dev.maho.browser.bridge

import dev.maho.browser.MahoBridge
import dev.maho.browser.models.CoreUpdate
import dev.maho.browser.models.ShellEvent
import dev.maho.browser.models.TabId
import dev.maho.browser.models.Url

object BridgeNavigation {

    fun navigate(tabId: TabId, url: Url): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.NavigateTo(tabId = tabId, url = url))

    fun goBack(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.GoBack(tabId = tabId))

    fun goForward(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.GoForward(tabId = tabId))

    fun reload(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.Reload(tabId = tabId))

    fun stop(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.Stop(tabId = tabId))

    fun setZoom(tabId: TabId, level: Double): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.SetZoom(tabId = tabId, zoomLevel = level))

    fun resetZoom(tabId: TabId): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.ResetZoom(tabId = tabId))

    fun reportUrlUpdated(tabId: TabId, url: Url): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.TabUrlUpdated(tabId = tabId, url = url))

    fun reportTitleUpdated(tabId: TabId, title: String): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.TabTitleUpdated(tabId = tabId, title = title))

    fun reportLoadingChanged(tabId: TabId, isLoading: Boolean): List<CoreUpdate> =
        MahoBridge.sendEvent(ShellEvent.TabLoadingChanged(tabId = tabId, isLoading = isLoading))

    fun reportNavigationStateChanged(
        tabId: TabId,
        canGoBack: Boolean,
        canGoForward: Boolean,
    ): List<CoreUpdate> =
        MahoBridge.sendEvent(
            ShellEvent.TabNavigationStateChanged(
                tabId = tabId,
                canGoBack = canGoBack,
                canGoForward = canGoForward,
            )
        )

    data class NavigationSnapshot(
        val tabId: TabId,
        val url: Url?,
        val title: String?,
        val canGoBack: Boolean,
        val canGoForward: Boolean,
        val isLoading: Boolean,
        val progress: Double,
        val zoomLevel: Double?,
    )

    fun snapshotFrom(update: CoreUpdate.NavigationStateChanged): NavigationSnapshot =
        NavigationSnapshot(
            tabId = update.tabId,
            url = update.url,
            title = update.title,
            canGoBack = update.canGoBack,
            canGoForward = update.canGoForward,
            isLoading = update.isLoading,
            progress = update.progress,
            zoomLevel = null,
        )

    fun isNavigationUpdate(update: CoreUpdate): Boolean = when (update) {
        is CoreUpdate.NavigateTab -> true
        is CoreUpdate.NavigationStateChanged -> true
        is CoreUpdate.ZoomChanged -> true
        is CoreUpdate.TabUpdated -> {
            update.changes.url != null || update.changes.title != null || update.changes.isLoading != null
        }
        else -> false
    }
}
