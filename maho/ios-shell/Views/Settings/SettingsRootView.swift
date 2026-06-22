import SwiftUI

struct SettingsRootView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationView {
            List {
                Section {
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

                Section {
                    NavigationLink(destination: SyncSettingsView()) {
                        Label { Text("Sync") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("arrow.triangle.2.circlepath")) }
                    }
                }

                Section {
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

                Section {
                    NavigationLink(destination: MaxSettingsView()) {
                        Label { Text("Max AI") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("sparkles")) }
                    }
                    NavigationLink(destination: AgentView()) {
                        Label { Text("Web Agent") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("cpu")) }
                    }
                    NavigationLink(destination: AdvancedSettingsView()) {
                        Label { Text("Advanced") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("gearshape.2")) }
                    }
                }

                Section {
                    NavigationLink(destination: AppIconPickerView()) {
                        Label { Text("App Icon") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("app.badge.checkmark")) }
                    }
                    NavigationLink(destination: FeedbackView()) {
                        Label { Text("Feedback") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("envelope")) }
                    }
                }

                Section {
                    Button(role: .destructive) {
                        MahoBridge.shared.resetSettings()
                    } label: {
                        Label { Text("Reset All Settings") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("arrow.counterclockwise")) }
                    }
                }
            }
            .listStyle(.insetGrouped)
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItemGroup(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}
