import Foundation
import BackgroundTasks
import os.log

protocol SyncKeyValueStore: AnyObject {
    func bool(forKey defaultName: String) -> Bool
    func set(_ value: Bool, forKey defaultName: String)
}

extension UserDefaults: SyncKeyValueStore {}

@MainActor
protocol SyncTransportManaging: AnyObject {
    func currentRelaySession() -> RelayAuthSession?
    func connect()
    func disconnect()
    @discardableResult
    func runCycle() async -> SyncCycleOutcome
    func cancelCurrentCycle()
}

extension SyncTransport: SyncTransportManaging {}

@MainActor
protocol SyncBackgroundScheduling: AnyObject {
    func scheduleBackgroundRefresh()
}

final class SystemSyncBackgroundScheduler: SyncBackgroundScheduling {
    private let log = Logger(subsystem: "dev.maho.browser", category: "SyncScheduler")

    func scheduleBackgroundRefresh() {
        let request = BGAppRefreshTaskRequest(identifier: SyncManager.backgroundRefreshIdentifier)
        request.earliestBeginDate = Date(timeIntervalSinceNow: 15 * 60)
        do {
            try BGTaskScheduler.shared.submit(request)
        } catch {
            log.error("Unable to schedule sync background refresh: \(error.localizedDescription)")
        }
    }
}

@MainActor
final class SyncManager {
    static let shared = SyncManager()
    static let syncEnabledKey = "maho_sync_polling_enabled"
    nonisolated static let backgroundRefreshIdentifier = "dev.maho.browser.sync.refresh"

    private(set) var isSyncing = false
    private(set) var lastSyncDate: Date?
    private(set) var currentState: SyncState = .idle
    private let userDefaults: SyncKeyValueStore
    private let transport: SyncTransportManaging
    private let backgroundScheduler: SyncBackgroundScheduling
    private let notificationCenter: NotificationCenter
    private let log = Logger(subsystem: "dev.maho.browser", category: "SyncManager")

    init(
        userDefaults: SyncKeyValueStore? = nil,
        transport: SyncTransportManaging? = nil,
        backgroundScheduler: SyncBackgroundScheduling? = nil,
        notificationCenter: NotificationCenter = .default
    ) {
        self.userDefaults = userDefaults ?? UserDefaults.standard
        self.transport = transport ?? SyncTransport.shared
        self.backgroundScheduler = backgroundScheduler ?? SystemSyncBackgroundScheduler()
        self.notificationCenter = notificationCenter
    }

    var isSyncEnabled: Bool {
        userDefaults.bool(forKey: Self.syncEnabledKey)
    }

    func configureForCurrentSession() {
        guard transport.currentRelaySession() != nil else {
            stopSync(persistPreference: false)
            return
        }

        if isSyncEnabled {
            startSync(persistPreference: false)
        }
    }

    func startSync(persistPreference: Bool = true) {
        if persistPreference {
            userDefaults.set(true, forKey: Self.syncEnabledKey)
        }

        isSyncing = true
        transport.connect()
        scheduleBackgroundRefresh()
        syncNow()
        notificationCenter.post(name: .syncStatusChanged, object: nil)
        log.info("Relay polling started")
    }

    func stopSync(persistPreference: Bool = true) {
        if persistPreference {
            userDefaults.set(false, forKey: Self.syncEnabledKey)
        }

        isSyncing = false
        currentState = .idle
        transport.disconnect()
        notificationCenter.post(name: .syncStatusChanged, object: nil)
        log.info("Relay polling stopped")
    }

    func syncNow() {
        Task { @MainActor in
            _ = await transport.runCycle()
            self.scheduleBackgroundRefresh()
        }
        isSyncing = true
        notificationCenter.post(name: .syncStatusChanged, object: nil)
    }

    func relayAuthenticationSucceeded() {
        startSync(persistPreference: true)
    }

    func relayAuthenticationEnded() {
        stopSync(persistPreference: false)
        notificationCenter.post(name: .syncStatusChanged, object: nil)
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
        guard isSyncEnabled, transport.currentRelaySession() != nil else { return }
        backgroundScheduler.scheduleBackgroundRefresh()
    }

    private func handleBackgroundRefresh(_ task: BGAppRefreshTask) {
        let completion = SyncBackgroundCompletionGate()
        task.expirationHandler = { [weak self] in
            Task { @MainActor in
                self?.transport.cancelCurrentCycle()
            }
            completion.complete(success: false) { task.setTaskCompleted(success: $0) }
        }
        Task { @MainActor in
            let outcome = await transport.runCycle()
            completion.complete(success: outcome.isSuccess) { task.setTaskCompleted(success: $0) }
            self.scheduleBackgroundRefresh()
        }
    }

    func handleSyncUpdate(_ update: CoreUpdate) {
        switch update {
        case .syncStateChanged:
            notificationCenter.post(name: .syncStatusChanged, object: nil)
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
            notificationCenter.post(name: .syncStatusChanged, object: nil)
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
