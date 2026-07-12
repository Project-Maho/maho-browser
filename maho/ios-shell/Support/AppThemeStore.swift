import SwiftUI
import UIKit
import Combine

/// Drives the app-wide appearance theme via `UIWindow.overrideUserInterfaceStyle`.
///
/// We deliberately do NOT use SwiftUI's `.preferredColorScheme(nil)` to reset
/// to system: on iOS 17 that call fails to clear a previously-set non-nil
/// scheme on already-presented sheets/fullScreenCovers, leaving modals
/// stuck in the wrong theme. Applying the override at the UIKit window
/// level propagates synchronously to every window including modal
/// presentations.
@MainActor
final class AppThemeStore: ObservableObject {
    @Published private(set) var theme: Theme = .system

    private let bridge = MahoBridge.shared

    init() {
        refresh()
        applyToWindows()
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
        guard let vm = bridge.getSettings() else { return }
        for section in vm.sections {
            for item in section.items where item.key == "appearance.theme" {
                if let raw = item.value.value as? String,
                   let parsed = Theme(rawValue: raw),
                   parsed != theme {
                    theme = parsed
                    applyToWindows()
                }
                return
            }
        }
    }

    private func applyToWindows() {
        let style: UIUserInterfaceStyle
        switch theme {
        case .light: style = .light
        case .dark: style = .dark
        case .system: style = .unspecified
        }

        for scene in UIApplication.shared.connectedScenes {
            guard let windowScene = scene as? UIWindowScene else { continue }
            for window in windowScene.windows {
                window.overrideUserInterfaceStyle = style
            }
        }
    }
}
