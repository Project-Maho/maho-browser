import Foundation

extension MahoBridge {
    func createSharedCollection(spaceId: String, name: String) -> String? {
        withFFIString(spaceId) { spaceCStr in
            withFFIString(name) { nameCStr in
                callCoreString { maho_core_create_shared_collection($0, spaceCStr, nameCStr) }
            }
        }
    }

    func getShareLink(collectionId: String) -> String? {
        withFFIString(collectionId) { cStr in
            callCoreString { maho_core_get_share_link($0, cStr) }
        }
    }

    func updateSharePermissions(collectionId: String, permission: String) {
        withFFIString(collectionId) { collCStr in
            withFFIString(permission) { permCStr in
                callCoreVoid { maho_core_update_share_permissions($0, collCStr, permCStr) }
            }
        }
    }

    func revokeShare(collectionId: String) {
        withFFIString(collectionId) { cStr in
            callCoreVoid { maho_core_revoke_share($0, cStr) }
        }
    }

    func getSharedCollections() -> String? {
        callCoreString { maho_core_get_shared_collections($0) }
    }

    func joinSharedCollection(shareLink: String) -> Bool {
        withFFIString(shareLink) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_join_shared_collection($0, cStr) }) else { return false }
            return result
        }
    }
}
