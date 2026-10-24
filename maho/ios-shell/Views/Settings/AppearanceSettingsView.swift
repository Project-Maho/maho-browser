import SwiftUI

struct AppearanceSettingsView: View {
    @EnvironmentObject private var themeStore: AppThemeStore
    @State private var density: Density = .comfortable
    @State private var showTabBar: Bool = true
    @State private var windowTransparency: Bool = false
    @State private var customCss: String = ""

    private let bridge = MahoBridge.shared

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

            Section("Layout") {
                Picker("Density", selection: $density) {
                    Text("Compact").tag(Density.compact)
                    Text("Comfortable").tag(Density.comfortable)
                }
                .pickerStyle(.segmented)
                .onChange(of: density) { _, newValue in
                    bridge.setDensity(newValue)
                }

                Toggle("Show Tab Bar", isOn: $showTabBar)
                    .onChange(of: showTabBar) { _, newValue in
                        bridge.setShowTabBar(newValue)
                    }

                Toggle("Window Transparency", isOn: $windowTransparency)
                    .onChange(of: windowTransparency) { _, newValue in
                        bridge.updateAppearanceSettings(
                            AppearanceSettingsUpdate(windowTransparency: newValue)
                        )
                    }
            }

            Section("Custom CSS") {
                TextEditor(text: $customCss)
                    .font(.system(.caption, design: .monospaced))
                    .frame(minHeight: 100)
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(Color.secondary.opacity(0.3), lineWidth: 1)
                    )

                Button("Apply CSS") {
                    bridge.updateAppearanceSettings(
                        AppearanceSettingsUpdate(customChromeCss: .set(customCss))
                    )
                }

                Button("Clear CSS", role: .destructive) {
                    customCss = ""
                    bridge.updateAppearanceSettings(
                        AppearanceSettingsUpdate(customChromeCss: .setNull)
                    )
                }
            }
        }
        .navigationTitle("Appearance")
        .onAppear(perform: loadSettings)
    }

    private func loadSettings() {
        let values = CoreSettingsSnapshot.values(from: bridge)
        guard !values.isEmpty else { return }
        // Core emits density as "Compact"/"Comfortable"; window transparency
        // and custom CSS are not emitted, so those controls keep their defaults.
        if let raw = values["density"] as? String, let d = Density(rawValue: raw.lowercased()) {
            density = d
        }
        showTabBar = (values["show_tab_bar"] as? Bool) ?? true
    }
}
