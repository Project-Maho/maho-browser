import SwiftUI
import LucideIcons

struct ArcBottomBar: View {
    struct SpaceMenuItem: Identifiable {
        let id: String
        let name: String
        let isActive: Bool
    }

    let tabCount: Int
    let tintColor: Color
    let isIncognito: Bool
    let isHomeMode: Bool
    let isLoading: Bool
    let canReload: Bool
    let canShare: Bool
    let isReaderMode: Bool
    let isDesktopMode: Bool
    let zoomLevel: Double
    let pageTitle: String
    let pageHost: String
    let canGoBack: Bool
    let canGoForward: Bool
    let tabSwipeOffset: CGFloat
    let spaces: [SpaceMenuItem]
    @Binding var homeQuery: String
    let onTabsTapped: () -> Void
    let onAddressTapped: () -> Void
    var onNewTabTapped: (() -> Void)? = nil
    let onGoBack: () -> Void
    let onGoForward: () -> Void
    let onTabSwipeLeft: () -> Void
    let onTabSwipeRight: () -> Void
    let onSelectSpace: (String) -> Void
    let onFindInPage: () -> Void
    let onSettings: () -> Void
    let onOpenAiChat: () -> Void
    let onOpenConversations: () -> Void
    let onShare: () -> Void
    let onReload: () -> Void
    let onArchive: () -> Void
    let onOpenArchive: () -> Void
    let onToggleReader: () -> Void
    let onZoomIn: () -> Void
    let onZoomOut: () -> Void
    let onResetZoom: () -> Void
    let onToggleDesktop: () -> Void
    let onPiP: () -> Void
    let onBrowseForMe: () -> Void

    var body: some View {
        if isHomeMode {
            homeFloatingBar
        } else {
            browsingBar
        }
    }

    private var browsingBar: some View {
        let bottomBarShadow = ShellTheme.Elevation.bottomBar(isIncognito: isIncognito)

        return HStack(spacing: ShellTheme.Spacing.xSmall) {
            tabButton

            browsingCenterCapsule
                .frame(maxWidth: .infinity)

            menuButton
        }
        .accessibilityElement(children: .contain)
        .padding(.horizontal, ShellTheme.Spacing.xSmall)
        .padding(.vertical, ShellTheme.Spacing.xxSmall)
        .frame(minHeight: ShellTheme.Size.bottomBarMinHeight)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBar, style: .continuous)
                .fill(ShellTheme.Palette.bottomBarFill(tint: tintColor, isIncognito: isIncognito))
                .background(ShellTheme.Materials.bottomBar, in: RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBar, style: .continuous))
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBar, style: .continuous)
                .strokeBorder(ShellTheme.Palette.bottomBarBorder(for: foregroundColor), lineWidth: ShellTheme.Stroke.hairline)
        )
        .overlay(alignment: .top) {
            Capsule()
                .fill(ShellTheme.Palette.bottomBarDragIndicator(isIncognito: isIncognito))
                .frame(width: ShellTheme.Size.bottomBarGrabberWidth, height: ShellTheme.Size.bottomBarGrabberHeight)
                .padding(.top, ShellTheme.Spacing.xSmall)

            if !isHomeMode && isLoading {
                ProgressView()
                    .progressViewStyle(.linear)
                    .tint(foregroundColor)
                    .frame(width: ShellTheme.Size.bottomBarProgressWidth)
                    .padding(.top, ShellTheme.Spacing.large)
                    .transition(.opacity)
            }
        }
        .shadow(color: bottomBarShadow.color, radius: bottomBarShadow.radius, x: bottomBarShadow.x, y: bottomBarShadow.y)
        .accessibilityIdentifier("arcBottomBar")
    }

    private var foregroundColor: Color {
        ShellTheme.Palette.bottomBarForeground(isIncognito: isIncognito)
    }

    private var mutedForegroundColor: Color {
        ShellTheme.Palette.bottomBarMutedForeground(isIncognito: isIncognito)
    }

    private var homeFloatingBar: some View {
        GeometryReader { geometry in
            let newTabWidth = homeNewTabWidth(for: geometry.size.width)

            ZStack {
                HStack(spacing: 0) {
                    tabButton

                    Spacer(minLength: 0)

                    HStack(spacing: ShellTheme.Spacing.medium) {
                        homeAiButton
                        homeSettingsButton
                    }
                }

                homeNewTabButton(width: newTabWidth)
            }
            .frame(width: geometry.size.width, height: ShellTheme.Size.touchTarget)
        }
        .frame(height: ShellTheme.Size.touchTarget)
        .frame(maxWidth: .infinity)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("arcBottomBar")
    }

    private func homeNewTabWidth(for barWidth: CGFloat) -> CGFloat {
        let rightGroupWidth = ShellTheme.Size.touchTarget * 2 + ShellTheme.Spacing.medium
        let centeredButtonMaximumWidth = barWidth - rightGroupWidth * 2
        return min(112, max(80, centeredButtonMaximumWidth))
    }

    private var displayTitle: String {
        let host = pageHost.trimmingCharacters(in: .whitespacesAndNewlines)
        return host.isEmpty ? "Search or enter URL" : host
    }

    private var displaySubtitle: String {
        if isIncognito {
            return "Private"
        }

        let trimmed = pageTitle.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "Search or enter URL" : trimmed
    }

    private var tabButton: some View {
        Button(action: onTabsTapped) {
            ZStack(alignment: .topTrailing) {
                Image(lucide: Lucide.copy)
                    .font(.body.weight(.semibold))
                    .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                    .foregroundStyle(foregroundColor)

                if tabCount > 0 {
                    Text("\(tabCount)")
                        .font(.caption2.weight(.bold))
                        .foregroundStyle(ShellTheme.Palette.bottomBarBadgeText(isIncognito: isIncognito))
                        .padding(.horizontal, ShellTheme.Spacing.badgeHorizontal)
                        .padding(.vertical, ShellTheme.Spacing.badgeVertical)
                        .background(foregroundColor, in: Capsule())
                        .offset(x: 5, y: -1)
                }
            }
            .padding(.horizontal, 2)
            .background(
                chromeControlBackground(isProminent: false)
            )
        }
        .buttonStyle(.plain)
        .contextMenu(menuItems: {
            ForEach(spaces, id: \.id) { space in
                Button {
                    onSelectSpace(space.id)
                } label: {
                    Label { Text(space.name) } icon: { Image(lucide: MahoIcon.imageForSFSymbol(space.isActive ? "checkmark.circle.fill" : "circle")) }
                }
            }
        })
        .accessibilityIdentifier("tabsButton")
        .accessibilityLabel("Tabs")
        .accessibilityValue(tabCount > 0 ? "\(tabCount) open" : "")
        .accessibilityHint("Double tap to open the tab switcher. Touch and hold to switch spaces.")
    }

    private func homeNewTabButton(width: CGFloat = 112) -> some View {
        Button(action: onNewTabTapped ?? onAddressTapped) {
            Image(lucide: Lucide.plus)
                .font(.body.weight(.semibold))
                .foregroundStyle(foregroundColor)
                .frame(width: width, height: ShellTheme.Size.bottomBarCapsuleHeight)
                .background(capsuleBackground)
                .overlay(capsuleStroke)
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("homeNewTabButton")
        .accessibilityLabel("New Tab")
        .accessibilityHint("Opens a new tab to search or enter an address.")
    }

    private var homeAiButton: some View {
        Button(action: onOpenAiChat) {
            Image(lucide: Lucide.bot)
                .font(.body.weight(.semibold))
                .foregroundStyle(foregroundColor)
                .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                .background(chromeCircleBackground)
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("homeAiButton")
        .accessibilityLabel("Agent")
        .accessibilityHint("Opens the AI agent.")
    }

    private var homeSettingsButton: some View {
        Button(action: onSettings) {
            Image(lucide: Lucide.settings)
                .font(.body.weight(.semibold))
                .foregroundStyle(foregroundColor)
                .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                .background(chromeCircleBackground)
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("homeSettingsButton")
        .accessibilityLabel("Settings")
        .accessibilityHint("Opens settings.")
    }

    private var browsingCenterCapsule: some View {
        HStack(spacing: 2) {
            chromeNavButton(
                icon: Lucide.chevronLeft,
                isEnabled: canGoBack,
                action: onGoBack,
                identifier: "arcBackButton",
                label: "Back",
                hint: "Moves to the previous page in browsing history."
            )

            Button(action: onAddressTapped) {
                searchButtonLabel
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier("arcAddressPill")
            .accessibilityLabel("Address and search")
            .accessibilityHint("Double tap to search or enter an address. Swipe left or right to move between tabs.")

            chromeNavButton(
                icon: Lucide.chevronRight,
                isEnabled: canGoForward,
                action: onGoForward,
                identifier: "arcForwardButton",
                label: "Forward",
                hint: "Moves to the next page in browsing history."
            )
        }
        .padding(.horizontal, 4)
        .padding(.vertical, 3)
        .frame(minHeight: ShellTheme.Size.bottomBarCapsuleHeight)
        .background(capsuleBackground)
        .overlay(capsuleStroke)
        .offset(x: tabSwipeOffset)
        .animation(.spring(response: 0.26, dampingFraction: 0.84), value: tabSwipeOffset)
        .simultaneousGesture(
            DragGesture(minimumDistance: 18, coordinateSpace: .local)
                .onEnded { value in
                    guard abs(value.translation.width) > abs(value.translation.height) else { return }
                    if value.translation.width <= -42 {
                        onTabSwipeLeft()
                    } else if value.translation.width >= 42 {
                        onTabSwipeRight()
                    }
                }
        )
    }

    private var searchButtonLabel: some View {
        HStack(spacing: ShellTheme.Spacing.xSmall) {
            Image(lucide: Lucide.search)
                .font(.footnote.weight(.semibold))
                .foregroundStyle(mutedForegroundColor)

            VStack(alignment: .leading, spacing: 1) {
                Text(displayTitle)
                    .font(.footnote.weight(.semibold))
                    .foregroundStyle(foregroundColor)
                    .lineLimit(1)

                Text(displaySubtitle)
                    .font(.caption2)
                    .foregroundStyle(mutedForegroundColor)
                    .lineLimit(1)
            }

            Spacer(minLength: 0)

            if isReaderMode {
                Image(lucide: Lucide.bookOpen)
                    .font(.caption2.weight(.semibold))
                    .foregroundStyle(mutedForegroundColor)
            } else if isDesktopMode {
                Image(lucide: Lucide.monitor)
                    .font(.caption2.weight(.semibold))
                    .foregroundStyle(mutedForegroundColor)
            }
        }
        .contentShape(Rectangle())
    }

    private var capsuleBackground: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBarCapsule, style: .continuous)
            .fill(ShellTheme.Palette.bottomBarCapsuleFill(tint: tintColor, isIncognito: isIncognito).opacity(isIncognito ? 1 : 0.64))
    }

    private var capsuleStroke: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBarCapsule, style: .continuous)
            .strokeBorder(ShellTheme.Palette.bottomBarBorder(for: foregroundColor), lineWidth: ShellTheme.Stroke.hairline)
    }

    private var menuButton: some View {
        Menu {
            Section {
                Button(action: onFindInPage) {
                    Label { Text("Find in Page") } icon: { Image(lucide: Lucide.search) }
                }
                .disabled(!canReload)

                Button(action: onShare) {
                    Label { Text("Share") } icon: { Image(lucide: Lucide.share) }
                }
                .disabled(!canShare)

                Button(action: onReload) {
                    Label { Text("Reload") } icon: { Image(lucide: Lucide.rotateCw) }
                }
                .disabled(!canReload)

                Button(action: onArchive) {
                    Label { Text("Add to Archive") } icon: { Image(lucide: Lucide.archive) }
                }
                .disabled(!canReload)

                Button(action: onOpenArchive) {
                    Label { Text("Archive") } icon: { Image(lucide: Lucide.archive) }
                }
                .accessibilityIdentifier("openArchiveButton")
            }

            Section {
                Button(action: onToggleReader) {
                    Label { Text(isReaderMode ? "Exit Reader Mode" : "Reader Mode") } icon: { Image(lucide: Lucide.bookOpen) }
                }
                .disabled(!canReload)

                Button(action: onToggleDesktop) {
                    Label { Text(isDesktopMode ? "Mobile Site" : "Desktop Site") } icon: { Image(lucide: Lucide.monitor) }
                }
                .disabled(!canReload)

                Menu {
                    Button(action: onZoomIn) {
                        Label { Text("Zoom In") } icon: { Image(lucide: Lucide.zoomIn) }
                    }
                    .disabled(!canReload)

                    Button(action: onZoomOut) {
                        Label { Text("Zoom Out") } icon: { Image(lucide: Lucide.zoomOut) }
                    }
                    .disabled(!canReload)

                    Button(action: onResetZoom) {
                        Label { Text("Reset Zoom") } icon: { Image(lucide: Lucide.rotateCcw) }
                    }
                    .disabled(!canReload)
                } label: {
                    Label { Text("Zoom \(Int((zoomLevel * 100).rounded()))%") } icon: { Image(lucide: Lucide.zoomIn) }
                }

                Button(action: onPiP) {
                    Label { Text("Picture in Picture") } icon: { Image(lucide: Lucide.appWindow) }
                }
                .disabled(!canReload)

                Button(action: onBrowseForMe) {
                    Label { Text("Browse for Me") } icon: { Image(lucide: Lucide.sparkles) }
                }
                .disabled(!canReload)
            }

            Section {
                Button(action: onOpenConversations) {
                    Label { Text("Conversations") } icon: { Image(lucide: Lucide.messageCircle) }
                }
                .accessibilityIdentifier("openConversationsButton")

                Button(action: onSettings) {
                    Label { Text("Settings") } icon: { Image(lucide: Lucide.settings) }
                }
            }
        } label: {
            Image(lucide: Lucide.menu)
                .font(.body.weight(.semibold))
                .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                .foregroundStyle(foregroundColor)
                .padding(.horizontal, 2)
                .background(
                    chromeControlBackground(isProminent: false)
                )
        }
        .accessibilityIdentifier("menuButton")
        .accessibilityLabel("Page menu")
        .accessibilityHint("Opens page actions and view options.")
    }

    private func chromeNavButton(
        icon: UIImage,
        isEnabled: Bool,
        action: @escaping () -> Void,
        identifier: String,
        label: String,
        hint: String
    ) -> some View {
        Button(action: action) {
            Image(lucide: icon)
                .font(.caption.weight(.bold))
                .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                .foregroundStyle(isEnabled ? foregroundColor : mutedForegroundColor)
                .opacity(isEnabled ? 1 : 0.55)
        }
        .buttonStyle(.plain)
        .disabled(!isEnabled)
        .accessibilityIdentifier(identifier)
        .accessibilityLabel(label)
        .accessibilityHint(hint)
    }

    private func chromeControlBackground(isProminent: Bool) -> some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBarControl, style: .continuous)
            .fill(
                (isProminent
                    ? ShellTheme.Palette.bottomBarCapsuleFill(tint: tintColor, isIncognito: isIncognito)
                    : ShellTheme.Palette.bottomBarSecondaryFill(isIncognito: isIncognito)
                )
                .opacity(isIncognito ? 1 : (isProminent ? 0.8 : 0.58))
            )
            .overlay(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBarControl, style: .continuous)
                    .strokeBorder(ShellTheme.Palette.bottomBarBorder(for: foregroundColor), lineWidth: ShellTheme.Stroke.hairline)
            )
    }

    private var chromeCircleBackground: some View {
        Circle()
            .fill(
                ShellTheme.Palette.bottomBarSecondaryFill(isIncognito: isIncognito)
                    .opacity(isIncognito ? 1 : 0.58)
            )
            .overlay(
                Circle()
                    .strokeBorder(ShellTheme.Palette.bottomBarBorder(for: foregroundColor), lineWidth: ShellTheme.Stroke.hairline)
            )
    }
}
