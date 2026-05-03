import AppKit
import WebKit

final class TabPreviewManager: CoreUpdateObserver {
    static let shared = TabPreviewManager()

    private var cache: [TabId: NSImage] = [:]
    private var accessOrder: [TabId] = []
    private let maxCacheSize = 50

    private init() {
        CoreUpdateDispatcher.shared.register(self)
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    // MARK: - Capture

    func capturePreview(tabId: TabId, webView: WKWebView, completion: @escaping (NSImage?) -> Void) {
        let config = WKSnapshotConfiguration()
        config.snapshotWidth = 300
        webView.takeSnapshot(with: config) { [weak self] image, error in
            DispatchQueue.main.async {
                guard let self = self, let image = image else {
                    completion(nil)
                    return
                }
                self.storeInCache(tabId: tabId, image: image)
                self.sendPreviewToBridge(tabId: tabId, image: image)
                completion(image)
            }
        }
    }

    // MARK: - Cache

    func getCachedPreview(for tabId: TabId) -> NSImage? {
        if let image = cache[tabId] {
            touchAccessOrder(tabId)
            return image
        }
        return nil
    }

    func invalidate(tabId: TabId) {
        cache.removeValue(forKey: tabId)
        accessOrder.removeAll { $0 == tabId }
    }

    private func storeInCache(tabId: TabId, image: NSImage) {
        cache[tabId] = image
        touchAccessOrder(tabId)
        evictIfNeeded()
    }

    private func touchAccessOrder(_ tabId: TabId) {
        accessOrder.removeAll { $0 == tabId }
        accessOrder.append(tabId)
    }

    private func evictIfNeeded() {
        while cache.count > maxCacheSize, let oldest = accessOrder.first {
            cache.removeValue(forKey: oldest)
            accessOrder.removeFirst()
        }
    }

    private func sendPreviewToBridge(tabId: TabId, image: NSImage) {
        guard let tiff = image.tiffRepresentation,
              let bitmap = NSBitmapImageRep(data: tiff),
              let png = bitmap.representation(using: .png, properties: [:]) else { return }
        MahoBridge.shared.updateTabPreview(tabId: tabId, data: Array(png))
    }

    // MARK: - CoreUpdateObserver

    func handleCoreUpdate(_ update: CoreUpdate) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            switch update {
            case .tabPreviewCaptureRequested(let tabId):
                // Delegate to WebViewManager which has the actual WKWebView
                NotificationCenter.default.post(
                    name: .mahoTabPreviewCaptureRequested,
                    object: nil,
                    userInfo: ["tabId": tabId]
                )
            case .tabPreviewUpdated(let tabId, let previewData):
                if let data = Data(base64Encoded: previewData),
                   let image = NSImage(data: data) {
                    self.storeInCache(tabId: tabId, image: image)
                }
            case .tabClosed(let tabId, _):
                self.invalidate(tabId: tabId)
            default:
                break
            }
        }
    }
}

extension Notification.Name {
    static let mahoTabPreviewCaptureRequested = Notification.Name("mahoTabPreviewCaptureRequested")
}
