import UIKit
import XCTest
@testable import Maho

private final class PresentedViewControllerStub: UIViewController {
    var stubPresentedViewController: UIViewController?

    override var presentedViewController: UIViewController? {
        stubPresentedViewController
    }
}

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

    func testNativeProviderSettingsDriveWebViewSettingsAndChatStart() throws {
        let defaults = UserDefaults.standard
        let keys = Array(Set([
            AiProviderResolver.providerDefaultsKey,
            AiProviderResolver.baseURLDefaultsKey,
            AiProviderResolver.modelDefaultsKey,
            AiSettingsKeys.provider,
            AiSettingsKeys.baseUrl,
            AiSettingsKeys.model
        ]))
        let originalDefaults = Dictionary(uniqueKeysWithValues: keys.map { ($0, defaults.object(forKey: $0)) })
        let originalKey = BYOKKeychain.get(provider: AiProvider.openaiCompatible.rawValue)
        defer {
            for (key, value) in originalDefaults {
                if let value {
                    defaults.set(value, forKey: key)
                } else {
                    defaults.removeObject(forKey: key)
                }
            }
            if let originalKey {
                _ = BYOKKeychain.set(provider: AiProvider.openaiCompatible.rawValue, key: originalKey)
            } else {
                _ = BYOKKeychain.delete(provider: AiProvider.openaiCompatible.rawValue)
            }
        }

        keys.forEach(defaults.removeObject(forKey:))
        let nativeSettingsAccount = try XCTUnwrap(AiProvider.openaiCompatible.byokKeychainProvider)
        AiProviderResolver.setSelectedProvider(.openaiCompatible)
        AiProviderResolver.setBaseURL("https://fixture.example/v1")
        AiProviderResolver.setModel("test-model")
        XCTAssertTrue(MahoBridge.shared.byokSetKey(
            provider: nativeSettingsAccount,
            key: "fixture-secret"
        ))

        let ptr = makePtr()
        var createdConfig: ChatSessionRPCConfig?
        let controller = WebViewBridgeController(
            bridge: .shared,
            chatRPC: chatDependencies(
                ptr: ptr,
                createdConfig: { createdConfig = $0 },
                handle: "fixture-chat",
                resolveSecret: { BYOKKeychain.get(provider: $0) }
            )
        )

        let settingsResponse = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":40,"method":"getAiSettings","params":[]}"#
        ))
        let settings = try XCTUnwrap(settingsResponse["result"] as? [String: Any])
        XCTAssertEqual(settings["provider"] as? String, AiProvider.openaiCompatible.rawValue)
        XCTAssertEqual(settings["baseUrl"] as? String, "https://fixture.example/v1")
        XCTAssertEqual(settings["model"] as? String, "test-model")
        XCTAssertEqual(settings["hasApiKey"] as? Bool, true)

        let chatResponse = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":41,"method":"chatSessionStart","params":{"opts":{"endpoint":"https://fixture.example/v1/chat/completions","model":"test-model","credentialProvider":"openai-compatible"}}}"#
        ))
        XCTAssertEqual(chatResponse["result"] as? String, "fixture-chat")
        XCTAssertEqual(createdConfig, ChatSessionRPCConfig(
            apiKey: "fixture-secret",
            endpoint: "https://fixture.example/v1/chat/completions",
            model: "test-model",
            systemInstruction: ""
        ))
    }

    func testManagedChatBootstrapWithoutAccountReturnsTypedCredentialEnvelope() throws {
        let controller = WebViewBridgeController(
            bridge: .shared,
            chatRPC: chatDependencies(
                ptr: makePtr(),
                createdConfig: { _ in XCTFail("managed auth failure must not create a chat session") },
                handle: "unused"
            )
        )

        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":"managed","method":"chatSessionStart","params":{"credentialProvider":"maho-managed"}}"#
        ))
        let error = try XCTUnwrap(response["error"] as? [String: Any])
        XCTAssertEqual(error["kind"] as? String, "operation_failed")
        XCTAssertEqual(
            error["reason"] as? String,
            #"{"version":1,"kind":"credential_error","code":"managed_auth_unavailable"}"#
        )
    }

    func testConversationListRejectsInvalidParamsAsParamsInvalid() throws {
        let controller = WebViewBridgeController(
            bridge: .shared,
            conversationRPC: conversationDependencies(listPayload: { _, _ in
                XCTFail("invalid params must not reach native list")
                return "[]"
            })
        )
        let invalidRequests = [
            #"{"jsonrpc":"2.0","id":50,"method":"conversationList","params":{"state":"deleted","limit":100}}"#,
            #"{"jsonrpc":"2.0","id":51,"method":"conversationList","params":{"state":"active","limit":0}}"#,
            #"{"jsonrpc":"2.0","id":52,"method":"conversationList","params":{"state":"active","limit":501}}"#,
            #"{"jsonrpc":"2.0","id":53,"method":"conversationList","params":{"state":"active","limit":1.5}}"#,
            #"{"jsonrpc":"2.0","id":70,"method":"conversationList","params":{"state":7,"limit":100}}"#,
            #"{"jsonrpc":"2.0","id":71,"method":"conversationList","params":{"state":"active","limit":true}}"#
        ]

        for request in invalidRequests {
            XCTAssertEqual(try rpcErrorKind(controller, request), "params_invalid")
        }
    }

    func testConversationMutationParamsAreStrictlyValidated() throws {
        let controller = WebViewBridgeController(
            bridge: .shared,
            conversationRPC: conversationDependencies(
                archive: { _ in XCTFail("invalid id must not reach archive"); return true },
                unarchive: { _ in XCTFail("invalid id must not reach unarchive"); return true },
                bulk: { _, _ in XCTFail("invalid bulk request must not reach native"); return "{}" },
                setPolicy: { _ in XCTFail("invalid policy must not reach native"); return true }
            )
        )
        let invalidRequests = [
            #"{"jsonrpc":"2.0","id":54,"method":"conversationArchive","params":{"id":""}}"#,
            #"{"jsonrpc":"2.0","id":55,"method":"conversationUnarchive","params":{"id":"   "}}"#,
            #"{"jsonrpc":"2.0","id":56,"method":"conversationBulk","params":{"op":"move","ids":["one"]}}"#,
            #"{"jsonrpc":"2.0","id":57,"method":"conversationBulk","params":{"op":"archive","ids":[]}}"#,
            #"{"jsonrpc":"2.0","id":58,"method":"conversationBulk","params":{"op":"delete","ids":["one","one"]}}"#,
            #"{"jsonrpc":"2.0","id":59,"method":"conversationBulk","params":{"op":"unarchive","ids":[""]}}"#,
            #"{"jsonrpc":"2.0","id":60,"method":"conversationSetAutoArchivePolicy","params":{"afterDays":0}}"#,
            #"{"jsonrpc":"2.0","id":61,"method":"conversationSetAutoArchivePolicy","params":{"afterDays":3.5}}"#,
            #"{"jsonrpc":"2.0","id":62,"method":"conversationSetAutoArchivePolicy","params":{"afterDays":14}}"#,
            #"{"jsonrpc":"2.0","id":72,"method":"conversationProjectCreate","params":{"name":" "}}"#,
            #"{"jsonrpc":"2.0","id":73,"method":"conversationProjectRename","params":{"id":"","name":"Name"}}"#,
            #"{"jsonrpc":"2.0","id":74,"method":"conversationProjectDelete","params":{"id":7}}"#,
            #"{"jsonrpc":"2.0","id":75,"method":"conversationProjectMove","params":{"ids":[],"projectId":null}}"#,
            #"{"jsonrpc":"2.0","id":76,"method":"conversationProjectMove","params":{"ids":["one","one"],"projectId":"project"}}"#,
            #"{"jsonrpc":"2.0","id":77,"method":"conversationProjectMove","params":{"ids":["one"],"projectId":" "}}"#
        ]

        for request in invalidRequests {
            XCTAssertEqual(try rpcErrorKind(controller, request), "params_invalid")
        }
    }

    func testConversationListNativeFailureIsNotAValidEmptyList() throws {
        for payload in [nil, "not-json", "{}"] as [String?] {
            let controller = WebViewBridgeController(
                bridge: .shared,
                conversationRPC: conversationDependencies(listPayload: { _, _ in payload })
            )
            let response = try rpcObject(controller.dispatchRPCForTesting(
                #"{"jsonrpc":"2.0","id":63,"method":"conversationList","params":{"state":"archived","limit":100}}"#
            ))

            XCTAssertNil(response["result"])
            XCTAssertEqual((response["error"] as? [String: Any])?["kind"] as? String, "operation_failed")
        }
    }

    func testRelayAuthenticationRPCsActivateSessionBeforePolling() throws {
        let session = RelayAuthSession(
            accessToken: "access-token",
            refreshToken: "refresh-token",
            tokenType: "Bearer",
            expiresAt: Date(timeIntervalSince1970: 2_000_000_000),
            refreshExpiresAt: Date(timeIntervalSince1970: 2_100_000_000),
            account: RelayAccount(id: "account", email: "user@example.com", displayName: "User"),
            device: RelayDevice(id: "device", name: "Test iPhone", deviceType: "ios"),
            session: RelaySessionInfo(id: "session")
        )
        let lock = NSLock()
        var events: [String] = []
        func append(_ event: String) {
            lock.lock()
            events.append(event)
            lock.unlock()
        }
        func snapshot() -> [String] {
            lock.lock()
            defer { lock.unlock() }
            return events
        }
        let relayAuth = RelayAuthRPCDependencies(
            authenticate: { mode, _, _, displayName in
                let name: String
                switch mode {
                case .login:
                    name = "login"
                case .signup:
                    name = "signup"
                }
                append("authenticate:\(name):\(displayName ?? "nil")")
                return session
            },
            googleSignIn: {
                append("google")
                return session
            },
            activateSession: { activatedSession in
                XCTAssertEqual(activatedSession, session)
                append("activate")
            },
            startPolling: {
                append("poll")
            }
        )
        let controller = WebViewBridgeController(bridge: .shared, relayAuthRPC: relayAuth)

        let requests = [
            (#"{"jsonrpc":"2.0","id":81,"method":"relaySignIn","params":["user@example.com","password"]}"#, "authenticate:login:nil"),
            (#"{"jsonrpc":"2.0","id":82,"method":"relaySignUp","params":["user@example.com","password","User"]}"#, "authenticate:signup:User"),
            (#"{"jsonrpc":"2.0","id":83,"method":"relaySignInWithGoogle","params":[]}"#, "google"),
        ]

        for (request, authenticationEvent) in requests {
            lock.lock()
            events.removeAll()
            lock.unlock()
            let completed = expectation(description: "relay RPC completed")
            var responseJSON: String?
            DispatchQueue.global(qos: .userInitiated).async {
                responseJSON = controller.dispatchRPCForTesting(request)
                completed.fulfill()
            }
            wait(for: [completed], timeout: 2)
            let response = try rpcObject(try XCTUnwrap(responseJSON))
            XCTAssertEqual((response["result"] as? [String: Bool])?["ok"], true)
            XCTAssertEqual(snapshot(), [authenticationEvent, "activate", "poll"])
        }
    }

    func testConversationListPreservesLegitimateEmptyList() throws {
        var received: (String, Int)?
        let controller = WebViewBridgeController(
            bridge: .shared,
            conversationRPC: conversationDependencies(listPayload: { state, limit in
                received = (state, limit)
                return "[]"
            })
        )
        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":64,"method":"conversationList","params":{"state":"all","limit":500}}"#
        ))

        XCTAssertEqual(received?.0, "all")
        XCTAssertEqual(received?.1, 500)
        XCTAssertEqual((response["result"] as? [Any])?.count, 0)
        XCTAssertNil(response["error"])
    }

    func testConversationBulkMalformedNativePayloadIsOperationFailure() throws {
        for payload in [nil, "not-json", "[]"] as [String?] {
            let controller = WebViewBridgeController(
                bridge: .shared,
                conversationRPC: conversationDependencies(bulk: { _, _ in payload })
            )

            XCTAssertEqual(try rpcErrorKind(
                controller,
                #"{"jsonrpc":"2.0","id":65,"method":"conversationBulk","params":{"op":"delete","ids":["one"]}}"#
            ), "operation_failed")
        }
    }

    func testConversationValidMutationParamsReachNativeUnchanged() throws {
        var archivedId: String?
        var unarchivedId: String?
        var bulkRequest: (String, [String])?
        var policies: [Int32] = []
        let controller = WebViewBridgeController(
            bridge: .shared,
            conversationRPC: conversationDependencies(
                archive: { archivedId = $0; return true },
                unarchive: { unarchivedId = $0; return true },
                bulk: { bulkRequest = ($0, $1); return #"{"requestedCount":2,"affectedIds":[],"unchangedIds":[],"missingIds":[]}"# },
                setPolicy: { policies.append($0); return true }
            )
        )

        XCTAssertEqual(try rpcResult(controller, #"{"jsonrpc":"2.0","id":66,"method":"conversationArchive","params":{"id":"active-id"}}"#) as? Bool, true)
        XCTAssertEqual(try rpcResult(controller, #"{"jsonrpc":"2.0","id":67,"method":"conversationUnarchive","params":{"id":"archived-id"}}"#) as? Bool, true)
        _ = try rpcResult(controller, #"{"jsonrpc":"2.0","id":68,"method":"conversationBulk","params":{"op":"unarchive","ids":["one","two"]}}"#)
        for value in ["null", "-1", "3", "7", "30"] {
            _ = try rpcResult(controller, "{\"jsonrpc\":\"2.0\",\"id\":69,\"method\":\"conversationSetAutoArchivePolicy\",\"params\":{\"afterDays\":\(value)}}")
        }

        XCTAssertEqual(archivedId, "active-id")
        XCTAssertEqual(unarchivedId, "archived-id")
        XCTAssertEqual(bulkRequest?.0, "unarchive")
        XCTAssertEqual(bulkRequest?.1, ["one", "two"])
        XCTAssertEqual(policies, [-1, -1, 3, 7, 30])
    }

    func testConversationCreateReturnsRequestedIdentifier() throws {
        let identifier = "conversation-contract-\(UUID().uuidString)"
        defer {
            _ = MahoBridge.shared.deleteConversation(sessionId: identifier)
        }

        let controller = WebViewBridgeController(bridge: .shared)

        let response = try rpcObject(controller.dispatchRPCForTesting(
            """
            {"jsonrpc":"2.0","id":42,"method":"conversationCreate","params":{"meta":{"id":"\(identifier)","title":"New conversation","spaceId":"space-1","model":"test-model"}}}
            """
        ))

        XCTAssertEqual(response["result"] as? String, identifier)
    }

    func testChatSessionStartAcceptsNamedOptionsAndPreservesLegacyApiKey() throws {
        let ptr = makePtr()
        var createdConfig: ChatSessionRPCConfig?
        let controller = WebViewBridgeController(
            bridge: .shared,
            chatRPC: chatDependencies(
                ptr: ptr,
                createdConfig: { createdConfig = $0 },
                handle: "started-handle"
            )
        )

        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":31,"method":"chatSessionStart","params":{"apiKey":"legacy-key","endpoint":"https://chat.example/v1","model":"custom-model","systemInstruction":"system-rule"}}"#
        ))

        XCTAssertEqual(response["result"] as? String, "started-handle")
        XCTAssertEqual(createdConfig, ChatSessionRPCConfig(
            apiKey: "legacy-key",
            endpoint: "https://chat.example/v1",
            model: "custom-model",
            systemInstruction: "system-rule"
        ))
    }

    func testBrowserToolInvokeReturnsStructuredUnknownToolResult() throws {
        let controller = WebViewBridgeController(bridge: .shared)

        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":37,"method":"browserToolInvoke","params":{"name":"not_a_tool","args":{}}}"#
        ))
        let result = try XCTUnwrap(response["result"] as? [String: Any])

        XCTAssertEqual(result["ok"] as? Bool, false)
        XCTAssertEqual(result["error"] as? String, "unknown_tool")
    }

    func testBrowserToolInvokeOpenTabMakesURLAvailableToListTabs() throws {
        let url = "https://example.com/maho-ios-mobile-tool-qa"
        let controller = WebViewBridgeController(bridge: .shared)

        let openResponse = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":43,"method":"browserToolInvoke","params":{"name":"open_tab","args":{"url":"https://example.com/maho-ios-mobile-tool-qa"}}}"#
        ))
        let openResult = try XCTUnwrap(openResponse["result"] as? [String: Any])
        XCTAssertEqual(openResult["ok"] as? Bool, true)

        let listResponse = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":44,"method":"browserToolInvoke","params":{"name":"list_tabs","args":{}}}"#
        ))
        let listResult = try XCTUnwrap(listResponse["result"] as? [String: Any])
        let tabs = try XCTUnwrap(listResult["result"] as? [[String: Any]])
        let tab = try XCTUnwrap(tabs.first(where: { $0["url"] as? String == url }))
        let tabId = try XCTUnwrap(tab["id"] as? String)

        let closeResponse = try rpcObject(controller.dispatchRPCForTesting(
            """
            {"jsonrpc":"2.0","id":45,"method":"browserToolInvoke","params":{"name":"close_tab","args":{"tab_id":"\(tabId)"}}}
            """
        ))
        let closeResult = try XCTUnwrap(closeResponse["result"] as? [String: Any])
        XCTAssertEqual(closeResult["ok"] as? Bool, true)
    }

    func testBrowserToolInvokeTruncatesOversizedArrayResultsAtCompleteEntries() throws {
        let entry = ["title": String(repeating: "x", count: 700)]
        let controller = WebViewBridgeController(
            bridge: .shared,
            browserToolExecutor: BrowserToolExecutor { _, _ in
                ["ok": true, "result": Array(repeating: entry, count: 10)]
            }
        )

        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":38,"method":"browserToolInvoke","params":{"name":"list_tabs","args":{}}}"#
        ))
        let result = try XCTUnwrap(response["result"] as? [String: Any])
        let entries = try XCTUnwrap(result["result"] as? [[String: String]])

        XCTAssertEqual(result["ok"] as? Bool, true)
        XCTAssertEqual(result["truncated"] as? Bool, true)
        XCTAssertTrue((1..<10).contains(entries.count))
        let resultData = try JSONSerialization.data(withJSONObject: result)
        XCTAssertLessThanOrEqual(resultData.count, 4_000)
    }

    func testChatToolResultForwardsExactNameAndFinalTrigger() throws {
        let ptr = makePtr()
        var received: (OpaquePointer, String, String, String, Bool)?
        let controller = WebViewBridgeController(
            bridge: .shared,
            chatRPC: ChatSessionRPCDependencies(
                create: { _ in ptr },
                handleForPointer: { _ in "chat-handle" },
                pointerForHandle: { $0 == "chat-handle" ? ptr : nil },
                hasSession: { $0 == "chat-handle" },
                conversation: { _ in nil },
                bind: { _, _ in },
                messages: { _ in [] },
                appendUser: { _, _ in },
                appendAssistant: { _, _ in },
                sendToolResult: { received = ($0, $1, $2, $3, $4); return true },
                resolveSecret: { _ in nil },
                resolveManagedProvider: { nil }
            )
        )
        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":47,"method":"chatSendToolResult","params":{"handle":"chat-handle","toolCallId":"tabs","result":{"output":"[]"},"toolName":"list_tabs","trigger":false}}"#
        ))

        XCTAssertEqual(response["result"] as? Bool, true)
        let sent = try XCTUnwrap(received)
        XCTAssertEqual(sent.0, ptr)
        XCTAssertEqual(sent.1, "tabs")
        XCTAssertEqual(sent.2, "list_tabs")
        XCTAssertEqual(sent.3, #"{"output":"[]"}"#)
        XCTAssertFalse(sent.4)
    }

    func testChatSessionStartManagedConfigOverridesJavaScriptAndRedactsToken() throws {
        let ptr = makePtr()
        var createdConfig: ChatSessionRPCConfig?
        let token = "native-relay-access-token"
        let controller = WebViewBridgeController(
            bridge: .shared,
            chatRPC: chatDependencies(
                ptr: ptr,
                createdConfig: { createdConfig = $0 },
                handle: "managed-handle",
                resolveManagedProvider: {
                    AiProviderConfig(
                        providerLabel: "Maho AI",
                        apiKey: token,
                        endpoint: "https://proxy.maho.co/v1/chat/completions",
                        model: "gpt-4o-mini"
                    )
                }
            )
        )

        let responseJSON = controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":35,"method":"chatSessionStart","params":{"opts":{"apiKey":"js-attacker-key","endpoint":"https://attacker.example/steal","model":"attacker-model","systemInstruction":"trusted-user-rule","credentialProvider":"maho-managed"}}}"#
        )
        let response = try rpcObject(responseJSON)

        XCTAssertEqual(response["result"] as? String, "managed-handle")
        XCTAssertEqual(createdConfig, ChatSessionRPCConfig(
            apiKey: token,
            endpoint: "https://proxy.maho.co/v1/chat/completions",
            model: "gpt-4o-mini",
            systemInstruction: "trusted-user-rule"
        ))
        XCTAssertFalse(responseJSON.contains(token))
        XCTAssertFalse(responseJSON.contains("js-attacker-key"))

        let failingController = WebViewBridgeController(
            bridge: .shared,
            chatRPC: chatDependencies(
                ptr: ptr,
                createdConfig: { _ in },
                handle: "unused-handle",
                createSucceeds: false,
                resolveManagedProvider: {
                    AiProviderConfig(
                        providerLabel: "Maho AI",
                        apiKey: token,
                        endpoint: "https://proxy.maho.co/v1/chat/completions",
                        model: "gpt-4o-mini"
                    )
                }
            )
        )
        let errorJSON = failingController.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":36,"method":"chatSessionStart","params":{"opts":{"credentialProvider":"maho-managed"}}}"#
        )
        let errorResponse = try rpcObject(errorJSON)

        XCTAssertNotNil(errorResponse["error"])
        XCTAssertFalse(errorJSON.contains(token))
    }

    func testChatSessionStartResolvesCustomSecretWithoutReturningItToJavaScript() throws {
        let ptr = makePtr()
        var createdConfig: ChatSessionRPCConfig?
        var resolvedProviders: [String] = []
        let secret = "native-keychain-secret"
        let controller = WebViewBridgeController(
            bridge: .shared,
            chatRPC: chatDependencies(
                ptr: ptr,
                createdConfig: { createdConfig = $0 },
                handle: "secret-handle",
                resolveSecret: {
                    resolvedProviders.append($0)
                    return secret
                }
            )
        )

        let responseJSON = controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":32,"method":"chatSessionStart","params":{"opts":{"apiKey":"","endpoint":"https://custom.example/v1","model":"local-model","systemInstruction":"rule","credentialProvider":"openai-compatible"}}}"#
        )
        let response = try rpcObject(responseJSON)

        XCTAssertEqual(response["result"] as? String, "secret-handle")
        XCTAssertEqual(resolvedProviders, [AiSettingsKeys.customApiKeyAccount])
        XCTAssertEqual(createdConfig?.apiKey, secret)
        XCTAssertFalse(responseJSON.contains(secret))
    }

    func testChatSessionResumeUsesNamedConfigAndRehydratesHistory() throws {
        let ptr = makePtr()
        var createdConfig: ChatSessionRPCConfig?
        var boundConversationId: String?
        var appendedMessages: [ChatSessionRPCMessage] = []
        let dependencies = ChatSessionRPCDependencies(
            create: {
                createdConfig = $0
                return ptr
            },
            handleForPointer: { _ in nil },
            pointerForHandle: { _ in nil },
            hasSession: { _ in false },
            conversation: { id in
                id == "conversation-7" ? ChatSessionRPCConversation(model: "stored-model") : nil
            },
            bind: { id, boundPtr in
                XCTAssertEqual(boundPtr, ptr)
                boundConversationId = id
            },
            messages: { id in
                XCTAssertEqual(id, "conversation-7")
                return [
                    ChatSessionRPCMessage(role: "user", content: "first"),
                    ChatSessionRPCMessage(role: "assistant", content: "second"),
                    ChatSessionRPCMessage(role: "system", content: "ignored")
                ]
            },
            appendUser: { appendedPtr, content in
                XCTAssertEqual(appendedPtr, ptr)
                appendedMessages.append(ChatSessionRPCMessage(role: "user", content: content))
            },
            appendAssistant: { appendedPtr, content in
                XCTAssertEqual(appendedPtr, ptr)
                appendedMessages.append(ChatSessionRPCMessage(role: "assistant", content: content))
            },
            sendToolResult: { _, _, _, _, _ in true },
            resolveSecret: { _ in XCTFail("legacy apiKey must not resolve a secret"); return nil },
            resolveManagedProvider: { XCTFail("openai-compatible must not resolve managed credentials"); return nil }
        )
        let controller = WebViewBridgeController(bridge: .shared, chatRPC: dependencies)

        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":33,"method":"chatSessionResume","params":{"conversationId":"conversation-7","opts":{"apiKey":"resume-key","endpoint":"https://resume.example/v1","model":"resume-model","systemInstruction":"resume-rule","credentialProvider":"openai-compatible"}}}"#
        ))

        XCTAssertEqual(response["result"] as? String, "conversation-7")
        XCTAssertEqual(createdConfig, ChatSessionRPCConfig(
            apiKey: "resume-key",
            endpoint: "https://resume.example/v1",
            model: "resume-model",
            systemInstruction: "resume-rule"
        ))
        XCTAssertEqual(boundConversationId, "conversation-7")
        XCTAssertEqual(appendedMessages.map(\.role), ["user", "assistant"])
        XCTAssertEqual(appendedMessages.map(\.content), ["first", "second"])
    }

    func testChatSessionResumeArrayParamsResolveSecretAndNeverCreateEmptyEndpoint() throws {
        let ptr = makePtr()
        var createdConfig: ChatSessionRPCConfig?
        let secret = "resume-native-secret"
        let dependencies = ChatSessionRPCDependencies(
            create: {
                createdConfig = $0
                return ptr
            },
            handleForPointer: { _ in nil },
            pointerForHandle: { _ in nil },
            hasSession: { _ in false },
            conversation: { _ in ChatSessionRPCConversation(model: "stored-model") },
            bind: { _, _ in },
            messages: { _ in [] },
            appendUser: { _, _ in },
            appendAssistant: { _, _ in },
            sendToolResult: { _, _, _, _, _ in true },
            resolveSecret: { provider in
                XCTAssertEqual(provider, AiSettingsKeys.customApiKeyAccount)
                return secret
            },
            resolveManagedProvider: { XCTFail("openai-compatible must not resolve managed credentials"); return nil }
        )
        let controller = WebViewBridgeController(bridge: .shared, chatRPC: dependencies)

        let responseJSON = controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":34,"method":"chatSessionResume","params":["conversation-8",{"apiKey":"","endpoint":"https://array.example/v1","model":"","systemInstruction":"array-rule","credentialProvider":"openai-compatible"}]}"#
        )
        let response = try rpcObject(responseJSON)

        XCTAssertEqual(response["result"] as? String, "conversation-8")
        XCTAssertEqual(createdConfig, ChatSessionRPCConfig(
            apiKey: secret,
            endpoint: "https://array.example/v1",
            model: "stored-model",
            systemInstruction: "array-rule"
        ))
        XCTAssertFalse(createdConfig?.endpoint.isEmpty ?? true)
        XCTAssertFalse(responseJSON.contains(secret))
    }

    func testArtifactListRPCDispatchesExactMethodAndReturnsMetadataOnly() throws {
        let ptr = makePtr()
        let handle = "artifact-list-handle"
        var listedPointer: OpaquePointer?
        let controller = WebViewBridgeController(
            bridge: .shared,
            agentHandleResolver: { $0 == handle ? ptr : nil },
            agentArtifactLister: { pointer in
                listedPointer = pointer
                return [[
                    "artifactId": "artifact-21",
                    "displayName": "report.txt",
                    "mimeType": "text/plain",
                    "sizeBytes": 12,
                    "createdAt": 1_700_000_000.0
                ]]
            },
            agentArtifactReader: { _, _ in XCTFail("list must not read bytes"); return nil }
        )

        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":21,"method":"agentListArtifacts","params":["artifact-list-handle"]}"#
        ))
        let result = try XCTUnwrap(response["result"] as? [[String: Any]])

        XCTAssertEqual(listedPointer, ptr)
        XCTAssertEqual(result.first?["artifactId"] as? String, "artifact-21")
        XCTAssertNil(result.first?["path"])
        XCTAssertNil(result.first?["storage_rel_path"])
        XCTAssertNil(result.first?["bytes"])
        XCTAssertNil(result.first?["data"])
    }

    func testArtifactShareUnknownIdReturnsErrorWithoutReadingOrPresenting() throws {
        let ptr = makePtr()
        var didRead = false
        var didPresent = false
        let tempRoot = FileManager.default.temporaryDirectory
            .appendingPathComponent("MahoArtifactShareTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: tempRoot) }
        let coordinator = ArtifactShareCoordinator(tempRoot: tempRoot) { _, _ in
            didPresent = true
            return true
        }
        let controller = WebViewBridgeController(
            bridge: .shared,
            agentHandleResolver: { _ in ptr },
            agentArtifactLister: { _ in [] },
            agentArtifactReader: { _, _ in didRead = true; return Data() },
            artifactShareCoordinator: coordinator
        )

        let response = try rpcObject(controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":22,"method":"artifactShare","params":["handle","missing"]}"#
        ))

        XCTAssertNotNil(response["error"])
        XCTAssertFalse(didRead)
        drainMainQueue()
        XCTAssertFalse(didPresent)
    }

    func testArtifactShareSanitizesNameRetainsTempUntilCompletionAndCleansIt() throws {
        let ptr = makePtr()
        let tempRoot = FileManager.default.temporaryDirectory
            .appendingPathComponent("MahoArtifactShareTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: tempRoot) }

        var presentedURL: URL?
        var presentationWasMainThread = false
        var completion: (() -> Void)?
        let coordinator = ArtifactShareCoordinator(tempRoot: tempRoot) { url, onComplete in
            presentedURL = url
            presentationWasMainThread = Thread.isMainThread
            completion = onComplete
            return true
        }
        let metadata: [String: Any] = [
            "artifactId": "artifact-21",
            "displayName": "../../Quarterly / Report?.txt",
            "mimeType": "text/plain",
            "sizeBytes": 12,
            "createdAt": 1_700_000_000.0
        ]
        let controller = WebViewBridgeController(
            bridge: .shared,
            agentHandleResolver: { _ in ptr },
            agentArtifactLister: { _ in [metadata] },
            agentArtifactReader: { _, artifactId in
                XCTAssertEqual(artifactId, "artifact-21")
                return Data("native bytes".utf8)
            },
            artifactShareCoordinator: coordinator
        )

        let responseJSON = controller.dispatchRPCForTesting(
            #"{"jsonrpc":"2.0","id":23,"method":"artifactShare","params":{"handle":"handle","artifactId":"artifact-21"}}"#
        )
        let response = try rpcObject(responseJSON)
        XCTAssertEqual(response["result"] as? Bool, true)
        XCTAssertFalse(responseJSON.contains("native bytes"))
        XCTAssertFalse(responseJSON.contains(tempRoot.path))

        drainMainQueue()
        let url = try XCTUnwrap(presentedURL)
        XCTAssertTrue(presentationWasMainThread)
        XCTAssertEqual(url.lastPathComponent, "Quarterly Report_.txt")
        XCTAssertEqual(try Data(contentsOf: url), Data("native bytes".utf8))
        XCTAssertTrue(FileManager.default.fileExists(atPath: url.path))

        completion?()
        XCTAssertFalse(FileManager.default.fileExists(atPath: url.deletingLastPathComponent().path))
    }

    func testArtifactShareCoordinatorCleansStaleExportsOnLaunchAndUsesUniqueLocations() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("MahoArtifactShareTests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let stale = root.appendingPathComponent("stale/export.txt")
        try FileManager.default.createDirectory(at: stale.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data("stale".utf8).write(to: stale)
        defer { try? FileManager.default.removeItem(at: root) }

        var urls: [URL] = []
        var completions: [() -> Void] = []
        let coordinator = ArtifactShareCoordinator(tempRoot: root) { url, completion in
            urls.append(url)
            completions.append(completion)
            return true
        }
        XCTAssertFalse(FileManager.default.fileExists(atPath: stale.path))

        try coordinator.share(data: Data("one".utf8), displayName: "report.txt")
        try coordinator.share(data: Data("two".utf8), displayName: "report.txt")
        drainMainQueue()

        XCTAssertEqual(urls.count, 2)
        XCTAssertNotEqual(urls[0].deletingLastPathComponent(), urls[1].deletingLastPathComponent())
        completions.forEach { $0() }
        XCTAssertTrue((try FileManager.default.contentsOfDirectory(atPath: root.path)).isEmpty)
    }

    func testNativeArtifactPresenterAnchorsIPadPopoverToTopPresenter() {
        let root = PresentedViewControllerStub()
        root.loadViewIfNeeded()
        let presented = UIViewController()
        presented.loadViewIfNeeded()
        root.stubPresentedViewController = presented
        XCTAssertTrue(NativeArtifactSharePresenter.topPresenter(from: root) === presented)

        var capturedActivity: UIActivityViewController?
        let didPresent = NativeArtifactSharePresenter.present(
            fileURL: URL(fileURLWithPath: "/tmp/report.txt"),
            rootViewController: root,
            activityFactory: { items in
                let activity = UIActivityViewController(activityItems: items, applicationActivities: nil)
                capturedActivity = activity
                return activity
            },
            completion: {}
        )

        let popover = capturedActivity?.popoverPresentationController
        XCTAssertTrue(didPresent)
        XCTAssertTrue(Thread.isMainThread)
        XCTAssertTrue(popover?.sourceView === presented.view)
        XCTAssertEqual(popover?.sourceRect.origin.x, presented.view.bounds.midX)
        XCTAssertEqual(popover?.sourceRect.origin.y, presented.view.bounds.maxY - ShellTheme.Size.bottomBarMinHeight)
        XCTAssertEqual(popover?.permittedArrowDirections, [])
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

    private func conversationDependencies(
        listPayload: @escaping (String, Int) -> String? = { _, _ in "[]" },
        archive: @escaping (String) -> Bool = { _ in true },
        unarchive: @escaping (String) -> Bool = { _ in true },
        bulk: @escaping (String, [String]) -> String? = { _, _ in "{}" },
        getPolicy: @escaping () -> Int32 = { -1 },
        setPolicy: @escaping (Int32) -> Bool = { _ in true }
    ) -> ConversationRPCDependencies {
        ConversationRPCDependencies(
            listPayload: listPayload,
            archive: archive,
            unarchive: unarchive,
            bulk: bulk,
            getPolicy: getPolicy,
            setPolicy: setPolicy
        )
    }

    private func chatDependencies(
        ptr: OpaquePointer,
        createdConfig: @escaping (ChatSessionRPCConfig) -> Void,
        handle: String,
        createSucceeds: Bool = true,
        resolveSecret: @escaping (String) -> String? = { _ in nil },
        resolveManagedProvider: @escaping () -> AiProviderConfig? = { nil }
    ) -> ChatSessionRPCDependencies {
        ChatSessionRPCDependencies(
            create: {
                createdConfig($0)
                return createSucceeds ? ptr : nil
            },
            handleForPointer: { $0 == ptr ? handle : nil },
            pointerForHandle: { $0 == handle ? ptr : nil },
            hasSession: { _ in false },
            conversation: { _ in nil },
            bind: { _, _ in },
            messages: { _ in [] },
            appendUser: { _, _ in },
            appendAssistant: { _, _ in },
            sendToolResult: { _, _, _, _, _ in true },
            resolveSecret: resolveSecret,
            resolveManagedProvider: resolveManagedProvider
        )
    }

    private func rpcObject(_ json: String) throws -> [String: Any] {
        let data = try XCTUnwrap(json.data(using: .utf8))
        return try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    private func rpcErrorKind(_ controller: WebViewBridgeController, _ request: String) throws -> String? {
        let response = try rpcObject(controller.dispatchRPCForTesting(request))
        return (response["error"] as? [String: Any])?["kind"] as? String
    }

    private func rpcResult(_ controller: WebViewBridgeController, _ request: String) throws -> Any? {
        try rpcObject(controller.dispatchRPCForTesting(request))["result"]
    }

    private func drainMainQueue() {
        let expectation = expectation(description: "main queue drained")
        DispatchQueue.main.async { expectation.fulfill() }
        wait(for: [expectation], timeout: 2)
    }
}
