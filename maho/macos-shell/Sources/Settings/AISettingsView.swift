import AppKit

final class AISettingsViewController: NSViewController, CoreUpdateObserver, NSTextFieldDelegate {

    private var providerPopup = NSPopUpButton()
    private var apiKeyField = NSSecureTextField()
    private var apiKeyStatusLabel = NSTextField(labelWithString: "")
    private var apiKeyActionButton = NSButton(title: "", target: nil, action: nil)
    private var saveChangesButton = NSButton(title: "Save Changes", target: nil, action: nil)
    private var cancelChangesButton = NSButton(title: "Cancel", target: nil, action: nil)
    private var modelPopup = NSPopUpButton()
    private var serverEndpointField = NSTextField()
    private var serverModelField = NSTextField()
    private var apiKeyLabel = NSTextField(labelWithString: "API Key:")
    private var apiKeyRow = NSStackView()
    private var modelRow = NSStackView()
    private var serverEndpointRow = NSStackView()
    private var serverModelRow = NSStackView()
    private var tidyTabTitlesCheckbox = NSButton(checkboxWithTitle: "Tidy Tab Titles", target: nil, action: nil)
    private var tidyDownloadsCheckbox = NSButton(checkboxWithTitle: "Tidy Downloads", target: nil, action: nil)
    private var tidyTabsCheckbox = NSButton(checkboxWithTitle: "Tidy Tabs", target: nil, action: nil)
    private var aiCommandBarCheckbox = NSButton(checkboxWithTitle: "AI Command Bar", target: nil, action: nil)
    private var pagePreviewsCheckbox = NSButton(checkboxWithTitle: "Page Previews", target: nil, action: nil)
    private var instantLinksCheckbox = NSButton(checkboxWithTitle: "Instant Links", target: nil, action: nil)
    private var statusLabel = NSTextField(labelWithString: "")
    private var mcpToggle = NSButton(checkboxWithTitle: "Enable MCP Server", target: nil, action: nil)
    private var mcpStatusLabel = NSTextField(labelWithString: "")
    private var mcpCopyConfigButton = NSButton(title: "Copy Config", target: nil, action: nil)

    private let providers: [LLMProviderType] = [.openai, .anthropic, .localServer, .remoteServer]
    private let openaiModels = ["gpt-4o-mini", "gpt-4o", "gpt-4-turbo"]
    private let anthropicModels = ["claude-sonnet-4-20250514", "claude-haiku-4-20250414"]

    private var draftProvider: LLMProviderType = .openai
    private var genericModelDrafts: [LLMProviderType: String] = [:]
    private var serverEndpointDrafts: [LLMProviderType: String] = [:]
    private var serverModelDrafts: [LLMProviderType: String] = [:]
    private var apiKeyDrafts: [LLMProviderType: String] = [:]
    private var providersWithPendingKeyRemoval = Set<LLMProviderType>()

    override func loadView() {
        providerPopup.removeAllItems()
        for provider in providers {
            providerPopup.addItem(withTitle: provider.rawValue)
        }
        providerPopup.target = self
        providerPopup.action = #selector(providerChanged(_:))
        let providerRow = makeSettingsLabeledRow("Provider:", providerPopup)

        apiKeyField.placeholderString = "Enter API key"
        apiKeyField.translatesAutoresizingMaskIntoConstraints = false
        apiKeyField.delegate = self
        styleSettingsEditableTextField(apiKeyField)

        apiKeyStatusLabel.font = .systemFont(ofSize: 11)
        apiKeyStatusLabel.maximumNumberOfLines = 2
        apiKeyStatusLabel.lineBreakMode = .byWordWrapping
        styleSettingsLabel(apiKeyStatusLabel, secondary: true)

        apiKeyActionButton.target = self
        apiKeyActionButton.action = #selector(apiKeyActionButtonPressed(_:))
        apiKeyActionButton.bezelStyle = .rounded

        let apiKeyStack = makeSettingsPlainRow([apiKeyField, apiKeyStatusLabel, apiKeyActionButton])
        apiKeyRow = makeSettingsLabeledRow("API Key:", apiKeyStack)
        apiKeyLabel = apiKeyRow.arrangedSubviews.first as? NSTextField ?? apiKeyLabel

        modelPopup.target = self
        modelPopup.action = #selector(modelChanged(_:))
        modelRow = makeSettingsLabeledRow("Model:", modelPopup)

        serverEndpointField.placeholderString = "http://localhost:11434/v1"
        serverEndpointField.translatesAutoresizingMaskIntoConstraints = false
        serverEndpointField.delegate = self
        styleSettingsEditableTextField(serverEndpointField)
        serverEndpointRow = makeSettingsLabeledRow("Endpoint URL:", serverEndpointField)

        serverModelField.placeholderString = "llama3.2"
        serverModelField.translatesAutoresizingMaskIntoConstraints = false
        serverModelField.delegate = self
        styleSettingsEditableTextField(serverModelField)
        serverModelRow = makeSettingsLabeledRow("Model Name:", serverModelField)

        statusLabel.font = .systemFont(ofSize: 12)
        let statusRow = makeSettingsLabeledRow("Status:", statusLabel)

        saveChangesButton.target = self
        saveChangesButton.action = #selector(saveChanges(_:))
        saveChangesButton.bezelStyle = .rounded

        cancelChangesButton.target = self
        cancelChangesButton.action = #selector(cancelChanges(_:))
        cancelChangesButton.bezelStyle = .rounded

        let actionRow = makeSettingsActionRow([cancelChangesButton, saveChangesButton])
        let actionCard = makeSettingsSectionCard([actionRow])

        let featuresLabel = NSTextField(labelWithString: "Features")
        featuresLabel.font = .boldSystemFont(ofSize: 13)
        styleSettingsLabel(featuresLabel)

        for checkbox in [tidyTabTitlesCheckbox, tidyDownloadsCheckbox, tidyTabsCheckbox, aiCommandBarCheckbox, pagePreviewsCheckbox, instantLinksCheckbox] {
            checkbox.target = self
            checkbox.action = #selector(featureToggled(_:))
            checkbox.setContentHuggingPriority(.defaultHigh, for: .horizontal)
        }

        let featuresCard = makeSettingsSectionCard([
            featuresLabel,
            tidyTabTitlesCheckbox,
            tidyDownloadsCheckbox,
            tidyTabsCheckbox,
            aiCommandBarCheckbox,
            pagePreviewsCheckbox,
            instantLinksCheckbox,
        ])

        let mcpLabel = NSTextField(labelWithString: "MCP Server")
        mcpLabel.font = .boldSystemFont(ofSize: 13)
        styleSettingsLabel(mcpLabel)

        mcpToggle.target = self
        mcpToggle.action = #selector(mcpToggleChanged(_:))
        mcpToggle.state = UserDefaults.standard.bool(forKey: "maho.mcp.enabled") ? .on : .off

        mcpStatusLabel.font = .systemFont(ofSize: 12)
        updateMCPStatus()
        let mcpStatusRow = makeSettingsLabeledRow("Status:", mcpStatusLabel, embedded: true)

        mcpCopyConfigButton.target = self
        mcpCopyConfigButton.action = #selector(copyMCPConfig(_:))
        mcpCopyConfigButton.bezelStyle = .rounded

        let mcpNote = NSTextField(labelWithString: "Only local connections are accepted")
        mcpNote.font = .systemFont(ofSize: 11)
        mcpNote.textColor = MahoAppColors.secondaryText

        let mcpActionRow = makeSettingsPlainRow([mcpCopyConfigButton, mcpNote])
        let mcpToggleRow = makeSettingsPlainRow([mcpToggle])
        let mcpCard = makeSettingsSectionCard([
            mcpLabel,
            mcpToggleRow,
            mcpStatusRow,
            mcpActionRow,
        ])

        view = SettingsPaneHostView(sections: [
            .plain([
                providerRow,
                apiKeyRow,
                serverEndpointRow,
                serverModelRow,
                modelRow,
                statusRow,
            ]),
            .plain([actionCard]),
            .plain([featuresCard]),
            .plain([mcpCard]),
        ])
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        CoreUpdateDispatcher.shared.register(self)
        resetDraftStateFromPersisted()
        hydrateFromSettings()
        updateStatus()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        if case .settingsChanged(let settings) = update {
            hydrateFromSettings(settings)
        }
    }

    // MARK: - Hydration

    private func hydrateFromSettings(_ settings: Settings? = nil) {
        let settings = settings ?? MahoBridge.shared.getSettings()
        guard let settings else { return }

        tidyTabTitlesCheckbox.state = settings.max.tidyTabTitles ? .on : .off
        tidyDownloadsCheckbox.state = settings.max.tidyDownloads ? .on : .off
        tidyTabsCheckbox.state = settings.max.tidyTabs ? .on : .off
        aiCommandBarCheckbox.state = settings.max.aiCommandBar ? .on : .off
        pagePreviewsCheckbox.state = settings.max.pagePreviews ? .on : .off
        instantLinksCheckbox.state = settings.max.instantLinks ? .on : .off
    }

    private func resetDraftStateFromPersisted() {
        draftProvider = persistedProvider()
        genericModelDrafts[.openai] = persistedGenericModel(for: .openai)
        genericModelDrafts[.anthropic] = persistedGenericModel(for: .anthropic)
        serverEndpointDrafts[.localServer] = persistedEndpoint(for: .localServer)
        serverModelDrafts[.localServer] = persistedServerModel(for: .localServer)
        serverEndpointDrafts[.remoteServer] = persistedEndpoint(for: .remoteServer)
        serverModelDrafts[.remoteServer] = persistedServerModel(for: .remoteServer)

        providersWithPendingKeyRemoval.removeAll()
        apiKeyDrafts.removeAll()
        for provider in providers {
            apiKeyDrafts[provider] = ""
        }

        applyDraftStateToControls()
    }

    private func applyDraftStateToControls() {
        providerPopup.selectItem(withTitle: draftProvider.rawValue)
        refreshProviderControls()
        updateChangeControls()
        updateStatus()
    }

    private func refreshProviderControls() {
        let provider = draftProvider
        let isServerProvider = isServer(provider)

        modelRow.isHidden = isServerProvider
        serverEndpointRow.isHidden = !isServerProvider
        serverModelRow.isHidden = !isServerProvider
        apiKeyLabel.stringValue = provider == .localServer ? "API Key (optional):" : "API Key:"

        if isServerProvider {
            serverEndpointField.stringValue = serverEndpointDrafts[provider] ?? ""
            serverModelField.stringValue = serverModelDrafts[provider] ?? ""
        } else {
            let models = models(for: provider)
            modelPopup.removeAllItems()
            for model in models {
                modelPopup.addItem(withTitle: model)
            }

            let draftModel = genericModelDrafts[provider] ?? models.first ?? ""
            if models.contains(draftModel) {
                modelPopup.selectItem(withTitle: draftModel)
            } else {
                modelPopup.selectItem(at: 0)
                genericModelDrafts[provider] = modelPopup.titleOfSelectedItem ?? ""
            }
        }

        apiKeyField.placeholderString = provider == .localServer ? "Optional API key" : "Enter API key"
        apiKeyField.stringValue = apiKeyDrafts[provider] ?? ""
        updateAPIKeyState(for: provider)
    }

    private func persistedProvider() -> LLMProviderType {
        if let name = UserDefaults.standard.string(forKey: AIProviderStorageKey.provider),
           let provider = LLMProviderType.persistedType(from: name) {
            return provider
        }
        return .openai
    }

    private func persistedGenericModel(for provider: LLMProviderType) -> String {
        let availableModels = models(for: provider)
        let storedModel = UserDefaults.standard.string(forKey: genericModelStorageKey(for: provider) ?? "")
            ?? UserDefaults.standard.string(forKey: AIProviderStorageKey.model)

        if let storedModel, availableModels.contains(storedModel) {
            return storedModel
        }
        return availableModels.first ?? ""
    }

    private func persistedEndpoint(for provider: LLMProviderType) -> String {
        let defaults = UserDefaults.standard
        switch provider {
        case .localServer:
            return defaults.string(forKey: AIProviderStorageKey.localEndpoint)
                ?? defaults.string(forKey: AIProviderStorageKey.legacyCustomEndpoint)
                ?? ""
        case .remoteServer:
            return defaults.string(forKey: AIProviderStorageKey.remoteEndpoint) ?? ""
        case .openai, .anthropic:
            return ""
        }
    }

    private func persistedServerModel(for provider: LLMProviderType) -> String {
        let defaults = UserDefaults.standard
        switch provider {
        case .localServer:
            return defaults.string(forKey: AIProviderStorageKey.localModel)
                ?? defaults.string(forKey: AIProviderStorageKey.legacyCustomModel)
                ?? ""
        case .remoteServer:
            return defaults.string(forKey: AIProviderStorageKey.remoteModel) ?? ""
        case .openai, .anthropic:
            return ""
        }
    }

    private func genericModelStorageKey(for provider: LLMProviderType) -> String? {
        switch provider {
        case .openai:
            return AIProviderStorageKey.openAIModel
        case .anthropic:
            return AIProviderStorageKey.anthropicModel
        case .localServer, .remoteServer:
            return nil
        }
    }

    private func isServer(_ provider: LLMProviderType) -> Bool {
        provider == .localServer || provider == .remoteServer
    }

    private func models(for provider: LLMProviderType) -> [String] {
        switch provider {
        case .openai:
            return openaiModels
        case .anthropic:
            return anthropicModels
        case .localServer, .remoteServer:
            return []
        }
    }

    private func captureVisibleDraftValues() {
        let provider = draftProvider
        apiKeyDrafts[provider] = apiKeyField.stringValue

        if isServer(provider) {
            serverEndpointDrafts[provider] = serverEndpointField.stringValue
            serverModelDrafts[provider] = serverModelField.stringValue
        } else if let selectedModel = modelPopup.titleOfSelectedItem {
            genericModelDrafts[provider] = selectedModel
        }
    }

    private func updateAPIKeyState(for provider: LLMProviderType) {
        let trimmedDraft = (apiKeyDrafts[provider] ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
        let hasSavedKey = APIKeyStore.shared.hasKey(for: provider.rawValue)
        let pendingRemoval = providersWithPendingKeyRemoval.contains(provider)

        apiKeyActionButton.isHidden = false
        apiKeyActionButton.isEnabled = true

        if pendingRemoval {
            apiKeyStatusLabel.stringValue = "Saved key will be removed when you save."
            apiKeyStatusLabel.textColor = NSColor.systemOrange
            apiKeyActionButton.title = "Keep Saved Key"
            return
        }

        styleSettingsLabel(apiKeyStatusLabel, secondary: true)

        if !trimmedDraft.isEmpty {
            apiKeyStatusLabel.stringValue = hasSavedKey
                ? "New key will replace the saved key when you save."
                : "New key will be saved securely when you save."
            apiKeyActionButton.title = hasSavedKey ? "Remove Saved Key" : "Clear Draft"
            apiKeyActionButton.isHidden = false
            return
        }

        if hasSavedKey {
            apiKeyStatusLabel.stringValue = "Key saved. Leave the field blank to keep it, or enter a new key to replace it."
            apiKeyActionButton.title = "Remove Saved Key"
            return
        }

        apiKeyStatusLabel.stringValue = provider == .localServer
            ? "Optional. Leave blank if your local server does not require a key."
            : "Not saved yet. Enter a key, then click Save Changes."
        apiKeyActionButton.isHidden = true
    }

    private func updateChangeControls() {
        let hasUnsavedChanges = hasPendingChanges
        saveChangesButton.isEnabled = hasUnsavedChanges
        cancelChangesButton.isEnabled = hasUnsavedChanges
    }

    private func updateStatus() {
        updateChangeControls()

        if hasPendingChanges {
            statusLabel.stringValue = "Unsaved changes — the current AI configuration stays active until you click Save Changes."
            statusLabel.textColor = MahoAppColors.secondaryText
            return
        }

        let provider = persistedProvider()
        let hasSavedKey = APIKeyStore.shared.hasKey(for: provider.rawValue)

        if isServer(provider) {
            let endpoint = persistedEndpoint(for: provider).trimmingCharacters(in: .whitespacesAndNewlines)
            let model = persistedServerModel(for: provider).trimmingCharacters(in: .whitespacesAndNewlines)

            if endpoint.isEmpty || model.isEmpty {
                statusLabel.stringValue = "✗ Endpoint or model not set"
                statusLabel.textColor = MahoAppColors.secondaryText
                return
            }

            if provider == .remoteServer && !hasSavedKey {
                statusLabel.stringValue = "✗ No API Key"
                statusLabel.textColor = MahoAppColors.secondaryText
                return
            }

            if LLMService.shared.currentProviderType == provider && LLMService.shared.isConfigured {
                statusLabel.stringValue = "✓ Connected"
                statusLabel.textColor = NSColor.systemGreen
            } else {
                statusLabel.stringValue = "Saved"
                statusLabel.textColor = MahoAppColors.secondaryText
            }
            return
        }

        if hasSavedKey && LLMService.shared.currentProviderType == provider && LLMService.shared.isConfigured {
            statusLabel.stringValue = "✓ Connected"
            statusLabel.textColor = NSColor.systemGreen
        } else if hasSavedKey {
            statusLabel.stringValue = "Saved"
            statusLabel.textColor = MahoAppColors.secondaryText
        } else {
            statusLabel.stringValue = "✗ No API Key"
            statusLabel.textColor = MahoAppColors.secondaryText
        }
    }

    private var hasPendingChanges: Bool {
        if draftProvider != persistedProvider() {
            return true
        }

        if genericModelDrafts[.openai] != persistedGenericModel(for: .openai) {
            return true
        }
        if genericModelDrafts[.anthropic] != persistedGenericModel(for: .anthropic) {
            return true
        }

        if normalizedDraft(serverEndpointDrafts[.localServer]) != persistedEndpoint(for: .localServer).trimmingCharacters(in: .whitespacesAndNewlines) {
            return true
        }
        if normalizedDraft(serverModelDrafts[.localServer]) != persistedServerModel(for: .localServer).trimmingCharacters(in: .whitespacesAndNewlines) {
            return true
        }
        if normalizedDraft(serverEndpointDrafts[.remoteServer]) != persistedEndpoint(for: .remoteServer).trimmingCharacters(in: .whitespacesAndNewlines) {
            return true
        }
        if normalizedDraft(serverModelDrafts[.remoteServer]) != persistedServerModel(for: .remoteServer).trimmingCharacters(in: .whitespacesAndNewlines) {
            return true
        }

        for provider in providers {
            let draftKey = normalizedDraft(apiKeyDrafts[provider])
            if !draftKey.isEmpty {
                return true
            }
            if providersWithPendingKeyRemoval.contains(provider) && APIKeyStore.shared.hasKey(for: provider.rawValue) {
                return true
            }
        }

        return false
    }

    private func normalizedDraft(_ value: String?) -> String {
        (value ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
    }

    // MARK: - Text Updates

    func controlTextDidChange(_ notification: Notification) {
        guard let field = notification.object as? NSTextField else { return }
        let provider = draftProvider

        if field === apiKeyField {
            apiKeyDrafts[provider] = field.stringValue
            if !normalizedDraft(field.stringValue).isEmpty {
                providersWithPendingKeyRemoval.remove(provider)
            }
            updateAPIKeyState(for: provider)
            updateStatus()
            return
        }

        if field === serverEndpointField {
            serverEndpointDrafts[provider] = field.stringValue
            updateStatus()
            return
        }

        if field === serverModelField {
            serverModelDrafts[provider] = field.stringValue
            updateStatus()
        }
    }

    // MARK: - Actions

    @objc private func providerChanged(_ sender: NSPopUpButton) {
        captureVisibleDraftValues()

        let selectedIndex = sender.indexOfSelectedItem
        if selectedIndex >= 0 && selectedIndex < providers.count {
            draftProvider = providers[selectedIndex]
        } else {
            draftProvider = .openai
        }

        refreshProviderControls()
        updateStatus()
    }

    @objc private func modelChanged(_ sender: NSPopUpButton) {
        guard let model = sender.titleOfSelectedItem else { return }
        genericModelDrafts[draftProvider] = model
        updateStatus()
    }

    @objc private func apiKeyActionButtonPressed(_ sender: NSButton) {
        let provider = draftProvider
        let hasSavedKey = APIKeyStore.shared.hasKey(for: provider.rawValue)
        let pendingRemoval = providersWithPendingKeyRemoval.contains(provider)
        let trimmedDraft = normalizedDraft(apiKeyDrafts[provider])

        if pendingRemoval {
            providersWithPendingKeyRemoval.remove(provider)
        } else if !trimmedDraft.isEmpty && !hasSavedKey {
            apiKeyDrafts[provider] = ""
        } else if hasSavedKey {
            providersWithPendingKeyRemoval.insert(provider)
            apiKeyDrafts[provider] = ""
            apiKeyField.stringValue = ""
        }

        updateAPIKeyState(for: provider)
        updateStatus()
    }

    @objc private func saveChanges(_ sender: NSButton) {
        view.window?.makeFirstResponder(nil)
        captureVisibleDraftValues()

        let defaults = UserDefaults.standard
        defaults.set(draftProvider.rawValue, forKey: AIProviderStorageKey.provider)
        defaults.set(normalizedDraft(serverEndpointDrafts[.localServer]), forKey: AIProviderStorageKey.localEndpoint)
        defaults.set(normalizedDraft(serverModelDrafts[.localServer]), forKey: AIProviderStorageKey.localModel)
        defaults.set(normalizedDraft(serverEndpointDrafts[.remoteServer]), forKey: AIProviderStorageKey.remoteEndpoint)
        defaults.set(normalizedDraft(serverModelDrafts[.remoteServer]), forKey: AIProviderStorageKey.remoteModel)

        if let openAIKey = genericModelStorageKey(for: .openai) {
            defaults.set(genericModelDrafts[.openai] ?? openaiModels.first ?? "", forKey: openAIKey)
        }
        if let anthropicKey = genericModelStorageKey(for: .anthropic) {
            defaults.set(genericModelDrafts[.anthropic] ?? anthropicModels.first ?? "", forKey: anthropicKey)
        }

        if let activeGenericModel = genericModelDrafts[draftProvider],
           !activeGenericModel.isEmpty,
           genericModelStorageKey(for: draftProvider) != nil {
            defaults.set(activeGenericModel, forKey: AIProviderStorageKey.model)
        }

        for provider in providers {
            let trimmedDraft = normalizedDraft(apiKeyDrafts[provider])

            if providersWithPendingKeyRemoval.contains(provider) {
                APIKeyStore.shared.delete(for: provider.rawValue)
                continue
            }

            if !trimmedDraft.isEmpty {
                _ = APIKeyStore.shared.save(key: trimmedDraft, for: provider.rawValue)
            }
        }

        LLMService.shared.configure(type: draftProvider)
        resetDraftStateFromPersisted()
    }

    @objc private func cancelChanges(_ sender: NSButton) {
        view.window?.makeFirstResponder(nil)
        resetDraftStateFromPersisted()
    }

    @objc private func featureToggled(_ sender: NSButton) {
        let maxUpdate = MaxSettingsUpdate(
            enabled: nil,
            pagePreviews: sender == pagePreviewsCheckbox ? (sender.state == .on) : nil,
            tidyTabTitles: sender == tidyTabTitlesCheckbox ? (sender.state == .on) : nil,
            tidyDownloads: sender == tidyDownloadsCheckbox ? (sender.state == .on) : nil,
            tidyTabs: sender == tidyTabsCheckbox ? (sender.state == .on) : nil,
            aiCommandBar: sender == aiCommandBarCheckbox ? (sender.state == .on) : nil,
            instantLinks: sender == instantLinksCheckbox ? (sender.state == .on) : nil
        )
        let update = SettingsUpdate(
            general: nil,
            appearance: nil,
            privacy: nil,
            reader: nil,
            keyboardShortcuts: nil,
            perSiteSettings: nil,
            toolbarItems: nil,
            max: maxUpdate,
            autofill: nil,
            advanced: nil,
            notifications: nil
        )
        CoreUpdateDispatcher.shared.sendAndDispatch(.updateSettings(changes: update))
    }

    @objc private func mcpToggleChanged(_ sender: NSButton) {
        let enabled = sender.state == .on
        UserDefaults.standard.set(enabled, forKey: "maho.mcp.enabled")
        if enabled {
            if let mainWC = (NSApp.delegate as? AppDelegate)?.mainWindowControllerRef {
                MCPServer.shared.toolRegistry.delegate = mainWC
            }
            MCPServer.shared.start()
        } else {
            MCPServer.shared.stop()
        }
        updateMCPStatus()
    }

    @objc private func copyMCPConfig(_ sender: NSButton) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(MCPServer.shared.configJSON(), forType: .string)
    }

    private func updateMCPStatus() {
        if MCPServer.shared.isRunning {
            mcpStatusLabel.stringValue = "✓ Running on localhost:9851"
            mcpStatusLabel.textColor = NSColor.systemGreen
        } else if UserDefaults.standard.bool(forKey: "maho.mcp.enabled") {
            mcpStatusLabel.stringValue = "Starting..."
            mcpStatusLabel.textColor = MahoAppColors.secondaryText
        } else {
            mcpStatusLabel.stringValue = "Stopped"
            mcpStatusLabel.textColor = MahoAppColors.secondaryText
        }
    }
}
