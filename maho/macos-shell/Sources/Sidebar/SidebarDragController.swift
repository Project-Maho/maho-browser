import AppKit



enum DropIntent: Equatable {
    case reorderTab(tabId: String, beforeTabId: String?, sectionId: SectionID)
    case moveTabToFolder(tabId: String, folderId: String)
    case reorderTabInFolder(tabId: String, folderId: String, targetIndex: Int)
    case removeTabFromFolder(tabId: String, folderId: String)
    case splitTab(sourceId: String, targetId: String, side: SplitSide)
    case reorderFolder(folderId: String, targetIndex: Int, sectionId: SectionID)
    case moveFolderIntoFolder(folderId: String, targetFolderId: String)
    case moveFolderToRoot(folderId: String, targetIndex: Int, sectionId: SectionID)
    case pinTab(tabId: String)
    case unpinTab(tabId: String)
    case pinFolder(folderId: String)
    case unpinFolder(folderId: String)
    case none
}

class SidebarDragController {
    var isDragging = false
    private var cachedIntent: DropIntent = .none
    private var cachedIntents: [DropIntent] = []
    private var cachedSourceOrigin: String?
    private var defaultDraggingFeedbackStyle: NSTableView.DraggingDestinationFeedbackStyle?
    weak var controller: SidebarController?

    static let dragType = NSPasteboard.PasteboardType("com.maho.sidebar.drag")

    func pasteboardWriterForItem(_ item: Any) -> NSPasteboardWriting? {
        let pasteboardItem = NSPasteboardItem()
        if let tab = item as? TabNode {
            let json = "{\"type\":\"tab\",\"id\":\"\(tab.tabId)\"}"
            pasteboardItem.setString(json, forType: SidebarDragController.dragType)
            return pasteboardItem
        }
        if let section = item as? SectionNode, section.isFolder, let folderId = section.folderId {
            let json = "{\"type\":\"folder\",\"id\":\"\(folderId)\"}"
            pasteboardItem.setString(json, forType: SidebarDragController.dragType)
            return pasteboardItem
        }
        return nil
    }

    func draggingSessionWillBegin() {
        isDragging = true
        if let outlineView = controller?.outlineView {
            defaultDraggingFeedbackStyle = outlineView.draggingDestinationFeedbackStyle
        }
    }

    func draggingSessionEnded() {
        isDragging = false
        cachedIntent = .none
        cachedIntents = []
        cachedSourceOrigin = nil
        if let outlineView = controller?.outlineView {
            configureDraggingFeedback(for: .none, in: outlineView)
            clearRowDropState(in: outlineView)
        }
        controller?.clearSidebarDropOverlay()
        controller?.flushDeferredReloadIfNeeded()
    }

    func draggingExited(outlineView: NSOutlineView) {
        cachedIntent = .none
        cachedIntents = []
        cachedSourceOrigin = nil
        configureDraggingFeedback(for: .none, in: outlineView)
        clearRowDropState(in: outlineView)
        controller?.clearSidebarDropOverlay()
    }

    func validateDrop(outlineView: NSOutlineView, info: NSDraggingInfo, item: Any?, childIndex: Int) -> NSDragOperation {
        let multiSources = parsePasteboardMulti(info.draggingPasteboard)

        if multiSources.count > 1 {
            cachedSourceOrigin = multiSources.first?.origin
            let intents = resolveMultiDropIntent(sources: multiSources, targetItem: item, childIndex: childIndex, outlineView: outlineView)
            cachedIntents = intents
            cachedIntent = intents.first ?? .none

            if intents.isEmpty {
                configureDraggingFeedback(for: .none, in: outlineView)
                clearRowDropState(in: outlineView)
                controller?.clearSidebarDropOverlay()
                return []
            }

            let representativeIntent = intents.first!
            configureDraggingFeedback(for: representativeIntent, in: outlineView)
            clearRowDropState(in: outlineView)
            switch representativeIntent {
            case .pinTab, .pinFolder:
                controller?.showSectionDropOverlay(for: .pinned)
            case .unpinTab, .unpinFolder:
                controller?.showSectionDropOverlay(for: .regular)
            default:
                controller?.clearSidebarDropOverlay()
            }
            return .move
        }

        guard let payload = parsePasteboard(info.draggingPasteboard) else {
            configureDraggingFeedback(for: .none, in: outlineView)
            clearRowDropState(in: outlineView)
            controller?.clearSidebarDropOverlay()
            return []
        }

        let sourceType = payload.type
        let sourceId = payload.id
        let sourceOrigin = payload.origin
        cachedSourceOrigin = sourceOrigin

        var intent = resolveDropIntent(sourceType: sourceType, sourceId: sourceId, sourceOrigin: sourceOrigin, targetItem: item, childIndex: childIndex, outlineView: outlineView)

        if case let .splitTab(srcId, tgtId, currentSide) = intent,
           let targetTab = item as? TabNode {
            let targetRow = outlineView.row(forItem: targetTab)
            if targetRow >= 0 {
                let localPoint = outlineView.convert(info.draggingLocation, from: nil)
                let rowRect = outlineView.rect(ofRow: targetRow)
                let side: SplitSide = localPoint.x < rowRect.midX ? .left : .right
                if side != currentSide {
                    intent = .splitTab(sourceId: srcId, targetId: tgtId, side: side)
                }
            }
        }

        if case let .moveFolderIntoFolder(srcFolderId, _) = intent,
           let targetFolder = item as? SectionNode,
           targetFolder.isFolder {
            let targetRow = outlineView.row(forItem: targetFolder)
            if targetRow >= 0 {
                let localPoint = outlineView.convert(info.draggingLocation, from: nil)
                let rowRect = outlineView.rect(ofRow: targetRow)
                let relativeY = (localPoint.y - rowRect.minY) / rowRect.height
                let edgeFraction: CGFloat = 0.25

                if relativeY < edgeFraction || relativeY > (1.0 - edgeFraction) {
                    if let parentSection = outlineView.parent(forItem: targetFolder) as? SectionNode,
                       !parentSection.isFolder {
                        let childIdx = outlineView.childIndex(forItem: targetFolder)
                        let insertIdx = relativeY < edgeFraction ? childIdx : childIdx + 1
                        let retargetedIntent = resolveDropIntent(
                            sourceType: sourceType, sourceId: srcFolderId, sourceOrigin: sourceOrigin,
                            targetItem: parentSection, childIndex: insertIdx, outlineView: outlineView
                        )
                        if retargetedIntent != .none {
                            intent = retargetedIntent
                            outlineView.setDropItem(parentSection, dropChildIndex: insertIdx)
                        }
                    }
                }
            }
        }

        let needsPositionFallback = intent == .none && (
            item == nil ||
            (item as? SectionNode)?.sectionId == .separator ||
            (item as? SectionNode)?.sectionId == .newTab ||
            ((item as? SectionNode)?.sectionId == .regular && childIndex == NSOutlineViewDropOnItemIndex) ||
            ((item as? SectionNode)?.sectionId == .pinned && childIndex == NSOutlineViewDropOnItemIndex)
        )

        if needsPositionFallback, let snapshot = controller?.dataSource.snapshot {
            let localPoint = outlineView.convert(info.draggingLocation, from: nil)
            intent = resolveDropByPosition(sourceType: sourceType, sourceId: sourceId, sourceOrigin: sourceOrigin, point: localPoint, outlineView: outlineView, snapshot: snapshot)
        }

        configureDraggingFeedback(for: intent, in: outlineView)
        clearRowDropState(in: outlineView)
        switch intent {
        case .pinTab(_), .pinFolder(_):
            controller?.showSectionDropOverlay(for: .pinned)
        case .unpinTab(_), .unpinFolder(_):
            controller?.showSectionDropOverlay(for: .regular)
        case let .splitTab(_, _, side):
            controller?.clearSidebarDropOverlay()
            if let targetTab = item as? TabNode {
                let row = outlineView.row(forItem: targetTab)
                if row >= 0,
                   let rowView = outlineView.rowView(atRow: row, makeIfNecessary: false) as? SidebarRowView {
                    rowView.splitHighlightSide = side
                }
            }
        default:
            controller?.clearSidebarDropOverlay()
            if childIndex == NSOutlineViewDropOnItemIndex,
               let targetRowItem = item {
                let row = outlineView.row(forItem: targetRowItem)
                if row >= 0,
                   let rowView = outlineView.rowView(atRow: row, makeIfNecessary: false) as? SidebarRowView {
                    rowView.customDropTargetHighlight = true
                }
            }
            break
        }

        cachedIntent = intent
        if intent == .none {
            controller?.clearSidebarDropOverlay()
            return []
        }
        return .move
    }

    func acceptDrop(outlineView: NSOutlineView, info: NSDraggingInfo, item: Any?, childIndex: Int) -> Bool {
        configureDraggingFeedback(for: .none, in: outlineView)
        clearRowDropState(in: outlineView)
        controller?.clearSidebarDropOverlay()

        if !cachedIntents.isEmpty {
            executeIntents(cachedIntents, sourceOrigin: cachedSourceOrigin)
            cachedIntents = []
            cachedIntent = .none
            cachedSourceOrigin = nil
            return true
        }

        if cachedIntent == .none {
            return false
        }
        executeIntent(cachedIntent, sourceOrigin: cachedSourceOrigin)
        cachedIntent = .none
        cachedSourceOrigin = nil
        return true
    }

    func resolveDropIntent(sourceType: String, sourceId: String, sourceOrigin: String? = nil, targetItem: Any?, childIndex: Int, outlineView: NSOutlineView) -> DropIntent {
        guard let snapshot = controller?.dataSource.snapshot else {
            return .none
        }

        if sourceType == "tab" {
            let tabId = sourceId
            let sourceSection = findSectionForTab(tabId: tabId, in: snapshot)
            let sourceFolderId = findFolderForTab(tabId: tabId, in: snapshot)
            let sourceOriginIsFavorites = sourceOrigin == "favorites"

            // Dropping onto a folder: same-folder drags reorder, cross-folder drags move.
            if let targetSection = targetItem as? SectionNode, targetSection.isFolder, let folderId = targetSection.folderId {
                if sourceFolderId == folderId, childIndex >= 0 {
                    return .reorderTabInFolder(tabId: tabId, folderId: folderId, targetIndex: childIndex)
                }
                return .moveTabToFolder(tabId: tabId, folderId: folderId)
            }

            // Tab on tab = split
            if childIndex == NSOutlineViewDropOnItemIndex, let targetTab = targetItem as? TabNode {
                if sourceId != targetTab.tabId {
                    return .splitTab(sourceId: sourceId, targetId: targetTab.tabId, side: .right)
                }
                return .none
            }

            // Dropping between children of a folder
            if let targetSection = targetItem as? SectionNode, targetSection.isFolder, childIndex >= 0 {
                if let folderId = targetSection.folderId {
                    return .reorderTabInFolder(tabId: tabId, folderId: folderId, targetIndex: childIndex)
                }
            }

            // Dropping directly onto a section header (childIndex == -1, not a folder)
            if let targetSection = targetItem as? SectionNode, !targetSection.isFolder, childIndex == NSOutlineViewDropOnItemIndex {
                if (sourceSection?.sectionId == .pinned || sourceOriginIsFavorites) && targetSection.sectionId == .regular {
                    return .unpinTab(tabId: tabId)
                }
                if (sourceSection?.sectionId == .regular || sourceOriginIsFavorites) && targetSection.sectionId == .pinned {
                    return .pinTab(tabId: tabId)
                }
            }

            // Dropping between children of a root section
            if let targetSection = targetItem as? SectionNode, !targetSection.isFolder, childIndex >= 0 {
                if let folderId = sourceFolderId {
                    return .removeTabFromFolder(tabId: tabId, folderId: folderId)
                }
                if (sourceSection?.sectionId == .pinned || sourceOriginIsFavorites) && targetSection.sectionId == .regular {
                    return .unpinTab(tabId: tabId)
                }
                if (sourceSection?.sectionId == .regular || sourceOriginIsFavorites) && targetSection.sectionId == .pinned {
                    return .pinTab(tabId: tabId)
                }
                let beforeTabId = findBeforeTabId(at: childIndex, in: targetSection)
                return .reorderTab(tabId: tabId, beforeTabId: beforeTabId, sectionId: targetSection.sectionId)
            }

            // Dropping onto root background or section header background
            if let folderId = sourceFolderId, targetItem == nil || (targetItem as? SectionNode)?.isFolder == false {
                return .removeTabFromFolder(tabId: tabId, folderId: folderId)
            }
        }

        if sourceType == "folder" {
            let folderId = sourceId
            let sourceSection = findSectionForFolder(folderId: folderId, in: snapshot)
            let sourceParentFolderId = findParentFolderForFolder(folderId: folderId, in: snapshot)

            // Dropping onto another folder
            if let targetSection = targetItem as? SectionNode, targetSection.isFolder, let targetFolderId = targetSection.folderId {
                if folderId == targetFolderId {
                    return .none
                }
                return .moveFolderIntoFolder(folderId: folderId, targetFolderId: targetFolderId)
            }

            // Dropping directly onto a section header
            if let targetSection = targetItem as? SectionNode, !targetSection.isFolder, childIndex == NSOutlineViewDropOnItemIndex {
                if sourceSection?.sectionId == .pinned && targetSection.sectionId == .regular {
                    return .unpinFolder(folderId: folderId)
                }
                if sourceSection?.sectionId == .regular && targetSection.sectionId == .pinned {
                    return .pinFolder(folderId: folderId)
                }
            }

            // Dropping between children of a root section
            if let targetSection = targetItem as? SectionNode, !targetSection.isFolder, childIndex >= 0 {
                if sourceSection?.sectionId == .pinned && targetSection.sectionId == .regular {
                    return .unpinFolder(folderId: folderId)
                }
                if sourceSection?.sectionId == .regular && targetSection.sectionId == .pinned {
                    return .pinFolder(folderId: folderId)
                }
                if sourceParentFolderId != nil {
                    return .moveFolderToRoot(folderId: folderId, targetIndex: childIndex, sectionId: targetSection.sectionId)
                }
                return .reorderFolder(folderId: folderId, targetIndex: childIndex, sectionId: targetSection.sectionId)
            }
        }

        return .none
    }

    func executeIntent(_ intent: DropIntent, sourceOrigin: String? = nil) {
        guard let spaceId = controller?.activeSpaceId else {
            return
        }

        // When dragging from favorites to pinned/regular, unfavorite first so the tab
        // actually leaves the favorites surface (move semantics, not copy).
        let sourceIsFavorites = sourceOrigin == "favorites"
        if sourceIsFavorites {
            switch intent {
            case .pinTab(let tabId), .unpinTab(let tabId),
                 .moveTabToFolder(let tabId, _), .splitTab(let tabId, _, _):
                CoreUpdateDispatcher.shared.sendAndDispatch(.unfavoriteTab(tabId: tabId))
            default:
                break
            }
        }

        let event: ShellEvent?
        var expandFolderId: FolderId? = nil
        var postAction: (() -> Void)? = nil

        switch intent {
        case let .reorderTab(tabId, beforeTabId, _):
            event = .reorderTab(tabId: tabId, beforeTabId: beforeTabId)
        case .moveTabToFolder(let tabId, let folderId):
            event = .moveTabToFolder(spaceId: spaceId, folderId: folderId, tabId: tabId)
            expandFolderId = folderId
        case .reorderTabInFolder(let tabId, let folderId, let targetIndex):
            event = .reorderTabInFolder(spaceId: spaceId, folderId: folderId, tabId: tabId, to: targetIndex)
        case .removeTabFromFolder(let tabId, let folderId):
            event = .removeTabFromFolder(spaceId: spaceId, folderId: folderId, tabId: tabId)
        case let .splitTab(sourceId, targetId, side):
            event = .createSplit(
                tabIds: side == .left ? [sourceId, targetId] : [targetId, sourceId],
                orientation: .horizontal
            )
        case .reorderFolder(let folderId, let targetIndex, let sectionId):
            let snap = controller?.dataSource.snapshot ?? SidebarSnapshot(rootSections: [])
            let parentFolderId = findParentFolderForFolder(folderId: folderId, in: snap)
            let beforeFolderId = findSiblingFolderIdAtIndex(targetIndex: targetIndex, draggingFolderId: folderId, parentFolderId: parentFolderId, sectionId: sectionId, in: snap)
            event = .reorderFolder(spaceId: spaceId, folderId: folderId, parentFolderId: parentFolderId, beforeFolderId: beforeFolderId)
        case .moveFolderIntoFolder(let folderId, let targetFolderId):
            event = .moveFolderIntoFolder(spaceId: spaceId, folderId: folderId, targetFolderId: targetFolderId)
            expandFolderId = targetFolderId
        case .moveFolderToRoot(let folderId, let targetIndex, let sectionId):
            let snap = controller?.dataSource.snapshot ?? SidebarSnapshot(rootSections: [])
            let beforeFolderId = findSiblingFolderIdAtIndex(targetIndex: targetIndex, draggingFolderId: folderId, parentFolderId: nil, sectionId: sectionId, in: snap)
            event = .moveFolderToRoot(spaceId: spaceId, folderId: folderId, beforeFolderId: beforeFolderId)
        case .pinTab(let tabId):
            event = .pinTab(tabId: tabId)
        case .unpinTab(let tabId):
            event = .unpinTab(tabId: tabId)
        case .pinFolder(let folderId):
            event = .pinFolder(spaceId: spaceId, folderId: folderId)
            postAction = { [weak self] in
                self?.expandFolder(folderId: folderId)
            }
        case .unpinFolder(let folderId):
            event = .unpinFolder(spaceId: spaceId, folderId: folderId)
            postAction = { [weak self] in
                self?.expandFolder(folderId: folderId)
            }
        case .none:
            event = nil
        }

        if let event = event {
            CoreUpdateDispatcher.shared.sendAndDispatch(event)
        }

        controller?.requestReload()
        DispatchQueue.main.async { [weak self] in
            if let folderId = expandFolderId {
                self?.expandFolder(folderId: folderId)
            }
            postAction?()
        }
    }

    private func parsePasteboard(_ pasteboard: NSPasteboard) -> (type: String, id: String, origin: String?)? {
        guard let string = pasteboard.string(forType: SidebarDragController.dragType),
              let data = string.data(using: .utf8),
              let dict = try? JSONSerialization.jsonObject(with: data) as? [String: String],
              let type = dict["type"],
              let id = dict["id"] else {
            return nil
        }
        return (type, id, dict["origin"])
    }

    func parsePasteboardMulti(_ pasteboard: NSPasteboard) -> [(type: String, id: String, origin: String?)] {
        guard let items = pasteboard.pasteboardItems else { return [] }
        var results: [(type: String, id: String, origin: String?)] = []
        for item in items {
            guard let string = item.string(forType: SidebarDragController.dragType),
                  let data = string.data(using: .utf8),
                  let dict = try? JSONSerialization.jsonObject(with: data) as? [String: String],
                  let type = dict["type"],
                  let id = dict["id"] else {
                continue
            }
            results.append((type: type, id: id, origin: dict["origin"]))
        }
        return results
    }

    func resolveMultiDropIntent(sources: [(type: String, id: String, origin: String?)], targetItem: Any?, childIndex: Int, outlineView: NSOutlineView) -> [DropIntent] {
        // For mixed tab+folder selections, only process tabs
        let hasMixedTypes = Set(sources.map { $0.type }).count > 1
        let filtered = hasMixedTypes ? sources.filter { $0.type == "tab" } : sources

        var intents: [DropIntent] = []
        for source in filtered {
            let intent = resolveDropIntent(
                sourceType: source.type,
                sourceId: source.id,
                sourceOrigin: source.origin,
                targetItem: targetItem,
                childIndex: childIndex,
                outlineView: outlineView
            )
            if intent != .none {
                intents.append(intent)
            }
        }
        return intents
    }

    func executeIntents(_ intents: [DropIntent], sourceOrigin: String?) {
        for intent in intents {
            executeIntent(intent, sourceOrigin: sourceOrigin)
        }
    }

    private func findSectionForTab(tabId: TabId, in snapshot: SidebarSnapshot) -> SectionNode? {
        for section in snapshot.rootSections {
            for child in section.children {
                if let tab = child as? TabNode, tab.tabId == tabId {
                    return section
                }
                if let folder = child as? SectionNode, folder.isFolder {
                    for folderChild in folder.children {
                        if let tab = folderChild as? TabNode, tab.tabId == tabId {
                            return section
                        }
                    }
                }
            }
        }
        return nil
    }

    private func findFolderForTab(tabId: TabId, in snapshot: SidebarSnapshot) -> FolderId? {
        for section in snapshot.rootSections {
            for child in section.children {
                if let folder = child as? SectionNode, folder.isFolder {
                    for folderChild in folder.children {
                        if let tab = folderChild as? TabNode, tab.tabId == tabId {
                            return folder.folderId
                        }
                    }
                }
            }
        }
        return nil
    }

    private func findSectionForFolder(folderId: FolderId, in snapshot: SidebarSnapshot) -> SectionNode? {
        for section in snapshot.rootSections {
            if findSectionForFolderRecursive(folderId: folderId, in: section.children) != nil {
                return section
            }
        }
        return nil
    }

    private func findParentFolderForFolder(folderId: FolderId, in snapshot: SidebarSnapshot) -> FolderId? {
        for section in snapshot.rootSections {
            if let found = findParentFolderForFolderRecursive(folderId: folderId, in: section.children) {
                return found
            }
        }
        return nil
    }

    private func findParentFolderForFolderRecursive(folderId: FolderId, in children: [AnyObject]) -> FolderId? {
        for child in children {
            if let folder = child as? SectionNode, folder.isFolder {
                for sub in folder.children {
                    if let subFolder = sub as? SectionNode, subFolder.isFolder, subFolder.folderId == folderId {
                        return folder.folderId
                    }
                }
                if let found = findParentFolderForFolderRecursive(folderId: folderId, in: folder.children) {
                    return found
                }
            }
        }
        return nil
    }

    private func findSectionForFolderRecursive(folderId: FolderId, in children: [AnyObject]) -> SectionNode? {
        for child in children {
            if let folder = child as? SectionNode, folder.isFolder {
                if folder.folderId == folderId {
                    return folder
                }
                if let found = findSectionForFolderRecursive(folderId: folderId, in: folder.children) {
                    return found
                }
            }
        }
        return nil
    }

    private func findBeforeTabId(at childIndex: Int, in section: SectionNode) -> TabId? {
        guard childIndex >= 0, childIndex < section.children.count else {
            return nil
        }
        for i in childIndex..<section.children.count {
            if let tab = section.children[i] as? TabNode {
                return tab.tabId
            }
        }
        return nil
    }

    private func expandFolder(folderId: FolderId) {
        guard let snapshot = controller?.dataSource.snapshot else { return }
        for section in snapshot.rootSections {
            if let found = findFolderToExpand(folderId: folderId, in: section.children) {
                controller?.outlineView.expandItem(found)
                return
            }
        }
    }

    func resolveDropByPosition(sourceType: String, sourceId: String, sourceOrigin: String?, point: NSPoint, outlineView: NSOutlineView, snapshot: SidebarSnapshot) -> DropIntent {
        let pinnedSection = snapshot.rootSections.first(where: { $0.sectionId == .pinned })
        let regularSection = snapshot.rootSections.first(where: { $0.sectionId == .regular })
        let separatorRow = (0..<outlineView.numberOfRows).first { row in
            (outlineView.item(atRow: row) as? SectionNode)?.sectionId == .separator
        }

        let isAboveSeparator: Bool
        if let separatorRow {
            isAboveSeparator = point.y < outlineView.rect(ofRow: separatorRow).midY
        } else if let pinnedRow = rowForSection(.pinned, in: outlineView), let regularRow = rowForSection(.regular, in: outlineView) {
            let pinnedRect = outlineView.rect(ofRow: pinnedRow)
            let regularRect = outlineView.rect(ofRow: regularRow)
            isAboveSeparator = point.y < ((pinnedRect.minY + regularRect.maxY) / 2)
        } else if pinnedSection?.children.isEmpty == true, regularSection?.children.isEmpty != true {
            isAboveSeparator = false
        } else {
            isAboveSeparator = true
        }

        if sourceType == "tab" {
            let sourceSection = findSectionForTab(tabId: sourceId, in: snapshot)
            let sourceFolderId = findFolderForTab(tabId: sourceId, in: snapshot)
            let sourceOriginIsFavorites = sourceOrigin == "favorites"
            if isAboveSeparator && (sourceSection?.sectionId == .regular || sourceOriginIsFavorites) {
                return .pinTab(tabId: sourceId)
            }
            if !isAboveSeparator && (sourceSection?.sectionId == .pinned || sourceOriginIsFavorites) {
                return .unpinTab(tabId: sourceId)
            }
            // Same-section background drop: remove from folder or reorder to end
            if let folderId = sourceFolderId {
                return .removeTabFromFolder(tabId: sourceId, folderId: folderId)
            }
            let targetSectionId: SectionID = isAboveSeparator ? .pinned : .regular
            if sourceSection?.sectionId == targetSectionId {
                return .reorderTab(tabId: sourceId, beforeTabId: nil, sectionId: targetSectionId)
            }
        }

        if sourceType == "folder" {
            let sourceSection = findSectionForFolder(folderId: sourceId, in: snapshot)
            let sourceParentFolderId = findParentFolderForFolder(folderId: sourceId, in: snapshot)
            if isAboveSeparator && sourceSection?.sectionId == .regular {
                return .pinFolder(folderId: sourceId)
            }
            if !isAboveSeparator && sourceSection?.sectionId == .pinned {
                return .unpinFolder(folderId: sourceId)
            }
            // Same-section background drop: move nested folder to root or reorder to end
            let targetSectionId: SectionID = isAboveSeparator ? .pinned : .regular
            if sourceSection?.sectionId == targetSectionId {
                if sourceParentFolderId != nil {
                    let section = snapshot.rootSections.first(where: { $0.sectionId == targetSectionId })
                    let endIndex = section?.children.count ?? 0
                    return .moveFolderToRoot(folderId: sourceId, targetIndex: endIndex, sectionId: targetSectionId)
                }
                let section = snapshot.rootSections.first(where: { $0.sectionId == targetSectionId })
                let endIndex = section?.children.count ?? 0
                return .reorderFolder(folderId: sourceId, targetIndex: endIndex, sectionId: targetSectionId)
            }
        }

        return .none
    }

    private func clearRowDropState(in outlineView: NSOutlineView) {
        for row in 0..<outlineView.numberOfRows {
            if let rowView = outlineView.rowView(atRow: row, makeIfNecessary: false) as? SidebarRowView {
                rowView.splitHighlightSide = nil
                rowView.customDropTargetHighlight = false
            }
        }
    }

    private func configureDraggingFeedback(for intent: DropIntent, in outlineView: NSOutlineView) {
        if defaultDraggingFeedbackStyle == nil {
            defaultDraggingFeedbackStyle = outlineView.draggingDestinationFeedbackStyle
        }

        let sidebarOutlineView = outlineView as? SidebarOutlineView

        if intent == .none || usesCustomDraggingFeedback(for: intent) {
            outlineView.draggingDestinationFeedbackStyle = .none
            outlineView.setDropItem(nil, dropChildIndex: NSOutlineViewDropOnItemIndex)
            sidebarOutlineView?.setCustomInsertionIndicatorVisible(false)
            return
        }

        outlineView.draggingDestinationFeedbackStyle = defaultDraggingFeedbackStyle ?? .regular
        sidebarOutlineView?.setCustomInsertionIndicatorVisible(true)
    }

    private func rowForSection(_ sectionId: SectionID, in outlineView: NSOutlineView) -> Int? {
        (0..<outlineView.numberOfRows).first { row in
            (outlineView.item(atRow: row) as? SectionNode)?.sectionId == sectionId
        }
    }

    private func usesCustomDraggingFeedback(for intent: DropIntent) -> Bool {
        switch intent {
        case .pinTab(tabId: _),
             .unpinTab(tabId: _),
             .pinFolder(folderId: _),
             .unpinFolder(folderId: _),
             .splitTab(sourceId: _, targetId: _, side: _):
            return true
        case .none,
             .reorderTab(tabId: _, beforeTabId: _, sectionId: _),
             .moveTabToFolder(tabId: _, folderId: _),
             .reorderTabInFolder(tabId: _, folderId: _, targetIndex: _),
             .removeTabFromFolder(tabId: _, folderId: _),
             .reorderFolder(folderId: _, targetIndex: _, sectionId: _),
             .moveFolderIntoFolder(folderId: _, targetFolderId: _),
             .moveFolderToRoot(folderId: _, targetIndex: _, sectionId: _):
            return false
        }
    }

    private func findFolderToExpand(folderId: FolderId, in children: [AnyObject]) -> SectionNode? {
        for child in children {
            if let folder = child as? SectionNode, folder.isFolder {
                if folder.folderId == folderId {
                    return folder
                }
                if let found = findFolderToExpand(folderId: folderId, in: folder.children) {
                    return found
                }
            }
        }
        return nil
    }

    func findSiblingFolderIdAtIndex(targetIndex: Int, draggingFolderId: FolderId, parentFolderId: FolderId?, sectionId: SectionID, in snapshot: SidebarSnapshot) -> FolderId? {
        // targetIndex is an NSOutlineView childIndex that counts every child type.
        let allChildren: [AnyObject]
        if let parentId = parentFolderId {
            var found: [AnyObject] = []
            for section in snapshot.rootSections {
                if let parent = findFolderNode(folderId: parentId, in: section.children) {
                    found = parent.children
                    break
                }
            }
            allChildren = found
        } else {
            guard let section = snapshot.rootSections.first(where: { $0.sectionId == sectionId }) else {
                return nil
            }
            allChildren = section.children
        }

        var foldersBefore = 0
        for i in 0..<min(targetIndex, allChildren.count) {
            if let folder = allChildren[i] as? SectionNode,
               folder.isFolder,
               folder.folderId != draggingFolderId {
                foldersBefore += 1
            }
        }

        let siblingFolders = allChildren
            .compactMap { $0 as? SectionNode }
            .filter { $0.isFolder && $0.folderId != draggingFolderId }

        if foldersBefore < siblingFolders.count {
            return siblingFolders[foldersBefore].folderId
        }
        return nil
    }

    private func findFolderNode(folderId: FolderId, in children: [AnyObject]) -> SectionNode? {
        for child in children {
            if let folder = child as? SectionNode, folder.isFolder {
                if folder.folderId == folderId { return folder }
                if let found = findFolderNode(folderId: folderId, in: folder.children) { return found }
            }
        }
        return nil
    }
}
