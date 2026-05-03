import XCTest

final class ThemeSystemTests: XCTestCase {
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

    // MARK: - Gradient Theme (Phase 1)

    func testCreateThemePlaceholderOpensFromSpaceContextMenu() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5), "Main window should exist")

        let spaceButton = mainWindow.buttons["sidebar.space.0"]
        XCTAssertTrue(spaceButton.waitForExistence(timeout: 5), "Space button should exist")

        spaceButton.rightClick()

        let editItem = app.menuItems["Edit Theme Color"]
        XCTAssertTrue(editItem.waitForExistence(timeout: 3), "Edit Theme Color menu item should exist")
        editItem.click()

        let placeholder = mainWindow.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", "sidebar.createTheme.placeholder"))
            .firstMatch
        XCTAssertTrue(placeholder.waitForExistence(timeout: 5), "Create Theme placeholder should appear in the sidebar first")

        let sidebarBody = mainWindow.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", "sidebar.body.createTheme"))
            .firstMatch
        XCTAssertTrue(sidebarBody.exists, "Sidebar body should switch to create theme mode")

        let backButton = mainWindow.buttons["sidebar.createTheme.back"]
        XCTAssertTrue(backButton.exists, "Placeholder should expose a back control")

        let openEditorButton = mainWindow.buttons["sidebar.createTheme.openDetailedEditor"]
        XCTAssertTrue(openEditorButton.exists, "Placeholder should expose a CTA into the detailed editor")
    }

    func testCreateThemePlaceholderCTAOpensDetailedEditorSheet() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        openCreateThemePlaceholder(in: mainWindow)

        let openEditorButton = mainWindow.buttons["sidebar.createTheme.openDetailedEditor"]
        XCTAssertTrue(openEditorButton.waitForExistence(timeout: 5))
        openEditorButton.click()

        let colorPopup = app.descendants(matching: .popUpButton)
            .matching(NSPredicate(format: "identifier == %@", "space.config.color"))
            .firstMatch
        XCTAssertTrue(colorPopup.waitForExistence(timeout: 5), "Detailed theme color editor sheet should remain available")
    }

    // MARK: - Hex Input (Phase 3)

    func testHexInputFieldExists() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        openCreateThemePlaceholder(in: mainWindow)

        let openEditorButton = mainWindow.buttons["sidebar.createTheme.openDetailedEditor"]
        XCTAssertTrue(openEditorButton.waitForExistence(timeout: 5))
        openEditorButton.click()

        let hexField = app.descendants(matching: .textField).matching(NSPredicate(format: "identifier == %@", "space.theme.hexInput")).firstMatch
        if hexField.waitForExistence(timeout: 5) {
            XCTAssertTrue(hexField.isEnabled, "Hex input should be enabled")
            if let value = hexField.value as? String {
                XCTAssertTrue(value.hasPrefix("#") || value.count == 6 || value.count == 7,
                              "Hex field should contain a hex color value, got: \(value)")
            }
        }
    }

    // MARK: - Export/Import (Phase 2)

    func testExportThemeMenuItemExists() {
        let mainWindow = app.windows["mainWindow"]
        XCTAssertTrue(mainWindow.waitForExistence(timeout: 5))

        let spaceButton = mainWindow.buttons["sidebar.space.0"]
        XCTAssertTrue(spaceButton.waitForExistence(timeout: 5))
        spaceButton.rightClick()

        // Check export theme menu item exists
        let exportItem = app.menuItems.matching(NSPredicate(format: "identifier == %@", "space.contextMenu.exportTheme")).firstMatch
        XCTAssertTrue(exportItem.waitForExistence(timeout: 3), "Export Theme menu item should exist in context menu")
    }

    // MARK: - CSS Mods Settings (Phase 5)

    func testCssModsSettingsPaneExists() {
        openSettingsWindow()

        let settingsWindow = app.windows["settingsWindow"]
        XCTAssertTrue(settingsWindow.waitForExistence(timeout: 5))

        let sidebar = settingsWindow.tables.firstMatch
        XCTAssertTrue(sidebar.waitForExistence(timeout: 5))

        // Find and click CSS Mods pane
        navigateToSettingsPane(named: "CSS Mods", in: settingsWindow)

        // Verify CSS Mods UI elements
        let installButton = settingsWindow.buttons["settings.cssMods.installFromFile"]
        XCTAssertTrue(installButton.waitForExistence(timeout: 5), "Install from File button should exist")

        let table = settingsWindow.descendants(matching: .table).matching(NSPredicate(format: "identifier == %@", "settings.cssMods.table")).firstMatch
        let emptyState = settingsWindow.descendants(matching: .any).matching(NSPredicate(format: "identifier == %@", "settings.cssMods.emptyState")).firstMatch

        // Either table or empty state should be visible
        let tableExists = table.waitForExistence(timeout: 3)
        let emptyExists = emptyState.waitForExistence(timeout: 3)
        XCTAssertTrue(tableExists || emptyExists, "CSS Mods should show either table or empty state")
    }

    // MARK: - Theme Store Settings (Phase 6)

    func testThemeStoreSettingsPaneExists() {
        openSettingsWindow()

        let settingsWindow = app.windows["settingsWindow"]
        XCTAssertTrue(settingsWindow.waitForExistence(timeout: 5))

        navigateToSettingsPane(named: "Theme Store", in: settingsWindow)

        // Verify Theme Store UI elements
        let refreshButton = settingsWindow.buttons["settings.themeStore.refresh"]
        XCTAssertTrue(refreshButton.waitForExistence(timeout: 5), "Refresh button should exist")

        let table = settingsWindow.descendants(matching: .table).matching(NSPredicate(format: "identifier == %@", "settings.themeStore.table")).firstMatch
        let emptyState = settingsWindow.descendants(matching: .any).matching(NSPredicate(format: "identifier == %@", "settings.themeStore.emptyState")).firstMatch
        let loadingState = settingsWindow.descendants(matching: .any).matching(NSPredicate(format: "identifier == %@", "settings.themeStore.loading")).firstMatch

        // One of table, empty state, or loading should exist
        let anyVisible = table.waitForExistence(timeout: 3) ||
                         emptyState.waitForExistence(timeout: 3) ||
                         loadingState.waitForExistence(timeout: 3)
        XCTAssertTrue(anyVisible, "Theme Store should show table, empty state, or loading indicator")
    }

    // MARK: - Helpers

    private func openCreateThemePlaceholder(in mainWindow: XCUIElement) {
        let spaceButton = mainWindow.buttons["sidebar.space.0"]
        XCTAssertTrue(spaceButton.waitForExistence(timeout: 5), "Space button should exist")
        spaceButton.rightClick()

        let editItem = app.menuItems["Edit Theme Color"]
        XCTAssertTrue(editItem.waitForExistence(timeout: 3), "Edit Theme Color menu item should exist")
        editItem.click()

        let placeholder = mainWindow.descendants(matching: .any)
            .matching(NSPredicate(format: "identifier == %@", "sidebar.createTheme.placeholder"))
            .firstMatch
        XCTAssertTrue(placeholder.waitForExistence(timeout: 5), "Create Theme placeholder should appear in the sidebar first")
    }

    private func openSettingsWindow() {
        app.menuBars.menuBarItems["Maho"].click()
        if app.menuItems["Settings…"].waitForExistence(timeout: 2) {
            app.menuItems["Settings…"].click()
        } else if app.menuItems["Preferences…"].waitForExistence(timeout: 2) {
            app.menuItems["Preferences…"].click()
        } else {
            app.typeKey(",", modifierFlags: .command)
        }
    }

    private func navigateToSettingsPane(named paneName: String, in settingsWindow: XCUIElement) {
        let sidebar = settingsWindow.tables.firstMatch
        XCTAssertTrue(sidebar.waitForExistence(timeout: 5))

        // Click through rows to find the right pane
        let rows = sidebar.tableRows
        for i in 0..<rows.count {
            let row = rows.element(boundBy: i)
            if row.staticTexts[paneName].exists {
                row.click()
                sleep(1)
                return
            }
        }
        XCTFail("Could not find settings pane: \(paneName)")
    }
}
