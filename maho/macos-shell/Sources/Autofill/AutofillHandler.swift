import WebKit

final class AutofillHandler: NSObject, WKScriptMessageHandler, CoreUpdateObserver {
    static let shared = AutofillHandler()

    static let fieldDetected = Notification.Name("AutofillFieldDetected")
    static let fieldBlurred = Notification.Name("AutofillFieldBlurred")
    static let systemProviderRequested = Notification.Name("AutofillSystemProviderRequested")

    static let injectionScript: String = """
    (function() {
        'use strict';

        var ATTR_ID = 'data-maho-autofill-id';
        var nextId = 1;
        var trackedFields = {};

        // ---------- Classification ----------

        var passwordAutocompletes = new Set([
            'current-password', 'new-password', 'username'
        ]);

        var paymentAutocompletes = new Set([
            'cc-number', 'cc-name', 'cc-given-name', 'cc-additional-name',
            'cc-family-name', 'cc-exp', 'cc-exp-month', 'cc-exp-year',
            'cc-csc', 'cc-type'
        ]);

        var addressAutocompletes = new Set([
            'street-address', 'address-line1', 'address-line2', 'address-line3',
            'address-level1', 'address-level2', 'postal-code', 'country',
            'country-name', 'tel', 'tel-national', 'email',
            'name', 'given-name', 'additional-name', 'family-name',
            'honorific-prefix', 'honorific-suffix', 'organization'
        ]);

        var passwordNameHints = /pass(word)?|pwd|passwd/i;
        var usernameNameHints = /user(name)?|login|email|acct|account/i;
        var paymentNameHints = /card.?num|cc.?num|ccn|cvc|cvv|csc|exp(ir|y)|card.?name|card.?holder/i;
        var addressNameHints = /street|address|addr|city|state|province|zip|postal|country|phone|tel|mobile/i;

        function classifyField(el) {
            var ac = (el.getAttribute('autocomplete') || '').trim().toLowerCase();
            // Strip section/shipping/billing tokens
            var tokens = ac.split(/\\s+/);
            var effectiveAc = tokens[tokens.length - 1] || '';

            if (passwordAutocompletes.has(effectiveAc)) return 'password';
            if (paymentAutocompletes.has(effectiveAc)) return 'payment';
            if (addressAutocompletes.has(effectiveAc)) return 'address';

            var type = (el.type || '').toLowerCase();
            if (type === 'password') return 'password';

            var nameId = ((el.name || '') + ' ' + (el.id || '') + ' ' + (el.placeholder || '')).toLowerCase();

            if (passwordNameHints.test(nameId)) return 'password';
            if (type === 'password' || type === 'text' || type === 'email' || type === 'tel' || type === '') {
                if (usernameNameHints.test(nameId)) {
                    // Only classify as password-related if near a password field
                    var form = el.form;
                    if (form) {
                        var inputs = form.querySelectorAll('input[type="password"]');
                        if (inputs.length > 0) return 'password';
                    }
                }
            }
            if (paymentNameHints.test(nameId)) return 'payment';
            if (addressNameHints.test(nameId)) return 'address';

            return null;
        }

        // ---------- Field tracking ----------

        function ensureFieldId(el) {
            var fid = el.getAttribute(ATTR_ID);
            if (!fid) {
                fid = 'maho_af_' + (nextId++);
                el.setAttribute(ATTR_ID, fid);
            }
            return fid;
        }

        function getFieldRect(el) {
            var r = el.getBoundingClientRect();
            return { x: r.x, y: r.y, width: r.width, height: r.height };
        }

        // ---------- Autocomplete dropdown detection ----------

        function hasSiteAutocomplete(el) {
            // Check for common autocomplete list patterns near the field
            if (el.getAttribute('aria-expanded') === 'true') return true;
            if (el.getAttribute('aria-owns') || el.getAttribute('aria-controls')) {
                var listId = el.getAttribute('aria-owns') || el.getAttribute('aria-controls');
                var list = document.getElementById(listId);
                if (list && list.offsetHeight > 0) return true;
            }
            if (el.list && el.list.options && el.list.options.length > 0) return true;
            return false;
        }

        // ---------- Event handlers ----------

        function onFocus(e) {
            var el = e.target;
            if (!el || !el.tagName || el.tagName.toLowerCase() !== 'input') return;

            var fieldType = classifyField(el);
            if (!fieldType) return;

            if (hasSiteAutocomplete(el)) return;

            var fieldId = ensureFieldId(el);
            trackedFields[fieldId] = el;

            try {
                window.webkit.messageHandlers.autofill.postMessage({
                    type: 'fieldFocused',
                    fieldType: fieldType,
                    fieldId: fieldId,
                    rect: getFieldRect(el)
                });
            } catch (err) {}
        }

        function onBlur(e) {
            var el = e.target;
            if (!el || !el.tagName || el.tagName.toLowerCase() !== 'input') return;

            var fieldId = el.getAttribute(ATTR_ID);
            if (!fieldId) return;

            try {
                window.webkit.messageHandlers.autofill.postMessage({
                    type: 'fieldBlurred',
                    fieldId: fieldId
                });
            } catch (err) {}
        }

        document.addEventListener('focusin', onFocus, true);
        document.addEventListener('focusout', onBlur, true);

        // ---------- Scan existing fields ----------

        function scanFields(root) {
            var inputs = root.querySelectorAll('input');
            for (var i = 0; i < inputs.length; i++) {
                var fieldType = classifyField(inputs[i]);
                if (fieldType) {
                    ensureFieldId(inputs[i]);
                    trackedFields[inputs[i].getAttribute(ATTR_ID)] = inputs[i];
                }
            }
        }

        scanFields(document);

        // ---------- MutationObserver for SPA ----------

        var observer = new MutationObserver(function(mutations) {
            for (var i = 0; i < mutations.length; i++) {
                var added = mutations[i].addedNodes;
                for (var j = 0; j < added.length; j++) {
                    var node = added[j];
                    if (node.nodeType === 1) {
                        if (node.tagName && node.tagName.toLowerCase() === 'input') {
                            var ft = classifyField(node);
                            if (ft) {
                                ensureFieldId(node);
                                trackedFields[node.getAttribute(ATTR_ID)] = node;
                            }
                        }
                        if (node.querySelectorAll) {
                            scanFields(node);
                        }
                    }
                }
            }
        });

        observer.observe(document.documentElement, {
            childList: true,
            subtree: true
        });

        // ---------- Fill functions (exposed globally) ----------

        window.__maho_autofill_fill = function(fieldId, value) {
            var el = trackedFields[fieldId] || document.querySelector('[' + ATTR_ID + '=\"' + fieldId + '\"]');
            if (!el) return false;

            var nativeSetter = Object.getOwnPropertyDescriptor(
                Object.getPrototypeOf(el), 'value'
            );
            if (nativeSetter && nativeSetter.set) {
                nativeSetter.set.call(el, value);
            } else {
                el.value = value;
            }

            el.dispatchEvent(new Event('input', { bubbles: true }));
            el.dispatchEvent(new Event('change', { bubbles: true }));
            return true;
        };

        window.__maho_autofill_fillForm = function(formData) {
            var keys = Object.keys(formData);
            for (var i = 0; i < keys.length; i++) {
                window.__maho_autofill_fill(keys[i], formData[keys[i]]);
            }
        };

        window.__maho_autofill_fillFormByType = function(semanticData) {
            var fields = document.querySelectorAll('input');
            var typeMap = {};

            for (var i = 0; i < fields.length; i++) {
                var el = fields[i];
                var fieldId = el.getAttribute(ATTR_ID);
                if (!fieldId) continue;

                var type = (el.type || '').toLowerCase();
                var ac = (el.getAttribute('autocomplete') || '').trim().toLowerCase();
                var tokens = ac.split(/\\s+/);
                var effectiveAc = tokens[tokens.length - 1] || '';
                var descriptor = ((el.name || '') + ' ' + (el.id || '') + ' ' + (el.placeholder || '')).toLowerCase();

                if (type === 'password' || effectiveAc === 'current-password' || effectiveAc === 'new-password') {
                    if (!typeMap['password']) typeMap['password'] = fieldId;
                }
                if (effectiveAc === 'username' || effectiveAc === 'email') {
                    if (!typeMap['username']) typeMap['username'] = fieldId;
                } else if (/user(name)?|login|email|acct|account/.test(descriptor) && type !== 'password') {
                    if (!typeMap['username']) typeMap['username'] = fieldId;
                }
            }

            var keys = Object.keys(semanticData);
            for (var i = 0; i < keys.length; i++) {
                var resolvedId = typeMap[keys[i]];
                if (resolvedId) {
                    window.__maho_autofill_fill(resolvedId, semanticData[keys[i]]);
                }
            }
        };
    })();
    """

    private override init() {
        super.init()
    }

    func start() {
        CoreUpdateDispatcher.shared.register(self)
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleCredentialReceived(_:)),
            name: Notification.Name("AutofillCredentialReceived"),
            object: nil
        )
    }

    @objc private func handleCredentialReceived(_ notification: Notification) {
        guard let userInfo = notification.userInfo,
              let webView = userInfo["webView"] as? WKWebView,
              let formData = userInfo["formData"] as? [String: String] else { return }
        fillFormByType(webView: webView, semanticData: formData)
    }

    // MARK: - WKScriptMessageHandler

    func userContentController(
        _ userContentController: WKUserContentController,
        didReceive message: WKScriptMessage
    ) {
        guard message.name == "autofill" else { return }
        guard let body = message.body as? [String: Any] else { return }
        guard let type = body["type"] as? String else { return }

        switch type {
        case "fieldFocused":
            handleFieldFocused(body: body, webView: message.webView)
        case "fieldBlurred":
            NotificationCenter.default.post(
                name: AutofillHandler.fieldBlurred,
                object: nil,
                userInfo: body
            )
        default:
            break
        }
    }

    // MARK: - Field Focus Handling

    private func handleFieldFocused(body: [String: Any], webView: WKWebView?) {
        guard let fieldType = body["fieldType"] as? String else { return }
        guard let settings = MahoBridge.shared.getSettings() else { return }

        let domain = webView?.url?.host ?? ""

        switch fieldType {
        case "password":
            switch settings.autofill.passwordProvider {
            case .builtin:
                var userInfo = body
                userInfo["domain"] = domain
                userInfo["webView"] = webView as Any
                NotificationCenter.default.post(
                    name: AutofillHandler.fieldDetected,
                    object: nil,
                    userInfo: userInfo
                )
            case .system:
                var userInfo = body
                userInfo["domain"] = domain
                userInfo["webView"] = webView as Any
                NotificationCenter.default.post(
                    name: AutofillHandler.systemProviderRequested,
                    object: nil,
                    userInfo: userInfo
                )
            case .disabled:
                break
            }

        case "payment":
            guard settings.autofill.paymentsEnabled else { return }
            let payments = MahoBridge.shared.getAutofillPayments()
            var userInfo = body
            userInfo["payments"] = payments
            userInfo["webView"] = webView as Any
            NotificationCenter.default.post(
                name: AutofillHandler.fieldDetected,
                object: nil,
                userInfo: userInfo
            )

        case "address":
            guard settings.autofill.addressesEnabled else { return }
            let addresses = MahoBridge.shared.getAutofillAddresses()
            var userInfo = body
            userInfo["addresses"] = addresses
            userInfo["webView"] = webView as Any
            NotificationCenter.default.post(
                name: AutofillHandler.fieldDetected,
                object: nil,
                userInfo: userInfo
            )

        default:
            break
        }
    }

    // MARK: - CoreUpdateObserver

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .passwordsSearchResult:
            break
        case .autofillAddressAdded:
            break
        case .autofillPaymentAdded:
            break
        default:
            break
        }
    }

    // MARK: - Fill API

    private func jsonEscape(_ s: String) -> String {
        guard let data = try? JSONSerialization.data(withJSONObject: [s]),
              let json = String(data: data, encoding: .utf8),
              json.count > 2 else { return "\"\"" }
        // json is ["value"] — extract the inner quoted string including quotes
        let start = json.index(json.startIndex, offsetBy: 1)
        let end = json.index(json.endIndex, offsetBy: -1)
        return String(json[start..<end])
    }

    func fillField(webView: WKWebView, fieldId: String, value: String) {
        let js = "window.__maho_autofill_fill(\(jsonEscape(fieldId)), \(jsonEscape(value)));"
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    func fillForm(webView: WKWebView, formData: [String: String]) {
        guard let jsonData = try? JSONSerialization.data(withJSONObject: formData),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return }
        let js = "window.__maho_autofill_fillForm(\(jsonString));"
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    func fillFormByType(webView: WKWebView, semanticData: [String: String]) {
        guard let jsonData = try? JSONSerialization.data(withJSONObject: semanticData),
              let jsonString = String(data: jsonData, encoding: .utf8) else { return }
        let js = "window.__maho_autofill_fillFormByType(\(jsonString));"
        webView.evaluateJavaScript(js, completionHandler: nil)
    }
}
