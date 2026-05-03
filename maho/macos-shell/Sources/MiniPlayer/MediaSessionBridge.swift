import WebKit

final class MediaSessionBridge: NSObject, WKScriptMessageHandler {
    static let shared = MediaSessionBridge()

    static let injectionScript = """
    (function() {
        if (window.__mahoMediaBridge) return;
        window.__mahoMediaBridge = true;

        const observer = new MutationObserver(() => {
            const videos = document.querySelectorAll('video');
            const audios = document.querySelectorAll('audio');
            const media = videos.length > 0 ? videos[0] : (audios.length > 0 ? audios[0] : null);
            if (media) {
                window.webkit.messageHandlers.mediaSession.postMessage({
                    type: 'mediaFound',
                    duration: media.duration || 0,
                    currentTime: media.currentTime || 0,
                    paused: media.paused
                });
            }
        });
        observer.observe(document.body, { childList: true, subtree: true });
    })();
    """

    static let skipBackScript = """
    (function() {
        const media = document.querySelector('video') || document.querySelector('audio');
        if (media) { media.currentTime = Math.max(0, media.currentTime - 10); }
    })();
    """

    static let skipForwardScript = """
    (function() {
        const media = document.querySelector('video') || document.querySelector('audio');
        if (media) { media.currentTime = Math.min(media.duration, media.currentTime + 10); }
    })();
    """

    static let playPauseScript = """
    (function() {
        const media = document.querySelector('video') || document.querySelector('audio');
        if (media) { media.paused ? media.play() : media.pause(); }
    })();
    """

    private(set) var lastMediaState: MediaState?

    struct MediaState {
        let type: String
        let duration: Double
        let currentTime: Double
        let paused: Bool
    }

    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
        guard let body = message.body as? [String: Any] else {
            NSLog("[MediaSessionBridge] Received non-dictionary message, ignoring")
            return
        }
        guard let type = body["type"] as? String else {
            NSLog("[MediaSessionBridge] Message missing 'type' field: \(body.keys.sorted())")
            return
        }

        switch type {
        case "mediaFound":
            let duration = body["duration"] as? Double ?? 0
            let currentTime = body["currentTime"] as? Double ?? 0
            let paused = body["paused"] as? Bool ?? true
            lastMediaState = MediaState(type: type, duration: duration, currentTime: currentTime, paused: paused)
            NotificationCenter.default.post(name: .mediaSessionStateChanged, object: nil)
        default:
            NSLog("[MediaSessionBridge] Unknown message type: \(type)")
        }
    }
}
