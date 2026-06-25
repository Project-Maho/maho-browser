import SwiftUI

struct SettingsView: View {
    @ObservedObject var state: BrowserState
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                Section("General") {
                    Toggle("Search Suggestions", isOn: $state.settings.searchSuggestions)
                        .onChange(of: state.settings.searchSuggestions) { _, newValue in
                            state.updateSetting(key: "general.search_suggestions", value: AnyCodable(newValue))
                        }
                    Toggle("Popup Blocker", isOn: $state.settings.popupBlocker)
                        .onChange(of: state.settings.popupBlocker) { _, newValue in
                            state.updateSetting(key: "general.popup_blocker", value: AnyCodable(newValue))
                        }
                }

                Section("Privacy") {
                    Toggle("Content Blocker", isOn: $state.settings.contentBlocker)
                        .accessibilityIdentifier("settingToggle_privacy_content_blocker")
                        .onChange(of: state.settings.contentBlocker) { _, newValue in
                            state.updateSetting(key: "privacy.content_blocker", value: AnyCodable(newValue))
                        }
                }
            }
            .navigationTitle("Settings")
            .toolbar {
                SwiftUI.ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                        .accessibilityIdentifier("settingsDoneButton")
                }
            }
        }
        .accessibilityIdentifier("settingsView")
    }
}
