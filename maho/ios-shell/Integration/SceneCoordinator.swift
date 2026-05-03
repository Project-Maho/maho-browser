import Foundation
import os.log

@MainActor
final class SceneCoordinator {
    static let shared = SceneCoordinator()
    private let log = Logger(subsystem: "dev.maho.browser", category: "Scene")
    private let defaults = UserDefaults.standard

    private let activeSpaceKey = "dev.maho.browser.restoredActiveSpaceId"
    private let activeTabKey = "dev.maho.browser.restoredActiveTabId"

    private init() {}

    func sceneWillConnect() {
        log.info("Scene will connect")
        restoreStateIfNeeded()
    }

    func sceneDidDisconnect() {
        log.info("Scene did disconnect")
        saveRestorationState()
    }

    func sceneDidEnterBackground() {
        log.info("Scene did enter background")
        MahoBridge.shared.saveState()
        saveRestorationState()
    }

    func sceneWillEnterForeground() {
        log.info("Scene will enter foreground")
        MahoBridge.shared.loadState()
    }

    func sceneDidBecomeActive() {
        log.info("Scene did become active")
    }

    func sceneWillResignActive() {
        log.info("Scene will resign active")
    }

    private func saveRestorationState() {
        if let spaceId = MahoBridge.shared.getActiveSpaceId() {
            defaults.set(spaceId, forKey: activeSpaceKey)
        }
        if let tabId = MahoBridge.shared.getActiveTabId() {
            defaults.set(tabId, forKey: activeTabKey)
        }
    }

    private func restoreStateIfNeeded() {
        let spaceId = defaults.string(forKey: activeSpaceKey)
        let tabId = defaults.string(forKey: activeTabKey)

        if let spaceId {
            MahoBridge.shared.sendEvent(.activateSpace(spaceId: spaceId))
        }
        if let tabId {
            MahoBridge.shared.sendEvent(.activateTab(tabId: tabId))
        }
    }
}
