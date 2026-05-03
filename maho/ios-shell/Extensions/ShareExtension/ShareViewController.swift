import Foundation
import UniformTypeIdentifiers

final class ShareViewController: UIViewController {
    private let sharedDefaultsKey = "dev.maho.browser.sharedURL"
    private let appGroupID = "group.dev.maho.browser"

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground

        let indicator = UIActivityIndicatorView(style: .large)
        indicator.translatesAutoresizingMaskIntoConstraints = false
        indicator.startAnimating()
        view.addSubview(indicator)
        NSLayoutConstraint.activate([
            indicator.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            indicator.centerYAnchor.constraint(equalTo: view.centerYAnchor)
        ])

        extractSharedURL { [weak self] url in
            guard let self, let url else {
                self?.extensionContext?.completeRequest(returningItems: nil)
                return
            }
            self.storeSharedURL(url)
            self.openMainApp(with: url)
            self.extensionContext?.completeRequest(returningItems: nil)
        }
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

    private func storeSharedURL(_ url: URL) {
        guard let defaults = UserDefaults(suiteName: appGroupID) else { return }
        defaults.set(url.absoluteString, forKey: sharedDefaultsKey)
        defaults.set(Date().timeIntervalSince1970, forKey: "\(sharedDefaultsKey).timestamp")
    }

    private func openMainApp(with url: URL) {
        guard let openURL = URL(string: "maho://open?url=\(url.absoluteString.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? "")") else { return }
        extensionContext?.open(openURL) { _ in }
    }
}
