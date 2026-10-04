import Foundation
import XCTest
@testable import Maho

final class MemoryThreadStartupTests: XCTestCase {
    private func storageRoot() throws -> URL {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("MemoryThreadStartup-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        addTeardownBlock { try FileManager.default.removeItem(at: root) }
        return root
    }

    func testReadyPublishesOnlyAfterHydration() throws {
        // Given: a fresh isolated storage owner, with the actual hydration entry observed.
        let root = try storageRoot()
        let bridge = MahoBridge()
        defer { bridge.destroy() }
        let hydration = expectation(description: "native hydration entered")
        bridge.memoryThreadHydrationEntered = { published in
            // Then: no usable core is published before storage has hydrated it.
            XCTAssertFalse(published, "Core is published before native hydration")
            hydration.fulfill()
        }

        // When: the app's existing open -> load sequence executes on real storage.
        XCTAssertTrue(bridge.initialize(storagePath: root.path))
        XCTAssertTrue(bridge.loadState())
        wait(for: [hydration], timeout: 5)
        XCTAssertTrue(bridge.isInitialized)
    }

    @MainActor
    func testLoadingUIAvoidsBridgeLock() throws {
        // Given: native storage-open is entered while the real initialization lock is held.
        let root = try storageRoot()
        let bridge = MahoBridge()
        let entered = expectation(description: "storage-open boundary entered")
        let completed = expectation(description: "initialization worker completed")
        let readCompleted = XCTestExpectation(description: "Main readiness read before storage release")
        bridge.memoryThreadStorageOpenEntered = {
            entered.fulfill()
            // Failure timeout is only a deadlock guard and releases the worker on RED.
            XCTAssertEqual(XCTWaiter.wait(for: [readCompleted], timeout: 5), .completed)
        }
        DispatchQueue.global(qos: .userInitiated).async {
            XCTAssertTrue(bridge.initialize(storagePath: root.path))
            completed.fulfill()
        }
        wait(for: [entered], timeout: 5)

        // When: loading UI reads readiness from Main before native open can finish.
        let ready = bridge.isInitialized
        readCompleted.fulfill()
        wait(for: [completed], timeout: 10)
        defer { bridge.destroy() }

        // Then: the read finishes before release and reports Loading, not Ready.
        XCTAssertFalse(ready)
    }

    func testFailureAndCloseDiscardLateCore() throws {
        // Given: a real impossible storage path, not a mocked readiness object.
        let root = try storageRoot()
        let file = root.appendingPathComponent("not-a-directory")
        try Data("fixture".utf8).write(to: file)
        let bridge = MahoBridge()
        defer { bridge.destroy() }
        XCTAssertFalse(bridge.initialize(storagePath: file.appendingPathComponent("profile").path))
        let controller = WebViewBridgeController(bridge: bridge)

        // When: a dependent RPC arrives after failed native initialization.
        let response = controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":"mt-failed","method":"getSpaceAIConfig","params":["mt-space"]}"#
        )
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(response.utf8)) as? [String: Any])

        // Then: failure is explicit, never a valid null/empty profile result.
        XCTAssertNil(object["result"])
        let error = try XCTUnwrap(object["error"] as? [String: String])
        XCTAssertEqual(error["reason"], "core_initialization_failed")
        XCTAssertFalse(bridge.isInitialized)
    }

    @MainActor
    func testCloseDuringStorageOpenReturnsBeforeNativeCompletion() throws {
        // Given: native open is held at its actual boundary before it returns a core.
        let root = try storageRoot()
        let bridge = MahoBridge()
        let entered = expectation(description: "native open entered")
        let completed = expectation(description: "native open settled")
        let closed = XCTestExpectation(description: "Main close returned before native open release")
        bridge.memoryThreadStorageOpenEntered = {
            entered.fulfill()
            XCTAssertEqual(XCTWaiter.wait(for: [closed], timeout: 5), .completed)
        }
        DispatchQueue.global(qos: .userInitiated).async {
            _ = bridge.initialize(storagePath: root.path)
            completed.fulfill()
        }
        wait(for: [entered], timeout: 5)

        // When: the owner closes while startup is loading.
        bridge.destroy()
        closed.fulfill()
        wait(for: [completed], timeout: 10)

        // Then: close preceded native completion, and no late core becomes usable.
        XCTAssertFalse(bridge.isInitialized)
        bridge.destroy() // Cleanup is idempotent even if the late-publication mutation wins.
    }
}
