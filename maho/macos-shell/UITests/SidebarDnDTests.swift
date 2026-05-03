import XCTest
import AppKit
import ApplicationServices

final class SidebarDnDTests: XCTestCase {
    var app: XCUIApplication!

    private func waitForLabel(_ element: XCUIElement, toBecome expected: String, timeout: TimeInterval = 3) -> Bool {
        let predicate = NSPredicate(format: "label == %@", expected)
        let expectation = XCTNSPredicateExpectation(predicate: predicate, object: element)
        return XCTWaiter.wait(for: [expectation], timeout: timeout) == .completed
    }

    private func clickElementCenter(_ element: XCUIElement) {
        element.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)).click()
    }

    private func waitForFrameChange(
        of element: XCUIElement,
        timeout: TimeInterval = 5,
        condition: @escaping (CGRect) -> Bool
    ) -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if element.waitForExistence(timeout: 0.2), condition(element.frame) {
                return true
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        }
        return false
    }

    private func mainWindow() -> XCUIElement {
        let window = app.windows["mainWindow"]
        XCTAssertTrue(window.waitForExistence(timeout: 5), "Main window should exist")
        return window
    }

    private func sidebarOutline(in window: XCUIElement) -> XCUIElement {
        let outline = window.descendants(matching: .outline)
            .element(matching: NSPredicate(format: "identifier == %@", "sidebar.outline"))
        XCTAssertTrue(outline.waitForExistence(timeout: 5), "Sidebar outline should exist")
        return outline
    }

    private func element(withIdentifier identifier: String, in container: XCUIElement) -> XCUIElement {
        container.descendants(matching: .any)
            .element(matching: NSPredicate(format: "identifier == %@", identifier))
    }

    private func favoriteTile(at index: Int, in container: XCUIElement) -> XCUIElement {
        element(withIdentifier: "sidebar.favorite.\(index)", in: container)
    }

    private func tabCell(containing text: String, in outline: XCUIElement) -> XCUIElement {
        outline.descendants(matching: .any).element(
            matching: NSPredicate(format: "identifier BEGINSWITH %@ AND label CONTAINS[c] %@", "sidebar.tab.", text)
        )
    }

    private func tabCell(withIdentifier identifier: String, in outline: XCUIElement) -> XCUIElement {
        outline.descendants(matching: .any)
            .element(matching: NSPredicate(format: "identifier == %@", identifier))
    }

    private func folderCell(named name: String, in outline: XCUIElement) -> XCUIElement {
        outline.cells.containing(NSPredicate(format: "label CONTAINS[c] %@", name)).firstMatch
    }

    private func folderCell(withExactLabel label: String, in outline: XCUIElement) -> XCUIElement {
        outline.cells.element(matching: NSPredicate(format: "label == %@", label))
    }

    private func visibleRegularRootTabs(in window: XCUIElement, outline: XCUIElement) -> [XCUIElement] {
        let regularSection = element(withIdentifier: "sidebar.section.regular", in: window)
        guard regularSection.waitForExistence(timeout: 3) else { return [] }

        let candidates = outline.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier BEGINSWITH %@", "sidebar.tab."))

        var regularTabs: [(element: XCUIElement, frame: CGRect)] = []
        for index in 0..<candidates.count {
            let candidate = candidates.element(boundBy: index)
            guard candidate.waitForExistence(timeout: 0.2) else { continue }
            let frame = candidate.frame
            if frame.minY > regularSection.frame.minY {
                regularTabs.append((candidate, frame))
            }
        }

        guard let minimumX = regularTabs.map({ $0.frame.minX }).min() else { return [] }
        return regularTabs
            .filter { abs($0.frame.minX - minimumX) < 4 }
            .sorted { $0.frame.minY < $1.frame.minY }
            .map { $0.element }
    }

    private func firstVisibleRegularRootTab(in window: XCUIElement, outline: XCUIElement) -> XCUIElement? {
        visibleRegularRootTabs(in: window, outline: outline).first
    }

    private func visibleFolders(in outline: XCUIElement) -> [XCUIElement] {
        let candidates = outline.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier BEGINSWITH %@", "sidebar.folder."))

        var folders: [(element: XCUIElement, frame: CGRect)] = []
        for index in 0..<candidates.count {
            let candidate = candidates.element(boundBy: index)
            guard candidate.waitForExistence(timeout: 0.2) else { continue }
            folders.append((candidate, candidate.frame))
        }

        return folders
            .sorted { $0.frame.minY < $1.frame.minY }
            .map { $0.element }
    }

    private func waitForExistence(of element: XCUIElement, timeout: TimeInterval = 3) -> Bool {
        element.waitForExistence(timeout: timeout)
    }

    private func uiTestState() -> [String: String] {
        let path = "/tmp/maho-uitest-state.json"
        guard let data = FileManager.default.contents(atPath: path),
              let object = try? JSONSerialization.jsonObject(with: data),
              let state = object as? [String: String] else {
            return [:]
        }
        return state
    }

    private func waitForUITestState(
        timeout: TimeInterval = 4,
        condition: ([String: String]) -> Bool
    ) -> [String: String]? {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            let state = uiTestState()
            if condition(state) {
                return state
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        }

        let finalState = uiTestState()
        return condition(finalState) ? finalState : nil
    }

    private func waitForSeededFavoritesToRender(in window: XCUIElement, timeout: TimeInterval = 10) -> Bool {
        XCTAssertTrue(window.waitForExistence(timeout: 1), "Main window should exist before waiting for seeded favorites")
        return waitForUITestState(timeout: timeout, condition: { state in
            state["favoritesSeededReady"] == "true"
        }) != nil
    }

    private func screenPoint(for coordinate: XCUICoordinate) -> CGPoint {
        let point = coordinate.screenPoint
        let screenHeight = NSScreen.screens.first(where: { NSMouseInRect(point, $0.frame, false) })?.frame.height
            ?? NSScreen.main?.frame.height
            ?? 0
        return CGPoint(x: point.x, y: screenHeight - point.y)
    }

    private func postMouseEvent(type: CGEventType, point: CGPoint, button: CGMouseButton = .left, source: CGEventSource) {
        let eventType: CGEventType
        switch type {
        case .leftMouseDown, .leftMouseDragged, .leftMouseUp:
            eventType = type
        default:
            eventType = type
        }
        let mouseEvent = CGEvent(mouseEventSource: source, mouseType: eventType, mouseCursorPosition: point, mouseButton: button)
        mouseEvent?.post(tap: .cghidEventTap)
    }

    private func dragElementCenterWithHold(
        _ source: XCUIElement,
        to destination: XCUIElement,
        holdDuration: TimeInterval = 1.0,
        moveSteps: Int = 12
    ) {
        XCTAssertTrue(source.waitForExistence(timeout: 3), "Drag source should exist")
        XCTAssertTrue(destination.waitForExistence(timeout: 3), "Drag destination should exist")

        let startPoint = screenPoint(for: source.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)))
        let endPoint = screenPoint(for: destination.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5)))
        guard let eventSource = CGEventSource(stateID: .hidSystemState) else {
            XCTFail("Expected Quartz event source for paused drag helper")
            return
        }
        eventSource.localEventsSuppressionInterval = 0

        postMouseEvent(type: .mouseMoved, point: startPoint, source: eventSource)
        usleep(50_000)
        postMouseEvent(type: .leftMouseDown, point: startPoint, source: eventSource)
        usleep(150_000)

        let stepCount = max(1, moveSteps)
        for step in 1...stepCount {
            let progress = CGFloat(step) / CGFloat(stepCount)
            let intermediatePoint = CGPoint(
                x: startPoint.x + ((endPoint.x - startPoint.x) * progress),
                y: startPoint.y + ((endPoint.y - startPoint.y) * progress)
            )
            postMouseEvent(type: .leftMouseDragged, point: intermediatePoint, source: eventSource)
            usleep(16_000)
        }

        RunLoop.current.run(until: Date().addingTimeInterval(holdDuration))
        postMouseEvent(type: .leftMouseUp, point: endPoint, source: eventSource)
    }

    private func dragElementCenter(_ source: XCUIElement, to destination: XCUIElement) {
        XCTAssertTrue(source.waitForExistence(timeout: 3), "Drag source should exist")
        XCTAssertTrue(destination.waitForExistence(timeout: 3), "Drag destination should exist")

        let start = source.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
        let end = destination.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
        start.click(forDuration: 1.0, thenDragTo: end)
    }

    private func dragAbove(_ source: XCUIElement, target: XCUIElement) {
        XCTAssertTrue(source.waitForExistence(timeout: 3), "Drag source should exist")
        XCTAssertTrue(target.waitForExistence(timeout: 3), "Drag target should exist")
        let start = source.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
        let end = target.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.1))
        start.click(forDuration: 1.0, thenDragTo: end)
    }

    private func childTabsOfFolder(_ folder: XCUIElement, in outline: XCUIElement) -> [XCUIElement] {
        let folderMaxY = folder.frame.maxY
        let folderMinX = folder.frame.minX
        let allTabs = outline.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier BEGINSWITH %@", "sidebar.tab."))

        var children: [(element: XCUIElement, frame: CGRect)] = []
        for i in 0..<allTabs.count {
            let tab = allTabs.element(boundBy: i)
            guard tab.waitForExistence(timeout: 0.2) else { continue }
            let frame = tab.frame
            if frame.minY >= folderMaxY && frame.minX > folderMinX {
                children.append((tab, frame))
            }
        }

        return children
            .sorted { $0.frame.minY < $1.frame.minY }
            .map { $0.element }
    }

    override func setUp() {
        super.setUp()
        continueAfterFailure = false
        app = XCUIApplication()
        app.terminate()
        app.launchArguments = ["--ui-testing"]
        app.launch()
    }

    override func tearDown() {
        app.terminate()
        super.tearDown()
    }

    func testSidebarExistsViaIdentifier() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5), "Main window should exist")

        let sidebarOutline = mainWindow.descendants(matching: .outline).element(matching: NSPredicate(format: "identifier == %@", "sidebar.outline"))
        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 5), "Sidebar outline should exist by identifier")
    }

    func testSearchPillOpensCommandBar() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let searchPill = mainWindow.buttons["sidebar.search"]
        XCTAssertTrue(searchPill.waitForExistence(timeout: 5), "Search pill should exist")

        searchPill.click()

        let commandBarSearch = app.textFields["commandBar.search"]
        XCTAssertTrue(commandBarSearch.waitForExistence(timeout: 3), "Command bar search field should appear after clicking search pill")
    }

    func testFavoriteTileExistsAndCanBeClicked() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let favoriteTile = mainWindow.descendants(matching: .any).element(matching: NSPredicate(format: "identifier == %@", "sidebar.favorite.0"))
        XCTAssertTrue(favoriteTile.waitForExistence(timeout: 5), "First favorite tile should exist in deterministic state")

        favoriteTile.click()

        XCTAssertTrue(favoriteTile.waitForExistence(timeout: 2), "Favorite tile should remain after click without crash")
    }

    func testNewTabRowExistsAndClicksWithoutCrash() {
        let mainWindow = mainWindow()
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let newTabRow = element(withIdentifier: "sidebar.newTab", in: mainWindow)
        XCTAssertTrue(newTabRow.waitForExistence(timeout: 5), "New tab row should exist")

        let outlineBefore = mainWindow.descendants(matching: .outline).element(matching: NSPredicate(format: "identifier == %@", "sidebar.outline"))
        XCTAssertTrue(outlineBefore.waitForExistence(timeout: 3))
        let cellCountBefore = outlineBefore.cells.count

        newTabRow.click()

        XCTAssertTrue(newTabRow.waitForExistence(timeout: 2), "New tab row should still exist after click")
        let cellCountAfter = outlineBefore.cells.count
        XCTAssertGreaterThanOrEqual(cellCountAfter, cellCountBefore, "Clicking new tab should not reduce sidebar cell count")
    }

    func testSidebarToggleHidesAndShowsSidebar() {
        let mainWindow = mainWindow()
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let toggleButton = mainWindow.buttons["sidebar.nav.toggle"]
        XCTAssertTrue(toggleButton.waitForExistence(timeout: 5), "Sidebar toggle button should exist")

        let sidebarOutline = mainWindow.descendants(matching: .outline).element(matching: NSPredicate(format: "identifier == %@", "sidebar.outline"))
        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 3), "Sidebar should initially be visible")

        toggleButton.click()
        sleep(1)
        XCTAssertFalse(sidebarOutline.isHittable, "Sidebar outline should be hidden after toggle")

        let toggleSidebarMenuItem = app.menuBars.menuItems["Toggle Sidebar"]
        XCTAssertTrue(toggleSidebarMenuItem.waitForExistence(timeout: 3), "Toggle Sidebar menu item should exist")
        toggleSidebarMenuItem.click()
        sleep(1)

        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 3), "Sidebar outline should be visible after restoring via menu")
    }

    func testFolderExpandCollapseIsExercised() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)

        let testFolderCell = folderCell(named: "Work", in: sidebarOutline)
        if testFolderCell.waitForExistence(timeout: 3) {
            XCTAssertEqual(testFolderCell.label, "Work expanded", "Seeded folder should start expanded")
            clickElementCenter(testFolderCell)
            XCTAssertTrue(
                folderCell(withExactLabel: "Work collapsed", in: sidebarOutline).waitForExistence(timeout: 3),
                "Collapsing Work should update accessibility label state"
            )
            XCTAssertTrue(folderCell(named: "Work", in: sidebarOutline).waitForExistence(timeout: 2), "Folder cell should persist after collapse")

            clickElementCenter(folderCell(withExactLabel: "Work collapsed", in: sidebarOutline))
            XCTAssertTrue(
                folderCell(withExactLabel: "Work expanded", in: sidebarOutline).waitForExistence(timeout: 3),
                "Expanding Work should restore accessibility label state"
            )
        } else {
            XCTFail("Work should exist in deterministic --ui-testing state")
        }
    }

    func testSidebarDoesNotShowTodaySection() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)

        let todayLabel = sidebarOutline.staticTexts["Today"]
        XCTAssertFalse(todayLabel.exists, "Sidebar should not render a visible Today section")
    }

    func testSidebarHidesPinnedAndNormalVisibleLabels() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)

        XCTAssertTrue(element(withIdentifier: "sidebar.section.pinned", in: mainWindow).waitForExistence(timeout: 3))
        XCTAssertTrue(element(withIdentifier: "sidebar.section.regular", in: mainWindow).waitForExistence(timeout: 3))

        XCTAssertFalse(sidebarOutline.staticTexts["Pinned"].exists, "Pinned label should stay visually hidden")
        XCTAssertFalse(sidebarOutline.staticTexts["Normal"].exists, "Normal label should stay visually hidden")
    }

    func testRegularTabCanBePinnedAndUnpinnedViaContextMenu() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)

        guard let regularTab = firstVisibleRegularRootTab(in: mainWindow, outline: sidebarOutline) else {
            return XCTFail("A visible regular root tab should exist in deterministic state")
        }

        let tabIdentifier = regularTab.identifier
        let pinnedSection = element(withIdentifier: "sidebar.section.pinned", in: mainWindow)
        XCTAssertTrue(pinnedSection.waitForExistence(timeout: 2), "Pinned section should exist")
        let regularSection = element(withIdentifier: "sidebar.section.regular", in: mainWindow)
        XCTAssertTrue(regularSection.waitForExistence(timeout: 2), "Regular section should exist")

        let tabFrame = regularTab.frame
        let outlineFrame = sidebarOutline.frame
        let relX = (tabFrame.midX - outlineFrame.minX) / outlineFrame.width
        let relY = (tabFrame.midY - outlineFrame.minY) / outlineFrame.height
        sidebarOutline.coordinate(withNormalizedOffset: CGVector(dx: relX, dy: relY)).rightClick()

        let pinMenuItem = app.menus.menuItems["Pin"]
        XCTAssertTrue(pinMenuItem.waitForExistence(timeout: 2), "Pin menu item should exist")
        pinMenuItem.click()

        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 5), "Sidebar should remain stable after pin action")

        let pinnedTab = sidebarOutline.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", tabIdentifier))
            .firstMatch
        XCTAssertTrue(pinnedTab.waitForExistence(timeout: 3),
                      "Tab \(tabIdentifier) should still be visible after pinning")
        XCTAssertLessThan(pinnedTab.frame.midY, regularSection.frame.minY,
                          "Pinned tab should appear above the regular section")

        let pinnedTabFrame = pinnedTab.frame
        let relX2 = (pinnedTabFrame.midX - outlineFrame.minX) / outlineFrame.width
        let relY2 = (pinnedTabFrame.midY - outlineFrame.minY) / outlineFrame.height
        sidebarOutline.coordinate(withNormalizedOffset: CGVector(dx: relX2, dy: relY2)).rightClick()

        let unpinMenuItem = app.menus.menuItems["Unpin"]
        XCTAssertTrue(unpinMenuItem.waitForExistence(timeout: 2), "Unpin menu item should exist")
        unpinMenuItem.click()

        let unpinnedTab = sidebarOutline.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", tabIdentifier))
            .firstMatch
        XCTAssertTrue(unpinnedTab.waitForExistence(timeout: 3),
                      "Tab \(tabIdentifier) should still be visible after unpinning")
        XCTAssertGreaterThan(unpinnedTab.frame.midY, regularSection.frame.minY,
                             "Unpinned tab should appear below the regular section header")
    }

    func testDragTabToPinnedSectionDoesNotCrash() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)
        let pinnedSection = element(withIdentifier: "sidebar.section.pinned", in: mainWindow)
        let regularSection = element(withIdentifier: "sidebar.section.regular", in: mainWindow)

        guard let regularTab = firstVisibleRegularRootTab(in: mainWindow, outline: sidebarOutline) else {
            return XCTFail("A visible regular root tab should exist in deterministic state")
        }

        let tabIdentifier = regularTab.identifier
        dragElementCenter(regularTab, to: pinnedSection)

        let pinnedTab = tabCell(withIdentifier: tabIdentifier, in: sidebarOutline)
        XCTAssertTrue(
            waitForFrameChange(of: pinnedTab, timeout: 5) { frame in
                frame.midY < regularSection.frame.minY
            },
            "Tab \(tabIdentifier) should move into the pinned section above the regular section"
        )
        XCTAssertLessThan(pinnedTab.frame.midY, regularSection.frame.minY, "Pinned tab should appear above the regular section")
    }

    func testDragTabToFolderDoesNotCrash() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)

        guard let regularTab = firstVisibleRegularRootTab(in: mainWindow, outline: sidebarOutline) else {
            return XCTFail("A visible regular root tab should exist in deterministic state")
        }
        let tabIdentifier = regularTab.identifier
        let targetFolder = folderCell(named: "Work", in: sidebarOutline)
        XCTAssertTrue(targetFolder.waitForExistence(timeout: 3), "Seeded folder should exist")

        let initialFolderLabel = targetFolder.label
        dragElementCenter(regularTab, to: targetFolder)

        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 3), "Sidebar should remain available after folder drag gesture")
        XCTAssertTrue(targetFolder.waitForExistence(timeout: 2), "Folder should remain visible after drag gesture")


        if targetFolder.label == initialFolderLabel && targetFolder.label.contains("collapsed") {
            clickElementCenter(targetFolder)
            XCTAssertTrue(
                waitForLabel(targetFolder, toBecome: "Work expanded", timeout: 3),
                "Dragging into Work should preserve or reveal the expanded folder state"
            )
        }
        let folderChildren = childTabsOfFolder(targetFolder, in: sidebarOutline)
        let tabInsideFolder = folderChildren.first { $0.identifier == tabIdentifier }
        XCTAssertNotNil(
            tabInsideFolder,
            "Dragged tab \(tabIdentifier) should appear inside the Work folder"
        )
    }

    func testDragTabBetweenRegularTabsReorders() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)
        let regularSection = element(withIdentifier: "sidebar.section.regular", in: mainWindow)
        XCTAssertTrue(regularSection.waitForExistence(timeout: 3), "Regular section should exist")

        let regularTabs = visibleRegularRootTabs(in: mainWindow, outline: sidebarOutline)
        guard regularTabs.count >= 2 else {
            return XCTFail("Expected at least two regular root tabs in deterministic state")
        }

        let firstTab = regularTabs[0]
        let secondTab = regularTabs[1]
        let firstIdentifier = firstTab.identifier
        let secondIdentifier = secondTab.identifier
        let firstInitialY = firstTab.frame.minY
        let secondInitialY = secondTab.frame.minY

        dragAbove(secondTab, target: firstTab)

        let reorderedFirst = tabCell(withIdentifier: firstIdentifier, in: sidebarOutline)
        let reorderedSecond = tabCell(withIdentifier: secondIdentifier, in: sidebarOutline)
        XCTAssertTrue(
            waitForFrameChange(of: reorderedSecond, timeout: 5) { _ in
                reorderedSecond.frame.minY < reorderedFirst.frame.minY
            },
            "Dragging the second tab above the first should reverse their vertical order"
        )
        XCTAssertLessThan(reorderedSecond.frame.minY, reorderedFirst.frame.minY, "Tabs should reorder vertically after drag")
        XCTAssertNotEqual(reorderedFirst.frame.minY, firstInitialY, "First tab should move from its original Y position")
        XCTAssertNotEqual(reorderedSecond.frame.minY, secondInitialY, "Second tab should move from its original Y position")
    }

    func testDragFolderToReorder() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)

        var folders = visibleFolders(in: sidebarOutline)
        if folders.count < 2 {
            let contextTarget = folders.first ?? sidebarOutline
            contextTarget.rightClick()

            let newFolderMenuItem = app.menus.menuItems["New Folder"]
            XCTAssertTrue(newFolderMenuItem.waitForExistence(timeout: 3), "New Folder menu item should exist")
            newFolderMenuItem.click()

            XCTAssertTrue(
                waitForFrameChange(of: sidebarOutline, timeout: 5) { _ in
                    self.visibleFolders(in: sidebarOutline).count >= 2
                },
                "Creating a new folder should result in at least two folders"
            )
            folders = visibleFolders(in: sidebarOutline)
        }

        guard folders.count >= 2 else {
            return XCTFail("Expected at least two folders to test folder reordering")
        }

        let firstFolder = folders[0]
        let secondFolder = folders[1]
        let firstIdentifier = firstFolder.identifier
        let secondIdentifier = secondFolder.identifier
        let firstInitialY = firstFolder.frame.minY
        let secondInitialY = secondFolder.frame.minY

        dragAbove(secondFolder, target: firstFolder)

        let reorderedFirst = element(withIdentifier: firstIdentifier, in: sidebarOutline)
        let reorderedSecond = element(withIdentifier: secondIdentifier, in: sidebarOutline)
        XCTAssertTrue(
            waitForFrameChange(of: reorderedSecond, timeout: 5) { _ in
                reorderedSecond.frame.minY < reorderedFirst.frame.minY
            },
            "Dragging one folder above another should change their relative order"
        )
        XCTAssertLessThan(reorderedSecond.frame.minY, reorderedFirst.frame.minY, "Folder order should change after drag")
        XCTAssertNotEqual(reorderedFirst.frame.minY, firstInitialY, "First folder should move from its original Y position")
        XCTAssertNotEqual(reorderedSecond.frame.minY, secondInitialY, "Second folder should move from its original Y position")
    }

    func testDragTabOutOfFolderRemovesFromFolder() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)
        let regularSection = element(withIdentifier: "sidebar.section.regular", in: mainWindow)
        XCTAssertTrue(regularSection.waitForExistence(timeout: 3), "Regular section should exist")

        let workFolder = folderCell(named: "Work", in: sidebarOutline)
        XCTAssertTrue(workFolder.waitForExistence(timeout: 3), "Work folder should exist")

        if workFolder.label.contains("collapsed") {
            clickElementCenter(workFolder)
            XCTAssertTrue(
                waitForLabel(workFolder, toBecome: "Work expanded", timeout: 3),
                "Work folder should be expanded before dragging a child tab out"
            )
        }

        let folderChildTabs = childTabsOfFolder(workFolder, in: sidebarOutline)
        guard !folderChildTabs.isEmpty else {
            return XCTFail("Work folder should contain at least one tab in deterministic state")
        }

        let childTab = folderChildTabs[0]
        let childIdentifier = childTab.identifier
        dragElementCenter(childTab, to: regularSection)

        let remainingFolderTabs = childTabsOfFolder(workFolder, in: sidebarOutline)
        XCTAssertTrue(
            remainingFolderTabs.allSatisfy { $0.identifier != childIdentifier },
            "Dragged tab \(childIdentifier) should no longer appear inside the Work folder"
        )

        let rootLevelTab = tabCell(withIdentifier: childIdentifier, in: sidebarOutline)
        XCTAssertTrue(
            rootLevelTab.waitForExistence(timeout: 3),
            "Dragged tab \(childIdentifier) should appear at the root regular section"
        )
        XCTAssertGreaterThan(rootLevelTab.frame.minY, regularSection.frame.minY, "Dragged tab should sit below the regular section header")
    }

    func testContextMenuOnTab() {
        let mainWindow = mainWindow()
        let sidebarOutline = sidebarOutline(in: mainWindow)

        let cells = sidebarOutline.cells
        if cells.count > 0 {
            cells.firstMatch.rightClick()
            let menu = app.menus.firstMatch
            XCTAssertTrue(menu.waitForExistence(timeout: 2), "Context menu should appear on right-click")
            app.typeKey(.escape, modifierFlags: [])
        } else {
            XCTFail("Sidebar should have at least one cell to open context menu")
        }
    }

    func testAllSixFavoriteTilesExist() {
        let mainWindow = mainWindow()
        XCTAssertTrue(waitForSeededFavoritesToRender(in: mainWindow), "Seeded favorites should be ready before asserting favorite tile identifiers")

        for index in 0..<6 {
            let tile = element(withIdentifier: "sidebar.favorite.\(index)", in: mainWindow)
            XCTAssertTrue(
                tile.waitForExistence(timeout: 5),
                "Favorite tile \(index) should exist in deterministic state"
            )
        }

        let extraTile = element(withIdentifier: "sidebar.favorite.6", in: mainWindow)
        XCTAssertFalse(extraTile.exists, "Only 6 favorites should be seeded; tile 6 should not exist")
    }

    func testFavoriteTileRightClickDoesNotCrash() {
        let mainWindow = mainWindow()

        let tile = element(withIdentifier: "sidebar.favorite.0", in: mainWindow)
        XCTAssertTrue(tile.waitForExistence(timeout: 5), "First favorite tile should exist")

        tile.rightClick()
        app.typeKey(.escape, modifierFlags: [])

        XCTAssertTrue(tile.waitForExistence(timeout: 3), "Favorite tile should remain after right-click dismissal")
    }


    func testDragFavoriteTileReordersFavoritesGrid() {
        let mainWindow = mainWindow()

        let firstTile = favoriteTile(at: 0, in: mainWindow)
        let secondTile = favoriteTile(at: 1, in: mainWindow)
        XCTAssertTrue(firstTile.waitForExistence(timeout: 5), "First favorite tile should exist")
        XCTAssertTrue(secondTile.waitForExistence(timeout: 5), "Second favorite tile should exist")

        let firstInitialLabel = firstTile.label
        let secondInitialLabel = secondTile.label
        XCTAssertFalse(firstInitialLabel.isEmpty, "First favorite tile should expose a stable accessibility label")
        XCTAssertFalse(secondInitialLabel.isEmpty, "Second favorite tile should expose a stable accessibility label")
        XCTAssertNotEqual(firstInitialLabel, secondInitialLabel, "Seeded favorites should begin with distinct labels for deterministic reorder assertions")

        dragElementCenter(secondTile, to: firstTile)

        let reorderedFirstTile = favoriteTile(at: 0, in: mainWindow)
        let reorderedSecondTile = favoriteTile(at: 1, in: mainWindow)
        XCTAssertTrue(reorderedFirstTile.waitForExistence(timeout: 5), "First reordered favorite tile should exist")
        XCTAssertTrue(reorderedSecondTile.waitForExistence(timeout: 5), "Second reordered favorite tile should exist")

        XCTAssertTrue(
            waitForLabel(reorderedFirstTile, toBecome: secondInitialLabel, timeout: 5),
            "Dragging the second favorite onto the first slot should move it into the first favorites position"
        )
        XCTAssertTrue(
            waitForLabel(reorderedSecondTile, toBecome: firstInitialLabel, timeout: 5),
            "Dragging the second favorite onto the first slot should push the original first favorite to the next position"
        )
        XCTAssertEqual(reorderedFirstTile.label, secondInitialLabel, "Favorite slot 0 should now expose the dragged tile label")
        XCTAssertEqual(reorderedSecondTile.label, firstInitialLabel, "Favorite slot 1 should now expose the displaced tile label")
    }

    func testSidebarToggleShowsFavoritesBar() {
        let mainWindow = mainWindow()

        let toggleButton = mainWindow.buttons["sidebar.nav.toggle"]
        XCTAssertTrue(toggleButton.waitForExistence(timeout: 5), "Sidebar toggle button should exist")

        let firstTile = element(withIdentifier: "sidebar.favorite.0", in: mainWindow)
        XCTAssertTrue(firstTile.waitForExistence(timeout: 5), "First favorite tile should be visible with sidebar open")

        let sidebarOutline = mainWindow.descendants(matching: .outline)
            .element(matching: NSPredicate(format: "identifier == %@", "sidebar.outline"))
        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 3), "Sidebar outline should be visible")

        toggleButton.click()
        sleep(1)

        XCTAssertFalse(sidebarOutline.isHittable, "Sidebar outline should be hidden after toggle")

        let toggleSidebarMenuItem = app.menuBars.menuItems["Toggle Sidebar"]
        XCTAssertTrue(toggleSidebarMenuItem.waitForExistence(timeout: 3))
        toggleSidebarMenuItem.click()
        sleep(1)

        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 3), "Sidebar outline should be visible after restoring via menu")
        XCTAssertTrue(firstTile.waitForExistence(timeout: 3), "First favorite tile should reappear when sidebar is restored")
    }
}
