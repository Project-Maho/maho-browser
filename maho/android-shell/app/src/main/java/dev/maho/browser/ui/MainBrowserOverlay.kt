package dev.maho.browser.ui

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import dev.maho.browser.models.TabId
import dev.maho.browser.ui.library.ArchiveScreen
import dev.maho.browser.ui.library.BookmarksScreen
import dev.maho.browser.ui.library.DownloadsScreen
import dev.maho.browser.ui.library.HistoryScreen
import dev.maho.browser.ui.library.NotesScreen
import dev.maho.browser.ui.library.ReadingListScreen
import dev.maho.browser.ui.profiles.ProfileSettingsScreen
import dev.maho.browser.ui.settings.AdvancedSettingsScreen
import dev.maho.browser.ui.settings.AppIconPickerScreen
import dev.maho.browser.ui.settings.AppearanceSettingsScreen
import dev.maho.browser.ui.settings.AutofillSettingsScreen
import dev.maho.browser.ui.settings.FeedbackScreen
import dev.maho.browser.ui.settings.GeneralSettingsScreen
import dev.maho.browser.ui.settings.LanguageSettingsScreen
import dev.maho.browser.ui.settings.MaxSettingsScreen
import dev.maho.browser.ui.settings.NotificationSettingsScreen
import dev.maho.browser.ui.settings.PrivacySettingsScreen
import dev.maho.browser.ui.settings.ReaderSettingsScreen
import dev.maho.browser.ui.settings.SettingsScreen
import dev.maho.browser.ui.settings.SyncSettingsScreen
import dev.maho.browser.ui.theme.BrowserShellTheme

internal enum class BrowserOverlay {
    Settings, GeneralSettings, AppearanceSettings, PrivacySettings, ReaderSettings,
    AutofillSettings, NotificationSettings, MaxSettings, ApiKeys, AdvancedSettings, Profiles,
    Bookmarks, History, Archive, Downloads, Notes, ReadingList, AppIconPicker,
    Feedback, LanguageSettings, SyncSettings, WebAgent, Conversations,
}

@Composable
internal fun MainBrowserOverlay(
    overlay: BrowserOverlay,
    webAgentInitialGoal: String?,
    selectedSearchEngine: String,
    onPush: (BrowserOverlay) -> Unit,
    onPop: () -> Unit,
    onClear: () -> Unit,
    onRefresh: () -> Unit,
    onSubmitSearch: (String) -> Unit,
    onOpenArchivedTab: (TabId) -> Unit,
    onSelectSearchEngine: (String) -> Unit,
    onClearBrowsingData: () -> Unit,
) {
    Surface(
        modifier = Modifier.fillMaxSize().safeDrawingPadding(),
        color = BrowserShellTheme.colors.overlayBackground,
    ) {
        when (overlay) {
            BrowserOverlay.Settings -> SettingsScreen(
                onNavigateToGeneral = { onPush(BrowserOverlay.GeneralSettings) },
                onNavigateToAppearance = { onPush(BrowserOverlay.AppearanceSettings) },
                onNavigateToPrivacy = { onPush(BrowserOverlay.PrivacySettings) },
                onNavigateToReader = { onPush(BrowserOverlay.ReaderSettings) },
                onNavigateToAutofill = { onPush(BrowserOverlay.AutofillSettings) },
                onNavigateToNotifications = { onPush(BrowserOverlay.NotificationSettings) },
                onNavigateToMax = { onPush(BrowserOverlay.MaxSettings) },
                onNavigateToWebAgent = { onPush(BrowserOverlay.WebAgent) },
                onNavigateToApiKeys = { onPush(BrowserOverlay.ApiKeys) },
                onNavigateToAdvanced = { onPush(BrowserOverlay.AdvancedSettings) },
                onNavigateToProfiles = { onPush(BrowserOverlay.Profiles) },
                onNavigateToBookmarks = { onPush(BrowserOverlay.Bookmarks) },
                onNavigateToHistory = { onPush(BrowserOverlay.History) },
                onNavigateToDownloads = { onPush(BrowserOverlay.Downloads) },
                onNavigateToNotes = { onPush(BrowserOverlay.Notes) },
                onNavigateToReadingList = { onPush(BrowserOverlay.ReadingList) },
                onNavigateToAppIcon = { onPush(BrowserOverlay.AppIconPicker) },
                onNavigateToFeedback = { onPush(BrowserOverlay.Feedback) },
                onNavigateToLanguage = { onPush(BrowserOverlay.LanguageSettings) },
                onNavigateToSync = { onPush(BrowserOverlay.SyncSettings) },
                onResetSettings = { dev.maho.browser.bridge.BridgeSettings.resetAllSettings() },
                selectedSearchEngine = selectedSearchEngine,
                onSelectSearchEngine = onSelectSearchEngine,
                onClearBrowsingData = onClearBrowsingData,
                onBack = onPop,
            )
            BrowserOverlay.GeneralSettings -> GeneralSettingsScreen(onBack = onPop)
            BrowserOverlay.AppearanceSettings -> AppearanceSettingsScreen(onBack = onPop)
            BrowserOverlay.PrivacySettings -> PrivacySettingsScreen(onBack = onPop)
            BrowserOverlay.ReaderSettings -> ReaderSettingsScreen(onBack = onPop)
            BrowserOverlay.AutofillSettings -> AutofillSettingsScreen(onBack = onPop)
            BrowserOverlay.NotificationSettings -> NotificationSettingsScreen(onBack = onPop)
            BrowserOverlay.MaxSettings -> MaxSettingsScreen(onBack = onPop)
            BrowserOverlay.ApiKeys -> dev.maho.browser.ui.webview.BYOKWebOverlay(onExit = onPop)
            BrowserOverlay.WebAgent -> dev.maho.browser.ui.webview.WebAgentOverlay(
                initialGoal = webAgentInitialGoal,
                onExit = onPop,
            )
            BrowserOverlay.Conversations -> dev.maho.browser.ui.webview.ConversationsWebOverlay(onExit = onPop)
            BrowserOverlay.AdvancedSettings -> AdvancedSettingsScreen(onBack = onPop)
            BrowserOverlay.Profiles -> ProfileSettingsScreen(onBack = onPop)
            BrowserOverlay.Bookmarks -> BookmarksScreen(
                onOpenUrl = { onClear(); onSubmitSearch(it) },
                onBack = onPop,
            )
            BrowserOverlay.History -> HistoryScreen(
                onOpenUrl = { onClear(); onSubmitSearch(it) },
                onBack = onPop,
            )
            BrowserOverlay.Archive -> ArchiveScreen(
                onOpenTab = { tabId -> onClear(); onRefresh(); onOpenArchivedTab(tabId) },
                onBack = onPop,
            )
            BrowserOverlay.Downloads -> DownloadsScreen(onBack = onPop)
            BrowserOverlay.Notes -> NotesScreen(onBack = onPop)
            BrowserOverlay.ReadingList -> ReadingListScreen(
                onOpenUrl = { onClear(); onSubmitSearch(it) },
                onBack = onPop,
            )
            BrowserOverlay.AppIconPicker -> AppIconPickerScreen(onBack = onPop)
            BrowserOverlay.Feedback -> FeedbackScreen(onBack = onPop)
            BrowserOverlay.LanguageSettings -> LanguageSettingsScreen(onBack = onPop)
            BrowserOverlay.SyncSettings -> SyncSettingsScreen(onBack = onPop)
        }
    }
}
