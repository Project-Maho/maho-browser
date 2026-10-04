import Foundation
import XCTest
@testable import Maho

final class MemoryThreadEventQueueTests: XCTestCase {
    private struct Event: Decodable {
        let type: String
        let seq: UInt64
        let data: Payload

        enum Payload: Decodable {
            case text(String)
            case fields([String: String])

            init(from decoder: Decoder) throws {
                let value = try decoder.singleValueContainer()
                if let text = try? value.decode(String.self) {
                    self = .text(text)
                } else {
                    self = .fields(try value.decode([String: String].self))
                }
            }

            var text: String? {
                switch self {
                case .text(let text): return text
                case .fields: return nil
                }
            }
        }
    }

    private func context() -> AgentSessionContext {
        // No artifact file is created by the queue; every test owns this context.
        AgentSessionContext(artifactRoot: FileManager.default.temporaryDirectory)
    }

    private func drain(_ context: AgentSessionContext) throws -> [Event] {
        var events: [Event] = []
        // Synchronous dequeue, not an async wait/poll. The producer has finished.
        while let json = context.dequeue() {
            events.append(try JSONDecoder().decode(Event.self, from: Data(json.utf8)))
        }
        return events
    }

    func testFullDeltaQueueDeliversCompleteAndError() throws {
        // Given: separate success/error turns, both count-saturated before polling.
        for terminal in ["complete", "error"] {
            let queue = context()
            defer { queue.beginClosing() }
            for _ in 0..<2048 { queue.enqueue(type: "token", data: "x") }

            // When: the native producer ends the turn before the consumer drains.
            queue.enqueue(type: terminal, data: "fixture-terminal")
            let events = try drain(queue)

            // Then: accepted text is exact and the actual terminal survives once.
            XCTAssertEqual(events.filter { $0.type == "token" }.compactMap { $0.data.text }.joined(),
                           String(repeating: "x", count: 2048))
            XCTAssertEqual(events.filter { $0.type == terminal }.count, 1)
            XCTAssertEqual(events.last?.type, terminal)
            XCTAssertEqual(events.map(\.seq), events.map(\.seq).sorted())
            XCTAssertEqual(Set(events.map(\.seq)).count, events.count)
        }
    }

    func testApprovalAndToolOrderSurvivesReduction() throws {
        // Given: 100,000 compatible deltas are representable below the byte bound.
        let queue = context()
        defer { queue.beginClosing() }
        for _ in 0..<100_000 { queue.enqueue(type: "token", data: "x") }
        let controls = ["tool_call", "tool_result", "approval_request", "complete"]

        // When: semantic barriers arrive after the count-saturated token stream.
        for kind in controls { queue.enqueue(type: kind, data: ["id": "mt-control"]) }
        let events = try drain(queue)

        // Then: neither reduction nor capacity may silently lose text or barriers.
        XCTAssertEqual(events.filter { $0.type == "token" }.compactMap { $0.data.text }.joined(),
                       String(repeating: "x", count: 100_000))
        XCTAssertEqual(events.filter { $0.type != "token" }.map(\.type), controls)
        XCTAssertLessThanOrEqual(events.count, 2048)
    }

    func testControlOverflowTerminatesExplicitly() throws {
        // Given: unique noncoalescible control records exceed the byte budget.
        let queue = context()
        defer { queue.beginClosing() }
        for index in 0..<80 {
            queue.enqueue(type: "tool_call", data: [
                "id": "tool-\(index)", "args": String(repeating: "q", count: 65_536)
            ])
        }

        // When: the producer attempts success after unreducible overflow.
        queue.enqueue(type: "complete", data: ["full_text": ""])
        var serialized: [String] = []
        while let event = queue.dequeue() { serialized.append(event) }
        let events = try serialized.map { try JSONDecoder().decode(Event.self, from: Data($0.utf8)) }

        // Then: bounded, explicit failure replaces a false successful terminal.
        XCTAssertLessThanOrEqual(serialized.reduce(0) { $0 + $1.utf8.count }, 4 * 1024 * 1024)
        XCTAssertEqual(events.filter { $0.type == "complete" }.count, 0)
        XCTAssertEqual(events.filter { $0.type == "error" && $0.data.text == "event_queue_overflow" }.count, 1)
    }

    func testArtifactDedupOnlyAfterAdmission() throws {
        // Given: pressure and a real artifact record with an independently chosen ID.
        let queue = context()
        defer { queue.beginClosing() }
        for _ in 0..<2048 { queue.enqueue(type: "token", data: "x") }
        let artifact = AgentArtifactRecord(
            artifactId: "mt-artifact", sessionId: "mt-session", displayName: "fixture.txt",
            mimeType: "text/plain", sizeBytes: 1, createdAtMs: 0
        )

        // When: delivery is retried after capacity is consumed and then drained.
        queue.enqueueArtifact(artifact)
        var deliveredIds: [String] = []
        func collect() throws {
            while let json = queue.dequeue() {
                let object = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(json.utf8)) as? [String: Any])
                if object["type"] as? String == "artifact_created" {
                    let data = try XCTUnwrap(object["data"] as? [String: Any])
                    deliveredIds.append(try XCTUnwrap(data["artifact_id"] as? String))
                }
            }
        }
        try collect()
        queue.enqueueArtifact(artifact)
        try collect()

        // Then: admission and retry together deliver exactly one artifact.
        XCTAssertEqual(deliveredIds, ["mt-artifact"])
    }

    func testDeltaOverflowReportsFailureWithoutWaitingForComplete() throws {
        let queue = context()
        defer { queue.beginClosing() }
        for index in 0..<80 {
            queue.enqueue(type: index.isMultiple(of: 2) ? "token" : "thinking",
                          data: String(repeating: "x", count: 65_536))
        }

        let events = try drain(queue)

        XCTAssertEqual(events.filter {
            $0.type == "error" && $0.data.text == "event_queue_overflow"
        }.count, 1)
        XCTAssertEqual(events.last?.type, "error")
    }

    func testOversizedCompleteCannotDisplaceAcceptedTextAndReportSuccess() throws {
        let queue = context()
        defer { queue.beginClosing() }
        let acceptedText = String(repeating: "x", count: 60 * 65_536)
        for _ in 0..<60 {
            queue.enqueue(type: "token", data: String(repeating: "x", count: 65_536))
        }

        queue.enqueue(type: "complete", data: ["full_text": acceptedText])
        let events = try drain(queue)

        XCTAssertEqual(events.filter { $0.type == "token" }
            .compactMap { $0.data.text }.joined(), acceptedText)
        XCTAssertFalse(events.contains { $0.type == "complete" })
        XCTAssertEqual(events.filter {
            $0.type == "error" && $0.data.text == "event_queue_overflow"
        }.count, 1)
    }

    func testLargeEscapedDeltaUsesBoundedChunksWithoutChangingText() throws {
        let queue = context()
        defer { queue.beginClosing() }
        let text = String(repeating: "\"\\\n\u{1F600}", count: 20_000)

        queue.enqueue(type: "token", data: text)
        queue.enqueue(type: "complete", data: "")
        let events = try drain(queue)
        let chunks = events.filter { $0.type == "token" }.compactMap { $0.data.text }

        XCTAssertEqual(chunks.joined(), text)
        XCTAssertGreaterThan(chunks.count, 1)
        XCTAssertTrue(chunks.allSatisfy { $0.utf8.count <= 65_536 })
    }

    func testTerminalIsDeliveredOncePerNativeTurn() throws {
        let queue = context()
        defer { queue.beginClosing() }
        XCTAssertTrue(queue.beginTurn())
        queue.enqueue(type: "token", data: "first")

        queue.enqueue(type: "complete", data: "")
        queue.enqueue(type: "complete", data: "")
        queue.enqueue(type: "error", data: "late")
        queue.endTurn()
        XCTAssertTrue(queue.beginTurn())
        queue.enqueue(type: "token", data: "second")
        queue.enqueue(type: "complete", data: "")
        queue.endTurn()
        let events = try drain(queue)

        XCTAssertEqual(events.filter { $0.type == "complete" }.count, 2)
        XCTAssertFalse(events.contains { $0.type == "error" })
        XCTAssertEqual(events.filter { $0.type == "token" }
            .compactMap { $0.data.text }, ["first", "second"])
    }
}
