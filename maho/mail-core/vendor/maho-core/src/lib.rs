#![allow(async_fn_in_trait, clippy::uninlined_format_args)]

#[cfg(feature = "legacy-ffi")]
pub mod credential_store;
pub mod db;
pub mod db_path;
pub mod error;
pub mod folder_normalize;
pub mod imap_client;
pub mod models;
pub mod services;
pub mod smtp_client;
pub mod transfer;
// Legacy stub C-ABI, superseded by the real `maho-mail-ffi` crate
// (maho/mail-core). Gated off by default to avoid duplicate `#[no_mangle]`
// `MahoMail*` symbols when maho-core is linked into that real FFI crate.
#[cfg(feature = "legacy-ffi")]
pub mod ffi;

#[cfg(test)]
pub mod test_helpers {
    use rusqlite::Connection;

    pub fn setup_test_db() -> Connection {
        let conn = Connection::open_in_memory().unwrap();
        conn.pragma_update(None, "key", "test-key-not-for-production")
            .unwrap();
        conn.execute_batch("PRAGMA journal_mode=WAL;").unwrap();
        conn.execute_batch("PRAGMA foreign_keys=ON;").unwrap();
        crate::db::migrations::run_migrations(&conn).unwrap();
        conn
    }

    pub fn seed_test_data(conn: &Connection) {
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, password, oauth2_access_token)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)",
            rusqlite::params!["acc1", "user@example.com", "Test User", "imap.example.com", 993, "Tls", "smtp.example.com", 587, "StartTls", "user@example.com", "password", "secret_password_123", Option::<String>::None],
        ).unwrap();

        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, password, oauth2_access_token)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)",
            rusqlite::params!["acc2", "other@example.com", "Other User", "imap.other.com", 993, "Tls", "smtp.other.com", 587, "Tls", "other@example.com", "oauth2", Option::<String>::None, "oauth2_access_token_456"],
        ).unwrap();

        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params!["fold1", "acc1", "INBOX", "INBOX", "inbox"],
        ).unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params!["fold2", "acc1", "Sent", "Sent", "sent"],
        ).unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES (?1, ?2, ?3, ?4, ?5)",
            rusqlite::params!["fold3", "acc2", "INBOX", "INBOX", "inbox"],
        ).unwrap();

        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, in_reply_to, subject, from_address, from_name, to_addresses, cc_addresses, bcc_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22)",
            rusqlite::params!["em1", "acc1", "fold1", 1_i64, "<msg1@example.com>", Option::<String>::None, "Hello World", "sender@example.com", "Sender Name", "[\"user@example.com\"]", Option::<String>::None, Option::<String>::None, "2024-01-15T10:00:00Z", "This is a test email", false, false, false, false, "This is the body of the test email", Option::<String>::None, 1024_i64, "2024-01-15T10:00:00Z"],
        ).unwrap();

        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, in_reply_to, subject, from_address, from_name, to_addresses, cc_addresses, bcc_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22)",
            rusqlite::params!["em2", "acc1", "fold1", 2_i64, "<msg2@example.com>", Some("<msg1@example.com>"), "Re: Hello World", "user@example.com", "Test User", "[\"sender@example.com\"]", Option::<String>::None, Option::<String>::None, "2024-01-15T11:00:00Z", "Reply to the test", true, true, false, false, "This is a reply", Option::<String>::None, 512_i64, "2024-01-15T11:00:00Z"],
        ).unwrap();

        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, in_reply_to, subject, from_address, from_name, to_addresses, cc_addresses, bcc_addresses, date, snippet, is_read, is_starred, is_draft, has_attachments, body_text, body_html, raw_size, body_fetched_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22)",
            rusqlite::params!["em3", "acc1", "fold2", 3_i64, "<msg3@example.com>", Option::<String>::None, "Sent Message", "user@example.com", "Test User", "[\"recipient@example.com\"]", Option::<String>::None, Option::<String>::None, "2024-01-16T09:00:00Z", "A sent message", true, false, false, false, "Sent body text", Option::<String>::None, 768_i64, "2024-01-16T09:00:00Z"],
        ).unwrap();

        conn.execute(
            "INSERT INTO attachments (id, email_id, part_id, filename, mime_type, size, content_id) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            rusqlite::params!["att1", "em1", "1", "report.pdf", "application/pdf", 2048, Option::<String>::None],
        ).unwrap();
    }
}
