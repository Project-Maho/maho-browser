import Foundation

// MARK: - Extension & Search Engine Operations

extension MahoBridge {

    // MARK: - Installed Extensions

    func getInstalledExtensions() -> [ExtensionViewModel] {
        guard let json = callCoreString({ maho_core_get_installed_extensions($0) }),
              let data = json.data(using: .utf8) else { return [] }
        return (try? JSONDecoder().decode([ExtensionViewModel].self, from: data)) ?? []
    }

    // MARK: - Search Engines

    func getSearchEngines() -> String? {
        return callCoreString { maho_core_get_search_engines($0) }
    }

    func getSearchURL(query: String) -> String? {
        return withFFIString(query) { cStr in
            callCoreString { maho_core_get_search_url($0, cStr) }
        }
    }

    // MARK: - Site Search

    func getSiteSearchEntries() -> String? {
        return callCoreString { maho_core_get_site_search_entries($0) }
    }

    func setSiteSearchEntries(json: String) {
        withFFIString(json) { cStr in
            callCoreVoid { maho_core_set_site_search_entries($0, cStr) }
        }
    }

    // MARK: - Recent Searches

    func getRecentSearches() -> String? {
        return callCoreString { maho_core_get_recent_searches($0) }
    }

    func saveSearch(query: String) {
        withFFIString(query) { cStr in
            callCoreVoid { maho_core_save_search($0, cStr) }
        }
    }
}
