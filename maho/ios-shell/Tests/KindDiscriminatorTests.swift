import XCTest
@testable import Maho

final class KindDiscriminatorTests: XCTestCase {

    // MARK: - ShellEvent uses kind

    func testShellEventNavigateToContainsKind() throws {
        let event = ShellEvent.navigateTo(tabId: "tab-1", url: Url("https://example.com"))
        let json = try encodeToJson(event)
        XCTAssertEqual(json["kind"] as? String, "navigate_to")
    }

    func testShellEventCreateTabContainsKind() throws {
        let event = ShellEvent.createTab(spaceId: "space-1", url: nil, parentId: nil)
        let json = try encodeToJson(event)
        XCTAssertEqual(json["kind"] as? String, "create_tab")
    }

    // MARK: - CoreUpdate uses kind (verified via decode from hand-crafted JSON)

    func testCoreUpdateTabCreatedDecodesWithKind() throws {
        let json = """
        {"kind": "tab_created", "tab": {"id": "t1", "spaceId": "s1", "title": "", "url": "", "favicon": null, "isLoading": false, "isPinned": false, "isFavorite": false, "isMuted": false, "isPlayingAudio": false, "lifecycleState": "active", "children": [], "createdAt": "2024-01-01T00:00:00Z", "lastActiveAt": "2024-01-01T00:00:00Z"}}
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(CoreUpdate.self, from: json)
        if case .tabCreated = decoded {} else { XCTFail("Expected tabCreated") }
    }

    func testCoreUpdateSpaceDeletedDecodesWithKind() throws {
        let json = """
        {"kind": "space_deleted", "space_id": "space-1"}
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(CoreUpdate.self, from: json)
        if case .spaceDeleted = decoded {} else { XCTFail("Expected spaceDeleted") }
    }

    // MARK: - ATCAction does NOT use kind (externally tagged)

    func testATCActionCloseDoesNotUseKind() throws {
        let action = ATCAction.close
        let jsonString = try encodeToString(action)
        XCTAssertTrue(jsonString.contains("\"close\""))
        XCTAssertFalse(jsonString.contains("\"kind\""))
    }

    func testATCActionRouteDoesNotUseKind() throws {
        let action = ATCAction.route(spaceId: "space-1")
        let json = try encodeToJson(action)
        XCTAssertNotNil(json["route"])
        XCTAssertNil(json["kind"])
    }

    // MARK: - DefaultLinkBehavior does NOT use kind (externally tagged)

    func testDefaultLinkBehaviorCurrentSpaceDoesNotUseKind() throws {
        let behavior = DefaultLinkBehavior.currentSpace
        let jsonString = try encodeToString(behavior)
        XCTAssertTrue(jsonString.contains("\"current_space\""))
        XCTAssertFalse(jsonString.contains("\"kind\""))
    }

    func testDefaultLinkBehaviorSpecificSpaceDoesNotUseKind() throws {
        let behavior = DefaultLinkBehavior.specificSpace(spaceId: "space-1")
        let json = try encodeToJson(behavior)
        XCTAssertNotNil(json["specific_space"])
        XCTAssertNil(json["kind"])
    }

    // MARK: - Helpers

    private func encodeToJson(_ value: some Encodable) throws -> [String: Any] {
        let data = try MahoJSON.encoder.encode(value)
        return try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    private func encodeToString(_ value: some Encodable) throws -> String {
        let data = try MahoJSON.encoder.encode(value)
        return try XCTUnwrap(String(data: data, encoding: .utf8))
    }
}
