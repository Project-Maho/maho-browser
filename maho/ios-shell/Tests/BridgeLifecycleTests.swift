import XCTest

@testable import Maho

final class BridgeLifecycleTests: XCTestCase {

    // MARK: - Lifecycle ShellEvent serialization (FFI-free)

    func testAppLaunchedEncoding() throws {
        let event = ShellEvent.appLaunched
        let data = try MahoJSON.encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(json["kind"] as? String, "app_launched")
    }

    func testAppWillTerminateEncoding() throws {
        let event = ShellEvent.appWillTerminate
        let data = try MahoJSON.encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(json["kind"] as? String, "app_will_terminate")
    }

    // MARK: - Lifecycle CoreUpdate decoding (from hand-crafted JSON)

    func testMemoryWarningEncoding() throws {
        let event = ShellEvent.memoryWarning(level: .critical)
        let data = try MahoJSON.encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(json["kind"] as? String, "memory_warning")
        XCTAssertEqual(json["level"] as? String, "critical")
    }

    func testAllNotificationsDismissedDecode() throws {
        let json = """
        {"kind": "all_notifications_dismissed"}
        """.data(using: .utf8)!
        let decoded = try JSONDecoder().decode(CoreUpdate.self, from: json)
        if case .allNotificationsDismissed = decoded {} else { XCTFail("Expected allNotificationsDismissed") }
    }

    // MARK: - Lifecycle events round-trip (encode ShellEvent, verify structure)

    func testLifecycleEventsAreParameterless() throws {
        let events: [ShellEvent] = [.appLaunched, .appWillTerminate]
        for event in events {
            let data = try MahoJSON.encoder.encode(event)
            let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
            XCTAssertNotNil(json["kind"], "Lifecycle event should have kind field")
            XCTAssertEqual(json.count, 1, "Lifecycle event should only have kind field")
        }
    }

    // MARK: - Handoff URL safety

    @MainActor
    func testHandoffManagerRejectsNonHttpSchemes() {
        let invalidURLs = [
            URL(string: "ftp://example.com")!,
            URL(string: "mailto:hello@example.com")!,
            URL(string: "file:///tmp/example.html")!
        ]

        for url in invalidURLs {
            XCTAssertNil(HandoffManager.shared.makeActivity(with: url), "Expected Handoff to reject \(url.absoluteString)")
        }
    }

    @MainActor
    func testHandoffManagerAcceptsHttpAndHttpsURLs() throws {
        let urls = [
            URL(string: "http://example.com")!,
            URL(string: "https://example.com/path?query=1")!
        ]

        for url in urls {
            let activity = try XCTUnwrap(HandoffManager.shared.makeActivity(with: url))
            XCTAssertEqual(activity.activityType, HandoffManager.activityType)
            XCTAssertEqual(activity.webpageURL, url)
            XCTAssertEqual(activity.title, url.absoluteString)
            XCTAssertTrue(activity.isEligibleForHandoff)
            XCTAssertFalse(activity.isEligibleForSearch)
            XCTAssertFalse(activity.isEligibleForPublicIndexing)
        }
    }

    // MARK: - Home / browsing shell route assumptions

    func testShellRouteHomeIsNotBrowsing() {
        let route = MainBrowserView.ShellRoute.home

        XCTAssertFalse(route.isBrowsing)
        XCTAssertNil(route.browsingTabId)
    }

    func testShellRouteBrowsingCarriesTabIdentity() {
        let route = MainBrowserView.ShellRoute.browsing("tab-123")

        XCTAssertTrue(route.isBrowsing)
        XCTAssertEqual(route.browsingTabId, "tab-123")
    }
}
