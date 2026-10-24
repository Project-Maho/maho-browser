import Foundation
import SwiftUI
import WebKit

struct GeneralSettingsView: View {

    @State private var restorePolicy: RestorePolicy = .restoreAll
    @State private var autoplayPolicy: AutoplayPolicy = .blockAudio
    @State private var archiveTimeoutHours: Double = 24.0
    @State private var selectedSearchEngine: String = Self.searchEngineOptions.first?.name ?? "Google"
    @State private var showClearBrowsingDataAlert = false

    private let bridge = MahoBridge.shared
    private let defaults = UserDefaults.standard

    private static let searchEngineDefaultsKey = SearchEngineCatalog.selectedEngineDefaultsKey
    private static let archiveTimingDefaultsKey = "general.archiveTimingHours"
    private static let searchEngineOptions = SearchEngineCatalog.options

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
            } footer: {
                Text("Applies the next time Maho launches. Pinned and favorite tabs are kept unless you choose Start Fresh, which also returns them to their pinned pages.")
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
            } footer: {
                Text("Applies to pages opened after the change.")
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
                    Text("Save To")
                    Spacer()
                    Text(Self.downloadsLocationLabel)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            } header: {
                Text("Downloads")
            } footer: {
                Text("Downloads are saved in the Maho folder of the Files app.")
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

    /// iOS always saves downloads to the app's Documents folder
    /// (WebDownloadCoordinator), which Files shows as "On My iPhone > Maho".
    private static var downloadsLocationLabel: String {
        let device = UIDevice.current.userInterfaceIdiom == .pad ? "iPad" : "iPhone"
        return "On My \(device) › Maho"
    }

    private func loadSettings() {
        // Restore and autoplay policies are not view-model items, so read them
        // from the full settings document.
        if let general = bridge.getSettingsDocument()?["general"] as? [String: Any] {
            if let raw = general["restoreOnLaunch"] as? String, let policy = RestorePolicy(rawValue: raw) {
                restorePolicy = policy
            }
            if let raw = general["autoplayPolicy"] as? String, let policy = AutoplayPolicy(rawValue: raw) {
                autoplayPolicy = policy
            }
        }

        let values = CoreSettingsSnapshot.values(from: bridge)
        guard !values.isEmpty else { return }

        if let storedSearchEngine = defaults.string(forKey: Self.searchEngineDefaultsKey) {
            selectedSearchEngine = storedSearchEngine
        }

        if defaults.object(forKey: Self.archiveTimingDefaultsKey) != nil {
            archiveTimeoutHours = defaults.double(forKey: Self.archiveTimingDefaultsKey)
        }

        // Core does not emit restore-on-launch or autoplay items; those were
        // loaded from the settings document above.
        if defaults.string(forKey: Self.searchEngineDefaultsKey) == nil,
           let raw = values["search_engine"] as? String,
           Self.searchEngineOptions.contains(where: { $0.name == raw }) {
            selectedSearchEngine = raw
        }
        if defaults.object(forKey: Self.archiveTimingDefaultsKey) == nil,
           let label = values["general.archive_timeout"] as? String,
           let hours = Self.archiveTimeoutHours(forCoreLabel: label) {
            archiveTimeoutHours = hours
        }
    }

    private static func archiveTimeoutHours(forCoreLabel label: String) -> Double? {
        switch label {
        case "Off": return -1.0
        case "12 hours": return 12.0
        case "24 hours": return 24.0
        case "7 days": return 168.0
        case "30 days": return 720.0
        case "60 days": return 1440.0
        case "90 days": return 2160.0
        default: return nil
        }
    }

    private func clearBrowsingData() {
        bridge.clearAllBrowsingData()
        defaults.removeObject(forKey: Self.searchEngineDefaultsKey)
        defaults.removeObject(forKey: Self.archiveTimingDefaultsKey)
        defaults.synchronize()
        selectedSearchEngine = Self.searchEngineOptions.first?.name ?? "Google"
        archiveTimeoutHours = 24.0
    }
}
