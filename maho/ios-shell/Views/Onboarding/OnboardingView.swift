import SwiftUI
import LucideIcons

enum OnboardingStep: Int, CaseIterable, Identifiable {
    case welcome = 0
    case byok = 1
    case defaultBrowser = 2

    var id: Int { rawValue }
}

struct OnboardingView: View {
    let onComplete: () -> Void
    
    @State private var currentStep: OnboardingStep = .welcome
    @State private var showBYOKSettings = false

    var body: some View {
        ZStack {
            ShellTheme.Palette.homeBackground.ignoresSafeArea()
            
            VStack {
                TabView(selection: $currentStep) {
                    WelcomePageView(currentStep: $currentStep)
                        .tag(OnboardingStep.welcome)
                    
                    BYOKPageView(currentStep: $currentStep, showBYOKSettings: $showBYOKSettings)
                        .tag(OnboardingStep.byok)
                    
                    DefaultBrowserPageView(onComplete: onComplete)
                        .tag(OnboardingStep.defaultBrowser)
                }
                .tabViewStyle(.page(indexDisplayMode: .always))
                .animation(.easeInOut, value: currentStep)
            }
        }
        .sheet(isPresented: $showBYOKSettings) {
            NavigationView {
                BYOKSettingsView()
                    .navigationBarItems(trailing: Button("Done") {
                        showBYOKSettings = false
                    })
            }
        }
    }
}

// MARK: - Subviews

struct WelcomePageView: View {
    @Binding var currentStep: OnboardingStep
    
    var body: some View {
        VStack(spacing: ShellTheme.Spacing.hero) {
            Spacer()
            
            VStack(spacing: ShellTheme.Spacing.large) {
                Image(lucide: Lucide.globe)
                    .font(.system(size: 100))
                    .foregroundColor(.blue)

                Text("Welcome to Maho")
                    .font(.system(size: 32, weight: .bold, design: .rounded))
                    .foregroundColor(.primary)

                Text("A beautiful, fast browser for iOS.")
                    .font(.title3)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 32)
            }

            Spacer()

            Button {
                currentStep = .byok
            } label: {
                Text("Continue")
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .padding(ShellTheme.Spacing.large)
                    .background(Color.blue)
                    .foregroundColor(.white)
                    .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.onboardingButton, style: .continuous))
            }
            .padding(.horizontal, ShellTheme.Spacing.page)
            .padding(.bottom, ShellTheme.Spacing.hero)
        }
    }
}

struct BYOKPageView: View {
    @Binding var currentStep: OnboardingStep
    @Binding var showBYOKSettings: Bool
    
    var body: some View {
        VStack(spacing: ShellTheme.Spacing.hero) {
            Spacer()
            
            VStack(spacing: ShellTheme.Spacing.large) {
                Image(lucide: Lucide.keyRound)
                    .font(.system(size: 100))
                    .foregroundColor(.indigo)

                Text("Bring Your Own Key")
                    .font(.system(size: 28, weight: .bold, design: .rounded))
                    .foregroundColor(.primary)

                Text("Use your OpenAI, Anthropic, or local model key for AI features. Your key stays safe on-device in the iOS Keychain.")
                    .font(.body)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 32)
                    .lineSpacing(4)
            }

            Spacer()

            VStack(spacing: 12) {
                Button {
                    showBYOKSettings = true
                } label: {
                    Text("Set Up AI")
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .padding(ShellTheme.Spacing.large)
                        .background(Color.indigo)
                        .foregroundColor(.white)
                        .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.onboardingButton, style: .continuous))
                }

                Button {
                    currentStep = .defaultBrowser
                } label: {
                    Text("Skip")
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .padding(ShellTheme.Spacing.large)
                        .background(ShellTheme.Palette.secondaryButtonFill)
                        .foregroundColor(.primary)
                        .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.onboardingButton, style: .continuous))
                }
            }
            .padding(.horizontal, ShellTheme.Spacing.page)
            .padding(.bottom, ShellTheme.Spacing.hero)
        }
    }
}

struct DefaultBrowserPageView: View {
    let onComplete: () -> Void
    
    var body: some View {
        VStack(spacing: ShellTheme.Spacing.hero) {
            Spacer()
            
            VStack(spacing: ShellTheme.Spacing.large) {
                Image(lucide: Lucide.compass)
                    .font(.system(size: 100))
                    .foregroundColor(.blue)

                Text("Set as Default Browser")
                    .font(.system(size: 28, weight: .bold, design: .rounded))
                    .foregroundColor(.primary)

                Text("Make Maho your primary browser to enjoy seamless AI features and fast browsing everywhere.")
                    .font(.body)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 32)
                    .lineSpacing(4)
            }

            Spacer()

            VStack(spacing: 12) {
                Button {
                    #if canImport(UIKit)
                    if let url = URL(string: UIApplication.openSettingsURLString) {
                        UIApplication.shared.open(url)
                    }
                    #endif
                } label: {
                    Label {
                        Text("Set as Default Browser")
                    } icon: {
                        Image(uiImage: MahoIcon.imageForSFSymbol("safari"))
                    }
                    .font(.headline)
                    .frame(maxWidth: .infinity)
                    .padding(ShellTheme.Spacing.large)
                    .background(Color.blue)
                    .foregroundColor(.white)
                    .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.onboardingButton, style: .continuous))
                }

                Button {
                    onComplete()
                } label: {
                    Text("Not Now")
                        .font(.headline)
                        .frame(maxWidth: .infinity)
                        .padding(ShellTheme.Spacing.large)
                        .background(ShellTheme.Palette.secondaryButtonFill)
                        .foregroundColor(.primary)
                        .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.onboardingButton, style: .continuous))
                }
            }
            .padding(.horizontal, ShellTheme.Spacing.page)
            .padding(.bottom, ShellTheme.Spacing.hero)
        }
    }
}
