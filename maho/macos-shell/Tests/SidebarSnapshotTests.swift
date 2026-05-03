import Foundation

// MARK: - Helpers

func makeTab(id: TabId, title: String, isPinned: Bool = false, isFavorite: Bool = false) -> TabViewModel {
    TabViewModel(
        id: id,
        spaceId: "space-1",
        title: title,
        customTitle: nil,
        url: "https://example.com/\(id)",
        favicon: nil,
        isLoading: false,
        isPinned: isPinned,
        isFavorite: isFavorite,
        favoriteOrder: nil,
        isMuted: false,
        isPlayingAudio: false,
        lifecycleState: "active",
        children: [],
        createdAt: MahoDateTime("2024-01-01T00:00:00Z"),
        lastActiveAt: MahoDateTime("2024-01-01T00:00:00Z")
    )
}

func makeFolder(id: FolderId, name: String, tabIds: [TabId] = [], isPinned: Bool = false, isExpanded: Bool = true) -> FolderViewModel {
    FolderViewModel(
        id: id,
        name: name,
        tabCount: tabIds.count,
        isExpanded: isExpanded,
        tabIds: tabIds,
        isPinned: isPinned,
        parentFolderId: nil
    )
}

// MARK: - Tests

func testSectionCount() {
    let favTab = makeTab(id: "fav-1", title: "Favorite", isFavorite: true)
    let pinTab = makeTab(id: "pin-1", title: "Pinned", isPinned: true)
    let regTab = makeTab(id: "reg-1", title: "Regular")

    let snapshot = SidebarSnapshot.build(
        favoritesTabs: [favTab],
        pinnedTabs: [pinTab],
        regularTabs: [regTab],
        folders: [],
        activeSpaceId: "space-1"
    )

    assertEqual(snapshot.rootSections.count, 4)
    assertEqual(snapshot.rootSections[0].sectionId, .pinned)
    assertEqual(snapshot.rootSections[1].sectionId, .separator)
    assertEqual(snapshot.rootSections[2].sectionId, .newTab)
    assertEqual(snapshot.rootSections[3].sectionId, .regular)
}

func testSnapshotContainsOnlyPinnedAndRegularSections() {
    let snapshot = SidebarSnapshot.build(
        favoritesTabs: [makeTab(id: "fav-1", title: "Favorite", isFavorite: true)],
        pinnedTabs: [makeTab(id: "pin-1", title: "Pinned", isPinned: true)],
        regularTabs: [makeTab(id: "reg-1", title: "Regular")],
        folders: [],
        activeSpaceId: "space-1"
    )

    let sectionIds = snapshot.rootSections.map(\.sectionId)
    assertEqual(sectionIds, [.pinned, .separator, .newTab, .regular])
    assertEqual(sectionIndex(in: snapshot, sectionId: .favorites), -1)
}

func testTabNodesInCorrectSections() {
    let favTab = makeTab(id: "fav-1", title: "Favorite", isFavorite: true)
    let pinTab = makeTab(id: "pin-1", title: "Pinned", isPinned: true)
    let regTab = makeTab(id: "reg-1", title: "Regular")

    let snapshot = SidebarSnapshot.build(
        favoritesTabs: [favTab],
        pinnedTabs: [pinTab],
        regularTabs: [regTab],
        folders: [],
        activeSpaceId: "space-1"
    )

    assertEqual(sectionIndex(in: snapshot, sectionId: .favorites), -1)

    let pinSection = snapshot.rootSections[0]
    assertEqual(pinSection.children.count, 1)
    assertTrue(pinSection.children[0] is TabNode)
    assertEqual((pinSection.children[0] as! TabNode).tabId, "pin-1")

    let regIdx = lastRegularSectionIndex(in: snapshot)
    let regSection = snapshot.rootSections[regIdx]
    assertEqual(regSection.children.count, 1)
    assertTrue(regSection.children[0] is TabNode)
    assertEqual((regSection.children[0] as! TabNode).tabId, "reg-1")

    assertEqual(regSection.title, "Normal")
}

func testFolderNodesContainTheirTabs() {
    let pinTab = makeTab(id: "pin-1", title: "Pinned", isPinned: true)
    let regTab = makeTab(id: "reg-1", title: "Regular")
    let extraRegTab = makeTab(id: "reg-2", title: "Extra Regular")

    let pinFolder = makeFolder(id: "fold-pin", name: "Pinned Folder", tabIds: ["pin-1"], isPinned: true)
    let regFolder = makeFolder(id: "fold-reg", name: "Regular Folder", tabIds: ["reg-1"], isPinned: false)

    let snapshot = SidebarSnapshot.build(
        favoritesTabs: [],
        pinnedTabs: [pinTab],
        regularTabs: [regTab, extraRegTab],
        folders: [pinFolder, regFolder],
        activeSpaceId: "space-1"
    )

    let pinIdx = sectionIndex(in: snapshot, sectionId: .pinned)
    let regIdx = lastRegularSectionIndex(in: snapshot)

    // Pinned section contains pinned folder + any pinned tabs not in folder
    let pinSection = snapshot.rootSections[pinIdx]
    assertEqual(pinSection.children.count, 1)
    let pinFolderNode = pinSection.children[0] as! SectionNode
    assertTrue(pinFolderNode.isFolder)
    assertEqual(pinFolderNode.folderId, "fold-pin")
    assertEqual(pinFolderNode.children.count, 1)
    assertEqual((pinFolderNode.children[0] as! TabNode).tabId, "pin-1")

    // Regular section contains regular folder + any regular tabs not in folder
    let regSection = snapshot.rootSections[regIdx]
    assertEqual(regSection.children.count, 2)
    let regFolderNode = regSection.children[0] as! SectionNode
    assertTrue(regFolderNode.isFolder)
    assertEqual(regFolderNode.folderId, "fold-reg")
    assertEqual(regFolderNode.children.count, 1)
    assertEqual((regFolderNode.children[0] as! TabNode).tabId, "reg-1")
    let extraNode = regSection.children[1] as! TabNode
    assertEqual(extraNode.tabId, "reg-2")
}

func sectionIndex(in snapshot: SidebarSnapshot, sectionId: SectionID) -> Int {
    for (index, section) in snapshot.rootSections.enumerated() {
        if section.sectionId == sectionId {
            return index
        }
    }
    return -1
}

func lastRegularSectionIndex(in snapshot: SidebarSnapshot) -> Int {
    return sectionIndex(in: snapshot, sectionId: .regular)
}

func testNodeIdentityAcrossBuilds() {
    let regTab = makeTab(id: "reg-1", title: "Regular")

    let snapshot1 = SidebarSnapshot.build(
        favoritesTabs: [],
        pinnedTabs: [],
        regularTabs: [regTab],
        folders: [],
        activeSpaceId: "space-1"
    )

    let snapshot2 = SidebarSnapshot.build(
        favoritesTabs: [],
        pinnedTabs: [],
        regularTabs: [regTab],
        folders: [],
        activeSpaceId: "space-1"
    )

    let idx1 = lastRegularSectionIndex(in: snapshot1)
    let idx2 = lastRegularSectionIndex(in: snapshot2)
    let node1 = snapshot1.rootSections[idx1].children[0] as! TabNode
    let node2 = snapshot2.rootSections[idx2].children[0] as! TabNode
    assertFalse(node1 === node2, "Expected different TabNode instances across builds")
}

func testSectionNodeIdentityAcrossSnapshots() {
    let regTab = makeTab(id: "reg-1", title: "Regular")

    let snapshot1 = SidebarSnapshot.build(
        favoritesTabs: [],
        pinnedTabs: [],
        regularTabs: [regTab],
        folders: [],
        activeSpaceId: "space-1"
    )

    let snapshot2 = SidebarSnapshot.build(
        favoritesTabs: [],
        pinnedTabs: [],
        regularTabs: [regTab],
        folders: [],
        activeSpaceId: "space-1"
    )

    let idx1 = lastRegularSectionIndex(in: snapshot1)
    let idx2 = lastRegularSectionIndex(in: snapshot2)
    let section1 = snapshot1.rootSections[idx1]
    let section2 = snapshot2.rootSections[idx2]
    assertFalse(section1 === section2, "Expected different SectionNode instances across snapshots")
}

func testTabNodeIdentityForSameTabIdInDifferentSnapshots() {
    let regTab = makeTab(id: "reg-1", title: "Regular")

    let snapshot1 = SidebarSnapshot.build(
        favoritesTabs: [],
        pinnedTabs: [],
        regularTabs: [regTab],
        folders: [],
        activeSpaceId: "space-1"
    )

    let snapshot2 = SidebarSnapshot.build(
        favoritesTabs: [],
        pinnedTabs: [],
        regularTabs: [regTab],
        folders: [],
        activeSpaceId: "space-1"
    )

    let idx1 = lastRegularSectionIndex(in: snapshot1)
    let idx2 = lastRegularSectionIndex(in: snapshot2)
    let tabNode1 = snapshot1.rootSections[idx1].children[0] as! TabNode
    let tabNode2 = snapshot2.rootSections[idx2].children[0] as! TabNode
    assertEqual(tabNode1.tabId, tabNode2.tabId)
    assertFalse(tabNode1 === tabNode2, "Expected different TabNode objects for same tabId in different snapshots")
}

// MARK: - Main

@main
struct TestRunner {
    static func main() {
        testSectionCount()
        testSnapshotContainsOnlyPinnedAndRegularSections()
        testTabNodesInCorrectSections()
        testFolderNodesContainTheirTabs()
        testNodeIdentityAcrossBuilds()
        testSectionNodeIdentityAcrossSnapshots()
        testTabNodeIdentityForSameTabIdInDifferentSnapshots()
        print("ALL TESTS PASSED")
    }
}
