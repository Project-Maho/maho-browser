import Foundation
import os.log

/// An intent that arrived before the browser view installed its handlers.
/// Queued in arrival order so a second intent cannot replace the first.
enum PendingDeepLink {
    case open(String)
    case search(String)
    case newTab
}

@MainActor
final class DeepLinkHandler {
    static let shared = DeepLinkHandler()
    private let log = Logger(subsystem: "dev.maho.browser", category: "DeepLinkHandler")

    var onOpenURL: ((String) -> Void)?
    var onSearch: ((String) -> Void)?
    var onNewTab: (() -> Void)?
    /// Set by App Intents and deep links that arrive before the browser view
    /// installs its handlers (cold launch, onboarding, launch data clear); the
    /// browser view drains them in arrival order when it appears.
    private var pendingDeepLinks: [PendingDeepLink] = []

    private init() {}

    @discardableResult
    func handle(_ url: URL) -> Bool {
        guard let components = URLComponents(url: url, resolvingAgainstBaseURL: true),
              let host = components.host else {
            return false
        }

        log.info("Handling deep link: \(url.absoluteString, privacy: .private)")

        switch host {
        case "open":
            guard let urlParam = components.queryItems?.first(where: { $0.name == "url" })?.value,
                  let target = URL(string: urlParam),
                  let scheme = target.scheme?.lowercased(),
                  scheme == "http" || scheme == "https",
                  target.host?.isEmpty == false else {
                log.warning("Rejected deep link open target with unsupported scheme")
                return false
            }
            openURL(target.absoluteString)
            return true

        case "search":
            // An empty query (e.g. the widget's search entry) opens a new tab
            // instead of a blank web search.
            guard let query = components.queryItems?.first(where: { $0.name == "q" })?.value,
                  !query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
                openNewTab()
                return true
            }
            openSearch(query)
            return true

        case "newtab":
            openNewTab()
            return true

        default:
            log.warning("Unhandled deep link host: \(host, privacy: .public)")
            return false
        }
    }

    /// Opens `urlString` now, or keeps it for the browser view when its
    /// handlers are not installed yet.
    func openURL(_ urlString: String) {
        if let onOpenURL {
            onOpenURL(urlString)
        } else {
            enqueue(.open(urlString))
        }
    }

    /// Runs `query` now, or keeps it for the browser view when its handlers are
    /// not installed yet.
    func openSearch(_ query: String) {
        if let onSearch {
            onSearch(query)
        } else {
            enqueue(.search(query))
        }
    }

    /// Opens a new tab now, or keeps it for the browser view when its handlers
    /// are not installed yet.
    func openNewTab() {
        if let onNewTab {
            onNewTab()
        } else {
            enqueue(.newTab)
        }
    }

    /// Takes the queued intents in arrival order for the browser view to replay
    /// after installing its handlers. Draining clears the queue, so a repeated
    /// call (a second `onAppear`) cannot replay an intent.
    func takePendingDeepLinks() -> [PendingDeepLink] {
        let queued = pendingDeepLinks
        pendingDeepLinks.removeAll()
        return queued
    }

    private func enqueue(_ link: PendingDeepLink) {
        pendingDeepLinks.append(link)
        log.info(
            "Queued deep link until the browser view appears (depth \(self.pendingDeepLinks.count)): \(String(describing: link), privacy: .private)"
        )
    }
}
