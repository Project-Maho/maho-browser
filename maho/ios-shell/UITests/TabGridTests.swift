import XCTest

final class TabGridTests: XCTestCase {

    let app = XCUIApplication()

    override func setUpWithError() throws {
        continueAfterFailure = false
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launch()
    }

    func testTabGridButtonExists() throws {
        let tabsButton = app.buttons["tabsButton"]
        XCTAssertTrue(tabsButton.waitForExistence(timeout: 5))
    }

    func testTabGridButtonIsHittable() throws {
        let tabsButton = app.buttons["tabsButton"]
        XCTAssertTrue(tabsButton.waitForExistence(timeout: 5))
        XCTAssertTrue(tabsButton.isHittable)
    }
}
