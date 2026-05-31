import SwiftUI
import LucideIcons

struct TabGridToolbar: View {
    let tabCount: Int
    let onNewTab: () -> Void
    let onCloseAll: () -> Void
    let onDismiss: () -> Void

    var body: some View {
        HStack(spacing: ShellTheme.Spacing.medium) {
            if tabCount > 0 {
                Button(role: .destructive) {
                    onCloseAll()
                } label: {
                    Text("Close All")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
                .padding(.horizontal, ShellTheme.Spacing.medium)
                .padding(.vertical, ShellTheme.Spacing.small)
                .background(
                    Capsule()
                        .fill(ShellTheme.Palette.tabGridMetaFill)
                )
                .accessibilityIdentifier("tabGridCloseAllButton")
                .accessibilityLabel("Close all tabs")
                .accessibilityHint("Closes every tab in the current space.")
            }

            Spacer(minLength: 0)

            actionButton(
                icon: Lucide.plus,
                title: nil,
                identifier: "tabGridNewTabButton",
                label: "New tab",
                hint: "Creates a new tab in the current space."
            ) {
                onNewTab()
            }

            actionButton(
                icon: Lucide.x,
                title: nil,
                identifier: "tabGridDismissButton",
                label: "Done",
                hint: "Closes the tab switcher."
            ) {
                onDismiss()
            }
        }
        .padding(.horizontal, ShellTheme.Spacing.large)
        .padding(.top, ShellTheme.Spacing.small)
        .padding(.bottom, ShellTheme.Spacing.medium)
        .background(
            Color.clear
                .background(.ultraThinMaterial)
                .mask(
                    LinearGradient(
                        colors: [Color.clear, Color.black.opacity(0.22), Color.black],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
        )
    }

    private func actionButton(
        icon: UIImage,
        title: String?,
        identifier: String,
        label: String,
        hint: String,
        action: @escaping () -> Void
    ) -> some View {
        Button {
            action()
        } label: {
            actionLabel(icon: icon, title: title)
        }
        .buttonStyle(.plain)
        .accessibilityElement(children: .ignore)
        .accessibilityIdentifier(identifier)
        .accessibilityLabel(label)
        .accessibilityHint(hint)
    }

    private func actionLabel(icon: UIImage, title: String?) -> some View {
        VStack(spacing: 0) {
            Image(lucide: icon)
                .font(.body.weight(.semibold))
                .frame(width: ShellTheme.Size.touchTarget, height: ShellTheme.Size.touchTarget)
                .foregroundStyle(.primary)

            if let title {
                Text(title)
                    .font(.caption2.weight(.semibold))
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.horizontal, 2)
        .background(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBarControl, style: .continuous)
                .fill(ShellTheme.Palette.tabGridMetaFill)
        )
        .overlay(
            RoundedRectangle(cornerRadius: ShellTheme.Radius.bottomBarControl, style: .continuous)
                .strokeBorder(ShellTheme.Palette.overlayBorder, lineWidth: ShellTheme.Stroke.hairline)
        )
    }
}
