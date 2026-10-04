import SwiftUI
import LucideIcons
#if canImport(UIKit)
import UIKit
#endif

struct PinchSummaryView: View {
    let title: String
    let sentences: [String]
    var isIncognito: Bool = false
    let onDismiss: () -> Void

    @Environment(\.colorScheme) private var systemColorScheme

    var body: some View {
        ZStack {
            Rectangle()
                .fill(.ultraThinMaterial)
                .overlay {
                    LinearGradient(
                        colors: [
                            Color.white.opacity(0.02),
                            ShellTheme.Palette.overlayDim.opacity(0.22),
                            ShellTheme.Palette.overlayDim.opacity(0.36)
                        ],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                }
                .ignoresSafeArea()

            VStack(spacing: 0) {
                Spacer(minLength: ShellTheme.Spacing.hero)

                VStack(alignment: .leading, spacing: ShellTheme.Spacing.large) {
                    HStack(alignment: .top, spacing: ShellTheme.Spacing.medium) {
                        summaryIcon

                        VStack(alignment: .leading, spacing: 4) {
                            Text("Pinch summary")
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(.secondary)
                                .textCase(.uppercase)
                                .tracking(0.8)

                            Text(titleText)
                                .font(.title3.weight(.semibold))
                                .foregroundStyle(.primary)
                                .lineLimit(3)

                            Text("A focused read of the visible page.")
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }

                        Spacer(minLength: 0)

                        Button(action: onDismiss) {
                            Image(lucide: Lucide.circleX)
                                .font(.title2)
                                .foregroundStyle(.tertiary)
                                .frame(minWidth: ShellTheme.Size.touchTarget, minHeight: ShellTheme.Size.touchTarget)
                                .contentShape(Rectangle())
                        }
                        .accessibilityLabel("Close summary")
                    }

                    VStack(alignment: .leading, spacing: ShellTheme.Spacing.medium) {
                        Text("Key points")
                            .font(.subheadline.weight(.semibold))
                            .foregroundStyle(.primary)

                        VStack(alignment: .leading, spacing: ShellTheme.Spacing.medium) {
                            ForEach(Array(sentences.enumerated()), id: \.offset) { index, sentence in
                                HStack(alignment: .top, spacing: ShellTheme.Spacing.medium) {
                                    ZStack {
                                        Circle()
                                            .fill(index == 0 ? ShellTheme.Palette.summaryAccentFill : ShellTheme.Palette.summaryMutedFill)
                                            .frame(width: 20, height: 20)

                                        Circle()
                                            .fill(index == 0 ? ShellTheme.Palette.summaryAccent : Color.primary.opacity(0.22))
                                            .frame(width: 5, height: 5)
                                    }
                                    .padding(.top, 1)

                                    Text(sentence)
                                        .font(.subheadline)
                                        .foregroundStyle(.primary)
                                        .lineSpacing(2)
                                        .fixedSize(horizontal: false, vertical: true)
                                }
                                .padding(.vertical, ShellTheme.Spacing.small)

                                if index < sentences.count - 1 {
                                    Divider()
                                }
                            }
                        }
                    }
                    .padding(.vertical, ShellTheme.Spacing.small)

                    HStack {
                        Label { Text("Page context") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("doc.text")) }
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(.secondary)
                            .padding(.horizontal, ShellTheme.Spacing.medium)
                            .padding(.vertical, ShellTheme.Spacing.xSmall)
                            .background(
                                Capsule()
                                    .fill(ShellTheme.Palette.summaryMutedFill)
                            )

                        Spacer(minLength: 0)

                        Button(action: onDismiss) {
                            Text("Done")
                                .font(.subheadline.weight(.medium))
                                .padding(.horizontal, ShellTheme.Spacing.large)
                                .padding(.vertical, ShellTheme.Spacing.medium)
                                .background(
                                    ShellTheme.Palette.summaryAccentFill,
                                    in: RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                                )
                                .overlay(
                                    RoundedRectangle(cornerRadius: ShellTheme.Radius.button, style: .continuous)
                                        .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                                )
                                .foregroundStyle(ShellTheme.Palette.summaryAccent)
                        }
                        .buttonStyle(.plain)
                        .accessibilityIdentifier("pinchSummaryBackButton")
                    }
                }
                .padding(ShellTheme.Spacing.page)
                .background {
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                        .fill(cardBackground)
                        .background(
                            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                                .fill(ShellTheme.Materials.summaryOverlay)
                        )
                        .shadow(color: ShellTheme.Elevation.summaryOverlay.color, radius: ShellTheme.Elevation.summaryOverlay.radius, x: ShellTheme.Elevation.summaryOverlay.x, y: ShellTheme.Elevation.summaryOverlay.y)
                }
                .overlay {
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                        .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                }
                .frame(maxWidth: ShellTheme.Size.summaryDetentMaxWidth)
                .padding(.horizontal, ShellTheme.Spacing.large)
                .padding(.bottom, ShellTheme.Spacing.page)
                .accessibilityIdentifier("pinchSummaryView")

                Spacer()
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .environment(\.colorScheme, isIncognito ? .dark : systemColorScheme)
    }

    private var cardBackground: Color {
        isIncognito ? ShellTheme.Palette.incognitoBackground : ShellTheme.Palette.tabGridFloatingSurface
    }

    private var summaryIcon: some View {
        ZStack {
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Palette.summaryHeroFill)
                .frame(width: ShellTheme.Size.summaryStateIcon + 4, height: ShellTheme.Size.summaryStateIcon + 4)

            Image(lucide: Lucide.fileSearch)
                .font(.headline.weight(.semibold))
                .foregroundStyle(ShellTheme.Palette.summaryAccent)
        }
        .accessibilityHidden(true)
    }

    private var titleText: String {
        let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "This page" : trimmed
    }
}
