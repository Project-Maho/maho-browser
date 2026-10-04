import Foundation

extension ShellEvent {
    func encodeTabsSpaces(to container: inout KeyedEncodingContainer<DynamicCodingKey>) throws -> Bool {
        switch self {
        case .navigateTo(let tabId, let url):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(url, forKey: DynamicCodingKey(stringValue: "url")!)
        case .goBack(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .goForward(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .reload(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .stop(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .createTab(let spaceId, let url, let parentId, let isPrivate):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encodeIfPresent(url, forKey: DynamicCodingKey(stringValue: "url")!)
            try container.encodeIfPresent(parentId, forKey: DynamicCodingKey(stringValue: "parent_id")!)
            try container.encode(isPrivate, forKey: DynamicCodingKey(stringValue: "is_private")!)
        case .closeTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .activateTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .duplicateTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .pinTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .unpinTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .favoriteTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .changeTabRole(let tabId, let newRole):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(newRole, forKey: DynamicCodingKey(stringValue: "new_role")!)
        case .reorderFavorite(let tabId, let newIndex):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(newIndex, forKey: DynamicCodingKey(stringValue: "new_index")!)
        case .muteTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .unmuteTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .freezeTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .moveTab(let tabId, let targetSpace, let position):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encode(targetSpace, forKey: DynamicCodingKey(stringValue: "target_space")!)
            try container.encode(position, forKey: DynamicCodingKey(stringValue: "position")!)
        case .setTabParent(let tabId, let newParentId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encodeIfPresent(newParentId, forKey: DynamicCodingKey(stringValue: "new_parent_id")!)
        case .reorderTab(let tabId, let beforeTabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
            try container.encodeIfPresent(beforeTabId, forKey: DynamicCodingKey(stringValue: "before_tab_id")!)
        case .closeOtherTabs(let spaceId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .closeTabsToRight(let spaceId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .closeTabsToLeft(let spaceId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .reopenLastClosed: break
        case .archiveTabById(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .restoreArchivedTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .resetPinnedTab(let tabId):
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .createSpace(let name, let color, let profileId):
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
            try container.encode(color, forKey: DynamicCodingKey(stringValue: "color")!)
            try container.encode(profileId, forKey: DynamicCodingKey(stringValue: "profile_id")!)
        case .deleteSpace(let spaceId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
        case .exportSpaceIntoFolder(let spaceId, let targetSpaceId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(targetSpaceId, forKey: DynamicCodingKey(stringValue: "target_space_id")!)
        case .activateSpace(let spaceId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
        case .renameSpace(let spaceId, let name):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
        case .recolorSpace(let spaceId, let color):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(color, forKey: DynamicCodingKey(stringValue: "color")!)
        case .reorderSpace(let spaceId, let from, let to):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(from, forKey: DynamicCodingKey(stringValue: "from")!)
            try container.encode(to, forKey: DynamicCodingKey(stringValue: "to")!)
        case .createFolder(let spaceId, let name, let isPinned):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
            try container.encode(isPinned, forKey: DynamicCodingKey(stringValue: "is_pinned")!)
        case .renameFolder(let spaceId, let folderId, let name):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(name, forKey: DynamicCodingKey(stringValue: "name")!)
        case .deleteFolder(let spaceId, let folderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .moveFolderIntoFolder(let spaceId, let folderId, let targetFolderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(targetFolderId, forKey: DynamicCodingKey(stringValue: "target_folder_id")!)
        case .reorderFolder(let spaceId, let folderId, let from, let to):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(from, forKey: DynamicCodingKey(stringValue: "from")!)
            try container.encode(to, forKey: DynamicCodingKey(stringValue: "to")!)
        case .moveTabToFolder(let spaceId, let folderId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .removeTabFromFolder(let spaceId, let folderId, let tabId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
            try container.encode(tabId, forKey: DynamicCodingKey(stringValue: "tab_id")!)
        case .toggleFolderExpanded(let spaceId, let folderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .pinFolder(let spaceId, let folderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        case .unpinFolder(let spaceId, let folderId):
            try container.encode(spaceId, forKey: DynamicCodingKey(stringValue: "space_id")!)
            try container.encode(folderId, forKey: DynamicCodingKey(stringValue: "folder_id")!)
        default:
            return false
        }
        return true
    }

    static func decodeTabsSpaces(kind: String, from container: KeyedDecodingContainer<DynamicCodingKey>) throws -> ShellEvent? {
        switch kind {
        // Navigation
        case "navigate_to":
            return .navigateTo(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                url: try container.decode(Url.self, forKey: DynamicCodingKey(stringValue: "url")!))
        case "go_back":
            return .goBack(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "go_forward":
            return .goForward(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "reload":
            return .reload(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "stop":
            return .stop(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))

        // Tab management
        case "create_tab":
            return .createTab(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                url: try container.decodeIfPresent(Url.self, forKey: DynamicCodingKey(stringValue: "url")!),
                parentId: try container.decodeIfPresent(TabId.self, forKey: DynamicCodingKey(stringValue: "parent_id")!),
                isPrivate: (try? container.decode(Bool.self, forKey: DynamicCodingKey(stringValue: "is_private")!)) ?? false)
        case "close_tab":
            return .closeTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "activate_tab":
            return .activateTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "duplicate_tab":
            return .duplicateTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "pin_tab":
            return .pinTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "unpin_tab":
            return .unpinTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "favorite_tab":
            return .favoriteTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "change_tab_role":
            return .changeTabRole(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                newRole: try container.decode(TabRole.self, forKey: DynamicCodingKey(stringValue: "new_role")!))
        case "reorder_favorite":
            return .reorderFavorite(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                newIndex: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "new_index")!))
        case "mute_tab":
            return .muteTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "unmute_tab":
            return .unmuteTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "freeze_tab":
            return .freezeTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "move_tab":
            return .moveTab(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                targetSpace: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "target_space")!),
                position: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "position")!))
        case "set_tab_parent":
            return .setTabParent(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                newParentId: try container.decodeIfPresent(TabId.self, forKey: DynamicCodingKey(stringValue: "new_parent_id")!))
        case "reorder_tab":
            return .reorderTab(
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!),
                beforeTabId: try container.decodeIfPresent(TabId.self, forKey: DynamicCodingKey(stringValue: "before_tab_id")!))
        case "close_other_tabs":
            return .closeOtherTabs(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "close_tabs_to_right":
            return .closeTabsToRight(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "close_tabs_to_left":
            return .closeTabsToLeft(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "reopen_last_closed":
            return .reopenLastClosed
        case "archive_tab_by_id":
            return .archiveTabById(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "restore_archived_tab":
            return .restoreArchivedTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "reset_pinned_tab":
            return .resetPinnedTab(tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))

        // Space management
        case "create_space":
            return .createSpace(
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!),
                color: try container.decode(SpaceColor.self, forKey: DynamicCodingKey(stringValue: "color")!),
                profileId: try container.decode(ProfileId.self, forKey: DynamicCodingKey(stringValue: "profile_id")!))
        case "delete_space":
            return .deleteSpace(spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!))
        case "export_space_into_folder":
            return .exportSpaceIntoFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                targetSpaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "target_space_id")!))
        case "activate_space":
            return .activateSpace(spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!))
        case "rename_space":
            return .renameSpace(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!))
        case "recolor_space":
            return .recolorSpace(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                color: try container.decode(SpaceColor.self, forKey: DynamicCodingKey(stringValue: "color")!))
        case "reorder_space":
            return .reorderSpace(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                from: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "from")!),
                to: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "to")!))

        // Folder management
        case "create_folder":
            return .createFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!),
                isPinned: try container.decodeIfPresent(Bool.self, forKey: DynamicCodingKey(stringValue: "is_pinned")!) ?? false)
        case "rename_folder":
            return .renameFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                name: try container.decode(String.self, forKey: DynamicCodingKey(stringValue: "name")!))
        case "delete_folder":
            return .deleteFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "move_folder_into_folder":
            return .moveFolderIntoFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                targetFolderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "target_folder_id")!))
        case "reorder_folder":
            return .reorderFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                from: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "from")!),
                to: try container.decode(Int.self, forKey: DynamicCodingKey(stringValue: "to")!))
        case "move_tab_to_folder":
            return .moveTabToFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "remove_tab_from_folder":
            return .removeTabFromFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!),
                tabId: try container.decode(TabId.self, forKey: DynamicCodingKey(stringValue: "tab_id")!))
        case "toggle_folder_expanded":
            return .toggleFolderExpanded(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "pin_folder":
            return .pinFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))
        case "unpin_folder":
            return .unpinFolder(
                spaceId: try container.decode(SpaceId.self, forKey: DynamicCodingKey(stringValue: "space_id")!),
                folderId: try container.decode(FolderId.self, forKey: DynamicCodingKey(stringValue: "folder_id")!))

        default:
            return nil
        }
    }
}
