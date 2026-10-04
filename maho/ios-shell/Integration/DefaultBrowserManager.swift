import Foundation
import UIKit
import os.log

@MainActor
final class DefaultBrowserManager {
    static let shared = DefaultBrowserManager()
    private let log = Logger(subsystem: "dev.maho.browser", category: "DefaultBrowser")
    private let defaults = UserDefaults.standard
    private let onboardingShownKey = "dev.maho.browser.defaultBrowserOnboardingShown"

    private init() {}

    var hasShownOnboarding: Bool {
        get { defaults.bool(forKey: onboardingShownKey) }
        set { defaults.set(newValue, forKey: onboardingShownKey) }
    }

    var canOpenSettings: Bool {
        guard let url = URL(string: UIApplication.openSettingsURLString) else { return false }
        return UIApplication.shared.canOpenURL(url)
    }

    func showOnboardingIfNeeded() {
        guard !hasShownOnboarding else { return }
        hasShownOnboarding = true
        log.info("Triggering default browser onboarding")
    }

    func openSettings() {
        guard let url = URL(string: UIApplication.openSettingsURLString) else { return }
        UIApplication.shared.open(url, options: [:]) { [weak self] success in
            if !success {
                self?.log.warning("Failed to open Settings app")
            }
        }
    }
}
