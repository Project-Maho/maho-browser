import Foundation
import SQLite3

final class ConversationStore {
    static let shared = ConversationStore()

    private var db: OpaquePointer?
    private let queue = DispatchQueue(label: "dev.maho.conversation-store")
    private let SQLITE_TRANSIENT = unsafeBitCast(-1, to: sqlite3_destructor_type.self)

    private init() {
        let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        let mahoDir = appSupport.appendingPathComponent("Maho")
        try? FileManager.default.createDirectory(at: mahoDir, withIntermediateDirectories: true)
        let dbPath = mahoDir.appendingPathComponent("conversations.db").path

        if sqlite3_open_v2(dbPath, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nil) != SQLITE_OK {
            db = nil
            return
        }

        exec("PRAGMA journal_mode=WAL")
        exec("PRAGMA foreign_keys=ON")

        exec("""
            CREATE TABLE IF NOT EXISTS conversations (
                id TEXT PRIMARY KEY,
                title TEXT NOT NULL DEFAULT '',
                created_at REAL NOT NULL,
                updated_at REAL NOT NULL
            )
        """)
        exec("""
            CREATE TABLE IF NOT EXISTS messages (
                id TEXT PRIMARY KEY,
                conversation_id TEXT NOT NULL,
                role TEXT NOT NULL,
                content TEXT NOT NULL,
                timestamp REAL NOT NULL,
                FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
            )
        """)
        exec("CREATE INDEX IF NOT EXISTS idx_messages_conversation ON messages(conversation_id, timestamp)")
    }

    deinit {
        if let db = db { sqlite3_close(db) }
    }

    func createConversation() -> String {
        let id = UUID().uuidString
        let now = Date().timeIntervalSince1970
        queue.sync {
            var stmt: OpaquePointer?
            defer { sqlite3_finalize(stmt) }
            guard sqlite3_prepare_v2(db, "INSERT INTO conversations (id, title, created_at, updated_at) VALUES (?, '', ?, ?)", -1, &stmt, nil) == SQLITE_OK else { return }
            sqlite3_bind_text(stmt, 1, (id as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_bind_double(stmt, 2, now)
            sqlite3_bind_double(stmt, 3, now)
            sqlite3_step(stmt)
        }
        return id
    }

    func saveMessage(conversationId: String, role: ChatMessage.Role, content: String, timestamp: Date) {
        let id = UUID().uuidString
        let roleStr = role == .user ? "user" : "assistant"
        let ts = timestamp.timeIntervalSince1970
        queue.sync {
            var stmt: OpaquePointer?
            defer { sqlite3_finalize(stmt) }
            guard sqlite3_prepare_v2(db, "INSERT INTO messages (id, conversation_id, role, content, timestamp) VALUES (?, ?, ?, ?, ?)", -1, &stmt, nil) == SQLITE_OK else { return }
            sqlite3_bind_text(stmt, 1, (id as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_bind_text(stmt, 2, (conversationId as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_bind_text(stmt, 3, (roleStr as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_bind_text(stmt, 4, (content as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_bind_double(stmt, 5, ts)
            sqlite3_step(stmt)

            var stmt2: OpaquePointer?
            defer { sqlite3_finalize(stmt2) }
            guard sqlite3_prepare_v2(db, "UPDATE conversations SET updated_at = ? WHERE id = ?", -1, &stmt2, nil) == SQLITE_OK else { return }
            sqlite3_bind_double(stmt2, 1, ts)
            sqlite3_bind_text(stmt2, 2, (conversationId as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_step(stmt2)
        }
    }

    func loadMessages(conversationId: String) -> [ChatMessage] {
        queue.sync {
            var stmt: OpaquePointer?
            defer { sqlite3_finalize(stmt) }
            guard sqlite3_prepare_v2(db, "SELECT role, content, timestamp FROM messages WHERE conversation_id = ? ORDER BY timestamp ASC", -1, &stmt, nil) == SQLITE_OK else { return [] }
            sqlite3_bind_text(stmt, 1, (conversationId as NSString).utf8String, -1, SQLITE_TRANSIENT)

            var results: [ChatMessage] = []
            while sqlite3_step(stmt) == SQLITE_ROW {
                guard let rolePtr = sqlite3_column_text(stmt, 0),
                      let contentPtr = sqlite3_column_text(stmt, 1) else { continue }
                let roleStr = String(cString: rolePtr)
                let content = String(cString: contentPtr)
                let ts = sqlite3_column_double(stmt, 2)
                let role: ChatMessage.Role = roleStr == "user" ? .user : .assistant
                results.append(ChatMessage(role: role, content: content, timestamp: Date(timeIntervalSince1970: ts)))
            }
            return results
        }
    }

    func listConversations(limit: Int = 20) -> [(id: String, title: String, updatedAt: Date)] {
        queue.sync {
            var stmt: OpaquePointer?
            defer { sqlite3_finalize(stmt) }
            guard sqlite3_prepare_v2(db, "SELECT id, title, updated_at FROM conversations ORDER BY updated_at DESC LIMIT ?", -1, &stmt, nil) == SQLITE_OK else { return [] }
            sqlite3_bind_int(stmt, 1, Int32(limit))

            var results: [(id: String, title: String, updatedAt: Date)] = []
            while sqlite3_step(stmt) == SQLITE_ROW {
                guard let idPtr = sqlite3_column_text(stmt, 0),
                      let titlePtr = sqlite3_column_text(stmt, 1) else { continue }
                let id = String(cString: idPtr)
                let title = String(cString: titlePtr)
                let ts = sqlite3_column_double(stmt, 2)
                results.append((id: id, title: title, updatedAt: Date(timeIntervalSince1970: ts)))
            }
            return results
        }
    }

    func updateTitle(conversationId: String, title: String) {
        queue.sync {
            var stmt: OpaquePointer?
            defer { sqlite3_finalize(stmt) }
            guard sqlite3_prepare_v2(db, "UPDATE conversations SET title = ? WHERE id = ?", -1, &stmt, nil) == SQLITE_OK else { return }
            sqlite3_bind_text(stmt, 1, (title as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_bind_text(stmt, 2, (conversationId as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_step(stmt)
        }
    }

    func deleteConversation(conversationId: String) {
        queue.sync {
            var stmt: OpaquePointer?
            defer { sqlite3_finalize(stmt) }
            guard sqlite3_prepare_v2(db, "DELETE FROM conversations WHERE id = ?", -1, &stmt, nil) == SQLITE_OK else { return }
            sqlite3_bind_text(stmt, 1, (conversationId as NSString).utf8String, -1, SQLITE_TRANSIENT)
            sqlite3_step(stmt)
        }
    }

    func autoTitle(conversationId: String, firstMessage: String) {
        var title = firstMessage.trimmingCharacters(in: .whitespacesAndNewlines)
        if title.count > 40 {
            title = String(title.prefix(37)) + "..."
        }
        updateTitle(conversationId: conversationId, title: title)
    }

    private func exec(_ sql: String) {
        sqlite3_exec(db, sql, nil, nil, nil)
    }
}
