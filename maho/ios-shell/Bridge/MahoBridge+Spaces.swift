import Foundation

extension MahoBridge {

    func getSpaceViewModels() -> [SpaceViewModel] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_space_view_models(ptr))
        } ?? nil) ?? []
    }

    func getActiveSpaceId() -> SpaceId? {
        withCore { ptr in
            FFIString.consumeJSON(maho_core_get_active_space_id(ptr))
        } ?? nil
    }

    func createSpace(name: String, color: SpaceColor, profileId: ProfileId) {
        sendEvent(.createSpace(name: name, color: color, profileId: profileId))
    }

    func deleteSpace(id spaceId: SpaceId) {
        sendEvent(.deleteSpace(spaceId: spaceId))
    }

    func activateSpace(id spaceId: SpaceId) {
        sendEvent(.activateSpace(spaceId: spaceId))
    }

    func renameSpace(id spaceId: SpaceId, name: String) {
        sendEvent(.renameSpace(spaceId: spaceId, name: name))
    }

    func recolorSpace(id spaceId: SpaceId, color: SpaceColor) {
        sendEvent(.recolorSpace(spaceId: spaceId, color: color))
    }

    func reorderSpace(id spaceId: SpaceId, from: Int, to: Int) {
        sendEvent(.reorderSpace(spaceId: spaceId, from: from, to: to))
    }
}
