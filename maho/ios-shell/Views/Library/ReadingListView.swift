import SwiftUI

struct ReadingListView: View {
    let onOpenURL: (String) -> Void

    @State private var items: [ReadingListItem] = []
    @State private var isLoading = false
    @State private var filterMode: FilterMode = .all

    private enum FilterMode: String, CaseIterable {
        case all = "All"
        case unread = "Unread"
        case read = "Read"
    }

    private var filteredItems: [ReadingListItem] {
        switch filterMode {
        case .all: return items
        case .unread: return items.filter { !$0.isRead }
        case .read: return items.filter { $0.isRead }
        }
    }

    private var unreadCount: Int {
        items.filter { !$0.isRead }.count
    }

    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if filteredItems.isEmpty {
                    emptyState
                } else {
                    readingList
                }
            }
            .navigationTitle("Reading List")
            .toolbar {
                ToolbarItemGroup(placement: .navigationBarTrailing) {
                    Picker("Filter", selection: $filterMode) {
                        ForEach(FilterMode.allCases, id: \.self) { mode in
                            Text(mode.rawValue).tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)
                    .frame(width: 200)
                }
            }
            .onAppear(perform: loadReadingList)
        }
    }

    // MARK: - Subviews

    private var readingList: some View {
        List {
            if unreadCount > 0 {
                Section {
                    HStack {
                        Text("Unread")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        Spacer()
                        Text("\(unreadCount)")
                            .font(.subheadline)
                            .fontWeight(.medium)
                            .foregroundStyle(.blue)
                    }
                }
            }

            ForEach(filteredItems) { item in
                ReadingListRow(item: item)
                    .contentShape(Rectangle())
                    .onTapGesture {
                        openItem(item)
                    }
                    .swipeActions(edge: .trailing, allowsFullSwipe: true) {
                        Button(role: .destructive) {
                            removeItem(item)
                        } label: {
                            Label("Remove", systemImage: "trash")
                        }
                    }
                    .swipeActions(edge: .leading, allowsFullSwipe: true) {
                        Button {
                            toggleRead(item)
                        } label: {
                            Label(
                                item.isRead ? "Mark Unread" : "Mark Read",
                                systemImage: item.isRead ? "circle" : "checkmark.circle"
                            )
                        }
                        .tint(item.isRead ? .orange : .green)
                    }
            }
        }
        .listStyle(.plain)
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(systemName: "eyeglasses")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text(filterMode == .all ? "Reading List Empty" : "No \(filterMode.rawValue) Items")
                .font(.title3)
                .fontWeight(.medium)
            Text(filterMode == .all
                ? "Add pages to read later."
                : "No items match this filter.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Actions

    private func loadReadingList() {
        isLoading = true
        items = MahoBridge.shared.getReadingList()
        isLoading = false
    }

    private func openItem(_ item: ReadingListItem) {
        onOpenURL(item.url)
    }

    private func removeItem(_ item: ReadingListItem) {
        MahoBridge.shared.removeFromReadingList(id: item.id)
        items.removeAll { $0.id == item.id }
    }

    private func toggleRead(_ item: ReadingListItem) {
        MahoBridge.shared.toggleRead(id: item.id)
        if let index = items.firstIndex(where: { $0.id == item.id }) {
            loadReadingList()
        }
    }
}

// MARK: - ReadingListRow

private struct ReadingListRow: View {
    let item: ReadingListItem

    var body: some View {
        HStack(spacing: 12) {
            Circle()
                .fill(item.isRead ? Color.clear : Color.blue)
                .frame(width: 8, height: 8)

            VStack(alignment: .leading, spacing: 4) {
                Text(item.title.isEmpty ? item.url : item.title)
                    .font(.body)
                    .lineLimit(1)
                    .foregroundStyle(item.isRead ? .secondary : .primary)
                Text(item.url)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
        .padding(.vertical, 2)
    }
}
