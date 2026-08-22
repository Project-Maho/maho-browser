import Foundation

/// Test-only boot hook for UITest end-to-end persistence verification.
///
/// This entry point is intentionally driven by launch environment variables
/// so a UITest can seed a conversation before force-quit and verify it on
/// relaunch. All logic is guarded by env presence and is a no-op in normal runs.
enum E2ETestHooks {
    static let uiTestModeEnvKey = "MAHO_UITEST_MODE"
    static let seedEnvKey = "MAHO_E2E_SEED_CONVERSATION"
    static let archivedTabSeedEnvKey = "MAHO_E2E_SEED_ARCHIVED_TAB"
    static let addressBarFixtureEnvKey = "MAHO_E2E_SEED_ADDRESS_BAR_TAB"
    static let verifyEnvKey = "MAHO_E2E_VERIFY_CONVERSATION_TITLE"
    static let resultOutputEnvKey = "MAHO_E2E_VERIFY_RESULT"

    static var seededTitle: String?
    static var archiveFixtureStatus: ArchiveFixtureStatus?
    static var archiveFixtureReady = false
    static var addressBarFixtureStatus: AddressBarFixtureStatus?
    static var verifyResult: VerifyResult = .notRun

    enum ArchiveFixtureStatus: String {
        case started
        case bridgeUnavailable
        case noActiveSpace
        case fixtureTabMissing
        case controlTabMissing
        case archivedTabsDecodeFailed
        case fixtureNotArchived
        case wrongActiveTab
        case ready
    }

    enum AddressBarFixtureStatus: String {
        case started
        case bridgeUnavailable
        case noActiveSpace
        case fixtureTabMissing
        case wrongTabCount
        case wrongActiveTab
        case wrongFixtureUrl
        case ready
    }

    enum VerifyResult: String {
        case notRun
        case matched
        case missing
    }

    static func runOnBoot() {
        let env = ProcessInfo.processInfo.environment
        let isUITest = env[uiTestModeEnvKey] == "1"
        let hasSeed = (env[seedEnvKey]?.isEmpty == false)
        let hasVerify = (env[verifyEnvKey]?.isEmpty == false)

        guard isUITest || hasSeed || hasVerify else { return }

        UserDefaults.standard.set(true, forKey: "onboardingCompleted")

        if let title = env[seedEnvKey], !title.isEmpty {
            seedConversation(title: title)
        }

        if isUITest, let title = env[archivedTabSeedEnvKey], !title.isEmpty {
            archiveFixtureStatus = .started
            seedArchivedTab(title: title)
            archiveFixtureReady = archiveFixtureStatus == .ready
        }

        if isUITest, let url = env[addressBarFixtureEnvKey], !url.isEmpty {
            addressBarFixtureStatus = .started
            seedAddressBarTab(url: url)
        }

        if let expected = env[verifyEnvKey], !expected.isEmpty {
            verifyResult = verify(expectedTitle: expected)
        }

        E2EAgenticJourney.shared.armAndStartIfRequested()
    }

    private static func seedConversation(title: String) {
        let id = "e2e-\(UUID().uuidString)"
        let created = MahoBridge.shared.createConversation(
            id: id,
            title: title,
            spaceId: nil,
            model: "e2e-test"
        )
        guard created else { return }
        _ = MahoBridge.shared.saveConversationMessage(
            sessionId: id,
            role: "user",
            content: "E2E seed message",
            urlContext: nil
        )
        MahoBridge.shared.saveState()
        seededTitle = title
    }

    private static func seedArchivedTab(title: String) {
        let bridge = MahoBridge.shared
        guard bridge.isInitialized else {
            archiveFixtureStatus = .bridgeUnavailable
            return
        }
        guard let spaceId = bridge.getActiveSpaceId() else {
            archiveFixtureStatus = .noActiveSpace
            return
        }

        if let archivedJson = bridge.getArchivedTabs(spaceId: spaceId),
           let archivedData = archivedJson.data(using: .utf8),
           let archivedTabs = try? JSONDecoder().decode([ArchivedTabViewModel].self, from: archivedData) {
            for tab in archivedTabs where tab.title == title {
                bridge.sendEvent(.restoreArchivedTab(tabId: tab.id))
                bridge.closeTab(id: tab.id)
            }
        }
        for tab in bridge.getTabViewModels() where tab.spaceId == spaceId {
            bridge.closeTab(id: tab.id)
        }

        let fixtureUrl = Url("https://example.invalid/todo-6-archive-fixture")
        let tabsBeforeFixture = Set(bridge.getTabViewModels().map(\.id))
        bridge.createTab(url: fixtureUrl, inSpace: spaceId)
        guard let fixtureTabId = bridge.getTabViewModels().first(where: {
            !tabsBeforeFixture.contains($0.id)
        })?.id else {
            archiveFixtureStatus = .fixtureTabMissing
            return
        }
        bridge.sendEvent(.tabTitleUpdated(tabId: fixtureTabId, title: title))
        bridge.sendEvent(.tabUrlUpdated(tabId: fixtureTabId, url: fixtureUrl))

        let tabsBeforeControl = Set(bridge.getTabViewModels().map(\.id))
        bridge.createTab(url: Url("https://example.invalid/todo-6-archive-control"), inSpace: spaceId)
        guard let controlTabId = bridge.getTabViewModels().first(where: {
            !tabsBeforeControl.contains($0.id)
        })?.id else {
            archiveFixtureStatus = .controlTabMissing
            return
        }
        bridge.sendEvent(.tabTitleUpdated(tabId: controlTabId, title: "Todo-6-Archive-Control"))

        bridge.activateTab(id: fixtureTabId)
        bridge.archiveTab(id: fixtureTabId)
        bridge.activateTab(id: controlTabId)

        guard let archivedJson = bridge.getArchivedTabs(spaceId: spaceId),
              let archivedData = archivedJson.data(using: .utf8),
              let archivedTabs = try? JSONDecoder().decode([ArchivedTabViewModel].self, from: archivedData) else {
            archiveFixtureStatus = .archivedTabsDecodeFailed
            return
        }
        guard archivedTabs.contains(where: { $0.id == fixtureTabId && $0.title == title }) else {
            archiveFixtureStatus = .fixtureNotArchived
            return
        }
        guard bridge.getActiveTabId() == controlTabId else {
            archiveFixtureStatus = .wrongActiveTab
            return
        }
        archiveFixtureStatus = .ready
    }

    private static func seedAddressBarTab(url: String) {
        let bridge = MahoBridge.shared
        guard bridge.isInitialized else {
            addressBarFixtureStatus = .bridgeUnavailable
            return
        }
        guard let spaceId = bridge.getActiveSpaceId() else {
            addressBarFixtureStatus = .noActiveSpace
            return
        }

        for tab in bridge.getTabViewModels() where tab.spaceId == spaceId && !tab.isPrivate {
            bridge.closeTab(id: tab.id)
        }

        let existingTabIds = Set(bridge.getTabViewModels().map(\.id))
        let fixtureURL = Url(url)
        bridge.createTab(url: fixtureURL, inSpace: spaceId)
        guard let tabId = bridge.getTabViewModels().first(where: {
            !existingTabIds.contains($0.id)
        })?.id else {
            addressBarFixtureStatus = .fixtureTabMissing
            return
        }

        bridge.sendEvent(.tabTitleUpdated(tabId: tabId, title: "Address Bar Fixture"))
        bridge.sendEvent(.tabUrlUpdated(tabId: tabId, url: fixtureURL))
        bridge.activateTab(id: tabId)

        let spaceTabs = bridge.getTabViewModels().filter { $0.spaceId == spaceId }
        guard spaceTabs.count == 1 else {
            addressBarFixtureStatus = .wrongTabCount
            return
        }
        guard bridge.getActiveTabId() == tabId else {
            addressBarFixtureStatus = .wrongActiveTab
            return
        }
        guard spaceTabs.first?.url == url else {
            addressBarFixtureStatus = .wrongFixtureUrl
            return
        }
        addressBarFixtureStatus = .ready
    }

    private static func verify(expectedTitle: String) -> VerifyResult {
        let conversations = MahoBridge.shared.listConversations(limit: 500)
        let match = conversations.contains { $0.title == expectedTitle }
        return match ? .matched : .missing
    }
}
