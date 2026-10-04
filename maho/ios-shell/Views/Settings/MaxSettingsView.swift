import SwiftUI

struct MaxSettingsView: View {
    @State private var enabled: Bool = false
    @State private var pagePreviews: Bool = false
    @State private var tidyTabTitles: Bool = false
    @State private var tidyDownloads: Bool = false
    @State private var tidyTabs: Bool = false
    @State private var aiCommandBar: Bool = false
    @State private var instantLinks: Bool = false

    private let bridge = MahoBridge.shared

    var body: some View {
        Form {
            Section {
                Toggle("Enable Max AI", isOn: $enabled)
                    .onChange(of: enabled) { _, newValue in
                        bridge.updateMaxSettings(MaxSettingsUpdate(enabled: newValue))
                    }

                if !enabled {
                    Text("Enable Max AI to unlock intelligent browsing features.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            if enabled {
                Section("AI Features") {
                    Toggle("Page Previews", isOn: $pagePreviews)
                        .onChange(of: pagePreviews) { _, newValue in
                            bridge.updateMaxSettings(MaxSettingsUpdate(pagePreviews: newValue))
                        }

                    Toggle("Tidy Tab Titles", isOn: $tidyTabTitles)
                        .onChange(of: tidyTabTitles) { _, newValue in
                            bridge.updateMaxSettings(MaxSettingsUpdate(tidyTabTitles: newValue))
                        }

                    Toggle("Tidy Downloads", isOn: $tidyDownloads)
                        .onChange(of: tidyDownloads) { _, newValue in
                            bridge.updateMaxSettings(MaxSettingsUpdate(tidyDownloads: newValue))
                        }

                    Toggle("Tidy Tabs", isOn: $tidyTabs)
                        .onChange(of: tidyTabs) { _, newValue in
                            bridge.updateMaxSettings(MaxSettingsUpdate(tidyTabs: newValue))
                        }
                }

                Section("Smart Features") {
                    Toggle("AI Command Bar", isOn: $aiCommandBar)
                        .onChange(of: aiCommandBar) { _, newValue in
                            bridge.updateMaxSettings(MaxSettingsUpdate(aiCommandBar: newValue))
                        }

                    Toggle("Instant Links", isOn: $instantLinks)
                        .onChange(of: instantLinks) { _, newValue in
                            bridge.updateMaxSettings(MaxSettingsUpdate(instantLinks: newValue))
                        }
                }
            }
        }
        .navigationTitle("Max AI")
        .onAppear(perform: loadSettings)
    }

    private func loadSettings() {
        guard let vm = bridge.getSettings() else { return }
        for section in vm.sections {
            for item in section.items {
                switch item.key {
                case "max.enabled": enabled = (item.value.value as? Bool) ?? false
                case "max.pagePreviews": pagePreviews = (item.value.value as? Bool) ?? false
                case "max.tidyTabTitles": tidyTabTitles = (item.value.value as? Bool) ?? false
                case "max.tidyDownloads": tidyDownloads = (item.value.value as? Bool) ?? false
                case "max.tidyTabs": tidyTabs = (item.value.value as? Bool) ?? false
                case "max.aiCommandBar": aiCommandBar = (item.value.value as? Bool) ?? false
                case "max.instantLinks": instantLinks = (item.value.value as? Bool) ?? false
                default: break
                }
            }
        }
    }
}
