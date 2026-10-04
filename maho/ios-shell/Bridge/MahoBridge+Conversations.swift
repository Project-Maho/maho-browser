import Foundation

enum ConversationArchiveForegroundBridge {
    static func sweep() {
        _ = MahoBridge.shared.autoArchiveConversations()
    }
}

struct ConversationSession: Codable, Identifiable {
    let id: String
    let title: String?
    let spaceId: String?
    let model: String?
    let createdAt: String
    let updatedAt: String
    let archivedAt: String?
    let projectId: String?

    var space_id: String? { spaceId }
    var created_at: String { createdAt }
    var last_message_at: String { updatedAt }
    var message_count: Int { 0 }

    private enum CodingKeys: String, CodingKey {
        case id
        case title
        case spaceId
        case model
        case createdAt
        case updatedAt
        case archivedAt
        case projectId
    }
}

struct ConversationProject: Codable, Identifiable {
    let id: String
    let name: String
    let createdAt: String
    let updatedAt: String
}

struct ConversationTurn: Codable, Identifiable {
    let id: String
    let role: String
    let content: String
    let urlContext: String?
    let createdAt: String

    var url_context: String? { urlContext }
    var created_at: String { createdAt }

    private enum CodingKeys: String, CodingKey {
        case id
        case role
        case content
        case urlContext
        case createdAt
    }
}

@_silgen_name("maho_core_create_conversation")
private func ffiCreateConversation(
    _ ptr: OpaquePointer?,
    _ id: UnsafePointer<CChar>?,
    _ title: UnsafePointer<CChar>?,
    _ spaceId: UnsafePointer<CChar>?,
    _ model: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_list_conversations")
private func ffiListConversations(
    _ ptr: OpaquePointer?,
    _ limit: Int
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_list_conversations_v2")
private func ffiListConversationsV2(
    _ ptr: OpaquePointer?,
    _ queryJson: UnsafePointer<CChar>?
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_archive_conversation")
private func ffiArchiveConversation(_ ptr: OpaquePointer?, _ id: UnsafePointer<CChar>?) -> Bool

@_silgen_name("maho_core_unarchive_conversation")
private func ffiUnarchiveConversation(_ ptr: OpaquePointer?, _ id: UnsafePointer<CChar>?) -> Bool

@_silgen_name("maho_core_apply_conversation_bulk_operation")
private func ffiApplyConversationBulkOperation(
    _ ptr: OpaquePointer?,
    _ requestJson: UnsafePointer<CChar>?
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_get_conversation_auto_archive_policy")
private func ffiGetConversationAutoArchivePolicy(_ ptr: OpaquePointer?) -> Int32

@_silgen_name("maho_core_set_conversation_auto_archive_policy")
private func ffiSetConversationAutoArchivePolicy(_ ptr: OpaquePointer?, _ days: Int32) -> Bool

@_silgen_name("maho_core_auto_archive_conversations")
private func ffiAutoArchiveConversations(_ ptr: OpaquePointer?, _ nowSeconds: Int64) -> Int32

@_silgen_name("maho_core_get_conversation_messages")
private func ffiGetConversationMessages(
    _ ptr: OpaquePointer?,
    _ sessionId: UnsafePointer<CChar>?
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_save_conversation_message")
private func ffiSaveConversationMessage(
    _ ptr: OpaquePointer?,
    _ sessionId: UnsafePointer<CChar>?,
    _ role: UnsafePointer<CChar>?,
    _ content: UnsafePointer<CChar>?,
    _ urlContext: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_delete_conversation")
private func ffiDeleteConversation(
    _ ptr: OpaquePointer?,
    _ sessionId: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_rename_conversation")
private func ffiRenameConversation(
    _ ptr: OpaquePointer?,
    _ sessionId: UnsafePointer<CChar>?,
    _ title: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_list_conversation_projects")
private func ffiListConversationProjects(_ ptr: OpaquePointer?) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_create_conversation_project")
private func ffiCreateConversationProject(_ ptr: OpaquePointer?, _ name: UnsafePointer<CChar>?) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_rename_conversation_project")
private func ffiRenameConversationProject(_ ptr: OpaquePointer?, _ id: UnsafePointer<CChar>?, _ name: UnsafePointer<CChar>?) -> Bool

@_silgen_name("maho_core_delete_conversation_project")
private func ffiDeleteConversationProject(_ ptr: OpaquePointer?, _ id: UnsafePointer<CChar>?) -> Bool

@_silgen_name("maho_core_move_conversations_to_project")
private func ffiMoveConversationsToProject(_ ptr: OpaquePointer?, _ requestJson: UnsafePointer<CChar>?) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_get_composer_draft")
private func ffiGetComposerDraft(
    _ ptr: OpaquePointer?,
    _ scopeJson: UnsafePointer<CChar>?
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("maho_core_set_composer_draft")
private func ffiSetComposerDraft(
    _ ptr: OpaquePointer?,
    _ scopeJson: UnsafePointer<CChar>?,
    _ text: UnsafePointer<CChar>?
) -> Bool

@_silgen_name("maho_core_delete_composer_draft")
private func ffiDeleteComposerDraft(
    _ ptr: OpaquePointer?,
    _ scopeJson: UnsafePointer<CChar>?
) -> Bool

private func withOptionalCString<R>(_ value: String?, _ body: (UnsafePointer<CChar>?) -> R) -> R {
    guard let value else {
        return body(nil)
    }

    return value.withCString(body)
}

extension MahoBridge {
    func createConversation(id: String, title: String?, spaceId: String?, model: String?) -> Bool {
        withCore { ptr in
            FFIString.withCString(id) { idPointer in
                withOptionalCString(title) { titlePointer in
                    withOptionalCString(spaceId) { spaceIdPointer in
                        withOptionalCString(model) { modelPointer in
                            ffiCreateConversation(ptr, idPointer, titlePointer, spaceIdPointer, modelPointer)
                        }
                    }
                }
            }
        } ?? false
    }

    func listConversations(limit: Int = 100) -> [ConversationSession] {
        (withCore { ptr in
            FFIString.consumeJSON(
                ffiListConversations(ptr, max(limit, 0)),
                as: [ConversationSession].self
            )
        } ?? nil) ?? []
    }

    func listConversationsPayload(state: String, limit: Int) -> String? {
        guard let data = try? JSONSerialization.data(withJSONObject: ["state": state, "limit": limit]),
              let queryJson = String(data: data, encoding: .utf8) else { return nil }
        return (withCore { ptr in
            FFIString.withCString(queryJson) { queryPointer in
                FFIString.consume(ffiListConversationsV2(ptr, queryPointer))
            }
        }) ?? nil
    }

    func listConversations(state: String, limit: Int = 100) -> [ConversationSession] {
        guard let payload = listConversationsPayload(state: state, limit: limit),
              let data = payload.data(using: .utf8),
              let sessions = try? data.mahoDecoded(as: [ConversationSession].self) else { return [] }
        return sessions
    }

    func archiveConversation(id: String) -> Bool {
        withCore { ptr in FFIString.withCString(id) { ffiArchiveConversation(ptr, $0) } } ?? false
    }

    func unarchiveConversation(id: String) -> Bool {
        withCore { ptr in FFIString.withCString(id) { ffiUnarchiveConversation(ptr, $0) } } ?? false
    }

    func applyConversationBulkOperation(operation: String, ids: [String]) -> String? {
        guard ["archive", "unarchive", "delete"].contains(operation),
              let data = try? JSONSerialization.data(withJSONObject: ["op": operation, "ids": ids]),
              let requestJson = String(data: data, encoding: .utf8) else { return nil }
        return (withCore { ptr in
            FFIString.withCString(requestJson) { FFIString.consume(ffiApplyConversationBulkOperation(ptr, $0)) }
        }) ?? nil
    }

    func getConversationAutoArchivePolicy() -> Int32 {
        withCore { ffiGetConversationAutoArchivePolicy($0) } ?? -1
    }

    func setConversationAutoArchivePolicy(days: Int32) -> Bool {
        withCore { ffiSetConversationAutoArchivePolicy($0, days) } ?? false
    }

    @discardableResult
    func autoArchiveConversations(nowSeconds: Int64 = Int64(Date().timeIntervalSince1970)) -> Int32 {
        withCore { ffiAutoArchiveConversations($0, nowSeconds) } ?? -1
    }

    func getConversationMessages(sessionId: String) -> [ConversationTurn] {
        (withCore { ptr in
            FFIString.withCString(sessionId) { sessionIdPointer in
                FFIString.consumeJSON(
                    ffiGetConversationMessages(ptr, sessionIdPointer),
                    as: [ConversationTurn].self
                )
            }
        } ?? nil) ?? []
    }

    func saveConversationMessage(sessionId: String, role: String, content: String, urlContext: String? = nil) -> Bool {
        withCore { ptr in
            FFIString.withCString(sessionId) { sessionIdPointer in
                FFIString.withCString(role) { rolePointer in
                    FFIString.withCString(content) { contentPointer in
                        withOptionalCString(urlContext) { urlContextPointer in
                            ffiSaveConversationMessage(
                                ptr,
                                sessionIdPointer,
                                rolePointer,
                                contentPointer,
                                urlContextPointer
                            )
                        }
                    }
                }
            }
        } ?? false
    }

    func deleteConversation(sessionId: String) -> Bool {
        withCore { ptr in
            FFIString.withCString(sessionId) { sessionIdPointer in
                ffiDeleteConversation(ptr, sessionIdPointer)
            }
        } ?? false
    }

    func renameConversation(sessionId: String, title: String) -> Bool {
        withCore { ptr in
            FFIString.withCString(sessionId) { sessionIdPointer in
                FFIString.withCString(title) { titlePointer in
                    ffiRenameConversation(ptr, sessionIdPointer, titlePointer)
                }
            }
        } ?? false
    }

    func listConversationProjectsPayload() -> String? {
        (withCore { FFIString.consume(ffiListConversationProjects($0)) }) ?? nil
    }

    func createConversationProjectPayload(name: String) -> String? {
        (withCore { ptr in FFIString.withCString(name) { FFIString.consume(ffiCreateConversationProject(ptr, $0)) } }) ?? nil
    }

    func renameConversationProject(id: String, name: String) -> Bool {
        withCore { ptr in FFIString.withCString(id) { idPointer in FFIString.withCString(name) { ffiRenameConversationProject(ptr, idPointer, $0) } } } ?? false
    }

    func deleteConversationProject(id: String) -> Bool {
        withCore { ptr in FFIString.withCString(id) { ffiDeleteConversationProject(ptr, $0) } } ?? false
    }

    func moveConversationsToProjectPayload(ids: [String], projectId: String?) -> String? {
        var request: [String: Any] = ["ids": ids]
        request["projectId"] = projectId ?? NSNull()
        guard let data = try? JSONSerialization.data(withJSONObject: request),
              let json = String(data: data, encoding: .utf8) else { return nil }
        return (withCore { ptr in FFIString.withCString(json) { FFIString.consume(ffiMoveConversationsToProject(ptr, $0)) } }) ?? nil
    }

    // MARK: - Composer drafts

    /// Returns the stored draft payload (`{"version":1,"text":...,"updatedAt":...}`)
    /// for `scopeJson`, or nil when absent, orphaned, or the scope is malformed.
    func getComposerDraft(scopeJson: String) -> String? {
        (withCore { ptr in
            FFIString.withCString(scopeJson) { scopePointer in
                FFIString.consume(ffiGetComposerDraft(ptr, scopePointer))
            }
        }) ?? nil
    }

    /// Stores `text` verbatim for `scopeJson`. An empty string deletes the row.
    func setComposerDraft(scopeJson: String, text: String) -> Bool {
        withCore { ptr in
            FFIString.withCString(scopeJson) { scopePointer in
                FFIString.withCString(text) { textPointer in
                    ffiSetComposerDraft(ptr, scopePointer, textPointer)
                }
            }
        } ?? false
    }

    func deleteComposerDraft(scopeJson: String) -> Bool {
        withCore { ptr in
            FFIString.withCString(scopeJson) { scopePointer in
                ffiDeleteComposerDraft(ptr, scopePointer)
            }
        } ?? false
    }
}
