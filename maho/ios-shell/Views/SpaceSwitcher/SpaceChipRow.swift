import SwiftUI
import LucideIcons

struct SpaceChipRow: View {
    let spaces: [SpaceViewModel]
    let activeSpaceId: SpaceId?
    let onSelectSpace: (SpaceId) -> Void
    let onShowAll: () -> Void

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 8) {
                ForEach(spaces) { space in
                    spaceChip(space)
                }

                Button(action: onShowAll) {
                    Image(lucide: Lucide.ellipsis)
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(
                            Capsule()
                                .fill(Color(.systemGray5))
                        )
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 8)
        }
    }

    private func spaceChip(_ space: SpaceViewModel) -> some View {
        let isActive = space.id == activeSpaceId
        let chipColor = Color(hue: space.color.hue, saturation: space.color.saturation, brightness: space.color.brightness)

        return Button {
            onSelectSpace(space.id)
        } label: {
            HStack(spacing: 4) {
                Circle()
                    .fill(chipColor)
                    .frame(width: 8, height: 8)

                Text(space.name)
                    .font(.caption.weight(.medium))
                    .lineLimit(1)
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(
                Capsule()
                    .fill(isActive ? chipColor.opacity(0.15) : Color(.systemGray6))
            )
            .overlay(
                Capsule()
                    .strokeBorder(isActive ? chipColor : Color.clear, lineWidth: 1.5)
            )
        }
        .buttonStyle(.plain)
    }
}
