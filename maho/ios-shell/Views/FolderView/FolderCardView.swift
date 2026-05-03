import SwiftUI

struct FolderCardView: View {
    let folder: FolderViewModel
    let spaceId: SpaceId
    let onTap: () -> Void
    let onRename: (String) -> Void
    let onDelete: () -> Void

    @State private var isEditing = false
    @State private var editedName: String = ""

    private let bridge = MahoBridge.shared

    private var tabLabel: String {
        folder.tabCount == 1 ? "tab" : "tabs"
    }

    var body: some View {
        Button(action: onTap) {
            HStack(spacing: 12) {
                Image(systemName: folder.isExpanded ? "folder.fill" : "folder")
                    .font(.title3)
                    .foregroundStyle(.secondary)
                    .frame(width: 28)

                VStack(alignment: .leading, spacing: 2) {
                    if isEditing {
                        TextField("Folder Name", text: $editedName, onCommit: {
                            if !editedName.isEmpty {
                                onRename(editedName)
                            }
                            isEditing = false
                        })
                        .textFieldStyle(.plain)
                        .font(.body.weight(.medium))
                    } else {
                        Text(folder.name)
                            .font(.body)
                            .fontWeight(.medium)
                    }

                    Text("\(folder.tabCount) \(tabLabel)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Spacer()

                if folder.isPinned {
                    Image(systemName: "pin.fill")
                        .font(.caption2)
                        .foregroundStyle(.orange)
                }

                Image(systemName: "chevron.right")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
            .padding(.vertical, 6)
        }
        .buttonStyle(.plain)
        .draggable(folder.id)
        .dropDestination(for: String.self) { tabIds, _ in
            for tabId in tabIds {
                bridge.moveTabToFolder(tabId: tabId, folderId: folder.id, spaceId: spaceId)
            }
            return true
        }
        .contextMenu {
            Button {
                editedName = folder.name
                isEditing = true
            } label: {
                Label("Rename", systemImage: "pencil")
            }

            Button {
                if folder.isPinned {
                    bridge.sendEvent(.unpinFolder(spaceId: spaceId, folderId: folder.id))
                } else {
                    bridge.sendEvent(.pinFolder(spaceId: spaceId, folderId: folder.id))
                }
            } label: {
                Label(
                    folder.isPinned ? "Unpin" : "Pin",
                    systemImage: folder.isPinned ? "pin.slash" : "pin"
                )
            }

            Divider()

            Button(role: .destructive, action: onDelete) {
                Label("Delete", systemImage: "trash")
            }
        }
    }
}
