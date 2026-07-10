import SwiftUI
import LucideIcons

struct MahoMenuSheet: View {
    var onAction: (MenuAction) -> Void

    var body: some View {
        NavigationView {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    menuSection(title: "Actions", actions: [
                        .newTab, .newPrivateTab, .reload, .sharePage, .findInPage, .desktopSite, .readerMode
                    ])
                    
                    menuSection(title: "Library", actions: [
                        .bookmarks, .history, .downloads, .readingList, .notes
                    ])

                    menuSection(title: "Account", actions: [
                        .signIn, .profileSwitcher
                    ])

                    menuSection(title: "App", actions: [
                        .settings
                    ])
                }
                .padding(.vertical, 16)
            }
            .navigationTitle("Menu")
            .navigationBarTitleDisplayMode(.inline)
            .background(Color(uiColor: .systemGroupedBackground))
        }
        .presentationDetents([.medium, .large])
    }

    private func menuSection(title: String, actions: [MenuAction]) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(title.uppercased())
                .font(.system(size: 13, weight: .semibold))
                .foregroundColor(.secondary)
                .padding(.horizontal, 16)
                .padding(.bottom, 6)

            VStack(spacing: 0) {
                ForEach(Array(actions.indices), id: \.self) { index in
                    let action = actions[index]
                    
                    Button(action: { onAction(action) }) {
                        HStack(spacing: 12) {
                            Image(lucide: action.lucideIcon)
                                .resizable()
                                .aspectRatio(contentMode: .fit)
                                .frame(width: 20, height: 20)
                                .foregroundColor(.primary)

                            Text(action.title)
                                .font(.system(size: 17))
                                .foregroundColor(.primary)

                            Spacer()

                            if shouldShowChevron(for: action) {
                                Image(lucide: Lucide.chevronRight)
                                    .resizable()
                                    .aspectRatio(contentMode: .fit)
                                    .frame(width: 12, height: 12)
                                    .foregroundColor(.secondary)
                            }
                        }
                        .frame(height: 44)
                        .padding(.horizontal, 16)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(MenuRowButtonStyle())

                    if index < actions.count - 1 {
                        Divider()
                            .padding(.leading, 48)
                    }
                }
            }
            .background(Color(uiColor: .secondarySystemGroupedBackground))
            .cornerRadius(10)
            .padding(.horizontal, 16)
        }
    }

    private func shouldShowChevron(for action: MenuAction) -> Bool {
        switch action {
        case .bookmarks, .history, .downloads, .readingList, .notes, .profileSwitcher, .settings:
            return true
        default:
            return false
        }
    }
}

struct MenuRowButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? Color(uiColor: .opaqueSeparator) : Color.clear)
    }
}
