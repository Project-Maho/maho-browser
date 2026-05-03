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
        return sharedURL != nil || !contentText.isEmpty
    }

    override func didSelectPost() {
        let urlToShare: URL?
        if let sharedURL = sharedURL {
            urlToShare = sharedURL
        } else if !contentText.isEmpty, let url = URL(string: contentText) {
            urlToShare = url
        } else {
            urlToShare = nil
        }

        if let url = urlToShare,
           let openURL = URL(string: "maho://open?url=\(url.absoluteString.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? "")") {
            extensionContext?.open(openURL) { _ in }
        }

        extensionContext?.completeRequest(returningItems: nil)
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
                                completion(value as? URL)
                            }
                        }
                        return
                    } else if provider.hasItemConformingToTypeIdentifier(UTType.plainText.identifier) {
                        provider.loadItem(forTypeIdentifier: UTType.plainText.identifier, options: nil) { (value, _) in
                            DispatchQueue.main.async {
                                if let text = value as? String, let url = URL(string: text) {
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
