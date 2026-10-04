import XCTest

final class SyncDeviceRelayJourneyTests: XCTestCase {
    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testSyncAndAiSettingsFlow() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launch()

        let homeSettingsButton = app.buttons["homeSettingsButton"]
        XCTAssertTrue(homeSettingsButton.waitForExistence(timeout: 5))
        homeSettingsButton.tap()

        let aiProviderRow = app.staticTexts["AI Provider"]
        XCTAssertTrue(aiProviderRow.waitForExistence(timeout: 5))
        aiProviderRow.tap()

        let baseURLField = app.textFields["aiProviderBaseURLField"]
        XCTAssertTrue(baseURLField.waitForExistence(timeout: 5))
        let apiKeyField = app.secureTextFields["aiProviderAPIKeyField"]
        XCTAssertTrue(apiKeyField.exists)
        let modelField = app.textFields["aiProviderModelField"]
        XCTAssertTrue(modelField.exists)

        let settingsBackButton = app.navigationBars.buttons.firstMatch
        XCTAssertTrue(settingsBackButton.waitForExistence(timeout: 5))
        settingsBackButton.tap()

        let syncStatus = app.staticTexts.matching(
            NSPredicate(format: "label CONTAINS 'Sync' OR label CONTAINS 'Settings'")
        ).firstMatch
        XCTAssertTrue(syncStatus.waitForExistence(timeout: 5))
    }
}
