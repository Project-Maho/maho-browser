import SwiftUI

struct ArchiveView: View {
    @ObservedObject var state: BrowserState
    @Environment(\.dismiss) private var dismiss
    @State private var isLoading = true

    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if state.archivedTabs.isEmpty {
                    ContentUnavailableView {
                        Label { Text("No Archived Tabs") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("archivebox")) }
                    } description: {
                        Text("Tabs archived in this space will appear here.")
                    }
                } else {
                    List {
                        ForEach(state.archivedTabs, id: \.id) { tab in
                            Button(action: {
                                state.restoreArchivedTab(tabId: tab.id)
                                dismiss()
                            }) {
                                VStack(alignment: .leading) {
                                    Text(tab.title)
                                        .lineLimit(1)
                                    Text(tab.url)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(1)
                                    Text("Archived \(tab.lastActiveAt)")
                                        .font(.caption2)
                                        .foregroundStyle(.tertiary)
                                        .lineLimit(1)
                                }
                            }
                            .accessibilityIdentifier("archiveRow_\(tab.id)")
                        }
                    }
                }
            }
            .navigationTitle("Archive")
            .toolbar {
                SwiftUI.ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                        .accessibilityIdentifier("archiveDoneButton")
                }
            }
        }
        .accessibilityIdentifier("archiveView")
        .onAppear {
            Task { @MainActor in
                isLoading = true
                await Task.yield()
                state.loadArchivedTabs()
                isLoading = false
            }
        }
    }
}
