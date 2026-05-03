import XCTest

final class SettingsScreenshotTests: XCTestCase {
    var app: XCUIApplication!

    override func setUp() {
        super.setUp()
        continueAfterFailure = true
        app = XCUIApplication()
        app.launchArguments = ["--ui-testing"]
        app.launch()
    }

    override func tearDown() {
        app.terminate()
        super.tearDown()
    }

    func testCaptureCreateSpaceFlow() {
        let outputDirectory = URL(fileURLWithPath: "/tmp/maho-create-space-screenshots/")
        try? FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true)

        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let addButton = mainWindow.buttons["sidebar.space.add"]
        XCTAssertTrue(addButton.waitForExistence(timeout: 5), "Add space button should exist")
        addButton.click()

        let newSpaceAction = app.descendants(matching: .any)
            .matching(NSPredicate(format: "label CONTAINS[c] %@", "New Space"))
            .firstMatch
        XCTAssertTrue(newSpaceAction.waitForExistence(timeout: 5), "New Space action should appear after clicking add")
        newSpaceAction.click()

        let createSpaceContainer = waitForCreateSpaceContainer(timeout: 15)
        XCTAssertNotNil(createSpaceContainer, "Create Space UI should appear after clicking add")
        guard let createSpaceContainer else { return }
        XCTAssertTrue(createSpaceContainer.textFields["space.create.name"].waitForExistence(timeout: 5))
        XCTAssertTrue(createSpaceContainer.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", "space.theme.creatorPreview"))
            .firstMatch
            .waitForExistence(timeout: 5))
        XCTAssertTrue(createSpaceContainer.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", "space.theme.gradientToggle"))
            .firstMatch
            .waitForExistence(timeout: 5))
        XCTAssertTrue(createSpaceContainer.descendants(matching: .textField)
            .matching(NSPredicate(format: "identifier == %@", "space.theme.hexInput"))
            .firstMatch
            .waitForExistence(timeout: 5))

        let screenshot = createSpaceContainer.screenshot()
        let attachment = XCTAttachment(screenshot: screenshot)
        attachment.name = "Create Space"
        attachment.lifetime = .keepAlways
        add(attachment)

        let fileURL = outputDirectory.appendingPathComponent("create-space.png")
        try? screenshot.pngRepresentation.write(to: fileURL)
    }

    func testCaptureAllSettingsPanes() {
        let outputDirectory = URL(fileURLWithPath: "/tmp/maho-settings-screenshots/")
        try? FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true)

        app.menuBars.menuBarItems["Maho"].click()
        if app.menuItems["Settings…"].waitForExistence(timeout: 2) {
            app.menuItems["Settings…"].click()
        } else if app.menuItems["Preferences…"].waitForExistence(timeout: 2) {
            app.menuItems["Preferences…"].click()
        } else {
            app.typeKey(",", modifierFlags: .command)
        }

        let settingsWindow = app.windows["Settings"]
        XCTAssertTrue(settingsWindow.waitForExistence(timeout: 5))

        let paneNames = [
            "General",
            "Appearance",
            "Privacy",
            "Shortcuts",
            "Profiles",
            "Advanced",
            "Notifications",
            "Autofill",
            "Content Blocker",
            "Air Traffic Control",
            "Passwords",
            "Account & Sync",
            "CSS Mods",
            "Extensions",
            "Theme Store",
            "Maho AI"
        ]

        let sidebar = settingsWindow.tables.firstMatch
        XCTAssertTrue(sidebar.waitForExistence(timeout: 5))

        for (index, paneName) in paneNames.enumerated() {
            let row = sidebar.tableRows.element(boundBy: index)
            XCTAssertTrue(row.waitForExistence(timeout: 5), "Missing settings row at index \(index) for \(paneName)")
            row.click()
            sleep(1)

            let screenshot = settingsWindow.screenshot()
            let attachment = XCTAttachment(screenshot: screenshot)
            attachment.name = paneName
            attachment.lifetime = .keepAlways
            add(attachment)

            let fileURL = outputDirectory.appendingPathComponent("\(index)_\(paneName).png")
            try? screenshot.pngRepresentation.write(to: fileURL)
        }
    }

    private func waitForCreateSpaceContainer(timeout: TimeInterval) -> XCUIElement? {
        let createSpacePredicate = NSPredicate(format: "identifier == %@ OR identifier == %@", "space.create.name", "space.theme.creatorPreview")
        let deadline = Date().addingTimeInterval(timeout)

        while Date() < deadline {
            for window in app.windows.allElementsBoundByIndex {
                if window.descendants(matching: .any).matching(createSpacePredicate).firstMatch.exists {
                    return window
                }
            }

            for sheet in app.sheets.allElementsBoundByIndex {
                if sheet.descendants(matching: .any).matching(createSpacePredicate).firstMatch.exists {
                    return sheet
                }
            }

            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        }

        return nil
    }
}
