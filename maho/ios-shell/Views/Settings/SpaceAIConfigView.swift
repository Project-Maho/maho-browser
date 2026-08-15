import SwiftUI
import LucideIcons

@MainActor
struct SpaceAIConfigView: View {
    private enum ToneOption: String, CaseIterable, Identifiable {
        case `default` = "Default"
        case professional = "Professional"
        case casual = "Casual"
        case technical = "Technical"
        case custom = "Custom"

        var id: String { rawValue }

        var storedValue: String? {
            switch self {
            case .default:
                return nil
            case .professional:
                return "Professional"
            case .casual:
                return "Casual"
            case .technical:
                return "Technical"
            case .custom:
                return nil
            }
        }
    }

    private enum ModelOption: String, CaseIterable, Identifiable {
        case `default` = "Default"
        case gpt4o = "gpt-4o"
        case claudeSonnet4 = "claude-sonnet-4"

        var id: String { rawValue }

        var title: String {
            switch self {
            case .default:
                return "Default"
            case .gpt4o:
                return "GPT-4o"
            case .claudeSonnet4:
                return "Claude Sonnet 4"
            }
        }

        var storedValue: String? {
            self == .default ? nil : rawValue
        }
    }

    private static let maximumSystemPromptLength = 500

    let spaceId: SpaceId
    let spaceName: String

    @Environment(\.dismiss) private var dismiss

    private let bridge = MahoBridge.shared

    @State private var systemPrompt: String = ""
    @State private var toneSelection: ToneOption = .default
    @State private var customTone: String = ""
    @State private var focusAreas: [String] = []
    @State private var preferredModel: ModelOption = .default
    @State private var memoryEnabled: Bool = true
    @State private var newFocusArea: String = ""
    @State private var initialConfig = SpaceAIConfig()
    @State private var isSaving = false
    @State private var didLoad = false
    @State private var saveErrorMessage: String?
    @State private var showSaveError = false

    var body: some View {
        Form {
            summarySection
            systemPromptSection
            toneSection
            focusAreasSection
            modelSection
            memorySection
        }
        .navigationTitle("AI Personality")
        .navigationBarTitleDisplayMode(.inline)
        .accessibilityIdentifier("aiScreenSpaceAIConfig")
        .toolbar {
            ToolbarItemGroup(placement: .cancellationAction) {
                Button("Cancel") {
                    dismiss()
                }
            }

            ToolbarItemGroup(placement: .confirmationAction) {
                Button(isSaving ? "Saving…" : "Save") {
                    saveConfig()
                }
                .disabled(!canSave)
            }
        }
        .onAppear(perform: loadConfig)
        .onChange(of: systemPrompt) { _, newValue in
            guard newValue.count > Self.maximumSystemPromptLength else { return }
            systemPrompt = String(newValue.prefix(Self.maximumSystemPromptLength))
        }
        .alert("Couldn't Save AI Personality", isPresented: $showSaveError) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(saveErrorMessage ?? "Try again in a moment.")
        }
    }

    private var summarySection: some View {
        Section {
            VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
                Text(spaceName)
                    .font(.headline)

                Text("Shape the assistant’s voice, priorities, and memory for this space only.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(.vertical, ShellTheme.Spacing.xxSmall)
        }
    }

    private var systemPromptSection: some View {
        Section {
            VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
                ZStack(alignment: .topLeading) {
                    if systemPrompt.isEmpty {
                        Text("Optional instructions for how the assistant should think, respond, and prioritize in this space.")
                            .foregroundStyle(.secondary)
                            .padding(.horizontal, ShellTheme.Spacing.mediumLarge)
                            .padding(.vertical, ShellTheme.Spacing.medium)
                            .allowsHitTesting(false)
                    }

                    TextEditor(text: $systemPrompt)
                        .scrollContentBackground(.hidden)
                        .frame(minHeight: ShellTheme.Size.touchTarget * 4)
                        .padding(.horizontal, ShellTheme.Spacing.xSmall)
                        .padding(.vertical, ShellTheme.Spacing.xxSmall)
                }
                .background(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        .fill(ShellTheme.Palette.searchFieldFill)
                )
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        .strokeBorder(ShellTheme.Palette.searchFieldBorder, lineWidth: ShellTheme.Stroke.hairline)
                )

                HStack {
                    Spacer()
                    Text("\(systemPrompt.count)/\(Self.maximumSystemPromptLength)")
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(systemPrompt.count >= Self.maximumSystemPromptLength ? ShellTheme.Palette.warning : .secondary)
                }
            }
        } header: {
            Label {
                Text("System Prompt")
            } icon: {
                Image(uiImage: Lucide.brain.withRenderingMode(.alwaysTemplate))
            }
        } footer: {
            Text("Leave blank to use the default personality. A short prompt usually works better than a long manifesto.")
        }
    }

    private var toneSection: some View {
        Section {
            Picker("Tone", selection: $toneSelection) {
                ForEach(ToneOption.allCases) { option in
                    Text(option.rawValue).tag(option)
                }
            }

            if toneSelection == .custom {
                TextField("Describe the tone", text: $customTone)
                    .textInputAutocapitalization(.sentences)
                    .autocorrectionDisabled()
            }
        } header: {
            Label {
                Text("Tone")
            } icon: {
                Image(uiImage: Lucide.tag.withRenderingMode(.alwaysTemplate))
            }
        } footer: {
            Text(toneSelection == .custom
                 ? "Custom tone needs a short description before you can save."
                 : "Pick a preset for fast setup, or switch to Custom for a more specific house voice.")
        }
    }

    private var focusAreasSection: some View {
        Section {
            if focusAreas.isEmpty {
                Text("No focus areas yet. Add a few tags like research, design critique, or code review.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } else {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: ShellTheme.Spacing.small) {
                        ForEach(focusAreas, id: \.self) { area in
                            focusAreaChip(area)
                        }
                    }
                    .padding(.vertical, ShellTheme.Spacing.xxSmall)
                }
            }

            HStack(spacing: ShellTheme.Spacing.medium) {
                TextField("Add focus area", text: $newFocusArea)
                    .textInputAutocapitalization(.words)
                    .autocorrectionDisabled()
                    .onSubmit(addFocusArea)

                Button {
                    addFocusArea()
                } label: {
                    Image(uiImage: Lucide.plus.withRenderingMode(.alwaysTemplate))
                        .frame(minWidth: ShellTheme.Size.touchTarget, minHeight: ShellTheme.Size.touchTarget)
                        .contentShape(Rectangle())
                }
                .disabled(trimmedNewFocusArea.isEmpty)
                .accessibilityLabel("Add focus area")
            }
        } header: {
            Label {
                Text("Focus Areas")
            } icon: {
                Image(uiImage: Lucide.target.withRenderingMode(.alwaysTemplate))
            }
        } footer: {
            Text("Tap a chip to remove it. Focus areas act like steering tags for what the assistant should emphasize.")
        }
    }

    private var modelSection: some View {
        Section {
            Picker("Preferred Model", selection: $preferredModel) {
                ForEach(ModelOption.allCases) { option in
                    Text(option.title).tag(option)
                }
            }
        } header: {
            Label {
                Text("Preferred Model")
            } icon: {
                Image(uiImage: Lucide.cpu.withRenderingMode(.alwaysTemplate))
            }
        } footer: {
            Text("Set a strong default for this space, or leave it on Default to let Maho choose automatically.")
        }
    }

    private var memorySection: some View {
        Section {
            Toggle(isOn: $memoryEnabled) {
                Label {
                    Text("Memory Enabled")
                } icon: {
                    Image(uiImage: Lucide.database.withRenderingMode(.alwaysTemplate))
                }
            }
        } footer: {
            Text("When enabled, the assistant can remember prior context from this space and carry it forward between chats.")
        }
    }

    private func focusAreaChip(_ area: String) -> some View {
        Button {
            focusAreas.removeAll { $0 == area }
        } label: {
            HStack(spacing: ShellTheme.Spacing.xSmall) {
                Image(uiImage: Lucide.tag.withRenderingMode(.alwaysTemplate))
                    .font(.caption.weight(.semibold))

                Text(area)
                    .font(.caption.weight(.medium))

                Image(uiImage: Lucide.x.withRenderingMode(.alwaysTemplate))
                    .font(.caption.weight(.semibold))
            }
            .foregroundStyle(ShellTheme.Palette.accent)
            .padding(.horizontal, ShellTheme.Spacing.medium)
            .padding(.vertical, ShellTheme.Spacing.small)
            .background(
                Capsule(style: .continuous)
                    .fill(ShellTheme.Palette.accent.opacity(0.12))
            )
            .overlay(
                Capsule(style: .continuous)
                    .strokeBorder(ShellTheme.Palette.accent.opacity(0.2), lineWidth: ShellTheme.Stroke.hairline)
            )
        }
        .buttonStyle(.plain)
    }

    private func addFocusArea() {
        let trimmed = trimmedNewFocusArea
        guard !trimmed.isEmpty else { return }

        let alreadyExists = focusAreas.contains { existing in
            existing.compare(trimmed, options: .caseInsensitive) == .orderedSame
        }
        guard !alreadyExists else {
            newFocusArea = ""
            return
        }

        focusAreas.append(trimmed)
        focusAreas.sort { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
        newFocusArea = ""
    }

    private func loadConfig() {
        guard !didLoad else { return }
        didLoad = true

        let config = normalizedConfig(bridge.getSpaceAIConfig(spaceId: spaceId) ?? SpaceAIConfig())
        initialConfig = config
        apply(config)
    }

    private func saveConfig() {
        guard canSave else { return }

        isSaving = true
        let config = currentConfig
        let didSave = bridge.setSpaceAIConfig(spaceId: spaceId, config: config)
        isSaving = false

        guard didSave else {
            saveErrorMessage = "Maho couldn’t save the AI personality for \(spaceName)."
            showSaveError = true
            return
        }

        initialConfig = config
        dismiss()
    }

    private func apply(_ config: SpaceAIConfig) {
        systemPrompt = config.systemPrompt ?? ""
        focusAreas = normalizeFocusAreas(config.focusAreas)
        memoryEnabled = config.memoryEnabled

        switch config.tone?.trimmingCharacters(in: .whitespacesAndNewlines) {
        case ToneOption.professional.storedValue:
            toneSelection = .professional
            customTone = ""
        case ToneOption.casual.storedValue:
            toneSelection = .casual
            customTone = ""
        case ToneOption.technical.storedValue:
            toneSelection = .technical
            customTone = ""
        case let tone? where !tone.isEmpty:
            toneSelection = .custom
            customTone = tone
        default:
            toneSelection = .default
            customTone = ""
        }

        preferredModel = ModelOption(rawValue: config.preferredModel ?? "") ?? .default
    }

    private func normalizedConfig(_ config: SpaceAIConfig) -> SpaceAIConfig {
        SpaceAIConfig(
            systemPrompt: config.systemPrompt?.trimmingCharacters(in: .whitespacesAndNewlines).nilIfEmpty,
            tone: config.tone?.trimmingCharacters(in: .whitespacesAndNewlines).nilIfEmpty,
            focusAreas: normalizeFocusAreas(config.focusAreas),
            preferredModel: config.preferredModel?.trimmingCharacters(in: .whitespacesAndNewlines).nilIfEmpty,
            memoryEnabled: config.memoryEnabled
        )
    }

    private func normalizeFocusAreas(_ areas: [String]) -> [String] {
        var seen = Set<String>()

        return areas
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }
            .filter { area in
                let key = area.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
                return seen.insert(key).inserted
            }
            .sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
    }

    private var trimmedNewFocusArea: String {
        newFocusArea.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private var resolvedTone: String? {
        switch toneSelection {
        case .default:
            return nil
        case .professional, .casual, .technical:
            return toneSelection.storedValue
        case .custom:
            return customTone.trimmingCharacters(in: .whitespacesAndNewlines).nilIfEmpty
        }
    }

    private var currentConfig: SpaceAIConfig {
        normalizedConfig(
            SpaceAIConfig(
                systemPrompt: systemPrompt.trimmingCharacters(in: .whitespacesAndNewlines).nilIfEmpty,
                tone: resolvedTone,
                focusAreas: focusAreas,
                preferredModel: preferredModel.storedValue,
                memoryEnabled: memoryEnabled
            )
        )
    }

    private var canSave: Bool {
        !isSaving && isCustomToneValid && currentConfig != initialConfig
    }

    private var isCustomToneValid: Bool {
        toneSelection != .custom || !customTone.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }
}

private extension String {
    var nilIfEmpty: String? {
        isEmpty ? nil : self
    }
}
