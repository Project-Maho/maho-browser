import XCTest

final class AddressOmniboxJourneyTests: XCTestCase {
    private let fixtureURL = "https://example.com"
    private let replacementURL = "https://example.org"

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testAddressOmniboxPrefillsAndReusesActiveTab() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launchEnvironment["MAHO_E2E_SEED_ADDRESS_BAR_TAB"] = fixtureURL
        app.launch()

        let fixtureStatus = app.staticTexts["addressBarFixtureStatus"]
        XCTAssertTrue(fixtureStatus.waitForExistence(timeout: 10))
        XCTAssertEqual(fixtureStatus.label, "ready")

        let continueButton = app.buttons.matching(
            NSPredicate(format: "identifier BEGINSWITH 'continueTab_'")
        ).firstMatch
        XCTAssertTrue(continueButton.waitForExistence(timeout: 5))
        continueButton.tap()

        let tabsButton = app.buttons["tabsButton"]
        XCTAssertTrue(tabsButton.waitForExistence(timeout: 5))
        XCTAssertEqual(tabsButton.value as? String, "1 open")

        let addressPill = app.buttons["arcAddressPill"]
        XCTAssertTrue(addressPill.waitForExistence(timeout: 5))
        addressPill.tap()

        let addressField = app.textFields["searchSheetTextField"]
        XCTAssertTrue(addressField.waitForExistence(timeout: 5))
        XCTAssertTrue(
            (addressField.value as? String)?.hasPrefix(fixtureURL) == true,
            "Active tab must prefill its full URL"
        )

        addressField.typeKey("a", modifierFlags: .command)
        addressField.typeKey(XCUIKeyboardKey.delete.rawValue, modifierFlags: [])
        addressField.typeText(replacementURL + "\n")

        let searchSheet = app.otherElements["searchSheet"]
        let searchSheetDismissed = expectation(
            for: NSPredicate(format: "exists == false"),
            evaluatedWith: searchSheet
        )
        wait(for: [searchSheetDismissed], timeout: 5)
        XCTAssertEqual(tabsButton.value as? String, "1 open", "Active tab must be reused without creating duplicates")

        addressPill.tap()
        XCTAssertTrue(addressField.waitForExistence(timeout: 5))
        XCTAssertTrue(
            (addressField.value as? String)?.hasPrefix(replacementURL) == true,
            "Submitted URL must be reflected on subsequent address pill taps"
        )
    }
}
