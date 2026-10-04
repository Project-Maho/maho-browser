import XCTest

@testable import Maho

final class BridgeTabsTests: XCTestCase {

    private let encoder = JSONEncoder()

    // MARK: - createTab sends correct ShellEvent

    func testCreateTabEventEncoding() throws {
        let event = ShellEvent.createTab(spaceId: "space-1", url: Url("https://example.com"), parentId: "tab-parent", isPrivate: false)
        let data = try encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["kind"] as? String, "create_tab")
        XCTAssertEqual(json["space_id"] as? String, "space-1")
        XCTAssertEqual(json["url"] as? String, "https://example.com")
        XCTAssertEqual(json["parent_id"] as? String, "tab-parent")
    }

    func testCreateTabEventWithoutOptionalFields() throws {
        let event = ShellEvent.createTab(spaceId: "space-2", url: nil, parentId: nil, isPrivate: false)
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
            "role": {"type": "pinned"},
            "isPrivate": false
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
            role: .favorite(order: 1),
            isPrivate: false
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

    // MARK: - Address-bar prefill and active-tab submission regression tests

    func testHomeOriginDoesNotPrefillActiveUrl() {
        let prefill = MainBrowserPresentation.searchPrefillQuery(for: .home, currentUrl: "https://example.com/page")
        XCTAssertEqual(prefill, "", "Home/new-tab search presentation must remain empty")
    }

    func testBrowsingOriginPrefillsActiveUrl() {
        let prefill = MainBrowserPresentation.searchPrefillQuery(for: .browsing("tab-1"), currentUrl: "https://example.com/page")
        XCTAssertEqual(prefill, "https://example.com/page", "Browsing address-bar presentation must prefill the active tab URL")
    }

    func testBrowsingOriginWithBlankUrlDoesNotPrefill() {
        let prefillBlank = MainBrowserPresentation.searchPrefillQuery(for: .browsing("tab-1"), currentUrl: "about:blank")
        XCTAssertEqual(prefillBlank, "", "Blank URL should not be prefilled")

        let prefillEmpty = MainBrowserPresentation.searchPrefillQuery(for: .browsing("tab-1"), currentUrl: "")
        XCTAssertEqual(prefillEmpty, "", "Empty URL should not be prefilled")
    }

    func testBrowsingOriginSubmissionReusesActiveTab() {
        let plan = MainBrowserPresentation.searchSubmissionPlan(
            for: .browsing("tab-1"),
            query: "example.org",
            activeTabId: "tab-1"
        )
        XCTAssertEqual(
            plan,
            .reuseActiveTab(tabId: "tab-1", url: "https://example.org"),
            "Browsing-origin submission must reuse the active tab rather than create a duplicate tab"
        )
    }

    func testHomeOriginSubmissionCreatesNewTab() {
        let plan = MainBrowserPresentation.searchSubmissionPlan(
            for: .home,
            query: "example.org",
            activeTabId: "tab-1"
        )
        XCTAssertEqual(
            plan,
            .createNewTab(url: "https://example.org"),
            "Home-origin submission must create a new tab"
        )
    }

    func testBrowsingOriginSubmissionNormalizesSearchTermsAndReusesActiveTab() {
        let plan = MainBrowserPresentation.searchSubmissionPlan(
            for: .browsing("tab-42"),
            query: "swiftui navigation",
            activeTabId: "tab-42"
        )
        XCTAssertEqual(
            plan,
            .reuseActiveTab(tabId: "tab-42", url: "https://www.google.com/search?q=swiftui%20navigation"),
            "Search queries from browsing origin must normalize to search URL and reuse the active tab"
        )
    }

    func testBrowsingOriginWithChangedActiveTabTargetsCurrentActiveTab() {
        let plan = MainBrowserPresentation.searchSubmissionPlan(
            for: .browsing("tab-normal-1"),
            query: "example.org",
            activeTabId: "tab-private-1"
        )
        XCTAssertEqual(
            plan,
            .reuseActiveTab(tabId: "tab-private-1", url: "https://example.org"),
            "Browsing origin whose active tab changed (e.g. private overlay transition) must target current active tab rather than stale captured tab"
        )
    }

    func testBrowsingOriginWithNoActiveTabCreatesNewTab() {
        let plan = MainBrowserPresentation.searchSubmissionPlan(
            for: .browsing("tab-normal-1"),
            query: "example.org",
            activeTabId: nil
        )
        XCTAssertEqual(
            plan,
            .createNewTab(url: "https://example.org"),
            "Browsing origin with no remaining active tab must create a new tab instead of reusing stale captured tab"
        )
    }

    func testNormalizedUrlHandling() {
        XCTAssertEqual(MainBrowserPresentation.normalizedURL(from: "http://localhost:3000"), "http://localhost:3000")
        XCTAssertEqual(MainBrowserPresentation.normalizedURL(from: "localhost:8080"), "http://localhost:8080")
        XCTAssertEqual(MainBrowserPresentation.normalizedURL(from: "127.0.0.1:8080"), "http://127.0.0.1:8080")
        XCTAssertEqual(MainBrowserPresentation.normalizedURL(from: "https://example.com"), "https://example.com")
        XCTAssertEqual(MainBrowserPresentation.normalizedURL(from: "apple.com"), "https://apple.com")
        XCTAssertEqual(MainBrowserPresentation.normalizedURL(from: "swiftui navigation"), "https://www.google.com/search?q=swiftui%20navigation")
    }
}
