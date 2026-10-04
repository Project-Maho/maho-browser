import SwiftUI
import WebKit
#if canImport(UIKit)
import UIKit
#endif

@MainActor
final class BrowserViewModel: ObservableObject {
    @Published var currentUrl: String = ""
    @Published var currentTitle: String = ""
    @Published var isLoading: Bool = false
    @Published var loadingProgress: Double = 0
    @Published var canGoBack: Bool = false
    @Published var canGoForward: Bool = false
    @Published var isSecure: Bool = false
    @Published var isUrlBarEditing: Bool = false
    @Published var tabId: TabId = ""
    @Published var extractedArticle: ExtractedArticle? = nil
    @Published var lastErrorMessage: String? = nil
    @Published var failedUrl: String? = nil

    weak var webView: WKWebView?

    private let bridge = MahoBridge.shared

    func navigate(to urlString: String) {
        lastErrorMessage = nil
        failedUrl = nil
        guard !tabId.isEmpty else { return }
        let url = Url(urlString)
        if let requestUrl = URL(string: urlString) {
            webView?.load(URLRequest(url: requestUrl))
        }
        bridge.navigate(tabId: tabId, url: url)
    }

    func goBack() {
        guard !tabId.isEmpty else { return }
        if webView?.canGoBack == true {
            webView?.goBack()
        }
        bridge.goBack(tabId: tabId)
    }

    func goForward() {
        guard !tabId.isEmpty else { return }
        if webView?.canGoForward == true {
            webView?.goForward()
        }
        bridge.goForward(tabId: tabId)
    }

    func reload() {
        let previousFailed = failedUrl
        lastErrorMessage = nil
        failedUrl = nil
        guard !tabId.isEmpty else { return }
        if let previousFailed, let url = URL(string: previousFailed) {
            webView?.load(URLRequest(url: url))
        } else {
            webView?.reload()
        }
        bridge.reload(tabId: tabId)
    }

    func stop() {
        guard !tabId.isEmpty else { return }
        webView?.stopLoading()
        bridge.stop(tabId: tabId)
    }

    func setZoom(_ level: Double) {
        guard !tabId.isEmpty else { return }
        bridge.setZoom(tabId: tabId, level: level)
    }

    func translatePage() {
        guard let webView = webView else { return }

        let locale = Locale.current
        let targetLanguage = locale.language.languageCode?.identifier ?? "en"

        if #available(iOS 17.4, *) {
            let selector = NSSelectorFromString("translateToLanguage:")
            if webView.responds(to: selector) {
                webView.perform(selector, with: targetLanguage)
                return
            }
        }

        let script = """
        (function() {
            var detected = document.documentElement.lang || navigator.language || 'en';
            return detected;
        })();
        """
        webView.evaluateJavaScript(script) { [weak webView] _, _ in
            guard let webView = webView else { return }
            let currentURL = webView.url?.absoluteString ?? ""
            let encoded = currentURL.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? currentURL
            if let url = URL(string: "https://translate.google.com/translate?sl=auto&tl=\(targetLanguage)&u=\(encoded)") {
                webView.load(URLRequest(url: url))
            }
        }
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .navigateTab(let updateTabId, let url):
            guard updateTabId == tabId else { return }
            currentUrl = url.value
        case .navigationStateChanged(let updateTabId, let url, let title, let goBack, let goForward, let loading, let progress):
            guard updateTabId == tabId else { return }
            currentUrl = url.value
            currentTitle = title
            canGoBack = goBack
            canGoForward = goForward
            isLoading = loading
            loadingProgress = progress
            isSecure = url.value.hasPrefix("https://")
        case .zoomChanged(let updateTabId, _):
            guard updateTabId == tabId else { return }
            break
        default:
            break
        }
    }
}

struct BrowserView: View {
    let viewModel: BrowserViewModel

    let tabId: TabId
    let isIncognito: Bool
    @Binding var isBarVisible: Bool
    @Binding var tintColor: Color
    @Binding var chromeVisibilityProgress: CGFloat
    var onPinchSummarize: (() -> Void)?
    var onNewTabRequested: ((URL) -> Void)?

    var body: some View {
        webContent
        .ignoresSafeArea(.keyboard, edges: .bottom)
        .onAppear {
            viewModel.tabId = tabId
            chromeVisibilityProgress = isBarVisible ? 1 : 0
        }
        .onChange(of: tabId) { _, newValue in
            viewModel.tabId = newValue
            isBarVisible = true
            chromeVisibilityProgress = 1
        }
    }

    private var webContent: some View {
        ZStack {
            WebViewContainer(
                viewModel: viewModel,
                isPrivate: isIncognito,
                onPinchSummarize: onPinchSummarize,
                onNewTabRequested: onNewTabRequested
            )
                .id("\(tabId)-\(isIncognito)")
                .background(ShellTheme.Palette.browsingBackground)
                .background(
                    BrowserChromeObserver(
                        viewModel: viewModel,
                        isBarVisible: $isBarVisible,
                        chromeVisibilityProgress: $chromeVisibilityProgress,
                        tintColor: $tintColor
                    )
                )
                .ignoresSafeArea(edges: .horizontal)

            if let errorMsg = viewModel.lastErrorMessage {
                WebErrorOverlay(
                    errorMessage: errorMsg,
                    failedUrl: viewModel.failedUrl ?? viewModel.currentUrl,
                    onRetry: { viewModel.reload() }
                )
                .transition(.opacity)
            }
        }
    }
}

struct WebErrorOverlay: View {
    let errorMessage: String
    let failedUrl: String
    let onRetry: () -> Void

    var body: some View {
        ZStack {
            ShellTheme.Palette.browsingBackground
                .ignoresSafeArea()

            VStack(spacing: 16) {
                Image(systemName: "exclamationmark.triangle.fill")
                    .font(.system(size: 48, weight: .semibold))
                    .foregroundColor(ShellTheme.Palette.accent)

                Text("Unable to Load Page")
                    .font(.system(size: 20, weight: .bold))
                    .foregroundColor(.primary)

                if !failedUrl.isEmpty {
                    Text(failedUrl)
                        .font(.system(size: 13, weight: .regular, design: .monospaced))
                        .foregroundColor(.secondary)
                        .lineLimit(2)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 32)
                }

                Text(errorMessage)
                    .font(.system(size: 14))
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 24)

                Button(action: onRetry) {
                    HStack(spacing: 8) {
                        Image(systemName: "arrow.clockwise")
                        Text("Try Again")
                    }
                    .font(.system(size: 15, weight: .medium))
                    .foregroundColor(.white)
                    .padding(.horizontal, 20)
                    .padding(.vertical, 10)
                    .background(ShellTheme.Palette.accent)
                    .cornerRadius(10)
                }
                .padding(.top, 8)
            }
            .padding(24)
        }
    }
}

private struct BrowserChromeObserver: View {
    let viewModel: BrowserViewModel
    @Binding var isBarVisible: Bool
    @Binding var chromeVisibilityProgress: CGFloat
    @Binding var tintColor: Color

    var body: some View {
#if canImport(UIKit)
        BrowserChromeObserverRepresentable(
            viewModel: viewModel,
            isBarVisible: $isBarVisible,
            chromeVisibilityProgress: $chromeVisibilityProgress,
            tintColor: $tintColor
        )
#else
        Color.clear
#endif
    }
}

#if canImport(UIKit)
private struct BrowserChromeObserverRepresentable: UIViewRepresentable {
    let viewModel: BrowserViewModel
    @Binding var isBarVisible: Bool
    @Binding var chromeVisibilityProgress: CGFloat
    @Binding var tintColor: Color

    func makeCoordinator() -> Coordinator {
        Coordinator(
            viewModel: viewModel,
            isBarVisible: $isBarVisible,
            chromeVisibilityProgress: $chromeVisibilityProgress,
            tintColor: $tintColor
        )
    }

    func makeUIView(context: Context) -> UIView {
        let view = UIView(frame: .zero)
        view.isUserInteractionEnabled = false
        view.backgroundColor = .clear
        return view
    }

    func updateUIView(_ uiView: UIView, context: Context) {
        context.coordinator.isBarVisible = $isBarVisible
        context.coordinator.chromeVisibilityProgress = $chromeVisibilityProgress
        context.coordinator.tintColor = $tintColor
        context.coordinator.viewModel = viewModel
        DispatchQueue.main.async {
            context.coordinator.attachIfNeeded(from: uiView)
        }
    }

    final class Coordinator: NSObject {
        var viewModel: BrowserViewModel
        var isBarVisible: Binding<Bool>
        var chromeVisibilityProgress: Binding<CGFloat>
        var tintColor: Binding<Color>

        private weak var observedWebView: WKWebView?
        private var scrollObservation: NSKeyValueObservation?
        private var loadingObservation: NSKeyValueObservation?
        private var lastOffsetY: CGFloat = 0
        private var lastReportedProgress: CGFloat = 1

        init(
            viewModel: BrowserViewModel,
            isBarVisible: Binding<Bool>,
            chromeVisibilityProgress: Binding<CGFloat>,
            tintColor: Binding<Color>
        ) {
            self.viewModel = viewModel
            self.isBarVisible = isBarVisible
            self.chromeVisibilityProgress = chromeVisibilityProgress
            self.tintColor = tintColor
        }

        @MainActor
        func attachIfNeeded(from view: UIView) {
            guard let webView = findWebView(from: view), observedWebView !== webView else { return }

            observedWebView = webView
            viewModel.webView = webView
            scrollObservation?.invalidate()
            loadingObservation?.invalidate()

            lastOffsetY = webView.scrollView.contentOffset.y
            updateChromeVisibilityProgress(for: webView.scrollView, offsetY: lastOffsetY)

            scrollObservation = webView.scrollView.observe(\.contentOffset, options: [.new]) { [weak self] scrollView, _ in
                DispatchQueue.main.async {
                    self?.handleScroll(offsetY: scrollView.contentOffset.y)
                }
            }

            loadingObservation = webView.observe(\.isLoading, options: [.new]) { [weak self] webView, _ in
                guard webView.isLoading == false else { return }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                    self?.refreshThemeColor(using: webView)
                }
            }

            refreshThemeColor(using: webView)
        }

        private func handleScroll(offsetY: CGFloat) {
            let delta = offsetY - lastOffsetY
            lastOffsetY = offsetY

            if let scrollView = observedWebView?.scrollView {
                updateChromeVisibilityProgress(for: scrollView, offsetY: offsetY)
            }

            if offsetY <= 4 {
                if isBarVisible.wrappedValue == false {
                    withAnimation(.spring(response: 0.28, dampingFraction: 0.88)) {
                        isBarVisible.wrappedValue = true
                    }
                }
                return
            }

            if delta > 10, isBarVisible.wrappedValue {
                withAnimation(.spring(response: 0.28, dampingFraction: 0.88)) {
                    isBarVisible.wrappedValue = false
                }
            } else if delta < -8, isBarVisible.wrappedValue == false {
                withAnimation(.spring(response: 0.28, dampingFraction: 0.88)) {
                    isBarVisible.wrappedValue = true
                }
            }
        }

        private func updateChromeVisibilityProgress(for scrollView: UIScrollView, offsetY: CGFloat) {
            let topInset = scrollView.adjustedContentInset.top
            let normalizedOffset = max(offsetY + topInset, 0)
            let progress = max(0, min(1, 1 - (normalizedOffset / 120)))

            guard abs(progress - lastReportedProgress) > 0.02 else { return }
            lastReportedProgress = progress
            chromeVisibilityProgress.wrappedValue = progress
        }

        private func refreshThemeColor(using webView: WKWebView) {
            let script = "(function(){ const theme = document.querySelector('meta[name=\\\"theme-color\\\"]')?.content; if (theme) { return theme; } const style = window.getComputedStyle(document.body || document.documentElement); return style.backgroundColor || ''; })();"
            webView.evaluateJavaScript(script) { [weak self] result, _ in
                guard let self else { return }
                guard let colorString = result as? String,
                      let color = Self.parseCSSColor(colorString) else {
                    self.tintColor.wrappedValue = ShellTheme.Palette.accent
                    return
                }
                self.tintColor.wrappedValue = color
            }
        }

        private func findWebView(from view: UIView) -> WKWebView? {
            if let webView = view.superview?.subviews.compactMap(Self.firstWebView(in:)).first {
                return webView
            }
            return Self.firstWebView(in: view.window)
        }

        private static func firstWebView(in view: UIView?) -> WKWebView? {
            guard let view else { return nil }
            if let webView = view as? WKWebView {
                return webView
            }
            for child in view.subviews {
                if let match = firstWebView(in: child) {
                    return match
                }
            }
            return nil
        }

        private static func parseCSSColor(_ value: String) -> Color? {
            let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()

            if trimmed.hasPrefix("#") {
                return colorFromHex(trimmed)
            }

            if trimmed.hasPrefix("rgb") {
                let numbers = trimmed
                    .replacingOccurrences(of: "rgba(", with: "")
                    .replacingOccurrences(of: "rgb(", with: "")
                    .replacingOccurrences(of: ")", with: "")
                    .split(separator: ",")
                    .compactMap { Double($0.trimmingCharacters(in: .whitespaces)) }

                guard numbers.count >= 3 else { return nil }
                let alpha = numbers.count > 3 ? numbers[3] : 1.0
                return Color(
                    .sRGB,
                    red: numbers[0] / 255.0,
                    green: numbers[1] / 255.0,
                    blue: numbers[2] / 255.0,
                    opacity: alpha
                )
            }

            return nil
        }

        private static func colorFromHex(_ hex: String) -> Color? {
            let value = hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
            guard let rgbValue = UInt64(value, radix: 16) else { return nil }

            switch value.count {
            case 3:
                let red = Double((rgbValue & 0xF00) >> 8) / 15.0
                let green = Double((rgbValue & 0x0F0) >> 4) / 15.0
                let blue = Double(rgbValue & 0x00F) / 15.0
                return Color(.sRGB, red: red, green: green, blue: blue, opacity: 1.0)
            case 6:
                let red = Double((rgbValue & 0xFF0000) >> 16) / 255.0
                let green = Double((rgbValue & 0x00FF00) >> 8) / 255.0
                let blue = Double(rgbValue & 0x0000FF) / 255.0
                return Color(.sRGB, red: red, green: green, blue: blue, opacity: 1.0)
            default:
                return nil
            }
        }
    }
}
#endif
