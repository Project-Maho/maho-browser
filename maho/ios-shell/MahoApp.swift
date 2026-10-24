import os.log
import os.signpost
import SwiftUI

private let launchLog = OSLog(subsystem: "dev.maho.browser", category: "Launch")

@main
struct MahoApp: App {
    @Environment(\.scenePhase) private var scenePhase
    @AppStorage("onboardingCompleted") private var onboardingCompleted: Bool = false
    @StateObject private var updateChecker = AppStoreUpdateChecker()
    @StateObject private var themeStore = AppThemeStore()
    @ObservedObject private var launchDataClear = LaunchDataClearGate.shared

    private var shouldShowOnboarding: Bool {
        !isRunningUITests && !onboardingCompleted
    }

    private var isRunningUITests: Bool {
        ProcessInfo.processInfo.environment["XCTestConfigurationFilePath"] != nil
    }

    init() {
        let storagePath = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first!
            .appendingPathComponent("dev.maho.browser", isDirectory: true)
            .path

        os_signpost(.begin, log: launchLog, name: "provisionSqlcipherKey")
        MahoBridge.shared.provisionSqlcipherKey(storagePath: storagePath)
        os_signpost(.end, log: launchLog, name: "provisionSqlcipherKey")

        MahoBridge.shared.provisionAssetsInBackground(storagePath: storagePath)

        os_signpost(.begin, log: launchLog, name: "initialize")
        MahoBridge.shared.initialize(storagePath: storagePath)
        os_signpost(.end, log: launchLog, name: "initialize")

        os_signpost(.begin, log: launchLog, name: "loadState")
        MahoBridge.shared.loadState()
        os_signpost(.end, log: launchLog, name: "loadState")
        // Clear Data on Exit runs here, at the start of a new process and before
        // any web view exists. iOS gives no reliable termination callback, and
        // clearing on every trip to the background signed users out mid-task.
        // The browser UI (and so every web view) is held back until WebKit has
        // finished removing the data, so a restored tab cannot load with the
        // cookies that were meant to be gone.
        if MahoBridge.shared.isClearDataOnExitEnabled() {
            let gate = LaunchDataClearGate.shared
            gate.close()
            MahoBridge.shared.clearAllBrowsingData {
                Task { @MainActor in gate.open() }
            }
        }
        if !isRunningUITests {
            MahoBridge.shared.applyRestorePolicyOnLaunch()
        }
        E2ETestHooks.runOnBoot()
        SyncManager.registerBackgroundRefresh()
        // The unit-test host shares this app's Keychain relay session. Starting real
        // sync there rotates the refresh token and the host is killed mid-request,
        // leaving the stored session revoked ("session expired" on next launch).
        if !isRunningUITests {
            SyncManager.shared.configureForCurrentSession()
        }
    }

    var body: some Scene {
        WindowGroup {
            ZStack(alignment: .top) {
                Group {
                    if !launchDataClear.isOpen {
                        Color(uiColor: .systemBackground).ignoresSafeArea()
                    } else if shouldShowOnboarding {
                        OnboardingView {
                            onboardingCompleted = true
                        }
                    } else {
                        MainBrowserView()
                    }
                }
                UpdateBannerView(checker: updateChecker)
            }
            .environmentObject(themeStore)
            .task {
                await updateChecker.check()
            }
            .onContinueUserActivity(HandoffManager.activityType) { userActivity in
                HandoffManager.shared.handleIncomingUserActivity(userActivity)
            }
            .onOpenURL { url in
                DeepLinkHandler.shared.handle(url)
            }
        }
        .onChange(of: scenePhase) { _, newPhase in
            switch newPhase {
            case .background:
                AutoArchiveBridge.send()
                MahoBridge.shared.saveState()
                SyncManager.shared.appDidEnterBackground()
            case .active:
                ConversationArchiveForegroundBridge.sweep()
                if !isRunningUITests {
                    SyncManager.shared.configureForCurrentSession()
                    FilterListUpdater.shared.refreshIfNeeded()
                }
                themeStore.refresh()
            case .inactive:
                break
            @unknown default:
                break
            }
        }
    }
}

/// Holds back the browser UI while launch-time data clearing is in flight.
/// Open by default; closed only between starting and finishing the clear.
@MainActor
final class LaunchDataClearGate: ObservableObject {
    static let shared = LaunchDataClearGate()

    @Published private(set) var isOpen = true

    func close() { isOpen = false }
    func open() { isOpen = true }

    private init() {}
}
