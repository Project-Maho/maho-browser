import Foundation
import os.log

@MainActor
final class HandoffManager {
    static let shared = HandoffManager()
    static let activityType = "dev.maho.browser.browsing"
    private let log = Logger(subsystem: "dev.maho.browser", category: "HandoffManager")

    var onReceiveURL: ((URL) -> Void)?

    private init() {}

    func makeActivity(with url: URL) -> NSUserActivity? {
        guard let scheme = url.scheme?.lowercased(), scheme == "http" || scheme == "https" else {
            return nil
        }

        let activity = NSUserActivity(activityType: Self.activityType)
        activity.webpageURL = url
        activity.title = url.absoluteString
        activity.isEligibleForHandoff = true
        activity.isEligibleForSearch = false
        activity.isEligibleForPublicIndexing = false
        activity.becomeCurrent()
        return activity
    }

    func clearActivity(_ activity: NSUserActivity) {
        activity.resignCurrent()
        activity.invalidate()
    }

    func handleIncomingUserActivity(_ userActivity: NSUserActivity) -> Bool {
        guard userActivity.activityType == Self.activityType,
              let url = userActivity.webpageURL else {
            return false
        }

        log.info("Received Handoff URL: \(url.absoluteString, privacy: .public)")
        onReceiveURL?(url)
        return true
    }
}
