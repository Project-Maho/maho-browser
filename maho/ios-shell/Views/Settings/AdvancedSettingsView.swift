import SwiftUI

struct AdvancedSettingsView: View {
    @State private var developerMode: Bool = false
    @State private var hardwareAcceleration: Bool = true
    @State private var experimentalFeatures: Bool = false

    private let bridge = MahoBridge.shared

    var body: some View {
        Form {
            Section("Developer") {
                Toggle("Developer Mode", isOn: $developerMode)
                    .onChange(of: developerMode) { _, newValue in
                        bridge.updateAdvancedSettings(
                            AdvancedSettingsUpdate(developerMode: newValue)
                        )
                    }

                if developerMode {
                    Text("Web Inspector and console logging are enabled.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section("Performance") {
                Toggle("Hardware Acceleration", isOn: $hardwareAcceleration)
                    .onChange(of: hardwareAcceleration) { _, newValue in
                        bridge.updateAdvancedSettings(
                            AdvancedSettingsUpdate(hardwareAcceleration: newValue)
                        )
                    }
            }

            Section("Experimental") {
                Toggle("Experimental Features", isOn: $experimentalFeatures)
                    .onChange(of: experimentalFeatures) { _, newValue in
                        bridge.updateAdvancedSettings(
                            AdvancedSettingsUpdate(experimentalFeatures: newValue)
                        )
                    }

                if experimentalFeatures {
                    Text("Experimental features may be unstable. Use at your own risk.")
                        .font(.caption)
                        .foregroundStyle(ShellTheme.Palette.warning)
                }
            }
        }
        .navigationTitle("Advanced")
        .onAppear(perform: loadSettings)
    }

    private func loadSettings() {
        let values = CoreSettingsSnapshot.values(from: bridge)
        guard !values.isEmpty else { return }
        developerMode = (values["developer_mode"] as? Bool) ?? false
        hardwareAcceleration = (values["hardware_acceleration"] as? Bool) ?? true
        experimentalFeatures = (values["experimental_features"] as? Bool) ?? false
    }
}
