import SwiftUI

struct TabGridSheet: View {
    @Binding var isIncognito: Bool

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
                isIncognito: isIncognito,
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
    }

    private var sheetHeader: some View {
        HStack(alignment: .center, spacing: ShellTheme.Spacing.medium) {
            VStack(alignment: .leading, spacing: 2) {
                Text(activeSpaceTitle)
                    .font(.title3.weight(.semibold))
                    .foregroundStyle(isIncognito ? ShellTheme.Palette.incognitoForeground : .primary)

                Text("\(filteredTabs.count) \(filteredTabs.count == 1 ? "tab" : "tabs")")
                    .font(.caption.weight(.medium))
                    .foregroundStyle(isIncognito ? ShellTheme.Palette.incognitoMutedForeground : .secondary)
            }

            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, ShellTheme.Spacing.large)
        .padding(.top, ShellTheme.Spacing.page)
        .padding(.bottom, ShellTheme.Spacing.small)
        .accessibilityIdentifier("tabGridHeader")
    }

    @ViewBuilder
    private var tabGridBackground: some View {
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
