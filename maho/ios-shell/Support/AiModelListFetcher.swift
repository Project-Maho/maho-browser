import Foundation

enum AiModelListFetcher {
    static func fetch(provider: AiProvider, bridge: MahoBridge = .shared) async -> [String] {
        guard let request = request(for: provider, bridge: bridge) else { return [] }
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
                return []
            }
            return parse(data: data, provider: provider)
        } catch {
            return []
        }
    }

    static func fallback(for provider: AiProvider) -> [String] {
        switch provider {
        case .openai:
            return ["gpt-4o", "gpt-4o-mini", "gpt-4-turbo", "gpt-3.5-turbo"]
        case .anthropic:
            return ["claude-sonnet-4-20250514", "claude-opus-4-20250514", "claude-3-5-haiku-20241022"]
        case .managed, .openaiCompatible, .localServer:
            return []
        }
    }

    static func emptyHint(for provider: AiProvider) -> String {
        switch provider {
        case .openai:
            return "Add your OpenAI API key to fetch available models."
        case .anthropic:
            return "Add your Anthropic API key to fetch available models."
        case .openaiCompatible:
            return "Enter a Base URL to fetch available models."
        case .localServer:
            return "Ollama not detected or no models found."
        case .managed:
            return "Sign in to fetch available models."
        }
    }

    private static func request(for provider: AiProvider, bridge: MahoBridge) -> URLRequest? {
        switch provider {
        case .openai:
            guard let key = byokKey(.openai, bridge: bridge) else { return nil }
            return bearerRequest(url: "https://api.openai.com/v1/models", token: key)

        case .anthropic:
            guard let key = byokKey(.anthropic, bridge: bridge),
                  let url = URL(string: "https://api.anthropic.com/v1/models") else { return nil }
            var request = URLRequest(url: url)
            request.setValue(key, forHTTPHeaderField: "x-api-key")
            request.setValue("2025-06-01", forHTTPHeaderField: "anthropic-version")
            return request

        case .openaiCompatible:
            let base = AiProviderResolver.baseURL
            guard !base.isEmpty, let url = modelsURL(base: base) else { return nil }
            var request = URLRequest(url: url)
            if let key = byokKey(.openaiCompatible, bridge: bridge) {
                request.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization")
            }
            return request

        case .localServer:
            guard let url = URL(string: "http://localhost:11434/api/tags") else { return nil }
            return URLRequest(url: url)

        case .managed:
            guard let session = RelayAuthStore.shared.loadSession(),
                  !session.isAccessTokenExpired,
                  let proxy = AiProviderResolver.currentProxyURL(bridge: bridge),
                  let url = modelsURL(base: proxy) else { return nil }
            return bearerRequest(url: url.absoluteString, token: session.accessToken)
        }
    }

    private static func parse(data: Data, provider: AiProvider) -> [String] {
        guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return [] }

        if provider == .localServer {
            let models = json["models"] as? [[String: Any]] ?? []
            return models.compactMap { $0["name"] as? String }
        }

        let entries = json["data"] as? [[String: Any]] ?? []
        return entries.compactMap { $0["id"] as? String }
    }

    private static func modelsURL(base: String) -> URL? {
        var trimmed = base.trimmingCharacters(in: .whitespacesAndNewlines)
        while trimmed.hasSuffix("/") { trimmed.removeLast() }
        let path = trimmed.contains("/v1") ? "\(trimmed)/models" : "\(trimmed)/v1/models"
        return URL(string: path)
    }

    private static func bearerRequest(url: String, token: String) -> URLRequest? {
        guard let parsed = URL(string: url) else { return nil }
        var request = URLRequest(url: parsed)
        request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        return request
    }

    private static func byokKey(_ provider: AiProvider, bridge: MahoBridge) -> String? {
        guard let keychainProvider = provider.byokKeychainProvider else { return nil }
        let key = bridge.byokGetKey(provider: keychainProvider)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return key.isEmpty ? nil : key
    }
}
