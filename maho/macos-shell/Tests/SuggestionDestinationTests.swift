import Foundation

func testNavigationSuggestionUsesExecutionPayload() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .navigation,
        key: "nav:https://example.com",
        title: "Example",
        subtitle: "example.com",
        executionPayload: "https://example.com/actual"
    )
    assertEqual(result, "https://example.com/actual")
}

func testBookmarkSuggestionUsesExecutionPayload() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .bookmark,
        key: "bookmark:abc123",
        title: "My Bookmark",
        subtitle: "example.com",
        executionPayload: "https://bookmarked.example.com"
    )
    assertEqual(result, "https://bookmarked.example.com")
}

func testHistorySuggestionUsesExecutionPayload() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .history,
        key: "history:https://visited.com",
        title: "Visited Page",
        subtitle: nil,
        executionPayload: "https://visited.com/page"
    )
    assertEqual(result, "https://visited.com/page")
}

func testNavPrefixFallbackWorks() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .navigation,
        key: "nav:https://fallback.com",
        title: "Fallback",
        subtitle: nil,
        executionPayload: nil
    )
    assertEqual(result, "https://fallback.com")
}

func testHistoryPrefixFallbackWorks() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .history,
        key: "history:https://old.com",
        title: "Old Page",
        subtitle: nil,
        executionPayload: nil
    )
    assertEqual(result, "https://old.com")
}

func testSearchPrefixFallbackWorks() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .navigation,
        key: "search:swift tutorials",
        title: "swift tutorials",
        subtitle: nil,
        executionPayload: nil
    )
    assertEqual(result, "swift tutorials")
}

func testOpaqueKeyWithoutPayloadReturnsNil() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .bookmark,
        key: "bookmark:abc123",
        title: "My Bookmark",
        subtitle: "example.com",
        executionPayload: nil
    )
    assertTrue(result == nil, "opaque bookmark key without payload must not navigate")
}

func testSpaceKeyWithoutPayloadReturnsNil() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .navigation,
        key: "space:workspace-id-456",
        title: "My Workspace",
        subtitle: nil,
        executionPayload: nil
    )
    assertTrue(result == nil, "space: key without payload must not navigate")
}

func testTitleNeverUsedAsDestination() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .navigation,
        key: "opaque-key-no-prefix",
        title: "https://evil.com",
        subtitle: "https://also-evil.com",
        executionPayload: nil
    )
    assertTrue(result == nil, "title/subtitle must never become navigation target")
}

func testTabKindReturnsNil() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .tab,
        key: "tab-id",
        title: "Tab Title",
        subtitle: nil,
        executionPayload: "https://should-not-matter.com"
    )
    assertTrue(result == nil, "tab kind should return nil (handled via activateTab)")
}

func testArchivedTabKindReturnsNil() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .archivedTab,
        key: "archived-id",
        title: "Archived",
        subtitle: nil,
        executionPayload: "https://ignored.com"
    )
    assertTrue(result == nil)
}

func testEmptyPayloadFallsThrough() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .navigation,
        key: "nav:https://fallback.com",
        title: "Title",
        subtitle: nil,
        executionPayload: ""
    )
    assertEqual(result, "https://fallback.com")
}

func testEmptyPrefixValueReturnsNil() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .navigation,
        key: "nav:",
        title: "Empty Nav",
        subtitle: nil,
        executionPayload: nil
    )
    assertTrue(result == nil, "empty prefix value must return nil")
}

func testPayloadTakesPriorityOverSafePrefix() {
    let result = SuggestionDestinationResolver.resolve(
        kind: .navigation,
        key: "nav:https://key-url.com",
        title: "Title",
        subtitle: nil,
        executionPayload: "https://payload-url.com"
    )
    assertEqual(result, "https://payload-url.com")
}

@main
struct SuggestionDestinationTestRunner {
    static func main() {
        testNavigationSuggestionUsesExecutionPayload()
        testBookmarkSuggestionUsesExecutionPayload()
        testHistorySuggestionUsesExecutionPayload()
        testNavPrefixFallbackWorks()
        testHistoryPrefixFallbackWorks()
        testSearchPrefixFallbackWorks()
        testOpaqueKeyWithoutPayloadReturnsNil()
        testSpaceKeyWithoutPayloadReturnsNil()
        testTitleNeverUsedAsDestination()
        testTabKindReturnsNil()
        testArchivedTabKindReturnsNil()
        testEmptyPayloadFallsThrough()
        testEmptyPrefixValueReturnsNil()
        testPayloadTakesPriorityOverSafePrefix()
        print("ALL TESTS PASSED")
    }
}
