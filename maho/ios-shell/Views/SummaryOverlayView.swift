import SwiftUI
import LucideIcons

struct SummaryOverlayView: View {
    let state: PageSummarizer.State
    let onDismiss: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.large) {
            header
            stateContent
            footerAction
        }
        .padding(ShellTheme.Spacing.page)
        .frame(maxWidth: ShellTheme.Size.summaryDetentMaxWidth, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .fill(ShellTheme.Materials.summaryOverlay)
                .background(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                        .fill(ShellTheme.Palette.tabGridFloatingSurface)
                )
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.overlay, style: .continuous)
                .strokeBorder(ShellTheme.Palette.summarySurfaceBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
        .shadow(
            color: ShellTheme.Elevation.summaryOverlay.color,
            radius: ShellTheme.Elevation.summaryOverlay.radius,
            x: ShellTheme.Elevation.summaryOverlay.x,
            y: ShellTheme.Elevation.summaryOverlay.y
        )
        .padding(.horizontal, ShellTheme.Spacing.large)
        .accessibilityIdentifier("browseForMeOverlay")
    }

    private var header: some View {
        HStack(alignment: .top, spacing: ShellTheme.Spacing.medium) {
            summaryIcon(icon: heroIcon)

            VStack(alignment: .leading, spacing: 4) {
                Text("Browse for Me")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                    .textCase(.uppercase)
                    .tracking(0.7)

                Text(heroTitle)
                    .font(.title3.weight(.semibold))
                    .foregroundStyle(.primary)

                Text(heroSubtitle)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer(minLength: 0)

            Button {
                onDismiss()
            } label: {
                Image(lucide: Lucide.circleX)
                    .font(.title3)
                    .foregroundStyle(.secondary)
                    .frame(minWidth: ShellTheme.Size.touchTarget, minHeight: ShellTheme.Size.touchTarget)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Close summary")
            .accessibilityIdentifier("browseForMeDismissButton")
        }
        .accessibilityIdentifier("browseForMeHeroPanel")
        .accessibilityValue(heroTitle)
    }

    @ViewBuilder
    private var stateContent: some View {
        switch state {
        case .idle:
            EmptyView()

        case .loading:
            sectionCard(
                title: "Summarizing",
                subtitle: "Reading the current page.",
                accessibilityIdentifier: "browseForMeLoadingState"
            ) {
                HStack(alignment: .center, spacing: ShellTheme.Spacing.medium) {
                    ProgressView()

                    Text("Pulling together a shorter, factual view of the page.")
                        .font(.subheadline)
                        .foregroundStyle(.primary)
                        .fixedSize(horizontal: false, vertical: true)
                }

                capsuleProgressPlaceholder
            }

        case .result(let bullets):
            sectionCard(
                title: bullets.count == 1 ? "1 takeaway" : "\(bullets.count) takeaways",
                subtitle: "From the current page",
                accessibilityIdentifier: "browseForMeResultState"
            ) {
                takeawayList(bullets)
            }

        case .error(let message):
            sectionCard(
                title: "Can’t summarize this page",
                subtitle: "This answer could not be prepared.",
                accessibilityIdentifier: "browseForMeErrorState"
            ) {
                HStack(alignment: .top, spacing: ShellTheme.Spacing.medium) {
                    Image(lucide: Lucide.triangleAlert)
                        .foregroundStyle(ShellTheme.Palette.warning)

                    Text(message)
                        .font(.subheadline)
                        .foregroundStyle(.primary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
    }

    private var footerAction: some View {
        HStack {
            statusPill

            Spacer(minLength: 0)

            Button("Done", action: onDismiss)
                .font(.subheadline.weight(.semibold))
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
                .accessibilityIdentifier("browseForMeDoneButton")
        }
    }

    private var statusPill: some View {
        Label {
            Text(stateLabel)
        } icon: {
            Image(lucide: heroIcon)
        }
            .font(.caption.weight(.semibold))
            .foregroundStyle(.secondary)
            .padding(.horizontal, ShellTheme.Spacing.medium)
            .padding(.vertical, ShellTheme.Spacing.xSmall)
            .background(
                Capsule()
                    .fill(ShellTheme.Palette.summaryMutedFill)
            )
    }

    private var heroIcon: UIImage {
        switch state {
        case .loading:
            return Lucide.sparkles
        case .result:
            return Lucide.list
        case .error:
            return Lucide.triangleAlert
        case .idle:
            return Lucide.sparkles
        }
    }

    private var heroTitle: String {
        switch state {
        case .loading:
            return "Preparing summary"
        case .result(let bullets):
            return bullets.count == 1 ? "1 takeaway" : "\(bullets.count) takeaways"
        case .error:
            return "Summary unavailable"
        case .idle:
            return "Summary"
        }
    }

    private var heroSubtitle: String {
        switch state {
        case .loading:
            return "A shorter view of the page is on the way."
        case .result:
            return "Key points from the current page."
        case .error:
            return "This page doesn’t have enough readable text."
        case .idle:
            return "The page summary appears here."
        }
    }

    private var stateLabel: String {
        switch state {
        case .loading:
            return "Live"
        case .result:
            return "Ready"
        case .error:
            return "Issue"
        case .idle:
            return "Page"
        }
    }

    private var capsuleProgressPlaceholder: some View {
        HStack(spacing: ShellTheme.Spacing.small) {
            ForEach(0..<3, id: \.self) { index in
                Capsule()
                    .fill(progressCapsuleColor(at: index))
                    .frame(height: ShellTheme.Size.bottomBarGrabberHeight + CGFloat(index + 1))
            }
        }
    }

    private func progressCapsuleColor(at index: Int) -> Color {
        index == 1 ? ShellTheme.Palette.summaryAccentFill : ShellTheme.Palette.summaryMutedFill
    }

    private func sectionCard<Content: View>(
        title: String,
        subtitle: String,
        accessibilityIdentifier: String,
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.medium) {
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.primary)

                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            content()
        }
        .padding(.vertical, ShellTheme.Spacing.small)
        .accessibilityIdentifier(accessibilityIdentifier)
    }

    private func summaryIcon(icon: UIImage) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: ShellTheme.Radius.card, style: .continuous)
                .fill(ShellTheme.Palette.summaryHeroFill)
                .frame(width: ShellTheme.Size.summaryStateIcon + 2, height: ShellTheme.Size.summaryStateIcon + 2)

            Image(lucide: icon)
                .font(.headline.weight(.semibold))
                .foregroundStyle(ShellTheme.Palette.summaryAccent)
        }
        .accessibilityHidden(true)
    }

    @ViewBuilder
    private func takeawayList(_ bullets: [String]) -> some View {
        VStack(alignment: .leading, spacing: ShellTheme.Spacing.medium) {
            ForEach(Array(bullets.enumerated()), id: \.offset) { index, sentence in
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
                        .fixedSize(horizontal: false, vertical: true)
                }
                .padding(.vertical, ShellTheme.Spacing.small)

                if index < bullets.count - 1 {
                    Divider()
                }
            }
        }
    }
}
