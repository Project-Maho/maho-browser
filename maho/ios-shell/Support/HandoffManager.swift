import Foundation
import os.log

@MainActor
final class HandoffManager {
    static let shared = HandoffManager()
    static let activityType = "dev.maho.browser.browsing"
    private let log = Logger(subsystem: "dev.maho.browser", category: "HandoffManager")

    var onReceiveURL: ((URL) -> Void)?

    /// The single advertised browsing activity, retained so it can be resigned.
    private(set) var currentActivity: NSUserActivity?

    private init() {}

    /// Advertises `url` as the one current Handoff activity. Same URL keeps the
    /// existing activity; nil (or a non-web URL) resigns and clears it.
    func updateCurrentActivity(with url: URL?) {
        if let url, currentActivity?.webpageURL == url { return }
        clearCurrentActivity()
        guard let url, let activity = makeActivity(with: url) else { return }
        activity.becomeCurrent()
        currentActivity = activity
    }

    func clearCurrentActivity() {
        guard let activity = currentActivity else { return }
        currentActivity = nil
        clearActivity(activity)
    }

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

        log.info("Received Handoff URL: \(url.absoluteString, privacy: .private)")
        if let onReceiveURL {
            onReceiveURL(url)
        } else {
            // Arrived before the browser view installed its handlers (cold
            // launch, onboarding, launch data clear); it opens on appear.
            DeepLinkHandler.shared.openURL(url.absoluteString)
        }
        return true
    }
}
