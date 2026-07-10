import SwiftUI

struct HomeSearchView: View {
    let activeSpaceName: String
    let activeSpaceTabCount: Int
    let recentTabs: [TabViewModel]
    let topSites: [TabViewModel]
    let showsUnderlyingPage: Bool
    let onSelectSite: (String) -> Void
    let onResumeTab: (TabId) -> Void

    var body: some View {
        GeometryReader { proxy in
            VStack {
                Spacer()
                
                // Centered Hero Brand Identity
                VStack(spacing: 12) {
                    Image(systemName: "globe.badge.chevron.backward")
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(width: 60, height: 60)
                        .foregroundColor(.blue)
                    
                    Text("Maho")
                        .font(.system(size: 28, weight: .bold, design: .rounded))
                        .foregroundColor(.primary)
                    
                    Text("A beautiful, fast browser.")
                        .font(.system(size: 16, weight: .medium))
                        .foregroundColor(.secondary)
                    
                    Text("↓ Tap the search bar to begin")
                        .font(.system(size: 13, weight: .regular))
                        .foregroundColor(.secondary)
                        .opacity(0.7)
                        .padding(.top, 8)
                }
                .frame(maxWidth: .infinity)
                
                Spacer()
            }
            .frame(width: proxy.size.width, height: proxy.size.height)
            .background(homeBackground)
        }
        .accessibilityIdentifier("homeSearchView")
    }

    private var homeBackground: some View {
        Group {
            if showsUnderlyingPage {
                ShellTheme.Palette.homeSurfaceFill(showsUnderlyingPage: true)
            } else {
                Color.clear
            }
        }
        .ignoresSafeArea()
    }
}
