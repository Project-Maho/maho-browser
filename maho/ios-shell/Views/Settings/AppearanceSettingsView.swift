import SwiftUI

struct AppearanceSettingsView: View {
    @EnvironmentObject private var themeStore: AppThemeStore

    /// Bind picker directly to the store so the segmented control always
    /// reflects the actual live theme (source of truth). Writing through the
    /// binding routes updates through `themeStore.set`, which also persists
    /// to the Rust bridge.
    private var themeBinding: Binding<Theme> {
        Binding(
            get: { themeStore.theme },
            set: { themeStore.set($0) }
        )
    }

    var body: some View {
        Form {
            Section("Theme") {
                Picker("Appearance", selection: themeBinding) {
                    Text("System").tag(Theme.system)
                    Text("Light").tag(Theme.light)
                    Text("Dark").tag(Theme.dark)
                }
                .pickerStyle(.segmented)
            }
            // Density, Show Tab Bar and Window Transparency are desktop layout
            // settings with no iOS consumer, so they are not offered here.
        }
        .navigationTitle("Appearance")
    }
}
