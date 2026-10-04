import SwiftUI
import WebKit
import XCTest
@testable import Maho

final class MemoryThreadOwnerTests: XCTestCase {
    private struct Agent {
        let handle: String
        let pointer: OpaquePointer
        let released: XCTestExpectation
        let cleanupReleased: XCTestExpectation
    }

    // The queued RPC result is shared only through this lock-protected box.
    private final class Response: @unchecked Sendable {
        private let lock = NSLock()
        private var value: String?
        func store(_ value: String) { lock.lock(); self.value = value; lock.unlock() }
        func load() -> String? { lock.lock(); defer { lock.unlock() }; return value }
    }

    private func createRequest() -> String {
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"agentCreateSession\",\"params\":[\"memory-thread-\(UUID().uuidString)\"]}"
    }

    private func resultHandle(_ response: String) throws -> String {
        let value = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(response.utf8)) as? [String: Any])
        return try XCTUnwrap(value["result"] as? String)
    }

    private func observe(_ handle: String) throws -> Agent {
        let ptr = try XCTUnwrap(AgentHandleRegistry.shared.get(id: handle))
        let context = try XCTUnwrap(AgentSessionRegistry.shared.context(for: ptr))
        let released = XCTestExpectation(description: "native context release")
        let cleanupReleased = XCTestExpectation(description: "cleanup native context release")
        context.memoryThreadDidRelease = { released.fulfill(); cleanupReleased.fulfill() }
        return Agent(handle: handle, pointer: ptr, released: released, cleanupReleased: cleanupReleased)
    }

    private func cleanup(_ agent: Agent, bridge: MahoBridge) {
        bridge.agentFreeSession(agent.pointer)
        AgentHandleRegistry.shared.unregister(id: agent.handle)
        XCTAssertEqual(XCTWaiter.wait(for: [agent.cleanupReleased], timeout: 5), .completed)
    }

    private func fence(_ controller: WebViewBridgeController) {
        let drained = expectation(description: "owner dispatch queue drained")
        controller.dispatchQueue.async { drained.fulfill() }
        wait(for: [drained], timeout: 5)
    }

    @MainActor
    private func dismantleAgent(_ host: (webView: WKWebView, controller: WebViewBridgeController)) {
        let coordinator = AgentWebView.Coordinator()
        coordinator.controller = host.controller
        AgentWebView.dismantleUIView(host.webView, coordinator: coordinator)
    }

    @MainActor
    func testDismantleFreesOnlyOwner() throws {
        // Given: two real factory hosts and native sessions under an isolated core.
        let bridge = try makeStorageBackedBridgeForTests()
        defer { bridge.destroy() }
        let a = WebViewBridgeController.makeAgenticWebView(bridge: bridge)
        let b = WebViewBridgeController.makeAgenticWebView(bridge: bridge)
        let agentA = try observe(resultHandle(a.controller.dispatchRPCForTesting(createRequest())))
        defer { cleanup(agentA, bridge: bridge) }
        let agentB = try observe(resultHandle(b.controller.dispatchRPCForTesting(createRequest())))
        defer { cleanup(agentB, bridge: bridge) }

        // When: only A is dismissed through the native representable lifecycle.
        dismantleAgent(a)
        fence(a.controller)

        // Then: A is truly freed; B still accepts a native operation.
        XCTAssertNil(AgentHandleRegistry.shared.get(id: agentA.handle))
        XCTAssertEqual(XCTWaiter.wait(for: [agentA.released], timeout: 5), .completed)
        XCTAssertEqual(AgentHandleRegistry.shared.get(id: agentB.handle), agentB.pointer)
        XCTAssertNotNil(bridge.agentListTools(agentB.pointer))
    }

    @MainActor
    func testLateCreateAfterCloseIsFreed() throws {
        // Given: native creation completed, but its production ownership handoff is parked.
        let bridge = try makeStorageBackedBridgeForTests()
        defer { bridge.destroy() }
        let host = WebViewBridgeController.makeAgenticWebView(bridge: bridge)
        let entered = expectation(description: "native create returned before ownership handoff")
        let completed = expectation(description: "queued create settled")
        let released = XCTestExpectation(description: "late native context release")
        let cleanupReleased = XCTestExpectation(description: "late cleanup release")
        let gate = DispatchSemaphore(value: 0)
        defer { gate.signal() }
        bridge.memoryThreadAgentCreated = { ptr in
            AgentSessionRegistry.shared.context(for: ptr)?.memoryThreadDidRelease = {
                released.fulfill(); cleanupReleased.fulfill()
            }
            entered.fulfill()
            XCTAssertEqual(gate.wait(timeout: .now() + 5), .success)
        }
        let response = Response()
        let request = createRequest()
        host.controller.dispatchQueue.async {
            response.store(host.controller.dispatchRPCForTesting(request))
            completed.fulfill()
        }
        wait(for: [entered], timeout: 5)

        // When: the native host disappears before the queued create can register.
        dismantleAgent(host)
        gate.signal()
        wait(for: [completed], timeout: 5)
        fence(host.controller)

        // Then: no globally usable orphan survives; its native context is released.
        // A closed request may return an error, so the handle is optional here.
        let json = try XCTUnwrap(response.load())
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(json.utf8)) as? [String: Any])
        let lateHandle = object["result"] as? String
        let latePointer = lateHandle.flatMap { AgentHandleRegistry.shared.get(id: $0) }
        defer {
            if let handle = lateHandle, let ptr = latePointer {
                bridge.agentFreeSession(ptr)
                AgentHandleRegistry.shared.unregister(id: handle)
            }
            XCTAssertEqual(XCTWaiter.wait(for: [cleanupReleased], timeout: 5), .completed)
        }
        XCTAssertNil(latePointer, "Late create registered a usable orphan")
        XCTAssertEqual(XCTWaiter.wait(for: [released], timeout: 5), .completed)
    }

    @MainActor
    func testBorrowDefersFreeWithoutBlockingMain() throws {
        // Given: an actual registry operation lease remains alive across dismissal.
        let bridge = try makeStorageBackedBridgeForTests()
        defer { bridge.destroy() }
        let host = WebViewBridgeController.makeAgenticWebView(bridge: bridge)
        let agent = try observe(resultHandle(host.controller.dispatchRPCForTesting(createRequest())))
        var lease: AgentSessionRegistry.OperationLease? = try XCTUnwrap(
            AgentSessionRegistry.shared.acquireOperation(for: agent.pointer)
        )
        defer { lease?.release(); lease = nil; cleanup(agent, bridge: bridge) }

        // When: Main dismantles the host while that native lease is held.
        // The watchdog owns the release even if a broken dismantle blocks Main.
        let returned = XCTestExpectation(description: "Main dismantle returned before lease release")
        let watchdogDone = expectation(description: "lease watchdog finished")
        DispatchQueue.global().async { [held = lease] in
            let result = XCTWaiter.wait(for: [returned], timeout: 5)
            XCTAssertEqual(result, .completed)
            if result != .completed { held?.release() }
            watchdogDone.fulfill()
        }
        dismantleAgent(host)
        returned.fulfill()
        wait(for: [watchdogDone], timeout: 10)
        fence(host.controller)

        // Then: admission closes but the existing leased context stays valid.
        XCTAssertNil(AgentSessionRegistry.shared.acquireOperation(for: agent.pointer))
        XCTAssertEqual(lease?.ptr, agent.pointer)
        lease?.release()
        lease = nil
        XCTAssertEqual(XCTWaiter.wait(for: [agent.released], timeout: 5), .completed)
    }

    @MainActor
    func testAllFactoryHostsDismantle() throws {
        // Given: each actual shared-factory caller owns a real native agent.
        let bridge = try makeStorageBackedBridgeForTests()
        defer { bridge.destroy() }
        let actions: [(WKWebView, WebViewBridgeController) -> Void] = [
            { view, controller in
                let coordinator = AgentWebView.Coordinator(); coordinator.controller = controller
                AgentWebView.dismantleUIView(view, coordinator: coordinator)
            },
            { view, controller in
                let coordinator = ConversationsWebView.Coordinator(); coordinator.controller = controller
                ConversationsWebView.dismantleUIView(view, coordinator: coordinator)
            },
            { view, controller in
                let coordinator = BYOKWebView.Coordinator(); coordinator.controller = controller
                BYOKWebView.dismantleUIView(view, coordinator: coordinator)
            },
            { view, controller in
                let coordinator = OnboardingWebView.Coordinator(); coordinator.controller = controller
                OnboardingWebView.dismantleUIView(view, coordinator: coordinator)
            }
        ]
        for action in actions {
            let host = WebViewBridgeController.makeAgenticWebView(bridge: bridge)
            let agent = try observe(resultHandle(host.controller.dispatchRPCForTesting(createRequest())))
            defer { cleanup(agent, bridge: bridge) }

            // When: SwiftUI's concrete host dismantling witness is invoked.
            action(host.webView, host.controller)
            fence(host.controller)

            // Then: every host releases ownership, independent of deinit timing.
            XCTAssertNil(AgentHandleRegistry.shared.get(id: agent.handle))
            XCTAssertEqual(XCTWaiter.wait(for: [agent.released], timeout: 5), .completed)
        }
    }
}

/// The agent runtime requires a storage-backed core: sqlite_db_path() is nil
/// for the in-memory core, so agent sessions need real storage.
func makeStorageBackedBridgeForTests() throws -> MahoBridge {
    let bridge = MahoBridge()
    let root = FileManager.default.temporaryDirectory
        .appendingPathComponent("maho-agent-core-\(UUID().uuidString)", isDirectory: true)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    XCTAssertTrue(bridge.initialize(storagePath: root.path))
    return bridge
}
