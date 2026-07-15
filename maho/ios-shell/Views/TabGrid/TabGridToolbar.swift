import SwiftUI
import LucideIcons

struct TabGridToolbar: View {
    let tabCount: Int
    let activeSpaceName: String
    let onCloseAll: () -> Void
    let onDone: () -> Void

    var body: some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            VStack(alignment: .leading, spacing: ShellTheme.Spacing.badgeVertical) {
                Text(activeSpaceName)
                    .font(.system(size: 9, weight: .semibold))
                    .foregroundStyle(.secondary)
                Text("\(tabCount) tab\(tabCount != 1 ? "s" : "")")
                    .font(.caption2.weight(.bold))
                    .foregroundStyle(.primary)
                    .padding(.horizontal, ShellTheme.Spacing.small)
                    .padding(.vertical, ShellTheme.Spacing.xxSmall)
                    .background(Color.primary.opacity(0.12), in: Capsule())
            }

            Spacer(minLength: 0)

            HStack(spacing: ShellTheme.Spacing.small) {
                if tabCount > 1 {
                    Button(role: .destructive, action: onCloseAll) {
                        HStack(spacing: 4) {
                            Image(lucide: Lucide.trash2)
                                .font(.body.weight(.bold))
                            Text("Close all")
                                .font(.caption.weight(.semibold))
                        }
                        .foregroundStyle(.secondary)
                        .padding(.horizontal, ShellTheme.Spacing.medium)
                        .padding(.vertical, ShellTheme.Spacing.small)
                        .background(
                            Capsule()
                                .fill(ShellTheme.Palette.tabGridMetaFill)
                        )
                    }
                    .buttonStyle(.plain)
                    .accessibilityIdentifier("tabGridCloseAllButton")
                }

                Button(action: onDone) {
                    Text("Done")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(ShellTheme.Palette.accent)
                        .padding(.horizontal, ShellTheme.Spacing.medium)
                        .padding(.vertical, ShellTheme.Spacing.small)
                        .background(
                            Capsule()
                                .fill(ShellTheme.Palette.tabGridMetaFill)
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
                .fill(ShellTheme.Palette.tabCardSurface.opacity(0.74))
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBar, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBar, style: .continuous)
                        .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
                )
        )
    }
}
