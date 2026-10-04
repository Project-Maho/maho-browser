import Foundation
import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct TabCardView: View {
    let tab: TabViewModel
    let isActive: Bool
    let isIncognito: Bool
    let previewImage: Data?
    let deckIndex: Int
    let onTap: () -> Void
    let onClose: () -> Void

    private var cardTitleColor: Color {
        isIncognito ? ShellTheme.Palette.incognitoForeground : .primary
    }

    private var cardHostColor: Color {
        isIncognito ? ShellTheme.Palette.incognitoMutedForeground : .secondary
    }

    private var cardSurfaceColor: Color {
        isIncognito ? ShellTheme.Palette.incognitoSurfaceStrong : ShellTheme.Palette.tabCardSurface.opacity(isActive ? 0.96 : 0.88)
    }

    private var cardBorderColor: Color {
        isIncognito ? ShellTheme.Palette.incognitoBorder : ShellTheme.Palette.overlayBorder
    }

    private var host: String {
        URLComponents(string: tab.url)?.host?.replacingOccurrences(of: "www.", with: "") ?? "New tab"
    }

    private var title: String {
        let trimmed = tab.title.trimmingCharacters(in: CharacterSet.whitespacesAndNewlines)
        return trimmed.isEmpty ? host : trimmed
    }

    private var sendableDevices: [ConnectedDevice] {
        let currentDeviceId = RelayAuthStore.shared.loadSession()?.device?.id
        return MahoBridge.shared.getConnectedDevices().filter { device in
            guard let currentDeviceId, !currentDeviceId.isEmpty else { return true }
            return device.id != currentDeviceId
        }
    }

    private func sendMenuTitle(for device: ConnectedDevice) -> String {
        device.appearsOffline ? "\(device.name) (offline)" : device.name
    }

    private func deviceIcon(for type: String) -> UIImage {
        switch type.lowercased() {
        case "desktop", "mac", "pc": return Lucide.monitor
        case "phone", "iphone", "android": return Lucide.smartphone
        case "tablet", "ipad": return Lucide.tablet
        default: return Lucide.laptop
        }
    }

    var body: some View {
        Button {
            onTap()
        } label: {
            cardContent
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button {
                MahoBridge.shared.duplicateTab(tabId: tab.id)
            } label: {
                Label { Text("Duplicate") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("plus.square.on.square")) }
            }

            Button {
                if tab.isPinned {
                    MahoBridge.shared.unpinTab(tabId: tab.id)
                } else {
                    MahoBridge.shared.pinTab(tabId: tab.id)
                }
            } label: {
                Label { Text(tab.isPinned ? "Unpin" : "Pin") } icon: { Image(lucide: MahoIcon.imageForSFSymbol(tab.isPinned ? "pin.slash" : "pin")) }
            }

            Button {
                MahoBridge.shared.sendEvent(.favoriteTab(tabId: tab.id))
            } label: {
                Label { Text(tab.isFavorite ? "Remove from Favorites" : "Add to Favorites") } icon: { Image(lucide: MahoIcon.imageForSFSymbol(tab.isFavorite ? "star.slash" : "star")) }
            }

            Menu {
                if sendableDevices.isEmpty {
                    Button {} label: {
                        Label { Text("No devices") } icon: { Image(lucide: Lucide.smartphone) }
                    }
                    .disabled(true)
                } else {
                    ForEach(sendableDevices) { device in
                        Button {
                            MahoBridge.shared.sendTabToDevice(url: tab.url, title: title, deviceId: device.id)
                        } label: {
                            Label { Text(sendMenuTitle(for: device)) } icon: { Image(lucide: deviceIcon(for: device.deviceType)) }
                        }
                    }
                }
            } label: {
                Label { Text("Send to Device") } icon: { Image(lucide: Lucide.send) }
            }

            Button {
                if tab.isMuted {
                    MahoBridge.shared.unmuteTab(tabId: tab.id)
                } else {
                    MahoBridge.shared.muteTab(tabId: tab.id)
                }
            } label: {
                Label { Text(tab.isMuted ? "Unmute" : "Mute") } icon: { Image(lucide: MahoIcon.imageForSFSymbol(tab.isMuted ? "speaker.wave.2" : "speaker.slash")) }
            }

            Divider()

            Button(role: .destructive) {
                onClose()
            } label: {
                Label { Text("Close") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("xmark")) }
            }
        }
        .accessibilityIdentifier("tabCard_\(tab.id)")
        .accessibilityLabel(title)
        .accessibilityHint("Opens this tab.")
        .accessibilityValue(isActive ? "active" : "inactive")
    }

    private var cardContent: some View {
        VStack(alignment: .leading, spacing: 0) {
            thumbnailArea
            infoPanel
        }
        .background(cardBackground)
        .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous))
        .overlay(alignment: .topLeading) {
            deckLayerAccent
        }
        .overlay(activeStroke)
        .shadow(
            color: shadowStyle.color,
            radius: shadowStyle.radius,
            x: shadowStyle.x,
            y: shadowStyle.y
        )
        .scaleEffect(isActive ? 1 : 0.985)
        .rotationEffect(.degrees(isActive ? 0 : (deckIndex.isMultiple(of: 2) ? -0.45 : 0.45)))
    }

    private var thumbnailArea: some View {
        ZStack(alignment: .topTrailing) {
            previewBackground
                .frame(height: ShellTheme.Size.tabPreviewHeight + ShellTheme.Spacing.page)
                .clipped()

            LinearGradient(
                colors: [
                    Color.clear,
                    Color.black.opacity(0.05),
                    Color.black.opacity(0.18)
                ],
                startPoint: .top,
                endPoint: .bottom
            )

            Button {
                onClose()
            } label: {
                Image(lucide: Lucide.x)
                    .font(.system(size: 11, weight: .bold))
                    .foregroundStyle(Color.primary.opacity(0.75))
                    .padding(ShellTheme.Spacing.xSmall)
                    .background(
                        ShellTheme.Palette.tabGridFloatingSurface,
                        in: Circle()
                    )
            }
            .buttonStyle(.plain)
            .padding(ShellTheme.Spacing.xSmall)
            .accessibilityIdentifier("tabCardClose_\(tab.id)")
            .accessibilityLabel("Close tab")
            .accessibilityHint("Closes this tab from the tab switcher.")
        }
    }

    @ViewBuilder
    private var previewBackground: some View {
#if canImport(UIKit)
        if let previewImage, let uiImage = UIImage(data: previewImage) {
            Image(uiImage: uiImage)
                .resizable()
                .aspectRatio(contentMode: .fill)
        } else {
            placeholderPreview
        }
#else
        placeholderPreview
#endif
    }

    private var infoPanel: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.small) {
            HStack(alignment: .top, spacing: ShellTheme.Spacing.small) {
                faviconBadge

                VStack(alignment: .leading, spacing: 4) {
                    Text(title)
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(cardTitleColor)
                        .lineLimit(2)

                    Text(host)
                        .font(.caption)
                        .foregroundStyle(cardHostColor)
                        .lineLimit(1)
                }

                Spacer(minLength: 0)
            }

            let isFavorite = tab.isFavorite
            if tab.isPinned || isFavorite || tab.isMuted || tab.isPlayingAudio {
                HStack(spacing: ShellTheme.Spacing.xSmall) {
                    if isFavorite { chip(text: "Saved", color: ShellTheme.Palette.accent) }
                    else if tab.isPinned { chip(text: "Pinned", color: ShellTheme.Palette.accent) }
                    
                    if tab.isPlayingAudio { chip(text: "Audio", color: .green) }
                    if tab.isMuted { chip(text: "Muted", color: .orange) }
                    
                    Spacer(minLength: 0)
                }
            }
        }
        .padding(.horizontal, ShellTheme.Spacing.medium)
        .padding(.top, ShellTheme.Spacing.medium)
        .padding(.bottom, ShellTheme.Spacing.medium)
        .background(panelBackground)
    }

    private func chip(text: String, color: Color) -> some View {
        Text(text)
            .font(.system(size: 9, weight: .bold))
            .foregroundStyle(color)
            .padding(.horizontal, 6)
            .padding(.vertical, 3)
            .background(color.opacity(0.12), in: Capsule())
    }

    private var placeholderPreview: some View {
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
                Image(lucide: Lucide.globe)
                    .font(.title2)
                    .foregroundStyle(.secondary)

                RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                    .fill(Color.white.opacity(0.32))
                    .frame(width: 56, height: 4)

                RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                    .fill(Color.white.opacity(0.2))
                    .frame(width: 82, height: 4)
            }
            .padding(ShellTheme.Spacing.large)
        }
    }

    private var faviconBadge: some View {
        ZStack {
            if let favicon = tab.favicon, let uiImage = favicon.uiImage {
                Image(uiImage: uiImage)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .frame(width: ShellTheme.Size.tabGridFavicon, height: ShellTheme.Size.tabGridFavicon)
                    .cornerRadius(4)
            } else {
                let monogramLetter = DomainMonogram.letter(for: host)
                let monogramColor = DomainMonogram.color(for: host)
                
                RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                    .fill(monogramColor)
                    .frame(width: ShellTheme.Size.tabGridFavicon, height: ShellTheme.Size.tabGridFavicon)
                
                Text(String(monogramLetter))
                    .font(.system(size: 11, weight: .bold))
                    .foregroundColor(.white)
            }
        }
    }

    private func metaIcon(_ icon: UIImage) -> some View {
        Image(lucide: icon)
            .font(.caption2.weight(.semibold))
            .foregroundStyle(.secondary)
    }

    private var shadowStyle: ShellShadowStyle {
        isActive ? ShellTheme.Elevation.tabDeckCard : ShellTheme.Elevation.tabCard
    }

    private var cardBackground: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
            .fill(ShellTheme.Materials.tabGridCard)
            .background(
                RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                    .fill(cardSurfaceColor)
            )
            .overlay {
                RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                    .strokeBorder(cardBorderColor, lineWidth: ShellTheme.Stroke.hairline)
            }
    }

    private var deckLayerAccent: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
            .fill(ShellTheme.Palette.tabGridMetaFill)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .padding(.top, ShellTheme.Spacing.small)
            .padding(.horizontal, ShellTheme.Spacing.small)
            .offset(y: ShellTheme.Spacing.small)
            .opacity(isActive ? 0 : 0.55)
            .allowsHitTesting(false)
    }

    private var panelBackground: some View {
        (isActive ? ShellTheme.Palette.tabGridActiveFill : (isIncognito ? ShellTheme.Palette.incognitoSurface : ShellTheme.Palette.tabCardSurface))
            .overlay(
                LinearGradient(
                    colors: [
                        Color.white.opacity(isActive ? 0.18 : 0.1),
                        Color.clear
                    ],
                    startPoint: .top,
                    endPoint: .bottom
                )
            )
    }

    private var activeStroke: some View {
        RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
            .strokeBorder(isActive ? ShellTheme.Palette.accent : Color.clear, lineWidth: ShellTheme.Stroke.activeTab)
    }
}
