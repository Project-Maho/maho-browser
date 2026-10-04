import XCTest

final class WebNavigationJourneyTests: XCTestCase {
    private let fixtureURL = "https://example.com"

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testWebNavigationWorkflow() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launchEnvironment["MAHO_E2E_SEED_ADDRESS_BAR_TAB"] = fixtureURL
        app.launch()

        let fixtureStatus = app.staticTexts["addressBarFixtureStatus"]
        XCTAssertTrue(fixtureStatus.waitForExistence(timeout: 10))

        let continueButton = app.buttons.matching(
            NSPredicate(format: "identifier BEGINSWITH 'continueTab_'")
        ).firstMatch
        XCTAssertTrue(continueButton.waitForExistence(timeout: 5))
        continueButton.tap()

        let tabsButton = app.buttons["tabsButton"]
        XCTAssertTrue(tabsButton.waitForExistence(timeout: 5))

        let addressPill = app.buttons["arcAddressPill"]
        XCTAssertTrue(addressPill.waitForExistence(timeout: 5))
    }
}
