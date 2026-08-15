import SwiftUI
import LucideIcons

struct HistoryView: View {
    let onOpenURL: (String) -> Void

    @State private var entries: [HistoryEntry] = []
    @State private var searchText = ""
    @State private var isLoading = false
    @State private var showClearConfirmation = false
    @State private var pendingDeleteEntry: HistoryEntry?
    @State private var showDeleteEntryConfirmation = false

    private var groupedEntries: [(String, [HistoryEntry])] {
        let source = searchText.isEmpty ? entries : entries.filter {
            $0.title.localizedCaseInsensitiveContains(searchText) ||
            $0.url.localizedCaseInsensitiveContains(searchText)
        }
        let grouped = Dictionary(grouping: source) { entry in
            dateGroupLabel(for: entry.visitedAt)
        }
        let order = ["Today", "Yesterday", "Last 7 Days", "Last 30 Days", "Older"]
        return order.compactMap { label in
            guard let items = grouped[label], !items.isEmpty else { return nil }
            return (label, items)
        }
    }

    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if groupedEntries.isEmpty {
                    emptyState
                } else {
                    historyList
                }
            }
            .navigationTitle("History")
            .searchable(text: $searchText, prompt: "Search history")
            .toolbar {
                ToolbarItemGroup(placement: .topBarTrailing) {
                    Button(role: .destructive) {
                        showClearConfirmation = true
                    } label: {
                        Text("Clear")
                    }
                    .disabled(entries.isEmpty)
                }
            }
            .confirmationDialog(
                "Clear All History?",
                isPresented: $showClearConfirmation,
                titleVisibility: .visible
            ) {
                Button("Clear All", role: .destructive) {
                    clearAllHistory()
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("This will permanently remove all browsing history.")
            }
            .onAppear(perform: loadHistory)
        }
    }

    // MARK: - Subviews

    private var historyList: some View {
        List {
            ForEach(groupedEntries, id: \.0) { section in
                Section(header: Text(section.0)) {
                    ForEach(section.1, id: \.id) { entry in
                        HistoryRow(entry: entry)
                            .contentShape(Rectangle())
                            .onTapGesture {
                                openEntry(entry)
                            }
                            .swipeActions(edge: .trailing, allowsFullSwipe: true) {
                                Button(role: .destructive) {
                                    pendingDeleteEntry = entry
                                    showDeleteEntryConfirmation = true
                                } label: {
                                    Label { Text("Delete") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("trash")) }
                                }
                            }
                    }
                }
            }
        }
        .listStyle(.plain)
        .confirmationDialog(
            "Delete History Entry?",
            isPresented: $showDeleteEntryConfirmation,
            titleVisibility: .visible,
            presenting: pendingDeleteEntry
        ) { entry in
            Button("Delete", role: .destructive) {
                deleteEntry(entry)
                pendingDeleteEntry = nil
            }
            Button("Cancel", role: .cancel) {
                pendingDeleteEntry = nil
            }
        } message: { entry in
            Text("This removes \"\(entry.title.isEmpty ? entry.url : entry.title)\" from history.")
        }
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(lucide: Lucide.clock)
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text(searchText.isEmpty ? "No History" : "No Results")
                .font(.title3)
                .fontWeight(.medium)
            Text(searchText.isEmpty
                ? "Pages you visit will appear here."
                : "Try a different search term.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Actions

    private func loadHistory() {
        isLoading = true
        entries = MahoBridge.shared.searchHistory(query: "", limit: 500)
        isLoading = false
    }

    private func openEntry(_ entry: HistoryEntry) {
        onOpenURL(entry.url)
    }

    private func deleteEntry(_ entry: HistoryEntry) {
        MahoBridge.shared.sendEvent(.deleteHistoryEntry(entryId: entry.id))
        entries.removeAll { $0.id == entry.id }
    }

    private func clearAllHistory() {
        MahoBridge.shared.sendEvent(.clearHistory)
        entries.removeAll()
    }

    // MARK: - Date Grouping

    private func dateGroupLabel(for dateString: String) -> String {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        guard let date = formatter.date(from: dateString) else {
            // Try without fractional seconds
            formatter.formatOptions = [.withInternetDateTime]
            guard let date = formatter.date(from: dateString) else {
                return "Older"
            }
            return groupLabel(for: date)
        }
        return groupLabel(for: date)
    }

    private func groupLabel(for date: Date) -> String {
        let calendar = Calendar.current
        if calendar.isDateInToday(date) {
            return "Today"
        } else if calendar.isDateInYesterday(date) {
            return "Yesterday"
        } else if let weekAgo = calendar.date(byAdding: .day, value: -7, to: Date()),
                  date >= weekAgo {
            return "Last 7 Days"
        } else if let monthAgo = calendar.date(byAdding: .day, value: -30, to: Date()),
                  date >= monthAgo {
            return "Last 30 Days"
        } else {
            return "Older"
        }
    }
}

// MARK: - HistoryRow

private struct HistoryRow: View {
    let entry: HistoryEntry

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(entry.title.isEmpty ? entry.url : entry.title)
                .font(.body)
                .lineLimit(1)
            Text(entry.url)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .padding(.vertical, 2)
    }
}
