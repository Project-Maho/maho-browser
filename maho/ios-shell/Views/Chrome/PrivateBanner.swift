import SwiftUI
import LucideIcons

struct PrivateBanner: View {
    var onDismiss: () -> Void

    var body: some View {
        HStack(spacing: 8) {
            Image(lucide: Lucide.eyeOff)
                .font(.footnote.weight(.semibold))
                .foregroundColor(.white)

            Text("Private Browsing")
                .font(.system(size: 13, weight: .bold))
                .foregroundColor(.white)

            Spacer()

            Button(action: onDismiss) {
                Image(lucide: Lucide.x)
                    .font(.caption.weight(.bold))
                    .foregroundColor(.white.opacity(0.8))
                    .frame(width: 24, height: 24)
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier("exitPrivateButton")
        }
        .padding(.horizontal, 16)
        .frame(height: 36)
        .background(
            Capsule()
                .fill(Color(uiColor: .systemIndigo))
        )
        .padding(.horizontal, 16)
        .padding(.top, 4)
        .accessibilityIdentifier("privateStateBanner")
    }
}
