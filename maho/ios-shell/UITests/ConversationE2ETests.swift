import XCTest

/// End-to-end test verifying that a persisted conversation survives a real
/// force-quit and relaunch cycle. Exercises the maho-core → FFI → Swift
/// persistence path through `MahoBridge.createConversation` +
/// `saveConversationMessage` on seed, `listConversations` on verify.
///
/// Boot-time hooks in `E2ETestHooks` interpret the launch environment:
///   MAHO_E2E_SEED_CONVERSATION           → seed a conversation with this title
///   MAHO_E2E_VERIFY_CONVERSATION_TITLE   → look for a conversation with this title
///
/// The verify boot writes a hidden accessibility element with identifier
/// "e2eVerifyResult" whose label is either "matched" or "missing".
final class ArchiveE2ETests: XCTestCase {
    private let fixtureTitle = "Todo-6-Archive-Fixture"

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testArchiveMenuRestoresBridgeBackedFixture() throws {
        let app = XCUIApplication()
        app.launchEnvironment["MAHO_UITEST_MODE"] = "1"
        app.launchEnvironment["MAHO_E2E_SEED_ARCHIVED_TAB"] = fixtureTitle
        app.launch()

        let fixtureStatus = app.staticTexts["archiveFixtureStatus"]
        XCTAssertTrue(
            fixtureStatus.waitForExistence(timeout: 10),
            "Archive fixture status element never appeared"
        )
        guard fixtureStatus.label == "ready" else {
            XCTFail("Archive fixture status: \(fixtureStatus.label)")
            return
        }
        XCTAssertTrue(app.staticTexts["archiveFixtureReady"].exists)

        let continueButton = app.buttons.matching(
            NSPredicate(format: "identifier BEGINSWITH 'continueTab_'")
        ).firstMatch
        XCTAssertTrue(continueButton.waitForExistence(timeout: 5))
        continueButton.tap()
        XCTAssertTrue(app.otherElements["mainBrowserBottomBar"].waitForExistence(timeout: 5))

        let menuButton = app.buttons["menuButton"]
        XCTAssertTrue(menuButton.waitForExistence(timeout: 5))
        menuButton.tap()

        let openArchiveButton = app.buttons["openArchiveButton"]
        XCTAssertTrue(openArchiveButton.waitForExistence(timeout: 5))
        try capture(app, name: "task-6-archive-menu-verified")
        openArchiveButton.tap()

        XCTAssertTrue(app.otherElements["archiveView"].waitForExistence(timeout: 5))
        let archiveRow = app.buttons.matching(
            NSPredicate(format: "identifier BEGINSWITH 'archiveRow_'")
        ).firstMatch
        XCTAssertTrue(archiveRow.waitForExistence(timeout: 5), "Archive has no populated row")
        XCTAssertTrue(archiveRow.label.contains(fixtureTitle))
        try capture(app, name: "task-6-archive-populated-verified")
        archiveRow.tap()

        let tabsButton = app.buttons["tabsButton"]
        XCTAssertTrue(tabsButton.waitForExistence(timeout: 5))
        tabsButton.tap()
        XCTAssertTrue(app.scrollViews["tabGridView"].waitForExistence(timeout: 5))

        let restoredTab = app.buttons.matching(
            NSPredicate(
                format: "identifier BEGINSWITH 'tabCard_' AND label == %@ AND value == 'active'",
                fixtureTitle
            )
        ).firstMatch
        XCTAssertTrue(
            restoredTab.waitForExistence(timeout: 5),
            "Restored fixture was not the active tab in the tab grid"
        )
        try capture(app, name: "task-6-archive-restored-verified")
    }

    private func capture(_ app: XCUIApplication, name: String) throws {
        let repositoryRoot = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
        let evidenceDirectory = repositoryRoot.appendingPathComponent(".omo/evidence", isDirectory: true)
        try FileManager.default.createDirectory(
            at: evidenceDirectory,
            withIntermediateDirectories: true
        )
        try app.screenshot().pngRepresentation.write(
            to: evidenceDirectory.appendingPathComponent("\(name).png"),
            options: .atomic
        )
    }
}

final class ConversationE2ETests: XCTestCase {

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testConversationPersistenceAcrossForceQuit() throws {
        let title = "E2E-Conv-\(UUID().uuidString.prefix(8))"

        // Phase 1: launch with seed env → persist a conversation.
        let seedApp = XCUIApplication()
        seedApp.launchEnvironment["MAHO_E2E_SEED_CONVERSATION"] = String(title)
        seedApp.launch()
        XCTAssertTrue(
            seedApp.wait(for: .runningForeground, timeout: 10),
            "Seed launch never reached foreground"
        )

        // Force-quit — this is the whole point of the test. In-memory-only
        // implementations survive a normal exit but die here.
        seedApp.terminate()
        XCTAssertEqual(seedApp.state, .notRunning)

        // Phase 2: relaunch WITHOUT seed, WITH verify env. The boot hook will
        // read persisted state via listConversations() and write the result to
        // an accessibility element the test can assert on.
        let verifyApp = XCUIApplication()
        verifyApp.launchEnvironment["MAHO_E2E_VERIFY_CONVERSATION_TITLE"] = String(title)
        verifyApp.launch()
        XCTAssertTrue(
            verifyApp.wait(for: .runningForeground, timeout: 10),
            "Verify launch never reached foreground"
        )

        let resultElement = verifyApp.staticTexts["e2eVerifyResult"]
        XCTAssertTrue(
            resultElement.waitForExistence(timeout: 5),
            "e2eVerifyResult element never appeared — boot hook did not run"
        )

        let label = resultElement.label
        XCTAssertEqual(
            label,
            "matched",
            "Persistence failed: expected 'matched', got '\(label)'. Conversation with title \(title) was seeded before terminate() but was NOT found in listConversations() after relaunch."
        )
    }
}
