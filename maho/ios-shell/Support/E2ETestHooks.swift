import Foundation

/// Test-only boot hook for UITest end-to-end persistence verification.
///
/// This entry point is intentionally driven by launch environment variables
/// so a UITest can seed a conversation before force-quit and verify it on
/// relaunch. All logic is guarded by env presence and is a no-op in normal runs.
enum E2ETestHooks {
    static let uiTestModeEnvKey = "MAHO_UITEST_MODE"
    static let seedEnvKey = "MAHO_E2E_SEED_CONVERSATION"
    static let verifyEnvKey = "MAHO_E2E_VERIFY_CONVERSATION_TITLE"
    static let resultOutputEnvKey = "MAHO_E2E_VERIFY_RESULT"

    static var seededTitle: String?
    static var verifyResult: VerifyResult = .notRun

    enum VerifyResult: String {
        case notRun
        case matched
        case missing
    }

    static func runOnBoot() {
        let env = ProcessInfo.processInfo.environment
        let isUITest = env[uiTestModeEnvKey] == "1"
        let hasSeed = (env[seedEnvKey]?.isEmpty == false)
        let hasVerify = (env[verifyEnvKey]?.isEmpty == false)

        guard isUITest || hasSeed || hasVerify else { return }

        UserDefaults.standard.set(true, forKey: "onboardingCompleted")

        if let title = env[seedEnvKey], !title.isEmpty {
            seedConversation(title: title)
        }

        if let expected = env[verifyEnvKey], !expected.isEmpty {
            verifyResult = verify(expectedTitle: expected)
        }
    }

    private static func seedConversation(title: String) {
        let id = "e2e-\(UUID().uuidString)"
        let created = MahoBridge.shared.createConversation(
            id: id,
            title: title,
            spaceId: nil,
            model: "e2e-test"
        )
        guard created else { return }
        _ = MahoBridge.shared.saveConversationMessage(
            sessionId: id,
            role: "user",
            content: "E2E seed message",
            urlContext: nil
        )
        MahoBridge.shared.saveState()
        seededTitle = title
    }

    private static func verify(expectedTitle: String) -> VerifyResult {
        let conversations = MahoBridge.shared.listConversations(limit: 500)
        let match = conversations.contains { $0.title == expectedTitle }
        return match ? .matched : .missing
    }
}
