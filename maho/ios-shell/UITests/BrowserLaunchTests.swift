import XCTest

final class BrowserLaunchTests: XCTestCase {

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testAppLaunch() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launch()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 5))
    }

    func testAppActivation() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launch()
        XCTAssertEqual(app.state, .runningForeground)
    }
}
