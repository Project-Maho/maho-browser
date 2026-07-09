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
