import XCTest
#if canImport(Maho)
@testable import Maho
#endif

private final class FakeKeyValueStore: SyncKeyValueStore {
    var storage: [String: Bool] = [:]

    init(initialValues: [String: Bool] = [:]) {
        self.storage = initialValues
    }

    func bool(forKey defaultName: String) -> Bool {
        storage[defaultName] ?? false
    }

    func set(_ value: Bool, forKey defaultName: String) {
        storage[defaultName] = value
    }
}

private final class FakeTransportManager: SyncTransportManaging {
    var session: RelayAuthSession?
    var connectCallCount = 0
    var disconnectCallCount = 0
    var runCycleCallCount = 0
    var cancelCallCount = 0

    init(session: RelayAuthSession? = nil) {
        self.session = session
    }

    func currentRelaySession() -> RelayAuthSession? {
        session
    }

    func connect() {
        connectCallCount += 1
    }

    func disconnect() {
        disconnectCallCount += 1
    }

    @discardableResult
    func runCycle() async -> SyncCycleOutcome {
        runCycleCallCount += 1
        return .completed(pushed: 0, applied: 0)
    }

    func cancelCurrentCycle() {
        cancelCallCount += 1
    }
}

private final class FakeBackgroundScheduler: SyncBackgroundScheduling {
    var scheduleCount = 0

    func scheduleBackgroundRefresh() {
        scheduleCount += 1
    }
}

@MainActor
final class SyncBackgroundSchedulerTests: XCTestCase {
    private var fakeDefaults: FakeKeyValueStore!
    private var fakeTransport: FakeTransportManager!
    private var fakeScheduler: FakeBackgroundScheduler!
    private var syncManager: SyncManager!

    private func makeSession() -> RelayAuthSession {
        RelayAuthSession(
            accessToken: "access-token-123",
            refreshToken: "refresh-token-123",
            tokenType: "Bearer",
            expiresAt: Date().addingTimeInterval(3600),
            refreshExpiresAt: Date().addingTimeInterval(86400),
            account: RelayAccount(email: "user@example.com", displayName: "Test User"),
            device: RelayDevice(id: "device-1", name: "iOS Device", deviceType: "ios"),
            session: RelaySessionInfo(id: "session-1")
        )
    }

    override func setUp() {
        super.setUp()
        fakeDefaults = FakeKeyValueStore()
        fakeTransport = FakeTransportManager(session: makeSession())
        fakeScheduler = FakeBackgroundScheduler()
        syncManager = SyncManager(
            userDefaults: fakeDefaults,
            transport: fakeTransport,
            backgroundScheduler: fakeScheduler,
            notificationCenter: NotificationCenter()
        )
    }

    func testCompletionGateCallsSystemCompletionOnce() {
        let gate = SyncBackgroundCompletionGate()
        var completions = [Bool]()

        gate.complete(success: true) { completions.append($0) }
        gate.complete(success: false) { completions.append($0) }

        XCTAssertEqual(completions, [true])
    }

    func testBackgroundRefreshIdentifierIsStable() {
        XCTAssertEqual(
            SyncManager.backgroundRefreshIdentifier,
            "dev.maho.browser.sync.refresh"
        )
    }

    func testFreshAuthenticationEnablesAndStartsPolling() {
        XCTAssertFalse(syncManager.isSyncEnabled, "Initial fresh state must have sync disabled")
        XCTAssertFalse(syncManager.isSyncing, "Initial fresh state must have polling idle")
        XCTAssertEqual(fakeTransport.connectCallCount, 0)

        // Post-bootstrap authentication completes
        syncManager.relayAuthenticationSucceeded()

        XCTAssertTrue(
            syncManager.isSyncEnabled,
            "Successful fresh authentication must persist enabled polling"
        )
        XCTAssertTrue(
            fakeDefaults.bool(forKey: SyncManager.syncEnabledKey),
            "Defaults must store sync enabled after fresh authentication"
        )
        XCTAssertTrue(
            syncManager.isSyncing,
            "SyncManager must transition to syncing on fresh authentication"
        )
        XCTAssertEqual(
            fakeTransport.connectCallCount,
            1,
            "Transport connect must be invoked"
        )
        XCTAssertGreaterThanOrEqual(
            fakeScheduler.scheduleCount,
            1,
            "Background refresh must be scheduled"
        )
    }

    func testExplicitOptOutDisablesPollingAndPreventsAutoStartOnNextSession() {
        // First authenticate (starts and enables sync)
        syncManager.relayAuthenticationSucceeded()
        XCTAssertTrue(syncManager.isSyncEnabled)
        XCTAssertTrue(syncManager.isSyncing)

        // User explicitly opts out of sync
        syncManager.stopSync(persistPreference: true)

        XCTAssertFalse(
            syncManager.isSyncEnabled,
            "Explicit opt-out must persist disabled polling"
        )
        XCTAssertFalse(
            fakeDefaults.bool(forKey: SyncManager.syncEnabledKey),
            "Defaults must store sync disabled after explicit opt-out"
        )
        XCTAssertFalse(
            syncManager.isSyncing,
            "Syncing must stop after explicit opt-out"
        )
        XCTAssertEqual(
            fakeTransport.disconnectCallCount,
            1,
            "Transport disconnect must be called on opt-out"
        )

        // Simulate next session / app launch
        let nextSessionTransport = FakeTransportManager(session: makeSession())
        let nextSessionScheduler = FakeBackgroundScheduler()
        let nextSessionManager = SyncManager(
            userDefaults: fakeDefaults,
            transport: nextSessionTransport,
            backgroundScheduler: nextSessionScheduler,
            notificationCenter: NotificationCenter()
        )

        nextSessionManager.configureForCurrentSession()

        XCTAssertFalse(
            nextSessionManager.isSyncEnabled,
            "Next session must respect explicit opt-out"
        )
        XCTAssertFalse(
            nextSessionManager.isSyncing,
            "Next session must remain idle when user previously opted out"
        )
        XCTAssertEqual(
            nextSessionTransport.connectCallCount,
            0,
            "Transport must not connect on next session if opted out"
        )
    }

    func testConfigureForCurrentSessionAutoStartsWhenSyncEnabledAndSessionPresent() {
        fakeDefaults.set(true, forKey: SyncManager.syncEnabledKey)

        syncManager.configureForCurrentSession()

        XCTAssertTrue(syncManager.isSyncing)
        XCTAssertEqual(fakeTransport.connectCallCount, 1)
    }

    func testConfigureForCurrentSessionStopsSyncWhenNoSession() {
        fakeDefaults.set(true, forKey: SyncManager.syncEnabledKey)
        fakeTransport.session = nil

        syncManager.configureForCurrentSession()

        XCTAssertFalse(syncManager.isSyncing)
        XCTAssertEqual(fakeTransport.disconnectCallCount, 1)
    }

    func testRelayAuthenticationEndedStopsSyncWithoutErasingPreference() {
        syncManager.startSync(persistPreference: true)
        XCTAssertTrue(syncManager.isSyncing)
        XCTAssertTrue(syncManager.isSyncEnabled)

        syncManager.relayAuthenticationEnded()

        XCTAssertFalse(syncManager.isSyncing)
        XCTAssertTrue(
            fakeDefaults.bool(forKey: SyncManager.syncEnabledKey),
            "relayAuthenticationEnded stops active sync without wiping saved preference"
        )
        XCTAssertEqual(fakeTransport.disconnectCallCount, 1)
    }
}
