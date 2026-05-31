import SwiftUI
import LucideIcons

struct SpaceCardView: View {
    let space: SpaceViewModel
    let isActive: Bool
    let onTap: () -> Void
    let onRename: (String) -> Void
    let onRecolor: (SpaceColor) -> Void
    let onDelete: () -> Void

    @State private var isEditing = false
    @State private var editedName: String = ""

    private var spaceColor: Color {
        Color(hue: space.color.hue, saturation: space.color.saturation, brightness: space.color.brightness)
    }

    var body: some View {
        Button(action: onTap) {
            HStack(spacing: 12) {
                Circle()
                    .fill(spaceColor)
                    .frame(width: 32, height: 32)
                    .overlay {
                        if let icon = space.icon {
                            Text(icon)
                                .font(.system(size: 14))
                        }
                    }

                VStack(alignment: .leading, spacing: 2) {
                    if isEditing {
                        TextField("Space Name", text: $editedName, onCommit: {
                            if !editedName.isEmpty {
                                onRename(editedName)
                            }
                            isEditing = false
                        })
                        .textFieldStyle(.plain)
                        .font(.body.weight(.medium))
                    } else {
                        Text(space.name)
                            .font(.body)
                            .fontWeight(.medium)
                    }

                    Text("\(space.tabCount) \(space.tabCount == 1 ? "tab" : "tabs")")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Spacer()

                if isActive {
                    Image(lucide: Lucide.circleCheck)
                        .foregroundStyle(spaceColor)
                }
            }
            .padding(.vertical, 8)
            .padding(.horizontal, 12)
            .background(
                RoundedRectangle(cornerRadius: 10)
                    .fill(isActive ? spaceColor.opacity(0.1) : Color.clear)
            )
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button {
                editedName = space.name
                isEditing = true
            } label: {
                Label { Text("Rename") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("pencil")) }
            }

            Divider()

            Button(role: .destructive, action: onDelete) {
                Label { Text("Delete") } icon: { Image(lucide: MahoIcon.imageForSFSymbol("trash")) }
            }
        }
    }
}
