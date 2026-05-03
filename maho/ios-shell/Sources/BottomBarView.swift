import SwiftUI

struct BottomBarView: View {
    @ObservedObject var state: BrowserState
    @Binding var showMenu: Bool
    @Binding var showTabSwitcher: Bool
    @Binding var showContentBlocker: Bool

    var body: some View {
        HStack(spacing: 16) {
            Button(action: { state.goBack() }) {
                Image(systemName: "arrow.backward")
            }
            .disabled(!state.canGoBack)
            .accessibilityIdentifier("backButton")

            Button(action: { state.goForward() }) {
                Image(systemName: "arrow.forward")
            }
            .disabled(!state.canGoForward)
            .accessibilityIdentifier("forwardButton")

            Button(action: { state.showCommandBar = true }) {
                Text(state.currentUrl.isEmpty ? "Search or enter address" : state.currentUrl)
                    .lineLimit(1)
                    .frame(maxWidth: .infinity)
            }
            .accessibilityIdentifier("addressBarButton")

            Button(action: { showTabSwitcher = true }) {
                Image(systemName: "square.on.square")
            }
            .accessibilityIdentifier("tabsButton")

            Menu {
                Button("Bookmarks") { state.showBookmarks = true }
                Button("History") { state.showHistory = true }
                Button("Downloads") { state.showDownloads = true }
                Button("Archive") { state.showArchive = true }
                Button("Content Blocker") { showContentBlocker = true }
                Button("Settings") { state.showSettings = true }
            } label: {
                Image(systemName: "ellipsis.circle")
            }
            .accessibilityIdentifier("menuButton")
        }
        .padding()
        .background(.ultraThinMaterial)
        .onChange(of: state.currentUrl) { oldValue, newValue in
            if newValue != oldValue {
                state.refreshState()
            }
        }
    }
}
