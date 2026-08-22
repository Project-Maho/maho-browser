import XCTest

final class ArchiveHistoryJourneyTests: XCTestCase {
    private let fixtureTitle = "Todo-6-Archive-Fixture"

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testArchiveAndHistoryLifecycle() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launchEnvironment["MAHO_E2E_SEED_ARCHIVED_TAB"] = fixtureTitle
        app.launch()

        let fixtureStatus = app.staticTexts["archiveFixtureStatus"]
        XCTAssertTrue(fixtureStatus.waitForExistence(timeout: 10))

        let continueButton = app.buttons.matching(
            NSPredicate(format: "identifier BEGINSWITH 'continueTab_'")
        ).firstMatch
        XCTAssertTrue(continueButton.waitForExistence(timeout: 5))
        continueButton.tap()

        let menuButton = app.buttons["menuButton"]
        XCTAssertTrue(menuButton.waitForExistence(timeout: 5))
        menuButton.tap()

        let openArchiveButton = app.buttons["openArchiveButton"]
        XCTAssertTrue(openArchiveButton.waitForExistence(timeout: 5))
        openArchiveButton.tap()

        XCTAssertTrue(app.otherElements["archiveView"].waitForExistence(timeout: 5))
    }
}
