import XCTest
@testable import Maho

final class SyncBackgroundSchedulerTests: XCTestCase {
    func testCompletionGateCallsSystemCompletionOnce() {
        let gate = SyncBackgroundCompletionGate()
        var completions = [Bool]()

        gate.complete(success: true) { completions.append($0) }
        gate.complete(success: false) { completions.append($0) }

        XCTAssertEqual(completions, [true])
    }

    func testBackgroundRefreshIdentifierIsStable() {
        XCTAssertEqual(
            SyncManager.backgroundRefreshIdentifier,
            "dev.maho.browser.sync.refresh"
        )
    }
}
