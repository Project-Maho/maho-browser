import Foundation
import BackgroundTasks
import os.log

@MainActor
final class SyncManager {
    static let shared = SyncManager()
    static let syncEnabledKey = "maho_sync_polling_enabled"
    static let backgroundRefreshIdentifier = "dev.maho.browser.sync.refresh"

    private(set) var isSyncing = false
    private(set) var lastSyncDate: Date?
    private(set) var currentState: SyncState = .idle
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
        SyncTransport.shared.connect()
        scheduleBackgroundRefresh()
        syncNow()
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        log.info("Relay polling started")
    }

    func stopSync(persistPreference: Bool = true) {
        if persistPreference {
            UserDefaults.standard.set(false, forKey: Self.syncEnabledKey)
        }

        isSyncing = false
        currentState = .idle
        SyncTransport.shared.disconnect()
        NotificationCenter.default.post(name: .syncStatusChanged, object: nil)
        log.info("Relay polling stopped")
    }

    func syncNow() {
        Task { @MainActor in
            _ = await SyncTransport.shared.runCycle()
            self.scheduleBackgroundRefresh()
        }
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

    func appDidEnterBackground() {
        scheduleBackgroundRefresh()
    }

    nonisolated static func registerBackgroundRefresh() {
        BGTaskScheduler.shared.register(
            forTaskWithIdentifier: backgroundRefreshIdentifier,
            using: nil
        ) { task in
            guard let refreshTask = task as? BGAppRefreshTask else {
                task.setTaskCompleted(success: false)
                return
            }
            Task { @MainActor in
                SyncManager.shared.handleBackgroundRefresh(refreshTask)
            }
        }
    }

    private func scheduleBackgroundRefresh() {
        guard isSyncEnabled, SyncTransport.shared.currentRelaySession() != nil else { return }
        let request = BGAppRefreshTaskRequest(identifier: Self.backgroundRefreshIdentifier)
        request.earliestBeginDate = Date(timeIntervalSinceNow: 15 * 60)
        do {
            try BGTaskScheduler.shared.submit(request)
        } catch {
            log.error("Unable to schedule sync background refresh: \(error.localizedDescription)")
        }
    }

    private func handleBackgroundRefresh(_ task: BGAppRefreshTask) {
        var completion = SyncBackgroundCompletionGate()
        task.expirationHandler = {
            Task { @MainActor in
                SyncTransport.shared.cancelCurrentCycle()
            }
            completion.complete(success: false) { task.setTaskCompleted(success: $0) }
        }
        Task { @MainActor in
            let outcome = await SyncTransport.shared.runCycle()
            completion.complete(success: outcome.isSuccess) { task.setTaskCompleted(success: $0) }
            self.scheduleBackgroundRefresh()
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

final class SyncBackgroundCompletionGate {
    private let lock = NSLock()
    private var didComplete = false

    func complete(success: Bool, completion: (Bool) -> Void) {
        lock.lock()
        defer { lock.unlock() }
        guard !didComplete else { return }
        didComplete = true
        completion(success)
    }
}

extension Notification.Name {
    static let syncStatusChanged = Notification.Name("syncStatusChanged")
}
