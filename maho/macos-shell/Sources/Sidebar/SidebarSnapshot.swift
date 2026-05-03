import AppKit

// MARK: - Section Identifier

enum SectionID: String, Equatable {
    case favorites = "favorites"
    case pinned = "pinned"
    case separator = "separator"
    case newTab = "newTab"
    case regular = "regular"
}

// MARK: - SectionNode
// CRITICAL: Do NOT override hash or isEqual — use NSObject identity (===)

class SectionNode: NSObject {
    let sectionId: SectionID
    let title: String
    var children: [AnyObject]
    var isFolder: Bool
    var folderId: FolderId?
    var isExpanded: Bool
    var isPinned: Bool
    var tabCount: Int

    init(
        sectionId: SectionID,
        title: String,
        children: [AnyObject] = [],
        isFolder: Bool = false,
        folderId: FolderId? = nil,
        isExpanded: Bool = true,
        isPinned: Bool = false,
        tabCount: Int = 0
    ) {
        self.sectionId = sectionId
        self.title = title
        self.children = children
        self.isFolder = isFolder
        self.folderId = folderId
        self.isExpanded = isExpanded
        self.isPinned = isPinned
        self.tabCount = tabCount
        super.init()
    }
}

// MARK: - TabNode
// CRITICAL: Do NOT override hash or isEqual — use NSObject identity (===)

class TabNode: NSObject {
    let tabId: TabId
    let title: String
    let customTitle: String?
    let url: String
    let favicon: ImageData?
    let isLoading: Bool
    let isPinned: Bool
    let isFavorite: Bool
    let isMuted: Bool
    let isPlayingAudio: Bool
    let lifecycleState: String

    init(
        tabId: TabId,
        title: String,
        customTitle: String?,
        url: String,
        favicon: ImageData?,
        isLoading: Bool,
        isPinned: Bool,
        isFavorite: Bool,
        isMuted: Bool,
        isPlayingAudio: Bool,
        lifecycleState: String
    ) {
        self.tabId = tabId
        self.title = title
        self.customTitle = customTitle
        self.url = url
        self.favicon = favicon
        self.isLoading = isLoading
        self.isPinned = isPinned
        self.isFavorite = isFavorite
        self.isMuted = isMuted
        self.isPlayingAudio = isPlayingAudio
        self.lifecycleState = lifecycleState
        super.init()
    }

    convenience init(from vm: TabViewModel) {
        self.init(
            tabId: vm.id,
            title: vm.title,
            customTitle: vm.customTitle,
            url: vm.url,
            favicon: vm.favicon,
            isLoading: vm.isLoading,
            isPinned: vm.isPinned,
            isFavorite: vm.isFavorite,
            isMuted: vm.isMuted,
            isPlayingAudio: vm.isPlayingAudio,
            lifecycleState: vm.lifecycleState
        )
    }
}

// MARK: - SidebarSnapshot

class LiveFolderItemNode: NSObject {
    let item: LiveFolderItem

    init(item: LiveFolderItem) {
        self.item = item
        super.init()
    }
}

struct SidebarSnapshot {
    let rootSections: [SectionNode]
    let isTablessSparseState: Bool

    init(rootSections: [SectionNode], isTablessSparseState: Bool = false) {
        self.rootSections = rootSections
        self.isTablessSparseState = isTablessSparseState
    }

    // CRITICAL: Creates NEW node instances every time — never cache/reuse
    static func build(
        pinnedTabs: [TabViewModel],
        regularTabs: [TabViewModel],
        folders: [FolderViewModel]
    ) -> SidebarSnapshot {
        var sections: [SectionNode] = []

        let pinnedFolderTabIds = Set(folders.filter(\.isPinned).flatMap(\.tabIds))
        let regularFolderTabIds = Set(folders.filter { !$0.isPinned }.flatMap(\.tabIds))

        // Build nested folder children recursively
        func buildFolderNode(for folder: FolderViewModel, sectionId: SectionID, availableTabs: [TabViewModel]) -> SectionNode {
            // Direct tabs in this folder
            let folderTabs = folder.tabIds.compactMap { tabId in
                availableTabs.first(where: { $0.id == tabId })
            }.map { TabNode(from: $0) }

            // Child folders whose parentFolderId matches this folder
            let childFolders = folders
                .filter { $0.parentFolderId == folder.id && $0.isPinned == folder.isPinned }
                .map { buildFolderNode(for: $0, sectionId: sectionId, availableTabs: availableTabs) }

            let children: [AnyObject] = childFolders as [AnyObject] + folderTabs as [AnyObject]

            return SectionNode(
                sectionId: sectionId,
                title: folder.name,
                children: children,
                isFolder: true,
                folderId: folder.id,
                isExpanded: folder.isExpanded,
                isPinned: folder.isPinned,
                tabCount: folder.tabCount
            )
        }

        // 1. Pinned section
        let pinnedFolders = folders.filter { $0.isPinned && $0.parentFolderId == nil }
        let allTabsForPinnedFolders = pinnedTabs + regularTabs
        let pinnedFolderChildren: [AnyObject] = pinnedFolders.map {
            buildFolderNode(for: $0, sectionId: .pinned, availableTabs: allTabsForPinnedFolders)
        }
        let pinnedTabsNotInFolder: [AnyObject] = pinnedTabs.filter { tab in
            !pinnedFolderTabIds.contains(tab.id)
        }.map { TabNode(from: $0) }
        let allPinnedChildren: [AnyObject] = pinnedFolderChildren + pinnedTabsNotInFolder
        let hasPinnedContent = !allPinnedChildren.isEmpty
        if hasPinnedContent {
            sections.append(SectionNode(
                sectionId: .pinned,
                title: "Pinned",
                children: allPinnedChildren
            ))
        }

        // 2. Separator between pinned and regular
        let regularFolders = folders.filter { !$0.isPinned && $0.parentFolderId == nil }
        let regularFolderChildren: [AnyObject] = regularFolders.map {
            buildFolderNode(for: $0, sectionId: .regular, availableTabs: regularTabs)
        }
        let regularTabsNotInFolder: [AnyObject] = regularTabs.filter { tab in
            !regularFolderTabIds.contains(tab.id) && !pinnedFolderTabIds.contains(tab.id)
        }.map { TabNode(from: $0) }
        let allRegularChildren: [AnyObject] = regularFolderChildren + regularTabsNotInFolder

        let hasRegularContent = !allRegularChildren.isEmpty
        let isTablessSparseState = !hasPinnedContent && !hasRegularContent

        if !isTablessSparseState {
            sections.append(SectionNode(
                sectionId: .separator,
                title: ""
            ))
        }

        sections.append(SectionNode(
            sectionId: .newTab,
            title: "New Tab"
        ))

        if hasRegularContent {
            sections.append(SectionNode(
                sectionId: .regular,
                title: "Normal",
                children: allRegularChildren
            ))
        }

        return SidebarSnapshot(rootSections: sections, isTablessSparseState: isTablessSparseState)
    }
}
