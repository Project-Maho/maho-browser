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
        let original = ShellEvent.createTab(spaceId: "space-1", url: Url("https://maho.dev"), parentId: "tab-parent", isPrivate: false)
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

    func testShellEventArchiveTabByIdUsesCoreWireContract() throws {
        let event = ShellEvent.archiveTabById(tabId: "tab-archive")
        let data = try MahoJSON.encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["kind"] as? String, "archive_tab_by_id")
        XCTAssertEqual(json["tab_id"] as? String, "tab-archive")
        XCTAssertNil(json["type"])
        XCTAssertNil(json["tabId"])

        let decoded = try JSONDecoder().decode(ShellEvent.self, from: data)
        let decodedJson = try jsonDict(from: decoded)
        XCTAssertEqual(decodedJson["kind"] as? String, "archive_tab_by_id")
        XCTAssertEqual(decodedJson["tab_id"] as? String, "tab-archive")
    }

    func testShellEventRestoreArchivedTabUsesCoreWireContract() throws {
        let event = ShellEvent.restoreArchivedTab(tabId: "tab-restore")
        let data = try MahoJSON.encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["kind"] as? String, "restore_archived_tab")
        XCTAssertEqual(json["tab_id"] as? String, "tab-restore")
        XCTAssertNil(json["type"])
        XCTAssertNil(json["tabId"])
    }

    func testDomainMonogramColorDoesNotOverflowForLongHost() {
        let host = String(repeating: "a", count: 48) + ".example"

        XCTAssertNoThrow(DomainMonogram.color(for: host))
    }

    func testArchivedTabViewModelDecodesCoreResponseShape() throws {
        let json = """
        {
            "id": "tab-archived",
            "spaceId": "space-1",
            "title": "Archived Fixture",
            "url": "https://example.invalid/archive",
            "favicon": null,
            "archivedAt": "2026-08-09T01:00:00Z"
        }
        """.data(using: .utf8)!

        let archived = try JSONDecoder().decode(ArchivedTabViewModel.self, from: json)
        XCTAssertEqual(archived.id, "tab-archived")
        XCTAssertEqual(archived.spaceId, "space-1")
        XCTAssertEqual(archived.title, "Archived Fixture")
        XCTAssertEqual(archived.url, "https://example.invalid/archive")
        XCTAssertNil(archived.favicon)
        XCTAssertEqual(archived.archivedAt, "2026-08-09T01:00:00Z")
        XCTAssertThrowsError(try JSONDecoder().decode(TabViewModel.self, from: json))
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
                "lastActiveAt": "2024-01-01T00:00:00Z",
                "role": {"type": "normal"},
                "isPrivate": false
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

    // MARK: - PinnedCloseBehavior round-trip

    func testPinnedCloseBehaviorRoundTripSwitch() throws {
        let encoded = try JSONEncoder().encode(PinnedCloseBehavior.switch)
        let decoded = try JSONDecoder().decode(PinnedCloseBehavior.self, from: encoded)
        XCTAssertEqual(decoded, .switch)
        let wire = try XCTUnwrap(String(data: encoded, encoding: .utf8))
        XCTAssertEqual(wire, "\"switch\"")
    }

    func testPinnedCloseBehaviorRoundTripReset() throws {
        let encoded = try JSONEncoder().encode(PinnedCloseBehavior.reset)
        let decoded = try JSONDecoder().decode(PinnedCloseBehavior.self, from: encoded)
        XCTAssertEqual(decoded, .reset)
        let wire = try XCTUnwrap(String(data: encoded, encoding: .utf8))
        XCTAssertEqual(wire, "\"reset\"")
    }

    func testPinnedCloseBehaviorRoundTripResetSwitch() throws {
        let encoded = try JSONEncoder().encode(PinnedCloseBehavior.resetSwitch)
        let decoded = try JSONDecoder().decode(PinnedCloseBehavior.self, from: encoded)
        XCTAssertEqual(decoded, .resetSwitch)
        let wire = try XCTUnwrap(String(data: encoded, encoding: .utf8))
        XCTAssertEqual(wire, "\"reset-switch\"")
    }

    func testPinnedCloseBehaviorRoundTripUnloadSwitch() throws {
        let encoded = try JSONEncoder().encode(PinnedCloseBehavior.unloadSwitch)
        let decoded = try JSONDecoder().decode(PinnedCloseBehavior.self, from: encoded)
        XCTAssertEqual(decoded, .unloadSwitch)
        let wire = try XCTUnwrap(String(data: encoded, encoding: .utf8))
        XCTAssertEqual(wire, "\"unload-switch\"")
    }

    func testPinnedCloseBehaviorRoundTripResetUnloadSwitch() throws {
        let encoded = try JSONEncoder().encode(PinnedCloseBehavior.resetUnloadSwitch)
        let decoded = try JSONDecoder().decode(PinnedCloseBehavior.self, from: encoded)
        XCTAssertEqual(decoded, .resetUnloadSwitch)
        let wire = try XCTUnwrap(String(data: encoded, encoding: .utf8))
        XCTAssertEqual(wire, "\"reset-unload-switch\"")
    }

    func testPinnedCloseBehaviorRoundTripClose() throws {
        let encoded = try JSONEncoder().encode(PinnedCloseBehavior.close)
        let decoded = try JSONDecoder().decode(PinnedCloseBehavior.self, from: encoded)
        XCTAssertEqual(decoded, .close)
        let wire = try XCTUnwrap(String(data: encoded, encoding: .utf8))
        XCTAssertEqual(wire, "\"close\"")
    }

    func testGeneralSettingsPinnedCloseBehaviorDefaultsToSwitch() throws {
        let json = """
        {
            "defaultSearchEngine": {"name": "Google", "urlTemplate": "https://google.com?q={query}", "isDefault": true},
            "todayTabTimeoutHours": 24.0,
            "restoreOnLaunch": "restore_all",
            "downloadPath": "/Downloads",
            "autoplayPolicy": "allow"
        }
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(GeneralSettings.self, from: json)
        XCTAssertEqual(decoded.archiveTimeoutHours, 24.0)
        XCTAssertTrue(decoded.siteSearchEntries.isEmpty)
        XCTAssertEqual(decoded.pinnedCloseBehavior, .switch)
    }

    func testGeneralSettingsPinnedCloseBehaviorDecodesExplicitValue() throws {
        let json = """
        {
            "defaultSearchEngine": {"name": "Google", "urlTemplate": "https://google.com?q={query}", "isDefault": true},
            "todayTabTimeoutHours": 24.0,
            "restoreOnLaunch": "restore_all",
            "downloadPath": "/Downloads",
            "autoplayPolicy": "allow",
            "pinnedCloseBehavior": "reset-unload-switch"
        }
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(GeneralSettings.self, from: json)
        XCTAssertEqual(decoded.pinnedCloseBehavior, .resetUnloadSwitch)
    }

    func testGeneralSettingsUpdatePinnedCloseBehaviorOptional() throws {
        let json = "{}".data(using: .utf8)!
        let decoded = try JSONDecoder().decode(GeneralSettingsUpdate.self, from: json)
        XCTAssertNil(decoded.pinnedCloseBehavior)
    }

    func testGeneralSettingsUpdatePinnedCloseBehaviorRoundTrip() throws {
        var update = GeneralSettingsUpdate()
        update.pinnedCloseBehavior = .close
        let encoded = try JSONEncoder().encode(update)
        let decoded = try JSONDecoder().decode(GeneralSettingsUpdate.self, from: encoded)
        XCTAssertEqual(decoded.pinnedCloseBehavior, .close)
    }

    // MARK: - Helpers

    private func jsonDict(from value: some Encodable) throws -> [String: Any] {
        let data = try MahoJSON.encoder.encode(value)
        return try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    }
}
