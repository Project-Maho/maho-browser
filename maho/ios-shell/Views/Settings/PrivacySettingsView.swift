import SwiftUI

struct PrivacySettingsView: View {
    @State private var doNotTrack: Bool = false
    @State private var blockThirdPartyCookies: Bool = true
    @State private var contentBlockerEnabled: Bool = true
    @State private var popupBlockerEnabled: Bool = true
    @State private var searchSuggestionsEnabled: Bool = false
    @State private var secureDnsEnabled: Bool = false
    @State private var secureDnsProvider: String = ""
    @State private var clearDataOnExit: Bool = false
    @State private var safeBrowsingEnabled: Bool = true

    @State private var showClearDataConfirmation = false

    private let bridge = MahoBridge.shared

    var body: some View {
        Form {
            Section("Tracking Protection") {
                Toggle("Send Do Not Track", isOn: $doNotTrack)
                    .onChange(of: doNotTrack) { _, newValue in
                        bridge.setDoNotTrack(newValue)
                    }

                Toggle("Block Third-Party Cookies", isOn: $blockThirdPartyCookies)
                    .onChange(of: blockThirdPartyCookies) { _, newValue in
                        bridge.setBlockThirdPartyCookies(newValue)
                    }

                Toggle("Safe Browsing", isOn: $safeBrowsingEnabled)
                    .onChange(of: safeBrowsingEnabled) { _, newValue in
                        bridge.updatePrivacySettings(
                            PrivacySettingsUpdate(safeBrowsingEnabled: newValue)
                        )
                    }
            }

            Section("Content") {
                Toggle("Content Blocker", isOn: $contentBlockerEnabled)
                    .onChange(of: contentBlockerEnabled) { _, newValue in
                        bridge.setContentBlocker(newValue)
                    }

                Toggle("Popup Blocker", isOn: $popupBlockerEnabled)
                    .onChange(of: popupBlockerEnabled) { _, newValue in
                        bridge.updatePrivacySettings(
                            PrivacySettingsUpdate(popupBlockerEnabled: newValue)
                        )
                    }

                Toggle("Search Suggestions", isOn: $searchSuggestionsEnabled)
                    .onChange(of: searchSuggestionsEnabled) { _, newValue in
                        bridge.updatePrivacySettings(
                            PrivacySettingsUpdate(searchSuggestionsEnabled: newValue)
                        )
                    }
            }

            Section("DNS") {
                Toggle("Secure DNS", isOn: $secureDnsEnabled)
                    .onChange(of: secureDnsEnabled) { _, newValue in
                        bridge.updatePrivacySettings(
                            PrivacySettingsUpdate(secureDnsEnabled: newValue)
                        )
                    }

                if secureDnsEnabled {
                    TextField("DNS Provider", text: $secureDnsProvider)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .onSubmit {
                            bridge.updatePrivacySettings(
                                PrivacySettingsUpdate(secureDnsProvider: secureDnsProvider)
                            )
                        }
                }
            }

            Section("Data") {
                Toggle("Clear Data on Exit", isOn: $clearDataOnExit)
                    .onChange(of: clearDataOnExit) { _, newValue in
                        bridge.setClearDataOnExit(newValue)
                    }

                Button("Clear Browsing Data Now", role: .destructive) {
                    showClearDataConfirmation = true
                }
                .confirmationDialog(
                    "Clear All Browsing Data?",
                    isPresented: $showClearDataConfirmation,
                    titleVisibility: .visible
                ) {
                    Button("Clear Data", role: .destructive) {
                        bridge.sendEvent(.clearHistory)
                    }
                    Button("Cancel", role: .cancel) {}
                }
            }
        }
        .navigationTitle("Privacy & Security")
        .onAppear(perform: loadSettings)
    }

    private func loadSettings() {
        guard let vm = bridge.getSettings() else { return }
        for section in vm.sections {
            for item in section.items {
                switch item.key {
                case "privacy.doNotTrack": doNotTrack = (item.value.value as? Bool) ?? false
                case "privacy.blockThirdPartyCookies": blockThirdPartyCookies = (item.value.value as? Bool) ?? false
                case "privacy.contentBlockerEnabled": contentBlockerEnabled = (item.value.value as? Bool) ?? false
                case "privacy.popupBlockerEnabled": popupBlockerEnabled = (item.value.value as? Bool) ?? false
                case "privacy.searchSuggestionsEnabled": searchSuggestionsEnabled = (item.value.value as? Bool) ?? false
                case "privacy.secureDnsEnabled": secureDnsEnabled = (item.value.value as? Bool) ?? false
                case "privacy.secureDnsProvider": secureDnsProvider = (item.value.value as? String) ?? ""
                case "privacy.clearDataOnExit": clearDataOnExit = (item.value.value as? Bool) ?? false
                case "privacy.safeBrowsingEnabled": safeBrowsingEnabled = (item.value.value as? Bool) ?? true
                default: break
                }
            }
        }
    }
}
