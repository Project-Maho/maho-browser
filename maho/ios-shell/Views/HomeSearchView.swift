import SwiftUI
import LucideIcons

struct HomeSearchView: View {
    let onOpenTabs: () -> Void
    let onOpenSettings: () -> Void
    let onResumeTab: (TabId) -> Void
    let onSelectSite: (String) -> Void

    private let bridge = MahoBridge.shared

    private var activeSpaceId: SpaceId? {
        bridge.getActiveSpaceId()
    }

    private var currentSpaceTabs: [TabViewModel] {
        bridge.getTabViewModels().filter { tab in
            activeSpaceId == nil || tab.spaceId == activeSpaceId
        }
    }

    private var recentTabs: [TabViewModel] {
        Array(currentSpaceTabs
            .sorted(by: { $0.lastActiveAt > $1.lastActiveAt })
            .prefix(3))
    }

    private var topSites: [TabViewModel] {
        var seenUrls = Set<String>()
        var uniqueTabs = [TabViewModel]()
        for tab in currentSpaceTabs.sorted(by: { $0.lastActiveAt > $1.lastActiveAt }) {
            let url = tab.url.trimmingCharacters(in: .whitespacesAndNewlines)
            if !url.isEmpty && !seenUrls.contains(url) {
                seenUrls.insert(url)
                uniqueTabs.append(tab)
            }
        }
        return Array(uniqueTabs.prefix(8))
    }

    var body: some View {
        ZStack {
            backdrop
            
            if recentTabs.isEmpty && topSites.isEmpty {
                VStack {
                    Spacer()
                    brandLogo
                    Spacer()
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(.horizontal, ShellTheme.Spacing.large)
            } else {
                ScrollView {
                    VStack(spacing: ShellTheme.Spacing.large) {
                        Spacer(minLength: ShellTheme.Spacing.large)
                        
                        brandLogo
                        
                        if !recentTabs.isEmpty {
                            continueSection
                        }
                        
                        if !topSites.isEmpty {
                            topSitesSection
                        }
                        
                        Spacer(minLength: ShellTheme.Size.homeBottomSpacer)
                    }
                    .padding(.horizontal, ShellTheme.Spacing.large)
                }
            }
        }
        .accessibilityIdentifier("homeSearchView")
    }

    private var backdrop: some View {
        LinearGradient(
            colors: [
                ShellTheme.Palette.homeBackgroundElevated,
                ShellTheme.Palette.homeBackground
            ],
            startPoint: .top,
            endPoint: .bottom
        )
        .ignoresSafeArea()
    }

    private var brandLogo: some View {
        ZStack {
            Image("MahoStar")
                .renderingMode(.template)
                .resizable()
                .aspectRatio(contentMode: .fit)
                .foregroundStyle(.ultraThinMaterial)
                .scaleEffect(1.13)
                .shadow(color: Color.black.opacity(0.16), radius: 7, x: 0, y: 3)
            Image("MahoStar")
                .resizable()
                .aspectRatio(contentMode: .fit)
        }
        .frame(width: 120, height: 120)
    }

    private var continueSection: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
            sectionHeader(title: "Continue", subtitle: "Pick up where you left off")
            
            VStack(spacing: 0) {
                ForEach(Array(recentTabs.enumerated()), id: \.offset) { index, tab in
                    Button {
                        onResumeTab(tab.id)
                    } label: {
                        HStack(spacing: ShellTheme.Spacing.medium) {
                            Image(lucide: Lucide.globe)
                                .font(.body)
                                .foregroundStyle(ShellTheme.Palette.accent)
                            
                            VStack(alignment: .leading, spacing: 2) {
                                Text(tab.title.isEmpty ? "New tab" : tab.title)
                                    .font(.subheadline.weight(.semibold))
                                    .foregroundStyle(.primary)
                                    .lineLimit(1)
                                
                                Text(displayHost(for: tab.url))
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }
                            
                            Spacer()
                            
                            Image(lucide: Lucide.chevronRight)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        .padding(.vertical, ShellTheme.Spacing.medium)
                        .padding(.horizontal, ShellTheme.Spacing.large)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    
                    if index < recentTabs.count - 1 {
                        Divider()
                            .padding(.leading, ShellTheme.Spacing.large)
                    }
                }
            }
            .background(ShellTheme.Palette.tabCardSurface.opacity(0.85), in: RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                    .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
            )
        }
    }

    private var topSitesSection: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
            sectionHeader(title: "Top sites", subtitle: "Fast ways back in")
            
            let columns = [
                GridItem(.flexible(), spacing: ShellTheme.Spacing.medium),
                GridItem(.flexible(), spacing: ShellTheme.Spacing.medium)
            ]
            
            LazyVGrid(columns: columns, spacing: ShellTheme.Spacing.medium) {
                ForEach(topSites) { tab in
                    Button {
                        onSelectSite(tab.url)
                    } label: {
                        HStack(spacing: ShellTheme.Spacing.medium) {
                            Image(lucide: Lucide.sparkles)
                                .font(.caption)
                                .foregroundStyle(ShellTheme.Palette.accent)
                            
                            VStack(alignment: .leading, spacing: 2) {
                                Text(tab.title.isEmpty ? displayHost(for: tab.url) : tab.title)
                                    .font(.footnote.weight(.semibold))
                                    .foregroundStyle(.primary)
                                    .lineLimit(1)
                                
                                Text(displayHost(for: tab.url))
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }
                            Spacer(minLength: 0)
                        }
                        .padding(ShellTheme.Spacing.medium)
                        .background(ShellTheme.Palette.tabCardSurface.opacity(0.85), in: RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous))
                        .overlay(
                            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                        )
                    }
                    .buttonStyle(.plain)
                }
            }
        }
    }

    private func sectionHeader(title: String, subtitle: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title)
                .font(.headline)
                .foregroundStyle(.primary)
            Text(subtitle)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(.leading, ShellTheme.Spacing.small)
    }

    private func displayHost(for url: String) -> String {
        guard let urlObj = URL(string: url), let host = urlObj.host else { return "New tab" }
        return host.replacingOccurrences(of: "www.", with: "")
    }
}
