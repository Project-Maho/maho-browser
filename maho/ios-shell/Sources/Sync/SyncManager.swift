import Foundation
import os.log

@MainActor
final class SyncManager {
    static let shared = SyncManager()
    static let syncEnabledKey = "maho_sync_polling_enabled"

    private(set) var isSyncing = false
    private(set) var lastSyncDate: Date?
    private(set) var currentState: SyncState = .idle
    private var syncTimer: Timer?
    private let syncInterval: TimeInterval = 30
    private let log = Logger(subsystem: "dev.maho.browser", category: "SyncManager")

    private init() {}

    var isSyncEnabled: Bool {
        UserDefaults.standard.bool(forKey: Self.syncEnabledKey)
    }

    func configureForCurrentSession() {
        guard SyncTransport.shared.currentRelaySession() != nil else {
            stopSync(persistPreference: false)
            return
        }

        if isSyncEnabled {
            startSync(persistPreference: false)
        }
    }

    func startSync(persistPreference: Bool = true) {
        if persistPreference {
            UserDefaults.standard.set(true, forKey: Self.syncEnabledKey)
        }

        isSyncing = true
        scheduleSyncTimer()
        SyncTransport.shared.connect()
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        log.info("Relay polling started")
    }

    func stopSync(persistPreference: Bool = true) {
        if persistPreference {
            UserDefaults.standard.set(false, forKey: Self.syncEnabledKey)
        }

        isSyncing = false
        currentState = .idle
        syncTimer?.invalidate()
        syncTimer = nil
        SyncTransport.shared.disconnect()
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        log.info("Relay polling stopped")
    }

    func syncNow() {
        SyncTransport.shared.triggerSync()
        isSyncing = true
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    func relayAuthenticationSucceeded() {
        if isSyncEnabled {
            startSync(persistPreference: false)
        } else {
            NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        }
    }

    func relayAuthenticationEnded() {
        stopSync(persistPreference: false)
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
    }

    private func scheduleSyncTimer() {
        syncTimer?.invalidate()
        syncTimer = Timer.scheduledTimer(withTimeInterval: syncInterval, repeats: true) { _ in
            Task { @MainActor in
                SyncManager.shared.syncNow()
            }
        }
    }

    func handleSyncUpdate(_ update: CoreUpdate) {
        switch update {
        case .syncStateChanged:
            NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        case .accountSyncStateChanged(let state):
            currentState = state
            switch state {
            case .syncing:
                isSyncing = true
            case .synced:
                isSyncing = false
                lastSyncDate = Date()
            case .idle, .offline, .error:
                isSyncing = false
            }
            NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        default:
            break
        }
    }
}

extension Notification.Name {
    static let syncStatusChanged = Notification.Name("syncStatusChanged")
}
