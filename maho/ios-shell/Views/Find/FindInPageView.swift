import SwiftUI
import LucideIcons

struct FindInPageView: View {
    let tabId: TabId
    @Binding var isVisible: Bool
    var isIncognito: Bool = false

    @Environment(\.colorScheme) private var systemColorScheme

    @State private var query: String = ""
    @State private var activeMatch: UInt32 = 0
    @State private var totalMatches: UInt32 = 0

    var body: some View {
        if isVisible {
            HStack(spacing: 12) {
                TextField("Find in page", text: $query)
                    .textFieldStyle(.roundedBorder)
                    .frame(maxWidth: 200)
                    .onChange(of: query) { _, newValue in
                        performFind(newValue)
                    }

                Text(resultLabel)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .monospacedDigit()

                HStack(spacing: 8) {
                    Button {
                        findPrevious()
                    } label: {
                        Image(lucide: Lucide.chevronUp)
                            .frame(minWidth: ShellTheme.Size.touchTarget, minHeight: ShellTheme.Size.touchTarget)
                            .contentShape(Rectangle())
                    }
                    .disabled(totalMatches == 0)
                    .accessibilityLabel("Previous match")

                    Button {
                        findNext()
                    } label: {
                        Image(lucide: Lucide.chevronDown)
                            .frame(minWidth: ShellTheme.Size.touchTarget, minHeight: ShellTheme.Size.touchTarget)
                            .contentShape(Rectangle())
                    }
                    .disabled(totalMatches == 0)
                    .accessibilityLabel("Next match")

                    Button {
                        dismissFind()
                    } label: {
                        Image(lucide: Lucide.x)
                            .frame(minWidth: ShellTheme.Size.touchTarget, minHeight: ShellTheme.Size.touchTarget)
                            .contentShape(Rectangle())
                    }
                    .accessibilityLabel("Close find in page")
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(findBarBackground)
            .clipShape(RoundedRectangle(cornerRadius: 12))
            .shadow(color: .black.opacity(0.1), radius: 4, x: 0, y: 2)
             .padding(.horizontal)
             .transition(.move(edge: .top).combined(with: .opacity))
             .environment(\.colorScheme, isIncognito ? .dark : systemColorScheme)
             .onAppear {
                 syncFromBridge()
             }
             .onChange(of: tabId) { _, _ in
                 syncFromBridge()
             }
         }
     }

    private var resultLabel: String {
        guard totalMatches > 0 else { return "0 of 0" }
        return "\(activeMatch + 1) of \(totalMatches)"
    }

    @ViewBuilder
    private var findBarBackground: some View {
        if isIncognito {
            ShellTheme.Palette.incognitoBackground
        } else {
            Rectangle().fill(.ultraThinMaterial)
        }
    }

    // MARK: - Actions

    private func performFind(_ newQuery: String) {
        if let state = MahoBridge.shared.startFind(
            tabId: tabId,
            query: newQuery,
            caseSensitive: false,
            wholeWord: false
        ) {
            activeMatch = state.activeIndex
            totalMatches = state.matchCount
        }
    }

    private func findNext() {
        if let state = MahoBridge.shared.findNext() {
            activeMatch = state.activeIndex
            totalMatches = state.matchCount
        }
    }

    private func findPrevious() {
        if let state = MahoBridge.shared.findPrevious() {
            activeMatch = state.activeIndex
            totalMatches = state.matchCount
        }
    }

    private func dismissFind() {
        MahoBridge.shared.dismissFind()
        isVisible = false
        query = ""
        activeMatch = 0
        totalMatches = 0
    }

    private func syncFromBridge() {
        if let state = MahoBridge.shared.getFindBarState() {
            query = state.query
            activeMatch = state.activeIndex
            totalMatches = state.matchCount
        }
    }
}
