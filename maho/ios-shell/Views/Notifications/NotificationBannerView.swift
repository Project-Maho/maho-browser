import SwiftUI
import LucideIcons

struct NotificationBannerView: View {
    let title: String
    let message: String
    let icon: UIImage
    var actions: [BannerAction] = []
    var isIncognito: Bool = false
    var onDismiss: () -> Void

    @Environment(\.colorScheme) private var systemColorScheme

    struct BannerAction: Identifiable {
        let id: String
        let label: String
        let handler: () -> Void
    }

    var body: some View {
        HStack(spacing: 12) {
            Image(lucide: icon)
                .font(.title2)
                .foregroundStyle(Color.accentColor)
                .frame(width: 32)

            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(1)

                Text(message)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }

            Spacer()

            if actions.isEmpty {
                Button {
                    onDismiss()
                } label: {
                    Image(lucide: Lucide.x)
                        .font(.caption.weight(.medium))
                        .foregroundStyle(.secondary)
                        .frame(minWidth: ShellTheme.Size.touchTarget, minHeight: ShellTheme.Size.touchTarget)
                        .contentShape(Rectangle())
                }
                .accessibilityLabel("Dismiss notification")
            } else {
                ForEach(actions) { action in
                    Button(action.label) {
                        action.handler()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                    .frame(minHeight: ShellTheme.Size.touchTarget)
                    .contentShape(Rectangle())
                }
            }
        }
        .padding(12)
        .background(bannerBackground)
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .shadow(color: .black.opacity(0.1), radius: 8, y: 4)
        .padding(.horizontal, 16)
        .environment(\.colorScheme, isIncognito ? .dark : systemColorScheme)
    }

    @ViewBuilder
    private var bannerBackground: some View {
        if isIncognito {
            ShellTheme.Palette.incognitoBackground
        } else {
            Rectangle().fill(.ultraThinMaterial)
        }
    }
}
