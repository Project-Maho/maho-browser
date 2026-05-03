import SwiftUI

struct SettingsRootView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationView {
            List {
                Section {
                    NavigationLink(destination: GeneralSettingsView()) {
                        Label("General", systemImage: "gearshape")
                    }
                    NavigationLink(destination: AppearanceSettingsView()) {
                        Label("Appearance", systemImage: "paintbrush")
                    }
                    NavigationLink(destination: PrivacySettingsView()) {
                        Label("Privacy & Security", systemImage: "shield")
                    }
                }

                Section {
                    NavigationLink(destination: SyncSettingsView()) {
                        Label("Sync", systemImage: "arrow.triangle.2.circlepath")
                    }
                }

                Section {
                    NavigationLink(destination: ReaderSettingsView()) {
                        Label("Reader Mode", systemImage: "book")
                    }
                    NavigationLink(destination: AutofillSettingsView()) {
                        Label("Autofill & Passwords", systemImage: "key")
                    }
                    NavigationLink(destination: NotificationSettingsView()) {
                        Label("Notifications", systemImage: "bell")
                    }
                    NavigationLink(destination: LanguageSettingsView()) {
                        Label("Language", systemImage: "globe")
                    }
                }

                Section {
                    NavigationLink(destination: MaxSettingsView()) {
                        Label("Max AI", systemImage: "sparkles")
                    }
                    NavigationLink(destination: AdvancedSettingsView()) {
                        Label("Advanced", systemImage: "gearshape.2")
                    }
                }

                Section {
                    NavigationLink(destination: AppIconPickerView()) {
                        Label("App Icon", systemImage: "app.badge.checkmark")
                    }
                    NavigationLink(destination: FeedbackView()) {
                        Label("Feedback", systemImage: "envelope")
                    }
                }

                Section {
                    Button(role: .destructive) {
                        MahoBridge.shared.resetSettings()
                    } label: {
                        Label("Reset All Settings", systemImage: "arrow.counterclockwise")
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
