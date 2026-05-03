import AppKit
import Foundation

func makeTabVM(id: TabId, title: String, isPinned: Bool = false) -> TabViewModel {
    TabViewModel(
        id: id,
        spaceId: "space-1",
        title: title,
        customTitle: nil,
        url: "https://example.com/\(id)",
        favicon: nil,
        isLoading: false,
        isPinned: isPinned,
        isFavorite: false,
        favoriteOrder: nil,
        isMuted: false,
        isPlayingAudio: false,
        lifecycleState: "active",
        children: [],
        createdAt: MahoDateTime("2024-01-01T00:00:00Z"),
        lastActiveAt: MahoDateTime("2024-01-01T00:00:00Z")
    )
}

func makeFolderVM(
    id: FolderId,
    name: String,
    tabIds: [TabId] = [],
    isPinned: Bool = false,
    parentFolderId: FolderId? = nil,
    isExpanded: Bool = true
) -> FolderViewModel {
    FolderViewModel(
        id: id,
        name: name,
        tabCount: tabIds.count,
        isExpanded: isExpanded,
        tabIds: tabIds,
        isPinned: isPinned,
        parentFolderId: parentFolderId
    )
}

func buildSnapshotForDragTests(
    pinnedTabs: [TabViewModel] = [],
    regularTabs: [TabViewModel] = [],
    folders: [FolderViewModel] = []
) -> SidebarSnapshot {
    SidebarSnapshot.build(
        favoritesTabs: [],
        pinnedTabs: pinnedTabs,
        regularTabs: regularTabs,
        folders: folders,
        activeSpaceId: "space-1"
    )
}

func section(_ snapshot: SidebarSnapshot, id: SectionID) -> SectionNode {
    guard let found = snapshot.rootSections.first(where: { $0.sectionId == id }) else {
        print("FAIL: missing section \(id)")
        exit(1)
    }
    return found
}

func folderNode(_ snapshot: SidebarSnapshot, folderId: FolderId) -> SectionNode {
    func search(_ children: [AnyObject]) -> SectionNode? {
        for child in children {
            if let folder = child as? SectionNode, folder.isFolder {
                if folder.folderId == folderId {
                    return folder
                }
                if let nested = search(folder.children) {
                    return nested
                }
            }
        }
        return nil
    }

    for root in snapshot.rootSections {
        if let found = search(root.children) {
            return found
        }
    }

    print("FAIL: missing folder node \(folderId)")
    exit(1)
}

final class TestSidebarController: SidebarController {
    override func loadView() {
        self.view = NSView()
    }

    override func viewDidLoad() {}
}

func makeDragController(snapshot: SidebarSnapshot) -> (SidebarDragController, NSOutlineView, TestSidebarController) {
    let controller = TestSidebarController()
    controller.dataSource.snapshot = snapshot
    controller.outlineView.addTableColumn(NSTableColumn(identifier: NSUserInterfaceItemIdentifier("MainColumn")))
    let dragController = SidebarDragController()
    dragController.controller = controller
    return (dragController, controller.outlineView, controller)
}

// MARK: - Tests

func testDropIntentEquality() {
    let a: DropIntent = .reorderTab(tabId: "t1", beforeTabId: "t3", sectionId: .regular)
    let b: DropIntent = .reorderTab(tabId: "t1", beforeTabId: "t3", sectionId: .regular)
    let c: DropIntent = .reorderTab(tabId: "t1", beforeTabId: "t4", sectionId: .regular)
    assertEqual(a, b)
    assertFalse(a == c)

    let d: DropIntent = .moveTabToFolder(tabId: "t1", folderId: "f1")
    let e: DropIntent = .moveTabToFolder(tabId: "t1", folderId: "f1")
    let f: DropIntent = .moveTabToFolder(tabId: "t1", folderId: "f2")
    assertEqual(d, e)
    assertFalse(d == f)

    assertEqual(DropIntent.none, DropIntent.none)
}

func testPinUnpinInequality() {
    let pin: DropIntent = .pinTab(tabId: "t1")
    let unpin: DropIntent = .unpinTab(tabId: "t1")
    assertFalse(pin == unpin)

    let pinFolder: DropIntent = .pinFolder(folderId: "f1")
    let unpinFolder: DropIntent = .unpinFolder(folderId: "f1")
    assertFalse(pinFolder == unpinFolder)
}

func testAllCasesConstructible() {
    let intents: [DropIntent] = [
        .reorderTab(tabId: "t1", beforeTabId: nil, sectionId: .favorites),
        .moveTabToFolder(tabId: "t1", folderId: "f1"),
        .reorderTabInFolder(tabId: "t1", folderId: "f1", targetIndex: 0),
        .removeTabFromFolder(tabId: "t1", folderId: "f1"),
        .splitTab(sourceId: "t1", targetId: "t2", side: .left),
        .splitTab(sourceId: "t1", targetId: "t2", side: .right),
        .reorderFolder(folderId: "f1", targetIndex: 0, sectionId: .regular),
        .moveFolderIntoFolder(folderId: "f1", targetFolderId: "f2"),
        .moveFolderToRoot(folderId: "f1", targetIndex: 0, sectionId: .regular),
        .pinTab(tabId: "t1"),
        .unpinTab(tabId: "t1"),
        .pinFolder(folderId: "f1"),
        .unpinFolder(folderId: "f1"),
        .none,
    ]

    assertEqual(intents.count, 14)

    // Verify each is distinct (no two equal unless structurally identical)
    var uniqueCount = 0
    for i in 0..<intents.count {
        var isDuplicate = false
        for j in 0..<i {
            if intents[i] == intents[j] {
                isDuplicate = true
                break
            }
        }
        if !isDuplicate {
            uniqueCount += 1
        }
    }
    assertEqual(uniqueCount, 14) // left/right split variants are distinct
}

func testSplitSideVariants() {
    let left: DropIntent = .splitTab(sourceId: "a", targetId: "b", side: .left)
    let right: DropIntent = .splitTab(sourceId: "a", targetId: "b", side: .right)
    assertFalse(left == right)
}

func testRegularTabDroppedIntoPinnedSectionPinsTab() {
    let regularTab = makeTabVM(id: "reg-1", title: "Regular")
    let snapshot = buildSnapshotForDragTests(regularTabs: [regularTab])
    let pinnedSection = SectionNode(sectionId: .pinned, title: "Pinned", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "tab",
        sourceId: regularTab.id,
        targetItem: pinnedSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intent, .pinTab(tabId: regularTab.id))
}

func testPinnedTabDroppedIntoRegularSectionUnpinsTab() {
    let pinnedTab = makeTabVM(id: "pin-1", title: "Pinned", isPinned: true)
    let snapshot = buildSnapshotForDragTests(pinnedTabs: [pinnedTab])
    let regularSection = SectionNode(sectionId: .regular, title: "Normal", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "tab",
        sourceId: pinnedTab.id,
        targetItem: regularSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intent, .unpinTab(tabId: pinnedTab.id))
}

func testRegularFolderDroppedIntoPinnedSectionPinsFolder() {
    let folder = makeFolderVM(id: "fold-reg", name: "Regular Folder")
    let snapshot = buildSnapshotForDragTests(folders: [folder])
    let pinnedSection = SectionNode(sectionId: .pinned, title: "Pinned", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "folder",
        sourceId: folder.id,
        targetItem: pinnedSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intent, .pinFolder(folderId: folder.id))
}

func testPinnedFolderDroppedIntoRegularSectionUnpinsFolder() {
    let folder = makeFolderVM(id: "fold-pin", name: "Pinned Folder", isPinned: true)
    let snapshot = buildSnapshotForDragTests(folders: [folder])
    let regularSection = SectionNode(sectionId: .regular, title: "Normal", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "folder",
        sourceId: folder.id,
        targetItem: regularSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intent, .unpinFolder(folderId: folder.id))
}

func testFolderDroppedOntoFolderMovesIntoFolder() {
    let parent = makeFolderVM(id: "parent", name: "Parent")
    let child = makeFolderVM(id: "child", name: "Child")
    let snapshot = buildSnapshotForDragTests(folders: [parent, child])
    let targetFolderNode = folderNode(snapshot, folderId: parent.id)
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "folder",
        sourceId: child.id,
        targetItem: targetFolderNode,
        childIndex: NSOutlineViewDropOnItemIndex,
        outlineView: outlineView
    )

    assertEqual(intent, .moveFolderIntoFolder(folderId: child.id, targetFolderId: parent.id))
}

func testNestedFolderDroppedIntoRegularRootMovesToRoot() {
    let parent = makeFolderVM(id: "parent", name: "Parent")
    let nested = makeFolderVM(id: "nested", name: "Nested", parentFolderId: parent.id)
    let snapshot = buildSnapshotForDragTests(folders: [parent, nested])
    let regularSection = section(snapshot, id: .regular)
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "folder",
        sourceId: nested.id,
        targetItem: regularSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intent, .moveFolderToRoot(folderId: nested.id, targetIndex: 0, sectionId: .regular))
}

func testTabDroppedOntoFolderMovesIntoFolder() {
    let regularTab = makeTabVM(id: "reg-1", title: "Regular")
    let folder = makeFolderVM(id: "fold-1", name: "Target Folder")
    let snapshot = buildSnapshotForDragTests(regularTabs: [regularTab], folders: [folder])
    let targetFolderNode = folderNode(snapshot, folderId: folder.id)
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "tab",
        sourceId: regularTab.id,
        targetItem: targetFolderNode,
        childIndex: NSOutlineViewDropOnItemIndex,
        outlineView: outlineView
    )

    assertEqual(intent, .moveTabToFolder(tabId: regularTab.id, folderId: folder.id))
}

func testFolderTabDroppedToRegularSectionRemovesTabFromFolder() {
    let folderTab = makeTabVM(id: "folder-tab", title: "Folder Tab")
    let folder = makeFolderVM(id: "fold-1", name: "Folder", tabIds: [folderTab.id])
    let snapshot = buildSnapshotForDragTests(regularTabs: [folderTab], folders: [folder])
    let regularSection = section(snapshot, id: .regular)
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "tab",
        sourceId: folderTab.id,
        targetItem: regularSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intent, .removeTabFromFolder(tabId: folderTab.id, folderId: folder.id))
}

func testRegularTabDroppedOntoSiblingTabSplits() {
    let sourceTab = makeTabVM(id: "reg-1", title: "Source")
    let targetTab = makeTabVM(id: "reg-2", title: "Target")
    let snapshot = buildSnapshotForDragTests(regularTabs: [sourceTab, targetTab])
    let targetNode = section(snapshot, id: .regular).children[1] as! TabNode
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "tab",
        sourceId: sourceTab.id,
        targetItem: targetNode,
        childIndex: NSOutlineViewDropOnItemIndex,
        outlineView: outlineView
    )

    assertEqual(intent, .splitTab(sourceId: sourceTab.id, targetId: targetTab.id, side: .right))
}

func testFolderChildDroppedOntoSiblingTabSplits() {
    let sourceTab = makeTabVM(id: "child-1", title: "Child One")
    let targetTab = makeTabVM(id: "child-2", title: "Child Two")
    let folder = makeFolderVM(id: "fold-1", name: "Folder", tabIds: [sourceTab.id, targetTab.id])
    let snapshot = buildSnapshotForDragTests(regularTabs: [sourceTab, targetTab], folders: [folder])
    let targetNode = folderNode(snapshot, folderId: folder.id).children[1] as! TabNode
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "tab",
        sourceId: sourceTab.id,
        targetItem: targetNode,
        childIndex: NSOutlineViewDropOnItemIndex,
        outlineView: outlineView
    )

    assertEqual(intent, .splitTab(sourceId: sourceTab.id, targetId: targetTab.id, side: .right))
}

func testFolderDroppedOntoItselfReturnsNone() {
    let folder = makeFolderVM(id: "fold-1", name: "Folder")
    let snapshot = buildSnapshotForDragTests(folders: [folder])
    let targetFolderNode = folderNode(snapshot, folderId: folder.id)
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "folder",
        sourceId: folder.id,
        targetItem: targetFolderNode,
        childIndex: NSOutlineViewDropOnItemIndex,
        outlineView: outlineView
    )

    assertEqual(intent, .none)
}

func testFavoriteTabDroppedIntoPinnedSectionPinsTab() {
    let snapshot = buildSnapshotForDragTests()
    let pinnedSection = SectionNode(sectionId: .pinned, title: "Pinned", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "tab",
        sourceId: "fav-tab-1",
        sourceOrigin: "favorites",
        targetItem: pinnedSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intent, .pinTab(tabId: "fav-tab-1"))
}

func testFavoriteTabDroppedIntoRegularSectionUnpinsTab() {
    let snapshot = buildSnapshotForDragTests()
    let regularSection = SectionNode(sectionId: .regular, title: "Normal", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intent = dragController.resolveDropIntent(
        sourceType: "tab",
        sourceId: "fav-tab-1",
        sourceOrigin: "favorites",
        targetItem: regularSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intent, .unpinTab(tabId: "fav-tab-1"))
}

// MARK: - Multi-Item Drag Tests

func testMultiTabReorder() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let t2 = makeTabVM(id: "t2", title: "Tab 2")
    let t3 = makeTabVM(id: "t3", title: "Tab 3")
    let t4 = makeTabVM(id: "t4", title: "Tab 4")
    let snapshot = buildSnapshotForDragTests(regularTabs: [t1, t2, t3, t4])
    let regularSection = section(snapshot, id: .regular)
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let sources: [(type: String, id: String, origin: String?)] = [
        (type: "tab", id: "t1", origin: nil),
        (type: "tab", id: "t2", origin: nil),
        (type: "tab", id: "t3", origin: nil),
    ]

    let intents = dragController.resolveMultiDropIntent(
        sources: sources,
        targetItem: regularSection,
        childIndex: 3,
        outlineView: outlineView
    )

    assertEqual(intents.count, 3)
    assertEqual(intents[0], .reorderTab(tabId: "t1", beforeTabId: "t4", sectionId: .regular))
    assertEqual(intents[1], .reorderTab(tabId: "t2", beforeTabId: "t4", sectionId: .regular))
    assertEqual(intents[2], .reorderTab(tabId: "t3", beforeTabId: "t4", sectionId: .regular))
}

func testMultiTabMoveToFolder() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let t2 = makeTabVM(id: "t2", title: "Tab 2")
    let folder = makeFolderVM(id: "f1", name: "Target Folder")
    let snapshot = buildSnapshotForDragTests(regularTabs: [t1, t2], folders: [folder])
    let targetFolderNode = folderNode(snapshot, folderId: folder.id)
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let sources: [(type: String, id: String, origin: String?)] = [
        (type: "tab", id: "t1", origin: nil),
        (type: "tab", id: "t2", origin: nil),
    ]

    let intents = dragController.resolveMultiDropIntent(
        sources: sources,
        targetItem: targetFolderNode,
        childIndex: NSOutlineViewDropOnItemIndex,
        outlineView: outlineView
    )

    assertEqual(intents.count, 2)
    assertEqual(intents[0], .moveTabToFolder(tabId: "t1", folderId: "f1"))
    assertEqual(intents[1], .moveTabToFolder(tabId: "t2", folderId: "f1"))
}

func testMultiTabPin() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let t2 = makeTabVM(id: "t2", title: "Tab 2")
    let snapshot = buildSnapshotForDragTests(regularTabs: [t1, t2])
    let pinnedSection = SectionNode(sectionId: .pinned, title: "Pinned", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let sources: [(type: String, id: String, origin: String?)] = [
        (type: "tab", id: "t1", origin: nil),
        (type: "tab", id: "t2", origin: nil),
    ]

    let intents = dragController.resolveMultiDropIntent(
        sources: sources,
        targetItem: pinnedSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intents.count, 2)
    assertEqual(intents[0], .pinTab(tabId: "t1"))
    assertEqual(intents[1], .pinTab(tabId: "t2"))
}

func testMultiTabUnpin() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1", isPinned: true)
    let t2 = makeTabVM(id: "t2", title: "Tab 2", isPinned: true)
    let snapshot = buildSnapshotForDragTests(pinnedTabs: [t1, t2])
    let regularSection = SectionNode(sectionId: .regular, title: "Normal", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let sources: [(type: String, id: String, origin: String?)] = [
        (type: "tab", id: "t1", origin: nil),
        (type: "tab", id: "t2", origin: nil),
    ]

    let intents = dragController.resolveMultiDropIntent(
        sources: sources,
        targetItem: regularSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intents.count, 2)
    assertEqual(intents[0], .unpinTab(tabId: "t1"))
    assertEqual(intents[1], .unpinTab(tabId: "t2"))
}

func testMixedTabFolderSelectionOnlyProcessesTabs() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let folder = makeFolderVM(id: "f1", name: "Folder")
    let snapshot = buildSnapshotForDragTests(regularTabs: [t1], folders: [folder])
    let pinnedSection = SectionNode(sectionId: .pinned, title: "Pinned", children: [])
    let (dragController, outlineView, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let sources: [(type: String, id: String, origin: String?)] = [
        (type: "tab", id: "t1", origin: nil),
        (type: "folder", id: "f1", origin: nil),
    ]

    let intents = dragController.resolveMultiDropIntent(
        sources: sources,
        targetItem: pinnedSection,
        childIndex: 0,
        outlineView: outlineView
    )

    assertEqual(intents.count, 1)
    assertEqual(intents[0], .pinTab(tabId: "t1"))
}

func testExecuteIntentsCallsEachIntent() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let t2 = makeTabVM(id: "t2", title: "Tab 2")
    let snapshot = buildSnapshotForDragTests(regularTabs: [t1, t2])
    let (dragController, _, retainedController) = makeDragController(snapshot: snapshot)
    _ = retainedController

    let intents: [DropIntent] = [
        .pinTab(tabId: "t1"),
        .pinTab(tabId: "t2"),
    ]

    dragController.executeIntents(intents, sourceOrigin: nil)
}

func testFindSiblingFolderIdWithTabsInterleavedBetweenFolders() {
    let tab1 = TabNode(tabId: "t1", title: "Tab 1", customTitle: nil, url: "https://example.com/1",
                       favicon: nil, isLoading: false, isPinned: false, isFavorite: false,
                       isMuted: false, isPlayingAudio: false, lifecycleState: "active")
    let tab2 = TabNode(tabId: "t2", title: "Tab 2", customTitle: nil, url: "https://example.com/2",
                       favicon: nil, isLoading: false, isPinned: false, isFavorite: false,
                       isMuted: false, isPlayingAudio: false, lifecycleState: "active")
    let f1 = SectionNode(sectionId: .regular, title: "Folder A", children: [], isFolder: true, folderId: "f1")
    let f2 = SectionNode(sectionId: .regular, title: "Folder B", children: [], isFolder: true, folderId: "f2")
    let f3 = SectionNode(sectionId: .regular, title: "Folder C", children: [], isFolder: true, folderId: "f3")

    let interleavedChildren: [AnyObject] = [f1, tab1, f2, tab2, f3]
    let regularSection = SectionNode(sectionId: .regular, title: "Normal", children: interleavedChildren)
    let snapshot = SidebarSnapshot(rootSections: [
        SectionNode(sectionId: .pinned, title: "Pinned"),
        SectionNode(sectionId: .separator, title: ""),
        SectionNode(sectionId: .newTab, title: "New Tab"),
        regularSection,
    ])

    let dragController = SidebarDragController()

    // Children layout: [F1(0), T1(1), F2(2), T2(3), F3(4)]

    // Drag F3 to index 0 (before F1): should get beforeFolderId = F1
    assertEqual(
        dragController.findSiblingFolderIdAtIndex(targetIndex: 0, draggingFolderId: "f3", parentFolderId: nil, sectionId: .regular, in: snapshot),
        "f1"
    )

    // Drag F1 to index 2 (between T1 and F2): should get beforeFolderId = F2
    assertEqual(
        dragController.findSiblingFolderIdAtIndex(targetIndex: 2, draggingFolderId: "f1", parentFolderId: nil, sectionId: .regular, in: snapshot),
        "f2"
    )

    // Drag F1 to index 5 (after F3, end): should get nil (append)
    assertEqual(
        dragController.findSiblingFolderIdAtIndex(targetIndex: 5, draggingFolderId: "f1", parentFolderId: nil, sectionId: .regular, in: snapshot),
        nil
    )

    // Drag F2 to index 4 (between T2 and F3): should get beforeFolderId = F3
    assertEqual(
        dragController.findSiblingFolderIdAtIndex(targetIndex: 4, draggingFolderId: "f2", parentFolderId: nil, sectionId: .regular, in: snapshot),
        "f3"
    )

    // REGRESSION: Drag F3 to index 2 (between T1 and F2)
    // Old buggy code: folder-only array [F1,F2,F3], siblings[2]=F3 (self) → no-op
    // Fixed code: should get beforeFolderId = F2
    assertEqual(
        dragController.findSiblingFolderIdAtIndex(targetIndex: 2, draggingFolderId: "f3", parentFolderId: nil, sectionId: .regular, in: snapshot),
        "f2"
    )

    // REGRESSION: Drag F1 to index 3 (between F2 and T2)
    // Old buggy code: siblings[3] → out of bounds → nil → append at end
    // Fixed code: should get beforeFolderId = F3
    assertEqual(
        dragController.findSiblingFolderIdAtIndex(targetIndex: 3, draggingFolderId: "f1", parentFolderId: nil, sectionId: .regular, in: snapshot),
        "f3"
    )
}

func testFindSiblingFolderIdExcludesDraggedFolder() {
    let tab1 = TabNode(tabId: "t1", title: "Tab 1", customTitle: nil, url: "https://example.com/1",
                       favicon: nil, isLoading: false, isPinned: false, isFavorite: false,
                       isMuted: false, isPlayingAudio: false, lifecycleState: "active")
    let f1 = SectionNode(sectionId: .regular, title: "Folder A", children: [], isFolder: true, folderId: "f1")
    let f2 = SectionNode(sectionId: .regular, title: "Folder B", children: [], isFolder: true, folderId: "f2")

    // Children layout: [F1(0), T1(1), F2(2)]
    let regularSection = SectionNode(sectionId: .regular, title: "Normal", children: [f1, tab1, f2])
    let snapshot = SidebarSnapshot(rootSections: [
        SectionNode(sectionId: .pinned, title: "Pinned"),
        SectionNode(sectionId: .separator, title: ""),
        SectionNode(sectionId: .newTab, title: "New Tab"),
        regularSection,
    ])

    let dragController = SidebarDragController()

    // Drag F1 at its own index (0): should skip F1 and return F2
    assertEqual(
        dragController.findSiblingFolderIdAtIndex(targetIndex: 0, draggingFolderId: "f1", parentFolderId: nil, sectionId: .regular, in: snapshot),
        "f2"
    )

    // Drag F2 at its own index (2): should skip F2 and return nil (F1 is before, no folder after)
    assertEqual(
        dragController.findSiblingFolderIdAtIndex(targetIndex: 2, draggingFolderId: "f2", parentFolderId: nil, sectionId: .regular, in: snapshot),
        nil
    )
}

// MARK: - Nested Folder Snapshot Tests

func testNestedFolderPreservesChildTabs() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let t2 = makeTabVM(id: "t2", title: "Tab 2")
    let t3 = makeTabVM(id: "t3", title: "Tab 3")

    let childFolder = makeFolderVM(id: "child-f", name: "Child", tabIds: ["t2", "t3"], parentFolderId: "parent-f")
    let parentFolder = makeFolderVM(id: "parent-f", name: "Parent", tabIds: ["t1"])

    let snapshot = buildSnapshotForDragTests(
        regularTabs: [t1, t2, t3],
        folders: [parentFolder, childFolder]
    )

    let parentNode = folderNode(snapshot, folderId: "parent-f")
    let childNode = folderNode(snapshot, folderId: "child-f")

    let parentTabIds = parentNode.children.compactMap { ($0 as? TabNode)?.tabId }
    let childTabIds = childNode.children.compactMap { ($0 as? TabNode)?.tabId }

    assertEqual(parentTabIds, ["t1"])
    assertEqual(childTabIds.sorted(), ["t2", "t3"])
}

func testDeeplyNestedFolderPreservesAllLevels() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let t2 = makeTabVM(id: "t2", title: "Tab 2")
    let t3 = makeTabVM(id: "t3", title: "Tab 3")

    let grandchild = makeFolderVM(id: "gc-f", name: "Grandchild", tabIds: ["t3"], parentFolderId: "child-f")
    let child = makeFolderVM(id: "child-f", name: "Child", tabIds: ["t2"], parentFolderId: "root-f")
    let root = makeFolderVM(id: "root-f", name: "Root", tabIds: ["t1"])

    let snapshot = buildSnapshotForDragTests(
        regularTabs: [t1, t2, t3],
        folders: [root, child, grandchild]
    )

    let rootNode = folderNode(snapshot, folderId: "root-f")
    let childNode = folderNode(snapshot, folderId: "child-f")
    let gcNode = folderNode(snapshot, folderId: "gc-f")

    assertEqual(rootNode.children.compactMap { ($0 as? TabNode)?.tabId }, ["t1"])
    assertEqual(childNode.children.compactMap { ($0 as? TabNode)?.tabId }, ["t2"])
    assertEqual(gcNode.children.compactMap { ($0 as? TabNode)?.tabId }, ["t3"])

    let rootChildFolders = rootNode.children.compactMap { ($0 as? SectionNode)?.folderId }
    let childChildFolders = childNode.children.compactMap { ($0 as? SectionNode)?.folderId }

    assertEqual(rootChildFolders, ["child-f"])
    assertEqual(childChildFolders, ["gc-f"])
}

func testNestedFolderTabsNotDuplicatedInRootSection() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let t2 = makeTabVM(id: "t2", title: "Tab 2")
    let t3 = makeTabVM(id: "t3", title: "Loose Tab")

    let child = makeFolderVM(id: "child-f", name: "Child", tabIds: ["t2"], parentFolderId: "parent-f")
    let parent = makeFolderVM(id: "parent-f", name: "Parent", tabIds: ["t1"])

    let snapshot = buildSnapshotForDragTests(
        regularTabs: [t1, t2, t3],
        folders: [parent, child]
    )

    let regularSection = section(snapshot, id: .regular)
    let looseTabIds = regularSection.children.compactMap { ($0 as? TabNode)?.tabId }

    assert(looseTabIds.contains("t3"), "Loose tab t3 should appear in regular section")
    assert(!looseTabIds.contains("t1"), "t1 should NOT appear as loose tab (inside parent folder)")
    assert(!looseTabIds.contains("t2"), "t2 should NOT appear as loose tab (inside child folder)")
}

func testPinnedNestedFolderPreservesTabs() {
    let t1 = makeTabVM(id: "t1", title: "Tab 1")
    let t2 = makeTabVM(id: "t2", title: "Tab 2")

    let child = makeFolderVM(id: "child-f", name: "Child", tabIds: ["t2"], isPinned: true, parentFolderId: "parent-f")
    let parent = makeFolderVM(id: "parent-f", name: "Parent", tabIds: ["t1"], isPinned: true)

    let snapshot = buildSnapshotForDragTests(
        regularTabs: [t1, t2],
        folders: [parent, child]
    )

    let parentNode = folderNode(snapshot, folderId: "parent-f")
    let childNode = folderNode(snapshot, folderId: "child-f")

    assertEqual(parentNode.children.compactMap { ($0 as? TabNode)?.tabId }, ["t1"])
    assertEqual(childNode.children.compactMap { ($0 as? TabNode)?.tabId }, ["t2"])
}

// MARK: - Main

@main
struct TestRunner {
    static func main() {
        testDropIntentEquality()
        testPinUnpinInequality()
        testAllCasesConstructible()
        testSplitSideVariants()
        testRegularTabDroppedIntoPinnedSectionPinsTab()
        testPinnedTabDroppedIntoRegularSectionUnpinsTab()
        testRegularFolderDroppedIntoPinnedSectionPinsFolder()
        testPinnedFolderDroppedIntoRegularSectionUnpinsFolder()
        testFolderDroppedOntoFolderMovesIntoFolder()
        testNestedFolderDroppedIntoRegularRootMovesToRoot()
        testTabDroppedOntoFolderMovesIntoFolder()
        testFolderTabDroppedToRegularSectionRemovesTabFromFolder()
        testRegularTabDroppedOntoSiblingTabSplits()
        testFolderChildDroppedOntoSiblingTabSplits()
        testFolderDroppedOntoItselfReturnsNone()
        testFavoriteTabDroppedIntoPinnedSectionPinsTab()
        testFavoriteTabDroppedIntoRegularSectionUnpinsTab()
        testMultiTabReorder()
        testMultiTabMoveToFolder()
        testMultiTabPin()
        testMultiTabUnpin()
        testMixedTabFolderSelectionOnlyProcessesTabs()
        testExecuteIntentsCallsEachIntent()
        testFindSiblingFolderIdWithTabsInterleavedBetweenFolders()
        testFindSiblingFolderIdExcludesDraggedFolder()
        testNestedFolderPreservesChildTabs()
        testDeeplyNestedFolderPreservesAllLevels()
        testNestedFolderTabsNotDuplicatedInRootSection()
        testPinnedNestedFolderPreservesTabs()
        print("ALL TESTS PASSED")
    }
}
