import SwiftUI
import LucideIcons

struct PrivateBanner: View {
    var body: some View {
        HStack(spacing: ShellTheme.Spacing.small) {
            Image(lucide: Lucide.eyeOff)
                .font(.caption2.weight(.semibold))
                .foregroundColor(.white)

            Text("Private Browsing")
                .font(.system(size: 12, weight: .semibold))
                .foregroundColor(.white)

            Spacer()
        }
        .padding(.horizontal, ShellTheme.Spacing.large)
        .frame(maxWidth: .infinity)
        .frame(height: 28)
        .background(ShellTheme.Palette.incognitoBackground)
        .accessibilityIdentifier("privateStateBanner")
    }
}
