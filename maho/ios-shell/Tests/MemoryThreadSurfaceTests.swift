import SwiftUI
import WebKit
import XCTest
@testable import Maho

// A real WK document exercises the production script-message handler and reply
// transport. It does not emulate the native dispatcher or any native registry.
@MainActor
private final class MemoryThreadDocument: NSObject, WKNavigationDelegate {
    let host: (webView: WKWebView, controller: WebViewBridgeController)
    private let loaded = XCTestExpectation(description: "fixture document loaded")

    init(bridge: MahoBridge) {
        host = WebViewBridgeController.makeAgenticWebView(bridge: bridge)
        super.init()
        host.webView.navigationDelegate = self
    }

    func load() {
        host.webView.loadHTMLString("<html><body>Memory thread native transport fixture</body></html>", baseURL: nil)
        XCTAssertEqual(XCTWaiter.wait(for: [loaded], timeout: 10), .completed)
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) { loaded.fulfill() }

    func evaluate(_ body: String, arguments: [String: Any] = [:]) throws -> String {
        let finished = XCTestExpectation(description: "native WebView round trip")
        var outcome: Result<Any, Error>?
        host.webView.callAsyncJavaScript(body, arguments: arguments, in: nil, in: .page) { result in
            outcome = result
            finished.fulfill()
        }
        XCTAssertEqual(XCTWaiter.wait(for: [finished], timeout: 30), .completed)
        return try XCTUnwrap(try XCTUnwrap(outcome).get() as? String)
    }

    func rpc(_ request: String) throws -> [String: Any] {
        let json = try evaluate("""
            return await new Promise(resolve => {
                window.__mahoBridgeResponse = response => resolve(response);
                window.webkit.messageHandlers.mahoBridge.postMessage(request);
            });
            """, arguments: ["request": request])
        return try XCTUnwrap(JSONSerialization.jsonObject(with: Data(json.utf8)) as? [String: Any])
    }

    func dismantle() {
        let coordinator = AgentWebView.Coordinator()
        coordinator.controller = host.controller
        AgentWebView.dismantleUIView(host.webView, coordinator: coordinator)
        let drained = XCTestExpectation(description: "native owner close fence")
        host.controller.dispatchQueue.async { drained.fulfill() }
        XCTAssertEqual(XCTWaiter.wait(for: [drained], timeout: 5), .completed)
    }
}

extension MemoryThreadOwnerTests {
    @MainActor
    func testDeviceDocumentDismantleReclaimsAgentAndChat() throws {
        // Given: two native hosts with actual document-originated resource creation.
        let bridge = try makeStorageBackedBridgeForTests()
        defer { bridge.destroy() }
        let a = MemoryThreadDocument(bridge: bridge)
        let b = MemoryThreadDocument(bridge: bridge)
        a.load(); b.load()
        let create = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"agentCreateSession\",\"params\":[\"mt-surface-\(UUID().uuidString)\"]}"
        let handleA = try XCTUnwrap(a.rpc(create)["result"] as? String)
        let pointerA = try XCTUnwrap(AgentHandleRegistry.shared.get(id: handleA))
        let cleanupA = XCTestExpectation(description: "surface A native release")
        AgentSessionRegistry.shared.context(for: pointerA)?.memoryThreadDidRelease = { cleanupA.fulfill() }
        defer {
            bridge.agentFreeSession(pointerA)
            AgentHandleRegistry.shared.unregister(id: handleA)
            XCTAssertEqual(XCTWaiter.wait(for: [cleanupA], timeout: 5), .completed)
        }
        let handleB = try XCTUnwrap(b.rpc(create)["result"] as? String)
        let pointerB = try XCTUnwrap(AgentHandleRegistry.shared.get(id: handleB))
        let cleanupB = XCTestExpectation(description: "surface B native release")
        AgentSessionRegistry.shared.context(for: pointerB)?.memoryThreadDidRelease = { cleanupB.fulfill() }
        defer {
            bridge.agentFreeSession(pointerB)
            AgentHandleRegistry.shared.unregister(id: handleB)
            XCTAssertEqual(XCTWaiter.wait(for: [cleanupB], timeout: 5), .completed)
        }
        let chat = try XCTUnwrap(a.rpc(
            #"{"jsonrpc":"2.0","id":2,"method":"chatSessionStart","params":{"apiKey":"fixture-not-a-secret","endpoint":"http://127.0.0.1:9/v1/chat/completions","model":"fixture","systemInstruction":""}}"#
        )["result"] as? String)
        let chatPointer = try XCTUnwrap(ChatSessionRegistry.shared.get(id: chat))
        defer { bridge.freeChatSession(chatPointer) }

        // When: A is dismissed natively, without a JS free/unmount message.
        a.dismantle()

        // Then: A loses both native owners and handlers; B's actual RPC still works.
        XCTAssertNil(AgentHandleRegistry.shared.get(id: handleA))
        XCTAssertNil(ChatSessionRegistry.shared.get(id: chat))
        XCTAssertEqual(try a.evaluate("return typeof window.webkit?.messageHandlers?.mahoBridge;"), "undefined")
        XCTAssertEqual(try a.evaluate("return typeof window.webkit?.messageHandlers?.mahoBridgeNav;"), "undefined")
        let tools = try b.rpc("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"agentListTools\",\"params\":[\"\(handleB)\"]}")
        XCTAssertNil(tools["error"])
        XCTAssertNotNil(tools["result"] as? [Any])
        b.dismantle()
    }
}

extension MemoryThreadStartupTests {
    @MainActor
    func testDeviceDocumentReportsFailedStartup() throws {
        // Given: failed native storage initialization and an actual factory document.
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("MemoryThreadSurface-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer {
            do { try FileManager.default.removeItem(at: root) }
            catch { XCTFail("Fixture directory cleanup failed: \(error)") }
        }
        let file = root.appendingPathComponent("file")
        try Data("fixture".utf8).write(to: file)
        let bridge = MahoBridge()
        defer { bridge.destroy() }
        XCTAssertFalse(bridge.initialize(storagePath: file.appendingPathComponent("profile").path))
        let document = MemoryThreadDocument(bridge: bridge)
        document.load()
        defer { document.dismantle() }

        // When: a dependent request crosses WK's real script-message transport.
        let response = try document.rpc(
            #"{"jsonrpc":"2.0","id":"mt-failed","method":"getSpaceAIConfig","params":["mt-space"]}"#
        )

        // Then: native failure is a typed error, never null success.
        XCTAssertNil(response["result"])
        let error = try XCTUnwrap(response["error"] as? [String: String])
        XCTAssertEqual(error["reason"], "core_initialization_failed")
    }
}

extension MemoryThreadEventQueueTests {
    @MainActor
    func testDeviceDocumentReceivesTerminalAfterSaturatedProducer() throws {
        // Given: a native agent created by a real WebView RPC, not a fake pointer.
        let bridge = try makeStorageBackedBridgeForTests()
        defer { bridge.destroy() }
        let document = MemoryThreadDocument(bridge: bridge)
        document.load()
        let request = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"agentCreateSession\",\"params\":[\"mt-queue-\(UUID().uuidString)\"]}"
        let handle = try XCTUnwrap(document.rpc(request)["result"] as? String)
        let pointer = try XCTUnwrap(AgentHandleRegistry.shared.get(id: handle))
        defer { bridge.agentFreeSession(pointer); AgentHandleRegistry.shared.unregister(id: handle) }
        let queue = try XCTUnwrap(AgentSessionRegistry.shared.context(for: pointer))
        for _ in 0..<2048 { queue.enqueue(type: "token", data: "x") }

        // When: the producer completes before the real document starts polling.
        queue.enqueue(type: "complete", data: ["full_text": String(repeating: "x", count: 2048), "tool_calls_json": "[]"])
        let json = try document.evaluate("""
            const events = [];
            for (let id = 2; id < 2053; id++) {
                const response = await new Promise(resolve => {
                    window.__mahoBridgeResponse = raw => resolve(JSON.parse(raw));
                    window.webkit.messageHandlers.mahoBridge.postMessage(JSON.stringify({
                        jsonrpc: "2.0", id, method: "agentPollEvent", params: [handle]
                    }));
                });
                if (response.error) throw new Error(JSON.stringify(response.error));
                if (response.result === null) break;
                events.push(JSON.parse(response.result));
            }
            return JSON.stringify(events);
            """, arguments: ["handle": handle])
        let events = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(json.utf8)) as? [[String: Any]])

        // Then: the real transport delivers all text and one successful terminal.
        XCTAssertEqual(events.filter { $0["type"] as? String == "token" }.compactMap { $0["data"] as? String }.joined(),
                       String(repeating: "x", count: 2048))
        XCTAssertEqual(events.filter { $0["type"] as? String == "complete" }.count, 1)
        document.dismantle()
    }
}
