import SwiftUI

struct MaxSettingsView: View {
    @State private var enabled: Bool = false

    private let bridge = MahoBridge.shared

    var body: some View {
        Form {
            Section {
                Toggle("Enable Maho AI", isOn: $enabled)
                    .onChange(of: enabled) { _, newValue in
                        bridge.updateMaxSettings(MaxSettingsUpdate(enabled: newValue))
                    }

                if !enabled {
                    Text("Enable Maho AI to unlock intelligent browsing features.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            // Tidy Tab Titles / Tidy Downloads / Tidy Tabs are not offered on iOS:
            // core requests them via CoreUpdate::RequestLlmCompletion, and the iOS
            // shell has no handler that fulfils that request, so the toggles
            // would change nothing. Page Previews, AI Command Bar and Instant
            // Links are desktop-only for the same reason.
        }
        .navigationTitle("Maho AI")
        .onAppear(perform: loadSettings)
    }

    private func loadSettings() {
        let values = CoreSettingsSnapshot.values(from: bridge)
        guard !values.isEmpty else { return }
        enabled = (values["max_enabled"] as? Bool) ?? false
    }
}
