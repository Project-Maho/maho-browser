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
    @Binding var homeSearchFocusRequest: Bool
    @Binding var isSearchFocused: Bool
    let onTabsTapped: () -> Void
    let onAddressTapped: () -> Void
    let onHomeSubmit: () -> Void
    let onGoBack: () -> Void
    let onGoForward: () -> Void
    let onTabSwipeLeft: () -> Void
    let onTabSwipeRight: () -> Void
    let onSelectSpace: (String) -> Void
    let onMenuTapped: () -> Void
    let onToggleIncognito: () -> Void
    let onFindInPage: () -> Void
    let onSettings: () -> Void
    let onShare: () -> Void
    let onReload: () -> Void
    let onToggleReader: () -> Void
    let onZoomIn: () -> Void
    let onZoomOut: () -> Void
    let onToggleDesktop: () -> Void
    let onPiP: () -> Void
    let onBrowseForMe: () -> Void

    @FocusState private var isHomeSearchFocused: Bool

    var body: some View {
        let bottomBarShadow = ShellTheme.Elevation.bottomBar(isIncognito: isIncognito)

        return HStack(spacing: ShellTheme.Spacing.xSmall) {
            tabButton

            centerCapsule
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
        .onChange(of: homeSearchFocusRequest) { _, newValue in
            guard newValue else { return }
            isHomeSearchFocused = true
            homeSearchFocusRequest = false
        }
        .onChange(of: isHomeSearchFocused) { _, newValue in
            if isSearchFocused != newValue {
                isSearchFocused = newValue
            }
        }
        .onChange(of: isSearchFocused) { _, newValue in
            if isHomeSearchFocused != newValue {
                isHomeSearchFocused = newValue
            }
        }
        .accessibilityIdentifier("arcBottomBar")
    }

    private var foregroundColor: Color {
        ShellTheme.Palette.bottomBarForeground(isIncognito: isIncognito)
    }

    private var mutedForegroundColor: Color {
        ShellTheme.Palette.bottomBarMutedForeground(isIncognito: isIncognito)
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

                Text("\(max(tabCount, 0))")
                    .font(.caption2.weight(.bold))
                    .foregroundStyle(ShellTheme.Palette.bottomBarBadgeText(isIncognito: isIncognito))
                    .padding(.horizontal, ShellTheme.Spacing.badgeHorizontal)
                    .padding(.vertical, ShellTheme.Spacing.badgeVertical)
                    .background(foregroundColor, in: Capsule())
                    .offset(x: 5, y: -1)
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
        .accessibilityValue("\(max(tabCount, 0)) open")
        .accessibilityHint("Double tap to open the tab switcher. Touch and hold to switch spaces.")
    }

    private var centerCapsule: some View {
        Group {
            if isHomeMode {
                homeCenterCapsule
            } else {
                browsingCenterCapsule
            }
        }
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

    private var homeCenterCapsule: some View {
        HStack(spacing: ShellTheme.Spacing.xSmall) {
            Image(lucide: isIncognito ? Lucide.eyeOff : Lucide.search)
                .font(.footnote.weight(.semibold))
                .foregroundStyle(mutedForegroundColor)

            HStack(spacing: ShellTheme.Spacing.xSmall) {
                TextField("Search or enter URL", text: $homeQuery)
                    .textFieldStyle(.plain)
                    .font(.footnote.weight(.semibold))
                    .foregroundStyle(foregroundColor)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                    .keyboardType(.webSearch)
                    .submitLabel(.search)
                    .focused($isHomeSearchFocused)
                    .onSubmit(onHomeSubmit)
                    .accessibilityIdentifier("homeSearchField")

                if !homeQuery.isEmpty {
                    Button {
                        homeQuery = ""
                        isHomeSearchFocused = true
                    } label: {
                        Image(lucide: Lucide.circleX)
                            .font(.footnote)
                            .foregroundStyle(mutedForegroundColor)
                            .frame(width: ShellTheme.Size.touchTarget - ShellTheme.Spacing.medium, height: ShellTheme.Size.touchTarget - ShellTheme.Spacing.medium)
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("homeSearchClearButton")
                    .accessibilityLabel("Clear search")
                    .accessibilityHint("Clears the current search or address text.")
                }

                Button(action: onHomeSubmit) {
                    Image(lucide: Lucide.arrowUpRight)
                        .font(.footnote.weight(.semibold))
                        .foregroundStyle(homeQuery.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? mutedForegroundColor : foregroundColor)
                        .frame(width: ShellTheme.Size.touchTarget - ShellTheme.Spacing.medium, height: ShellTheme.Size.touchTarget - ShellTheme.Spacing.medium)
                }
                .buttonStyle(.plain)
                .disabled(homeQuery.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                .opacity(homeQuery.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? 0.42 : 1)
                .accessibilityIdentifier("homeSearchSubmitButton")
                .accessibilityLabel("Submit search")
                .accessibilityHint("Submits the current search or address.")
            }
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("homeSearchBar")
        }
        .padding(.leading, ShellTheme.Spacing.medium)
        .padding(.trailing, ShellTheme.Spacing.xSmall)
        .padding(.vertical, 3)
        .frame(minHeight: ShellTheme.Size.bottomBarCapsuleHeight)
        .background(capsuleBackground)
        .overlay(capsuleStroke)
        .contentShape(Rectangle())
        .onTapGesture {
            isHomeSearchFocused = true
        }
        .accessibilityIdentifier("arcAddressPill")
        .accessibilityLabel("Address and search")
        .accessibilityHint("Double tap to search or enter an address.")
    }

    private var searchButtonLabel: some View {
        HStack(spacing: ShellTheme.Spacing.xSmall) {
            Image(lucide: isIncognito ? Lucide.eyeOff : Lucide.search)
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
        Button(action: onMenuTapped) {
            Image(lucide: Lucide.menu)
                .font(.body.weight(.semibold))
                .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                .foregroundStyle(foregroundColor)
                .padding(.horizontal, 2)
                .background(
                    chromeControlBackground(isProminent: false)
                )
        }
        .buttonStyle(.plain)
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
}
