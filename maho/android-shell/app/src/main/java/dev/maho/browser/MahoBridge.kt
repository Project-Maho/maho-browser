package dev.maho.browser

import android.content.Intent
import dev.maho.browser.bridge.BridgeAgent
import dev.maho.browser.bridge.BridgeAi
import dev.maho.browser.bridge.BridgeChat
import dev.maho.browser.bridge.BridgeConversations

object MahoBridge {

    init {
        System.loadLibrary("maho_jni")
    }

    @Volatile
    internal var corePtr: Long = 0

    @Volatile
    var appContext: android.content.Context? = null

    internal var artifactShareLauncher: ((android.content.Context, Intent) -> Boolean)? = null

    val isInitialized: Boolean get() = corePtr != 0L

    // === Core Lifecycle ===

    /**
     * U06a hydration: runs OFF Main. The loader owns the native pointer
     * privately through key -> open -> load -> snapshot; the pointer is
     * published (isInitialized) only after a complete initial snapshot, and
     * a close requested during loading defers the reclaim to this loader.
     * Returns true when the core is Ready for dependent setup (sync, restore).
     */
    fun beginHydration(storagePath: String): Boolean {
        if (!CoreStartupState.beginLoading()) return false
        val ptr = nativeCreateWithStorage(storagePath)
        if (CoreStartupState.isClosingOrClosed()) {
            if (ptr != 0L) nativeDestroy(ptr)
            CoreStartupState.markClosed()
            return false
        }
        if (ptr == 0L) {
            CoreStartupState.markFailed()
            return false
        }
        val loaded = nativeLoadState(ptr)
        if (CoreStartupState.isClosingOrClosed()) {
            nativeDestroy(ptr)
            CoreStartupState.markClosed()
            return false
        }
        if (!loaded) {
            nativeDestroy(ptr)
            CoreStartupState.markFailed()
            return false
        }
        // Complete initial immutable snapshot BEFORE publication: views must
        // never observe a hydrated-but-unpopulated core.
        nativeGetTabViewModels(ptr)
        nativeGetSpaceViewModels(ptr)
        if (CoreStartupState.isClosingOrClosed()) {
            nativeDestroy(ptr)
            CoreStartupState.markClosed()
            return false
        }
        corePtr = ptr
        CoreStartupState.markReady()
        return true
    }

    fun initialize(storagePath: String) {
        if (corePtr != 0L) return
        corePtr = nativeCreateWithStorage(storagePath)
    }

    fun initializeDefault() {
        if (corePtr != 0L) return
        corePtr = nativeCreate()
    }

    fun destroy() {
        // A Loading core is owned by its loader: defer the reclaim instead of
        // freeing a pointer that is mid-native-load.
        if (CoreStartupState.requestClose()) return
        val ptr = corePtr
        corePtr = 0
        if (ptr != 0L) nativeDestroy(ptr)
        CoreStartupState.markClosed()
    }

    fun handleEvent(eventJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeHandleEvent(ptr, eventJson)
    }

    fun tick(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeTick(ptr)
    }

    fun saveState(): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeSaveState(ptr)
    }

    fun autoArchiveConversations(nowSeconds: Long = System.currentTimeMillis() / 1000): Int {
        val ptr = corePtr
        if (ptr == 0L) return -1
        return nativeAutoArchiveConversations(ptr, nowSeconds)
    }

    fun loadState(): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeLoadState(ptr)
    }

    // === View Models ===

    fun getTabViewModels(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetTabViewModels(ptr)
    }

    fun getSpaceViewModels(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetSpaceViewModels(ptr)
    }

    fun getFolderViewModels(spaceIdJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetFolderViewModels(ptr, spaceIdJson)
    }

    fun getDownloadViewModels(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetDownloadViewModels(ptr)
    }

    fun getNoteViewModels(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetNoteViewModels(ptr)
    }

    // === Settings ===

    fun getSettings(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetSettings(ptr)
    }

    fun updateSettings(settingsJson: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeUpdateSettings(ptr, settingsJson)
    }

    fun getSiteSearchEntries(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetSiteSearchEntries(ptr)
    }

    fun setSiteSearchEntries(json: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetSiteSearchEntries(ptr, json)
    }

    fun setDensity(density: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetDensity(ptr, density)
    }

    fun setCustomChromeCss(css: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetCustomChromeCss(ptr, css)
    }

    fun setWindowTransparency(enabled: Boolean) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetWindowTransparency(ptr, enabled)
    }

    fun setAppIcon(path: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetAppIcon(ptr, path)
    }

    fun setCrashSaveInterval(seconds: Long) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetCrashSaveInterval(ptr, seconds)
    }

    // === Tabs ===

    fun getPinnedTabs(spaceIdJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetPinnedTabs(ptr, spaceIdJson)
    }

    fun getTodayTabs(spaceIdJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetTodayTabs(ptr, spaceIdJson)
    }

    fun getArchivedTabs(spaceIdJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetArchivedTabs(ptr, spaceIdJson)
    }

    fun getFavoriteTabs(spaceIdJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetFavoriteTabs(ptr, spaceIdJson)
    }

    fun favoriteTab(tabIdJson: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeFavoriteTab(ptr, tabIdJson)
    }

    fun transitionTabRole(tabIdJson: String, newRoleJson: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeTransitionTabRole(ptr, tabIdJson, newRoleJson)
    }

    fun setTabParent(tabIdJson: String, newParentIdJson: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetTabParent(ptr, tabIdJson, newParentIdJson)
    }

    fun resetPinnedTab(tabId: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeResetPinnedTab(ptr, tabId)
    }

    fun checkPinnedNavigation(tabId: String, url: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeCheckPinnedNavigation(ptr, tabId, url)
    }

    // === Tab Previews ===

    fun schedulePreviewCapture(tabIdJson: String): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeSchedulePreviewCapture(ptr, tabIdJson)
    }

    fun takePendingPreviewCaptures(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeTakePendingPreviewCaptures(ptr)
    }

    fun hasTabPreview(tabIdJson: String): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeHasTabPreview(ptr, tabIdJson)
    }

    fun updateTabPreview(tabIdJson: String, data: ByteArray) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeUpdateTabPreview(ptr, tabIdJson, data)
    }

    fun getTabPreview(tabIdJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetTabPreview(ptr, tabIdJson)
    }

    fun freePreviewData() {
        nativeFreePreviewData()
    }

    // === Spaces ===

    fun getActiveSpaceId(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetActiveSpaceId(ptr)
    }

    fun createSpace(name: String, colorJson: String, profileId: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeCreateSpace(ptr, name, colorJson, profileId)
    }

    fun deleteSpace(spaceIdJson: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeDeleteSpace(ptr, spaceIdJson)
    }

    fun renameSpace(spaceIdJson: String, name: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeRenameSpace(ptr, spaceIdJson, name)
    }

    fun recolorSpace(spaceIdJson: String, colorJson: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeRecolorSpace(ptr, spaceIdJson, colorJson)
    }

    fun reorderSpace(spaceIdJson: String, from: Long, to: Long) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeReorderSpace(ptr, spaceIdJson, from, to)
    }

    fun activateSpace(spaceIdJson: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeActivateSpace(ptr, spaceIdJson)
    }

    // === Bookmarks ===

    fun getBookmarks(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetBookmarks(ptr)
    }

    fun addBookmark(url: String, title: String, folderId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeAddBookmark(ptr, url, title, folderId)
    }

    fun removeBookmark(bookmarkId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeRemoveBookmark(ptr, bookmarkId)
    }

    fun moveBookmark(bookmarkId: String, folderId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeMoveBookmark(ptr, bookmarkId, folderId)
    }

    fun searchBookmarks(query: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeSearchBookmarks(ptr, query)
    }

    fun createBookmarkFolder(name: String, parentId: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeCreateBookmarkFolder(ptr, name, parentId)
    }

    fun deleteBookmarkFolder(folderId: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeDeleteBookmarkFolder(ptr, folderId)
    }

    // === History ===

    fun searchHistory(query: String, limit: Long): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeSearchHistory(ptr, query, limit)
    }

    fun searchHistoryPaginated(query: String, limit: Long, offset: Long): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeSearchHistoryPaginated(ptr, query, limit, offset)
    }

    fun getHistoryGroupedByDate(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetHistoryGroupedByDate(ptr)
    }

    fun addHistoryEntry(url: String, title: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeAddHistoryEntry(ptr, url, title)
    }

    fun deleteHistoryEntry(entryId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeDeleteHistoryEntry(ptr, entryId)
    }

    fun clearHistory() {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeClearHistory(ptr)
    }

    // === Search Engines ===

    fun getSearchEngines(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetSearchEngines(ptr)
    }

    // === Content Blocker ===

    fun compileContentRules(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeCompileContentRules(ptr)
    }

    fun getContentRules(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetContentRules(ptr)
    }

    fun getFilterLists(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetFilterLists(ptr)
    }

    fun addFilterList(id: String, name: String, url: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeAddFilterList(ptr, id, name, url)
    }

    fun updateFilterListContent(id: String, content: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeUpdateFilterListContent(ptr, id, content)
    }

    fun rebuildContentRules() {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeRebuildContentRules(ptr)
    }

    fun getContentRuleCount(): Long {
        val ptr = corePtr
        if (ptr == 0L) return 0
        return nativeGetContentRuleCount(ptr)
    }

    // === Air Traffic Control ===

    fun getAtcRules(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetAtcRules(ptr)
    }

    fun addAtcRule(ruleJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeAddAtcRule(ptr, ruleJson)
    }

    fun removeAtcRule(ruleId: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeRemoveAtcRule(ptr, ruleId)
    }

    fun toggleAtcRule(ruleId: String, enabled: Boolean) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeToggleAtcRule(ptr, ruleId, enabled)
    }

    // === Reader Mode ===

    fun toggleReaderMode(tabIdJson: String): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeToggleReaderMode(ptr, tabIdJson)
    }

    fun isReaderMode(tabIdJson: String): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeIsReaderMode(ptr, tabIdJson)
    }

    fun getReaderSettings(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetReaderSettings(ptr)
    }

    // === Find in Page ===

    fun startFind(tabIdJson: String, query: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeStartFind(ptr, tabIdJson, query)
    }

    fun findNext(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeFindNext(ptr)
    }

    fun findPrevious(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeFindPrevious(ptr)
    }

    fun dismissFind() {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeDismissFind(ptr)
    }

    // === Downloads ===

    fun startDownload(url: String, filename: String, contentType: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeStartDownload(ptr, url, filename, contentType)
    }

    fun updateDownloadProgress(downloadId: String, bytesReceived: Long, totalBytes: Long) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeUpdateDownloadProgress(ptr, downloadId, bytesReceived, totalBytes)
    }

    fun pauseDownload(downloadId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativePauseDownload(ptr, downloadId)
    }

    fun resumeDownload(downloadId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeResumeDownload(ptr, downloadId)
    }

    fun cancelDownload(downloadId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeCancelDownload(ptr, downloadId)
    }

    fun removeDownload(downloadId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeRemoveDownload(ptr, downloadId)
    }

    // === Zoom ===

    fun getZoom(site: String): Double {
        val ptr = corePtr
        if (ptr == 0L) return 1.0
        return nativeGetZoom(ptr, site)
    }

    fun setZoom(site: String, zoom: Double) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetZoom(ptr, site, zoom)
    }

    // === Notifications ===

    fun getNotifications(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetNotifications(ptr)
    }

    fun dismissNotification(notificationId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeDismissNotification(ptr, notificationId)
    }

    fun dismissAllNotifications() {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeDismissAllNotifications(ptr)
    }

    fun setNotificationFilter(origin: String, allowed: Boolean) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSetNotificationFilter(ptr, origin, allowed)
    }

    fun getUnreadCount(): Long {
        val ptr = corePtr
        if (ptr == 0L) return 0
        return nativeGetUnreadCount(ptr)
    }

    // === Toolbar ===

    fun getToolbarItems(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetToolbarItems(ptr)
    }

    fun setToolbarItems(itemsJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeSetToolbarItems(ptr, itemsJson)
    }

    fun getDefaultToolbarItems(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetDefaultToolbarItems(ptr)
    }

    // === Sync ===

    fun startSync(serverUrl: String, syncKey: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeStartSync(ptr, serverUrl, syncKey)
    }

    fun stopSync() {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeStopSync(ptr)
    }

    fun getSyncStatus(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetSyncStatus(ptr)
    }

    fun getConnectedDevices(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetConnectedDevices(ptr)
    }

    fun sendTab(url: String, title: String, targetDeviceId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSendTab(ptr, url, title, targetDeviceId)
    }

    fun generateSyncKey(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGenerateSyncKey(ptr)
    }

    fun generateSyncBootstrap(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGenerateSyncBootstrap(ptr)
    }

    fun joinSync(recoveryPhrase: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeJoinSync(ptr, recoveryPhrase)
    }

    fun configureSyncBootstrap(serverUrl: String, bootstrapSeed: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeConfigureSyncBootstrap(ptr, serverUrl, bootstrapSeed)
    }

    fun removeSyncDevice(deviceId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeRemoveSyncDevice(ptr, deviceId)
    }

    fun applyRemoteEntities(entitiesJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeApplyRemoteEntities(ptr, entitiesJson)
    }

    fun drainOutgoing(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeDrainOutgoing(ptr)
    }

    fun getSyncRoomId(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetSyncRoomId(ptr)
    }

    fun leaseSyncOutgoingEnvelopes(limit: Int): String? {
        val ptr = corePtr
        if (ptr == 0L || limit <= 0) return null
        return nativeLeaseSyncOutgoingEnvelopes(ptr)
    }

    fun acknowledgeSyncEnvelope(ackJson: String): Boolean {
        val ptr = corePtr
        return ptr != 0L && nativeAcknowledgeSyncEnvelope(ptr, ackJson)
    }

    fun getSyncReceiveCursor(roomId: String): Long {
        val ptr = corePtr
        return if (ptr == 0L) 0L else nativeGetSyncReceiveCursor(ptr, roomId)
    }

    fun applySyncEnvelopeAndAdvanceCursor(roomId: String, envelopeJson: String): Boolean {
        val ptr = corePtr
        return ptr != 0L && nativeApplySyncEnvelopeAndAdvanceCursor(ptr, roomId, envelopeJson)
    }

    fun reportSyncTransportState(reportJson: String) {
        val ptr = corePtr
        if (ptr != 0L) nativeReportSyncTransportState(ptr, reportJson)
    }

    fun queryFilterDecision(url: String, sourceUrl: String, requestType: String): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeQueryFilterDecision(ptr, url, sourceUrl, requestType)
    }

    // === Profiles ===

    fun getActiveProfileId(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetActiveProfileId(ptr)
    }

    fun listProfiles(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeListProfiles(ptr)
    }

    fun createProfile(name: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeCreateProfile(ptr, name)
    }

    fun deleteProfile(profileId: String): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeDeleteProfile(ptr, profileId)
    }

    fun getProfileDataStoreId(profileId: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetProfileDataStoreId(ptr, profileId)
    }

    fun switchProfile(profileId: String): Boolean {
        val ptr = corePtr
        if (ptr == 0L) return false
        return nativeSwitchProfile(ptr, profileId)
    }

    // === Command Bar ===

    fun recordUsage(itemKey: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeRecordUsage(ptr, itemKey)
    }

    fun getTopUsed(limit: Long): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetTopUsed(ptr, limit)
    }

    fun getSearchableItems(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetSearchableItems(ptr)
    }

    fun getRecentSearches(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetRecentSearches(ptr)
    }

    fun saveSearch(query: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSaveSearch(ptr, query)
    }

    // === Notes ===

    fun linkNoteToTab(noteId: String, tabId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeLinkNoteToTab(ptr, noteId, tabId)
    }

    fun linkNoteToUrl(noteId: String, url: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeLinkNoteToUrl(ptr, noteId, url)
    }

    fun unlinkNoteFromTab(noteId: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeUnlinkNoteFromTab(ptr, noteId)
    }

    fun getLinkedTabId(noteId: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetLinkedTabId(ptr, noteId)
    }

    fun exportNotes(format: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeExportNotes(ptr, format)
    }

    fun exportSingleNote(noteId: String, format: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeExportSingleNote(ptr, noteId, format)
    }

    fun searchNotesFts(query: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeSearchNotesFts(ptr, query)
    }

    // === Reading List ===

    fun toggleReadingListRead(itemId: String): Long {
        val ptr = corePtr
        if (ptr == 0L) return -1
        return nativeToggleReadingListRead(ptr, itemId)
    }

    fun getReadingList(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetReadingList(ptr)
    }

    // === Autofill ===

    fun saveFormData(tabId: String, formJson: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeSaveFormData(ptr, tabId, formJson)
    }

    // === Shortcuts ===

    fun getShortcuts(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetShortcuts(ptr)
    }

    fun setShortcut(action: String, keyComboJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeSetShortcut(ptr, action, keyComboJson)
    }

    fun resetShortcut(action: String) {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeResetShortcut(ptr, action)
    }

    fun resetAllShortcuts() {
        val ptr = corePtr
        if (ptr == 0L) return
        nativeResetAllShortcuts(ptr)
    }

    fun resolveShortcut(keyComboJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeResolveShortcut(ptr, keyComboJson)
    }

    // === Backups ===

    fun createBackup(configJson: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeCreateBackup(ptr, configJson)
    }

    fun restoreBackup(backupB64: String, password: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeRestoreBackup(ptr, backupB64, password)
    }

    fun getBackupHistory(): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeGetBackupHistory(ptr)
    }

    // === Import/Export ===

    fun detectBrowserProfiles(): String? {
        return nativeDetectBrowserProfiles()
    }

    fun importChromeBookmarks(profilePath: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeImportChromeBookmarks(ptr, profilePath)
    }

    fun importFirefoxBookmarks(profilePath: String): String? {
        val ptr = corePtr
        if (ptr == 0L) return null
        return nativeImportFirefoxBookmarks(ptr, profilePath)
    }

    fun provisionSqlcipherKey(): Boolean {
        val hex = BridgeAi.getOrCreateSqlcipherKeyHex() ?: return false
        return nativeSetSqlcipherKey(hex)
    }

    fun provisionAssets(context: android.content.Context, force: Boolean = false): Boolean {
        return dev.maho.browser.support.AssetProvisioner.provisionAssetsIfNeeded(context, force)
    }

    // --- JVM callback bridges for JNI ---
    @JvmStatic
    fun requestAgentPermission(toolName: String, arguments: String): Int {
        if (toolName == "shell_exec") return 1
        if (toolName == "fs_read" || toolName == "web_search" || toolName == "view_file" || toolName == "list_dir") {
            return 0
        }
        return 1
    }

    @JvmStatic
    fun getAgentSecureStorage(provider: String): String? = BridgeAi.getAgentSecureStorage(provider)

    // --- Agent sessions ---
    fun agentCreateSession(sessionId: String): Long = BridgeAgent.agentCreateSession(sessionId)
    fun agentFreeSession(sessionPtr: Long) = BridgeAgent.agentFreeSession(sessionPtr)
    fun agentSendMessage(sessionPtr: Long, message: String): Boolean = BridgeAgent.agentSendMessage(sessionPtr, message)
    fun agentCancel(sessionPtr: Long): Boolean = BridgeAgent.agentCancel(sessionPtr)
    fun agentListTools(sessionPtr: Long): String? = BridgeAgent.agentListTools(sessionPtr)
    fun agentPollEvent(sessionPtr: Long): String? = BridgeAgent.agentPollEvent(sessionPtr)
    fun agentListArtifacts(sessionHandle: Long): String? = BridgeAgent.agentListArtifacts(sessionHandle)
    fun agentReadArtifact(sessionHandle: Long, artifactId: String): ByteArray? =
        BridgeAgent.agentReadArtifact(sessionHandle, artifactId)
    internal fun agentInstallDeterministicFsWriteModel(sessionHandle: Long): Boolean =
        BridgeAgent.agentInstallDeterministicFsWriteModel(sessionHandle)
    internal fun agentTestCompletionGeneration(sessionHandle: Long): Long =
        BridgeAgent.agentTestCompletionGeneration(sessionHandle)
    internal fun agentTestAwaitCompletion(sessionHandle: Long, generation: Long, timeoutMs: Long): Boolean =
        BridgeAgent.agentTestAwaitCompletion(sessionHandle, generation, timeoutMs)
    internal fun agentTestEnqueueArtifactCreatedTwice(sessionHandle: Long, artifactId: String): Boolean =
        BridgeAgent.agentTestEnqueueArtifactCreatedTwice(sessionHandle, artifactId)
    internal fun agentTestSeedArtifact(
        sessionHandle: Long,
        artifactId: String,
        storageRelPath: String,
        displayName: String,
        mimeType: String,
    ): Boolean = BridgeAgent.agentTestSeedArtifact(
        sessionHandle,
        artifactId,
        storageRelPath,
        displayName,
        mimeType,
    )
    fun artifactShare(sessionHandle: Long, artifactId: String): Boolean =
        BridgeAgent.artifactShare(sessionHandle, artifactId)

    // --- Space AI Config ---
    fun getSpaceAIConfig(spaceId: String): SpaceAIConfig? = null
    fun getSpaceAIConfigJson(spaceId: String): String? = null
    fun setSpaceAIConfig(spaceId: String, config: SpaceAIConfig): Boolean = true
    fun setSpaceAIConfig(spaceId: String, configJson: String): Boolean = true

    // --- Offline Model ---
    @kotlinx.serialization.Serializable
    data class OfflineModelInfo(val status: String, val size: Long)
    fun offlineModelInfo(): OfflineModelInfo? = null
    fun offlineModelIsDownloaded(): Boolean = false
    fun offlineModelDelete(): Boolean = true

    // ============================================================
    // Native extern declarations
    // ============================================================
    private external fun nativeCreate(): Long
    private external fun nativeCreateWithStorage(storagePath: String): Long
    private external fun nativeDestroy(ptr: Long)
    private external fun nativeHandleEvent(ptr: Long, eventJson: String): String?
    private external fun nativeTick(ptr: Long): String?
    private external fun nativeSaveState(ptr: Long): Boolean
    private external fun nativeLoadState(ptr: Long): Boolean
    private external fun nativeGetTabViewModels(ptr: Long): String?
    private external fun nativeGetSpaceViewModels(ptr: Long): String?
    private external fun nativeGetFolderViewModels(ptr: Long, spaceIdJson: String): String?
    private external fun nativeGetDownloadViewModels(ptr: Long): String?
    private external fun nativeGetNoteViewModels(ptr: Long): String?
    private external fun nativeGetSettings(ptr: Long): String?
    private external fun nativeUpdateSettings(ptr: Long, settingsJson: String)
    private external fun nativeGetSiteSearchEntries(ptr: Long): String?
    private external fun nativeSetSiteSearchEntries(ptr: Long, json: String)
    private external fun nativeSetDensity(ptr: Long, density: String)
    private external fun nativeSetCustomChromeCss(ptr: Long, css: String)
    private external fun nativeSetWindowTransparency(ptr: Long, enabled: Boolean)
    private external fun nativeSetAppIcon(ptr: Long, path: String)
    private external fun nativeSetCrashSaveInterval(ptr: Long, seconds: Long)
    private external fun nativeGetPinnedTabs(ptr: Long, spaceIdJson: String): String?
    private external fun nativeGetTodayTabs(ptr: Long, spaceIdJson: String): String?
    private external fun nativeGetArchivedTabs(ptr: Long, spaceIdJson: String): String?
    private external fun nativeGetFavoriteTabs(ptr: Long, spaceIdJson: String): String?
    private external fun nativeFavoriteTab(ptr: Long, tabIdJson: String)
    private external fun nativeTransitionTabRole(ptr: Long, tabIdJson: String, newRoleJson: String)
    private external fun nativeSetTabParent(ptr: Long, tabIdJson: String, newParentIdJson: String)
    private external fun nativeResetPinnedTab(ptr: Long, tabId: String): String?
    private external fun nativeCheckPinnedNavigation(ptr: Long, tabId: String, url: String): String?
    private external fun nativeSchedulePreviewCapture(ptr: Long, tabIdJson: String): Boolean
    private external fun nativeTakePendingPreviewCaptures(ptr: Long): String?
    private external fun nativeHasTabPreview(ptr: Long, tabIdJson: String): Boolean
    private external fun nativeUpdateTabPreview(ptr: Long, tabIdJson: String, data: ByteArray)
    private external fun nativeGetTabPreview(ptr: Long, tabIdJson: String): String?
    private external fun nativeFreePreviewData()
    private external fun nativeGetActiveSpaceId(ptr: Long): String?
    private external fun nativeCreateSpace(ptr: Long, name: String, colorJson: String, profileId: String): String?
    private external fun nativeDeleteSpace(ptr: Long, spaceIdJson: String)
    private external fun nativeRenameSpace(ptr: Long, spaceIdJson: String, name: String)
    private external fun nativeRecolorSpace(ptr: Long, spaceIdJson: String, colorJson: String)
    private external fun nativeReorderSpace(ptr: Long, spaceIdJson: String, from: Long, to: Long)
    private external fun nativeActivateSpace(ptr: Long, spaceIdJson: String)
    private external fun nativeGetBookmarks(ptr: Long): String?
    private external fun nativeAddBookmark(ptr: Long, url: String, title: String, folderId: String)
    private external fun nativeRemoveBookmark(ptr: Long, bookmarkId: String)
    private external fun nativeMoveBookmark(ptr: Long, bookmarkId: String, folderId: String)
    private external fun nativeSearchBookmarks(ptr: Long, query: String): String?
    private external fun nativeCreateBookmarkFolder(ptr: Long, name: String, parentId: String): String?
    private external fun nativeDeleteBookmarkFolder(ptr: Long, folderId: String): String?
    private external fun nativeSearchHistory(ptr: Long, query: String, limit: Long): String?
    private external fun nativeSearchHistoryPaginated(ptr: Long, query: String, limit: Long, offset: Long): String?
    private external fun nativeGetHistoryGroupedByDate(ptr: Long): String?
    private external fun nativeAddHistoryEntry(ptr: Long, url: String, title: String)
    private external fun nativeDeleteHistoryEntry(ptr: Long, entryId: String)
    private external fun nativeClearHistory(ptr: Long)
    private external fun nativeGetSearchEngines(ptr: Long): String?
    private external fun nativeCompileContentRules(ptr: Long): String?
    private external fun nativeGetContentRules(ptr: Long): String?
    private external fun nativeGetFilterLists(ptr: Long): String?
    private external fun nativeAddFilterList(ptr: Long, id: String, name: String, url: String)
    private external fun nativeUpdateFilterListContent(ptr: Long, id: String, content: String)
    private external fun nativeRebuildContentRules(ptr: Long)
    private external fun nativeGetContentRuleCount(ptr: Long): Long
    private external fun nativeGetAtcRules(ptr: Long): String?
    private external fun nativeAddAtcRule(ptr: Long, ruleJson: String): String?
    private external fun nativeRemoveAtcRule(ptr: Long, ruleId: String): String?
    private external fun nativeToggleAtcRule(ptr: Long, ruleId: String, enabled: Boolean)
    private external fun nativeToggleReaderMode(ptr: Long, tabIdJson: String): Boolean
    private external fun nativeIsReaderMode(ptr: Long, tabIdJson: String): Boolean
    private external fun nativeGetReaderSettings(ptr: Long): String?
    private external fun nativeStartFind(ptr: Long, tabIdJson: String, query: String): String?
    private external fun nativeFindNext(ptr: Long): String?
    private external fun nativeFindPrevious(ptr: Long): String?
    private external fun nativeDismissFind(ptr: Long)
    private external fun nativeStartDownload(ptr: Long, url: String, filename: String, contentType: String): String?
    private external fun nativeUpdateDownloadProgress(ptr: Long, downloadId: String, bytesReceived: Long, totalBytes: Long)
    private external fun nativePauseDownload(ptr: Long, downloadId: String)
    private external fun nativeResumeDownload(ptr: Long, downloadId: String)
    private external fun nativeCancelDownload(ptr: Long, downloadId: String)
    private external fun nativeRemoveDownload(ptr: Long, downloadId: String)
    private external fun nativeGetZoom(ptr: Long, site: String): Double
    private external fun nativeSetZoom(ptr: Long, site: String, zoom: Double)
    private external fun nativeGetNotifications(ptr: Long): String?
    private external fun nativeDismissNotification(ptr: Long, notificationId: String)
    private external fun nativeDismissAllNotifications(ptr: Long)
    private external fun nativeSetNotificationFilter(ptr: Long, origin: String, allowed: Boolean)
    private external fun nativeGetUnreadCount(ptr: Long): Long
    private external fun nativeGetToolbarItems(ptr: Long): String?
    private external fun nativeSetToolbarItems(ptr: Long, itemsJson: String): String?
    private external fun nativeGetDefaultToolbarItems(ptr: Long): String?
    private external fun nativeStartSync(ptr: Long, serverUrl: String, syncKey: String)
    private external fun nativeStopSync(ptr: Long)
    private external fun nativeGetSyncStatus(ptr: Long): String?
    private external fun nativeGetConnectedDevices(ptr: Long): String?
    private external fun nativeSendTab(ptr: Long, url: String, title: String, targetDeviceId: String)
    private external fun nativeGenerateSyncKey(ptr: Long): String?
    private external fun nativeGenerateSyncBootstrap(ptr: Long): String?
    private external fun nativeJoinSync(ptr: Long, recoveryPhrase: String): String?
    private external fun nativeConfigureSyncBootstrap(
        ptr: Long,
        serverUrl: String,
        bootstrapSeed: String,
    ): String?
    private external fun nativeRemoveSyncDevice(ptr: Long, deviceId: String)
    private external fun nativeApplyRemoteEntities(ptr: Long, entitiesJson: String): String?
    private external fun nativeDrainOutgoing(ptr: Long): String?
    private external fun nativeGetSyncRoomId(ptr: Long): String?
    private external fun nativeLeaseSyncOutgoingEnvelopes(ptr: Long): String?
    private external fun nativeAcknowledgeSyncEnvelope(ptr: Long, ackJson: String): Boolean
    private external fun nativeGetSyncReceiveCursor(ptr: Long, roomId: String): Long
    private external fun nativeApplySyncEnvelopeAndAdvanceCursor(
        ptr: Long,
        roomId: String,
        envelopeJson: String,
    ): Boolean
    private external fun nativeReportSyncTransportState(ptr: Long, reportJson: String)
    private external fun nativeQueryFilterDecision(ptr: Long, url: String, sourceUrl: String, requestType: String): Boolean
    private external fun nativeGetActiveProfileId(ptr: Long): String?
    private external fun nativeListProfiles(ptr: Long): String?
    private external fun nativeCreateProfile(ptr: Long, name: String): String?
    private external fun nativeDeleteProfile(ptr: Long, profileId: String): Boolean
    private external fun nativeGetProfileDataStoreId(ptr: Long, profileId: String): String?
    private external fun nativeSwitchProfile(ptr: Long, profileId: String): Boolean
    private external fun nativeRecordUsage(ptr: Long, itemKey: String)
    private external fun nativeGetTopUsed(ptr: Long, limit: Long): String?
    private external fun nativeGetSearchableItems(ptr: Long): String?
    private external fun nativeGetRecentSearches(ptr: Long): String?
    private external fun nativeSaveSearch(ptr: Long, query: String)
    private external fun nativeLinkNoteToTab(ptr: Long, noteId: String, tabId: String)
    private external fun nativeLinkNoteToUrl(ptr: Long, noteId: String, url: String)
    private external fun nativeUnlinkNoteFromTab(ptr: Long, noteId: String)
    private external fun nativeGetLinkedTabId(ptr: Long, noteId: String): String?
    private external fun nativeExportNotes(ptr: Long, format: String): String?
    private external fun nativeExportSingleNote(ptr: Long, noteId: String, format: String): String?
    private external fun nativeSearchNotesFts(ptr: Long, query: String): String?
    private external fun nativeToggleReadingListRead(ptr: Long, itemId: String): Long
    private external fun nativeGetReadingList(ptr: Long): String?
    private external fun nativeSaveFormData(ptr: Long, tabId: String, formJson: String)
    private external fun nativeGetShortcuts(ptr: Long): String?
    private external fun nativeSetShortcut(ptr: Long, action: String, keyComboJson: String): String?
    private external fun nativeResetShortcut(ptr: Long, action: String)
    private external fun nativeResetAllShortcuts(ptr: Long)
    private external fun nativeResolveShortcut(ptr: Long, keyComboJson: String): String?
    private external fun nativeCreateBackup(ptr: Long, configJson: String): String?
    private external fun nativeRestoreBackup(ptr: Long, backupB64: String, password: String): String?
    private external fun nativeGetBackupHistory(ptr: Long): String?
    private external fun nativeDetectBrowserProfiles(): String?
    private external fun nativeImportChromeBookmarks(ptr: Long, profilePath: String): String?
    private external fun nativeImportFirefoxBookmarks(ptr: Long, profilePath: String): String?
    internal fun invokeAgentCreateSession(corePtr: Long, sessionId: String, artifactRoot: String): Long =
        nativeAgentCreateSession(corePtr, sessionId, artifactRoot)
    internal fun invokeAgentFreeSession(sessionPtr: Long) = nativeAgentFreeSession(sessionPtr)
    internal fun invokeAgentSendMessage(sessionPtr: Long, message: String): Boolean =
        nativeAgentSendMessage(sessionPtr, message)
    internal fun invokeAgentCancel(sessionPtr: Long): Boolean = nativeAgentCancel(sessionPtr)
    internal fun invokeAgentListTools(sessionPtr: Long): String? = nativeAgentListTools(sessionPtr)
    internal fun invokeAgentListArtifacts(sessionHandle: Long): String? = nativeAgentListArtifacts(sessionHandle)
    internal fun invokeAgentReadArtifact(sessionHandle: Long, artifactId: String): ByteArray? =
        nativeAgentReadArtifact(sessionHandle, artifactId)
    internal fun invokeAgentInstallDeterministicFsWriteModel(sessionHandle: Long): Boolean =
        nativeAgentInstallDeterministicFsWriteModel(sessionHandle)
    internal fun invokeAgentTestCompletionGeneration(sessionHandle: Long): Long =
        nativeAgentTestCompletionGeneration(sessionHandle)
    internal fun invokeAgentTestAwaitCompletion(sessionHandle: Long, generation: Long, timeoutMs: Long): Boolean =
        nativeAgentTestAwaitCompletion(sessionHandle, generation, timeoutMs)
    internal fun invokeAgentTestEnqueueArtifactCreatedTwice(sessionHandle: Long, artifactId: String): Boolean =
        nativeAgentTestEnqueueArtifactCreatedTwice(sessionHandle, artifactId)
    internal fun invokeAgentTestSeedArtifact(
        sessionHandle: Long,
        artifactId: String,
        storageRelPath: String,
        displayName: String,
        mimeType: String,
    ): Boolean = nativeAgentTestSeedArtifact(sessionHandle, artifactId, storageRelPath, displayName, mimeType)
    internal fun invokeAgentPollEvent(sessionPtr: Long): String? = nativeAgentPollEvent(sessionPtr)

    internal fun invokeCreateConversation(
        ptr: Long,
        id: String,
        title: String,
        spaceId: String,
        model: String,
    ): Boolean = nativeCreateConversation(ptr, id, title, spaceId, model)
    internal fun invokeListConversations(ptr: Long, limit: Long): String? = nativeListConversations(ptr, limit)
    internal fun invokeListConversationsV2(ptr: Long, queryJson: String): String? =
        nativeListConversationsV2(ptr, queryJson)
    internal fun invokeArchiveConversation(ptr: Long, id: String): Boolean = nativeArchiveConversation(ptr, id)
    internal fun invokeUnarchiveConversation(ptr: Long, id: String): Boolean = nativeUnarchiveConversation(ptr, id)
    internal fun invokeApplyConversationBulkOperation(ptr: Long, requestJson: String): String? =
        nativeApplyConversationBulkOperation(ptr, requestJson)
    internal fun invokeGetConversationAutoArchivePolicy(ptr: Long): Int =
        nativeGetConversationAutoArchivePolicy(ptr)
    internal fun invokeSetConversationAutoArchivePolicy(ptr: Long, days: Int): Boolean =
        nativeSetConversationAutoArchivePolicy(ptr, days)
    internal fun invokeGetConversationMessages(ptr: Long, sessionId: String): String? =
        nativeGetConversationMessages(ptr, sessionId)
    internal fun invokeSaveConversationMessage(
        ptr: Long,
        sessionId: String,
        role: String,
        content: String,
        urlContext: String,
    ): Boolean = nativeSaveConversationMessage(ptr, sessionId, role, content, urlContext)
    internal fun invokeDeleteConversation(ptr: Long, id: String): Boolean = nativeDeleteConversation(ptr, id)
    internal fun invokeRenameConversation(ptr: Long, id: String, title: String): Boolean =
        nativeRenameConversation(ptr, id, title)
    internal fun invokeListConversationProjects(ptr: Long): String? = nativeListConversationProjects(ptr)
    internal fun invokeCreateConversationProject(ptr: Long, name: String): String? = nativeCreateConversationProject(ptr, name)
    internal fun invokeRenameConversationProject(ptr: Long, id: String, name: String): Boolean =
        nativeRenameConversationProject(ptr, id, name)
    internal fun invokeDeleteConversationProject(ptr: Long, id: String): Boolean = nativeDeleteConversationProject(ptr, id)
    internal fun invokeMoveConversationsToProject(ptr: Long, requestJson: String): String? =
        nativeMoveConversationsToProject(ptr, requestJson)

    internal fun invokeGetComposerDraft(ptr: Long, scopeJson: String): String? =
        nativeGetComposerDraft(ptr, scopeJson)
    internal fun invokeSetComposerDraft(ptr: Long, scopeJson: String, text: String): Boolean =
        nativeSetComposerDraft(ptr, scopeJson, text)
    internal fun invokeDeleteComposerDraft(ptr: Long, scopeJson: String): Boolean =
        nativeDeleteComposerDraft(ptr, scopeJson)

    internal fun invokeChatSessionNew(
        apiKey: String,
        endpoint: String,
        model: String,
        systemInstruction: String,
    ): Long = nativeChatSessionNew(apiKey, endpoint, model, systemInstruction)
    internal fun invokeChatSessionFree(sessionPtr: Long) = nativeChatSessionFree(sessionPtr)
    internal fun invokeChatSendUserTurn(sessionPtr: Long, message: String): Boolean =
        nativeChatSendUserTurn(sessionPtr, message)
    internal fun invokeChatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean =
        nativeChatSendImage(sessionPtr, mime, data)
    internal fun invokeChatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean =
        nativeChatSendTextWithImage(sessionPtr, text, mime, data)
    internal fun invokeChatCancel(sessionPtr: Long): Boolean = nativeChatCancel(sessionPtr)
    internal fun invokeChatSessionPollEvent(sessionPtr: Long): String? = nativeChatSessionPollEvent(sessionPtr)
    internal fun invokeChatRegisterTool(
        sessionPtr: Long,
        name: String,
        description: String,
        parametersJson: String,
    ): Boolean = nativeChatRegisterTool(sessionPtr, name, description, parametersJson)
    internal fun invokeChatSendToolResult(
        sessionPtr: Long,
        toolCallId: String,
        name: String,
        output: String,
        trigger: Boolean,
    ): Boolean = nativeChatSendToolResult(sessionPtr, toolCallId, name, output, trigger)
    internal fun invokeChatAppendUserMessage(sessionPtr: Long, content: String): Boolean =
        nativeChatAppendUserMessage(sessionPtr, content)
    internal fun invokeChatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean =
        nativeChatAppendAssistantMessage(sessionPtr, content, toolCallsJson)

    private external fun nativeAgentCreateSession(corePtr: Long, sessionId: String, artifactRoot: String): Long
    private external fun nativeAgentFreeSession(sessionPtr: Long)
    private external fun nativeAgentSendMessage(sessionPtr: Long, message: String): Boolean
    private external fun nativeAgentCancel(sessionPtr: Long): Boolean
    private external fun nativeAgentListTools(sessionPtr: Long): String?
    private external fun nativeAgentListArtifacts(sessionHandle: Long): String?
    private external fun nativeAgentReadArtifact(sessionHandle: Long, artifactId: String): ByteArray?
    private external fun nativeAgentInstallDeterministicFsWriteModel(sessionHandle: Long): Boolean
    private external fun nativeAgentTestCompletionGeneration(sessionHandle: Long): Long
    private external fun nativeAgentTestAwaitCompletion(sessionHandle: Long, generation: Long, timeoutMs: Long): Boolean
    private external fun nativeAgentTestEnqueueArtifactCreatedTwice(sessionHandle: Long, artifactId: String): Boolean
    private external fun nativeAgentTestSeedArtifact(
        sessionHandle: Long,
        artifactId: String,
        storageRelPath: String,
        displayName: String,
        mimeType: String,
    ): Boolean
    private external fun nativeAgentPollEvent(sessionPtr: Long): String?
    private external fun nativeSetSqlcipherKey(keyHex: String): Boolean
    private external fun nativeCreateConversation(ptr: Long, id: String, title: String, spaceId: String, model: String): Boolean
    private external fun nativeListConversations(ptr: Long, limit: Long): String?
    private external fun nativeListConversationsV2(ptr: Long, queryJson: String): String?
    private external fun nativeArchiveConversation(ptr: Long, id: String): Boolean
    private external fun nativeUnarchiveConversation(ptr: Long, id: String): Boolean
    private external fun nativeApplyConversationBulkOperation(ptr: Long, requestJson: String): String?
    private external fun nativeGetConversationAutoArchivePolicy(ptr: Long): Int
    private external fun nativeSetConversationAutoArchivePolicy(ptr: Long, days: Int): Boolean
    private external fun nativeAutoArchiveConversations(ptr: Long, nowSeconds: Long): Int
    private external fun nativeGetConversationMessages(ptr: Long, sessionId: String): String?
    private external fun nativeSaveConversationMessage(ptr: Long, sessionId: String, role: String, content: String, urlContext: String): Boolean
    private external fun nativeDeleteConversation(ptr: Long, id: String): Boolean
    private external fun nativeRenameConversation(ptr: Long, id: String, title: String): Boolean
    private external fun nativeListConversationProjects(ptr: Long): String?
    private external fun nativeCreateConversationProject(ptr: Long, name: String): String?
    private external fun nativeRenameConversationProject(ptr: Long, id: String, name: String): Boolean
    private external fun nativeDeleteConversationProject(ptr: Long, id: String): Boolean
    private external fun nativeMoveConversationsToProject(ptr: Long, requestJson: String): String?
    private external fun nativeGetComposerDraft(ptr: Long, scopeJson: String): String?
    private external fun nativeSetComposerDraft(ptr: Long, scopeJson: String, text: String): Boolean
    private external fun nativeDeleteComposerDraft(ptr: Long, scopeJson: String): Boolean
    private external fun nativeChatSessionNew(apiKey: String, endpoint: String, model: String, systemInstruction: String): Long
    private external fun nativeChatSessionFree(sessionPtr: Long)
    private external fun nativeChatSendUserTurn(sessionPtr: Long, message: String): Boolean
    private external fun nativeChatSendImage(sessionPtr: Long, mime: String, data: ByteArray): Boolean
    private external fun nativeChatSendTextWithImage(sessionPtr: Long, text: String, mime: String, data: ByteArray): Boolean
    private external fun nativeChatCancel(sessionPtr: Long): Boolean
    private external fun nativeChatSessionPollEvent(sessionPtr: Long): String?
    private external fun nativeChatRegisterTool(sessionPtr: Long, name: String, description: String, parametersJson: String): Boolean
    private external fun nativeChatSendToolResult(sessionPtr: Long, toolCallId: String, name: String, output: String, trigger: Boolean): Boolean
    private external fun nativeChatAppendUserMessage(sessionPtr: Long, content: String): Boolean
    private external fun nativeChatAppendAssistantMessage(sessionPtr: Long, content: String, toolCallsJson: String): Boolean
}
