import AppKit
import WebKit

final class LinkPreviewHandler: NSObject, WKScriptMessageHandler, CoreUpdateObserver {
    static let shared = LinkPreviewHandler()

    private let previewPopover = AIPreviewPopover()
    private weak var activeWebView: WKWebView?

    private static let searchEngineDomains: Set<String> = [
        "google.com", "www.google.com",
        "duckduckgo.com", "www.duckduckgo.com",
        "bing.com", "www.bing.com",
        "x.com", "www.x.com",
        "news.ycombinator.com",
    ]

    static let injectionScript: String = """
    (function() {
        let hoverTimeout = null;
        document.addEventListener('mouseover', function(e) {
            const link = e.target.closest('a[href]');
            if (!link) return;
            clearTimeout(hoverTimeout);
            hoverTimeout = setTimeout(function() {
                window.webkit.messageHandlers.linkHover.postMessage({
                    url: link.href,
                    text: link.textContent.substring(0, 200),
                    rect: JSON.stringify(link.getBoundingClientRect())
                });
            }, 300);
        });
        document.addEventListener('mouseout', function(e) {
            if (e.target.closest('a[href]')) {
                clearTimeout(hoverTimeout);
                window.webkit.messageHandlers.linkHover.postMessage({ cancelled: true });
            }
        });
    })();
    """

    private override init() {
        super.init()
    }

    func start() {
        CoreUpdateDispatcher.shared.register(self)
    }

    func userContentController(
        _ userContentController: WKUserContentController,
        didReceive message: WKScriptMessage
    ) {
        guard message.name == "linkHover" else { return }
        guard let body = message.body as? [String: Any] else { return }

        if body["cancelled"] as? Bool == true {
            previewPopover.dismiss()
            return
        }

        guard let url = body["url"] as? String,
              let rectJSON = body["rect"] as? String else { return }

        guard let settings = MahoBridge.shared.getSettings() else { return }
        guard settings.max.pagePreviews else { return }

        guard let webView = message.webView else { return }
        activeWebView = webView

        let isSearchEngine = isCurrentPageSearchEngine()
        if !isSearchEngine && !NSEvent.modifierFlags.contains(.shift) {
            return
        }

        let rect = parseRect(from: rectJSON, in: webView)

        if previewPopover.activeUrl == url && previewPopover.isShown {
            return
        }

        previewPopover.show(url: url, relativeTo: rect, of: webView)

        CoreUpdateDispatcher.shared.sendAndDispatch(
            .requestPagePreview(url: url)
        )
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        guard case .pagePreviewReady(let url, let title, let summary) = update else { return }
        DispatchQueue.main.async { [weak self] in
            self?.previewPopover.updateWithResult(url: url.value, title: title, summary: summary)
        }
    }

    private func isCurrentPageSearchEngine() -> Bool {
        guard let host = activeWebView?.url?.host else { return false }
        return Self.searchEngineDomains.contains(host)
    }

    private func parseRect(from json: String, in webView: WKWebView) -> NSRect {
        guard let data = json.data(using: .utf8),
              let dict = try? JSONSerialization.jsonObject(with: data) as? [String: Double],
              let x = dict["x"], let y = dict["y"],
              let width = dict["width"], let height = dict["height"] else {
            return NSRect(x: 0, y: 0, width: 1, height: 1)
        }

        let flippedY = webView.bounds.height - y - height
        return NSRect(x: x, y: flippedY, width: width, height: height)
    }
}
