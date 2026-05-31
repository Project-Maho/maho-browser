import SwiftUI
import LucideIcons

struct SpaceSwitcherSheet: View {
    @Environment(\.dismiss) private var dismiss

    @State private var spaces: [SpaceViewModel] = []
    @State private var activeSpaceId: SpaceId?
    @State private var showNewSpaceAlert = false
    @State private var newSpaceName = ""

    var onSpaceSelected: (() -> Void)?

    private let bridge = MahoBridge.shared

    private let defaultColors: [SpaceColor] = [
        SpaceColor(hue: 0.6, saturation: 0.7, brightness: 0.9),
        SpaceColor(hue: 0.35, saturation: 0.7, brightness: 0.8),
        SpaceColor(hue: 0.08, saturation: 0.8, brightness: 0.95),
        SpaceColor(hue: 0.0, saturation: 0.7, brightness: 0.9),
        SpaceColor(hue: 0.8, saturation: 0.6, brightness: 0.85),
    ]

    var body: some View {
        NavigationStack {
            List {
                ForEach(spaces) { space in
                    SpaceCardView(
                        space: space,
                        isActive: space.id == activeSpaceId,
                        onTap: {
                            bridge.activateSpace(id: space.id)
                            onSpaceSelected?()
                            dismiss()
                        },
                        onRename: { name in
                            bridge.renameSpace(id: space.id, name: name)
                            reloadSpaces()
                        },
                        onRecolor: { color in
                            bridge.recolorSpace(id: space.id, color: color)
                            reloadSpaces()
                        },
                        onDelete: {
                            bridge.deleteSpace(id: space.id)
                            reloadSpaces()
                        }
                    )
                    .listRowInsets(EdgeInsets(top: 4, leading: 8, bottom: 4, trailing: 8))
                }
            }
            .listStyle(.plain)
            .navigationTitle("Spaces")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItemGroup(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
                ToolbarItemGroup(placement: .primaryAction) {
                    Button {
                        newSpaceName = ""
                        showNewSpaceAlert = true
                    } label: {
                        Image(lucide: Lucide.plus)
                    }
                }
            }
            .alert("New Space", isPresented: $showNewSpaceAlert) {
                TextField("Space name", text: $newSpaceName)
                Button("Cancel", role: .cancel) {}
                Button("Create") {
                    guard !newSpaceName.isEmpty else { return }
                    let color = defaultColors.randomElement() ?? defaultColors[0]
                    let profileId = bridge.getActiveProfileId() ?? ""
                    bridge.createSpace(name: newSpaceName, color: color, profileId: profileId)
                    reloadSpaces()
                }
            } message: {
                Text("Enter a name for the new space.")
            }
            .onAppear(perform: reloadSpaces)
        }
    }

    private func reloadSpaces() {
        spaces = bridge.getSpaceViewModels()
        activeSpaceId = bridge.getActiveSpaceId()
    }
}
