import WebKit
#if canImport(UIKit)
import UIKit
#endif

/// Captures a JPEG screenshot of a WKWebView's current visible content.
///
/// Uses `WKWebView.takeSnapshot(with:)` (iOS 11+), which correctly captures
/// GPU-composited content including videos and transformed layers.
///
/// - Parameter webView: The WKWebView to capture.
/// - Returns: JPEG-compressed image data, or `nil` on failure.
@MainActor
func captureWebViewScreenshot(_ webView: WKWebView) async -> Data? {
    let config = WKSnapshotConfiguration()
    // Capture the visible viewport at the natural device scale.
    config.snapshotWidth = NSNumber(value: Double(webView.bounds.width))

    return await withCheckedContinuation { continuation in
        webView.takeSnapshot(with: config) { image, error in
            guard error == nil, let image else {
                continuation.resume(returning: nil)
                return
            }
#if canImport(UIKit)
            let data = image.jpegData(compressionQuality: 0.8)
            continuation.resume(returning: data)
#else
            continuation.resume(returning: nil)
#endif
        }
    }
}
