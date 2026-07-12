import SwiftUI
import LucideIcons

struct TabGridSheet: View {
    @Binding var isIncognito: Bool
    let onRequestNewTab: () -> Void

    @Environment(\.dismiss) private var dismiss

    @State private var tabs: [TabViewModel] = []
    @State private var spaces: [SpaceViewModel] = []
    @State private var activeTabId: TabId?
    @State private var activeSpaceId: SpaceId?
    @State private var showSpaceSwitcher = false
    @State private var showClustering = false

    private let bridge = MahoBridge.shared

    var body: some View {
        VStack(spacing: 0) {
            sheetHeader

            if !spaces.isEmpty {
                SpaceChipRow(
                    spaces: spaces,
                    activeSpaceId: activeSpaceId,
                    onSelectSpace: { spaceId in
                        bridge.activateSpace(id: spaceId)
                        reloadState()
                    },
                    onShowAll: { showSpaceSwitcher = true }
                )
                .padding(.bottom, ShellTheme.Spacing.xSmall)
            }

            TabGridView(
                tabs: filteredTabs,
                activeTabId: activeTabId,
                activeSpaceId: activeSpaceId,
                isIncognito: isIncognito,
                onSelectTab: { tabId in
                    bridge.activateTab(id: tabId)
                    dismiss()
                },
                onCloseTab: { tabId in
                    bridge.closeTab(id: tabId)
                    reloadState()
                }
            )

            let activeSpaceName = spaces.first { $0.id == activeSpaceId }?.name ?? "Current space"
            TabGridToolbar(
                tabCount: filteredTabs.count,
                activeSpaceName: activeSpaceName,
                onNewTab: {
                    onRequestNewTab()
                    dismiss()
                },
                onCloseAll: {
                    for tab in filteredTabs {
                        bridge.closeTab(id: tab.id)
                    }
                    reloadState()
                },
                onDone: {
                    dismiss()
                }
            )
        }
        .background(tabGridBackground)
        .onAppear(perform: reloadState)
        .sheet(isPresented: $showSpaceSwitcher) {
            SpaceSwitcherSheet(
                onSpaceSelected: {
                    reloadState()
                }
            )
        }
        .sheet(isPresented: $showClustering) {
            if let spaceId = activeSpaceId {
                TabGroupSuggestionView(
                    spaceId: spaceId,
                    tabs: filteredTabs,
                    spaceName: activeSpaceTitle,
                    onFoldersCreated: {
                        reloadState()
                    }
                )
            }
        }
    }

    private var sheetHeader: some View {
        HStack(alignment: .center, spacing: ShellTheme.Spacing.medium) {
            VStack(alignment: .leading, spacing: 2) {
                Text(activeSpaceTitle)
                    .font(.title3.weight(.semibold))
                    .foregroundStyle(.primary)

                Text("\(filteredTabs.count) \(filteredTabs.count == 1 ? "tab" : "tabs")")
                    .font(.caption.weight(.medium))
                    .foregroundStyle(.secondary)
            }

            Spacer(minLength: 0)

            if FeatureFlags.clusterTabsEnabled {
                Button(action: { showClustering = true }) {
                    Image(lucide: Lucide.sparkles)
                        .font(.body)
                        .foregroundStyle(ShellTheme.Palette.accent)
                }
                .padding(.trailing, ShellTheme.Spacing.small)
                .accessibilityIdentifier("tabClusteringButton")
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, ShellTheme.Spacing.large)
        .padding(.top, ShellTheme.Spacing.page)
        .padding(.bottom, ShellTheme.Spacing.small)
        .accessibilityIdentifier("tabGridHeader")
    }

    private var tabGridBackground: some View {
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

    private var activeSpaceTitle: String {
        spaces.first(where: { $0.id == activeSpaceId })?.name ?? "Tabs"
    }

    private var filteredTabs: [TabViewModel] {
        tabs.filter { tab in
            (activeSpaceId == nil || tab.spaceId == activeSpaceId) && tab.isPrivate == isIncognito
        }
    }

    private func reloadState() {
        tabs = bridge.getTabViewModels()
        spaces = bridge.getSpaceViewModels()
        activeSpaceId = bridge.getActiveSpaceId()
        activeTabId = bridge.getActiveTabId()
    }
}
