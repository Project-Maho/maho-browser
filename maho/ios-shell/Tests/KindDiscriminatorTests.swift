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
        let event = ShellEvent.createTab(spaceId: "space-1", url: nil, parentId: nil, isPrivate: false)
        let json = try encodeToJson(event)
        XCTAssertEqual(json["kind"] as? String, "create_tab")
    }

    // MARK: - CoreUpdate uses kind (verified via decode from hand-crafted JSON)

    func testCoreUpdateTabCreatedDecodesWithKind() throws {
        let json = """
        {"kind": "tab_created", "tab": {"id": "t1", "spaceId": "s1", "title": "", "url": "", "favicon": null, "isLoading": false, "isPinned": false, "isFavorite": false, "isMuted": false, "isPlayingAudio": false, "lifecycleState": "active", "children": [], "createdAt": "2024-01-01T00:00:00Z", "lastActiveAt": "2024-01-01T00:00:00Z", "role": {"type": "normal"}, "isPrivate": false}}
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

final class AgenticBrowsingBridgeTests: XCTestCase {
    func testBrowserToolInvokePreservesAgenticBrowsingResultFormats() throws {
        let controller = WebViewBridgeController(
            bridge: .shared,
            browserToolExecutor: BrowserToolExecutor { name, args in
                switch name {
                case "get_page_elements":
                    return [
                        "ok": true,
                        "result": [
                            "url": "https://example.com/form",
                            "title": "Example form",
                            "viewport": ["width": 390, "height": 844, "scrollX": 0, "scrollY": 120],
                            "elements": [[
                                "id": 1,
                                "tag": "button",
                                "label": "Continue",
                                "selector": #"[data-maho-agent-ref="1"]"#,
                                "bounds": ["x": 12.0, "y": 20.0, "width": 80.0, "height": 44.0],
                            ]],
                        ],
                    ]
                case "click_element":
                    XCTAssertEqual(args["id"] as? Int, 1)
                    return ["ok": true, "result": ["id": 1, "selector": NSNull(), "clicked": true]]
                case "fill_input":
                    XCTAssertEqual(args["selector"] as? String, "#email")
                    XCTAssertEqual(args["text"] as? String, "hello@example.com")
                    return ["ok": true, "result": ["id": NSNull(), "selector": "#email", "filled": true, "length": 17]]
                case "scroll_page":
                    XCTAssertEqual(args["direction"] as? String, "down")
                    XCTAssertEqual(args["amount"] as? Int, 320)
                    return [
                        "ok": true,
                        "result": [
                            "direction": "down",
                            "amount": 320.0,
                            "from": ["x": 0.0, "y": 100.0],
                            "target": ["x": 0.0, "y": 420.0],
                        ],
                    ]
                default:
                    return ["ok": false, "error": "unexpected_tool"]
                }
            }
        )

        let snapshot = try rpcResult(
            controller,
            #"{"id":"snapshot","method":"browserToolInvoke","params":{"name":"get_page_elements","args":{}}}"#
        )
        XCTAssertEqual(snapshot["ok"] as? Bool, true)
        let snapshotPayload = try XCTUnwrap(snapshot["result"] as? [String: Any])
        XCTAssertEqual(snapshotPayload["url"] as? String, "https://example.com/form")
        XCTAssertEqual((snapshotPayload["viewport"] as? [String: Any])?["scrollY"] as? Int, 120)
        let elements = try XCTUnwrap(snapshotPayload["elements"] as? [[String: Any]])
        XCTAssertEqual(elements.first?["id"] as? Int, 1)
        XCTAssertEqual(elements.first?["label"] as? String, "Continue")

        let click = try rpcResult(
            controller,
            #"{"id":"click","method":"browserToolInvoke","params":{"name":"click_element","args":{"id":1}}}"#
        )
        XCTAssertEqual((click["result"] as? [String: Any])?["clicked"] as? Bool, true)

        let fill = try rpcResult(
            controller,
            ##"{"id":"fill","method":"browserToolInvoke","params":{"name":"fill_input","args":{"selector":"#email","text":"hello@example.com"}}}"##
        )
        let fillPayload = try XCTUnwrap(fill["result"] as? [String: Any])
        XCTAssertEqual(fillPayload["filled"] as? Bool, true)
        XCTAssertEqual(fillPayload["length"] as? Int, 17)
        XCTAssertNil(fillPayload["text"], "fill result must not echo typed text")

        let scroll = try rpcResult(
            controller,
            #"{"id":"scroll","method":"browserToolInvoke","params":{"name":"scroll_page","args":{"direction":"down","amount":320}}}"#
        )
        XCTAssertEqual((scroll["result"] as? [String: Any])?["direction"] as? String, "down")
        XCTAssertEqual(((scroll["result"] as? [String: Any])?["target"] as? [String: Any])?["y"] as? Double, 420.0)
    }

    func testAgenticBrowsingEvaluatorMapsErrorsAndInjectsRequiredEvents() {
        let click = AgenticBrowsingDOM.invoke(name: "click_element", args: ["id": 2]) { script in
            XCTAssertTrue(script.contains("data-maho-agent-ref"))
            XCTAssertTrue(script.contains("touchstart"))
            XCTAssertTrue(script.contains("PointerEvent('pointerdown'"))
            XCTAssertTrue(script.contains("MouseEvent('mousedown'"))
            return .failure(code: "page_script_timeout")
        }
        XCTAssertEqual(click["ok"] as? Bool, false)
        XCTAssertEqual(click["error"] as? String, "page_script_timeout")

        let fill = AgenticBrowsingDOM.invoke(
            name: "fill_input",
            args: ["selector": "#name", "text": "Ada"]
        ) { script in
            XCTAssertTrue(script.contains("InputEvent('input'"))
            XCTAssertTrue(script.contains("new Event('change'"))
            return .success(##"{"ok":true,"result":{"id":null,"selector":"#name","filled":true,"length":3}}"##)
        }
        XCTAssertEqual(fill["ok"] as? Bool, true)
        XCTAssertEqual((fill["result"] as? [String: Any])?["length"] as? Int, 3)

        let scroll = AgenticBrowsingDOM.invoke(name: "scroll_page", args: ["direction": "down"]) { script in
            XCTAssertTrue(script.contains("behavior: 'smooth'"))
            return .success(#"{"ok":true,"result":{"direction":"down","amount":600,"from":{"x":0,"y":0},"target":{"x":0,"y":600}}}"#)
        }
        XCTAssertEqual((scroll["result"] as? [String: Any])?["direction"] as? String, "down")
    }

    func testAgenticBrowsingToolDescriptorsSeparateReadsFromMutations() throws {
        let byName = Dictionary(uniqueKeysWithValues: AgenticBrowsingDOM.toolDescriptors.compactMap { descriptor -> (String, [String: Any])? in
            guard let name = descriptor["name"] as? String,
                  let policy = descriptor["policy"] as? [String: Any] else { return nil }
            return (name, policy)
        })

        XCTAssertEqual(byName["get_page_elements"]?["permission"] as? String, "auto_approve")
        XCTAssertEqual(byName["get_page_snapshot"]?["permission"] as? String, "auto_approve")
        XCTAssertEqual(byName["scroll_page"]?["permission"] as? String, "auto_approve")
        XCTAssertEqual(byName["click_element"]?["permission"] as? String, "always_ask")
        XCTAssertEqual(byName["fill_input"]?["permission"] as? String, "always_ask")
        XCTAssertEqual(byName["click_element"]?["sensitive"] as? Bool, true)
        XCTAssertEqual(byName["fill_input"]?["sensitive"] as? Bool, true)
    }

    private func rpcResult(_ controller: WebViewBridgeController, _ request: String) throws -> [String: Any] {
        let data = try XCTUnwrap(controller.dispatchRPCForTesting(request).data(using: .utf8))
        let response = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        return try XCTUnwrap(response["result"] as? [String: Any])
    }
}
