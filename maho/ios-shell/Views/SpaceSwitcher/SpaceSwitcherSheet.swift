import SwiftUI
import LucideIcons

struct SpaceSwitcherSheet: View {
    @Environment(\.dismiss) private var dismiss

    @State private var spaces: [SpaceViewModel] = []
    @State private var activeSpaceId: SpaceId?
    @State private var showNewSpaceAlert = false
    @State private var newSpaceName = ""
    @State private var pendingDeleteSpace: SpaceViewModel?
    @State private var spaceErrorMessage: String?

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
                            pendingDeleteSpace = space
                        },
                        canDelete: spaces.count > 1
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
            .confirmationDialog(
                "Delete \"\(pendingDeleteSpace?.name ?? "Space")\"?",
                isPresented: Binding(
                    get: { pendingDeleteSpace != nil },
                    set: { if !$0 { pendingDeleteSpace = nil } }
                ),
                titleVisibility: .visible,
                presenting: pendingDeleteSpace
            ) { space in
                Button("Delete Space", role: .destructive) {
                    let updates = bridge.handleEvent(.deleteSpace(spaceId: space.id))
                    pendingDeleteSpace = nil
                    reloadSpaces()
                    if let message = Self.errorMessage(in: updates) {
                        spaceErrorMessage = message
                    }
                }
                Button("Cancel", role: .cancel) {
                    pendingDeleteSpace = nil
                }
            } message: { _ in
                Text("This space will be deleted and its tabs will move to another space.")
            }
            .alert(
                "Couldn't Delete Space",
                isPresented: Binding(
                    get: { spaceErrorMessage != nil },
                    set: { if !$0 { spaceErrorMessage = nil } }
                )
            ) {
                Button("OK", role: .cancel) { spaceErrorMessage = nil }
            } message: {
                Text(spaceErrorMessage ?? "")
            }
            .onAppear(perform: reloadSpaces)
        }
    }

    private static func errorMessage(in updates: [CoreUpdate]) -> String? {
        for update in updates {
            if case .error(_, let error) = update {
                return error.message
            }
        }
        return nil
    }

    private func reloadSpaces() {
        spaces = bridge.getSpaceViewModels()
        activeSpaceId = bridge.getActiveSpaceId()
    }
}
