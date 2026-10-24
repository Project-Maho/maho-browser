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
        let values = CoreSettingsSnapshot.values(from: bridge)
        guard !values.isEmpty else { return }
        enabled = (values["max_enabled"] as? Bool) ?? false
        pagePreviews = (values["max_page_previews"] as? Bool) ?? false
        tidyTabTitles = (values["max_tidy_tab_titles"] as? Bool) ?? false
        tidyDownloads = (values["max_tidy_downloads"] as? Bool) ?? false
        tidyTabs = (values["max_tidy_tabs"] as? Bool) ?? false
        aiCommandBar = (values["max_ai_command_bar"] as? Bool) ?? false
        instantLinks = (values["max_instant_links"] as? Bool) ?? false
    }
}
