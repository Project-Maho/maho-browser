import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct SearchSheet: View {
    @Binding var query: String
    @ObservedObject var viewModel: CommandBarViewModel

    let isIncognito: Bool
    let onSubmit: (String) -> Void
    let onSelectSuggestion: (SuggestionViewModel) -> Void
    let onBrowseForMe: (String) -> Void
    let onTogglePrivate: () -> Void
    let onCancel: () -> Void
    let voiceManager: VoiceSearchManager
    let onVoiceAssistantRequested: () -> Void

    @FocusState private var isFieldFocused: Bool
    @Environment(\.colorScheme) private var systemColorScheme

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .bottom) {
                ShellTheme.Palette.searchOverlayBackdrop
                    .ignoresSafeArea()
                    .onTapGesture(perform: onCancel)

                panel(height: min(geometry.size.height * 0.5, 460))
                    .frame(maxWidth: .infinity)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
        }
        .onAppear(perform: focusSearchField)
        .onChange(of: voiceManager.state) { _, newState in
            handleVoiceStateChange(newState)
        }
        .accessibilityIdentifier("searchSheet")
        .environment(\.colorScheme, isIncognito ? .dark : systemColorScheme)
    }

    private var panelShape: UnevenRoundedRectangle {
        UnevenRoundedRectangle(
            topLeadingRadius: ShellTheme.Radius.searchOverlayPanel,
            bottomLeadingRadius: 0,
            bottomTrailingRadius: 0,
            topTrailingRadius: ShellTheme.Radius.searchOverlayPanel,
            style: .continuous
        )
    }

    private func panel(height: CGFloat) -> some View {
        VStack(spacing: ShellTheme.Spacing.medium) {
            searchField
            content
        }
        .padding(.horizontal, ShellTheme.Spacing.large)
        .padding(.top, ShellTheme.Spacing.large)
        .padding(.bottom, ShellTheme.Spacing.page)
        .frame(maxWidth: ShellTheme.Size.searchOverlayMaxWidth)
        .frame(height: height, alignment: .top)
        .background(panelBackground)
        .clipShape(panelShape)
        .overlay(
            panelShape
                .strokeBorder(ShellTheme.Palette.searchOverlayBorder(isIncognito: isIncognito), lineWidth: ShellTheme.Stroke.hairline)
        )
        .contentShape(panelShape)
        .onTapGesture {}
    }

    private var panelBackground: some View {
        LinearGradient(
            colors: [
                ShellTheme.Palette.searchOverlayElevatedCanvas(isIncognito: isIncognito),
                ShellTheme.Palette.searchOverlayCanvas(isIncognito: isIncognito)
            ],
            startPoint: .top,
            endPoint: .bottom
        )
    }

    private var searchField: some View {
        HStack(spacing: ShellTheme.Spacing.small) {
            ZStack(alignment: .leading) {
                if query.isEmpty {
                    Text("Search…")
                        .font(.title3.weight(.medium))
                        .foregroundStyle(ShellTheme.Palette.searchOverlayMutedForeground(isIncognito: isIncognito))
                        .allowsHitTesting(false)
                }

                TextField("", text: $query)
                    .textFieldStyle(.plain)
                    .font(.title3.weight(.medium))
                    .foregroundStyle(ShellTheme.Palette.searchOverlayForeground(isIncognito: isIncognito))
                    .tint(ShellTheme.Palette.searchOverlayAccent(isIncognito: isIncognito))
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                    .keyboardType(.webSearch)
                    .submitLabel(.search)
                    .focused($isFieldFocused)
                    .onSubmit(submitQuery)
                    .layoutPriority(1)
                    .accessibilityIdentifier("searchSheetTextField")
            }

            trailingFieldControls
        }
        .padding(.leading, ShellTheme.Spacing.large)
        .padding(.trailing, ShellTheme.Spacing.medium)
        .frame(height: ShellTheme.Size.searchOverlayFieldHeight)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.searchField, style: .continuous)
                .fill(ShellTheme.Palette.searchOverlayFieldFill(isIncognito: isIncognito))
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.searchField, style: .continuous)
                .strokeBorder(ShellTheme.Palette.searchOverlayBorder(isIncognito: isIncognito), lineWidth: ShellTheme.Stroke.hairline)
        )
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("searchSheetField")
    }

    @ViewBuilder
    private var trailingFieldControls: some View {
        if isIncognito {
            Button(action: onTogglePrivate) {
                HStack(spacing: ShellTheme.Spacing.xSmall) {
                    Text("Incognito")
                        .font(.subheadline.weight(.semibold))
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)

                    Image(systemName: "eyes")
                        .font(.subheadline.weight(.semibold))
                }
                .fixedSize(horizontal: true, vertical: false)
                .foregroundStyle(ShellTheme.Palette.searchOverlayMutedForeground(isIncognito: isIncognito))
                .padding(.leading, ShellTheme.Spacing.small)
                .frame(height: ShellTheme.Size.searchOverlayIconButton)
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier("searchSheetPrivateToggle")
            .accessibilityLabel("Exit Incognito")
        } else {
            fieldIconButton(
                icon: voiceIcon,
                accessibilityIdentifier: "searchSheetMicButton",
                accessibilityLabel: voiceAccessibilityLabel,
                action: handleMicTap
            )

            fieldIconButton(
                systemName: "eyes",
                accessibilityIdentifier: "searchSheetPrivateToggle",
                accessibilityLabel: "Enter Incognito",
                action: onTogglePrivate
            )
        }
    }

    private func fieldIconButton(
        icon: UIImage,
        accessibilityIdentifier: String,
        accessibilityLabel: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Image(lucide: icon)
                .font(.footnote.weight(.semibold))
                .foregroundStyle(fieldIconColor)
                .frame(width: ShellTheme.Size.searchOverlayIconButton, height: ShellTheme.Size.searchOverlayIconButton)
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(accessibilityIdentifier)
        .accessibilityLabel(accessibilityLabel)
    }

    private func fieldIconButton(
        systemName: String,
        accessibilityIdentifier: String,
        accessibilityLabel: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.footnote.weight(.semibold))
                .foregroundStyle(fieldIconColor)
                .frame(width: ShellTheme.Size.searchOverlayIconButton, height: ShellTheme.Size.searchOverlayIconButton)
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(accessibilityIdentifier)
        .accessibilityLabel(accessibilityLabel)
    }

    @ViewBuilder
    private var content: some View {
        if isIncognito && trimmedQuery.isEmpty {
            incognitoEmptyState
        } else if hasVisibleSuggestions {
            suggestionsList
        } else {
            emptySuggestionsState
        }
    }

    private var suggestionsList: some View {
        ScrollView(showsIndicators: false) {
            LazyVStack(alignment: .leading, spacing: 0) {
                suggestionRows(primarySuggestions)
                suggestionSection(title: "History", suggestions: historySuggestions)
                suggestionSection(title: "Commands", suggestions: actionSuggestions)
            }
            .padding(.top, ShellTheme.Spacing.xSmall)
        }
        .accessibilityIdentifier("searchSheetSuggestions")
    }

    private var incognitoEmptyState: some View {
        VStack(spacing: ShellTheme.Spacing.medium) {
            Spacer(minLength: ShellTheme.Spacing.hero)

            Text("You're browsing Incognito")
                .font(.headline.weight(.semibold))
                .foregroundStyle(ShellTheme.Palette.searchOverlayMutedForeground(isIncognito: isIncognito))
                .multilineTextAlignment(.center)

            Spacer(minLength: ShellTheme.Spacing.hero)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityIdentifier("searchSheetIncognitoEmptyState")
    }

    private var emptySuggestionsState: some View {
        VStack(spacing: ShellTheme.Spacing.small) {
            Spacer(minLength: ShellTheme.Spacing.hero)

            Text(trimmedQuery.isEmpty ? "Search or enter a website" : "Press Search to go")
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(ShellTheme.Palette.searchOverlayMutedForeground(isIncognito: isIncognito))
                .multilineTextAlignment(.center)

            Spacer(minLength: ShellTheme.Spacing.hero)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityIdentifier("searchSheetEmptyState")
    }

    @ViewBuilder
    private func suggestionSection(title: String, suggestions: [SuggestionViewModel]) -> some View {
        if !suggestions.isEmpty {
            VStack(alignment: .leading, spacing: 0) {
                sectionHeader(title)
                suggestionRows(suggestions)
            }
        }
    }

    private func sectionHeader(_ title: String) -> some View {
        Text(title.uppercased())
            .font(.caption2.weight(.bold))
            .foregroundStyle(ShellTheme.Palette.searchOverlaySecondaryForeground(isIncognito: isIncognito))
            .tracking(0.8)
            .padding(.horizontal, ShellTheme.Spacing.medium)
            .padding(.top, ShellTheme.Spacing.medium)
            .padding(.bottom, ShellTheme.Spacing.xSmall)
    }

    @ViewBuilder
    private func suggestionRows(_ suggestions: [SuggestionViewModel]) -> some View {
        ForEach(Array(suggestions.enumerated()), id: \.element.id) { index, suggestion in
            SearchSuggestionRow(
                suggestion: suggestion,
                query: trimmedQuery,
                isIncognito: isIncognito,
                onSelect: { onSelectSuggestion(suggestion) },
                onBrowseForMe: { onBrowseForMe(browseQuery(for: suggestion)) }
            )

            if index < suggestions.count - 1 {
                rowDivider
            }
        }
    }

    private var rowDivider: some View {
        Rectangle()
            .fill(ShellTheme.Palette.searchOverlayBorder(isIncognito: isIncognito))
            .frame(height: ShellTheme.Stroke.hairline)
            .padding(.leading, ShellTheme.Size.searchOverlayLeadingIcon + ShellTheme.Spacing.large + ShellTheme.Spacing.medium)
    }

    private var primarySuggestions: [SuggestionViewModel] {
        viewModel.suggestions.filter { suggestion in
            switch suggestion.kind {
            case .action, .history:
                return false
            default:
                return true
            }
        }
    }

    private var historySuggestions: [SuggestionViewModel] {
        viewModel.suggestions.filter { $0.kind == .history }
    }

    private var actionSuggestions: [SuggestionViewModel] {
        guard !trimmedQuery.isEmpty else { return [] }
        return viewModel.suggestions.filter { $0.kind == .action }
    }

    private var hasVisibleSuggestions: Bool {
        !primarySuggestions.isEmpty || !historySuggestions.isEmpty || !actionSuggestions.isEmpty
    }

    private var trimmedQuery: String {
        query.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private var voiceIcon: UIImage {
        switch voiceManager.state {
        case .listening:
            return Lucide.mic
        case .processing:
            return Lucide.ellipsis
        case .error:
            return Lucide.micOff
        default:
            return Lucide.mic
        }
    }

    private var fieldIconColor: Color {
        if voiceManager.state == .listening {
            return ShellTheme.Palette.searchOverlayAccent(isIncognito: isIncognito)
        }

        return ShellTheme.Palette.searchOverlayMutedForeground(isIncognito: isIncognito)
    }

    private var voiceAccessibilityLabel: String {
        switch voiceManager.state {
        case .listening:
            return "Stop voice search"
        case .processing:
            return "Processing speech"
        default:
            return "Voice search"
        }
    }

    private func focusSearchField() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.08) {
            isFieldFocused = true
        }
    }

    private func submitQuery() {
        guard !trimmedQuery.isEmpty else { return }
        onSubmit(trimmedQuery)
    }

    private func browseQuery(for suggestion: SuggestionViewModel) -> String {
        if suggestion.key.hasPrefix("ai_search:") {
            return String(suggestion.key.dropFirst("ai_search:".count))
        }

        if !trimmedQuery.isEmpty {
            return trimmedQuery
        }

        return suggestion.executionPayload ?? suggestion.title
    }

    private func handleMicTap() {
        guard voiceManager.state != .processing else { return }
        onVoiceAssistantRequested()
    }

    private func handleVoiceStateChange(_ newState: VoiceSearchState) {
        guard case .result(let text) = newState else { return }
        query = text
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            voiceManager.reset()
            isFieldFocused = true
        }
    }
}

private struct SearchSuggestionRow: View {
    let suggestion: SuggestionViewModel
    let query: String
    let isIncognito: Bool
    let onSelect: () -> Void
    let onBrowseForMe: () -> Void

    var body: some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            Button(action: onSelect) {
                HStack(spacing: ShellTheme.Spacing.medium) {
                    leadingIcon
                    titleStack
                    Spacer(minLength: 0)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(SearchOverlayRowButtonStyle(isIncognito: isIncognito))

            if showsBrowseForMe {
                Button(action: onBrowseForMe) {
                    Text("Browse for Me")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(ShellTheme.Palette.searchOverlayBrowsePillForeground)
                        .lineLimit(1)
                        .minimumScaleFactor(0.85)
                        .padding(.horizontal, ShellTheme.Spacing.medium)
                        .frame(height: ShellTheme.Size.searchOverlayBrowsePillHeight)
                        .background(
                            Capsule()
                                .fill(ShellTheme.Palette.searchOverlayBrowsePillFill)
                        )
                        .overlay(
                            Capsule()
                                .strokeBorder(ShellTheme.Palette.searchOverlayBorder(isIncognito: isIncognito), lineWidth: ShellTheme.Stroke.hairline)
                        )
                }
                .buttonStyle(.plain)
                .accessibilityIdentifier("searchSheetBrowseForMeButton")
                .accessibilityLabel("Browse for Me")
            }
        }
        .padding(.vertical, ShellTheme.Spacing.xSmall)
        .padding(.trailing, ShellTheme.Spacing.small)
        .accessibilityElement(children: .contain)
    }

    private var leadingIcon: some View {
        ZStack {
            RoundedRectangle(cornerRadius: ShellTheme.Radius.searchOverlayIcon, style: .continuous)
                .fill(ShellTheme.Palette.searchOverlayIconFill(isIncognito: isIncognito))
                .frame(width: ShellTheme.Size.searchOverlayLeadingIcon, height: ShellTheme.Size.searchOverlayLeadingIcon)

            if let icon = suggestion.icon, let uiImage = icon.uiImage {
                Image(uiImage: uiImage)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .frame(width: ShellTheme.Size.searchOverlayLeadingIcon - ShellTheme.Spacing.medium, height: ShellTheme.Size.searchOverlayLeadingIcon - ShellTheme.Spacing.medium)
                    .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.searchOverlayIcon, style: .continuous))
            } else {
                Image(lucide: fallbackIcon)
                    .font(.footnote.weight(.semibold))
                    .foregroundStyle(iconColor)
            }
        }
        .accessibilityHidden(true)
    }

    private var titleStack: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.badgeVertical) {
            Text(highlightedText(suggestion.title, ranges: suggestion.matchRanges))
                .font(.subheadline.weight(.regular))
                .foregroundStyle(titleColor)
                .lineLimit(1)
                .truncationMode(.tail)

            if let subtitle = suggestion.subtitle, !subtitle.isEmpty {
                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(ShellTheme.Palette.searchOverlaySecondaryForeground(isIncognito: isIncognito))
                    .lineLimit(1)
                    .truncationMode(.tail)
            }
        }
    }

    private var showsBrowseForMe: Bool {
        switch suggestion.kind {
        case .search, .aiAnswer, .navigation:
            return true
        case .history:
            return !query.isEmpty
        default:
            return false
        }
    }

    private var titleColor: Color {
        ShellTheme.Palette.searchOverlayForeground(isIncognito: isIncognito)
    }

    private var iconColor: Color {
        ShellTheme.Palette.searchOverlayMutedForeground(isIncognito: isIncognito)
    }

    private var fallbackIcon: UIImage {
        switch suggestion.kind {
        case .tab:
            return Lucide.copy
        case .bookmark:
            return Lucide.bookmark
        case .history:
            return Lucide.clock
        case .action:
            return Lucide.sparkles
        case .navigation, .aiAnswer:
            return Lucide.search
        case .search:
            return Lucide.search
        case .folder:
            return Lucide.folder
        case .archivedTab:
            return Lucide.archive
        case .closedTab:
            return Lucide.circleX
        case .calculator, .unitConversion:
            return Lucide.search
        }
    }

    private func highlightedText(_ text: String, ranges: [[Int]]?) -> AttributedString {
        var attributedText = AttributedString(text)
        guard let ranges else { return attributedText }

        let nsString = text as NSString
        for range in ranges {
            guard range.count == 2 else { continue }
            let start = range[0]
            let length = range[1]
            guard start >= 0, start + length <= nsString.length else { continue }
            let nsRange = NSRange(location: start, length: length)
            if let attributedRange = Range(nsRange, in: attributedText) {
                attributedText[attributedRange].inlinePresentationIntent = .stronglyEmphasized
                attributedText[attributedRange].foregroundColor = ShellTheme.Palette.searchOverlayAccent(isIncognito: isIncognito)
            }
        }

        return attributedText
    }
}

private struct SearchOverlayRowButtonStyle: ButtonStyle {
    let isIncognito: Bool

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(.vertical, ShellTheme.Spacing.small)
            .padding(.leading, ShellTheme.Spacing.small)
            .background(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.searchOverlayRow, style: .continuous)
                    .fill(configuration.isPressed ? ShellTheme.Palette.searchOverlayRowPressed(isIncognito: isIncognito) : Color.clear)
            )
    }
}
