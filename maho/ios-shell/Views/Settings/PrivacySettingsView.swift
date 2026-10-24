import SwiftUI

struct PrivacySettingsView: View {
    @State private var contentBlockerEnabled: Bool = false
    @State private var contentBlockerRuleCount: Int = 0
    @State private var filterDownloadFailed = false
    @State private var isRetryingFilterDownload = false
    @State private var popupBlockerEnabled: Bool = true
    @State private var clearDataOnExit: Bool = false
    @State private var safeBrowsingEnabled: Bool = true

    @State private var showClearDataConfirmation = false

    private let bridge = MahoBridge.shared

    var body: some View {
        Form {
            Section {
                Toggle("Safe Browsing", isOn: $safeBrowsingEnabled)
                    .onChange(of: safeBrowsingEnabled) { _, newValue in
                        bridge.updatePrivacySettings(
                            PrivacySettingsUpdate(safeBrowsingEnabled: newValue)
                        )
                    }
            } header: {
                Text("Tracking Protection")
            } footer: {
                Text("Warns before opening suspected fraudulent sites. Applies to pages opened after the change.")
            }

            Section {
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
            } header: {
                Text("Content")
            } footer: {
                if contentBlockerEnabled {
                    if contentBlockerRuleCount > 0 {
                        Text("Blocking ads and trackers with \(contentBlockerRuleCount.formatted()) filter rules. Lists update daily.")
                    } else if filterDownloadFailed && !isRetryingFilterDownload {
                        VStack(alignment: .leading, spacing: 6) {
                            Text("Couldn't download filter lists. Nothing is blocked until a download succeeds.")
                            Button("Try Again") {
                                isRetryingFilterDownload = true
                                FilterListUpdater.shared.refreshIfNeeded(force: true)
                            }
                            .font(.footnote.weight(.semibold))
                        }
                    } else {
                        Text("Downloading filter lists. Nothing is blocked until the download finishes.")
                    }
                }
            }

            // Do Not Track, third-party cookie, Secure DNS and search-suggestion
            // options are not offered: WKWebView exposes no API for them, and
            // settings do not sync, so they could not reach a desktop browser.
            Section {
                Text("Safari's engine always blocks third-party cookies on iPhone. Secure DNS is set in iOS Settings. To change these options for Maho on a computer, open Settings in Maho there.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            } header: {
                Text("Cookies & DNS")
            }

            Section {
                Toggle("Clear Data on Launch", isOn: $clearDataOnExit)
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
                        bridge.clearAllBrowsingData()
                    }
                    Button("Cancel", role: .cancel) {}
                } message: {
                    Text("This clears your history, cookies, cache, and website data on this device.")
                }
            } header: {
                Text("Data")
            } footer: {
                Text("Clears history and website data each time Maho starts after being closed. Switching apps keeps you signed in.")
            }

            Section("AI Settings") {
                NavigationLink("AI Privacy Dashboard") {
                    PrivacyDashboardView()
                }
            }
        }
        .navigationTitle("Privacy & Security")
        .onAppear(perform: loadSettings)
        .onReceive(NotificationCenter.default.publisher(for: .mahoContentBlockerChanged)) { _ in
            contentBlockerRuleCount = bridge.contentBlockerRuleCount()
        }
        .onReceive(NotificationCenter.default.publisher(for: .mahoFilterListRefreshFinished)) { _ in
            contentBlockerRuleCount = bridge.contentBlockerRuleCount()
            filterDownloadFailed = FilterListUpdater.shared.lastDownloadFailed()
            isRetryingFilterDownload = false
        }
    }

    private func loadSettings() {
        let values = CoreSettingsSnapshot.values(from: bridge)
        guard !values.isEmpty else { return }
        contentBlockerEnabled = (values["content_blocker"] as? Bool) ?? false
        contentBlockerRuleCount = bridge.contentBlockerRuleCount()
        filterDownloadFailed = FilterListUpdater.shared.lastDownloadFailed()
        popupBlockerEnabled = (values["popup_blocker"] as? Bool) ?? false
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

