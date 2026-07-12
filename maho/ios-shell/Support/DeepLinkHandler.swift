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

        log.info("Handling deep link: \(url.absoluteString, privacy: .public)")

        switch host {
        case "open":
            guard let urlParam = components.queryItems?.first(where: { $0.name == "url" })?.value else {
                return false
            }
            onOpenURL?(urlParam)
            return true

        case "search":
            guard let query = components.queryItems?.first(where: { $0.name == "q" })?.value else {
                return false
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
