import SwiftUI
import LucideIcons

struct NotesView: View {
    @State private var notes: [NoteViewModel] = []
    @State private var searchText = ""
    @State private var isLoading = false
    @State private var showCreateSheet = false
    @State private var editingNote: NoteViewModel?
    @State private var showExportOptions = false
    @State private var pendingDeleteNote: NoteViewModel?
    @State private var showDeleteConfirmation = false

    private var filteredNotes: [NoteViewModel] {
        guard !searchText.isEmpty else { return notes }
        return notes.filter {
            $0.content.localizedCaseInsensitiveContains(searchText)
        }
    }

    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    ProgressView()
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if filteredNotes.isEmpty {
                    emptyState
                } else {
                    notesList
                }
            }
            .navigationTitle("Notes")
            .searchable(text: $searchText, prompt: "Search notes")
            .toolbar {
                ToolbarItemGroup(placement: .topBarTrailing) {
                    Menu {
                        Button {
                            showCreateSheet = true
                        } label: {
                            Label { Text("New Note") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("plus")) }
                        }
                        Button {
                            exportAllNotes()
                        } label: {
                            Label { Text("Export All") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("square.and.arrow.up")) }
                        }
                        .disabled(notes.isEmpty)
                    } label: {
                        Image(lucide: Lucide.circleEllipsis)
                    }
                }
            }
            .sheet(isPresented: $showCreateSheet) {
                NoteEditorSheet(mode: .create) { content in
                    createNote(content: content)
                    showCreateSheet = false
                }
            }
            .sheet(item: $editingNote) { note in
                NoteEditorSheet(mode: .edit(note)) { content in
                    updateNote(id: note.id, content: content)
                    editingNote = nil
                }
            }
            .onChange(of: searchText) { _, newValue in
                if !newValue.isEmpty {
                    searchNotes(query: newValue)
                } else {
                    loadNotes()
                }
            }
            .onAppear(perform: loadNotes)
        }
    }

    // MARK: - Subviews

    private var notesList: some View {
        List {
            ForEach(filteredNotes) { note in
                NoteRow(note: note)
                    .contentShape(Rectangle())
                    .onTapGesture {
                        editingNote = note
                    }
                    .swipeActions(edge: .trailing, allowsFullSwipe: true) {
                        Button(role: .destructive) {
                            pendingDeleteNote = note
                            showDeleteConfirmation = true
                        } label: {
                            Label { Text("Delete") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("trash")) }
                        }
                    }
                    .swipeActions(edge: .leading) {
                        Button {
                            exportSingleNote(id: note.id)
                        } label: {
                            Label { Text("Export") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("square.and.arrow.up")) }
                        }
                        .tint(.blue)
                    }
            }
        }
        .listStyle(.plain)
        .confirmationDialog(
            "Delete Note?",
            isPresented: $showDeleteConfirmation,
            titleVisibility: .visible,
            presenting: pendingDeleteNote
        ) { note in
            Button("Delete", role: .destructive) {
                deleteNote(id: note.id)
                pendingDeleteNote = nil
            }
            Button("Cancel", role: .cancel) {
                pendingDeleteNote = nil
            }
        } message: { _ in
            Text("This note will be permanently deleted.")
        }
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(lucide: Lucide.notebookText)
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text(searchText.isEmpty ? "No Notes" : "No Results")
                .font(.title3)
                .fontWeight(.medium)
            Text(searchText.isEmpty
                ? "Notes you create will appear here."
                : "Try a different search term.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Actions

    private func loadNotes() {
        isLoading = true
        notes = MahoBridge.shared.getNoteViewModels()
        isLoading = false
    }

    private func createNote(content: String) {
        MahoBridge.shared.createNote(content: content)
        loadNotes()
    }

    private func updateNote(id: NoteId, content: String) {
        MahoBridge.shared.updateNote(id: id, content: content)
        loadNotes()
    }

    private func deleteNote(id: NoteId) {
        MahoBridge.shared.deleteNote(id: id)
        notes.removeAll { $0.id == id }
    }

    private func searchNotes(query: String) {
        notes = MahoBridge.shared.searchNotes(query: query)
    }

    private func exportAllNotes() {
        _ = MahoBridge.shared.exportNotes()
    }

    private func exportSingleNote(id: NoteId) {
        _ = MahoBridge.shared.exportSingleNote(id: id)
    }
}

// MARK: - NoteRow

private struct NoteRow: View {
    let note: NoteViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(note.content.prefix(100).replacingOccurrences(of: "\n", with: " "))
                .font(.body)
                .lineLimit(2)

            if let linkedUrl = note.linkedUrl, !linkedUrl.isEmpty {
                HStack(spacing: 4) {
                    Image(lucide: Lucide.link)
                        .font(.caption2)
                    Text(linkedUrl)
                        .font(.caption)
                        .lineLimit(1)
                }
                .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 2)
    }
}

// MARK: - NoteEditorSheet

private struct NoteEditorSheet: View {
    enum Mode {
        case create
        case edit(NoteViewModel)
    }

    let mode: Mode
    let onSave: (String) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var content: String = ""

    private var title: String {
        switch mode {
        case .create: return "New Note"
        case .edit: return "Edit Note"
        }
    }

    var body: some View {
        NavigationStack {
            TextEditor(text: $content)
                .padding()
                .navigationTitle(title)
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItemGroup(placement: .cancellationAction) {
                        Button("Cancel") { dismiss() }
                    }
                    ToolbarItemGroup(placement: .confirmationAction) {
                        Button("Save") {
                            onSave(content)
                        }
                        .disabled(content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    }
                }
                .onAppear {
                    if case .edit(let note) = mode {
                        content = note.content
                    }
                }
        }
    }
}
