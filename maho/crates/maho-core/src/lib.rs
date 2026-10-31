pub mod account_manager;
pub mod ai_search_intent;
pub mod atc_manager;
pub mod autofill_manager;
pub mod backup_manager;
pub mod bookmark_manager;
pub mod boost_css_sanitizer;
pub mod boost_manager;
pub mod command_bar;
pub mod composer_draft;
pub mod config_resolver;
pub mod content_blocker;
pub mod crash_recovery;
pub mod css_mod_manager;

pub mod chat_session;
pub mod conversation_manager;
pub mod download_manager;
pub mod easel_manager;
pub mod embedding;
pub mod error;
pub mod event_dispatcher;
pub mod extension_bridge;
pub mod find_manager;
pub mod google_identity;
pub(crate) mod http;
pub mod import_export_manager;
pub mod llm_client;
pub mod llm_manager;
pub mod llm_provider;
pub mod maho_core;
pub mod memory;
pub mod memory_manager;
pub mod note_manager;
pub mod notification_manager;
pub mod oscrypt;
pub mod permission_manager;
pub mod profile_import;
pub mod profile_manager;
pub mod provider_oauth;
pub mod providers;
pub mod reading_list_manager;
pub mod routine_runs;
pub mod routines;
pub mod security;
pub mod settings_manager;
pub mod sharing_manager;
pub mod shortcut_manager;
pub mod skills_manager;
pub mod snapshot;
pub mod space_manager;
pub mod stream_utils;
pub mod sync_crypto;
pub mod sync_manager;
pub mod sync_models;
pub mod tab_clustering;
pub mod tab_lifecycle;
pub mod tab_preview_manager;
pub mod tool_registry;
pub mod vault_crypto;
pub mod vault_generator;
pub mod vault_manager;
pub mod vault_runtime;
pub mod workspace_manager;

/// Test-only SQLCipher key installer for this crate's `--lib` test binary.
///
/// `maho_storage::sqlite::set_sqlcipher_key` writes a process-global slot that
/// every `SqliteStorage::open` in the process reads. `cargo test` runs the lib
/// tests as parallel threads in ONE process, so a test that injects its own key
/// mid-run silently re-keys databases other tests already opened: the victim
/// either sees `KeyNotConfigured` or `file is not a database` depending on
/// scheduling. Every key-dependent lib test installs the SAME key through this
/// function, which writes the slot exactly once, so the global value is stable
/// for the whole binary and no test can observe another test's key.
#[cfg(test)]
pub(crate) fn install_test_sqlcipher_key() {
    static INSTALL: std::sync::Once = std::sync::Once::new();
    INSTALL.call_once(|| {
        maho_storage::sqlite::set_sqlcipher_key("maho-core-lib-test-key")
            .expect("install process-wide test SQLCipher key");
    });
}
