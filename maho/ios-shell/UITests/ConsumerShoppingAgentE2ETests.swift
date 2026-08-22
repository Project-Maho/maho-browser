import XCTest

/// Live-site consumer checkout journey: a real e-commerce demo
/// (https://www.saucedemo.com) is loaded in the browsing WKWebView, the
/// user asks the agent to complete the purchase through the real omnibox /
/// Browse-for-Me surface, and the production agentic browsing tool pipeline
/// (get_page_snapshot / fill_input / click_element / scroll_page via
/// AgenticBrowsingDOM) logs in, adds a product, fills the checkout form,
/// and places a real order on the live site. Completion is verified on the
/// real order-confirmation page. The LLM planning layer is not exercised
/// (no BYOK key in the test environment); the plan is scripted while every
/// tool action runs against the real page.
final class ConsumerShoppingAgentE2ETests: XCTestCase {
    private let liveStoreURL = "https://www.saucedemo.com/"

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testLiveSiteCheckoutJourneyViaAgentTools() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launchEnvironment["MAHO_E2E_SEED_ADDRESS_BAR_TAB"] = liveStoreURL
        app.launchEnvironment["MAHO_E2E_AGENTIC_JOURNEY"] = "1"
        app.launch()

        let journeyStatus = app.staticTexts["agenticJourneyStatus"]
        XCTAssertTrue(journeyStatus.waitForExistence(timeout: 15), "agentic journey fixture must be armed")

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

        let livePageShown = expectation(
            for: NSPredicate(format: "NOT label BEGINSWITH 'running:waiting_page' AND NOT label == 'idle'"),
            evaluatedWith: journeyStatus
        )
        wait(for: [livePageShown], timeout: 120)

        let addressPill = app.buttons["arcAddressPill"]
        XCTAssertTrue(addressPill.waitForExistence(timeout: 5))
        addressPill.tap()

        let addressField = app.textFields["searchSheetTextField"]
        XCTAssertTrue(addressField.waitForExistence(timeout: 5))
        addressField.typeKey("a", modifierFlags: .command)
        addressField.typeKey(XCUIKeyboardKey.delete.rawValue, modifierFlags: [])
        addressField.typeText("Log in, add an item to the cart, and complete the checkout for me")

        let browseForMeButton = app.buttons.matching(identifier: "searchSheetBrowseForMeButton").firstMatch
        XCTAssertTrue(browseForMeButton.waitForExistence(timeout: 5))
        browseForMeButton.tap()

        let agentSurface = app.otherElements["agentWebViewContainer"]
        let agentWebView = app.webViews.firstMatch
        let agentShown = agentSurface.waitForExistence(timeout: 10) || agentWebView.waitForExistence(timeout: 10)
        XCTAssertTrue(agentShown, "agent surface must be presented over the live store page")

        let completed = expectation(
            for: NSPredicate(format: "label == 'completed' OR label BEGINSWITH 'failed:'"),
            evaluatedWith: journeyStatus
        )
        wait(for: [completed], timeout: 300)
        XCTAssertEqual(
            journeyStatus.label, "completed",
            "live-site journey must finish the real order; steps: \(app.staticTexts["agenticJourneySteps"].label)"
        )

        let steps = app.staticTexts["agenticJourneySteps"].label
        XCTAssertTrue(steps.contains("page_loaded:https://www.saucedemo.com"), steps)
        XCTAssertTrue(steps.contains("filled:Username:ok"), steps)
        XCTAssertTrue(steps.contains("filled:Password:ok"), steps)
        XCTAssertTrue(steps.contains("nav:inventory"), steps)
        XCTAssertTrue(steps.contains("clicked:add_to_cart:ok"), steps)
        XCTAssertTrue(steps.contains("nav:cart.html"), steps)
        XCTAssertTrue(steps.contains("nav:checkout-step-one"), steps)
        XCTAssertTrue(steps.contains("filled:First Name:ok"), steps)
        XCTAssertTrue(steps.contains("filled:Last Name:ok"), steps)
        XCTAssertTrue(steps.contains("filled:Zip:ok"), steps)
        XCTAssertTrue(steps.contains("nav:checkout-complete"), steps)
        XCTAssertTrue(steps.contains("verified:order_complete:yes"), steps)
    }
}
