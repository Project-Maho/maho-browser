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
                                        .foregroundStyle(.primary)
                                        .lineLimit(1)
                                    Text(tab.url)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(1)
                                    Text(archivedDateLabel(tab.archivedAt))
                                        .font(.caption2)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(1)
                                }
                                .frame(maxWidth: .infinity, alignment: .leading)
                            }
                            .buttonStyle(.plain)
                            .contentShape(Rectangle())
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

    private func archivedDateLabel(_ timestamp: String) -> String {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        let date = formatter.date(from: timestamp) ?? {
            formatter.formatOptions = [.withInternetDateTime]
            return formatter.date(from: timestamp)
        }()
        guard let date else { return "Archived" }
        return "Archived \(date.formatted(date: .abbreviated, time: .shortened))"
    }
}
