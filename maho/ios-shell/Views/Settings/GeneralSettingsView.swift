import Foundation
import SwiftUI
import WebKit

private struct SearchEngineOption: Identifiable {
    let id: String
    let name: String
    let urlTemplate: String
}

struct GeneralSettingsView: View {

    @State private var restorePolicy: RestorePolicy = .restoreAll
    @State private var autoplayPolicy: AutoplayPolicy = .blockAudio
    @State private var archiveTimeoutHours: Double = 24.0
    @State private var downloadPath: String = ""
    @State private var selectedSearchEngine: String = Self.searchEngineOptions.first?.name ?? "Google"
    @State private var showClearBrowsingDataAlert = false

    private let bridge = MahoBridge.shared
    private let defaults = UserDefaults.standard

    private static let searchEngineDefaultsKey = "general.selectedSearchEngine"
    private static let archiveTimingDefaultsKey = "general.archiveTimingHours"
    private static let searchEngineOptions: [SearchEngineOption] = [
        SearchEngineOption(id: "google", name: "Google", urlTemplate: "https://www.google.com/search?q=%@"),
        SearchEngineOption(id: "bing", name: "Bing", urlTemplate: "https://www.bing.com/search?q=%@"),
        SearchEngineOption(id: "duckduckgo", name: "DuckDuckGo", urlTemplate: "https://duckduckgo.com/?q=%@"),
        SearchEngineOption(id: "ecosia", name: "Ecosia", urlTemplate: "https://www.ecosia.org/search?q=%@"),
        SearchEngineOption(id: "perplexity", name: "Perplexity", urlTemplate: "https://www.perplexity.ai/search?q=%@"),
        SearchEngineOption(id: "kagi", name: "Kagi", urlTemplate: "https://kagi.com/search?q=%@")
    ]

    var body: some View {
        Form {
            Section {

                Picker("Search Engine", selection: $selectedSearchEngine) {
                    ForEach(Self.searchEngineOptions) { option in
                        Text(option.name).tag(option.name)
                    }
                }
                .onChange(of: selectedSearchEngine) { _, newValue in
                    defaults.set(newValue, forKey: Self.searchEngineDefaultsKey)
                    if let searchEngine = searchEngine(named: newValue) {
                        bridge.updateGeneralSettings(
                            GeneralSettingsUpdate(defaultSearchEngine: searchEngine)
                        )
                    }
                }
            } header: {
                Text("Search")
            }

            Section {

                Picker("On Launch", selection: $restorePolicy) {
                    Text("Restore All Tabs").tag(RestorePolicy.restoreAll)
                    Text("Restore Pinned Only").tag(RestorePolicy.restorePinned)
                    Text("Start Fresh").tag(RestorePolicy.startFresh)
                }
                .onChange(of: restorePolicy) { _, newValue in
                    bridge.setRestorePolicy(newValue)
                }
            } header: {
                Text("Startup")
            }

            Section {

                Picker("Archive Timeout", selection: $archiveTimeoutHours) {
                    Text("Off").tag(-1.0)
                    Text("12 hours").tag(12.0)
                    Text("24 hours").tag(24.0)
                    Text("7 days").tag(168.0)
                    Text("30 days").tag(720.0)
                    Text("60 days").tag(1440.0)
                    Text("90 days").tag(2160.0)
                }
                .onChange(of: archiveTimeoutHours) { _, newValue in
                    defaults.set(newValue, forKey: Self.archiveTimingDefaultsKey)
                    bridge.setArchiveTimeout(newValue)
                }
            } header: {
                Text("Tabs")
            }

            Section {

                Picker("Autoplay", selection: $autoplayPolicy) {
                    Text("Allow All").tag(AutoplayPolicy.allow)
                    Text("Block All").tag(AutoplayPolicy.blockAll)
                    Text("Block Audio").tag(AutoplayPolicy.blockAudio)
                }
                .onChange(of: autoplayPolicy) { _, newValue in
                    bridge.setAutoplayPolicy(newValue)
                }
            } header: {
                Text("Media")
            }

            Section {

                Button(role: .destructive) {
                    showClearBrowsingDataAlert = true
                } label: {
                    Text("Clear Browsing Data")
                }
            } header: {
                Text("Privacy")
            }

            Section {

                HStack {
                    Text("Download Path")
                    Spacer()
                    Text(downloadPath.isEmpty ? "Default" : downloadPath)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            } header: {
                Text("Downloads")
            }
        }
        .navigationTitle("General")
        .onAppear(perform: loadSettings)
        .alert("Clear Browsing Data?", isPresented: $showClearBrowsingDataAlert) {
            Button("Cancel", role: .cancel) {}
            Button("Clear", role: .destructive, action: clearBrowsingData)
        } message: {
            Text("This clears website data and local browser preferences on this device.")
        }
    }

    private func searchEngine(named name: String) -> SettingsSearchEngine? {
        guard let option = Self.searchEngineOptions.first(where: { $0.name == name }) else {
            return nil
        }

        return SettingsSearchEngine(
            name: option.name,
            urlTemplate: option.urlTemplate,
            isDefault: true
        )
    }

    private func loadSettings() {
        guard let vm = bridge.getSettings() else { return }

        if let storedSearchEngine = defaults.string(forKey: Self.searchEngineDefaultsKey) {
            selectedSearchEngine = storedSearchEngine
        }

        if defaults.object(forKey: Self.archiveTimingDefaultsKey) != nil {
            archiveTimeoutHours = defaults.double(forKey: Self.archiveTimingDefaultsKey)
        }

        for section in vm.sections {
            for item in section.items {
                switch item.key {
                case "general.restoreOnLaunch":
                    if let raw = item.value.value as? String, let policy = RestorePolicy(rawValue: raw) {
                        restorePolicy = policy
                    }
                case "general.autoplayPolicy":
                    if let raw = item.value.value as? String, let policy = AutoplayPolicy(rawValue: raw) {
                        autoplayPolicy = policy
                    }
                case "general.defaultSearchEngine":
                    if defaults.string(forKey: Self.searchEngineDefaultsKey) == nil,
                       let raw = item.value.value as? String,
                       Self.searchEngineOptions.contains(where: { $0.name == raw }) {
                        selectedSearchEngine = raw
                    }
                case "general.archive_timeout":
                    if defaults.object(forKey: Self.archiveTimingDefaultsKey) == nil,
                       let val = item.value.value as? Double {
                        archiveTimeoutHours = val
                    }
                case "general.downloadPath":
                    if let val = item.value.value as? String { downloadPath = val }
                default: break
                }
            }
        }
    }

    private func clearBrowsingData() {
        bridge.sendEvent(.clearHistory)
        let dataStore = WKWebsiteDataStore.default()
        let dataTypes = WKWebsiteDataStore.allWebsiteDataTypes()

        dataStore.fetchDataRecords(ofTypes: dataTypes) { records in
            dataStore.removeData(ofTypes: dataTypes, for: records) {}
        }

        defaults.removeObject(forKey: Self.searchEngineDefaultsKey)
        defaults.removeObject(forKey: Self.archiveTimingDefaultsKey)
        defaults.synchronize()
        selectedSearchEngine = Self.searchEngineOptions.first?.name ?? "Google"
        archiveTimeoutHours = 24.0
    }
}
