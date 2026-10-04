import Foundation

extension MahoBridge {

    func getTabViewModels() -> [TabViewModel] {
        (withCore { ptr in
            FFIString.consumeJSON(maho_core_get_tab_view_models(ptr))
        } ?? nil) ?? []
    }

    func getActiveTabId() -> TabId? {
        let tabs = getTabViewModels()
        guard let activeSpaceId = getActiveSpaceId() else { return nil }
        let spaceTabs = tabs.filter { $0.spaceId == activeSpaceId }
        return spaceTabs.sorted(by: { $0.lastActiveAt > $1.lastActiveAt }).first?.id
    }

    func createTab(url: Url?, inSpace spaceId: SpaceId, parentId: TabId? = nil, isPrivate: Bool = false) {
        sendEvent(.createTab(spaceId: spaceId, url: url, parentId: parentId, isPrivate: isPrivate))
    }

    func closeTab(id tabId: TabId) {
        sendEvent(.closeTab(tabId: tabId))
    }

    func archiveTab(id tabId: TabId) {
        sendEvent(.archiveTabById(tabId: tabId))
    }

    func activateTab(id tabId: TabId) {
        sendEvent(.activateTab(tabId: tabId))
    }

    func reorderTab(id tabId: TabId, beforeTabId: TabId?) {
        sendEvent(.reorderTab(tabId: tabId, beforeTabId: beforeTabId))
    }

    func setTabParent(tabId: TabId, newParentId: TabId?) {
        sendEvent(.setTabParent(tabId: tabId, newParentId: newParentId))
    }

    func moveTab(tabId: TabId, targetSpace: SpaceId, position: Int) {
        sendEvent(.moveTab(tabId: tabId, targetSpace: targetSpace, position: position))
    }

    func duplicateTab(tabId: TabId) {
        sendEvent(.duplicateTab(tabId: tabId))
    }

    func pinTab(tabId: TabId) {
        sendEvent(.pinTab(tabId: tabId))
    }

    func unpinTab(tabId: TabId) {
        sendEvent(.unpinTab(tabId: tabId))
    }

    func muteTab(tabId: TabId) {
        sendEvent(.muteTab(tabId: tabId))
    }

    func unmuteTab(tabId: TabId) {
        sendEvent(.unmuteTab(tabId: tabId))
    }

    func closeOtherTabs(spaceId: SpaceId, keepTabId tabId: TabId) {
        sendEvent(.closeOtherTabs(spaceId: spaceId, tabId: tabId))
    }

    func getArchivedTabs(spaceId: SpaceId) -> String? {
        guard let jsonString = try? spaceId.mahoJSONString() else {
            return nil
        }
        return withCore { ptr in
            jsonString.withCString { cStr in
                FFIString.consume(maho_core_get_archived_tabs(ptr, cStr))
            }
        } ?? nil
    }

    func getFavoriteTabs(spaceId: SpaceId) -> [TabViewModel] {
        guard let jsonString = try? spaceId.mahoJSONString() else {
            return []
        }
        return (withCore { ptr in
            jsonString.withCString { cStr in
                FFIString.consumeJSON(maho_core_get_favorite_tabs(ptr, cStr))
            }
        } ?? nil) ?? []
    }
}
