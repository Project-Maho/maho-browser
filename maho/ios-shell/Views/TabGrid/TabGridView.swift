import Foundation
import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct TabGridView: View {
    let tabs: [TabViewModel]
    let activeTabId: TabId?
    let onSelectTab: (TabId) -> Void
    let onCloseTab: (TabId) -> Void

    private let bridge = MahoBridge.shared

    var body: some View {
        Group {
            if tabs.isEmpty {
                emptyState
            } else {
                deckLayout
            }
        }
    }

    private var deckLayout: some View {
        ScrollView {
            LazyVStack(spacing: ShellTheme.Spacing.mediumLarge) {
                ForEach(Array(tabs.indices), id: \.self) { index in
                    deckCard(for: tabs[index], index: index)
                }
            }
            .padding(.horizontal, ShellTheme.Spacing.large)
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
                        .foregroundStyle(ShellTheme.Palette.accent)
                }
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                )

            VStack(spacing: ShellTheme.Spacing.small) {
                Text("No tabs in this space")
                    .font(.headline)
                    .foregroundStyle(.primary)

                Text("Open a tab and it will appear here.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(ShellTheme.Spacing.page)
        .background(tabGridBackdrop)
        .accessibilityIdentifier("tabGridEmptyState")
    }

    private var tabGridBackdrop: some View {
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

    private func deckCard(for tab: TabViewModel, index: Int) -> some View {
        TabCardView(
            tab: tab,
            isActive: tab.id == activeTabId,
            previewImage: bridge.getTabPreview(tabId: tab.id),
            deckIndex: index,
            onTap: { onSelectTab(tab.id) },
            onClose: { onCloseTab(tab.id) }
        )
        .padding(.horizontal, ShellTheme.Spacing.xSmall)
        .offset(x: index.isMultiple(of: 2) ? -ShellTheme.Spacing.small : ShellTheme.Spacing.small)
    }

    private func displayHost(for url: String) -> String {
        URLComponents(string: url)?.host?.replacingOccurrences(of: "www.", with: "") ?? "New tab"
    }
}
