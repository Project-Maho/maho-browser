import SwiftUI
import UIKit
import WebKit
import Combine

extension Notification.Name {
    static let mahoThemeChanged = Notification.Name("dev.maho.browser.themeChanged")
}

/// Drives the app-wide appearance theme via `UIWindow.overrideUserInterfaceStyle`.
///
/// We deliberately do NOT use SwiftUI's `.preferredColorScheme(nil)` to reset
/// to system: on iOS 17 that call fails to clear a previously-set non-nil
/// scheme on already-presented sheets/fullScreenCovers, leaving modals
/// stuck in the wrong theme. Applying the override at the UIKit window
/// level propagates synchronously to every window including modal
/// presentations and hosted WKWebViews.
@MainActor
final class AppThemeStore: ObservableObject {
    @Published private(set) var theme: Theme = .system

    private let bridge = MahoBridge.shared
    private var themeChangeCancellable: AnyCancellable?

    init() {
        refresh()
        applyToWindows()
        themeChangeCancellable = NotificationCenter.default.publisher(for: .mahoThemeChanged)
            .compactMap { $0.object as? Theme }
            .receive(on: DispatchQueue.main)
            .sink { [weak self] newTheme in
                guard let self else { return }
                if self.theme != newTheme {
                    self.theme = newTheme
                }
                self.applyToWindows()
            }
    }

    var colorScheme: ColorScheme? {
        switch theme {
        case .light: return .light
        case .dark: return .dark
        case .system: return nil
        }
    }

    func set(_ newTheme: Theme) {
        guard newTheme != theme else { return }
        theme = newTheme
        bridge.setTheme(newTheme)
        applyToWindows()
    }

    func refresh() {
        let parsed = Self.persistedTheme(from: bridge)
        guard parsed != theme else { return }
        theme = parsed
        applyToWindows()
    }

    static func persistedTheme(from bridge: MahoBridge = .shared) -> Theme {
        guard let vm = bridge.getSettings() else { return .system }
        for section in vm.sections {
            for item in section.items where item.key == "appearance.theme" {
                if let raw = item.value.value as? String,
                   let parsed = Theme(rawValue: raw) {
                    return parsed
                }
            }
        }
        return .system
    }

    /// Installs a document-start hook that makes the selected native appearance
    /// observable to CSS (`prefers-color-scheme`) and to controls on every page.
    /// System mode keeps following OS changes without requiring a reload.
    static let webColorSchemeBootstrapScript = """
    (function() {
      if (window.__mahoColorSchemeInstalled) return;
      window.__mahoColorSchemeInstalled = true;
      var media = window.matchMedia('(prefers-color-scheme: dark)');
      window.__mahoThemeMode = 'system';
      window.__mahoApplyColorScheme = function(mode) {
        window.__mahoThemeMode = mode || 'system';
        var resolved = window.__mahoThemeMode === 'system'
          ? (media.matches ? 'dark' : 'light')
          : window.__mahoThemeMode;
        document.documentElement.style.colorScheme = resolved;
        document.documentElement.setAttribute('data-maho-color-scheme', resolved);
      };
      var onSystemThemeChanged = function() {
        if (window.__mahoThemeMode === 'system') {
          window.__mahoApplyColorScheme('system');
        }
      };
      if (media.addEventListener) media.addEventListener('change', onSystemThemeChanged);
      else if (media.addListener) media.addListener(onSystemThemeChanged);
      window.__mahoApplyColorScheme('system');
    })();
    """

    static func installWebColorSchemeBootstrap(on controller: WKUserContentController) {
        controller.addUserScript(
            WKUserScript(
                source: webColorSchemeBootstrapScript,
                injectionTime: .atDocumentStart,
                forMainFrameOnly: false
            )
        )
    }

    static func apply(_ theme: Theme, to webView: WKWebView) {
        webView.overrideUserInterfaceStyle = interfaceStyle(for: theme)
        let raw = theme.rawValue
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "'", with: "\\'")
        webView.evaluateJavaScript(
            "window.__mahoApplyColorScheme && window.__mahoApplyColorScheme('\(raw)')",
            completionHandler: nil
        )
    }

    private static func interfaceStyle(for theme: Theme) -> UIUserInterfaceStyle {
        switch theme {
        case .light: return .light
        case .dark: return .dark
        case .system: return .unspecified
        }
    }

    private func applyToWindows() {
        let style = Self.interfaceStyle(for: theme)

        for scene in UIApplication.shared.connectedScenes {
            guard let windowScene = scene as? UIWindowScene else { continue }
            for window in windowScene.windows {
                window.overrideUserInterfaceStyle = style
                Self.applyTheme(toWebViewsIn: window, theme: theme)
            }
        }
    }

    private static func applyTheme(toWebViewsIn view: UIView, theme: Theme) {
        if let webView = view as? WKWebView {
            apply(theme, to: webView)
        }
        for subview in view.subviews {
            applyTheme(toWebViewsIn: subview, theme: theme)
        }
    }
}
