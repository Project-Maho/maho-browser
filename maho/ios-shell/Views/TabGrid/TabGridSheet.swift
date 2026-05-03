import SwiftUI

struct TabGridSheet: View {
    let onNewTab: () -> Bool

    @Environment(\.dismiss) private var dismiss

    @State private var tabs: [TabViewModel] = []
    @State private var spaces: [SpaceViewModel] = []
    @State private var activeTabId: TabId?
    @State private var activeSpaceId: SpaceId?
    @State private var showSpaceSwitcher = false

    private let bridge = MahoBridge.shared

    var body: some View {
        VStack(spacing: 0) {
            sheetHeader

            if spaces.count > 1 {
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
                onSelectTab: { tabId in
                    bridge.activateTab(id: tabId)
                    dismiss()
                },
                onCloseTab: { tabId in
                    bridge.closeTab(id: tabId)
                    reloadState()
                }
            )

            TabGridToolbar(
                tabCount: filteredTabs.count,
                onNewTab: {
                    if onNewTab() {
                        dismiss()
                    }
                },
                onCloseAll: {
                    for tab in filteredTabs {
                        bridge.closeTab(id: tab.id)
                    }
                    reloadState()
                },
                onDismiss: { dismiss() }
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

            Text("\(filteredTabs.count)")
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
                .padding(.horizontal, ShellTheme.Spacing.medium)
                .padding(.vertical, ShellTheme.Spacing.xSmall)
                .background(
                    ShellTheme.Palette.tabGridMetaFill,
                    in: Capsule()
                )
                .accessibilityIdentifier("tabGridCountBadge")
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
        guard let activeSpaceId else { return tabs }
        return tabs.filter { $0.spaceId == activeSpaceId }
    }

    private func reloadState() {
        tabs = bridge.getTabViewModels()
        spaces = bridge.getSpaceViewModels()
        activeSpaceId = bridge.getActiveSpaceId()
        activeTabId = bridge.getActiveTabId()
    }
}
