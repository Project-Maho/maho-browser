import Foundation

extension MahoBridge {
    func getEaselViewModels() -> String? {
        return callCoreString { maho_core_get_easel_view_models($0) }
    }

    func getEasel(easelId: String) -> String? {
        return withFFIString(easelId) { cStr -> String? in
            return callCoreString { maho_core_get_easel($0, cStr) }
        }
    }

    func createEasel(name: String) -> String? {
        return withFFIString(name) { cStr -> String? in
            return callCoreString { maho_core_create_easel($0, cStr) }
        }
    }

    func deleteEasel(easelId: String) -> Bool {
        return withFFIString(easelId) { cStr -> Bool in
            guard let result = callCoreNonOptional({ maho_core_delete_easel($0, cStr) }) else { return false }
            return result
        }
    }

    func updateEasel(easelId: String, json: String) -> Bool {
        return withFFIString(easelId) { easelCStr -> Bool in
            return withFFIString(json) { jsonCStr -> Bool in
                guard let result = callCoreNonOptional({ maho_core_update_easel($0, easelCStr, jsonCStr) }) else { return false }
                return result
            }
        }
    }
}
