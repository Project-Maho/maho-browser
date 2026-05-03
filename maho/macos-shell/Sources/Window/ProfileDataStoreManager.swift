import WebKit

final class ProfileDataStoreManager {
    static let shared = ProfileDataStoreManager()
    private var stores: [String: WKWebsiteDataStore] = [:]
    private init() {}

    func dataStore(for profileId: String?) -> WKWebsiteDataStore {
        dispatchPrecondition(condition: .onQueue(.main))
        guard let profileId = profileId, profileId != "default" else {
            return .default()
        }

        if let cached = stores[profileId] {
            return cached
        }

        guard let dataStoreId = MahoBridge.shared.getProfileDataStoreId(profileId: profileId),
              let uuid = UUID(uuidString: dataStoreId) else {
            return .default()
        }

        if #available(macOS 14.0, *) {
            let store = WKWebsiteDataStore(forIdentifier: uuid)
            stores[profileId] = store
            return store
        } else {
            return .default()
        }
    }

    func removeDataStore(for dataStoreId: String) {
        dispatchPrecondition(condition: .onQueue(.main))
        stores.removeValue(forKey: dataStoreId)

        guard let uuid = UUID(uuidString: dataStoreId) else { return }

        if #available(macOS 14.0, *) {
            Task {
                do {
                    try await WKWebsiteDataStore.remove(forIdentifier: uuid)
                } catch {
                    NSLog("Failed to remove data store \(dataStoreId): \(error.localizedDescription)")
                }
            }
        }
    }
}
