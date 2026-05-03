import Foundation

final class APIKeyStore {
    static let shared = APIKeyStore()
    private let keyPrefix = "apikey."
    private let store = SecureLocalStore.shared

    private init() {}

    func save(key: String, for provider: String) -> Bool {
        store.save(value: key, for: storageKey(provider))
    }

    func get(for provider: String) -> String? {
        store.get(for: storageKey(provider))
    }

    @discardableResult
    func delete(for provider: String) -> Bool {
        store.delete(for: storageKey(provider))
    }

    func hasKey(for provider: String) -> Bool {
        store.has(key: storageKey(provider))
    }

    private func storageKey(_ provider: String) -> String {
        "\(keyPrefix)\(provider)"
    }
}
