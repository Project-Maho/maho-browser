import AppKit
import AuthenticationServices
import WebKit

/// Integrates with macOS AuthenticationServices to request credentials from system providers
/// (Apple Passwords, 1Password, Bitwarden, etc.) and passkeys for autofill in WKWebView.
///
/// Posts `AutofillCredentialReceived` when a password credential is obtained, and
/// `AutofillPasskeyAssertionReceived` for passkey assertions. AutofillHandler should
/// observe `AutofillCredentialReceived` to fill form fields via its fillForm logic.
final class CredentialProviderManager: NSObject, ASAuthorizationControllerDelegate, ASAuthorizationControllerPresentationContextProviding {

    static let shared = CredentialProviderManager()

    private weak var activeWebView: WKWebView?
    private var activeFieldId: String?

    private override init() {
        super.init()
    }

    func start() {
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleSystemProviderRequest(_:)),
            name: Notification.Name("AutofillSystemProviderRequested"),
            object: nil
        )
    }

    @objc private func handleSystemProviderRequest(_ notification: Notification) {
        guard let userInfo = notification.userInfo,
              let domain = userInfo["domain"] as? String,
              let webView = userInfo["webView"] as? WKWebView,
              let fieldId = userInfo["fieldId"] as? String else { return }

        self.activeWebView = webView
        self.activeFieldId = fieldId

        let passwordProvider = ASAuthorizationPasswordProvider()
        let passwordRequest = passwordProvider.createRequest()

        var requests: [ASAuthorizationRequest] = [passwordRequest]

        if #available(macOS 12.0, *) {
            if !domain.isEmpty {
                let passkeyProvider = ASAuthorizationPlatformPublicKeyCredentialProvider(relyingPartyIdentifier: domain)
                let passkeyRequest = passkeyProvider.createCredentialAssertionRequest(challenge: Data())
                requests.append(passkeyRequest)
            }
        }

        let controller = ASAuthorizationController(authorizationRequests: requests)
        controller.delegate = self
        controller.presentationContextProvider = self
        controller.performRequests()
    }

    func authorizationController(controller: ASAuthorizationController, didCompleteWithAuthorization authorization: ASAuthorization) {
        switch authorization.credential {
        case let passwordCredential as ASPasswordCredential:
            guard let webView = activeWebView else { return }
            let formData: [String: String] = [
                "username": passwordCredential.user,
                "password": passwordCredential.password
            ]
            NotificationCenter.default.post(
                name: Notification.Name("AutofillCredentialReceived"),
                object: nil,
                userInfo: [
                    "webView": webView,
                    "formData": formData,
                    "fieldId": activeFieldId ?? ""
                ]
            )

        default:
            if #available(macOS 12.0, *) {
                if let passkeyCredential = authorization.credential as? ASAuthorizationPlatformPublicKeyCredentialAssertion {
                    guard let webView = activeWebView else { return }
                    let response: [String: Any] = [
                        "authenticatorData": passkeyCredential.rawAuthenticatorData.base64EncodedString(),
                        "clientDataJSON": passkeyCredential.rawClientDataJSON.base64EncodedString(),
                        "signature": passkeyCredential.signature.base64EncodedString(),
                        "userHandle": passkeyCredential.userID.base64EncodedString()
                    ]
                    NotificationCenter.default.post(
                        name: Notification.Name("AutofillPasskeyAssertionReceived"),
                        object: nil,
                        userInfo: [
                            "webView": webView,
                            "response": response
                        ]
                    )
                }
            }
        }

        activeWebView = nil
        activeFieldId = nil
    }

    func authorizationController(controller: ASAuthorizationController, didCompleteWithError error: Error) {
        NSLog("CredentialProviderManager: Authorization error: \(error.localizedDescription)")
        activeWebView = nil
        activeFieldId = nil
    }

    func presentationAnchor(for controller: ASAuthorizationController) -> ASPresentationAnchor {
        return activeWebView?.window ?? NSApp.keyWindow ?? NSApp.mainWindow ?? NSWindow()
    }
}
