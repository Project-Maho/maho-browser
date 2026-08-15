import SwiftUI
import LucideIcons

struct TabGridToolbar: View {
    let tabCount: Int
    let activeSpaceName: String
    let isIncognito: Bool
    let onCloseAll: () -> Void
    let onDone: () -> Void

    @State private var showCloseAllConfirmation = false

    private var spaceNameColor: Color {
        isIncognito ? ShellTheme.Palette.incognitoMutedForeground : .secondary
    }

    private var tabCountColor: Color {
        isIncognito ? ShellTheme.Palette.incognitoForeground : .primary
    }

    private var countCapsuleFill: Color {
        isIncognito ? ShellTheme.Palette.incognitoSurfaceStrong : Color.primary.opacity(0.12)
    }

    private var controlFill: Color {
        isIncognito ? ShellTheme.Palette.incognitoSurface : ShellTheme.Palette.tabGridMetaFill
    }

    private var doneForeground: Color {
        isIncognito ? ShellTheme.Palette.incognitoForeground : ShellTheme.Palette.accent
    }

    private var toolbarSurface: Color {
        isIncognito ? ShellTheme.Palette.incognitoBackground.opacity(0.92) : ShellTheme.Palette.tabCardSurface.opacity(0.74)
    }

    private var toolbarBorder: Color {
        isIncognito ? ShellTheme.Palette.incognitoBorder : ShellTheme.Palette.overlayBorder
    }

    var body: some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            VStack(alignment: .leading, spacing: ShellTheme.Spacing.badgeVertical) {
                Text(activeSpaceName)
                    .font(.system(size: 9, weight: .semibold))
                    .foregroundStyle(spaceNameColor)
                Text("\(tabCount) tab\(tabCount != 1 ? "s" : "")")
                    .font(.caption2.weight(.bold))
                    .foregroundStyle(tabCountColor)
                    .padding(.horizontal, ShellTheme.Spacing.small)
                    .padding(.vertical, ShellTheme.Spacing.xxSmall)
                    .background(countCapsuleFill, in: Capsule())
            }

            Spacer(minLength: 0)

            HStack(spacing: ShellTheme.Spacing.small) {
                if tabCount > 1 {
                    Button(role: .destructive) {
                        showCloseAllConfirmation = true
                    } label: {
                        HStack(spacing: 4) {
                            Image(lucide: Lucide.trash2)
                                .font(.body.weight(.bold))
                            Text("Close all")
                                .font(.caption.weight(.semibold))
                        }
                        .foregroundStyle(.red)
                        .padding(.horizontal, ShellTheme.Spacing.medium)
                        .padding(.vertical, ShellTheme.Spacing.small)
                        .background(
                            Capsule()
                                .fill(controlFill)
                        )
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("tabGridCloseAllButton")
                    .confirmationDialog(
                        "Close all tabs?",
                        isPresented: $showCloseAllConfirmation,
                        titleVisibility: .visible
                    ) {
                        Button("Close all", role: .destructive, action: onCloseAll)
                        Button("Cancel", role: .cancel) {}
                    }
                }

                Button(action: onDone) {
                    Text("Done")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(doneForeground)
                        .padding(.horizontal, ShellTheme.Spacing.medium)
                        .padding(.vertical, ShellTheme.Spacing.small)
                        .background(
                            Capsule()
                                .fill(controlFill)
                        )
                }
                .buttonStyle(.plain)
                .accessibilityIdentifier("tabGridDoneButton")
            }
        }
        .padding(.horizontal, ShellTheme.Spacing.large)
        .padding(.vertical, ShellTheme.Spacing.small)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBar, style: .continuous)
                .fill(toolbarSurface)
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBar, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBar, style: .continuous)
                        .strokeBorder(toolbarBorder, lineWidth: ShellTheme.Stroke.hairline)
                )
        )
    }
}
