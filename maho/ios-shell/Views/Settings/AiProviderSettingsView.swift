import SwiftUI

@MainActor
struct AiProviderSettingsView: View {
    private let bridge = MahoBridge.shared

    @State private var provider: AiProvider = .managed
    @State private var model: String = ""
    @State private var baseURL: String = ""
    @State private var apiKey: String = ""
    @State private var availableModels: [String] = []
    @State private var isFetchingModels = false
    @State private var modelsAttempted = false

    var body: some View {
        Form {
            Section {
                Picker("Provider", selection: $provider) {
                    ForEach(AiProvider.allCases) { option in
                        Text(option.displayName).tag(option)
                    }
                }
                .onChange(of: provider) { _, newValue in
                    AiProviderResolver.setSelectedProvider(newValue)
                    loadKeyForCurrentProvider()
                    refreshModels()
                }
            } header: {
                Text("AI Provider")
            } footer: {
                Text(providerFooter)
            }

            switch provider {
            case .managed:
                Section {
                    Text(AiProviderResolver.hasSignedInAccount
                         ? "Signed in — AI runs on your Maho account credits."
                         : "Sign in under Settings → Sync to use free credits.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                } header: {
                    Text("Maho Managed")
                } footer: {
                    Text("AI runs on Maho's servers using your account credits — no setup required.")
                }

            case .openai, .anthropic:
                keySection
                modelSection

            case .openaiCompatible:
                Section {
                    TextField("Base URL", text: $baseURL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .keyboardType(.URL)
                        .textContentType(.URL)
                        .accessibilityLabel("Base URL")
                        .accessibilityIdentifier("aiProviderBaseURLField")
                        .onChange(of: baseURL) { _, newValue in
                            AiProviderResolver.setBaseURL(newValue)
                        }
                } header: {
                    Text("Base URL")
                } footer: {
                    Text("Enter the provider's OpenAI-compatible API endpoint, including its version path.")
                }
                keySection
                modelSection

            case .localServer:
                Section {
                    Text(AiProviderResolver.localServerEndpoint)
                        .font(.system(.body, design: .monospaced))
                        .foregroundStyle(.secondary)
                } header: {
                    Text("Endpoint")
                } footer: {
                    Text("Fixed local Ollama endpoint. Start Ollama on this device's host.")
                }
                modelSection
            }
        }
        .navigationTitle("AI Provider")
        .onAppear(perform: loadAll)
    }

    private var keySection: some View {
        Section {
            SecureField(keyPlaceholder, text: $apiKey)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .accessibilityLabel("API Key")
                .accessibilityIdentifier("aiProviderAPIKeyField")
                .onChange(of: apiKey) { _, newValue in
                    persistKey(newValue)
                }
        } header: {
            Text("API Key")
        } footer: {
            Text(provider == .openaiCompatible
                 ? "Optional. Sent as a Bearer token. Stored in the iOS Keychain."
                 : "Stored securely in the iOS Keychain.")
        }
    }

    private var modelSection: some View {
        Section {
            TextField(modelPlaceholder, text: $model)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .accessibilityLabel("Model")
                .accessibilityIdentifier("aiProviderModelField")
                .onChange(of: model) { _, newValue in
                    AiProviderResolver.setModel(newValue)
                }

            if isFetchingModels {
                HStack(spacing: 8) {
                    ProgressView()
                    Text("Fetching models…")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            } else if !availableModels.isEmpty {
                Menu {
                    ForEach(availableModels, id: \.self) { name in
                        Button(name) {
                            model = name
                            AiProviderResolver.setModel(name)
                        }
                    }
                } label: {
                    Label("Choose from \(availableModels.count) models", systemImage: "list.bullet")
                }
            } else if modelsAttempted {
                Text(AiModelListFetcher.emptyHint(for: provider))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        } header: {
            HStack {
                Text("Model")
                Spacer()
                Button("Refresh") { refreshModels() }
                    .font(.caption)
                    .textCase(nil)
                    .disabled(isFetchingModels)
            }
        }
    }

    private var providerFooter: String {
        switch provider {
        case .managed: return "Route AI through Maho's managed proxy using your account credits."
        case .openai: return "Call OpenAI directly with your own API key."
        case .anthropic: return "Call Anthropic directly with your own API key."
        case .openaiCompatible: return "Call any OpenAI-compatible server (custom URL)."
        case .localServer: return "Call a local Ollama server running on this device's host."
        }
    }

    private var keyPlaceholder: String {
        switch provider {
        case .openai: return "sk-..."
        case .anthropic: return "sk-ant-..."
        default: return "API key (optional)"
        }
    }

    private var modelPlaceholder: String {
        switch provider {
        case .localServer: return "e.g. llama3.1:8b"
        case .anthropic: return "e.g. claude-sonnet-4-20250514"
        default: return "e.g. gpt-4o-mini"
        }
    }

    private func loadAll() {
        provider = AiProviderResolver.selectedProvider ?? .managed
        model = AiProviderResolver.model
        baseURL = AiProviderResolver.baseURL
        loadKeyForCurrentProvider()
        refreshModels()
    }

    private func refreshModels() {
        availableModels = []
        modelsAttempted = true
        isFetchingModels = true
        let requested = provider
        Task {
            let fetched = await AiModelListFetcher.fetch(provider: requested)
            let result = fetched.isEmpty ? AiModelListFetcher.fallback(for: requested) : fetched
            await MainActor.run {
                guard provider == requested else { return }
                availableModels = result
                isFetchingModels = false
            }
        }
    }

    private func loadKeyForCurrentProvider() {
        guard let keychainProvider = provider.byokKeychainProvider else {
            apiKey = ""
            return
        }
        apiKey = bridge.byokGetKey(provider: keychainProvider) ?? ""
    }

    private func persistKey(_ value: String) {
        guard let keychainProvider = provider.byokKeychainProvider else { return }
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            _ = bridge.byokDeleteKey(provider: keychainProvider)
        } else {
            _ = bridge.byokSetKey(provider: keychainProvider, key: trimmed)
        }
    }
}
