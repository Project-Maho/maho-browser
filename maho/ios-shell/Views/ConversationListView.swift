import SwiftUI
import LucideIcons

@MainActor
struct ConversationListView: View {
    private struct ConversationRoute: Hashable {
        let token = UUID()
        let sessionId: String?
    }

    @Environment(\.dismiss) private var dismiss

    @State private var conversations: [ConversationSession] = []
    @State private var isLoading = false
    @State private var navigationPath: [ConversationRoute] = []
    @State private var hasPerformedInitialNavigation = false
    @State private var conversationPendingRename: ConversationSession?
    @State private var renameDraft = ""
    @State private var conversationPendingDelete: ConversationSession?

    private let pageUrl: String
    private let pageTitle: String
    private let pageText: String
    private let bridge: MahoBridge
    private let captureCurrentPageScreenshot: (@MainActor () async -> Data?)?
    private let autoOpenChatOnAppear: Bool

    init(
        pageUrl: String,
        pageTitle: String,
        pageText: String,
        bridge: MahoBridge = .shared,
        captureCurrentPageScreenshot: (@MainActor () async -> Data?)? = nil,
        autoOpenChatOnAppear: Bool = false
    ) {
        self.pageUrl = pageUrl
        self.pageTitle = pageTitle
        self.pageText = pageText
        self.bridge = bridge
        self.captureCurrentPageScreenshot = captureCurrentPageScreenshot
        self.autoOpenChatOnAppear = autoOpenChatOnAppear
    }

    var body: some View {
        NavigationStack(path: $navigationPath) {
            ZStack {
                backgroundLayer

                Group {
                    if isLoading {
                        ProgressView()
                            .controlSize(.large)
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                    } else {
                        conversationList
                    }
                }
            }
            .navigationTitle("Chats")
            .navigationBarTitleDisplayMode(.large)
            .toolbar {
                ToolbarItemGroup(placement: .cancellationAction) {
                    Button("Done") {
                        dismiss()
                    }
                }
            }
            .navigationDestination(for: ConversationRoute.self) { route in
                ChatView(
                    pageUrl: pageUrl,
                    pageTitle: pageTitle,
                    pageText: pageText,
                    sessionId: route.sessionId,
                    bridge: bridge,
                    captureCurrentPageScreenshot: captureCurrentPageScreenshot,
                    onClose: { dismiss() }
                )
            }
            .alert("Rename Chat", isPresented: renameAlertBinding) {
                TextField("Conversation title", text: $renameDraft)
                Button("Cancel", role: .cancel) {
                    conversationPendingRename = nil
                }
                Button("Save") {
                    commitRename()
                }
            } message: {
                Text("Use a short label you can spot instantly.")
            }
            .confirmationDialog(
                "Delete chat?",
                isPresented: deleteDialogBinding,
                titleVisibility: .visible
            ) {
                Button("Delete", role: .destructive) {
                    commitDelete()
                }
                Button("Cancel", role: .cancel) {
                    conversationPendingDelete = nil
                }
            } message: {
                Text("This permanently removes the conversation and all saved messages.")
            }
            .onAppear {
                reloadConversations()
                performInitialNavigationIfNeeded()
            }
        }
        .accessibilityIdentifier("aiScreenConversationList")
    }

    private var backgroundLayer: some View {
        LinearGradient(
            colors: [
                ShellTheme.Palette.tabGridBackdropTop,
                ShellTheme.Palette.homeBackground,
                ShellTheme.Palette.browsingBackground
            ],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
        .ignoresSafeArea()
    }

    private var conversationList: some View {
        List {
            Section {
                Button {
                    createConversationAndOpen()
                } label: {
                    newChatCard
                }
                .buttonStyle(.plain)
                .listRowInsets(
                    EdgeInsets(
                        top: ShellTheme.Spacing.small,
                        leading: ShellTheme.Spacing.large,
                        bottom: ShellTheme.Spacing.small,
                        trailing: ShellTheme.Spacing.large
                    )
                )
                .listRowBackground(Color.clear)
                .listRowSeparator(.hidden)
            }

            if conversations.isEmpty {
                Section {
                    emptyState
                        .listRowInsets(
                            EdgeInsets(
                                top: ShellTheme.Spacing.large,
                                leading: ShellTheme.Spacing.large,
                                bottom: ShellTheme.Spacing.large,
                                trailing: ShellTheme.Spacing.large
                            )
                        )
                        .listRowBackground(Color.clear)
                        .listRowSeparator(.hidden)
                }
            } else {
                Section("Saved conversations") {
                    ForEach(Array(conversations.enumerated()), id: \.element.id) { _, conversation in
                        NavigationLink(value: ConversationRoute(sessionId: conversation.id)) {
                            ConversationRow(conversation: conversation)
                        }
                        .swipeActions(edge: .trailing, allowsFullSwipe: false) {
                            Button(role: .destructive) {
                                conversationPendingDelete = conversation
                            } label: {
                                Label {
                                    Text("Delete")
                                } icon: {
                                    Image(uiImage: Lucide.trash2.withRenderingMode(.alwaysTemplate))
                                }
                            }

                            Button {
                                renameDraft = conversation.rawTitle
                                conversationPendingRename = conversation
                            } label: {
                                Label {
                                    Text("Rename")
                                } icon: {
                                    Image(uiImage: Lucide.pencilLine.withRenderingMode(.alwaysTemplate))
                                }
                            }
                            .tint(ShellTheme.Palette.summaryAccent)
                        }
                        .listRowInsets(
                            EdgeInsets(
                                top: ShellTheme.Spacing.xSmall,
                                leading: ShellTheme.Spacing.large,
                                bottom: ShellTheme.Spacing.xSmall,
                                trailing: ShellTheme.Spacing.large
                            )
                        )
                        .listRowBackground(Color.clear)
                        .listRowSeparator(.hidden)
                    }
                }
            }
        }
        .listStyle(.plain)
        .scrollContentBackground(.hidden)
        .refreshable {
            reloadConversations()
        }
    }

    private var newChatCard: some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            ZStack {
                RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                    .fill(ShellTheme.Palette.summaryAccentFill)
                    .frame(
                        width: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.medium,
                        height: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.medium
                    )

                Image(uiImage: Lucide.plus.withRenderingMode(.alwaysTemplate))
                    .font(.headline.weight(.semibold))
                    .foregroundStyle(ShellTheme.Palette.summaryAccent)
            }

            VStack(alignment: .leading, spacing: ShellTheme.Spacing.xxSmall) {
                Text("New Chat")
                    .font(.headline)
                    .foregroundStyle(.primary)

                Text("Start a fresh saved conversation for this page.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer(minLength: 0)
        }
        .padding(ShellTheme.Spacing.large)
        .background(cardBackground)
        .overlay(cardBorder)
    }

    private var emptyState: some View {
        VStack(spacing: ShellTheme.Spacing.mediumLarge) {
            Image(uiImage: Lucide.messageCircle.withRenderingMode(.alwaysTemplate))
                .font(.system(size: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.medium, weight: .semibold))
                .foregroundStyle(ShellTheme.Palette.summaryAccent)

            Text("No saved chats yet")
                .font(.title3.weight(.semibold))
                .foregroundStyle(.primary)

            Text("Create a new chat and it’ll show up here with its latest activity.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity)
        .padding(ShellTheme.Spacing.page)
        .background(cardBackground)
        .overlay(cardBorder)
    }

    private var cardBackground: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
            .fill(ShellTheme.Palette.tabGridFloatingSurface)
            .background(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                    .fill(ShellTheme.Materials.summaryOverlay)
            )
    }

    private var cardBorder: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
            .strokeBorder(ShellTheme.Palette.summarySurfaceBorder, lineWidth: ShellTheme.Stroke.hairline)
    }

    private var renameAlertBinding: Binding<Bool> {
        Binding(
            get: { conversationPendingRename != nil },
            set: { isPresented in
                if !isPresented {
                    conversationPendingRename = nil
                }
            }
        )
    }

    private var deleteDialogBinding: Binding<Bool> {
        Binding(
            get: { conversationPendingDelete != nil },
            set: { isPresented in
                if !isPresented {
                    conversationPendingDelete = nil
                }
            }
        )
    }

    private func performInitialNavigationIfNeeded() {
        guard autoOpenChatOnAppear, !hasPerformedInitialNavigation else {
            return
        }

        hasPerformedInitialNavigation = true
        navigationPath.append(ConversationRoute(sessionId: nil))
    }

    private func reloadConversations() {
        isLoading = true
        conversations = bridge.listConversations(limit: 100)
        isLoading = false
    }

    private func createConversationAndOpen() {
        let sessionId = UUID().uuidString
        let created = bridge.createConversation(id: sessionId, title: nil, spaceId: nil, model: nil)

        if created {
            SpotlightManager.shared.indexConversation(id: sessionId, title: "", lastMessageAt: "")
        }
        reloadConversations()
        navigationPath.append(ConversationRoute(sessionId: created ? sessionId : nil))
    }

    private func commitRename() {
        guard let conversation = conversationPendingRename else {
            return
        }

        let updatedTitle = renameDraft.trimmingCharacters(in: .whitespacesAndNewlines)
        _ = bridge.renameConversation(sessionId: conversation.id, title: updatedTitle)
        SpotlightManager.shared.indexConversation(
            id: conversation.id,
            title: updatedTitle,
            lastMessageAt: conversation.last_message_at
        )
        conversationPendingRename = nil
        reloadConversations()
    }

    private func commitDelete() {
        guard let conversation = conversationPendingDelete else {
            return
        }

        _ = bridge.deleteConversation(sessionId: conversation.id)
        SpotlightManager.shared.removeConversation(id: conversation.id)
        conversationPendingDelete = nil
        reloadConversations()
    }
}

private struct ConversationRow: View {
    let conversation: ConversationSession

    var body: some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            ZStack {
                RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                    .fill(ShellTheme.Palette.summaryAccentFill)
                    .frame(
                        width: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.small,
                        height: ShellTheme.Size.summaryStateIcon + ShellTheme.Spacing.small
                    )

                Image(uiImage: Lucide.messageCircle.withRenderingMode(.alwaysTemplate))
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(ShellTheme.Palette.summaryAccent)
            }

            VStack(alignment: .leading, spacing: ShellTheme.Spacing.xxSmall) {
                Text(conversation.displayTitle)
                    .font(.body.weight(.semibold))
                    .foregroundStyle(.primary)
                    .lineLimit(1)

                Text(conversation.metadataLabel)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            Spacer(minLength: 0)
        }
        .padding(.horizontal, ShellTheme.Spacing.medium)
        .padding(.vertical, ShellTheme.Spacing.medium)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Palette.tabGridFloatingSurface)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
    }
}

private extension ConversationSession {
    var rawTitle: String {
        title?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
    }

    var displayTitle: String {
        rawTitle.isEmpty ? "Untitled chat" : rawTitle
    }

    var messageCountLabel: String {
        message_count == 1 ? "1 message" : "\(message_count) messages"
    }

    var metadataLabel: String {
        "\(ConversationTimestampFormatter.label(from: last_message_at)) · \(messageCountLabel)"
    }
}

private enum ConversationTimestampFormatter {
    static let isoWithFractionalSeconds: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter
    }()

    static let iso8601: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime]
        return formatter
    }()

    static let sqlite: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = TimeZone(secondsFromGMT: 0)
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return formatter
    }()

    static func label(from rawValue: String) -> String {
        guard let date = date(from: rawValue) else {
            return rawValue
        }

        let calendar = Calendar.current
        if calendar.isDateInToday(date) {
            return date.formatted(.dateTime.hour().minute())
        }

        if calendar.isDateInYesterday(date) {
            return "Yesterday"
        }

        return date.formatted(.dateTime.month(.abbreviated).day().hour().minute())
    }

    private static func date(from rawValue: String) -> Date? {
        isoWithFractionalSeconds.date(from: rawValue)
            ?? iso8601.date(from: rawValue)
            ?? sqlite.date(from: rawValue)
    }
}
