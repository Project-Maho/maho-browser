import Foundation

struct AiProviderConfig {
    let providerLabel: String
    let apiKey: String
    let endpoint: String
    let model: String
}

enum AiProvider: String, CaseIterable, Identifiable {
    case managed = "maho-managed"
    case openai
    case anthropic
    case openaiCompatible = "openai-compatible"
    case localServer = "local-server"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .managed: return "Maho Managed"
        case .openai: return "OpenAI"
        case .anthropic: return "Anthropic"
        case .openaiCompatible: return "OpenAI-compatible"
        case .localServer: return "Local Server (Ollama)"
        }
    }

    var byokKeychainProvider: String? {
        switch self {
        case .openai: return "openai"
        case .anthropic: return "anthropic"
        case .openaiCompatible: return "openai-compatible"
        case .managed, .localServer: return nil
        }
    }
}

enum AiProviderResolver {
    static let providerDefaultsKey = "maho_ai_provider"
    static let modelDefaultsKey = "maho_ai_model"
    static let baseURLDefaultsKey = "maho_ai_base_url"

    static let defaultModel = "gpt-4o-mini"
    static let localServerEndpoint = "http://localhost:11434/v1"

    // MARK: Selected provider (mirrors desktop `maho.ai.provider`)

    static var selectedProvider: AiProvider? {
        let raw = (UserDefaults.standard.string(forKey: providerDefaultsKey) ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if let provider = AiProvider(rawValue: raw) { return provider }
        return hasSignedInAccount ? .managed : nil
    }

    static func setSelectedProvider(_ provider: AiProvider) {
        UserDefaults.standard.set(provider.rawValue, forKey: providerDefaultsKey)
    }

    // MARK: Model / base URL / managed proxy overrides

    static var model: String {
        let stored = (UserDefaults.standard.string(forKey: modelDefaultsKey) ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return stored.isEmpty ? defaultModel : stored
    }

    static func setModel(_ rawValue: String) {
        UserDefaults.standard.set(rawValue.trimmingCharacters(in: .whitespacesAndNewlines), forKey: modelDefaultsKey)
    }

    static var baseURL: String {
        (UserDefaults.standard.string(forKey: baseURLDefaultsKey) ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    static func setBaseURL(_ rawValue: String) {
        UserDefaults.standard.set(rawValue.trimmingCharacters(in: .whitespacesAndNewlines), forKey: baseURLDefaultsKey)
    }

    static func currentProxyURL(bridge: MahoBridge = .shared) -> String? {
        return bridge.managedProxyURL()
    }

    static var hasSignedInAccount: Bool {
        guard let session = RelayAuthStore.shared.loadSession() else { return false }
        return !session.isAccessTokenExpired
    }

    // MARK: Resolution — returns config for the SELECTED provider only

    static func resolve(bridge: MahoBridge = .shared) -> AiProviderConfig? {
        guard let provider = selectedProvider else { return nil }

        switch provider {
        case .managed:
            return resolveManaged(bridge: bridge)

        case .openai:
            guard let key = byokKey(for: provider, bridge: bridge) else { return nil }
            return AiProviderConfig(
                providerLabel: "OpenAI",
                apiKey: key,
                endpoint: chatCompletionsEndpoint(base: "https://api.openai.com/v1"),
                model: model
            )

        case .anthropic:
            guard let key = byokKey(for: provider, bridge: bridge) else { return nil }
            return AiProviderConfig(
                providerLabel: "Anthropic",
                apiKey: key,
                endpoint: chatCompletionsEndpoint(base: "https://api.anthropic.com/v1"),
                model: model
            )

        case .openaiCompatible:
            let base = baseURL
            guard !base.isEmpty else { return nil }
            return AiProviderConfig(
                providerLabel: "Custom",
                apiKey: byokKey(for: provider, bridge: bridge) ?? "",
                endpoint: chatCompletionsEndpoint(base: base),
                model: model
            )

        case .localServer:
            return AiProviderConfig(
                providerLabel: "Local Server",
                apiKey: "",
                endpoint: chatCompletionsEndpoint(base: localServerEndpoint),
                model: model
            )
        }
    }

    static func resolveManaged(bridge: MahoBridge = .shared) -> AiProviderConfig? {
        guard let session = RelayAuthStore.shared.loadSession(),
              !session.isAccessTokenExpired,
              let proxyBase = currentProxyURL(bridge: bridge) else { return nil }
        return AiProviderConfig(
            providerLabel: "Maho AI",
            apiKey: session.accessToken,
            endpoint: chatCompletionsEndpoint(base: proxyBase),
            model: model
        )
    }

    private static func byokKey(for provider: AiProvider, bridge: MahoBridge) -> String? {
        guard let keychainProvider = provider.byokKeychainProvider else { return nil }
        let key = bridge.byokGetKey(provider: keychainProvider)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return key.isEmpty ? nil : key
    }

    // chat_session.rs POSTs to the raw endpoint (no path appended), so the caller
    // must supply the full completions URL — desktop appends it identically.
    static func chatCompletionsEndpoint(base: String) -> String {
        var trimmed = base
        while trimmed.hasSuffix("/") { trimmed.removeLast() }
        if trimmed.hasSuffix("/chat/completions") { return trimmed }
        return trimmed + "/chat/completions"
    }
}
