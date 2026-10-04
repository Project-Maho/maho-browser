import XCTest
@testable import Maho

final class ChatSessionRegistryTests: XCTestCase {

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    /// Returns a registry whose nativeFree tallies calls rather than crossing FFI.
    private func makeRegistry() -> (ChatSessionRegistry, () -> Int) {
        var count = 0
        let lock = NSLock()
        let registry = ChatSessionRegistry { _ in
            lock.lock()
            count += 1
            lock.unlock()
        }
        return (registry, { count })
    }

    /// Returns a non-null OpaquePointer backed by a heap allocation.
    /// Caller is responsible for ensuring the allocating test doesn't actually
    /// trigger the real FFI free (the injected nativeFree is a no-op here).
    private func makePtr() -> OpaquePointer {
        // Allocate a small buffer so we have a valid, non-null opaque pointer.
        let raw = UnsafeMutableRawPointer.allocate(byteCount: 8, alignment: 8)
        return OpaquePointer(raw)
    }

    // -------------------------------------------------------------------------
    // Basic functionality
    // -------------------------------------------------------------------------

    func testRegisterReturnsNonEmptyUUID() {
        let (registry, _) = makeRegistry()
        let ptr = makePtr()
        let id = registry.register(ptr: ptr)
        XCTAssertFalse(id.isEmpty)
    }

    func testGetReturnsPointerAfterRegister() {
        let (registry, _) = makeRegistry()
        let ptr = makePtr()
        let id = registry.register(ptr: ptr)
        XCTAssertEqual(registry.get(id: id), ptr)
    }

    func testGetReturnsNilForUnknownID() {
        let (registry, _) = makeRegistry()
        XCTAssertNil(registry.get(id: "unknown-uuid"))
    }

    func testReleaseFreesPointerAndRemovesFromMap() {
        let (registry, freeCount) = makeRegistry()
        let ptr = makePtr()
        let id = registry.register(ptr: ptr)
        registry.release(id: id)
        XCTAssertEqual(freeCount(), 1)
        XCTAssertNil(registry.get(id: id))
    }

    func testReleaseOfUnknownIDIsNoOp() {
        let (registry, freeCount) = makeRegistry()
        registry.release(id: "does-not-exist")
        XCTAssertEqual(freeCount(), 0)
    }

    func testReleaseAlreadyReleasedIDIsNoOp() {
        let (registry, freeCount) = makeRegistry()
        let ptr = makePtr()
        let id = registry.register(ptr: ptr)
        registry.release(id: id)
        registry.release(id: id) // second call — must not double-free
        XCTAssertEqual(freeCount(), 1)
    }

    func testReleaseAllFreesEverySession() {
        let (registry, freeCount) = makeRegistry()
        let count = 20
        for _ in 0..<count {
            _ = registry.register(ptr: makePtr())
        }
        registry.releaseAll()
        XCTAssertEqual(freeCount(), count)
    }

    // -------------------------------------------------------------------------
    // Concurrency — 10 concurrent tasks × 100 register+release cycles each
    // -------------------------------------------------------------------------

    func testConcurrentRegisterRelease() {
        let threadCount = 10
        let opsPerThread = 100
        let expected = threadCount * opsPerThread

        var totalFreed = 0
        let countLock = NSLock()
        let registry = ChatSessionRegistry { _ in
            countLock.lock()
            totalFreed += 1
            countLock.unlock()
        }

        let expectation = self.expectation(description: "all tasks complete")
        expectation.expectedFulfillmentCount = threadCount
        let queues = (0..<threadCount).map { i in
            DispatchQueue(label: "test.registry.\(i)", attributes: .concurrent)
        }

        for q in queues {
            q.async {
                for _ in 0..<opsPerThread {
                    let ptr = UnsafeMutableRawPointer.allocate(byteCount: 8, alignment: 8)
                    let id = registry.register(ptr: OpaquePointer(ptr))
                    registry.release(id: id)
                }
                expectation.fulfill()
            }
        }

        wait(for: [expectation], timeout: 30)
        XCTAssertEqual(totalFreed, expected, "Expected \(expected) freed pointers, got \(totalFreed)")
    }

    // -------------------------------------------------------------------------
    // IDs are unique across concurrent registrations
    // -------------------------------------------------------------------------

    func testEachRegistrationGetsDistinctID() {
        let (registry, _) = makeRegistry()
        var ids = Set<String>()
        let idsLock = NSLock()
        let group = DispatchGroup()

        for _ in 0..<100 {
            group.enter()
            DispatchQueue.global().async {
                let id = registry.register(ptr: self.makePtr())
                idsLock.lock()
                ids.insert(id)
                idsLock.unlock()
                group.leave()
            }
        }

        group.wait()
        XCTAssertEqual(ids.count, 100)
    }
}
