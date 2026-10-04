import Foundation

@_silgen_name("mock_core") private func mockCore() -> OpaquePointer?
@_silgen_name("mock_pause_next_operation") private func mockPauseNextOperation()
@_silgen_name("mock_wait_until_operation_paused") private func mockWaitUntilOperationPaused(_ timeoutMs: UInt32) -> Bool
@_silgen_name("mock_resume_operation") private func mockResumeOperation()
@_silgen_name("mock_block_next_native_free") private func mockBlockNextNativeFree()
@_silgen_name("mock_wait_until_native_free_blocked") private func mockWaitUntilNativeFreeBlocked(_ timeoutMs: UInt32) -> Bool
@_silgen_name("mock_resume_native_free") private func mockResumeNativeFree()
@_silgen_name("mock_wait_for_native_free_count") private func mockWaitForNativeFreeCount(_ count: UInt32, _ timeoutMs: UInt32) -> Bool
@_silgen_name("mock_native_free_count") private func mockNativeFreeCount() -> UInt32
@_silgen_name("mock_free_before_operation_release_count") private func mockFreeBeforeOperationReleaseCount() -> UInt32
@_silgen_name("mock_operation_poison_observation_count") private func mockOperationPoisonObservationCount() -> UInt32
@_silgen_name("mock_distinct_session_count") private func mockDistinctSessionCount() -> UInt32
@_silgen_name("mock_emit_artifact") private func mockEmitArtifact()
@_silgen_name("mock_release_turn") private func mockReleaseTurn()
@_silgen_name("mock_set_artifact_path") private func mockSetArtifactPath(_ path: UnsafePointer<CChar>?)
@_silgen_name("mock_artifact_root") private func mockArtifactRoot() -> UnsafePointer<CChar>?
@_silgen_name("mock_write_at") private func mockWriteAt(_ relative: UnsafePointer<CChar>?, _ contents: UnsafePointer<CChar>?) -> Bool

enum FFIString {
    static func consume(_ ptr: UnsafeMutablePointer<CChar>?) -> String? {
        guard let ptr else { return nil }
        let value = String(cString: ptr)
        maho_string_free(ptr)
        return value
    }

    static func withCString<R>(_ string: String, _ body: (UnsafePointer<CChar>) -> R) -> R {
        string.withCString(body)
    }
}

final class MahoBridge {
    private let core = mockCore()!

    func withCore<R>(_ body: (OpaquePointer) -> R) -> R? {
        body(core)
    }
}

private enum TestFailure: Error, CustomStringConvertible {
    case assertion(String)

    var description: String {
        switch self {
        case .assertion(let message): return message
        }
    }
}

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    guard condition() else { throw TestFailure.assertion(message) }
}

private func writeMock(_ relative: String, _ contents: String) -> Bool {
    relative.withCString { relativePointer in
        contents.withCString { contentsPointer in
            mockWriteAt(relativePointer, contentsPointer)
        }
    }
}

private func setMockPath(_ path: String) {
    path.withCString { mockSetArtifactPath($0) }
}

private final class LockedResult {
    private let lock = NSLock()
    private var value: Bool?

    func set(_ value: Bool) {
        lock.lock()
        self.value = value
        lock.unlock()
    }

    func get() -> Bool? {
        lock.lock()
        defer { lock.unlock() }
        return value
    }
}

@main
private struct AgentOperationLeaseFocusedTests {
    static func main() throws {
        try testIdleFreeReturnsBeforeNativeTeardownCompletes()
        try testFreeWaitsForSendOperationLease()
        try testFreeWaitsForCancelOperationLease()
        try testFreeWaitsForListAndReadOperationLeases()
        try testClosedSessionRejectsOperationsAndCallbacks()
        try testNoFollowArtifactReads()
        print("PASS: deterministic native operation lease race tests")
    }

    private static func makeSession(_ bridge: MahoBridge) throws -> OpaquePointer {
        guard let session = bridge.agentCreateSession(sessionId: "session-23") else {
            throw TestFailure.assertion("session creation failed")
        }
        return session
    }

    private static func testIdleFreeReturnsBeforeNativeTeardownCompletes() throws {
        let bridge = MahoBridge()
        let session = try makeSession(bridge)
        let startingFreeCount = mockNativeFreeCount()
        let freeReturned = DispatchSemaphore(value: 0)

        mockBlockNextNativeFree()
        DispatchQueue.global(qos: .userInitiated).async {
            bridge.agentFreeSession(session)
            freeReturned.signal()
        }
        try expect(mockWaitUntilNativeFreeBlocked(2_000), "idle native free did not begin")
        try expect(freeReturned.wait(timeout: .now() + 1) == .success, "idle close blocked the caller/main queue")
        try expect(mockNativeFreeCount() == startingFreeCount, "idle native free completed before release signal")
        try expect(!bridge.agentCancel(session), "idle close allowed a future operation")

        mockResumeNativeFree()
        try expect(mockWaitForNativeFreeCount(startingFreeCount + 1, 2_000), "idle native free did not complete")
        try expect(mockOperationPoisonObservationCount() == 0, "idle close observed a poisoned pointer")
    }

    private static func raceFree(
        _ name: String,
        operation: @escaping (MahoBridge, OpaquePointer) -> Bool
    ) throws {
        let bridge = MahoBridge()
        let session = try makeSession(bridge)
        let startingFreeCount = mockNativeFreeCount()
        let operationFinished = DispatchSemaphore(value: 0)
        let freeReturned = DispatchSemaphore(value: 0)
        let operationResult = LockedResult()

        mockPauseNextOperation()
        DispatchQueue.global(qos: .userInitiated).async {
            operationResult.set(operation(bridge, session))
            operationFinished.signal()
        }
        try expect(mockWaitUntilOperationPaused(2_000), "\(name) did not pause inside native operation")

        DispatchQueue.global(qos: .userInitiated).async {
            bridge.agentFreeSession(session)
            freeReturned.signal()
        }
        try expect(freeReturned.wait(timeout: .now() + 2) == .success, "\(name) free blocked its caller")
        try expect(mockNativeFreeCount() == startingFreeCount, "\(name) native free ran before operation release")
        try expect(mockFreeBeforeOperationReleaseCount() == 0, "\(name) mock observed poisoned native free")

        mockResumeOperation()
        try expect(operationFinished.wait(timeout: .now() + 2) == .success, "\(name) operation did not resume")
        try expect(operationResult.get() == true, "\(name) operation failed")
        try expect(mockWaitForNativeFreeCount(startingFreeCount + 1, 2_000), "\(name) native free did not follow operation release")
        try expect(mockFreeBeforeOperationReleaseCount() == 0, "\(name) native free overlapped operation")
        try expect(mockOperationPoisonObservationCount() == 0, "\(name) operation observed poisoned pointer")
    }

    private static func testFreeWaitsForSendOperationLease() throws {
        try raceFree("send") { bridge, session in
            let accepted = bridge.agentSendMessage(session, message: "hello")
            mockReleaseTurn()
            return accepted
        }
    }

    private static func testFreeWaitsForCancelOperationLease() throws {
        try raceFree("cancel") { bridge, session in
            bridge.agentCancel(session)
        }
    }

    private static func testFreeWaitsForListAndReadOperationLeases() throws {
        try raceFree("list-tools") { bridge, session in
            bridge.agentListTools(session) == "[]"
        }
        try raceFree("list-artifacts") { bridge, session in
            bridge.agentListArtifacts(session).count == 1
        }

        let bridge = MahoBridge()
        let session = try makeSession(bridge)
        try expect(writeMock("report.txt", "native artifact bytes"), "could not write artifact fixture")
        let startingFreeCount = mockNativeFreeCount()
        let readFinished = DispatchSemaphore(value: 0)
        let readResult = LockedResult()
        mockPauseNextOperation()
        DispatchQueue.global(qos: .userInitiated).async {
            let data = bridge.agentReadArtifact(session, artifactId: "artifact-23")
            readResult.set(data == Data("native artifact bytes".utf8))
            readFinished.signal()
        }
        try expect(mockWaitUntilOperationPaused(2_000), "read did not pause in artifact-path FFI")
        bridge.agentFreeSession(session)
        try expect(mockNativeFreeCount() == startingFreeCount, "read native free ran before path operation release")
        mockResumeOperation()
        try expect(readFinished.wait(timeout: .now() + 2) == .success, "read did not resume")
        try expect(readResult.get() == true, "artifact read failed")
        try expect(mockWaitForNativeFreeCount(startingFreeCount + 1, 2_000), "read native free did not follow operation release")
        try expect(mockFreeBeforeOperationReleaseCount() == 0, "read overlapped native free")
        try expect(mockOperationPoisonObservationCount() == 0, "read observed poisoned pointer")
    }

    private static func testClosedSessionRejectsOperationsAndCallbacks() throws {
        let bridge = MahoBridge()
        let session = try makeSession(bridge)
        mockEmitArtifact()
        mockEmitArtifact()
        guard let event = bridge.agentPollEvent(session) else {
            throw TestFailure.assertion("artifact callback missing")
        }
        try expect(!event.contains("storage_rel_path"), "artifact event exposed a storage path")
        try expect(bridge.agentPollEvent(session) == nil, "duplicate artifact produced a second poll event")
        let listedArtifact = bridge.agentListArtifacts(session).first
        try expect(listedArtifact?["artifactId"] as? String == "artifact-23", "artifact list mapping failed")
        try expect(listedArtifact?["storage_rel_path"] == nil, "artifact list exposed a storage path")

        let startingFreeCount = mockNativeFreeCount()
        let operationFinished = DispatchSemaphore(value: 0)
        mockPauseNextOperation()
        DispatchQueue.global(qos: .userInitiated).async {
            _ = bridge.agentCancel(session)
            operationFinished.signal()
        }
        try expect(mockWaitUntilOperationPaused(2_000), "close callback test did not pause")
        bridge.agentFreeSession(session)
        mockEmitArtifact()
        try expect(bridge.agentPollEvent(session) == nil, "closed callback reached poll queue")
        try expect(!bridge.agentSendMessage(session, message: "closed"), "closed send accepted")
        try expect(!bridge.agentCancel(session), "closed cancel accepted")
        try expect(bridge.agentListTools(session) == nil, "closed list tools accepted")
        try expect(bridge.agentListArtifacts(session).isEmpty, "closed list artifacts accepted")
        try expect(bridge.agentReadArtifact(session, artifactId: "artifact-23") == nil, "closed read accepted")
        mockResumeOperation()
        try expect(operationFinished.wait(timeout: .now() + 2) == .success, "close callback operation did not resume")
        try expect(mockWaitForNativeFreeCount(startingFreeCount + 1, 2_000), "close callback native free missing")
        try expect(mockOperationPoisonObservationCount() == 0, "closed-session test observed poisoned pointer")
        try expect(mockDistinctSessionCount() >= 7, "mock did not allocate distinct native sessions")
    }

    private static func testNoFollowArtifactReads() throws {
        let bridge = MahoBridge()
        let session = try makeSession(bridge)
        defer {
            let expectedFreeCount = mockNativeFreeCount() + 1
            bridge.agentFreeSession(session)
            _ = mockWaitForNativeFreeCount(expectedFreeCount, 2_000)
        }
        try expect(writeMock("report.txt", "native artifact bytes"), "could not write nofollow fixture")
        setMockPath("report.txt")
        try expect(
            bridge.agentReadArtifact(session, artifactId: "artifact-23") == Data("native artifact bytes".utf8),
            "valid descriptor read failed"
        )
        for invalidPath in ["", "/etc/passwd", "../outside", "a/../outside", "a//outside"] {
            setMockPath(invalidPath)
            try expect(bridge.agentReadArtifact(session, artifactId: "artifact-23") == nil, "accepted invalid path \(invalidPath)")
        }

        guard let rootPointer = mockArtifactRoot() else {
            throw TestFailure.assertion("missing artifact root")
        }
        let root = URL(fileURLWithPath: String(cString: rootPointer))
        let outside = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try Data("outside".utf8).write(to: outside)
        defer { try? FileManager.default.removeItem(at: outside) }
        let link = root.appendingPathComponent("escape-link")
        try? FileManager.default.removeItem(at: link)
        try FileManager.default.createSymbolicLink(at: link, withDestinationURL: outside)
        setMockPath("escape-link")
        try expect(bridge.agentReadArtifact(session, artifactId: "artifact-23") == nil, "followed artifact symlink")
    }
}
