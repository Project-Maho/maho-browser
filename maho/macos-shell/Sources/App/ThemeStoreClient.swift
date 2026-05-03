import Foundation

final class ThemeStoreClient {
    static let shared = ThemeStoreClient()

    /// Default store URL — can be overridden via UserDefaults
    private var storeUrl: URL {
        if let custom = UserDefaults.standard.string(forKey: "maho_theme_store_url"),
           let url = URL(string: custom) {
            return url
        }
        // Default: bundled or remote manifest
        return URL(string: "https://themes.maho.app/manifest.json")!
    }

    private var cachedManifest: ThemeStoreManifest?
    private var lastFetchDate: Date?

    /// Fetch themes from store (with 5-minute cache)
    func fetchThemes(completion: @escaping (Result<[ThemeStoreEntry], Error>) -> Void) {
        // Return cache if fresh
        if let cached = cachedManifest,
           let lastFetch = lastFetchDate,
           Date().timeIntervalSince(lastFetch) < 300 {
            completion(.success(cached.themes))
            return
        }

        let request = URLRequest(url: storeUrl, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 15)
        URLSession.shared.dataTask(with: request) { [weak self] data, response, error in
            if let error = error {
                // If we have cache, return it even if stale
                if let cached = self?.cachedManifest {
                    completion(.success(cached.themes))
                } else {
                    completion(.failure(error))
                }
                return
            }

            guard let data = data else {
                completion(.failure(NSError(domain: "ThemeStore", code: -1, userInfo: [NSLocalizedDescriptionKey: "No data received"])))
                return
            }

            do {
                let decoder = JSONDecoder()
                decoder.keyDecodingStrategy = .convertFromSnakeCase
                let manifest = try decoder.decode(ThemeStoreManifest.self, from: data)
                self?.cachedManifest = manifest
                self?.lastFetchDate = Date()
                completion(.success(manifest.themes))
            } catch {
                completion(.failure(error))
            }
        }.resume()
    }

    /// Install a theme from the store — applies it to the active space
    func installTheme(_ entry: ThemeStoreEntry) {
        let spaces = MahoBridge.shared.getSpaceViewModels()
        guard let activeSpace = spaces.first(where: { $0.isActive }) else { return }

        MahoBridge.shared.updateSpaceConfig(
            SpaceConfigUpdate(
                spaceId: activeSpace.id,
                name: nil,
                color: nil,
                theme: entry.theme,
                icon: nil,
                profileId: nil
            )
        )
    }
}
