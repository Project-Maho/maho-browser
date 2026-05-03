import SwiftUI

struct FindInPageView: View {
    let tabId: TabId
    @Binding var isVisible: Bool

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
                        Image(systemName: "chevron.up")
                    }
                    .disabled(totalMatches == 0)

                    Button {
                        findNext()
                    } label: {
                        Image(systemName: "chevron.down")
                    }
                    .disabled(totalMatches == 0)

                    Button {
                        dismissFind()
                    } label: {
                        Image(systemName: "xmark")
                    }
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(.ultraThinMaterial)
            .clipShape(RoundedRectangle(cornerRadius: 12))
            .shadow(color: .black.opacity(0.1), radius: 4, x: 0, y: 2)
             .padding(.horizontal)
             .transition(.move(edge: .top).combined(with: .opacity))
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
