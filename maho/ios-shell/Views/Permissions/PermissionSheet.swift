import SwiftUI
import LucideIcons

struct PermissionSheet: View {
    let permissionType: String
    let origin: String
    var isIncognito: Bool = false
    var onGrant: () -> Void
    var onDeny: () -> Void

    @Environment(\.dismiss) private var dismiss
    @Environment(\.colorScheme) private var systemColorScheme

    var body: some View {
        ScrollView {
            VStack(spacing: 24) {
                Image(lucide: permissionIcon)
                    .font(.system(size: 48))
                    .foregroundStyle(Color.accentColor)
                    .padding(.top, 24)

                Text("\"\(origin)\" wants to access your \(permissionType)")
                    .font(.headline)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal)

                Text(permissionDescription)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal)

                HStack(spacing: 16) {
                    Button("Deny") {
                        onDeny()
                        dismiss()
                    }
                    .buttonStyle(.bordered)

                    Button("Allow") {
                        onGrant()
                        dismiss()
                    }
                    .buttonStyle(.borderedProminent)
                }
                .padding(.bottom, 24)
            }
            .frame(maxWidth: .infinity)
        }
        .background(permissionBackground)
        .presentationDetents([.medium])
        .environment(\.colorScheme, isIncognito ? .dark : systemColorScheme)
    }

    @ViewBuilder
    private var permissionBackground: some View {
        if isIncognito {
            ShellTheme.Palette.incognitoBackground.ignoresSafeArea()
        } else {
            Color.clear
        }
    }

    private var permissionIcon: UIImage {
        switch permissionType {
        case "camera": return Lucide.camera
        case "microphone": return Lucide.mic
        case "location": return Lucide.mapPin
        case "notifications": return Lucide.bell
        default: return Lucide.shieldCheck
        }
    }

    private var permissionDescription: String {
        switch permissionType {
        case "camera":
            return "This site wants to use your camera. You can change this later in Privacy settings."
        case "microphone":
            return "This site wants to use your microphone. You can change this later in Privacy settings."
        case "location":
            return "This site wants to know your location. You can change this later in Privacy settings."
        case "notifications":
            return "This site wants to send you notifications. You can change this later in Notification settings."
        default:
            return "This site is requesting a permission. You can change this later in settings."
        }
    }
}
