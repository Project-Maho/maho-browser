import Foundation

struct ConversationSession: Codable, Identifiable {
    let id: String
    let title: String?
    let space_id: String?
    let model: String?
    let message_count: Int
    let created_at: String
    let last_message_at: String
}

struct ConversationTurn: Codable, Identifiable {
    let id: String
    let role: String
    let content: String
    let url_context: String?
    let created_at: String
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
}
