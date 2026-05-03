import Foundation

final class LiveFolderManager {
    static let shared = LiveFolderManager()

    private var providers: [String: LiveFolderProvider] = [:]
    private var folders: [LiveFolderConfig] = []
    private var cachedItems: [String: [LiveFolderItem]] = [:]
    private var timers: [String: Timer] = [:]

    struct LiveFolderConfig: Codable {
        let id: String
        let providerId: String
        let name: String
        var config: [String: String]
        var spaceId: String?
    }

    private let storageKey = "maho_live_folders"

    private init() {
        registerProvider(GitHubPRProvider())
        registerProvider(GoogleCalendarProvider())
        loadFolders()
    }

    func registerProvider(_ provider: LiveFolderProvider) {
        providers[provider.providerId] = provider
    }

    var availableProviders: [LiveFolderProvider] {
        Array(providers.values)
    }

    func getFolders() -> [LiveFolderConfig] { folders }
    func getItems(for folderId: String) -> [LiveFolderItem] { cachedItems[folderId] ?? [] }

    func addFolder(providerId: String, name: String, config: [String: String], spaceId: String?) -> LiveFolderConfig {
        let folderId = UUID().uuidString
        var sanitizedConfig = config
        if let token = sanitizedConfig.removeValue(forKey: "token") {
            storeToken(folderId: folderId, token: token)
        }
        let folder = LiveFolderConfig(id: folderId, providerId: providerId, name: name, config: sanitizedConfig, spaceId: spaceId)
        folders.append(folder)
        saveFolders()
        startRefresh(for: folder)
        return folder
    }

    func removeFolder(id: String) {
        timers[id]?.invalidate()
        timers.removeValue(forKey: id)
        cachedItems.removeValue(forKey: id)
        folders.removeAll { $0.id == id }
        saveFolders()
    }

    func refreshAll() {
        for folder in folders {
            refresh(folder: folder)
        }
    }

    private func startRefresh(for folder: LiveFolderConfig) {
        refresh(folder: folder)
        guard let provider = providers[folder.providerId] else { return }
        let timer = Timer.scheduledTimer(withTimeInterval: provider.refreshInterval, repeats: true) { [weak self] _ in
            self?.refresh(folder: folder)
        }
        timers[folder.id] = timer
    }

    private func refresh(folder: LiveFolderConfig) {
        guard let provider = providers[folder.providerId] else { return }
        var fullConfig = folder.config
        if let token = getToken(folderId: folder.id) {
            fullConfig["token"] = token
        }
        provider.fetchItems(config: fullConfig) { [weak self] items in
            DispatchQueue.main.async {
                self?.cachedItems[folder.id] = items
                NotificationCenter.default.post(name: .liveFolderUpdated, object: nil, userInfo: ["folderId": folder.id])
            }
        }
    }

    private let store = SecureLocalStore.shared

    private func storeToken(folderId: String, token: String) {
        store.save(value: token, for: "livefolder.\(folderId).token")
    }

    private func getToken(folderId: String) -> String? {
        store.get(for: "livefolder.\(folderId).token")
    }

    private func loadFolders() {
        guard let data = UserDefaults.standard.data(forKey: storageKey),
              let decoded = try? JSONDecoder().decode([LiveFolderConfig].self, from: data) else { return }
        folders = decoded
        for folder in folders {
            startRefresh(for: folder)
        }
    }

    private func saveFolders() {
        if let data = try? JSONEncoder().encode(folders) {
            UserDefaults.standard.set(data, forKey: storageKey)
        }
    }
}

extension Notification.Name {
    static let liveFolderUpdated = Notification.Name("liveFolderUpdated")
}
