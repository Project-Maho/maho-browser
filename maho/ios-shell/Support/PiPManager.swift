import Foundation
import WebKit
import os.log

@MainActor
final class PiPManager {
    static let shared = PiPManager()
    private let log = Logger(subsystem: "dev.maho.browser", category: "PiPManager")

    private init() {}

    func enablePiP(for webView: WKWebView) {
        webView.configuration.allowsInlineMediaPlayback = true
        webView.configuration.allowsPictureInPictureMediaPlayback = true

        let script = """
        (function() {
            const videos = document.querySelectorAll('video');
            if (videos.length === 0) { return 'no_video'; }
            for (let video of videos) {
                video.webkitSetPresentationMode('picture-in-picture');
            }
            return 'pip_triggered';
        })();
        """

        webView.evaluateJavaScript(script) { result, error in
            if let error = error {
                self.log.error("PiP script error: \(error.localizedDescription, privacy: .public)")
            } else if let result = result {
                self.log.info("PiP result: \(String(describing: result), privacy: .public)")
            }
        }
    }

    func hasVideoElement(in webView: WKWebView, completion: @escaping (Bool) -> Void) {
        let script = """
        (function() {
            return document.querySelectorAll('video').length > 0;
        })();
        """
        webView.evaluateJavaScript(script) { result, _ in
            completion(result as? Bool ?? false)
        }
    }
}
