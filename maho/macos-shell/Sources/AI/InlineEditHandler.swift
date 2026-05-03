import AppKit
import WebKit

final class InlineEditHandler: NSObject, WKScriptMessageHandler {
    static let shared = InlineEditHandler()

    static let injectionScript: String = {
        let url = Bundle.main.url(forResource: "InlineEditScript", withExtension: "js")
        if let url = url, let contents = try? String(contentsOf: url) {
            return contents
        }
        return """
        (function() {
            let currentSelection = null;
            document.addEventListener('selectionchange', function() {
                const sel = window.getSelection();
                if (!sel || sel.isCollapsed || sel.rangeCount === 0) {
                    window.webkit.messageHandlers.inlineEdit.postMessage({ type: 'cancel' });
                    currentSelection = null;
                    return;
                }
                const range = sel.getRangeAt(0);
                const container = range.commonAncestorContainer;
                const element = container.nodeType === 1 ? container : container.parentElement;
                const editableParent = element ? element.closest('[contenteditable="true"], input, textarea') : null;
                if (!editableParent) return;
                const text = sel.toString();
                if (text.trim().length < 2) return;
                const rect = range.getBoundingClientRect();
                currentSelection = { range: range.cloneRange(), element: editableParent };
                window.webkit.messageHandlers.inlineEdit.postMessage({
                    type: 'selection',
                    text: text,
                    rect: JSON.stringify({ x: rect.x, y: rect.y, width: rect.width, height: rect.height }),
                    isInput: editableParent.tagName === 'INPUT' || editableParent.tagName === 'TEXTAREA'
                });
            });
            window.__mahoReplaceSelection = function(newText) {
                if (!currentSelection) return false;
                const { range, element } = currentSelection;
                if (element.tagName === 'INPUT' || element.tagName === 'TEXTAREA') {
                    const start = element.selectionStart;
                    const end = element.selectionEnd;
                    element.value = element.value.substring(0, start) + newText + element.value.substring(end);
                    element.selectionStart = start;
                    element.selectionEnd = start + newText.length;
                    element.dispatchEvent(new Event('input', { bubbles: true }));
                } else {
                    const sel = window.getSelection();
                    sel.removeAllRanges();
                    sel.addRange(range);
                    document.execCommand('insertText', false, newText);
                }
                currentSelection = null;
                return true;
            };
        })();
        """
    }()

    private let toolbar = InlineEditToolbar()
    private weak var activeWebView: WKWebView?
    private var selectedText: String = ""

    private override init() {
        super.init()
    }

    func userContentController(
        _ userContentController: WKUserContentController,
        didReceive message: WKScriptMessage
    ) {
        guard message.name == "inlineEdit" else { return }
        guard let body = message.body as? [String: Any] else { return }

        let type = body["type"] as? String ?? ""

        if type == "cancel" {
            toolbar.dismiss()
            return
        }

        guard type == "selection",
              let text = body["text"] as? String,
              let rectJSON = body["rect"] as? String,
              let webView = message.webView else { return }

        guard LLMService.shared.isConfigured else { return }

        activeWebView = webView
        selectedText = text

        let rect = parseRect(from: rectJSON, in: webView)

        toolbar.show(relativeTo: rect, in: webView, text: text) { [weak self] action in
            self?.performAction(action)
        }
    }

    private func performAction(_ action: InlineEditAction) {
        let text = selectedText
        toolbar.showLoading()

        let messages = [
            LLMMessage(role: .user, content: action.prompt + text)
        ]

        Task {
            do {
                let result = try await LLMService.shared.completeChat(messages: messages)
                let trimmed = result.trimmingCharacters(in: .whitespacesAndNewlines)
                await MainActor.run {
                    self.toolbar.showResult(trimmed)
                }
            } catch {
                await MainActor.run {
                    self.toolbar.showError(error.localizedDescription)
                }
            }
        }
    }

    private func parseRect(from json: String, in webView: WKWebView) -> NSRect {
        guard let data = json.data(using: .utf8),
              let dict = try? JSONSerialization.jsonObject(with: data) as? [String: Double],
              let x = dict["x"], let y = dict["y"],
              let width = dict["width"], let height = dict["height"] else {
            return NSRect(x: 0, y: 0, width: 1, height: 1)
        }

        let flippedY = webView.bounds.height - y - height
        return NSRect(x: x, y: flippedY, width: width, height: height)
    }
}
