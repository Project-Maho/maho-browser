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
        E2ETestHooks.runOnBoot()
        SyncManager.registerBackgroundRefresh()
        SyncManager.shared.configureForCurrentSession()
    }

    var body: some Scene {
        WindowGroup {
            ZStack(alignment: .top) {
                Group {
                    if shouldShowOnboarding {
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
                SyncManager.shared.configureForCurrentSession()
                themeStore.refresh()
            case .inactive:
                break
            @unknown default:
                break
            }
        }
    }
}
