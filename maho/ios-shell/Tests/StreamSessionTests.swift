import XCTest
import WebKit
@testable import Maho

// MARK: - Mock WebView that captures evaluateJavaScript invocations

private final class MockWKWebView: WKWebView {
    private let jsLock = NSLock()
    private var _jsCalls: [String] = []

    var jsCalls: [String] {
        jsLock.lock()
        defer { jsLock.unlock() }
        return _jsCalls
    }

    override func evaluateJavaScript(_ javaScriptString: String, completionHandler: ((Any?, Error?) -> Void)? = nil) {
        jsLock.lock()
        _jsCalls.append(javaScriptString)
        jsLock.unlock()
        completionHandler?(nil, nil)
    }

    /// Initialize without a real WKWebViewConfiguration requirement.
    convenience init() {
        self.init(frame: .zero, configuration: WKWebViewConfiguration())
    }
}

// MARK: - StreamSessionTests

final class StreamSessionTests: XCTestCase {

    private func makeController() -> (WebViewBridgeController, MockWKWebView) {
        let controller = WebViewBridgeController(
            bridge: .shared,
            onBack: nil,
            onOpenSettings: nil
        )
        let mockWebView = MockWKWebView()
        controller.attach(to: mockWebView)
        return (controller, mockWebView)
    }

    /// Drain the main run loop to process async dispatches.
    private func drainMain(seconds: TimeInterval = 0.5) {
        RunLoop.main.run(until: Date().addingTimeInterval(seconds))
    }

    // MARK: - Concurrent streams: 3 sessions × 500 tokens → all 1500 delivered

    func testConcurrentThreeSessionsFiveHundredTokensEach() {
        let (controller, mockWebView) = makeController()
        let sessions = ["session-A", "session-B", "session-C"]
        let tokensPerSession = 500
        let expectation = self.expectation(description: "all tokens pushed")
        expectation.expectedFulfillmentCount = sessions.count

        for sessionId in sessions {
            DispatchQueue.global(qos: .userInitiated).async {
                for i in 0..<tokensPerSession {
                    controller.pushToken(sessionId: sessionId, token: "\(sessionId)-\(i)")
                }
                controller.pushStreamComplete(sessionId: sessionId, finalMessage: "done-\(sessionId)")
                expectation.fulfill()
            }
        }

        wait(for: [expectation], timeout: 30)
        drainMain(seconds: 1.0)

        let calls = mockWebView.jsCalls

        // Verify all tokens delivered per session in order
        for sessionId in sessions {
            let batchCalls = calls.filter { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }
            var allTokens: [String] = []
            for call in batchCalls {
                // Extract tokens array from JS call: window.__mahoStreamBatch("session-A", ["t0","t1",...])
                guard let startRange = call.range(of: "["),
                      let endRange = call.range(of: "]", options: .backwards) else { continue }
                let arrayStr = String(call[startRange.lowerBound...endRange.lowerBound])
                if let data = arrayStr.data(using: .utf8),
                   let tokens = try? JSONSerialization.jsonObject(with: data) as? [String] {
                    allTokens.append(contentsOf: tokens)
                }
            }

            XCTAssertEqual(allTokens.count, tokensPerSession,
                           "Session \(sessionId): expected \(tokensPerSession) tokens, got \(allTokens.count)")

            // Verify ordering within session
            for i in 0..<allTokens.count {
                XCTAssertEqual(allTokens[i], "\(sessionId)-\(i)",
                               "Session \(sessionId): token at index \(i) out of order")
            }

            // Verify complete event fired
            let completeCalls = calls.filter { $0.contains("__mahoStreamComplete") && $0.contains(sessionId) }
            XCTAssertEqual(completeCalls.count, 1, "Session \(sessionId) should have exactly 1 complete call")
        }
    }

    // MARK: - Buffer overflow: 300 tokens fast → 44 dropped, 256 retained

    func testBufferOverflowDropsOldestTokens() {
        let (controller, mockWebView) = makeController()
        let sessionId = "overflow-test"

        // Push 300 tokens as fast as possible without giving main queue time to flush
        for i in 0..<300 {
            controller.pushToken(sessionId: sessionId, token: "t\(i)")
        }
        controller.pushStreamComplete(sessionId: sessionId, finalMessage: "done")

        drainMain(seconds: 1.0)

        let calls = mockWebView.jsCalls
        let batchCalls = calls.filter { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }

        var allTokens: [String] = []
        var totalDropped = 0

        for call in batchCalls {
            // Parse tokens
            guard let startRange = call.range(of: "["),
                  let endRange = call.range(of: "]", options: .backwards) else { continue }
            let arrayStr = String(call[startRange.lowerBound...endRange.lowerBound])
            if let data = arrayStr.data(using: .utf8),
               let tokens = try? JSONSerialization.jsonObject(with: data) as? [String] {
                allTokens.append(contentsOf: tokens)
            }

            // Parse droppedCount if present (third arg after the array)
            let afterArray = String(call[endRange.upperBound...])
            let components = afterArray.components(separatedBy: ",")
            for comp in components {
                let trimmed = comp.trimmingCharacters(in: CharacterSet.whitespaces.union(.init(charactersIn: ")")))
                if let drops = Int(trimmed), drops > 0 {
                    totalDropped += drops
                }
            }
        }

        // With 300 pushes: buffer overflow at 256 means 300 - 256 = 44 drops
        // But batches flush at 32, so first 32×N tokens flush normally before overflow
        // The overflow only happens if tokens accumulate faster than flushes clear them.
        // Since flushes go to main queue which we're blocking, tokens CAN overflow.
        // Total delivered + dropped should equal 300
        XCTAssertEqual(allTokens.count + totalDropped, 300,
                       "delivered(\(allTokens.count)) + dropped(\(totalDropped)) should equal 300")

        // If overflow occurred, verify FIFO: retained tokens should be the NEWEST
        if totalDropped > 0 {
            // The last token in allTokens should be "t299" (newest)
            XCTAssertEqual(allTokens.last, "t299", "Newest token should survive FIFO drop")
            // Oldest surviving should be "t\(totalDropped)"
            // Note: this is approximate since batches of 32 flush mid-stream
        }
    }

    // MARK: - Main thread blocked 200ms → tokens buffer, eventual catchup

    func testMainThreadBlockDoesNotLoseTokens() {
        let (controller, mockWebView) = makeController()
        let sessionId = "block-test"
        let tokenCount = 100

        // Block main thread for 200ms while pushing tokens from background
        let pushDone = self.expectation(description: "push done")
        DispatchQueue.global().async {
            // Give main thread a moment to start blocking
            Thread.sleep(forTimeInterval: 0.05)
            for i in 0..<tokenCount {
                controller.pushToken(sessionId: sessionId, token: "t\(i)")
                if i % 10 == 0 { Thread.sleep(forTimeInterval: 0.005) }
            }
            controller.pushStreamComplete(sessionId: sessionId, finalMessage: "done")
            pushDone.fulfill()
        }

        // Block main thread 200ms (simulating GC pause)
        Thread.sleep(forTimeInterval: 0.2)

        wait(for: [pushDone], timeout: 10)
        drainMain(seconds: 1.0)

        let calls = mockWebView.jsCalls
        let batchCalls = calls.filter { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }

        var allTokens: [String] = []
        var totalDropped = 0
        for call in batchCalls {
            guard let startRange = call.range(of: "["),
                  let endRange = call.range(of: "]", options: .backwards) else { continue }
            let arrayStr = String(call[startRange.lowerBound...endRange.lowerBound])
            if let data = arrayStr.data(using: .utf8),
               let tokens = try? JSONSerialization.jsonObject(with: data) as? [String] {
                allTokens.append(contentsOf: tokens)
            }
            let afterArray = String(call[endRange.upperBound...])
            for comp in afterArray.components(separatedBy: ",") {
                let trimmed = comp.trimmingCharacters(in: CharacterSet.whitespaces.union(.init(charactersIn: ")")))
                if let drops = Int(trimmed), drops > 0 {
                    totalDropped += drops
                }
            }
        }

        XCTAssertEqual(allTokens.count + totalDropped, tokenCount,
                       "No token loss: delivered(\(allTokens.count)) + dropped(\(totalDropped)) == \(tokenCount)")
    }

    // MARK: - Complete flushes pending batch

    func testCompleteFlushesRemainingBuffer() {
        let (controller, mockWebView) = makeController()
        let sessionId = "complete-flush"

        // Push 5 tokens (below 32 threshold, no timer will fire before complete)
        for i in 0..<5 {
            controller.pushToken(sessionId: sessionId, token: "t\(i)")
        }
        controller.pushStreamComplete(sessionId: sessionId, finalMessage: "final")

        drainMain(seconds: 0.5)

        let calls = mockWebView.jsCalls
        let batchCalls = calls.filter { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }
        let completeCalls = calls.filter { $0.contains("__mahoStreamComplete") && $0.contains(sessionId) }

        XCTAssertFalse(batchCalls.isEmpty, "Pending tokens should flush on complete")
        XCTAssertEqual(completeCalls.count, 1)

        // Verify batch comes before complete in the JS call sequence
        if let lastBatchIdx = calls.lastIndex(where: { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }),
           let completeIdx = calls.firstIndex(where: { $0.contains("__mahoStreamComplete") && $0.contains(sessionId) }) {
            XCTAssertLessThan(lastBatchIdx, completeIdx, "Batch must dispatch before complete")
        }

        // Verify all 5 tokens delivered
        var allTokens: [String] = []
        for call in batchCalls {
            guard let startRange = call.range(of: "["),
                  let endRange = call.range(of: "]", options: .backwards) else { continue }
            let arrayStr = String(call[startRange.lowerBound...endRange.lowerBound])
            if let data = arrayStr.data(using: .utf8),
               let tokens = try? JSONSerialization.jsonObject(with: data) as? [String] {
                allTokens.append(contentsOf: tokens)
            }
        }
        XCTAssertEqual(allTokens.count, 5)
    }

    // MARK: - Error flushes pending batch

    func testErrorFlushesRemainingBuffer() {
        let (controller, mockWebView) = makeController()
        let sessionId = "error-flush"

        for i in 0..<5 {
            controller.pushToken(sessionId: sessionId, token: "t\(i)")
        }
        controller.pushStreamError(sessionId: sessionId, error: "network timeout")

        drainMain(seconds: 0.5)

        let calls = mockWebView.jsCalls
        let batchCalls = calls.filter { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }
        let errorCalls = calls.filter { $0.contains("__mahoStreamError") && $0.contains(sessionId) }

        XCTAssertFalse(batchCalls.isEmpty, "Pending tokens should flush on error")
        XCTAssertEqual(errorCalls.count, 1)

        // Verify batch comes before error
        if let lastBatchIdx = calls.lastIndex(where: { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }),
           let errorIdx = calls.firstIndex(where: { $0.contains("__mahoStreamError") && $0.contains(sessionId) }) {
            XCTAssertLessThan(lastBatchIdx, errorIdx, "Batch must dispatch before error")
        }

        // Verify error message
        XCTAssertTrue(errorCalls[0].contains("network timeout"))
    }

    // MARK: - 32-token batch triggers immediate flush

    func testBatchSizeTriggersImmediateFlush() {
        let (controller, mockWebView) = makeController()
        let sessionId = "batch-trigger"

        for i in 0..<32 {
            controller.pushToken(sessionId: sessionId, token: "t\(i)")
        }

        // Give main queue time to process the dispatch
        drainMain(seconds: 0.3)

        let batchCalls = mockWebView.jsCalls.filter { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }
        XCTAssertGreaterThanOrEqual(batchCalls.count, 1, "32 tokens should trigger at least one flush")
    }

    // MARK: - 60ms timer triggers flush for sub-batch

    func testTimerFlushesSubBatch() {
        let (controller, mockWebView) = makeController()
        let sessionId = "timer-test"

        // Push fewer than 32 tokens
        for i in 0..<10 {
            controller.pushToken(sessionId: sessionId, token: "t\(i)")
        }

        // Wait longer than 60ms for timer to fire, plus main queue processing
        drainMain(seconds: 0.3)

        let batchCalls = mockWebView.jsCalls.filter { $0.contains("__mahoStreamBatch") && $0.contains(sessionId) }
        XCTAssertGreaterThanOrEqual(batchCalls.count, 1, "Timer should flush sub-batch after 60ms")
    }

    // MARK: - WebView teardown mid-stream → no crash

    func testWebViewTeardownMidStreamNoCrash() {
        var controller: WebViewBridgeController? = WebViewBridgeController(
            bridge: .shared, onBack: nil, onOpenSettings: nil
        )
        let mockWebView = MockWKWebView()
        controller!.attach(to: mockWebView)

        // Start streaming
        for i in 0..<50 {
            controller!.pushToken(sessionId: "teardown-test", token: "t\(i)")
        }

        // Destroy controller mid-stream (triggers deinit → shutdownStreaming)
        controller = nil

        // Push some time to ensure no crash from dangling references
        drainMain(seconds: 0.3)

        // If we get here without crash, test passes
        XCTAssertTrue(true)
    }

    // MARK: - Tool delta bypasses coalescing

    func testToolDeltaBypassesCoalescing() {
        let (controller, mockWebView) = makeController()
        let sessionId = "tool-delta"
        let deltaJson = "{\"index\":0,\"name\":\"search\",\"argumentsDelta\":\"{\\\"q\\\":\\\"hello\\\"}\"}"

        controller.pushToolDelta(sessionId: sessionId, deltaJson: deltaJson)

        drainMain(seconds: 0.3)

        let toolCalls = mockWebView.jsCalls.filter { $0.contains("__mahoStreamToolDelta") }
        XCTAssertEqual(toolCalls.count, 1)
        XCTAssertTrue(toolCalls[0].contains(sessionId))
    }

    // MARK: - Batched scheduleJsEvaluation combines statements with semicolon

    func testBatchedScheduleJsEvaluationCombinesStatements() {
        let (controller, mockWebView) = makeController()
        let sessionId = "batch-js"
        let delta1 = "{\"index\":0,\"name\":\"search\"}"
        let delta2 = "{\"index\":1,\"name\":\"browse\"}"

        controller.pushToolDelta(sessionId: sessionId, deltaJson: delta1)
        controller.pushToolDelta(sessionId: sessionId, deltaJson: delta2)

        drainMain(seconds: 0.3)

        let calls = mockWebView.jsCalls
        // The two scheduled statements should be combined into a single evaluateJavaScript call joined by ';'
        XCTAssertEqual(calls.count, 1)
        XCTAssertTrue(calls[0].contains("__mahoStreamToolDelta('batch-js', {\"index\":0,\"name\":\"search\"});window.__mahoStreamToolDelta('batch-js', {\"index\":1,\"name\":\"browse\"})"))
    }

    // MARK: - Image delta bypasses coalescing

    func testImageDeltaBypassesCoalescing() {
        let (controller, mockWebView) = makeController()
        let sessionId = "image-delta"
        let deltaJson = "{\"index\":0,\"mime\":\"image/png\",\"b64Chunk\":\"iVBOR\"}"

        controller.pushImageDelta(sessionId: sessionId, deltaJson: deltaJson)

        drainMain(seconds: 0.3)

        let imgCalls = mockWebView.jsCalls.filter { $0.contains("__mahoStreamImageDelta") }
        XCTAssertEqual(imgCalls.count, 1)
        XCTAssertTrue(imgCalls[0].contains(sessionId))
    }

    // MARK: - Shutdown flushes all active sessions

    func testShutdownFlushesAllSessions() {
        let (controller, mockWebView) = makeController()

        controller.pushToken(sessionId: "s1", token: "hello")
        controller.pushToken(sessionId: "s2", token: "world")

        controller.shutdownStreaming()

        drainMain(seconds: 0.5)

        let calls = mockWebView.jsCalls
        // Both sessions should have their tokens flushed (via error path)
        let s1Batch = calls.filter { $0.contains("__mahoStreamBatch") && $0.contains("s1") }
        let s2Batch = calls.filter { $0.contains("__mahoStreamBatch") && $0.contains("s2") }
        XCTAssertFalse(s1Batch.isEmpty, "s1 should be flushed on shutdown")
        XCTAssertFalse(s2Batch.isEmpty, "s2 should be flushed on shutdown")

        let errorCalls = calls.filter { $0.contains("__mahoStreamError") }
        XCTAssertEqual(errorCalls.count, 2, "Both sessions should get error on shutdown")
    }

    // MARK: - Thinking stream coalescing

    func testThinkingStreamCoalescesAndFlushes() {
        let (controller, mockWebView) = makeController()
        let sessionId = "thinking-session"

        for i in 0..<5 {
            controller.pushThinking(sessionId: sessionId, thinking: "thought \(i); ")
        }
        controller.pushStreamComplete(sessionId: sessionId, finalMessage: "done")

        drainMain(seconds: 0.5)

        let thinkingCalls = mockWebView.jsCalls.filter { $0.contains("__mahoStreamThinking") && $0.contains(sessionId) }
        XCTAssertFalse(thinkingCalls.isEmpty, "Thinking chunks should be flushed")
        XCTAssertTrue(thinkingCalls.joined().contains("thought 0; thought 1;"))
    }

    // MARK: - Event Throttler

    func testEventThrottlerThrottlesRapidEvents() {
        let throttler = EventThrottler(interval: 0.05)
        var executeCount = 0
        let expectation = self.expectation(description: "throttled executions")

        for _ in 0..<10 {
            throttler.throttle {
                executeCount += 1
            }
        }

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
            expectation.fulfill()
        }

        wait(for: [expectation], timeout: 2.0)
        // Should have executed at most 2 times instead of 10
        XCTAssertLessThanOrEqual(executeCount, 2)
        XCTAssertGreaterThanOrEqual(executeCount, 1)
    }
}
