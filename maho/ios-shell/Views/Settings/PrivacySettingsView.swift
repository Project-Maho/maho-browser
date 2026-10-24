import SwiftUI

struct PrivacySettingsView: View {
    @State private var doNotTrack: Bool = false
    @State private var blockThirdPartyCookies: Bool = true
    @State private var contentBlockerEnabled: Bool = false
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

                if contentBlockerEnabled {
                    NavigationLink("Whitelisted Domains") {
                        ContentBlockerWhitelistView()
                    }
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
                        bridge.clearHistory()
                    }
                    Button("Cancel", role: .cancel) {}
                }
            }

            Section("AI Settings") {
                NavigationLink("AI Privacy Dashboard") {
                    PrivacyDashboardView()
                }
            }
        }
        .navigationTitle("Privacy & Security")
        .onAppear(perform: loadSettings)
    }

    private func loadSettings() {
        let values = CoreSettingsSnapshot.values(from: bridge)
        guard !values.isEmpty else { return }
        doNotTrack = (values["do_not_track"] as? Bool) ?? false
        blockThirdPartyCookies = (values["block_third_party_cookies"] as? Bool) ?? false
        contentBlockerEnabled = (values["content_blocker"] as? Bool) ?? false
        popupBlockerEnabled = (values["popup_blocker"] as? Bool) ?? false
        searchSuggestionsEnabled = (values["search_suggestions"] as? Bool) ?? false
        secureDnsEnabled = (values["secure_dns"] as? Bool) ?? false
        secureDnsProvider = (values["secure_dns_provider"] as? String) ?? ""
        clearDataOnExit = (values["clear_data_on_exit"] as? Bool) ?? false
        safeBrowsingEnabled = (values["safe_browsing"] as? Bool) ?? true
    }
}

struct ContentBlockerWhitelistView: View {
    @State private var domains: [String] = []
    @State private var newDomain: String = ""
    private let bridge = MahoBridge.shared
    
    var body: some View {
        List {
            Section("Add Domain") {
                HStack {
                    TextField("example.com", text: $newDomain)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Button("Add") {
                        let trimmed = newDomain.trimmingCharacters(in: .whitespacesAndNewlines)
                        if !trimmed.isEmpty {
                            bridge.addDomainToWhitelist(trimmed)
                            loadDomains()
                            newDomain = ""
                        }
                    }
                }
            }
            
            Section("Whitelisted Domains") {
                if domains.isEmpty {
                    Text("No whitelisted domains.")
                        .foregroundColor(.secondary)
                } else {
                    ForEach(domains, id: \.self) { domain in
                        Text(domain)
                    }
                    .onDelete(perform: deleteDomains)
                }
            }
        }
        .navigationTitle("Whitelist")
        .onAppear(perform: loadDomains)
    }
    
    private func loadDomains() {
        domains = bridge.getWhitelistedDomains()
    }
    
    private func deleteDomains(at offsets: IndexSet) {
        for idx in offsets {
            let domain = domains[idx]
            bridge.removeDomainFromWhitelist(domain)
        }
        loadDomains()
    }
}

