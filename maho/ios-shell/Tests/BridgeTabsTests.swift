import XCTest

@testable import Maho

final class BridgeTabsTests: XCTestCase {

    private let encoder = JSONEncoder()

    // MARK: - createTab sends correct ShellEvent

    func testCreateTabEventEncoding() throws {
        let event = ShellEvent.createTab(spaceId: "space-1", url: Url("https://example.com"), parentId: "tab-parent")
        let data = try encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["kind"] as? String, "create_tab")
        XCTAssertEqual(json["space_id"] as? String, "space-1")
        XCTAssertEqual(json["url"] as? String, "https://example.com")
        XCTAssertEqual(json["parent_id"] as? String, "tab-parent")
    }

    func testCreateTabEventWithoutOptionalFields() throws {
        let event = ShellEvent.createTab(spaceId: "space-2", url: nil, parentId: nil)
        let data = try encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["kind"] as? String, "create_tab")
        XCTAssertEqual(json["space_id"] as? String, "space-2")
        XCTAssertNil(json["url"])
        XCTAssertNil(json["parent_id"])
    }

    // MARK: - closeTab sends correct ShellEvent

    func testCloseTabEventEncoding() throws {
        let event = ShellEvent.closeTab(tabId: "tab-1")
        let data = try encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["kind"] as? String, "close_tab")
        XCTAssertEqual(json["tab_id"] as? String, "tab-1")
    }

    // MARK: - TabViewModel decoding

    func testTabViewModelDecoding() throws {
        let json = """
        {
            "id": "tab-1",
            "spaceId": "space-1",
            "title": "Maho Browser",
            "url": "https://maho.dev",
            "isLoading": false,
            "isPinned": true,
            "isFavorite": false,
            "isMuted": false,
            "isPlayingAudio": false,
            "lifecycleState": "active",
            "children": [],
            "createdAt": "2024-01-01T00:00:00Z",
            "lastActiveAt": "2024-01-01T00:00:00Z",
            "role": {"type": "pinned"}
        }
        """
        let data = try XCTUnwrap(json.data(using: .utf8))
        let model = try JSONDecoder().decode(TabViewModel.self, from: data)

        XCTAssertEqual(model.id, "tab-1")
        XCTAssertEqual(model.spaceId, "space-1")
        XCTAssertEqual(model.title, "Maho Browser")
        XCTAssertEqual(model.url, "https://maho.dev")
        XCTAssertTrue(model.isPinned)
        XCTAssertFalse(model.isFavorite)
        XCTAssertEqual(model.lifecycleState, "active")
    }

    func testTabViewModelRoundTrip() throws {
        let original = TabViewModel(
            id: "tab-2",
            spaceId: "space-2",
            title: "Example",
            customTitle: nil,
            url: "https://example.com",
            favicon: nil,
            isLoading: true,
            isMuted: false,
            isPlayingAudio: true,
            lifecycleState: "frozen",
            children: ["tab-child"],
            createdAt: "2024-06-01T12:00:00Z",
            lastActiveAt: "2024-06-01T12:30:00Z",
            role: .favorite(order: 1)
        )

        let data = try encoder.encode(original)
        let decoded = try JSONDecoder().decode(TabViewModel.self, from: data)

        XCTAssertEqual(decoded.id, original.id)
        XCTAssertEqual(decoded.spaceId, original.spaceId)
        XCTAssertEqual(decoded.title, original.title)
        XCTAssertEqual(decoded.url, original.url)
        XCTAssertEqual(decoded.isLoading, original.isLoading)
        XCTAssertEqual(decoded.isPinned, original.isPinned)
        XCTAssertEqual(decoded.isFavorite, original.isFavorite)
        XCTAssertEqual(decoded.isMuted, original.isMuted)
        XCTAssertEqual(decoded.isPlayingAudio, original.isPlayingAudio)
        XCTAssertEqual(decoded.lifecycleState, original.lifecycleState)
        XCTAssertEqual(decoded.children, original.children)
    }
}
