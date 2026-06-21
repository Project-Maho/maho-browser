import SwiftUI
#if canImport(LucideIcons)
import LucideIcons
#endif

@MainActor
struct BYOKSettingsView: View {
    private enum Provider: String, CaseIterable, Identifiable {
        case openai
        case anthropic

        var id: String { rawValue }

        var displayName: String {
            switch self {
            case .openai:
                return "OpenAI"
            case .anthropic:
                return "Anthropic"
            }
        }

        var keyPlaceholder: String {
            switch self {
            case .openai:
                return "sk-..."
            case .anthropic:
                return "sk-ant-..."
            }
        }

        var helperText: String {
            switch self {
            case .openai:
                return "Used for OpenAI-powered browsing and AI features."
            case .anthropic:
                return "Used for Claude-powered browsing and AI features."
            }
        }

        static func ordered(using identifiers: [String]) -> [Provider] {
            let resolved = identifiers.compactMap { Provider(rawValue: $0.lowercased()) }
            return resolved + Provider.allCases.filter { !resolved.contains($0) }
        }
    }

    private enum ValidationState: Equatable {
        case idle
        case checking
        case valid
        case invalid(String)

        var message: String? {
            switch self {
            case .idle:
                return nil
            case .checking:
                return "Checking API key…"
            case .valid:
                return "Key is valid and ready to use."
            case let .invalid(message):
                return message
            }
        }
    }

    private let bridge = MahoBridge.shared

    @State private var providers: [Provider] = Provider.allCases
    @State private var storedKeys: [Provider: String] = [:]
    @State private var validationStates: [Provider: ValidationState] = [:]
    @State private var showAddKeySheet = false
    @State private var draftProvider: Provider = .openai
    @State private var draftKey = ""
    @State private var draftValidationState: ValidationState = .idle

    var body: some View {
        Form {
            Section {
                Text("Bring your own provider keys for Max AI, summaries, and future assistant features.")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Button {
                    presentAddKeyFlow()
                } label: {
                    Label {
                        Text("Add Key")
                    } icon: {
                        byokIcon("plus")
                    }
                }
            } header: {
                Text("Bring Your Own Key")
            } footer: {
                Text("Pick a provider, validate the key, then save it securely through the existing Maho bridge.")
            }

            ForEach(providers) { provider in
                Section {
                    providerSummaryRow(provider)
                    providerActionRow(provider)

                    if let statusMessage = statusMessage(for: provider) {
                        validationStatusRow(for: provider, message: statusMessage)
                    }
                } header: {
                    Text(provider.displayName)
                } footer: {
                    Text(provider.helperText)
                }
            }
        }
        .navigationTitle("API Keys")
        .accessibilityIdentifier("aiScreenBYOK")
        .onAppear(perform: loadProviders)
        .sheet(isPresented: $showAddKeySheet) {
            addKeySheet
                .presentationDetents([.medium])
        }
    }

    private var addKeySheet: some View {
        NavigationStack {
            Form {
                Section {
                    Picker("Provider", selection: $draftProvider) {
                        ForEach(providers) { provider in
                            Text(provider.displayName).tag(provider)
                        }
                    }

                    SecureField(draftProvider.keyPlaceholder, text: $draftKey)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()

                    if hasStoredKey(for: draftProvider) {
                        Text("Saving will replace the current \(draftProvider.displayName) key.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }

                    HStack(spacing: ShellTheme.Spacing.medium) {
                        Button {
                            validateDraftKey()
                        } label: {
                            Label {
                                Text("Validate")
                            } icon: {
                                byokIcon("checkmark")
                            }
                        }
                        .disabled(trimmedDraftKey.isEmpty || draftValidationState == .checking)

                        Spacer()

                        Button {
                            saveDraftKey()
                        } label: {
                            Label {
                                Text("Save")
                            } icon: {
                                byokIcon("key")
                            }
                        }
                        .disabled(!canSaveDraft)
                    }

                    if let draftStatusMessage = draftValidationState.message {
                        validationStatusRow(for: draftValidationState, message: draftStatusMessage)
                    }
                } header: {
                    Text("Add Key")
                }
            }
            .navigationTitle("Add Key")
            .toolbar(content: {
                ToolbarItemGroup(placement: .topBarLeading) {
                    Button("Cancel") {
                        dismissAddKeyFlow()
                    }
                }
            })
            .onChange(of: draftProvider) { _, _ in
                draftValidationState = .idle
            }
            .onChange(of: draftKey) { _, _ in
                draftValidationState = .idle
            }
        }
    }

    private func providerSummaryRow(_ provider: Provider) -> some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            byokIcon("key")
                .foregroundStyle(ShellTheme.Palette.accent)

            VStack(alignment: .leading, spacing: ShellTheme.Spacing.xxSmall) {
                Text(provider.displayName)
                    .font(.body)
                Text(maskedKeyText(for: provider))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func providerActionRow(_ provider: Provider) -> some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            Button {
                validateStoredKey(for: provider)
            } label: {
                Label {
                    Text("Validate")
                } icon: {
                    byokIcon("checkmark")
                }
            }
            .disabled(!hasStoredKey(for: provider) || validationState(for: provider) == .checking)

            Spacer()

            Button(role: .destructive) {
                deleteKey(for: provider)
            } label: {
                Label {
                    Text("Delete")
                } icon: {
                    byokIcon("trash")
                }
            }
            .disabled(!hasStoredKey(for: provider))
        }
    }

    private func validationStatusRow(for provider: Provider, message: String) -> some View {
        validationStatusRow(for: validationState(for: provider), message: message)
    }

    private func validationStatusRow(for state: ValidationState, message: String) -> some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            switch state {
            case .idle:
                EmptyView()
            case .checking:
                ProgressView()
            case .valid:
                byokIcon("checkmark.circle")
                    .foregroundStyle(.green)
            case .invalid:
                byokIcon("exclamationmark.triangle.fill")
                    .foregroundStyle(.red)
            }

            Text(message)
                .font(.caption)
                .foregroundStyle(statusColor(for: state))
        }
    }

    private func loadProviders() {
        providers = Provider.ordered(using: bridge.byokGetProviders())

        for provider in providers {
            let trimmedKey = bridge.byokGetKey(provider: provider.rawValue)?
                .trimmingCharacters(in: .whitespacesAndNewlines)

            if let trimmedKey, !trimmedKey.isEmpty {
                storedKeys[provider] = trimmedKey
            } else {
                storedKeys.removeValue(forKey: provider)
            }

            if validationStates[provider] == nil {
                validationStates[provider] = .idle
            }
        }

        if !providers.contains(draftProvider), let firstProvider = providers.first {
            draftProvider = firstProvider
        }
    }

    private func presentAddKeyFlow() {
        draftProvider = providers.first(where: { !hasStoredKey(for: $0) }) ?? providers.first ?? .openai
        draftKey = ""
        draftValidationState = .idle
        showAddKeySheet = true
    }

    private func dismissAddKeyFlow() {
        draftKey = ""
        draftValidationState = .idle
        showAddKeySheet = false
    }

    private func validateStoredKey(for provider: Provider) {
        guard let key = storedKeys[provider], !key.isEmpty else { return }

        validationStates[provider] = .checking
        let providerID = provider.rawValue

        DispatchQueue.global(qos: .userInitiated).async {
            let isValid = MahoBridge.shared.byokValidateKey(provider: providerID, key: key)

            DispatchQueue.main.async {
                validationStates[provider] = isValid
                    ? .valid
                    : .invalid("Validation failed. Double-check the saved key and try again.")
            }
        }
    }

    private func validateDraftKey() {
        let key = trimmedDraftKey
        guard !key.isEmpty else { return }

        draftValidationState = .checking
        let providerID = draftProvider.rawValue

        DispatchQueue.global(qos: .userInitiated).async {
            let isValid = MahoBridge.shared.byokValidateKey(provider: providerID, key: key)

            DispatchQueue.main.async {
                draftValidationState = isValid
                    ? .valid
                    : .invalid("Validation failed. Make sure the provider and key match.")
            }
        }
    }

    private func saveDraftKey() {
        let key = trimmedDraftKey
        guard !key.isEmpty, draftValidationState == .valid else { return }

        guard bridge.byokSetKey(provider: draftProvider.rawValue, key: key) else {
            draftValidationState = .invalid("The key validated but could not be saved.")
            return
        }

        storedKeys[draftProvider] = key
        validationStates[draftProvider] = .valid
        dismissAddKeyFlow()
    }

    private func deleteKey(for provider: Provider) {
        guard bridge.byokDeleteKey(provider: provider.rawValue) else {
            validationStates[provider] = .invalid("Could not delete the saved key.")
            return
        }

        storedKeys.removeValue(forKey: provider)
        validationStates[provider] = .idle
    }

    private func maskedKeyText(for provider: Provider) -> String {
        guard let key = storedKeys[provider], !key.isEmpty else {
            return "No key saved"
        }

        let visibleSuffix = String(key.suffix(4))
        let maskLength = max(8, key.count - visibleSuffix.count)
        return String(repeating: "•", count: maskLength) + visibleSuffix
    }

    private func hasStoredKey(for provider: Provider) -> Bool {
        guard let key = storedKeys[provider] else { return false }
        return !key.isEmpty
    }

    private func validationState(for provider: Provider) -> ValidationState {
        validationStates[provider] ?? .idle
    }

    private func statusMessage(for provider: Provider) -> String? {
        validationState(for: provider).message
    }

    private func statusColor(for state: ValidationState) -> Color {
        switch state {
        case .idle, .checking:
            return .secondary
        case .valid:
            return .green
        case .invalid:
            return .red
        }
    }

    private var trimmedDraftKey: String {
        draftKey.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private var canSaveDraft: Bool {
        !trimmedDraftKey.isEmpty && draftValidationState == .valid
    }

    @ViewBuilder
    private func byokIcon(_ symbol: String) -> some View {
#if canImport(LucideIcons)
        switch symbol {
        case "plus":
            Image(uiImage: Lucide.plus.withRenderingMode(.alwaysTemplate))
        case "checkmark":
            Image(uiImage: Lucide.check.withRenderingMode(.alwaysTemplate))
        case "key":
            Image(uiImage: Lucide.keyRound.withRenderingMode(.alwaysTemplate))
        case "trash":
            Image(uiImage: Lucide.trash2.withRenderingMode(.alwaysTemplate))
        case "checkmark.circle":
            Image(uiImage: Lucide.circleCheck.withRenderingMode(.alwaysTemplate))
        case "exclamationmark.triangle.fill":
            Image(uiImage: Lucide.triangleAlert.withRenderingMode(.alwaysTemplate))
        default:
            Image(uiImage: UIImage())
        }
#else
        Image(systemName: "questionmark.square")
#endif
    }
}
