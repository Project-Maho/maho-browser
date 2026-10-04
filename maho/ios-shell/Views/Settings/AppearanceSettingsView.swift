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
        guard let vm = bridge.getSettings() else { return }
        for section in vm.sections {
            for item in section.items {
                switch item.key {
                case "appearance.density":
                    if let raw = item.value.value as? String, let d = Density(rawValue: raw) {
                        density = d
                    }
                case "appearance.showTabBar": showTabBar = (item.value.value as? Bool) ?? true
                case "appearance.windowTransparency": windowTransparency = (item.value.value as? Bool) ?? false
                case "appearance.customChromeCss": customCss = (item.value.value as? String) ?? ""
                default: break
                }
            }
        }
    }
}
