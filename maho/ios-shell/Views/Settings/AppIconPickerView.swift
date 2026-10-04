import SwiftUI
import LucideIcons

struct AppIconPickerView: View {
    struct IconOption: Identifiable {
        let id: String
        let name: String
        let icon: UIImage
        let alternateIconName: String?
    }

    private let options: [IconOption] = [
        IconOption(id: "default", name: "Default", icon: Lucide.appWindow, alternateIconName: nil),
        IconOption(id: "dark", name: "Dark", icon: Lucide.moon, alternateIconName: "AppIcon-Dark"),
        IconOption(id: "light", name: "Light", icon: Lucide.sun, alternateIconName: "AppIcon-Light"),
        IconOption(id: "minimal", name: "Minimal", icon: Lucide.square, alternateIconName: "AppIcon-Minimal"),
        IconOption(id: "neon", name: "Neon", icon: Lucide.zap, alternateIconName: "AppIcon-Neon")
    ]

    @State private var currentIcon: String = UIApplication.shared.alternateIconName ?? "default"
    @State private var showError = false

    var body: some View {
        Form {
            Section {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 72))], spacing: 20) {
                    ForEach(options) { option in
                        let isSelected = currentIcon == option.id
                        Button {
                            setIcon(option)
                        } label: {
                            VStack(spacing: 8) {
                                ZStack {
                                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                                        .fill(isSelected ? Color.accentColor.opacity(0.15) : Color.secondary.opacity(0.1))
                                        .frame(width: 72, height: 72)

                                    Image(lucide: option.icon)
                                        .font(.system(size: 32))
                                        .foregroundStyle(isSelected ? Color.accentColor : Color.primary)

                                    if isSelected {
                                        RoundedRectangle(cornerRadius: 16, style: .continuous)
                                            .strokeBorder(Color.accentColor, lineWidth: 2)
                                            .frame(width: 72, height: 72)

                                        Image(systemName: "checkmark.circle.fill")
                                            .font(.system(size: 20))
                                            .foregroundStyle(Color.accentColor, Color(.systemBackground))
                                            .offset(x: 28, y: -28)
                                    }
                                }

                                Text(option.name)
                                    .font(.caption.weight(.medium))
                                    .foregroundStyle(isSelected ? Color.accentColor : Color.primary)
                            }
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel(option.name)
                        .accessibilityValue(isSelected ? "Selected" : "")
                        .accessibilityAddTraits(isSelected ? .isSelected : [])
                    }
                }
                .padding(.vertical, 8)
            } header: {
                Text("Choose an App Icon")
            }
        }
        .navigationTitle("App Icon")
        .alert("Unable to Change Icon", isPresented: $showError) {
            Button("OK", role: .cancel) {}
        } message: {
            Text("The selected icon could not be applied. Make sure alternate icon assets are included in the app bundle.")
        }
    }

    private func setIcon(_ option: IconOption) {
        guard currentIcon != option.id else { return }

        UIApplication.shared.setAlternateIconName(option.alternateIconName) { error in
            if error != nil {
                showError = true
            } else {
                currentIcon = option.id
            }
        }
    }
}
