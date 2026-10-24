import UIKit
import Social
import UniformTypeIdentifiers
import MobileCoreServices

final class ShareViewController: SLComposeServiceViewController {
    private var sharedURL: URL?

    override func viewDidLoad() {
        super.viewDidLoad()
        placeholder = "Share to Maho"
        extractSharedURL { [weak self] url in
            self?.sharedURL = url
            if let url = url {
                self?.textView.text = url.absoluteString
            }
        }
    }

    override func isContentValid() -> Bool {
        return resolvedWebURL() != nil
    }

    override func didSelectPost() {
        // Only http(s) URLs are forwarded; anything else (javascript:, file:,
        // custom schemes, free text) is dropped so the host app never receives
        // a non-web URL through maho://open.
        //
        // Note: iOS does not let a share extension launch its host app —
        // NSExtensionContext.open(_:) is honored only for Today/widget
        // extensions and reports failure here. A reliable handoff needs a
        // shared App Group container (not provisioned yet); until then this
        // is a best-effort attempt and the request completes either way.
        if let url = resolvedWebURL(), let openURL = Self.mahoOpenURL(for: url) {
            extensionContext?.open(openURL) { _ in }
        }

        extensionContext?.completeRequest(returningItems: nil)
    }

    private func resolvedWebURL() -> URL? {
        if let sharedURL = Self.webURL(sharedURL) {
            return sharedURL
        }
        let text = contentText.trimmingCharacters(in: .whitespacesAndNewlines)
        return Self.webURL(URL(string: text))
    }

    static func webURL(_ url: URL?) -> URL? {
        guard let url,
              let scheme = url.scheme?.lowercased(),
              scheme == "http" || scheme == "https",
              let host = url.host, !host.isEmpty else {
            return nil
        }
        return url
    }

    /// Builds `maho://open?url=<encoded>`, percent-encoding every reserved
    /// query delimiter so a shared URL with its own query string or fragment
    /// survives DeepLinkHandler's URLComponents parsing intact.
    static func mahoOpenURL(for url: URL) -> URL? {
        var allowed = CharacterSet.urlQueryAllowed
        allowed.remove(charactersIn: "&=+?#")
        guard let encoded = url.absoluteString.addingPercentEncoding(withAllowedCharacters: allowed) else {
            return nil
        }
        return URL(string: "maho://open?url=\(encoded)")
    }

    override func configurationItems() -> [Any]! {
        return []
    }

    private func extractSharedURL(completion: @escaping (URL?) -> Void) {
        guard let items = extensionContext?.inputItems as? [NSExtensionItem] else {
            completion(nil)
            return
        }

        for item in items {
            if let attachments = item.attachments {
                for provider in attachments {
                    if provider.hasItemConformingToTypeIdentifier(UTType.url.identifier) {
                        provider.loadItem(forTypeIdentifier: UTType.url.identifier, options: nil) { (value, _) in
                            DispatchQueue.main.async {
                                completion(Self.webURL(value as? URL))
                            }
                        }
                        return
                    } else if provider.hasItemConformingToTypeIdentifier(UTType.plainText.identifier) {
                        provider.loadItem(forTypeIdentifier: UTType.plainText.identifier, options: nil) { (value, _) in
                            DispatchQueue.main.async {
                                if let text = value as? String,
                                   let url = Self.webURL(URL(string: text.trimmingCharacters(in: .whitespacesAndNewlines))) {
                                    completion(url)
                                } else {
                                    completion(nil)
                                }
                            }
                        }
                        return
                    }
                }
            }
        }

        completion(nil)
    }
}
