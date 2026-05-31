import SwiftUI
import LucideIcons

struct HomeSearchView: View {
    let activeSpaceName: String
    let activeSpaceTabCount: Int
    let recentTabs: [TabViewModel]
    let topSites: [TabViewModel]
    let showsUnderlyingPage: Bool
    let onSelectSite: (String) -> Void
    let onResumeTab: (TabId) -> Void

    private let columns = [
        GridItem(.flexible(), spacing: ShellTheme.Spacing.mediumLarge),
        GridItem(.flexible(), spacing: ShellTheme.Spacing.mediumLarge)
    ]

    var body: some View {
        GeometryReader { proxy in
            ScrollView(showsIndicators: false) {
                VStack(spacing: ShellTheme.Spacing.large) {
                    quietCanvas(minHeight: proxy.size.height)

                    if hasCollections {
                        homeCollections
                    }
                }
                .frame(maxWidth: ShellTheme.Size.homeContentMaxWidth)
                .frame(
                    minHeight: max(proxy.size.height - ShellTheme.Size.bottomInsetWithBar, 0),
                    alignment: .bottom
                )
                .frame(maxWidth: .infinity, alignment: .bottom)
                .padding(.horizontal, ShellTheme.Spacing.page)
                .padding(.top, ShellTheme.Spacing.xLarge)
                .padding(.bottom, ShellTheme.Size.bottomInsetWithBar + ShellTheme.Size.bottomBarMinHeight + ShellTheme.Spacing.small)
            }
            .background(homeBackground)
        }
        .accessibilityIdentifier("homeSearchView")
    }

    private var hasCollections: Bool {
        !recentTabs.isEmpty || !topSites.isEmpty
    }

    private func quietCanvas(minHeight: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
            Spacer(minLength: max(minHeight - ShellTheme.Size.homeAnchorThreshold, ShellTheme.Spacing.hero * 2))
        }
        .frame(maxWidth: .infinity, minHeight: minHeight, alignment: .bottomLeading)
    }

    private var homeBackground: some View {
        Group {
            if showsUnderlyingPage {
                ShellTheme.Palette.homeSurfaceFill(showsUnderlyingPage: true)
            } else {
                Color.clear
            }
        }
        .ignoresSafeArea()
    }

    @ViewBuilder
    private var homeCollections: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.section) {
            if !recentTabs.isEmpty {
                recentTabsSection
            }

            if !topSites.isEmpty {
                topSitesGrid
            }
        }
    }

    private var topSitesGrid: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.mediumLarge) {
            sectionHeader(title: "Top Sites")

            LazyVGrid(columns: columns, spacing: ShellTheme.Spacing.mediumLarge) {
                ForEach(Array(topSites.indices), id: \.self) { index in
                    let site = topSites[index]
                    Button {
                        onSelectSite(site.url)
                    } label: {
                        VStack(alignment: .leading, spacing: 8) {
                            Image(lucide: site.isFavorite ? Lucide.star : Lucide.globe)
                                .font(.headline)
                                .foregroundStyle(ShellTheme.Palette.accent)
                                .frame(width: ShellTheme.Size.topSiteIcon, height: ShellTheme.Size.topSiteIcon)
                                .background(
                                    ShellTheme.Palette.topSiteIconFill,
                                    in: RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                                )

                            Text(site.title.isEmpty ? displayTitle(for: site.url) : site.title)
                                .font(.subheadline.weight(.semibold))
                                .foregroundStyle(.primary)
                                .lineLimit(1)

                            Text(displayHost(for: site.url))
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(ShellTheme.Spacing.mediumLarge)
                        .background(
                            ShellTheme.Palette.topSiteCardFill,
                            in: RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                        )
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("homeTopSite_\(index)")
                }
            }
        }
    }

    private var recentTabsSection: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.mediumLarge) {
            sectionHeader(title: "Continue")

            VStack(spacing: ShellTheme.Spacing.medium) {
                ForEach(Array(recentTabs.indices), id: \.self) { index in
                    let tab = recentTabs[index]
                    Button {
                        onResumeTab(tab.id)
                    } label: {
                        HStack(spacing: ShellTheme.Spacing.medium) {
                            Image(lucide: tab.isFavorite ? Lucide.star : Lucide.history)
                                .font(.headline)
                                .foregroundStyle(ShellTheme.Palette.accent)
                                .frame(width: ShellTheme.Size.topSiteIcon, height: ShellTheme.Size.topSiteIcon)
                                .background(
                                    ShellTheme.Palette.topSiteIconFill,
                                    in: RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                                )

                            VStack(alignment: .leading, spacing: ShellTheme.Spacing.xSmall) {
                                Text(tab.title.isEmpty ? displayTitle(for: tab.url) : tab.title)
                                    .font(.subheadline.weight(.semibold))
                                    .foregroundStyle(.primary)
                                    .lineLimit(1)

                                Text(displayHost(for: tab.url))
                                    .font(.footnote)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }

                            Spacer(minLength: 0)

                            Image(lucide: Lucide.arrowUpRight)
                                .font(.footnote.weight(.semibold))
                                .foregroundStyle(.secondary)
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(ShellTheme.Spacing.mediumLarge)
                        .background(
                            ShellTheme.Palette.topSiteCardFill,
                            in: RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                        )
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("homeRecentTab_\(index)")
                }
            }
        }
    }

    private func sectionHeader(title: String) -> some View {
        Text(title)
            .font(.caption.weight(.semibold))
            .foregroundStyle(.secondary)
            .textCase(.uppercase)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func displayHost(for urlString: String) -> String {
        URLComponents(string: urlString)?.host ?? urlString
    }

    private func displayTitle(for urlString: String) -> String {
        displayHost(for: urlString)
            .replacingOccurrences(of: "www.", with: "")
    }
}
