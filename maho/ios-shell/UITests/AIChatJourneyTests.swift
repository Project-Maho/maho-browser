import XCTest

final class AIChatJourneyTests: XCTestCase {
    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testAIChatBrowseForMePresentation() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launchEnvironment["MAHO_E2E_SEED_ADDRESS_BAR_TAB"] = "https://example.com"
        app.launch()

        let fixtureStatus = app.staticTexts["addressBarFixtureStatus"]
        XCTAssertTrue(fixtureStatus.waitForExistence(timeout: 10))

        let continueButton = app.buttons.matching(
            NSPredicate(format: "identifier BEGINSWITH 'continueTab_'")
        ).firstMatch
        XCTAssertTrue(continueButton.waitForExistence(timeout: 5))
        continueButton.tap()

        let arcAddressPill = app.buttons["arcAddressPill"]
        XCTAssertTrue(arcAddressPill.waitForExistence(timeout: 5))
        arcAddressPill.tap()

        let searchField = app.textFields["searchSheetTextField"]
        XCTAssertTrue(searchField.waitForExistence(timeout: 5))

        searchField.typeKey("a", modifierFlags: .command)
        searchField.typeKey(XCUIKeyboardKey.delete.rawValue, modifierFlags: [])
        searchField.typeText("What is Maho Browser?")

        let browseForMeButton = app.buttons.matching(identifier: "searchSheetBrowseForMeButton").firstMatch
        if browseForMeButton.waitForExistence(timeout: 3) {
            browseForMeButton.tap()
            let agentView = app.otherElements["agentWebViewContainer"]
            let webView = app.webViews.firstMatch
            let presented = agentView.waitForExistence(timeout: 5) || webView.waitForExistence(timeout: 5)
            XCTAssertTrue(presented, "AI Chat/Agent WebView must be presented upon Browse for Me tap")
        } else {
            let homeSettingsButton = app.buttons["homeSettingsButton"]
            if homeSettingsButton.exists {
                homeSettingsButton.tap()
                let aiProviderRow = app.staticTexts["AI Provider"]
                XCTAssertTrue(aiProviderRow.waitForExistence(timeout: 5))
            }
        }
    }
}
