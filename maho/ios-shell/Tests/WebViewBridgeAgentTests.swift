import XCTest
@testable import Maho

final class WebViewBridgeAgentTests: XCTestCase {

    private func makePtr() -> OpaquePointer {
        let raw = UnsafeMutableRawPointer.allocate(byteCount: 8, alignment: 8)
        return OpaquePointer(raw)
    }

    func testRegisterReturnsNonEmptyStringHandle() {
        let registry = AgentHandleRegistry()
        let handle = registry.register(ptr: makePtr())
        XCTAssertFalse(handle.isEmpty)
    }

    func testGetResolvesHandleBackToPointer() {
        let registry = AgentHandleRegistry()
        let ptr = makePtr()
        let handle = registry.register(ptr: ptr)
        XCTAssertEqual(registry.get(id: handle), ptr)
    }

    func testGetReturnsNilForUnknownHandle() {
        let registry = AgentHandleRegistry()
        XCTAssertNil(registry.get(id: "not-a-real-handle"))
    }

    func testUnregisterRemovesMapping() {
        let registry = AgentHandleRegistry()
        let handle = registry.register(ptr: makePtr())
        registry.unregister(id: handle)
        XCTAssertNil(registry.get(id: handle))
    }

    func testUnregisterUnknownHandleIsNoOp() {
        let registry = AgentHandleRegistry()
        let handle = registry.register(ptr: makePtr())
        registry.unregister(id: "does-not-exist")
        XCTAssertNotNil(registry.get(id: handle))
    }

    func testEachRegistrationGetsDistinctHandle() {
        let registry = AgentHandleRegistry()
        var handles = Set<String>()
        for _ in 0..<100 {
            handles.insert(registry.register(ptr: makePtr()))
        }
        XCTAssertEqual(handles.count, 100)
    }

    func testConcurrentRegisterGetUnregisterIsThreadSafe() {
        let registry = AgentHandleRegistry()
        let threadCount = 10
        let opsPerThread = 100

        let expectation = self.expectation(description: "all tasks complete")
        expectation.expectedFulfillmentCount = threadCount

        for i in 0..<threadCount {
            DispatchQueue(label: "test.agent.registry.\(i)", attributes: .concurrent).async {
                for _ in 0..<opsPerThread {
                    let ptr = UnsafeMutableRawPointer.allocate(byteCount: 8, alignment: 8)
                    let handle = registry.register(ptr: OpaquePointer(ptr))
                    XCTAssertEqual(registry.get(id: handle), OpaquePointer(ptr))
                    registry.unregister(id: handle)
                    XCTAssertNil(registry.get(id: handle))
                    ptr.deallocate()
                }
                expectation.fulfill()
            }
        }

        wait(for: [expectation], timeout: 30)
    }
}
