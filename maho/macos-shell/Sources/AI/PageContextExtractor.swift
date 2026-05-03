import Foundation
import WebKit

struct HeadingInfo {
    let level: Int
    let text: String
}

struct LinkInfo {
    let href: String
    let text: String
}

struct PageContext {
    let title: String
    let url: String
    let text: String
    var headings: [HeadingInfo] = []
    var links: [LinkInfo] = []
    var metaDescription: String = ""
    var selectedText: String = ""
}

final class PageContextExtractor {
    private static let maxTextLength = 4000

    static func extractText(from webView: WKWebView, completion: @escaping (PageContext) -> Void) {
        let title = webView.title ?? ""
        let url = webView.url?.absoluteString ?? ""

        webView.evaluateJavaScript("document.body.innerText") { result, _ in
            let text: String
            if let str = result as? String {
                text = String(str.prefix(maxTextLength))
            } else {
                text = ""
            }
            completion(PageContext(title: title, url: url, text: text))
        }
    }

    static func extractRichContext(from webView: WKWebView, completion: @escaping (PageContext) -> Void) {
        let title = webView.title ?? ""
        let url = webView.url?.absoluteString ?? ""

        let js = """
        (function() {
            var headings = [];
            var allHeadings = document.querySelectorAll('h1,h2,h3,h4,h5,h6');
            for (var i = 0; i < Math.min(allHeadings.length, 100); i++) {
                headings.push({ level: parseInt(allHeadings[i].tagName.charAt(1)), text: allHeadings[i].innerText.trim() });
            }
            var links = [];
            var anchors = document.querySelectorAll('a[href]');
            for (var i = 0; i < Math.min(anchors.length, 50); i++) {
                links.push({ href: anchors[i].href, text: (anchors[i].innerText || '').trim() });
            }
            var metaDesc = '';
            var metaEl = document.querySelector('meta[name="description"]');
            if (metaEl) metaDesc = metaEl.content || '';
            if (!metaDesc) {
                var ogEl = document.querySelector('meta[property="og:description"]');
                if (ogEl) metaDesc = ogEl.content || '';
            }
            var selectedText = window.getSelection().toString();
            var bodyText = ((document.body && document.body.innerText) || '').substring(0, 4000);
            return JSON.stringify({ headings: headings, links: links, metaDescription: metaDesc, selectedText: selectedText, bodyText: bodyText });
        })()
        """

        webView.evaluateJavaScript(js) { result, _ in
            guard let jsonString = result as? String,
                  let data = jsonString.data(using: .utf8),
                  let dict = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                completion(PageContext(title: title, url: url, text: ""))
                return
            }

            let bodyText = (dict["bodyText"] as? String) ?? ""
            let metaDescription = (dict["metaDescription"] as? String) ?? ""
            let selectedText = (dict["selectedText"] as? String) ?? ""

            var headings: [HeadingInfo] = []
            if let rawHeadings = dict["headings"] as? [[String: Any]] {
                headings = rawHeadings.compactMap { h in
                    guard let level = h["level"] as? Int, let text = h["text"] as? String else { return nil }
                    return HeadingInfo(level: level, text: text)
                }
            }

            var links: [LinkInfo] = []
            if let rawLinks = dict["links"] as? [[String: Any]] {
                links = rawLinks.compactMap { l in
                    guard let href = l["href"] as? String, let text = l["text"] as? String else { return nil }
                    return LinkInfo(href: href, text: text)
                }
            }

            completion(PageContext(
                title: title,
                url: url,
                text: String(bodyText.prefix(maxTextLength)),
                headings: headings,
                links: links,
                metaDescription: metaDescription,
                selectedText: selectedText
            ))
        }
    }
}
