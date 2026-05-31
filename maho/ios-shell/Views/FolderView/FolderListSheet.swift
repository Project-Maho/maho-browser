import Foundation
import SwiftUI
import LucideIcons

struct FolderListSheet: View {
    @Environment(\.dismiss) private var dismiss

    let spaceId: SpaceId

    @State private var folders: [FolderViewModel] = []
    @State private var tabs: [TabViewModel] = []
    @State private var selectedFolder: FolderViewModel?
    @State private var showNewFolderAlert = false
    @State private var newFolderName = ""

    private let bridge = MahoBridge.shared

    var body: some View {
        NavigationStack {
            Group {
                if folders.isEmpty {
                    emptyState
                } else {
                    folderList
                }
            }
            .navigationTitle("Folders")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItemGroup(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
                ToolbarItemGroup(placement: .primaryAction) {
                    Button {
                        newFolderName = ""
                        showNewFolderAlert = true
                    } label: {
                        Image(lucide: Lucide.plus)
                    }
                }
            }
            .alert("New Folder", isPresented: $showNewFolderAlert) {
                TextField("Folder name", text: $newFolderName)
                Button("Cancel", role: .cancel) {}
                Button("Create") {
                    guard !newFolderName.isEmpty else { return }
                    bridge.createFolder(name: newFolderName, spaceId: spaceId)
                    reloadFolders()
                }
            } message: {
                Text("Enter a name for the new folder.")
            }
            .sheet(item: $selectedFolder) { folder in
                folderDetailSheet(folder)
            }
            .onAppear(perform: reloadFolders)
        }
    }

    private var folderList: some View {
        List {
            ForEach(folders) { folder in
                FolderCardView(
                    folder: folder,
                    spaceId: spaceId,
                    onTap: { selectedFolder = folder },
                    onRename: { name in
                        bridge.renameFolder(id: folder.id, spaceId: spaceId, name: name)
                        reloadFolders()
                    },
                    onDelete: {
                        bridge.deleteFolder(id: folder.id, spaceId: spaceId)
                        reloadFolders()
                    }
                )
            }
            .onMove { source, destination in
                let currentFolders = folders
                var reorderedFolders = currentFolders
                reorderedFolders.move(fromOffsets: source, toOffset: destination)

                for offset in source {
                    let movedFolder = currentFolders[offset]
                    guard let fromIndex = currentFolders.firstIndex(where: { $0.id == movedFolder.id }),
                          let toIndex = reorderedFolders.firstIndex(where: { $0.id == movedFolder.id }) else {
                        continue
                    }
                    bridge.reorderFolder(id: movedFolder.id, spaceId: spaceId, from: fromIndex, to: toIndex)
                }

                reloadFolders()
            }
        }
        .listStyle(.plain)
        .dropDestination(for: String.self) { _, _ in
            true
        }
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(lucide: Lucide.folder)
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("No Folders")
                .font(.title3)
                .fontWeight(.medium)
            Text("Create a folder to organize your tabs.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func folderDetailSheet(_ folder: FolderViewModel) -> some View {
        NavigationStack {
            let folderTabs = folder.tabIds.compactMap { tabId in
                tabs.first(where: { $0.id == tabId })
            }

            List {
                if folderTabs.isEmpty {
                    Text("No tabs in this folder")
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity)
                        .listRowSeparator(.hidden)
                } else {
                    ForEach(folderTabs) { tab in
                        HStack(spacing: 10) {
                            if tab.isLoading {
                                ProgressView()
                                    .controlSize(.mini)
                            }

                            VStack(alignment: .leading, spacing: 2) {
                                Text(tab.title.isEmpty ? "New Tab" : tab.title)
                                    .font(.body)
                                    .lineLimit(1)
                                Text(tab.url)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }
                        }
                        .swipeActions(edge: .trailing) {
                            Button(role: .destructive) {
                                bridge.removeTabFromFolder(
                                    tabId: tab.id,
                                    folderId: folder.id,
                                    spaceId: spaceId
                                )
                                reloadFolders()
                            } label: {
                                Label { Text("Remove") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("folder.badge.minus")) }
                            }
                        }
                    }
                    .onMove { source, destination in
                        let currentTabs = folderTabs
                        var reorderedTabs = currentTabs
                        reorderedTabs.move(fromOffsets: source, toOffset: destination)

                        for offset in source {
                            let movedTab = currentTabs[offset]
                            let newIndex = reorderedTabs.firstIndex(where: { $0.id == movedTab.id }) ?? 0
                            let beforeTabId = newIndex + 1 < reorderedTabs.count ? reorderedTabs[newIndex + 1].id : nil
                            bridge.reorderTab(id: movedTab.id, beforeTabId: beforeTabId)
                        }

                        reloadFolders()
                    }
                }
            }
            .listStyle(.plain)
            .navigationTitle(folder.name)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItemGroup(placement: .cancellationAction) {
                    Button("Back") { selectedFolder = nil }
                }
            }
        }
    }

    private func reloadFolders() {
        folders = bridge.getFolderViewModels(spaceId: spaceId)
        tabs = bridge.getTabViewModels()
    }
}
