// Copyright 2026 Maho Browser. All rights reserved.

//! Real mail backend for the Chromium mail helper process.
//!
//! Foundation scope implemented here: account auth resolution, SQLCipher DB
//! initialization from injected keys, IMAP folder/envelope sync, IDLE monitoring,
//! progressive body backfill, and read helpers over the local model.
//!
//! TODO(deferred-mail-verticals): calendar, PGP, S/MIME, rules, templates,
//! signatures, translation, send/compose, offline queue flushing, smart inbox,
//! contacts, and llama-cpp-2 integration are intentionally out of this crate's
//! current scope.

pub mod account;
pub mod backfill;
pub mod credentials;
pub mod error;
pub mod ffi;
pub mod oauth;
pub mod otp;
pub mod read;
pub mod runtime;
pub mod state;
pub mod sync;
pub mod transfer;

#[cfg(test)]
#[allow(dead_code)]
pub(crate) mod test_support {
    use std::path::PathBuf;
    use std::sync::Arc;

    use crate::state::AppCtx;

    pub(crate) fn pool_with_seeded_data() -> maho_core::db::SqlitePool {
        let path = db_path();
        let pool = maho_core::db::init_database_pool(&path)
            .expect("test database pool");
        {
            let conn = pool.get().expect("test connection");
            seed_test_data(&conn);
        }
        pool
    }

    pub(crate) fn ctx(pool: maho_core::db::SqlitePool) -> AppCtx {
        AppCtx {
            pool,
            db_path: db_path(),
            sqlcipher_key: "test-key-not-for-production".to_string(),
            credential_key: [7u8; 32],
        }
    }

    pub(crate) fn ctx_arc(pool: maho_core::db::SqlitePool) -> Arc<AppCtx> {
        Arc::new(ctx(pool))
    }

    /// Serializes tests that install the process-global [`AppCtx`] via
    /// [`crate::state::set_ctx`]. That ctx lives behind a single
    /// `RwLock<Option<Arc<AppCtx>>>`, so two such tests running concurrently in
    /// the same test binary can clobber each other's ctx between FFI calls (e.g.
    /// one test's Set-then-Get would read a different pool and see stale/empty
    /// data). Every test that calls `set_ctx` must hold this guard for its whole
    /// body. Poison is recovered because a panicking test only leaves the unit
    /// `()` behind.
    pub(crate) fn global_ctx_guard() -> std::sync::MutexGuard<'static, ()> {
        static GLOBAL_CTX_LOCK: std::sync::OnceLock<std::sync::Mutex<()>> =
            std::sync::OnceLock::new();
        GLOBAL_CTX_LOCK
            .get_or_init(|| std::sync::Mutex::new(()))
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }

    fn db_path() -> PathBuf {
        std::env::temp_dir().join(format!("maho-mail-ffi-test-{}.db", uuid::Uuid::new_v4()))
    }

    // em2.in_reply_to = em1.message_id links them into one thread; accounts
    // carry explicit created_at so list ordering is deterministic.
    fn seed_test_data(conn: &rusqlite::Connection) {
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, password, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)",
            rusqlite::params!["acc1", "user@example.com", "Test User", "imap.example.com", 993, "Tls", "smtp.example.com", 587, "StartTls", "user@example.com", "password", "secret_password_123", "2024-01-01T00:00:00Z"],
        ).expect("seed account 1");
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
            rusqlite::params!["acc2", "other@example.com", "Other User", "imap.other.com", 993, "Tls", "smtp.other.com", 587, "StartTls", "other@example.com", "oauth2", "2024-01-02T00:00:00Z"],
        ).expect("seed account 2");
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params!["fold1", "acc1", "INBOX", "INBOX", "inbox"],
        ).expect("seed folder inbox");
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params!["fold2", "acc1", "Sent", "Sent", "sent"],
        ).expect("seed folder sent");
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, in_reply_to, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, 0, 0, ?14, ?15)",
            rusqlite::params!["em1", "acc1", "fold1", 1_i64, "<msg1@example.com>", Option::<String>::None, "Hello", "sender@example.com", "[\"user@example.com\"]", "2024-01-15T10:00:00Z", "This is a test", false, false, 1024_i64, "2024-01-15T10:00:00Z"],
        ).expect("seed email 1");
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, in_reply_to, subject, from_address, to_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, 0, 0, ?14, ?15)",
            rusqlite::params!["em2", "acc1", "fold1", 2_i64, "<msg2@example.com>", "<msg1@example.com>", "Re: Hello", "user@example.com", "[\"sender@example.com\"]", "2024-01-15T11:00:00Z", "Reply", true, true, 512_i64, "2024-01-15T11:00:00Z"],
        ).expect("seed email 2");
    }
}
