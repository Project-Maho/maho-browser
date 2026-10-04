use maho_storage::sqlite::SqliteStorage;
use maho_types::chat::{
    Conversation, ConversationBulkOperation, ConversationBulkResult, ConversationListState,
    ConversationProject, ConversationTurn,
};

pub struct ConversationManager;

impl ConversationManager {
    pub fn create(
        storage: &SqliteStorage,
        id: &str,
        title: Option<&str>,
        space_id: Option<&str>,
        model: Option<&str>,
    ) -> bool {
        storage
            .create_conversation(id, title, space_id, model)
            .is_ok()
    }

    pub fn list(storage: &SqliteStorage, limit: usize) -> Vec<Conversation> {
        storage
            .list_conversations(maho_types::chat::ConversationListState::Active, limit)
            .unwrap_or_default()
    }

    pub fn list_v2(
        storage: &SqliteStorage,
        state: ConversationListState,
        limit: usize,
    ) -> Vec<Conversation> {
        storage.list_conversations(state, limit).unwrap_or_default()
    }

    pub fn archive(storage: &SqliteStorage, id: &str, now: &str) -> bool {
        storage.archive_conversation(id, now).unwrap_or(false)
    }

    pub fn unarchive(storage: &SqliteStorage, id: &str, now: &str) -> bool {
        storage.unarchive_conversation(id, now).unwrap_or(false)
    }

    pub fn bulk(
        storage: &SqliteStorage,
        operation: ConversationBulkOperation,
        ids: &[String],
        now: &str,
    ) -> Result<ConversationBulkResult, maho_storage::StorageError> {
        storage.apply_conversation_bulk_operation(operation, ids, now)
    }

    pub fn list_projects(storage: &SqliteStorage) -> Vec<ConversationProject> {
        storage.list_conversation_projects().unwrap_or_default()
    }

    pub fn get_messages(storage: &SqliteStorage, session_id: &str) -> Vec<ConversationTurn> {
        storage
            .get_conversation_messages(session_id)
            .unwrap_or_default()
    }

    pub fn save_message(
        storage: &SqliteStorage,
        session_id: &str,
        role: &str,
        content: &str,
        url_context: Option<&str>,
    ) -> bool {
        let id = uuid::Uuid::new_v4().to_string();
        storage
            .insert_conversation_turn(&id, session_id, role, content, url_context)
            .is_ok()
    }

    pub fn delete(storage: &SqliteStorage, id: &str) -> bool {
        storage.delete_conversation(id).unwrap_or(false)
    }

    pub fn rename(storage: &SqliteStorage, id: &str, title: &str) -> bool {
        storage.rename_conversation(id, title).unwrap_or(false)
    }
}
