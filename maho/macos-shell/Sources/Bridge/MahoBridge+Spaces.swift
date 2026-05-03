import Foundation

// MARK: - Space Operations

extension MahoBridge {

    func getSpaceViewModels() -> [SpaceViewModel] {
        guard let json = callCoreString({ maho_core_get_space_view_models($0) }) else { return [] }
        return decode(json) ?? []
    }

    func getCanonicalSpaceViewModels() -> [SpaceViewModel] {
        getSpaceViewModels().sortedByCanonicalOrder()
    }

    func getActiveSpaceId() -> SpaceId? {
        guard let json = callCoreString({ maho_core_get_active_space_id($0) }) else { return nil }
        return decode(json)
    }

    func getCanonicalActiveSpaceId() -> SpaceId? {
        let spaces = getCanonicalSpaceViewModels()
        if let activeSpaceId = spaces.first(where: { $0.isActive })?.id {
            return activeSpaceId
        }
        if let activeSpaceId = getActiveSpaceId(), spaces.contains(where: { $0.id == activeSpaceId }) {
            return activeSpaceId
        }
        return spaces.first?.id
    }

    func createSpace(name: String, colorJson: String, profileId: String) -> String? {
        return withFFIString(name) { nameCStr -> String? in
            return withFFIString(colorJson) { colorCStr -> String? in
                return withFFIString(profileId) { profileCStr -> String? in
                    return callCoreString { maho_core_create_space($0, nameCStr, colorCStr, profileCStr) }
                }
            }
        }
    }

    func deleteSpace(spaceId: String) {
        withFFIString(spaceId) { cStr in
            callCoreVoid { maho_core_delete_space($0, cStr) }
        }
    }

    func renameSpace(spaceId: String, name: String) {
        withFFIString(spaceId) { spaceCStr in
            withFFIString(name) { nameCStr in
                callCoreVoid { maho_core_rename_space($0, spaceCStr, nameCStr) }
            }
        }
    }

    func recolorSpace(spaceId: String, colorJson: String) {
        withFFIString(spaceId) { spaceCStr in
            withFFIString(colorJson) { colorCStr in
                callCoreVoid { maho_core_recolor_space($0, spaceCStr, colorCStr) }
            }
        }
    }

    func reorderSpace(spaceId: String, from: UInt, to: UInt) {
        withFFIString(spaceId) { cStr in
            callCoreVoid { maho_core_reorder_space($0, cStr, from, to) }
        }
    }

    func activateSpace(spaceId: String) {
        withFFIString(spaceId) { cStr in
            callCoreVoid { maho_core_activate_space($0, cStr) }
        }
    }

    func updateSpaceConfig(_ changes: SpaceConfigUpdate) {
        let updates = sendEvent(.updateSpaceConfig(changes: changes))
        CoreUpdateDispatcher.shared.dispatch(updates)
    }

    // MARK: - Folder Operations

    func getFolderViewModels(spaceId: SpaceId) -> [FolderViewModel] {
        guard let spaceJson = encode(spaceId) else { return [] }
        return withFFIString(spaceJson) { cStr -> [FolderViewModel] in
            guard let ptr = callCore({ maho_core_get_folder_view_models($0, cStr) }) else { return [] }
            defer { maho_string_free(ptr) }
            return decode(String(cString: ptr)) ?? []
        }
    }
}
