import Foundation
import os.log

@MainActor
final class DeepLinkHandler {
    static let shared = DeepLinkHandler()
    private let log = Logger(subsystem: "dev.maho.browser", category: "DeepLinkHandler")

    var onOpenURL: ((String) -> Void)?
    var onSearch: ((String) -> Void)?
    var onNewTab: (() -> Void)?
    var pendingNewTabSearch = false

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
            onOpenURL?(target.absoluteString)
            return true

        case "search":
            // An empty query (e.g. the widget's search entry) opens a new tab
            // instead of a blank web search.
            guard let query = components.queryItems?.first(where: { $0.name == "q" })?.value,
                  !query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
                onNewTab?()
                return true
            }
            onSearch?(query)
            return true

        case "newtab":
            onNewTab?()
            return true

        default:
            log.warning("Unhandled deep link host: \(host, privacy: .public)")
            return false
        }
    }
}
