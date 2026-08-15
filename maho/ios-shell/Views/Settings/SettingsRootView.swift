import SwiftUI

struct SettingsRootView: View {
    @Environment(\.dismiss) private var dismiss

    @State private var showResetConfirmation = false

    var body: some View {
        NavigationStack {
            List {
                Section(header: Text("Content")) {
                    NavigationLink(destination: GeneralSettingsView()) {
                        Label { Text("General") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("gearshape")) }
                    }
                    NavigationLink(destination: AppearanceSettingsView()) {
                        Label { Text("Appearance") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("paintbrush")) }
                    }
                    NavigationLink(destination: PrivacySettingsView()) {
                        Label { Text("Privacy & Security") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("shield")) }
                    }
                }

                Section(header: Text("Account")) {
                    NavigationLink(destination: SyncSettingsView()) {
                        HStack(spacing: 12) {
                            Image(lucide: MahoIcon.imageForSFSymbol("arrow.triangle.2.circlepath"))
                                .foregroundColor(.primary)
                            
                            VStack(alignment: .leading, spacing: 2) {
                                Text("Sync")
                                    .font(.body)
                                    .foregroundColor(.primary)
                                Text(syncSubtitle)
                                    .font(.footnote)
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                }

                Section(header: Text("Browsing")) {
                    NavigationLink(destination: ReaderSettingsView()) {
                        Label { Text("Reader Mode") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("book")) }
                    }
                    NavigationLink(destination: AutofillSettingsView()) {
                        Label { Text("Autofill & Passwords") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("key")) }
                    }
                    NavigationLink(destination: NotificationSettingsView()) {
                        Label { Text("Notifications") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("bell")) }
                    }
                    NavigationLink(destination: LanguageSettingsView()) {
                        Label { Text("Language") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("globe")) }
                    }
                }

                Section(header: Text("AI")) {
                    NavigationLink(destination: AiProviderSettingsView()) {
                        Label { Text("AI Provider") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("cpu")) }
                    }
                    NavigationLink(destination: MaxSettingsView()) {
                        Label { Text("Maho AI") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("sparkles")) }
                    }
                    NavigationLink(destination: AgentView()) {
                        Label { Text("Web Agent") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("bot")) }
                    }
                    NavigationLink(destination: BYOKWebView()) {
                        Label { Text("API Keys") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("key")) }
                    }
                    NavigationLink(destination: AdvancedSettingsView()) {
                        Label { Text("Advanced") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("gearshape.2")) }
                    }
                }

                Section(header: Text("App")) {
                    NavigationLink(destination: AppIconPickerView()) {
                        Label { Text("App Icon") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("app.badge.checkmark")) }
                    }
                    NavigationLink(destination: FeedbackView()) {
                        Label { Text("Feedback") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("envelope")) }
                    }
                }

                Section(header: Text("Danger"), footer: versionFooter) {
                    Button(role: .destructive) {
                        showResetConfirmation = true
                    } label: {
                        HStack(spacing: 12) {
                            Image(lucide: MahoIcon.imageForSFSymbol("arrow.counterclockwise"))
                                .foregroundColor(.red)
                            Text("Reset All Settings")
                                .foregroundColor(.red)
                        }
                    }
                    .confirmationDialog(
                        "Reset All Settings?",
                        isPresented: $showResetConfirmation,
                        titleVisibility: .visible
                    ) {
                        Button("Reset All Settings", role: .destructive) {
                            MahoBridge.shared.resetSettings()
                        }
                        Button("Cancel", role: .cancel) {}
                    } message: {
                        Text("This restores every setting to its default. This can't be undone.")
                    }
                }
            }
            .listStyle(.insetGrouped)
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItemGroup(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }

    private var syncSubtitle: String {
        if let session = RelayAuthStore.shared.loadSession() {
            let email = session.account.email
            if let status = MahoBridge.shared.getSyncStatus() {
                switch status {
                case .syncing:
                    return "Syncing…"
                case .synced:
                    return "Synced — \(email)"
                case .error(let msg):
                    return "Error: \(msg)"
                default:
                    return "Signed in as \(email)"
                }
            }
            return "Signed in as \(email)"
        } else {
            return "Off — tap to sign in"
        }
    }

    private var appVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0.0"
    }

    private var buildNumber: String {
        Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "1"
    }

    private var versionFooter: some View {
        Text("Maho \(appVersion) (\(buildNumber))")
            .font(.caption2)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .center)
            .padding(.top, 12)
    }
}
