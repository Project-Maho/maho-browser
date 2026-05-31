import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct TabPreviewView: View {
    let tab: TabViewModel
    let previewImage: Data?
    let onShare: () -> Void
    let onClose: () -> Void

    var body: some View {
        VStack(spacing: 16) {
            previewArea

            metadataSection

            actionBar
        }
        .padding(ShellTheme.Spacing.large)
    }

    private var previewArea: some View {
        Group {
#if canImport(UIKit)
            if let previewImage, let uiImage = UIImage(data: previewImage) {
                Image(uiImage: uiImage)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .clipShape(RoundedRectangle(cornerRadius: ShellTheme.Radius.button))
            } else {
                RoundedRectangle(cornerRadius: ShellTheme.Radius.button)
                    .fill(ShellTheme.Palette.tabPreviewPlaceholder)
                    .aspectRatio(4.0 / 3.0, contentMode: .fit)
                    .overlay {
                        VStack(spacing: 8) {
                            Image(lucide: Lucide.image)
                                .font(.title)
                                .foregroundStyle(.tertiary)
                            Text("No Preview")
                                .font(.caption)
                                .foregroundStyle(.tertiary)
                        }
                    }
            }
#else
            RoundedRectangle(cornerRadius: ShellTheme.Radius.button)
                .fill(ShellTheme.Palette.tabPreviewPlaceholder)
                .aspectRatio(4.0 / 3.0, contentMode: .fit)
                .overlay {
                    VStack(spacing: ShellTheme.Spacing.small) {
                        Image(lucide: Lucide.image)
                            .font(.title)
                            .foregroundStyle(.tertiary)
                        Text("No Preview")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }
                }
#endif
        }
    }

    private var metadataSection: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                if tab.isLoading {
                    ProgressView()
                        .controlSize(.mini)
                }
                Text(tab.title.isEmpty ? "New Tab" : tab.title)
                    .font(.headline)
                    .lineLimit(2)
            }

            Text(tab.url)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)

            HStack(spacing: 12) {
                if tab.isPinned {
                    Label { Text("Pinned") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("pin.fill")) }
                        .font(.caption2)
                        .foregroundStyle(.orange)
                }
                if tab.isMuted {
                    Label { Text("Muted") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("speaker.slash.fill")) }
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
                if tab.isPlayingAudio {
                    Label { Text("Playing") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("speaker.wave.2.fill")) }
                        .font(.caption2)
                        .foregroundStyle(.blue)
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var actionBar: some View {
        HStack(spacing: 24) {
            Button(action: onShare) {
                Label { Text("Share") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("square.and.arrow.up")) }
            }

            Spacer()

            Button(role: .destructive, action: onClose) {
                Label { Text("Close Tab") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("xmark")) }
            }
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
    }
}
