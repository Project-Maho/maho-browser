import SwiftUI

extension View {
    func mainBrowserSheets(
        showTabGrid: Binding<Bool>,
        showSettings: Binding<Bool>,
        showHistory: Binding<Bool>,
        showBookmarks: Binding<Bool>,
        showArchive: Binding<Bool>,
        showReadingList: Binding<Bool>,
        isReaderMode: Binding<Bool>,
        showDownloads: Binding<Bool>,
        showNotes: Binding<Bool>,
        showAiChat: Binding<Bool>,
        showConversations: Binding<Bool>,
        isIncognito: Binding<Bool>,
        browserVM: BrowserViewModel,
        browserState: BrowserState,
        askAIQuery: String?,
        onTabGridDismiss: @escaping () -> Void,
        onNavigate: @escaping (String) -> Void
    ) -> some View {
        modifier(
            MainBrowserSheets(
                showTabGrid: showTabGrid,
                showSettings: showSettings,
                showHistory: showHistory,
                showBookmarks: showBookmarks,
                showArchive: showArchive,
                showReadingList: showReadingList,
                isReaderMode: isReaderMode,
                showDownloads: showDownloads,
                showNotes: showNotes,
                showAiChat: showAiChat,
                showConversations: showConversations,
                isIncognito: isIncognito,
                browserVM: browserVM,
                browserState: browserState,
                askAIQuery: askAIQuery,
                onTabGridDismiss: onTabGridDismiss,
                onNavigate: onNavigate
            )
        )
    }
}

private struct MainBrowserSheets: ViewModifier {
    @Binding var showTabGrid: Bool
    @Binding var showSettings: Bool
    @Binding var showHistory: Bool
    @Binding var showBookmarks: Bool
    @Binding var showArchive: Bool
    @Binding var showReadingList: Bool
    @Binding var isReaderMode: Bool
    @Binding var showDownloads: Bool
    @Binding var showNotes: Bool
    @Binding var showAiChat: Bool
    @Binding var showConversations: Bool
    @Binding var isIncognito: Bool
    @ObservedObject var browserVM: BrowserViewModel

    let browserState: BrowserState
    let askAIQuery: String?
    let onTabGridDismiss: () -> Void
    let onNavigate: (String) -> Void

    func body(content: Content) -> some View {
        content
            .sheet(isPresented: $showTabGrid, onDismiss: onTabGridDismiss) {
                TabGridSheet(isIncognito: $isIncognito)
                    .presentationDragIndicator(.visible)
            }
            .sheet(isPresented: $showSettings) {
                SettingsRootView()
            }
            .sheet(isPresented: $showHistory) {
                HistoryView { url in
                    showHistory = false
                    onNavigate(url)
                }
            }
            .sheet(isPresented: $showBookmarks) {
                BookmarksView { url in
                    showBookmarks = false
                    onNavigate(url)
                }
            }
            .sheet(isPresented: $showArchive) {
                ArchiveView(state: browserState)
            }
            .sheet(isPresented: $showReadingList) {
                ReadingListView { url in
                    showReadingList = false
                    onNavigate(url)
                }
            }
            .fullScreenCover(isPresented: Binding(
                get: { isReaderMode && browserVM.extractedArticle != nil },
                set: { newValue in
                    isReaderMode = newValue
                    if !newValue {
                        browserVM.extractedArticle = nil
                    }
                }
            )) {
                if let article = browserVM.extractedArticle {
                    ReaderView(article: article, isPresented: $isReaderMode)
                }
            }
            .sheet(isPresented: $showDownloads) {
                DownloadsView()
            }
            .sheet(isPresented: $showNotes) {
                NotesView()
            }
            .sheet(isPresented: $showAiChat) {
                AgentWebView(query: askAIQuery ?? "")
            }
            .sheet(isPresented: $showConversations) {
                ConversationsWebView()
            }
    }
}
