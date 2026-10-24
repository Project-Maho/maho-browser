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

    /// Wipes the current private store and drops it. The data is removed from the
    /// old store (not just the reference dropped) so a web view that is still alive
    /// while it tears down cannot keep the cookies/cache of the ended session.
    func reset() {
        guard let store = backingStore else { return }
        backingStore = nil
        store.removeData(
            ofTypes: WKWebsiteDataStore.allWebsiteDataTypes(),
            modifiedSince: .distantPast
        ) {
            withExtendedLifetime(store) {}
        }
    }

    /// Resets the store when no private tab remains in `tabs`.
    func resetIfNoPrivateTabs(in tabs: [TabViewModel]) {
        guard !tabs.contains(where: { $0.isPrivate }) else { return }
        reset()
    }

    private init() {}
}
