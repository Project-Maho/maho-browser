import SwiftUI

struct NotificationBannerView: View {
    let title: String
    let message: String
    let iconName: String
    var actions: [BannerAction] = []
    var onDismiss: () -> Void

    struct BannerAction: Identifiable {
        let id: String
        let label: String
        let handler: () -> Void
    }

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: iconName)
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
                    Image(systemName: "xmark")
                        .font(.caption.weight(.medium))
                        .foregroundStyle(.secondary)
                }
            } else {
                ForEach(actions) { action in
                    Button(action.label) {
                        action.handler()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
            }
        }
        .padding(12)
        .background(.ultraThinMaterial)
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .shadow(color: .black.opacity(0.1), radius: 8, y: 4)
        .padding(.horizontal, 16)
    }
}
