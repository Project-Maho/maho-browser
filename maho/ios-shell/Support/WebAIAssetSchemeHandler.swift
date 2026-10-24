import Foundation
import UniformTypeIdentifiers
import WebKit

/// Serves the web-ai bundle from `maho-webai://app/` instead of `file://`.
///
/// WKWebView treats a `file://` page as an opaque origin and denies its
/// `<script type="module">` loads under CORS, so the bundle never boots. A
/// custom scheme gives the page a real origin while keeping every response
/// confined to the resolved web-ai root directory.
final class WebAIAssetSchemeHandler: NSObject, WKURLSchemeHandler {
    static let scheme = "maho-webai"
    static let host = "app"

    private let rootProvider: () -> URL?

    init(rootProvider: @escaping () -> URL? = WebAIAssetSchemeHandler.currentRoot) {
        self.rootProvider = rootProvider
    }

    /// The directory holding `index.html`: provisioned storage first, then the app bundle.
    static func currentRoot() -> URL? {
        let storagePath = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("dev.maho.browser", isDirectory: true)
            .path
        if let storagePath,
           let provisioned = MahoBridge.shared.getProvisionedAssetURL(storagePath: storagePath, relativePath: "web-ai/index.html") {
            return provisioned.deletingLastPathComponent()
        }
        return Bundle.main.url(forResource: "index", withExtension: "html", subdirectory: "web-ai")?
            .deletingLastPathComponent()
    }

    static func entryURL(fragment: String) -> URL? {
        URL(string: "\(scheme)://\(host)/index.html\(fragment)")
    }

    /// Maps a scheme URL to a regular file inside `root`; nil for foreign hosts,
    /// traversal outside the root, or missing files.
    static func resolve(url: URL, root: URL) -> URL? {
        guard url.scheme?.lowercased() == scheme, url.host?.lowercased() == host else { return nil }
        var path = url.path
        if path.isEmpty || path == "/" { path = "/index.html" }
        let rootPath = root.standardizedFileURL.resolvingSymlinksInPath().path
        let candidate = root.appendingPathComponent(String(path.dropFirst()))
            .standardizedFileURL.resolvingSymlinksInPath()
        guard candidate.path.hasPrefix(rootPath + "/") else { return nil }
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: candidate.path, isDirectory: &isDirectory),
              !isDirectory.boolValue else { return nil }
        return candidate
    }

    static func mimeType(for file: URL) -> String {
        switch file.pathExtension.lowercased() {
        case "js", "mjs": return "text/javascript"
        case "html": return "text/html"
        case "css": return "text/css"
        case "json": return "application/json"
        case "wasm": return "application/wasm"
        default: return UTType(filenameExtension: file.pathExtension)?.preferredMIMEType ?? "application/octet-stream"
        }
    }

    func webView(_ webView: WKWebView, start urlSchemeTask: WKURLSchemeTask) {
        guard let url = urlSchemeTask.request.url,
              let root = rootProvider(),
              let file = Self.resolve(url: url, root: root),
              let data = try? Data(contentsOf: file) else {
            urlSchemeTask.didFailWithError(URLError(.fileDoesNotExist))
            return
        }
        let response = HTTPURLResponse(
            url: url,
            statusCode: 200,
            httpVersion: "HTTP/1.1",
            headerFields: [
                "Content-Type": Self.mimeType(for: file),
                "Content-Length": String(data.count),
                "Cache-Control": "no-store",
            ]
        )!
        urlSchemeTask.didReceive(response)
        urlSchemeTask.didReceive(data)
        urlSchemeTask.didFinish()
    }

    func webView(_ webView: WKWebView, stop urlSchemeTask: WKURLSchemeTask) {}
}
