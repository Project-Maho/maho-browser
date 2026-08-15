import Foundation
import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct TabGridView: View {
    let tabs: [TabViewModel]
    let activeTabId: TabId?
    let activeSpaceId: SpaceId?
    let isIncognito: Bool
    let onSelectTab: (TabId) -> Void
    let onCloseTab: (TabId) -> Void

    private let bridge = MahoBridge.shared

    private var sectionHeaderColor: Color {
        isIncognito ? ShellTheme.Palette.incognitoMutedForeground : .secondary
    }

    private var primaryTextColor: Color {
        isIncognito ? ShellTheme.Palette.incognitoForeground : .primary
    }

    private var gridColumns: [GridItem] {
        [GridItem(.flexible(), spacing: ShellTheme.Spacing.medium),
         GridItem(.flexible(), spacing: ShellTheme.Spacing.medium)]
    }

    var body: some View {
        let favorites = favoriteTabs

        return Group {
            if tabs.isEmpty && favorites.isEmpty {
                emptyState
            } else {
                gridLayout(favorites: favorites)
            }
        }
    }

    private func gridLayout(favorites: [TabViewModel]) -> some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: ShellTheme.Spacing.large) {
                let pinned = tabs.filter { $0.isPinned && !$0.isFavorite }
                let today = tabs.filter { !$0.isPinned && !$0.isFavorite }

                if !favorites.isEmpty {
                    VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
                        Text("Favorites")
                            .font(.headline)
                            .foregroundStyle(sectionHeaderColor)
                            .padding(.horizontal, ShellTheme.Spacing.large)
                        
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(spacing: ShellTheme.Spacing.medium) {
                                ForEach(favorites) { tab in
                                    favoriteTile(for: tab)
                                }
                            }
                            .padding(.horizontal, ShellTheme.Spacing.large)
                        }
                    }
                }

                if !pinned.isEmpty {
                    VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
                        Text("Pinned")
                            .font(.headline)
                            .foregroundStyle(sectionHeaderColor)
                            .padding(.horizontal, ShellTheme.Spacing.large)
                        
                        LazyVGrid(columns: gridColumns, spacing: ShellTheme.Spacing.medium) {
                            ForEach(pinned) { tab in
                                gridCard(for: tab)
                            }
                        }
                        .padding(.horizontal, ShellTheme.Spacing.large)
                    }
                }

                if !today.isEmpty {
                    VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
                        Text("Today")
                            .font(.headline)
                            .foregroundStyle(sectionHeaderColor)
                            .padding(.horizontal, ShellTheme.Spacing.large)

                        LazyVGrid(columns: gridColumns, spacing: ShellTheme.Spacing.medium) {
                            ForEach(today) { tab in
                                gridCard(for: tab)
                            }
                        }
                        .padding(.horizontal, ShellTheme.Spacing.large)
                    }
                }
            }
            .padding(.top, ShellTheme.Spacing.large)
            .padding(.bottom, ShellTheme.Size.homeBottomSpacer)
        }
        .background(tabGridBackdrop)
        .accessibilityIdentifier("tabGridView")
    }

    private var emptyState: some View {
        VStack(spacing: ShellTheme.Spacing.mediumLarge) {
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Materials.tabGridCard)
                .frame(width: ShellTheme.Size.tabGridEmptyDeckFrontWidth, height: ShellTheme.Size.tabGridEmptyDeckFrontHeight)
                .overlay {
                    Image(lucide: Lucide.sparkles)
                        .font(.system(size: 30, weight: .semibold))
                        .foregroundStyle(isIncognito ? ShellTheme.Palette.incognitoMutedForeground : ShellTheme.Palette.accent)
                }
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        .strokeBorder(isIncognito ? ShellTheme.Palette.incognitoBorder : ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                )

            VStack(spacing: ShellTheme.Spacing.small) {
                Text("No tabs in this space")
                    .font(.headline)
                    .foregroundStyle(primaryTextColor)

                Text("Open a tab and it will appear here.")
                    .font(.subheadline)
                    .foregroundStyle(sectionHeaderColor)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(ShellTheme.Spacing.page)
        .background(tabGridBackdrop)
        .accessibilityIdentifier("tabGridEmptyState")
    }

    @ViewBuilder
    private var tabGridBackdrop: some View {
        if isIncognito {
            LinearGradient(
                colors: [
                    ShellTheme.Palette.incognitoBackground,
                    ShellTheme.Palette.incognitoTint,
                    ShellTheme.Palette.incognitoBackground
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()
        } else {
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
    }

    private func gridCard(for tab: TabViewModel) -> some View {
        TabCardView(
            tab: tab,
            isActive: tab.id == activeTabId,
            isIncognito: isIncognito,
            previewImage: bridge.getTabPreview(tabId: tab.id),
            deckIndex: 0,
            onTap: { onSelectTab(tab.id) },
            onClose: { onCloseTab(tab.id) }
        )
    }

    private var favoriteTabs: [TabViewModel] {
        guard let activeSpaceId else { return [] }
        return bridge.getFavoriteTabs(spaceId: activeSpaceId)
            .filter { $0.isPrivate == isIncognito }
    }

    private func favoriteTile(for tab: TabViewModel) -> some View {
        Button {
            onSelectTab(tab.id)
        } label: {
            let host = displayHost(for: tab.url)
            ZStack {
                if let favicon = tab.favicon, let uiImage = favicon.uiImage {
                    Image(uiImage: uiImage)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(width: 24, height: 24)
                        .cornerRadius(4)
                } else {
                    let monogramLetter = DomainMonogram.letter(for: host)
                    let monogramColor = DomainMonogram.color(for: host)

                    RoundedRectangle(cornerRadius: 6, style: .continuous)
                        .fill(monogramColor)
                        .frame(width: 36, height: 36)

                    Text(String(monogramLetter))
                        .font(.system(size: 13, weight: .bold))
                        .foregroundColor(.white)
                }
            }
            .frame(width: 36, height: 36)
            .background((isIncognito ? ShellTheme.Palette.incognitoSurface : ShellTheme.Palette.tabCardSurface.opacity(0.85)), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .strokeBorder(tab.id == activeTabId ? ShellTheme.Palette.accent : (isIncognito ? ShellTheme.Palette.incognitoBorder : ShellTheme.Palette.overlayBorder), lineWidth: tab.id == activeTabId ? 2 : ShellTheme.Stroke.hairline)
            )
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("favoriteDock_\(tab.id)")
        .accessibilityLabel(tab.title.isEmpty ? displayHost(for: tab.url) : tab.title)
        .accessibilityHint("Opens this favorite tab.")
    }

    private func displayHost(for url: String) -> String {
        guard let urlObj = URL(string: url), let host = urlObj.host else { return "" }
        return host
    }
}
