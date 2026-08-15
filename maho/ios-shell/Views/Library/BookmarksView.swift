import SwiftUI
import LucideIcons

struct BookmarksView: View {
    let onOpenURL: (String) -> Void

    @State private var bookmarks: [BookmarkEntry] = []
    @State private var searchText = ""
    @State private var folderPath: [BookmarkFolder] = []
    @State private var isLoading = false
    @State private var pendingDeleteBookmark: BookmarkEntry?
    @State private var showDeleteConfirmation = false

    private var currentFolderId: String? {
        folderPath.last?.id
    }

    private var filteredBookmarks: [BookmarkEntry] {
        let inFolder = bookmarks.filter { $0.folderId == currentFolderId }
        guard !searchText.isEmpty else { return inFolder }
        return inFolder.filter {
            $0.title.localizedCaseInsensitiveContains(searchText) ||
            $0.url.localizedCaseInsensitiveContains(searchText)
        }
    }

    private var navigationTitle: String {
        folderPath.last?.name ?? "Bookmarks"
    }

    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if filteredBookmarks.isEmpty {
                    emptyState
                } else {
                    bookmarkList
                }
            }
            .navigationTitle(navigationTitle)
            .searchable(text: $searchText, prompt: "Search bookmarks")
            .toolbar {
                if !folderPath.isEmpty {
                    ToolbarItemGroup(placement: .topBarLeading) {
                        Button {
                            folderPath.removeLast()
                        } label: {
                            HStack(spacing: 4) {
                                Image(lucide: Lucide.chevronLeft)
                                Text("Back")
                            }
                        }
                    }
                }
            }
            .onAppear(perform: loadBookmarks)
        }
    }

    // MARK: - Subviews

    private var bookmarkList: some View {
        List {
            ForEach(filteredBookmarks, id: \.id) { bookmark in
                BookmarkRow(bookmark: bookmark)
                    .contentShape(Rectangle())
                    .onTapGesture {
                        openBookmark(bookmark)
                    }
                    .swipeActions(edge: .trailing, allowsFullSwipe: true) {
                        Button(role: .destructive) {
                            pendingDeleteBookmark = bookmark
                            showDeleteConfirmation = true
                        } label: {
                            Label { Text("Delete") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("trash")) }
                        }
                    }
                    .swipeActions(edge: .leading) {
                        Button {
                            moveBookmarkToRoot(bookmark)
                        } label: {
                            Label { Text("Move") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("folder")) }
                        }
                        .tint(.blue)
                    }
            }
        }
        .listStyle(.plain)
        .confirmationDialog(
            "Delete Bookmark?",
            isPresented: $showDeleteConfirmation,
            titleVisibility: .visible,
            presenting: pendingDeleteBookmark
        ) { bookmark in
            Button("Delete", role: .destructive) {
                deleteBookmark(bookmark)
                pendingDeleteBookmark = nil
            }
            Button("Cancel", role: .cancel) {
                pendingDeleteBookmark = nil
            }
        } message: { bookmark in
            Text("\"\(bookmark.title)\" will be removed from bookmarks.")
        }
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(lucide: Lucide.bookmark)
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text(searchText.isEmpty ? "No Bookmarks" : "No Results")
                .font(.title3)
                .fontWeight(.medium)
            Text(searchText.isEmpty
                ? "Bookmarks you add will appear here."
                : "Try a different search term.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Actions

    private func loadBookmarks() {
        isLoading = true
        bookmarks = MahoBridge.shared.getBookmarks()
        isLoading = false
    }

    private func openBookmark(_ bookmark: BookmarkEntry) {
        onOpenURL(bookmark.url)
    }

    private func deleteBookmark(_ bookmark: BookmarkEntry) {
        MahoBridge.shared.sendEvent(.removeBookmark(bookmarkId: bookmark.id))
        bookmarks.removeAll { $0.id == bookmark.id }
    }

    private func moveBookmarkToRoot(_ bookmark: BookmarkEntry) {
        MahoBridge.shared.sendEvent(.moveBookmark(bookmarkId: bookmark.id, folderId: nil))
        loadBookmarks()
    }

    func navigateToFolder(_ folder: BookmarkFolder) {
        folderPath.append(folder)
    }
}

// MARK: - BookmarkRow

private struct BookmarkRow: View {
    let bookmark: BookmarkEntry

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(bookmark.title)
                .font(.body)
                .lineLimit(1)
            Text(bookmark.url)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .padding(.vertical, 2)
    }
}

// MARK: - BookmarkFolder (navigation model)

struct BookmarkFolder: Identifiable, Hashable {
    let id: String
    let name: String
}
