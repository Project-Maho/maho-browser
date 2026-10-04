import WebKit

/// Shared non-persistent data store for private browsing.
///
/// Privacy contract: all private tabs share ONE `WKWebsiteDataStore.nonPersistent()`
/// so cookies/sessions are visible across private tabs within a session, while
/// nothing is written to disk. `reset()` discards the store and all its in-memory
/// cookies/cache — call it when the last private tab closes so the next private
/// session starts clean.
@MainActor
final class PrivateBrowsingDataStorePool {
    static let shared = PrivateBrowsingDataStorePool()

    private var backingStore: WKWebsiteDataStore?

    var store: WKWebsiteDataStore {
        if let existing = backingStore { return existing }
        let created = WKWebsiteDataStore.nonPersistent()
        backingStore = created
        return created
    }

    func reset() {
        backingStore = nil
    }

    private init() {}
}
