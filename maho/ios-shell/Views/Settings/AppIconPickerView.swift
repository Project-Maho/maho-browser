import SwiftUI

struct AppIconPickerView: View {
    struct IconOption: Identifiable {
        let id: String
        let name: String
        let iconName: String
        let alternateIconName: String?
    }

    private let options: [IconOption] = [
        IconOption(id: "default", name: "Default", iconName: "app.fill", alternateIconName: nil),
        IconOption(id: "dark", name: "Dark", iconName: "moon.fill", alternateIconName: "AppIcon-Dark"),
        IconOption(id: "light", name: "Light", iconName: "sun.max.fill", alternateIconName: "AppIcon-Light"),
        IconOption(id: "minimal", name: "Minimal", iconName: "square.fill", alternateIconName: "AppIcon-Minimal"),
        IconOption(id: "neon", name: "Neon", iconName: "bolt.fill", alternateIconName: "AppIcon-Neon")
    ]

    @State private var currentIcon: String = UIApplication.shared.alternateIconName ?? "default"
    @State private var showError = false

    var body: some View {
        Form {
            Section {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 72))], spacing: 20) {
                    ForEach(options) { option in
                        Button {
                            setIcon(option)
                        } label: {
                            VStack(spacing: 8) {
                                ZStack {
                                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                                        .fill(currentIcon == option.id ? Color.accentColor.opacity(0.15) : Color.secondary.opacity(0.1))
                                        .frame(width: 72, height: 72)

                                    Image(systemName: option.iconName)
                                        .font(.system(size: 32))
                                        .foregroundStyle(currentIcon == option.id ? Color.accentColor : Color.primary)
                                }

                                Text(option.name)
                                    .font(.caption.weight(.medium))
                                    .foregroundStyle(currentIcon == option.id ? Color.accentColor : Color.primary)
                            }
                        }
                        .buttonStyle(.plain)
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
