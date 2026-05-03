import XCTest

final class SettingsTests: XCTestCase {

    let app = XCUIApplication()

    override func setUpWithError() throws {
        continueAfterFailure = false
        app.launch()
    }

    func testSettingsButtonExists() throws {
        let menuButton = app.buttons["menuButton"]
        XCTAssertTrue(menuButton.waitForExistence(timeout: 5))
    }

    func testSettingsFlowCanBePresented() throws {
        let menuButton = app.buttons["menuButton"]
        XCTAssertTrue(menuButton.waitForExistence(timeout: 5))
        XCTAssertTrue(menuButton.isHittable)
    }
}
