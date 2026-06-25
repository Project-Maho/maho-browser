import SwiftUI
import LucideIcons

struct UpdateBannerView: View {
    @ObservedObject var checker: AppStoreUpdateChecker

    var body: some View {
        if checker.updateAvailable, let version = checker.latestVersion {
            HStack {
                Image(lucide: Lucide.circleArrowDown)
                    .foregroundStyle(.white)
                Text("Maho \(version) is available")
                    .font(.subheadline.weight(.medium))
                    .foregroundStyle(.white)
                Spacer()
                if let url = checker.updateURL {
                    Link("Update", destination: url)
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.white)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 4)
                        .background(.white.opacity(0.25))
                        .clipShape(Capsule())
                }
                Button {
                    withAnimation { checker.updateAvailable = false }
                } label: {
                    Image(lucide: Lucide.x)
                        .foregroundStyle(.white.opacity(0.8))
                }
                .buttonStyle(.plain)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(Color.accentColor.gradient)
            .transition(.move(edge: .top).combined(with: .opacity))
        }
    }
}
