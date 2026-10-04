import SwiftUI
import UIKit
import WebKit

struct WebViewContainer: UIViewRepresentable {
    @EnvironmentObject private var themeStore: AppThemeStore
    @ObservedObject var viewModel: BrowserViewModel
    let isPrivate: Bool
    var onPinchSummarize: (() -> Void)?
    var onNewTabRequested: ((URL) -> Void)?

    func makeCoordinator() -> WebViewCoordinator {
        let coordinator = WebViewCoordinator(viewModel: viewModel)
        coordinator.onNewTabRequested = onNewTabRequested
        return coordinator
    }

    func makeUIView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        if isPrivate {
            configuration.websiteDataStore = PrivateBrowsingDataStorePool.shared.store
        }
        let preferences = WKWebpagePreferences()
        preferences.allowsContentJavaScript = true
        configuration.defaultWebpagePreferences = preferences
        configuration.allowsInlineMediaPlayback = true
        configuration.allowsPictureInPictureMediaPlayback = true
        configuration.mediaTypesRequiringUserActionForPlayback = [.all]

        configuration.userContentController.add(context.coordinator, name: "mahoReader")
        Self.installReadabilityUserScript(on: configuration.userContentController)
        AppThemeStore.installWebColorSchemeBootstrap(on: configuration.userContentController)

        let webView = WKWebView(frame: .zero, configuration: configuration)
        AppThemeStore.apply(themeStore.theme, to: webView)
        webView.allowsBackForwardNavigationGestures = true
        webView.navigationDelegate = context.coordinator
        webView.uiDelegate = context.coordinator

        let refreshControl = UIRefreshControl()
        refreshControl.addTarget(
            context.coordinator,
            action: #selector(WebViewCoordinator.handleRefresh(_:)),
            for: .valueChanged
        )
        webView.scrollView.refreshControl = refreshControl

        let pinchRecognizer = UIPinchGestureRecognizer(
            target: context.coordinator,
            action: #selector(WebViewCoordinator.handlePinchToSummarize(_:))
        )
        pinchRecognizer.delegate = context.coordinator
        webView.scrollView.addGestureRecognizer(pinchRecognizer)

        context.coordinator.webView = webView
        context.coordinator.onPinchSummarize = onPinchSummarize
        context.coordinator.startObserving()
        AgenticBrowsingPageRegistry.shared.attach(webView)

        let initialUrl = viewModel.currentUrl.trimmingCharacters(in: .whitespacesAndNewlines)
        if !initialUrl.isEmpty && initialUrl != "about:blank", let url = URL(string: initialUrl) {
            webView.load(URLRequest(url: url))
        }

        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {
        context.coordinator.onPinchSummarize = onPinchSummarize
        context.coordinator.onNewTabRequested = onNewTabRequested
        AppThemeStore.apply(themeStore.theme, to: webView)

        let targetUrl = viewModel.currentUrl.trimmingCharacters(in: .whitespacesAndNewlines)
        if !targetUrl.isEmpty && targetUrl != "about:blank" && webView.url == nil,
           let url = URL(string: targetUrl) {
            webView.load(URLRequest(url: url))
        }
    }

    static func dismantleUIView(_ webView: WKWebView, coordinator: WebViewCoordinator) {
        AgenticBrowsingPageRegistry.shared.detach(webView)
        webView.configuration.userContentController.removeScriptMessageHandler(forName: "mahoReader")
        coordinator.stopObserving()
        coordinator.webView = nil
    }

    /// Pre-registers Readability.js at `.atDocumentEnd` so the extractor is
    /// available on every page load without re-injecting the 2812-line script.
    /// The reader-mode toggle then only evaluates a small `runMahoReadability()`
    /// call, which reduces per-toggle payload from ~350KB to ~200 bytes and
    /// avoids parse-time reflow of loaded pages.
    private static func installReadabilityUserScript(on controller: WKUserContentController) {
        guard let url = Bundle.main.url(forResource: "Readability", withExtension: "js"),
              let source = try? String(contentsOf: url, encoding: .utf8) else {
            return
        }
        let wrapped = """
        \(source)
        (function() {
            window.__mahoRunReadability = function() {
                try {
                    var docClone = document.cloneNode(true);
                    var article = new Readability(docClone).parse();
                    if (article) {
                        window.webkit.messageHandlers.mahoReader.postMessage({
                            title: article.title || "",
                            content: article.content || "",
                            textContent: article.textContent || "",
                            byline: article.byline || "",
                            dir: article.dir || "",
                            excerpt: article.excerpt || null,
                            siteName: article.siteName || null,
                            length: article.length || null,
                            publishedTime: article.publishedTime || null
                        });
                    } else {
                        window.webkit.messageHandlers.mahoReader.postMessage({ error: "Failed to parse page content." });
                    }
                } catch (e) {
                    window.webkit.messageHandlers.mahoReader.postMessage({ error: e.toString() });
                }
            };
        })();
        """
        let script = WKUserScript(
            source: wrapped,
            injectionTime: .atDocumentEnd,
            forMainFrameOnly: true
        )
        controller.addUserScript(script)
    }
}

enum AgenticPageEvaluation {
    case success(String)
    case failure(code: String, message: String? = nil)
}

/// Registry for the user-visible page WKWebView. Agent/AI overlay WKWebViews are
/// deliberately not registered so DOM tools cannot accidentally act on the UI.
final class AgenticBrowsingPageRegistry {
    static let shared = AgenticBrowsingPageRegistry()

    private weak var webView: WKWebView?
    private let lock = NSLock()

    private init() {}

    func attach(_ webView: WKWebView) {
        lock.lock()
        self.webView = webView
        lock.unlock()
    }

    func detach(_ webView: WKWebView) {
        lock.lock()
        if self.webView === webView {
            self.webView = nil
        }
        lock.unlock()
    }

    func evaluate(_ script: String, timeout: TimeInterval = 3) -> AgenticPageEvaluation {
        lock.lock()
        let target = webView
        lock.unlock()
        guard let target else {
            return .failure(code: "no_active_page")
        }
        guard !Thread.isMainThread else {
            return .failure(code: "page_script_main_thread")
        }

        let semaphore = DispatchSemaphore(value: 0)
        let resultLock = NSLock()
        var result: AgenticPageEvaluation = .failure(code: "page_script_empty_result")
        DispatchQueue.main.async {
            target.evaluateJavaScript(script) { value, error in
                let next: AgenticPageEvaluation
                if let error {
                    next = .failure(code: "page_script_failed", message: error.localizedDescription)
                } else if let json = value as? String, !json.isEmpty {
                    next = .success(json)
                } else {
                    next = .failure(code: "page_script_empty_result")
                }
                resultLock.lock()
                result = next
                resultLock.unlock()
                semaphore.signal()
            }
        }
        guard semaphore.wait(timeout: .now() + timeout) == .success else {
            return .failure(code: "page_script_timeout")
        }
        resultLock.lock()
        let completed = result
        resultLock.unlock()
        return completed
    }
}

enum AgenticBrowsingDOM {
    static let toolNames: Set<String> = [
        "get_page_elements",
        "get_page_snapshot",
        "click_element",
        "fill_input",
        "scroll_page",
    ]

    static func invoke(
        name: String,
        args: [String: Any],
        evaluate: (String) -> AgenticPageEvaluation = { AgenticBrowsingPageRegistry.shared.evaluate($0) }
    ) -> [String: Any] {
        guard toolNames.contains(name) else {
            return ["ok": false, "error": "unknown_tool"]
        }
        guard let script = script(name: name, args: args) else {
            return ["ok": false, "error": "invalid_arguments"]
        }
        switch evaluate(script) {
        case .failure(let code, let message):
            var failure: [String: Any] = ["ok": false, "error": code]
            if let message, !message.isEmpty {
                failure["message"] = String(message.prefix(200))
            }
            return failure
        case .success(let json):
            guard let data = json.data(using: .utf8),
                  let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
            else {
                return ["ok": false, "error": "invalid_page_script_result"]
            }
            return object
        }
    }

    static var toolDescriptors: [[String: Any]] {
        [
            descriptor(
                name: "get_page_elements",
                description: "List visible interactive page elements with stable numeric refs, labels, and bounding boxes.",
                schema: ["type": "object", "additionalProperties": false, "properties": [:]],
                sensitive: false,
                permission: "auto_approve"
            ),
            descriptor(
                name: "get_page_snapshot",
                description: "Get the current page viewport plus visible interactive elements with numeric refs.",
                schema: ["type": "object", "additionalProperties": false, "properties": [:]],
                sensitive: false,
                permission: "auto_approve"
            ),
            descriptor(
                name: "click_element",
                description: "Click a page element by numeric ref or CSS selector.",
                schema: targetSchema(),
                sensitive: true,
                permission: "always_ask"
            ),
            descriptor(
                name: "fill_input",
                description: "Fill an input, textarea, or contenteditable element and fire input/change events.",
                schema: fillSchema(),
                sensitive: true,
                permission: "always_ask"
            ),
            descriptor(
                name: "scroll_page",
                description: "Smoothly scroll the current page up, down, left, or right.",
                schema: [
                    "type": "object",
                    "additionalProperties": false,
                    "properties": [
                        "direction": ["type": "string", "enum": ["up", "down", "left", "right"]],
                        "amount": ["type": "number", "minimum": 1, "maximum": 10_000],
                    ],
                    "required": ["direction"],
                ],
                sensitive: false,
                permission: "auto_approve"
            ),
        ]
    }

    private static func descriptor(
        name: String,
        description: String,
        schema: [String: Any],
        sensitive: Bool,
        permission: String
    ) -> [String: Any] {
        [
            "capabilityId": name,
            "name": name,
            "description": description,
            "inputSchema": schema,
            "schemaVersion": 1,
            "policy": ["sensitive": sensitive, "permission": permission],
        ]
    }

    private static func targetSchema() -> [String: Any] {
        [
            "type": "object",
            "additionalProperties": false,
            "properties": [
                "id": ["type": "integer", "minimum": 1],
                "selector": ["type": "string", "minLength": 1],
            ],
        ]
    }

    private static func fillSchema() -> [String: Any] {
        var schema = targetSchema()
        var properties = schema["properties"] as? [String: Any] ?? [:]
        properties["text"] = ["type": "string"]
        schema["properties"] = properties
        schema["required"] = ["text"]
        return schema
    }

    private static func script(name: String, args: [String: Any]) -> String? {
        let request: [String: Any] = ["tool": name, "args": args]
        guard JSONSerialization.isValidJSONObject(request),
              let data = try? JSONSerialization.data(withJSONObject: request),
              let requestJSON = String(data: data, encoding: .utf8)
        else {
            return nil
        }
        return """
        (function() {
          const request = \(requestJSON);
          const tool = request.tool;
          const args = request.args || {};
          const refAttr = 'data-maho-agent-ref';
          const ok = (result) => JSON.stringify({ok: true, result: result});
          const fail = (error, message) => JSON.stringify(Object.assign({ok: false, error: error}, message ? {message: String(message).slice(0, 200)} : {}));
          const round = (value) => Math.round(Number(value) * 10) / 10;
          const visible = (el) => {
            if (!(el instanceof Element)) return false;
            const style = getComputedStyle(el);
            const rect = el.getBoundingClientRect();
            return style.display !== 'none' && style.visibility !== 'hidden' && Number(style.opacity) !== 0 && rect.width >= 1 && rect.height >= 1 && rect.bottom > 0 && rect.right > 0 && rect.top < innerHeight && rect.left < innerWidth;
          };
          const safeSelector = (selector) => {
            if (typeof selector !== 'string' || selector.trim() === '') return null;
            try { return document.querySelector(selector); } catch (_) { return '__invalid_selector__'; }
          };
          const resolveTarget = () => {
            if (Number.isInteger(Number(args.id)) && Number(args.id) > 0) {
              return document.querySelector('[' + refAttr + '="' + Number(args.id) + '"]');
            }
            return safeSelector(args.selector);
          };
          const labelFor = (el) => {
            const direct = [el.getAttribute('aria-label'), el.getAttribute('placeholder'), el.getAttribute('alt'), el.getAttribute('title')]
              .find((value) => typeof value === 'string' && value.trim() !== '');
            if (direct) return direct.trim().slice(0, 200);
            if (el.id) {
              try {
                const label = document.querySelector('label[for="' + CSS.escape(el.id) + '"]');
                if (label && label.innerText.trim()) return label.innerText.trim().slice(0, 200);
              } catch (_) {}
            }
            const labelledBy = (el.getAttribute('aria-labelledby') || '').trim();
            if (labelledBy) {
              const text = labelledBy.split(' ').filter(Boolean).map((id) => document.getElementById(id)?.innerText || '').join(' ').trim();
              if (text) return text.slice(0, 200);
            }
            return (el.innerText || el.textContent || '').trim().slice(0, 200);
          };
          const snapshot = () => {
            document.querySelectorAll('[' + refAttr + ']').forEach((el) => el.removeAttribute(refAttr));
            const query = 'a[href],button,input:not([type="hidden"]),textarea,select,[role="button"],[role="link"],[role="checkbox"],[role="radio"],[role="switch"],[role="tab"],[onclick],[contenteditable="true"],[tabindex]:not([tabindex="-1"])';
            const elements = [];
            let nextId = 1;
            for (const el of Array.from(document.querySelectorAll(query))) {
              if (elements.length >= 150 || !visible(el)) continue;
              const id = nextId++;
              el.setAttribute(refAttr, String(id));
              const rect = el.getBoundingClientRect();
              const item = {
                id: id,
                tag: el.tagName.toLowerCase(),
                label: labelFor(el),
                selector: '[' + refAttr + '="' + id + '"]',
                bounds: {x: round(rect.left), y: round(rect.top), width: round(rect.width), height: round(rect.height)}
              };
              const role = (el.getAttribute('role') || '').trim();
              const type = (el.getAttribute('type') || '').trim();
              const href = el instanceof HTMLAnchorElement ? el.href : '';
              const text = (el.innerText || '').trim().slice(0, 200);
              if (role) item.role = role;
              if (type) item.type = type;
              if (href) item.href = href.slice(0, 1000);
              if (text) item.text = text;
              elements.push(item);
            }
            return ok({
              url: location.href,
              title: document.title || '',
              viewport: {width: innerWidth, height: innerHeight, scrollX: round(scrollX), scrollY: round(scrollY)},
              elements: elements
            });
          };
          try {
            if (tool === 'get_page_elements' || tool === 'get_page_snapshot') return snapshot();
            if (tool === 'click_element') {
              if (args.id == null && (typeof args.selector !== 'string' || args.selector.trim() === '')) return fail('missing_target');
              const target = resolveTarget();
              if (target === '__invalid_selector__') return fail('invalid_selector');
              if (!(target instanceof Element)) return fail('element_not_found');
              if (!visible(target)) return fail('element_not_interactable');
              if (target.matches(':disabled,[aria-disabled="true"]')) return fail('element_disabled');
              if (typeof target.focus === 'function') target.focus({preventScroll: true});
              for (const eventName of ['touchstart', 'touchend']) target.dispatchEvent(new Event(eventName, {bubbles: true, cancelable: true}));
              if (typeof PointerEvent === 'function') {
                target.dispatchEvent(new PointerEvent('pointerdown', {bubbles: true, cancelable: true, pointerType: 'touch', isPrimary: true}));
                target.dispatchEvent(new PointerEvent('pointerup', {bubbles: true, cancelable: true, pointerType: 'touch', isPrimary: true}));
              }
              target.dispatchEvent(new MouseEvent('mousedown', {bubbles: true, cancelable: true, view: window}));
              target.dispatchEvent(new MouseEvent('mouseup', {bubbles: true, cancelable: true, view: window}));
              if (typeof target.click === 'function') target.click();
              else target.dispatchEvent(new MouseEvent('click', {bubbles: true, cancelable: true, view: window}));
              return ok({id: Number(target.getAttribute(refAttr)) || null, selector: args.selector || null, clicked: true});
            }
            if (tool === 'fill_input') {
              if (args.id == null && (typeof args.selector !== 'string' || args.selector.trim() === '')) return fail('missing_target');
              if (typeof args.text !== 'string') return fail('missing_text');
              const target = resolveTarget();
              if (target === '__invalid_selector__') return fail('invalid_selector');
              if (!(target instanceof Element)) return fail('element_not_found');
              if (!visible(target)) return fail('element_not_interactable');
              if (target.matches(':disabled,[aria-disabled="true"]')) return fail('element_disabled');
              if ('readOnly' in target && target.readOnly) return fail('element_readonly');
              const editable = target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target.isContentEditable;
              if (!editable) return fail('element_not_editable');
              if (typeof target.focus === 'function') target.focus({preventScroll: true});
              if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement) {
                const proto = target instanceof HTMLTextAreaElement ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
                const setter = Object.getOwnPropertyDescriptor(proto, 'value')?.set;
                if (setter) setter.call(target, args.text); else target.value = args.text;
              } else {
                target.textContent = args.text;
              }
              try {
                target.dispatchEvent(new InputEvent('input', {bubbles: true, inputType: 'insertText', data: args.text}));
              } catch (_) {
                target.dispatchEvent(new Event('input', {bubbles: true}));
              }
              target.dispatchEvent(new Event('change', {bubbles: true}));
              return ok({id: Number(target.getAttribute(refAttr)) || null, selector: args.selector || null, filled: true, length: args.text.length});
            }
            if (tool === 'scroll_page') {
              const direction = typeof args.direction === 'string' ? args.direction.toLowerCase() : '';
              if (!['up', 'down', 'left', 'right'].includes(direction)) return fail('invalid_direction');
              const defaultAmount = (direction === 'up' || direction === 'down') ? innerHeight * 0.8 : innerWidth * 0.8;
              const amount = args.amount == null ? defaultAmount : Number(args.amount);
              if (!Number.isFinite(amount) || amount <= 0 || amount > 10000) return fail('invalid_amount');
              const fromX = scrollX;
              const fromY = scrollY;
              const dx = direction === 'left' ? -amount : direction === 'right' ? amount : 0;
              const dy = direction === 'up' ? -amount : direction === 'down' ? amount : 0;
              const maxX = Math.max(0, document.documentElement.scrollWidth - innerWidth);
              const maxY = Math.max(0, document.documentElement.scrollHeight - innerHeight);
              const targetX = Math.max(0, Math.min(maxX, fromX + dx));
              const targetY = Math.max(0, Math.min(maxY, fromY + dy));
              window.scrollBy({left: dx, top: dy, behavior: 'smooth'});
              return ok({direction: direction, amount: round(amount), from: {x: round(fromX), y: round(fromY)}, target: {x: round(targetX), y: round(targetY)}});
            }
            return fail('unknown_tool');
          } catch (error) {
            return fail('script_error', error && error.message ? error.message : String(error));
          }
        })();
        """
    }
}
