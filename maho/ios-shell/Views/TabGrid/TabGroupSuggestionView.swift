import Foundation
import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

private struct SuggestedTabCluster: Identifiable {
    struct Preview: Identifiable {
        let id: TabId
        let title: String
        let host: String
        let imageData: Data?
    }

    let id: String
    let name: String
    let tabs: [TabViewModel]
    let previews: [Preview]
}

struct TabGroupSuggestionView: View {
    let spaceId: SpaceId
    let tabs: [TabViewModel]
    let spaceName: String
    let onFoldersCreated: () -> Void

    @Environment(\.dismiss) private var dismiss

    @State private var suggestions: [SuggestedTabCluster] = []
    @State private var isLoading = true
    @State private var creatingClusterId: String?
    @State private var errorMessage: String?

    private let bridge = MahoBridge.shared

    var body: some View {
        NavigationStack {
            Group {
                if isLoading {
                    loadingState
                } else if suggestions.isEmpty {
                    emptyState
                } else {
                    suggestionList
                }
            }
            .navigationTitle("Suggest Groups")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                SwiftUI.ToolbarItem(placement: .cancellationAction) {
                    Button("Not now") { dismiss() }
                }
            }
            .background(backgroundGradient)
        }
        .onAppear(perform: loadSuggestions)
    }

    private var suggestionList: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: ShellTheme.Spacing.mediumLarge) {
                suggestionBanner

                if let errorMessage {
                    errorBanner(message: errorMessage)
                }

                ForEach(suggestions) { cluster in
                    clusterCard(cluster)
                }
            }
            .padding(.horizontal, ShellTheme.Spacing.large)
            .padding(.top, ShellTheme.Spacing.large)
            .padding(.bottom, ShellTheme.Spacing.page)
        }
        .background(backgroundGradient)
    }

    private var suggestionBanner: some View {
        HStack(alignment: .center, spacing: ShellTheme.Spacing.medium) {
            ZStack {
                Circle()
                    .fill(ShellTheme.Palette.tabGridActiveFill)
                    .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)

                Image(uiImage: Lucide.sparkles.withRenderingMode(.alwaysTemplate))
                    .font(.body.weight(.semibold))
                    .foregroundStyle(ShellTheme.Palette.accent)
            }

            VStack(alignment: .leading, spacing: ShellTheme.Spacing.xxSmall) {
                Text("AI suggests grouping these tabs")
                    .font(.headline)
                    .foregroundStyle(.primary)

                Text("Review semantic clusters in \(spaceName) and create folders only for the ones you want.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(ShellTheme.Spacing.large)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Materials.tabGridCard)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
    }

    private var loadingState: some View {
        VStack(spacing: ShellTheme.Spacing.mediumLarge) {
            ProgressView()
                .controlSize(.regular)

            Text("Finding related tabs…")
                .font(.headline)
                .foregroundStyle(.primary)

            Text("Maho is clustering tabs in \(spaceName) using their semantic similarity.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(ShellTheme.Spacing.page)
        .background(backgroundGradient)
    }

    private var emptyState: some View {
        VStack(spacing: ShellTheme.Spacing.mediumLarge) {
            suggestionBanner

            VStack(spacing: ShellTheme.Spacing.small) {
                Image(uiImage: Lucide.folder.withRenderingMode(.alwaysTemplate))
                    .font(.title2)
                    .foregroundStyle(.secondary)

                Text("No suggested groups yet")
                    .font(.headline)
                    .foregroundStyle(.primary)

                Text("Open a few related tabs and try again. Single tabs and already-filed tabs are skipped.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(ShellTheme.Spacing.page)
            .frame(maxWidth: .infinity)
            .background(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                    .fill(ShellTheme.Materials.tabGridCard)
            )
            .overlay(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                    .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
            )

            if let errorMessage {
                errorBanner(message: errorMessage)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .padding(ShellTheme.Spacing.large)
        .background(backgroundGradient)
    }

    private var backgroundGradient: some View {
        LinearGradient(
            colors: [
                ShellTheme.Palette.tabGridBackdropTop,
                ShellTheme.Palette.browsingBackground,
                ShellTheme.Palette.tabGridBackdropBottom
            ],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
        .ignoresSafeArea()
    }

    private func clusterCard(_ cluster: SuggestedTabCluster) -> some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.medium) {
            HStack(alignment: .top, spacing: ShellTheme.Spacing.medium) {
                VStack(alignment: .leading, spacing: ShellTheme.Spacing.xxSmall) {
                    Text(cluster.name)
                        .font(.headline)
                        .foregroundStyle(.primary)

                    Text(clusterTabCountLabel(for: cluster))
                        .font(.caption.weight(.medium))
                        .foregroundStyle(.secondary)
                }

                Spacer(minLength: 0)

                Button {
                    createFolder(for: cluster)
                } label: {
                    HStack(spacing: ShellTheme.Spacing.xSmall) {
                        if creatingClusterId == cluster.id {
                            ProgressView()
                                .controlSize(.mini)
                        }

                        Text("Create Folder")
                            .font(.caption.weight(.semibold))
                    }
                    .foregroundStyle(ShellTheme.Palette.accent)
                    .padding(.horizontal, ShellTheme.Spacing.medium)
                    .padding(.vertical, ShellTheme.Spacing.small)
                    .background(
                        Capsule()
                            .fill(ShellTheme.Palette.tabGridActiveFill)
                    )
                }
                .buttonStyle(.plain)
                .disabled(creatingClusterId != nil)
            }

            HStack(spacing: ShellTheme.Spacing.small) {
                ForEach(cluster.previews) { preview in
                    previewTile(preview)
                }
            }

            Text(cluster.previews.map(\.host).joined(separator: " · "))
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .padding(ShellTheme.Spacing.large)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Materials.tabGridCard)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
    }

    private func clusterTabCountLabel(for cluster: SuggestedTabCluster) -> String {
        let noun = cluster.tabs.count == 1 ? "tab" : "tabs"
        return "\(cluster.tabs.count) \(noun)"
    }

    private func previewTile(_ preview: SuggestedTabCluster.Preview) -> some View {
        ZStack(alignment: .bottomLeading) {
            previewBackground(preview)
                .frame(maxWidth: .infinity)
                .frame(height: ShellTheme.Size.tabGroupSuggestionPreviewHeight)
                .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous))

            LinearGradient(
                colors: [Color.clear, Color.black.opacity(0.14), Color.black.opacity(0.28)],
                startPoint: .top,
                endPoint: .bottom
            )
            .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous))

            Text(preview.title)
                .font(.caption2.weight(.semibold))
                .foregroundStyle(Color.white)
                .lineLimit(1)
                .padding(.horizontal, ShellTheme.Spacing.small)
                .padding(.bottom, ShellTheme.Spacing.small)
        }
        .frame(maxWidth: .infinity)
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
    }

    @ViewBuilder
    private func previewBackground(_ preview: SuggestedTabCluster.Preview) -> some View {
#if canImport(UIKit)
        if let imageData = preview.imageData, let image = UIImage(data: imageData) {
            Image(uiImage: image)
                .resizable()
                .aspectRatio(contentMode: .fill)
        } else {
            previewPlaceholder(host: preview.host)
        }
#else
        previewPlaceholder(host: preview.host)
#endif
    }

    private func previewPlaceholder(host: String) -> some View {
        LinearGradient(
            colors: [
                ShellTheme.Palette.tabPreviewFallback,
                ShellTheme.Palette.tabPreviewPlaceholder
            ],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
        .overlay {
            VStack(spacing: ShellTheme.Spacing.small) {
                Image(uiImage: Lucide.globe.withRenderingMode(.alwaysTemplate))
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)

                Text(host)
                    .font(.caption2.weight(.medium))
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .padding(.horizontal, ShellTheme.Spacing.small)
            }
            .padding(ShellTheme.Spacing.medium)
        }
    }

    private func errorBanner(message: String) -> some View {
        Text(message)
            .font(.caption.weight(.medium))
            .foregroundStyle(.primary)
            .padding(.horizontal, ShellTheme.Spacing.medium)
            .padding(.vertical, ShellTheme.Spacing.small)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                    .fill(ShellTheme.Palette.tabGridMetaFill)
            )
            .overlay(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                    .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
            )
    }

    private func loadSuggestions() {
        isLoading = true
        errorMessage = nil

        let groupedTabIds = Set(bridge.getFolderViewModels(spaceId: spaceId).flatMap(\.tabIds))
        let tabLookup = Dictionary(uniqueKeysWithValues: tabs.map { ($0.id, $0) })

        suggestions = bridge.clusterTabs(threshold: 0.75)
            .compactMap { cluster in
                let clusterTabs = cluster.tab_ids.compactMap { tabLookup[$0] }
                    .filter { !groupedTabIds.contains($0.id) }

                guard clusterTabs.count > 1 else {
                    return nil
                }

                return SuggestedTabCluster(
                    id: ([cluster.group_name] + clusterTabs.map(\.id)).joined(separator: ":"),
                    name: suggestedName(from: cluster.group_name, tabs: clusterTabs),
                    tabs: clusterTabs,
                    previews: Array(clusterTabs.prefix(3)).map { tab in
                        SuggestedTabCluster.Preview(
                            id: tab.id,
                            title: displayTitle(for: tab),
                            host: displayHost(for: tab.url),
                            imageData: bridge.getTabPreview(tabId: tab.id)
                        )
                    }
                )
            }
            .sorted { $0.tabs.count > $1.tabs.count }

        isLoading = false
    }

    private func createFolder(for cluster: SuggestedTabCluster) {
        guard creatingClusterId == nil else { return }
        creatingClusterId = cluster.id
        errorMessage = nil

        guard let folder = bridge.createFolderAndReturnModel(name: cluster.name, spaceId: spaceId) else {
            errorMessage = "Maho couldn't create this folder right now. Please try again."
            creatingClusterId = nil
            return
        }

        for tab in cluster.tabs {
            bridge.moveTabToFolder(tabId: tab.id, folderId: folder.id, spaceId: spaceId)
        }

        suggestions.removeAll { $0.id == cluster.id }
        creatingClusterId = nil
        onFoldersCreated()

        if suggestions.isEmpty {
            dismiss()
        }
    }

    private func displayTitle(for tab: TabViewModel) -> String {
        let trimmed = tab.title.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? displayHost(for: tab.url) : trimmed
    }

    private func displayHost(for url: String) -> String {
        URLComponents(string: url)?.host?.replacingOccurrences(of: "www.", with: "") ?? "New tab"
    }

    private func suggestedName(from rawName: String, tabs: [TabViewModel]) -> String {
        let trimmed = rawName.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmed.isEmpty {
            return trimmed
        }

        return tabs.first.map { displayHost(for: $0.url).capitalized } ?? "Suggested Group"
    }
}
