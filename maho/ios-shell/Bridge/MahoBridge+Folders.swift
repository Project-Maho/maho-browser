import Foundation

extension MahoBridge {

    func getFolderViewModels(spaceId: SpaceId) -> [FolderViewModel] {
        guard let jsonString = try? spaceId.mahoJSONString() else {
            return []
        }
        return (withCore { ptr in
            jsonString.withCString { cStr in
                FFIString.consumeJSON(maho_core_get_folder_view_models(ptr, cStr))
            }
        } ?? nil) ?? []
    }

    func createFolder(name: String, spaceId: SpaceId, isPinned: Bool = false) {
        sendEvent(.createFolder(spaceId: spaceId, name: name, isPinned: isPinned))
    }

    func createFolderAndReturnModel(name: String, spaceId: SpaceId) -> FolderViewModel? {
        let updates = handleEvent(.createFolder(spaceId: spaceId, name: name, isPinned: false))
        for update in updates {
            if case .folderCreated(let folder) = update {
                return folder
            }
        }
        return nil
    }

    func deleteFolder(id folderId: FolderId, spaceId: SpaceId) {
        sendEvent(.deleteFolder(spaceId: spaceId, folderId: folderId))
    }

    func renameFolder(id folderId: FolderId, spaceId: SpaceId, name: String) {
        sendEvent(.renameFolder(spaceId: spaceId, folderId: folderId, name: name))
    }

    func reorderFolder(id folderId: FolderId, spaceId: SpaceId, from: Int, to: Int) {
        sendEvent(.reorderFolder(spaceId: spaceId, folderId: folderId, from: from, to: to))
    }

    func moveTabToFolder(tabId: TabId, folderId: FolderId, spaceId: SpaceId) {
        sendEvent(.moveTabToFolder(spaceId: spaceId, folderId: folderId, tabId: tabId))
    }

    func removeTabFromFolder(tabId: TabId, folderId: FolderId, spaceId: SpaceId) {
        sendEvent(.removeTabFromFolder(spaceId: spaceId, folderId: folderId, tabId: tabId))
    }

    func toggleFolderExpanded(folderId: FolderId, spaceId: SpaceId) {
        sendEvent(.toggleFolderExpanded(spaceId: spaceId, folderId: folderId))
    }
}
