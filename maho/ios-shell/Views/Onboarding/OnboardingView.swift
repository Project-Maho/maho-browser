import SwiftUI
#if canImport(UIKit)
import UIKit
#endif

struct OnboardingView: View {
    let onComplete: () -> Void

    var body: some View {
        VStack(spacing: ShellTheme.Spacing.hero) {
            Spacer()

            VStack(spacing: ShellTheme.Spacing.large) {
                Image(systemName: "globe")
                    .font(.system(size: ShellTheme.Size.onboardingHeroIcon))
                    .foregroundStyle(ShellTheme.Palette.accent)

                Text("Welcome to Maho")
                    .font(.largeTitle.weight(.bold))

                Text("A beautiful, fast browser for iOS.")
                    .font(.title3)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }

            Spacer()

            VStack(spacing: ShellTheme.Spacing.medium) {
                Button {
                    #if canImport(UIKit)
                    if let url = URL(string: UIApplication.openSettingsURLString) {
                        UIApplication.shared.open(url)
                    }
                    #endif
                } label: {
                    Label("Set as Default Browser", systemImage: "safari")
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .padding(ShellTheme.Spacing.large)
                        .background(ShellTheme.Palette.accent)
                        .foregroundStyle(.white)
                        .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.onboardingButton, style: .continuous))
                }

                Button {
                    onComplete()
                } label: {
                    Text("Skip")
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .padding(ShellTheme.Spacing.large)
                        .background(ShellTheme.Palette.secondaryButtonFill)
                        .foregroundStyle(.primary)
                        .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.onboardingButton, style: .continuous))
                }
            }
            .padding(.horizontal, ShellTheme.Spacing.page)
            .padding(.bottom, ShellTheme.Spacing.hero)
        }
        .background(ShellTheme.Palette.homeBackground.ignoresSafeArea())
    }
}
