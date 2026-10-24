import SwiftUI
import WebKit

/// Shared reader-mode style contract: the storage keys, the font families every
/// reader settings surface offers, and how each one renders.
enum ReaderStyle {
    static let fontFamilyKey = "readerFontFamily"
    static let fontSizeKey = "readerFontSize"
    static let themeKey = "readerTheme"

    static let defaultFontFamily = "System"
    static let defaultFontSize: Double = 18.0
    static let fontSizeRange: ClosedRange<Double> = 12...36

    static let fontFamilies = [
        "System", "Georgia", "Palatino", "Times New Roman",
        "Helvetica", "Arial", "Verdana", "Courier New"
    ]

    static func cssFontStack(for family: String) -> String {
        switch family {
        case "Georgia": return "Georgia, serif"
        case "Palatino": return "Palatino, 'Palatino Linotype', serif"
        case "Times New Roman": return "'Times New Roman', Times, serif"
        case "Helvetica": return "Helvetica, Arial, sans-serif"
        case "Arial": return "Arial, Helvetica, sans-serif"
        case "Verdana": return "Verdana, Geneva, sans-serif"
        case "Courier New": return "'Courier New', monospace"
        default: return "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif"
        }
    }

    static func previewFont(family: String, size: Double) -> Font {
        guard family != defaultFontFamily else { return .system(size: CGFloat(size)) }
        return .custom(family, size: CGFloat(size))
    }
}

struct ReaderView: View {
    let article: ExtractedArticle
    @Binding var isPresented: Bool
    
    @AppStorage(ReaderStyle.fontFamilyKey) private var fontFamily: String = ReaderStyle.defaultFontFamily
    @AppStorage(ReaderStyle.fontSizeKey) private var fontSize: Double = ReaderStyle.defaultFontSize
    @AppStorage(ReaderStyle.themeKey) private var readerTheme: ReaderTheme = .light
    
    @State private var showSettings = false
    
    var body: some View {
        NavigationStack {
            ZStack {
                backgroundColor
                    .ignoresSafeArea()
                
                ReaderWebView(
                    article: article,
                    fontFamily: fontFamily,
                    fontSize: fontSize,
                    theme: readerTheme
                )
                .padding(.horizontal, 8)
            }
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                SwiftUI.ToolbarItem(placement: .topBarLeading) {
                    Button(action: { isPresented = false }) {
                        Image(systemName: "xmark")
                            .foregroundStyle(textColor)
                    }
                    .accessibilityLabel("Close reader")
                }
                
                SwiftUI.ToolbarItem(placement: .topBarTrailing) {
                    Button(action: { showSettings = true }) {
                        Image(systemName: "textformat.size")
                            .foregroundStyle(textColor)
                    }
                    .accessibilityLabel("Reader settings")
                }
            }
            .sheet(isPresented: $showSettings) {
                ReaderSettingsSheet()
            }
        }
    }
    
    private var backgroundColor: Color {
        ShellTheme.ReaderPalette.background(for: readerTheme)
    }

    private var textColor: Color {
        ShellTheme.ReaderPalette.foreground(for: readerTheme)
    }
}

struct ReaderWebView: UIViewRepresentable {
    let article: ExtractedArticle
    let fontFamily: String
    let fontSize: Double
    let theme: ReaderTheme
    
    func makeUIView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        configuration.defaultWebpagePreferences.allowsContentJavaScript = false
        let webView = WKWebView(frame: .zero, configuration: configuration)
        webView.navigationDelegate = context.coordinator
        webView.isOpaque = false
        webView.backgroundColor = .clear
        #if os(iOS)
        webView.scrollView.backgroundColor = .clear
        #endif
        return webView
    }
    
    func updateUIView(_ webView: WKWebView, context: Context) {
        let css = generateCSS()
        let html = """
        <!DOCTYPE html>
        <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <style>
                \(css)
            </style>
        </head>
        <body>
            <div class="container">
                <h1 class="title">\(Self.escapeHTML(article.title))</h1>
                \(article.byline.isEmpty ? "" : "<p class=\"byline\">By \(Self.escapeHTML(article.byline))</p>")
                <div class="content">
                    \(article.content)
                </div>
            </div>
        </body>
        </html>
        """
        webView.loadHTMLString(html, baseURL: nil)
    }
    
    func makeCoordinator() -> Coordinator {
        Coordinator()
    }
    
    static func escapeHTML(_ text: String) -> String {
        var escaped = ""
        escaped.reserveCapacity(text.count)
        for character in text {
            switch character {
            case "&": escaped += "&amp;"
            case "<": escaped += "&lt;"
            case ">": escaped += "&gt;"
            case "\"": escaped += "&quot;"
            case "'": escaped += "&#39;"
            default: escaped.append(character)
            }
        }
        return escaped
    }

    /// The reader view only ever shows its own `loadHTMLString` document;
    /// link taps open in the system handler and nothing else may navigate it.
    class Coordinator: NSObject, WKNavigationDelegate {
        func webView(
            _ webView: WKWebView,
            decidePolicyFor navigationAction: WKNavigationAction,
            decisionHandler: @escaping (WKNavigationActionPolicy) -> Void
        ) {
            let url = navigationAction.request.url
            if navigationAction.navigationType == .other,
               navigationAction.targetFrame?.isMainFrame == true,
               url == nil || url?.absoluteString == "about:blank" {
                decisionHandler(.allow)
                return
            }
            decisionHandler(.cancel)
            if navigationAction.navigationType == .linkActivated,
               let url,
               let scheme = url.scheme?.lowercased(),
               scheme == "http" || scheme == "https" {
                UIApplication.shared.open(url)
            }
        }
    }
    
    private func generateCSS() -> String {
        let scheme = ShellTheme.ReaderPalette.scheme(for: theme)
        let bgHex = scheme.backgroundHex
        let fgHex = scheme.foregroundHex
        let mutedHex = scheme.mutedForegroundHex
        let linkHex = scheme.linkHex

        let fontStack = ReaderStyle.cssFontStack(for: fontFamily)
        
        return """
        body {
            background-color: \(bgHex);
            color: \(fgHex);
            font-family: \(fontStack);
            font-size: \(fontSize)px;
            line-height: 1.6;
            margin: 0;
            padding: 16px;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        .title {
            font-size: 1.5em;
            margin-bottom: 8px;
            line-height: 1.2;
        }
        .byline {
            font-size: 0.9em;
            color: \(mutedHex);
            margin-bottom: 24px;
            font-style: italic;
        }
        img {
            max-width: 100%;
            height: auto;
            border-radius: 8px;
            margin: 16px 0;
        }
        p {
            margin-bottom: 1.2em;
        }
        a {
            color: \(linkHex);
            text-decoration: none;
        }
        """
    }
}
