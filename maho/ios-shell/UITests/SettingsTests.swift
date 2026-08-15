import XCTest

final class SettingsTests: XCTestCase {

    let app = XCUIApplication()

    override func setUpWithError() throws {
        continueAfterFailure = false
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
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

    func testOpenAICompatibleBaseURLIsAnEditableNativeField() throws {
        app.launchArguments += ["-maho_ai_provider", "openai-compatible"]
        app.terminate()
        app.launch()

        let settingsButton = app.buttons["homeSettingsButton"]
        XCTAssertTrue(settingsButton.waitForExistence(timeout: 5))
        settingsButton.tap()

        let providerRow = app.staticTexts["AI Provider"]
        XCTAssertTrue(providerRow.waitForExistence(timeout: 5))
        providerRow.tap()

        let baseURLField = app.textFields["aiProviderBaseURLField"]
        XCTAssertTrue(baseURLField.waitForExistence(timeout: 5))
        XCTAssertTrue(baseURLField.isHittable)
        XCTAssertTrue(app.secureTextFields["aiProviderAPIKeyField"].exists)
        XCTAssertTrue(app.textFields["aiProviderModelField"].exists)
        XCTAssertEqual(app.links.count, 0, "Provider settings must not expose an external URL link")

        let expectedURL = "https://api.together.xyz/v1"
        baseURLField.tap()
        baseURLField.typeKey("a", modifierFlags: .command)
        baseURLField.typeKey(XCUIKeyboardKey.delete.rawValue, modifierFlags: [])
        baseURLField.typeText(expectedURL)
        app.navigationBars["AI Provider"].buttons["Settings"].tap()
        app.staticTexts["AI Provider"].tap()
        XCTAssertEqual(app.textFields["aiProviderBaseURLField"].value as? String, expectedURL)
    }
}
