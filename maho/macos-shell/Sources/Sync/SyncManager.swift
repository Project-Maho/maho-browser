import Foundation

final class SyncManager: CoreUpdateObserver {
    static let shared = SyncManager()

    private(set) var isSyncing = false
    private(set) var lastSyncDate: Date?
    private(set) var currentState: SyncState = .idle
    private let syncInterval: TimeInterval = 30
    private var syncTimer: Timer?

    private init() {
        CoreUpdateDispatcher.shared.register(self)
    }

    deinit { CoreUpdateDispatcher.shared.unregister(self) }

    func startSync() {
        let serverUrl = UserDefaults.standard.string(forKey: "maho_sync_server_url") ?? "https://sync.maho.app"
        let syncKey = UserDefaults.standard.string(forKey: "maho_sync_key") ?? ""
        startSync(serverUrl: serverUrl, syncKey: syncKey)
    }

    func startSync(serverUrl: String, syncKey: String) {
        MahoBridge.shared.startSync(serverUrl: serverUrl, syncKey: syncKey)
        isSyncing = true
        currentState = .syncing(progress: 0)
        scheduleSyncTimer()
        SyncTransport.shared.connect(serverUrl: serverUrl, syncKey: syncKey)
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    func stopSync() {
        MahoBridge.shared.stopSync()
        isSyncing = false
        currentState = .idle
        syncTimer?.invalidate()
        syncTimer = nil
        SyncTransport.shared.disconnect()
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    func syncNow() {
        SyncTransport.shared.triggerSync()
        currentState = .syncing(progress: 0)
        lastSyncDate = Date()
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    func resumeSyncIfNeeded() {
        guard isSyncing else { return }
        SyncTransport.shared.resumeIfPossible()
    }

    private func scheduleSyncTimer() {
        syncTimer?.invalidate()
        syncTimer = Timer.scheduledTimer(withTimeInterval: syncInterval, repeats: true) { [weak self] _ in
            self?.syncNow()
        }
        if let syncTimer {
            RunLoop.main.add(syncTimer, forMode: .common)
        }
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            switch update {
            case .syncStateChanged:
                NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
            case .accountSyncStateChanged(let state):
                self.currentState = state
                switch state {
                case .syncing:
                    self.isSyncing = true
                case .synced:
                    self.isSyncing = true
                    self.lastSyncDate = Date()
                case .idle, .offline:
                    self.isSyncing = false
                case .error:
                    self.isSyncing = false
                }
                NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
            default:
                break
            }
        }
    }
}

extension Notification.Name {
    static let syncStatusChanged = Notification.Name("syncStatusChanged")
    static let syncDevicesChanged = Notification.Name("syncDevicesChanged")
    static let mediaSessionStateChanged = Notification.Name("mediaSessionStateChanged")
}
