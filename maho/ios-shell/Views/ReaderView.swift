import SwiftUI
import WebKit

struct ReaderView: View {
    let article: ExtractedArticle
    @Binding var isPresented: Bool
    
    @AppStorage("readerFontFamily") private var fontFamily: String = "System"
    @AppStorage("readerFontSize") private var fontSize: Double = 18.0
    @AppStorage("readerTheme") private var readerTheme: ReaderTheme = .light
    
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
                }
                
                SwiftUI.ToolbarItem(placement: .topBarTrailing) {
                    Button(action: { showSettings = true }) {
                        Image(systemName: "textformat.size")
                            .foregroundStyle(textColor)
                    }
                }
            }
            .sheet(isPresented: $showSettings) {
                ReaderSettingsSheet()
            }
        }
    }
    
    private var backgroundColor: Color {
        switch readerTheme {
        case .light: return .white
        case .sepia: return Color(red: 0.98, green: 0.95, blue: 0.87)
        case .dark: return Color(red: 0.12, green: 0.12, blue: 0.12)
        }
    }
    
    private var textColor: Color {
        switch readerTheme {
        case .light: return .black
        case .sepia: return Color(red: 0.35, green: 0.25, blue: 0.15)
        case .dark: return .white
        }
    }
}

struct ReaderWebView: UIViewRepresentable {
    let article: ExtractedArticle
    let fontFamily: String
    let fontSize: Double
    let theme: ReaderTheme
    
    func makeUIView(context: Context) -> WKWebView {
        let webView = WKWebView()
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
                <h1 class="title">\(article.title)</h1>
                \(article.byline.isEmpty ? "" : "<p class=\"byline\">By \(article.byline)</p>")
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
    
    class Coordinator: NSObject, WKNavigationDelegate {}
    
    private func generateCSS() -> String {
        let bgHex: String
        let fgHex: String
        
        switch theme {
        case .light:
            bgHex = "#ffffff"
            fgHex = "#000000"
        case .sepia:
            bgHex = "#faf2df"
            fgHex = "#5a402a"
        case .dark:
            bgHex = "#1e1e1e"
            fgHex = "#dcdcdc"
        }
        
        let fontStack: String
        switch fontFamily {
        case "Georgia":
            fontStack = "Georgia, serif"
        case "Courier New":
            fontStack = "'Courier New', monospace"
        default:
            fontStack = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif"
        }
        
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
            color: \(theme == .dark ? "#888" : "#666");
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
            color: \(theme == .dark ? "#88c0d0" : "#0066cc");
            text-decoration: none;
        }
        """
    }
}
