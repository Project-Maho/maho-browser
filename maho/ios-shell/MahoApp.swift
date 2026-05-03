import SwiftUI

@main
struct MahoApp: App {
    @Environment(\.scenePhase) private var scenePhase
    @AppStorage("onboardingCompleted") private var onboardingCompleted: Bool = false
    @StateObject private var updateChecker = AppStoreUpdateChecker()

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

        MahoBridge.shared.initialize(storagePath: storagePath)
        MahoBridge.shared.loadState()
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
            case .active:
                SyncManager.shared.configureForCurrentSession()
            case .inactive:
                break
            @unknown default:
                break
            }
        }
    }
}
