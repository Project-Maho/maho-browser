import XCTest

final class VisualQAScreenshots: XCTestCase {

    private let app = XCUIApplication()

    override func setUpWithError() throws {
        continueAfterFailure = true
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launch()
        _ = app.wait(for: .runningForeground, timeout: 10)
        sleep(1)
    }

    private func snap(_ name: String) {
        let att = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        att.name = name
        att.lifetime = .keepAlways
        add(att)
    }

    func testCaptureAllScreens() throws {
        snap("02-home-empty")

        let homeSearch = app.textFields["homeSearchField"]
        if homeSearch.waitForExistence(timeout: 3) {
            homeSearch.tap()
            sleep(2)
            snap("05-url-bar-focused")
            homeSearch.typeText("apple")
            sleep(1)
            snap("06-url-bar-typed")
            if app.buttons["Cancel"].firstMatch.exists { app.buttons["Cancel"].firstMatch.tap() }
            sleep(1)
        }

        let menuButton = app.buttons["menuButton"]
        if menuButton.waitForExistence(timeout: 3) {
            menuButton.tap()
            sleep(1)
            snap("03-menu-open")
            let settingsBtn = app.buttons["Settings"].firstMatch
            if settingsBtn.waitForExistence(timeout: 2) {
                settingsBtn.tap()
                sleep(2)
                snap("07-settings-root")
                app.swipeUp()
                sleep(1)
                snap("08-settings-scrolled")
                let back = app.buttons["Done"].firstMatch
                if back.exists { back.tap() } else { app.swipeDown() }
                sleep(1)
            } else {
                app.swipeDown()
                sleep(1)
            }
        }

        if menuButton.waitForExistence(timeout: 3) {
            menuButton.tap()
            sleep(1)
            let privateBtn = app.buttons["Private"].firstMatch
            if privateBtn.waitForExistence(timeout: 2) {
                privateBtn.tap()
                sleep(2)
                snap("09-private-mode")
            }
        }

        let tabsButton = app.buttons["tabsButton"]
        if tabsButton.waitForExistence(timeout: 3) {
            tabsButton.tap()
            sleep(2)
            snap("04-tab-grid")
            let plusBtn = app.buttons["+"].firstMatch
            if plusBtn.waitForExistence(timeout: 2) {
                plusBtn.tap()
                sleep(1)
                snap("10-tab-grid-after-new")
            }
        }
    }
}
