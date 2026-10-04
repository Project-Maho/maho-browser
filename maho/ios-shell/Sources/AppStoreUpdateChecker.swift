import Foundation

@MainActor
final class AppStoreUpdateChecker: ObservableObject {
    @Published var updateAvailable = false
    @Published var latestVersion: String?
    @Published var updateURL: URL?

    private let bundleId: String
    private let currentVersion: String

    init() {
        self.bundleId = Bundle.main.bundleIdentifier ?? ""
        self.currentVersion = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.0.0"
    }

    func check() async {
        // Auto-update disabled until App Store listing is live.
    }

    private func versionIsNewer(_ remote: String, than local: String) -> Bool {
        let parse: (String) -> [Int] = { version in
            version.split(separator: ".").compactMap { Int($0) }
        }
        let r = parse(remote)
        let l = parse(local)
        let count = max(r.count, l.count)
        for i in 0..<count {
            let rv = i < r.count ? r[i] : 0
            let lv = i < l.count ? l[i] : 0
            if rv != lv { return rv > lv }
        }
        return false
    }
}

private struct AppStoreLookupResponse: Decodable {
    let results: [AppStoreResult]
}

private struct AppStoreResult: Decodable {
    let version: String
    let trackId: Int
}
