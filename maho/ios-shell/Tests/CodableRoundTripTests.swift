import XCTest
@testable import Maho

final class CodableRoundTripTests: XCTestCase {

    // MARK: - ShellEvent round-trips (encode → decode)

    func testShellEventNavigateToRoundTrip() throws {
        let original = ShellEvent.navigateTo(tabId: "tab-1", url: Url("https://example.com"))
        let data = try MahoJSON.encoder.encode(original)
        let decoded = try JSONDecoder().decode(ShellEvent.self, from: data)
        let originalJson = try jsonDict(from: original)
        let decodedJson = try jsonDict(from: decoded)
        XCTAssertEqual(originalJson["kind"] as? String, decodedJson["kind"] as? String)
        XCTAssertEqual(originalJson["tab_id"] as? String, decodedJson["tab_id"] as? String)
    }

    func testShellEventCreateTabRoundTrip() throws {
        let original = ShellEvent.createTab(spaceId: "space-1", url: Url("https://maho.dev"), parentId: "tab-parent")
        let data = try MahoJSON.encoder.encode(original)
        let decoded = try JSONDecoder().decode(ShellEvent.self, from: data)
        let originalJson = try jsonDict(from: original)
        let decodedJson = try jsonDict(from: decoded)
        XCTAssertEqual(originalJson["kind"] as? String, decodedJson["kind"] as? String)
        XCTAssertEqual(originalJson["space_id"] as? String, decodedJson["space_id"] as? String)
    }

    func testShellEventCloseTabRoundTrip() throws {
        let original = ShellEvent.closeTab(tabId: "tab-2")
        let data = try MahoJSON.encoder.encode(original)
        let decoded = try JSONDecoder().decode(ShellEvent.self, from: data)
        let originalJson = try jsonDict(from: original)
        let decodedJson = try jsonDict(from: decoded)
        XCTAssertEqual(originalJson["kind"] as? String, decodedJson["kind"] as? String)
    }

    func testShellEventCreateSpaceRoundTrip() throws {
        let color = SpaceColor(hue: 0.5, saturation: 0.8, brightness: 0.9)
        let original = ShellEvent.createSpace(name: "Work", color: color, profileId: "profile-1")
        let data = try MahoJSON.encoder.encode(original)
        let decoded = try JSONDecoder().decode(ShellEvent.self, from: data)
        let originalJson = try jsonDict(from: original)
        let decodedJson = try jsonDict(from: decoded)
        XCTAssertEqual(originalJson["kind"] as? String, decodedJson["kind"] as? String)
        XCTAssertEqual(originalJson["name"] as? String, decodedJson["name"] as? String)
    }

    func testShellEventReorderTabRoundTrip() throws {
        let original = ShellEvent.reorderTab(tabId: "tab-3", beforeTabId: "tab-5")
        let data = try MahoJSON.encoder.encode(original)
        let decoded = try JSONDecoder().decode(ShellEvent.self, from: data)
        let originalJson = try jsonDict(from: original)
        let decodedJson = try jsonDict(from: decoded)
        XCTAssertEqual(originalJson["kind"] as? String, decodedJson["kind"] as? String)
        XCTAssertEqual(originalJson["before_tab_id"] as? String, decodedJson["before_tab_id"] as? String)
    }

    func testShellEventEncodedJsonContainsKind() throws {
        let event = ShellEvent.appLaunched
        let data = try MahoJSON.encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(json["kind"] as? String, "app_launched")
    }

    // MARK: - CoreUpdate decode-only (CoreUpdate.encode is a stub; test decode from hand-crafted JSON)

    func testCoreUpdateTabCreatedDecodeFromJson() throws {
        let json = """
        {
            "kind": "tab_created",
            "tab": {
                "id": "tab-1",
                "spaceId": "space-1",
                "title": "Example",
                "url": "https://example.com",
                "favicon": null,
                "isLoading": false,
                "isPinned": false,
                "isFavorite": false,
                "isMuted": false,
                "isPlayingAudio": false,
                "lifecycleState": "active",
                "children": [],
                "createdAt": "2024-01-01T00:00:00Z",
                "lastActiveAt": "2024-01-01T00:00:00Z"
            }
        }
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(CoreUpdate.self, from: json)
        if case .tabCreated(let tab) = decoded {
            XCTAssertEqual(tab.id, "tab-1")
            XCTAssertEqual(tab.spaceId, "space-1")
            XCTAssertEqual(tab.title, "Example")
        } else {
            XCTFail("Expected tabCreated, got \(decoded)")
        }
    }

    func testCoreUpdateTabClosedDecodeFromJson() throws {
        let json = """
        {"kind": "tab_closed", "tab_id": "tab-2", "animated": true}
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(CoreUpdate.self, from: json)
        if case .tabClosed(let tabId, let animated) = decoded {
            XCTAssertEqual(tabId, "tab-2")
            XCTAssertTrue(animated)
        } else {
            XCTFail("Expected tabClosed, got \(decoded)")
        }
    }

    func testCoreUpdateNavigationStateChangedDecodeFromJson() throws {
        let json = """
        {
            "kind": "navigation_state_changed",
            "tab_id": "tab-1",
            "url": "https://example.com",
            "title": "Example",
            "can_go_back": true,
            "can_go_forward": false,
            "is_loading": true,
            "progress": 0.5
        }
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(CoreUpdate.self, from: json)
        if case .navigationStateChanged(let tabId, _, let title, let canGoBack, _, let isLoading, _) = decoded {
            XCTAssertEqual(tabId, "tab-1")
            XCTAssertEqual(title, "Example")
            XCTAssertTrue(canGoBack)
            XCTAssertTrue(isLoading)
        } else {
            XCTFail("Expected navigationStateChanged, got \(decoded)")
        }
    }

    func testCoreUpdateSpaceCreatedDecodeFromJson() throws {
        let json = """
        {
            "kind": "space_created",
            "space": {
                "id": "space-1",
                "name": "Personal",
                "color": {"hue": 0.3, "saturation": 0.7, "brightness": 0.8},
                "tabCount": 3,
                "isActive": true,
                "icon": null
            }
        }
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(CoreUpdate.self, from: json)
        if case .spaceCreated(let space) = decoded {
            XCTAssertEqual(space.id, "space-1")
            XCTAssertEqual(space.name, "Personal")
        } else {
            XCTFail("Expected spaceCreated, got \(decoded)")
        }
    }

    func testCoreUpdateAllNotificationsDismissedDecodeFromJson() throws {
        let json = """
        {"kind": "all_notifications_dismissed"}
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(CoreUpdate.self, from: json)
        if case .allNotificationsDismissed = decoded {
        } else {
            XCTFail("Expected allNotificationsDismissed, got \(decoded)")
        }
    }

    // MARK: - Helpers

    private func jsonDict(from value: some Encodable) throws -> [String: Any] {
        let data = try MahoJSON.encoder.encode(value)
        return try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    }
}
