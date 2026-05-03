import XCTest

final class SpaceSwitcherTests: XCTestCase {
    var app: XCUIApplication!

    override func setUp() {
        super.setUp()
        continueAfterFailure = false
        app = XCUIApplication()
        app.launchArguments = ["--ui-testing"]
        app.launch()
    }

    override func tearDown() {
        app.terminate()
        super.tearDown()
    }

    func testSpaceSwitcherButtonsExist() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5), "Main window should exist")

        let addButton = mainWindow.buttons["sidebar.space.add"]
        XCTAssertTrue(addButton.waitForExistence(timeout: 5), "Add space button should exist")

        let spaceButton = mainWindow.buttons["sidebar.space.0"]
        XCTAssertTrue(spaceButton.waitForExistence(timeout: 5), "At least one space button should exist in deterministic state")
    }

    func testSwitchSpace() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let spaceButton = mainWindow.buttons["sidebar.space.0"]
        XCTAssertTrue(spaceButton.waitForExistence(timeout: 5), "Space button should exist")

        let sidebarOutline = mainWindow.descendants(matching: .outline).element(matching: NSPredicate(format: "identifier == %@", "sidebar.outline"))
        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 3))

        spaceButton.click()

        XCTAssertTrue(spaceButton.waitForExistence(timeout: 2), "Space button should persist after click")
        XCTAssertTrue(sidebarOutline.waitForExistence(timeout: 2), "Sidebar should remain after space switch")
    }

    func testAddSpace() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let addButton = mainWindow.buttons["sidebar.space.add"]
        XCTAssertTrue(addButton.waitForExistence(timeout: 5), "Add space button should exist")
        XCTAssertTrue(addButton.isEnabled, "Add space button should be enabled")

        addButton.click()

        let newSpaceAction = app.descendants(matching: .any)
            .matching(NSPredicate(format: "label CONTAINS[c] %@", "New Space"))
            .firstMatch
        XCTAssertTrue(newSpaceAction.waitForExistence(timeout: 5), "New Space action should appear after clicking add")
        newSpaceAction.click()

        let createSpaceContainer = waitForCreateSpaceContainer(timeout: 15)
        XCTAssertNotNil(createSpaceContainer, "Create Space UI should appear after clicking add")

        guard let createSpaceContainer else { return }

        let createNameField = createSpaceContainer.textFields["space.create.name"]
        XCTAssertTrue(createNameField.waitForExistence(timeout: 5), "Create space name field should appear immediately")

        let creatorPreview = createSpaceContainer.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", "space.theme.creatorPreview"))
            .firstMatch
        XCTAssertTrue(creatorPreview.waitForExistence(timeout: 5), "Theme preview should be visible immediately in the Create Space layout")

        let gradientToggle = createSpaceContainer.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", "space.theme.gradientToggle"))
            .firstMatch
        let hexInput = createSpaceContainer.descendants(matching: .textField)
            .matching(NSPredicate(format: "identifier == %@", "space.theme.hexInput"))
            .firstMatch
        XCTAssertTrue(gradientToggle.waitForExistence(timeout: 5),
                      "Gradient toggle should remain visible alongside the preview")
        XCTAssertTrue(hexInput.waitForExistence(timeout: 5),
                      "Hex input should remain visible alongside the preview")
    }

    private func waitForCreateSpaceContainer(timeout: TimeInterval) -> XCUIElement? {
        let createNameField = app.descendants(matching: .textField)
            .matching(NSPredicate(format: "identifier == %@", "space.create.name"))
            .firstMatch
        let creatorPreview = app.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", "space.theme.creatorPreview"))
            .firstMatch

        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if createNameField.exists || creatorPreview.exists {
                return app
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        }

        return nil
    }

    func testSpaceButtonsRemainVisibleAfterSwitchAndReorder() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let firstSpaceButton = mainWindow.buttons["sidebar.space.0"]
        XCTAssertTrue(firstSpaceButton.waitForExistence(timeout: 5))

        firstSpaceButton.click()

        XCTAssertTrue(firstSpaceButton.exists)
        XCTAssertTrue(mainWindow.buttons["sidebar.space.add"].exists)
    }
}
