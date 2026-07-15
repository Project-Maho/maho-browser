import SwiftUI

struct SuggestionsListView: View {
    @ObservedObject var viewModel: CommandBarViewModel
    var onSelect: (SuggestionViewModel) -> Void
    var onCancel: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            // Cancel / Utility Top Bar
            HStack {
                Button(action: onCancel) {
                    Text("Cancel")
                        .font(.system(size: 17))
                        .foregroundColor(.blue)
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
                .frame(minHeight: 44)
                
                Spacer()
            }
            .background(Color(uiColor: .systemBackground))
            
            Divider()

            if viewModel.suggestions.isEmpty {
                VStack {
                    Spacer()
                    Text("No Suggestions")
                        .foregroundColor(.secondary)
                        .font(.system(size: 15))
                    Spacer()
                }
                .frame(maxHeight: .infinity)
                .background(Color(uiColor: .systemGroupedBackground))
            } else {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 0) {
                        suggestionRows(primarySuggestions)
                        suggestionSection(title: "History", suggestions: historySuggestions)
                        suggestionSection(title: "Commands", suggestions: actionSuggestions)
                    }
                }
                .background(Color(uiColor: .systemBackground))
            }
        }
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
        viewModel.suggestions.filter { $0.kind == .action }
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
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
            Divider()

            Text(title.uppercased())
                .font(.caption2.weight(.bold))
                .foregroundStyle(.secondary)
                .tracking(0.8)
                .padding(.horizontal, ShellTheme.Spacing.large)
        }
        .padding(.top, ShellTheme.Spacing.medium)
        .padding(.bottom, ShellTheme.Spacing.xSmall)
    }

    @ViewBuilder
    private func suggestionRows(_ suggestions: [SuggestionViewModel]) -> some View {
        ForEach(Array(suggestions.enumerated()), id: \.element.id) { index, suggestion in
            Button(action: { onSelect(suggestion) }) {
                SuggestionRow(suggestion: suggestion)
            }
            .buttonStyle(SuggestionRowButtonStyle())

            if index < suggestions.count - 1 {
                Divider()
                    .padding(.leading, 48)
            }
        }
    }
}

struct SuggestionRow: View {
    let suggestion: SuggestionViewModel

    var body: some View {
        HStack(spacing: 12) {
            // Icon
            ZStack {
                if let icon = suggestion.icon, let uiImage = icon.uiImage {
                    Image(uiImage: uiImage)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(width: 20, height: 20)
                } else {
                    Image(systemName: sfSymbol(for: suggestion.kind))
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(width: 16, height: 16)
                        .foregroundColor(suggestion.kind == .aiAnswer ? .purple : .secondary)
                }
            }
            .frame(width: 32, height: 32)
            .background(suggestion.kind == .aiAnswer ? Color.purple.opacity(0.15) : Color(uiColor: .secondarySystemBackground))
            .cornerRadius(6)

            // Text
            VStack(alignment: .leading, spacing: 2) {
                Text(highlightedText(suggestion.title, ranges: suggestion.matchRanges))
                    .font(.system(size: 16, weight: suggestion.kind == .aiAnswer ? .semibold : .regular))
                    .foregroundColor(suggestion.kind == .aiAnswer ? .purple : .primary)
                    .lineLimit(1)
                
                if let subtitle = suggestion.subtitle, !subtitle.isEmpty {
                    Text(subtitle)
                        .font(.system(size: 13))
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                }
            }
            
            Spacer()
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
        .contentShape(Rectangle())
    }

    private func sfSymbol(for kind: SuggestionType) -> String {
        switch kind {
        case .tab: return "square.on.square"
        case .bookmark: return "bookmark.fill"
        case .history: return "clock"
        case .action: return "sparkles"
        case .navigation: return "globe"
        case .search: return "magnifyingglass"
        case .folder: return "folder"
        case .calculator: return "plus.slash.minus"
        case .unitConversion: return "arrow.left.and.right"
        case .archivedTab: return "archivebox"
        case .closedTab: return "xmark.circle"
        case .aiAnswer: return "sparkles"
        }
    }

    private func highlightedText(_ text: String, ranges: [[Int]]?) -> AttributedString {
        var attr = AttributedString(text)
        guard let ranges = ranges else { return attr }
        let nsString = text as NSString
        for range in ranges {
            guard range.count == 2 else { continue }
            let start = range[0]
            let length = range[1]
            guard start >= 0, start + length <= nsString.length else { continue }
            let nsRange = NSRange(location: start, length: length)
            if let rangeInAttr = Range(nsRange, in: attr) {
                attr[rangeInAttr].inlinePresentationIntent = .stronglyEmphasized
                attr[rangeInAttr].foregroundColor = .blue
            }
        }
        return attr
    }
}

struct SuggestionRowButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? Color(uiColor: .secondarySystemBackground) : Color.clear)
    }
}
