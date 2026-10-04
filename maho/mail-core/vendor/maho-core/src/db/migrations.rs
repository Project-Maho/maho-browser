use rusqlite::Connection;

use crate::error::AppError;

#[path = "calendar_date_migration.rs"]
mod calendar_date_migration;
use calendar_date_migration::migrate_v55;

#[cfg(test)]
#[path = "queue_identity_review_tests.rs"]
mod queue_identity_review_tests;

pub fn run_migrations(conn: &Connection) -> Result<(), AppError> {
    let current_version = get_schema_version(conn);
    let mut ver = current_version;

    macro_rules! run_mig {
        ($v:expr, $mig:ident) => {
            if ver < $v {
                conn.execute("BEGIN IMMEDIATE TRANSACTION;", [])
                    .map_err(AppError::Database)?;
                // Re-read the applied version under the write lock. The first
                // read happens before any lock is held, so another connection
                // -- a second mail helper generation, or another member of the
                // same pool -- can apply this step in between; replaying it
                // then fails with "duplicate column name" and aborts startup.
                ver = get_schema_version(conn);
                if ver >= $v {
                    conn.execute("COMMIT TRANSACTION;", [])
                        .map_err(AppError::Database)?;
                } else {
                    match $mig(conn) {
                        Ok(_) => {
                            conn.execute("COMMIT TRANSACTION;", [])
                                .map_err(AppError::Database)?;
                            ver = $v;
                        }
                        Err(e) => {
                            let _ = conn.execute("ROLLBACK TRANSACTION;", []);
                            return Err(e);
                        }
                    }
                }
            }
        };
    }

    run_mig!(1, migrate_v1);
    run_mig!(2, migrate_v2);
    run_mig!(3, migrate_v3);
    run_mig!(4, migrate_v4);
    run_mig!(5, migrate_v5);
    run_mig!(6, migrate_v6);
    run_mig!(7, migrate_v7);
    run_mig!(8, migrate_v8);
    run_mig!(9, migrate_v9);
    run_mig!(10, migrate_v10);
    run_mig!(11, migrate_v11);
    run_mig!(12, migrate_v12);
    run_mig!(13, migrate_v13);
    run_mig!(14, migrate_v14);
    run_mig!(15, migrate_v15);
    run_mig!(16, migrate_v16);
    run_mig!(17, migrate_v17);
    run_mig!(18, migrate_v18);
    run_mig!(19, migrate_v19);
    run_mig!(20, migrate_v20);
    run_mig!(21, migrate_v21);
    run_mig!(22, migrate_v22);
    run_mig!(23, migrate_v23);
    run_mig!(24, migrate_v24);
    run_mig!(25, migrate_v25);
    run_mig!(26, migrate_v26);
    run_mig!(27, migrate_v27);
    run_mig!(28, migrate_v28);
    run_mig!(30, migrate_v30);
    run_mig!(31, migrate_v31);
    run_mig!(32, migrate_v32);
    run_mig!(33, migrate_v33);
    run_mig!(34, migrate_v34);
    run_mig!(35, migrate_v35);
    run_mig!(36, migrate_v36);
    run_mig!(37, migrate_v37);
    run_mig!(38, migrate_v38);
    run_mig!(39, migrate_v39);
    run_mig!(40, migrate_v40);
    run_mig!(41, migrate_v41);
    run_mig!(42, migrate_v42);
    run_mig!(43, migrate_v43);
    run_mig!(44, migrate_v44);
    run_mig!(45, migrate_v45);
    run_mig!(46, migrate_v46);
    run_mig!(47, migrate_v47);
    run_mig!(48, migrate_v48);
    run_mig!(49, migrate_v49);
    run_mig!(50, migrate_v50);
    run_mig!(51, migrate_v51);
    run_mig!(52, migrate_v52);
    run_mig!(53, migrate_v53);
    run_mig!(54, migrate_v54);
    run_mig!(55, migrate_v55);
    run_mig!(56, migrate_v56);
    run_mig!(57, migrate_v57);

    let _ = ver;
    Ok(())
}

fn migrate_v57(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "ALTER TABLE folders ADD COLUMN reconciliation_version INTEGER NOT NULL DEFAULT 0;
         INSERT INTO schema_version(version) VALUES(57);",
    )?;
    Ok(())
}

fn migrate_v56(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "ALTER TABLE folders ADD COLUMN reconciliation_uid INTEGER NOT NULL DEFAULT 0;
         INSERT INTO schema_version(version) VALUES(56);",
    )?;
    Ok(())
}

fn migrate_v54(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "CREATE TABLE pending_mutations_v54 (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            email_uid INTEGER,
            folder_path TEXT,
            mutation_type TEXT NOT NULL CHECK(mutation_type IN (
                'mark_read', 'mark_unread', 'star', 'unstar', 'move', 'delete',
                'calendar_rsvp', 'calendar_rsvp_reply',
                'calendar_create', 'calendar_update', 'calendar_delete'
            )),
            target_folder TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            calendar_event_id TEXT,
            payload_json TEXT,
            UNIQUE(account_id, folder_path, email_uid, mutation_type)
        );
        INSERT INTO pending_mutations_v54
            (id, account_id, email_uid, folder_path, mutation_type, target_folder,
             created_at, calendar_event_id, payload_json)
            SELECT id, account_id, email_uid, folder_path, mutation_type,
                   target_folder, created_at, calendar_event_id, payload_json
            FROM pending_mutations;
        DROP TABLE pending_mutations;
        ALTER TABLE pending_mutations_v54 RENAME TO pending_mutations;
        CREATE INDEX idx_pending_mutations_account ON pending_mutations(account_id);
        CREATE INDEX idx_pending_mutations_cal_event ON pending_mutations(calendar_event_id)
            WHERE calendar_event_id IS NOT NULL;
        CREATE UNIQUE INDEX uniq_pending_mutations_cal
            ON pending_mutations(account_id, calendar_event_id, mutation_type)
            WHERE calendar_event_id IS NOT NULL;
        INSERT INTO schema_version (version) VALUES (54);",
    )?;
    Ok(())
}

fn migrate_v53(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "ALTER TABLE emails ADD COLUMN email_references TEXT;
         ALTER TABLE emails ADD COLUMN read_receipt INTEGER;
         INSERT INTO schema_version (version) VALUES (53);",
    )?;
    Ok(())
}

fn get_schema_version(conn: &Connection) -> i64 {
    conn.query_row(
        "SELECT COALESCE(MAX(version), 0) FROM schema_version",
        [],
        |row| row.get(0),
    )
    .unwrap_or(0)
}

fn migrate_v1(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER PRIMARY KEY,
            applied_at TEXT NOT NULL DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS accounts (
            id TEXT PRIMARY KEY,
            email TEXT NOT NULL UNIQUE,
            display_name TEXT NOT NULL DEFAULT '',
            imap_host TEXT NOT NULL,
            imap_port INTEGER NOT NULL DEFAULT 993,
            imap_encryption TEXT NOT NULL DEFAULT 'tls',
            smtp_host TEXT NOT NULL,
            smtp_port INTEGER NOT NULL DEFAULT 587,
            smtp_encryption TEXT NOT NULL DEFAULT 'starttls',
            username TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS folders (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            name TEXT NOT NULL,
            path TEXT NOT NULL,
            folder_type TEXT NOT NULL DEFAULT 'custom',
            unread_count INTEGER NOT NULL DEFAULT 0,
            total_count INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS emails (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            folder_id TEXT NOT NULL,
            uid INTEGER NOT NULL DEFAULT 0,
            message_id TEXT,
            in_reply_to TEXT,
            subject TEXT NOT NULL DEFAULT '',
            from_address TEXT NOT NULL DEFAULT '',
            from_name TEXT,
            to_addresses TEXT NOT NULL DEFAULT '[]',
            cc_addresses TEXT,
            bcc_addresses TEXT,
            date TEXT NOT NULL,
            snippet TEXT NOT NULL DEFAULT '',
            is_read INTEGER NOT NULL DEFAULT 0,
            is_starred INTEGER NOT NULL DEFAULT 0,
            is_draft INTEGER NOT NULL DEFAULT 0,
            has_attachments INTEGER NOT NULL DEFAULT 0,
            body_text TEXT,
            body_html TEXT,
            raw_size INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
            FOREIGN KEY (folder_id) REFERENCES folders(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_emails_account_folder ON emails(account_id, folder_id);
        CREATE INDEX IF NOT EXISTS idx_emails_date ON emails(date DESC);
        CREATE INDEX IF NOT EXISTS idx_emails_message_id ON emails(message_id);
        CREATE UNIQUE INDEX IF NOT EXISTS idx_emails_folder_uid ON emails(folder_id, uid);

        CREATE TABLE IF NOT EXISTS attachments (
            id TEXT PRIMARY KEY,
            email_id TEXT NOT NULL,
            part_id TEXT NOT NULL DEFAULT '',
            filename TEXT,
            mime_type TEXT NOT NULL DEFAULT 'application/octet-stream',
            size INTEGER NOT NULL DEFAULT 0,
            content_id TEXT,
            FOREIGN KEY (email_id) REFERENCES emails(id) ON DELETE CASCADE,
            UNIQUE(email_id, part_id)
        );

        CREATE TABLE IF NOT EXISTS ai_configs (
            id TEXT PRIMARY KEY,
            provider TEXT NOT NULL,
            model TEXT NOT NULL,
            base_url TEXT,
            feature TEXT NOT NULL DEFAULT 'default',
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        );

        CREATE UNIQUE INDEX IF NOT EXISTS idx_ai_configs_feature ON ai_configs(feature);

        CREATE VIRTUAL TABLE IF NOT EXISTS emails_fts USING fts5(
            subject,
            from_address,
            from_name,
            snippet,
            body_text,
            content='emails',
            content_rowid='rowid'
        );

        CREATE TRIGGER IF NOT EXISTS emails_ai AFTER INSERT ON emails BEGIN
            INSERT INTO emails_fts(rowid, subject, from_address, from_name, snippet, body_text)
            VALUES (NEW.rowid, NEW.subject, NEW.from_address, NEW.from_name, NEW.snippet, NEW.body_text);
        END;

        CREATE TRIGGER IF NOT EXISTS emails_ad AFTER DELETE ON emails BEGIN
            INSERT INTO emails_fts(emails_fts, rowid, subject, from_address, from_name, snippet, body_text)
            VALUES ('delete', OLD.rowid, OLD.subject, OLD.from_address, OLD.from_name, OLD.snippet, OLD.body_text);
        END;

        CREATE TRIGGER IF NOT EXISTS emails_au AFTER UPDATE ON emails BEGIN
            INSERT INTO emails_fts(emails_fts, rowid, subject, from_address, from_name, snippet, body_text)
            VALUES ('delete', OLD.rowid, OLD.subject, OLD.from_address, OLD.from_name, OLD.snippet, OLD.body_text);
            INSERT INTO emails_fts(rowid, subject, from_address, from_name, snippet, body_text)
            VALUES (NEW.rowid, NEW.subject, NEW.from_address, NEW.from_name, NEW.snippet, NEW.body_text);
        END;

        INSERT OR IGNORE INTO schema_version (version) VALUES (1);
        ",
    )?;

    Ok(())
}

fn migrate_v2(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        ALTER TABLE accounts ADD COLUMN auth_type TEXT NOT NULL DEFAULT 'password';
        ALTER TABLE accounts ADD COLUMN oauth2_client_id TEXT;
        ALTER TABLE accounts ADD COLUMN oauth2_client_secret TEXT;
        ALTER TABLE accounts ADD COLUMN oauth2_refresh_token TEXT;

        INSERT OR IGNORE INTO schema_version (version) VALUES (2);
        ",
    )?;

    Ok(())
}

fn migrate_v3(conn: &Connection) -> Result<(), AppError> {
    // Add body_fetched_at column to track explicit body fetch state
    // This enables background backfill of email bodies without blocking sync
    conn.execute_batch(
        "
        ALTER TABLE emails ADD COLUMN body_fetched_at TEXT;

        -- Create index for efficient queries of emails needing body fetch
        CREATE INDEX IF NOT EXISTS idx_emails_body_fetched_at ON emails(body_fetched_at);

        -- Backfill: set body_fetched_at for emails that already have body content
        UPDATE emails SET body_fetched_at = created_at 
        WHERE body_text IS NOT NULL OR body_html IS NOT NULL;

        INSERT OR IGNORE INTO schema_version (version) VALUES (3);
        ",
    )?;

    Ok(())
}

fn migrate_v4(conn: &Connection) -> Result<(), AppError> {
    // Add password and oauth2_access_token columns to accounts table
    // These secrets are now stored in the database instead of the system keyring
    conn.execute_batch(
        "
        ALTER TABLE accounts ADD COLUMN password TEXT;
        ALTER TABLE accounts ADD COLUMN oauth2_access_token TEXT;

        INSERT OR IGNORE INTO schema_version (version) VALUES (4);
        ",
    )?;

    Ok(())
}

fn migrate_v5(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        ALTER TABLE accounts ADD COLUMN oauth2_expires_at TEXT;

        INSERT OR IGNORE INTO schema_version (version) VALUES (5);
        ",
    )?;

    Ok(())
}

fn migrate_v6(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        -- New columns on emails table
        ALTER TABLE emails ADD COLUMN is_pinned INTEGER NOT NULL DEFAULT 0;
        ALTER TABLE emails ADD COLUMN snoozed_until TEXT;
        ALTER TABLE emails ADD COLUMN reminder_at TEXT;
        ALTER TABLE emails ADD COLUMN smart_category TEXT;

        CREATE INDEX IF NOT EXISTS idx_emails_is_pinned ON emails(is_pinned) WHERE is_pinned = 1;
        CREATE INDEX IF NOT EXISTS idx_emails_snoozed ON emails(snoozed_until) WHERE snoozed_until IS NOT NULL;
        CREATE INDEX IF NOT EXISTS idx_emails_reminder ON emails(reminder_at) WHERE reminder_at IS NOT NULL;
        CREATE INDEX IF NOT EXISTS idx_emails_smart_category ON emails(smart_category);

        -- Contacts table for autocomplete and VIP
        CREATE TABLE IF NOT EXISTS contacts (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            email TEXT NOT NULL,
            name TEXT,
            frequency INTEGER NOT NULL DEFAULT 1,
            last_contacted_at TEXT,
            is_vip INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
            UNIQUE(account_id, email)
        );
        CREATE INDEX IF NOT EXISTS idx_contacts_account_email ON contacts(account_id, email);
        CREATE INDEX IF NOT EXISTS idx_contacts_frequency ON contacts(frequency DESC);

        -- Signatures table
        CREATE TABLE IF NOT EXISTS signatures (
            id TEXT PRIMARY KEY,
            account_id TEXT,
            name TEXT NOT NULL,
            body_html TEXT NOT NULL DEFAULT '',
            body_text TEXT NOT NULL DEFAULT '',
            is_default INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );

        -- Email templates table
        CREATE TABLE IF NOT EXISTS email_templates (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            subject TEXT NOT NULL DEFAULT '',
            body_html TEXT NOT NULL DEFAULT '',
            body_text TEXT NOT NULL DEFAULT '',
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        );

        -- Labels table
        CREATE TABLE IF NOT EXISTS labels (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            name TEXT NOT NULL,
            color TEXT NOT NULL DEFAULT '#3b82f6',
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
            UNIQUE(account_id, name)
        );

        -- Email-labels join table
        CREATE TABLE IF NOT EXISTS email_labels (
            email_id TEXT NOT NULL,
            label_id TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            PRIMARY KEY (email_id, label_id),
            FOREIGN KEY (email_id) REFERENCES emails(id) ON DELETE CASCADE,
            FOREIGN KEY (label_id) REFERENCES labels(id) ON DELETE CASCADE
        );

        -- Send later queue
        CREATE TABLE IF NOT EXISTS send_later (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            to_addresses TEXT NOT NULL DEFAULT '[]',
            cc_addresses TEXT,
            bcc_addresses TEXT,
            subject TEXT NOT NULL DEFAULT '',
            body_html TEXT,
            body_text TEXT,
            scheduled_at TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'pending',
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_send_later_scheduled ON send_later(scheduled_at, status);

        INSERT OR IGNORE INTO schema_version (version) VALUES (6);
        ",
    )?;

    Ok(())
}

fn migrate_v7(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        ALTER TABLE emails ADD COLUMN mdn_requested TEXT;

        INSERT OR IGNORE INTO schema_version (version) VALUES (7);
        ",
    )?;

    Ok(())
}

fn migrate_v8(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS encrypted_credentials (
            account_id TEXT NOT NULL,
            credential_type TEXT NOT NULL,
            encrypted_value BLOB NOT NULL,
            nonce BLOB NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now')),
            PRIMARY KEY (account_id, credential_type)
        );

        INSERT OR IGNORE INTO schema_version (version) VALUES (8);
        ",
    )?;

    Ok(())
}

fn migrate_v9(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        ALTER TABLE folders ADD COLUMN last_synced_uid INTEGER NOT NULL DEFAULT 0;
        ALTER TABLE folders ADD COLUMN uid_validity INTEGER NOT NULL DEFAULT 0;

        INSERT OR IGNORE INTO schema_version (version) VALUES (9);
        ",
    )?;

    Ok(())
}

fn migrate_v10(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS ai_action_log (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            email_id TEXT,
            action_type TEXT NOT NULL,
            provider TEXT NOT NULL,
            model TEXT,
            input_summary TEXT,
            output_summary TEXT,
            status TEXT NOT NULL DEFAULT 'success',
            error_message TEXT,
            tokens_used INTEGER,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
        CREATE INDEX IF NOT EXISTS idx_ai_action_log_account ON ai_action_log(account_id);
        CREATE INDEX IF NOT EXISTS idx_ai_action_log_created ON ai_action_log(created_at);

        INSERT OR IGNORE INTO schema_version (version) VALUES (10);
        ",
    )?;

    Ok(())
}

fn migrate_v11(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS mail_rules (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            name TEXT NOT NULL,
            priority INTEGER NOT NULL DEFAULT 0,
            is_enabled INTEGER NOT NULL DEFAULT 1,
            conditions_json TEXT NOT NULL DEFAULT '[]',
            actions_json TEXT NOT NULL DEFAULT '[]',
            stop_processing INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_mail_rules_account ON mail_rules(account_id, priority);

        CREATE TABLE IF NOT EXISTS outbox (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            to_addresses TEXT NOT NULL DEFAULT '[]',
            cc_addresses TEXT,
            bcc_addresses TEXT,
            subject TEXT NOT NULL DEFAULT '',
            body_html TEXT,
            body_text TEXT,
            in_reply_to TEXT,
            attachments_json TEXT,
            status TEXT NOT NULL DEFAULT 'queued',
            retry_count INTEGER NOT NULL DEFAULT 0,
            max_retries INTEGER NOT NULL DEFAULT 3,
            last_error TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_outbox_status ON outbox(status);

        INSERT OR IGNORE INTO schema_version (version) VALUES (11);
        ",
    )?;

    Ok(())
}

fn migrate_v12(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS calendar_events (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            email_id TEXT,
            uid TEXT NOT NULL,
            summary TEXT NOT NULL DEFAULT '',
            description TEXT,
            dtstart TEXT NOT NULL,
            dtend TEXT,
            location TEXT,
            organizer TEXT,
            status TEXT NOT NULL DEFAULT 'confirmed',
            rsvp_status TEXT,
            recurrence_rule TEXT,
            all_day INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_calendar_events_account ON calendar_events(account_id, dtstart);
        CREATE INDEX IF NOT EXISTS idx_calendar_events_uid ON calendar_events(uid);

        INSERT OR IGNORE INTO schema_version (version) VALUES (12);
        ",
    )?;

    Ok(())
}

fn migrate_v13(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS pgp_keys (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            email TEXT NOT NULL,
            key_type TEXT NOT NULL,
            fingerprint TEXT NOT NULL,
            key_data BLOB NOT NULL,
            is_private INTEGER NOT NULL DEFAULT 0,
            is_default INTEGER NOT NULL DEFAULT 0,
            expires_at TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_pgp_keys_email ON pgp_keys(email);
        CREATE INDEX IF NOT EXISTS idx_pgp_keys_fingerprint ON pgp_keys(fingerprint);

        INSERT OR IGNORE INTO schema_version (version) VALUES (13);
        ",
    )?;

    Ok(())
}

fn migrate_v14(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS muted_threads (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            thread_id TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            UNIQUE(account_id, thread_id)
        );
        CREATE INDEX IF NOT EXISTS idx_muted_threads_account ON muted_threads(account_id, thread_id);

        INSERT OR IGNORE INTO schema_version (version) VALUES (14);
        ",
    )?;

    Ok(())
}

fn migrate_v15(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS saved_searches (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            query TEXT NOT NULL,
            account_id TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        );

        INSERT OR IGNORE INTO schema_version (version) VALUES (15);
        ",
    )?;

    Ok(())
}

fn migrate_v16(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS smime_identities (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            email TEXT NOT NULL,
            subject TEXT NOT NULL,
            issuer TEXT NOT NULL,
            serial_number TEXT NOT NULL,
            fingerprint TEXT NOT NULL,
            not_before TEXT NOT NULL,
            not_after TEXT NOT NULL,
            cert_pem TEXT NOT NULL,
            is_default INTEGER NOT NULL DEFAULT 0,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            UNIQUE(account_id, fingerprint),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_smime_identities_account ON smime_identities(account_id);
        CREATE INDEX IF NOT EXISTS idx_smime_identities_email ON smime_identities(email);

        INSERT OR IGNORE INTO schema_version (version) VALUES (16);
        ",
    )?;

    Ok(())
}

fn migrate_v17(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        -- Performance indexes for frequently-queried email columns
        CREATE INDEX IF NOT EXISTS idx_emails_is_read ON emails(account_id, is_read);
        CREATE INDEX IF NOT EXISTS idx_emails_is_starred ON emails(is_starred) WHERE is_starred = 1;
        CREATE INDEX IF NOT EXISTS idx_emails_in_reply_to ON emails(in_reply_to) WHERE in_reply_to IS NOT NULL;
        CREATE INDEX IF NOT EXISTS idx_emails_folder_id ON emails(folder_id);
        CREATE INDEX IF NOT EXISTS idx_emails_from_address ON emails(from_address);

        INSERT OR IGNORE INTO schema_version (version) VALUES (17);
        ",
    )?;

    Ok(())
}

fn migrate_v18(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS pending_mutations (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            email_uid INTEGER NOT NULL,
            folder_path TEXT NOT NULL,
            mutation_type TEXT NOT NULL CHECK(mutation_type IN ('mark_read', 'mark_unread', 'star', 'unstar', 'move', 'delete')),
            target_folder TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            UNIQUE(account_id, email_uid, mutation_type)
        );
        CREATE INDEX IF NOT EXISTS idx_pending_mutations_account ON pending_mutations(account_id);

        INSERT OR IGNORE INTO schema_version (version) VALUES (18);
        ",
    )?;

    Ok(())
}

fn migrate_v19(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS contact_groups (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            name TEXT NOT NULL,
            member_emails_json TEXT NOT NULL DEFAULT '[]',
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
        CREATE INDEX IF NOT EXISTS idx_contact_groups_account ON contact_groups(account_id);

        INSERT OR IGNORE INTO schema_version (version) VALUES (19);
        ",
    )?;

    Ok(())
}

fn migrate_v20(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        ALTER TABLE outbox ADD COLUMN read_receipt INTEGER;
        INSERT OR IGNORE INTO schema_version (version) VALUES (20);
        ",
    )?;

    Ok(())
}

fn migrate_v21(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        ALTER TABLE outbox ADD COLUMN email_references TEXT;
        INSERT OR IGNORE INTO schema_version (version) VALUES (21);
        ",
    )?;

    Ok(())
}

fn migrate_v22(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        ALTER TABLE send_later ADD COLUMN attachments_json TEXT;
        ALTER TABLE send_later ADD COLUMN read_receipt INTEGER;
        ALTER TABLE send_later ADD COLUMN in_reply_to TEXT;
        ALTER TABLE send_later ADD COLUMN email_references TEXT;
        INSERT OR IGNORE INTO schema_version (version) VALUES (22);
        ",
    )?;

    Ok(())
}

fn migrate_v23(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "ALTER TABLE emails ADD COLUMN draft_attachments_json TEXT;
         INSERT OR IGNORE INTO schema_version (version) VALUES (23);",
    )?;

    Ok(())
}

fn migrate_v24(conn: &Connection) -> Result<(), AppError> {
    // Add feature column to ai_configs for multi-config support (agent vs translation)
    let cols: Vec<String> = conn
        .prepare("PRAGMA table_info(ai_configs)")?
        .query_map([], |row| row.get::<_, String>(1))?
        .filter_map(|r| r.ok())
        .collect();

    if !cols.contains(&"feature".to_string()) {
        conn.execute(
            "ALTER TABLE ai_configs ADD COLUMN feature TEXT NOT NULL DEFAULT 'default'",
            [],
        )?;
    }

    conn.execute(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_ai_configs_feature ON ai_configs(feature)",
        [],
    )?;

    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (24);")?;

    Ok(())
}

fn migrate_v25(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS agent_sessions (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            title TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS agent_messages (
            id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(session_id) REFERENCES agent_sessions(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS auto_drafts (
            id TEXT PRIMARY KEY,
            email_id TEXT NOT NULL UNIQUE,
            draft_content TEXT NOT NULL,
            status TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(email_id) REFERENCES emails(id) ON DELETE CASCADE
        );

        INSERT OR IGNORE INTO schema_version (version) VALUES (25);
        ",
    )?;

    Ok(())
}

fn migrate_v26(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS calendar_sync_state (
            account_id TEXT NOT NULL,
            calendar_id TEXT NOT NULL,
            sync_token TEXT,
            last_synced_at TEXT,
            last_error TEXT,
            PRIMARY KEY (account_id, calendar_id),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );",
    )?;

    let _ = conn.execute_batch(
        "ALTER TABLE calendar_events ADD COLUMN source TEXT NOT NULL DEFAULT 'local';",
    );
    let _ = conn.execute_batch("ALTER TABLE calendar_events ADD COLUMN external_id TEXT;");

    conn.execute_batch(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_calendar_events_external
            ON calendar_events(account_id, source, external_id)
            WHERE external_id IS NOT NULL;

        INSERT OR IGNORE INTO schema_version (version) VALUES (26);",
    )?;

    Ok(())
}

fn migrate_v27(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS app_settings (
            key TEXT PRIMARY KEY NOT NULL,
            value TEXT NOT NULL,
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        );

        INSERT OR IGNORE INTO schema_version (version) VALUES (27);",
    )?;
    Ok(())
}

fn migrate_v28(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS google_calendars (
            id TEXT PRIMARY KEY NOT NULL,
            account_id TEXT NOT NULL,
            calendar_id TEXT NOT NULL,
            summary TEXT NOT NULL,
            background_color TEXT,
            foreground_color TEXT,
            is_primary INTEGER NOT NULL DEFAULT 0,
            access_role TEXT,
            visible INTEGER NOT NULL DEFAULT 1,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            UNIQUE (account_id, calendar_id),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_google_calendars_account
            ON google_calendars(account_id);",
    )?;

    let _ = conn.execute_batch("ALTER TABLE calendar_events ADD COLUMN google_calendar_id TEXT;");

    conn.execute_batch(
        "CREATE INDEX IF NOT EXISTS idx_calendar_events_google_cal
            ON calendar_events(google_calendar_id)
            WHERE google_calendar_id IS NOT NULL;

        DELETE FROM calendar_sync_state;

        INSERT OR IGNORE INTO schema_version (version) VALUES (28);",
    )?;

    Ok(())
}

/// v30: Repeat v28's schema setup using a fresh version number to recover from
/// installs whose `schema_version` already advanced past 28 via unrelated
/// parallel branches (e.g. audit/billing/routines workspaces). The body is
/// idempotent (`CREATE TABLE IF NOT EXISTS`, tolerant `ALTER`) so it is safe to
/// run even if v28 actually applied. Skips the destructive
/// `DELETE FROM calendar_sync_state` only when the Google calendar columns
/// already exist (i.e. true v28 actually ran).
fn migrate_v30(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS google_calendars (
            id TEXT PRIMARY KEY NOT NULL,
            account_id TEXT NOT NULL,
            calendar_id TEXT NOT NULL,
            summary TEXT NOT NULL,
            background_color TEXT,
            foreground_color TEXT,
            is_primary INTEGER NOT NULL DEFAULT 0,
            access_role TEXT,
            visible INTEGER NOT NULL DEFAULT 1,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            UNIQUE (account_id, calendar_id),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_google_calendars_account
            ON google_calendars(account_id);",
    )?;

    let had_column: bool = conn
        .prepare(
            "SELECT 1 FROM pragma_table_info('calendar_events') WHERE name = 'google_calendar_id'",
        )
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);

    if !had_column {
        let _ =
            conn.execute_batch("ALTER TABLE calendar_events ADD COLUMN google_calendar_id TEXT;");
        conn.execute_batch("DELETE FROM calendar_sync_state;")?;
    }

    conn.execute_batch(
        "CREATE INDEX IF NOT EXISTS idx_calendar_events_google_cal
            ON calendar_events(google_calendar_id)
            WHERE google_calendar_id IS NOT NULL;

        INSERT OR IGNORE INTO schema_version (version) VALUES (30);",
    )?;

    Ok(())
}

fn migrate_v38(conn: &Connection) -> Result<(), AppError> {
    let _ = conn.execute_batch("ALTER TABLE pending_mutations ADD COLUMN calendar_event_id TEXT;");
    let _ = conn.execute_batch("ALTER TABLE pending_mutations ADD COLUMN payload_json TEXT;");

    conn.execute_batch(
        "CREATE INDEX IF NOT EXISTS idx_pending_mutations_cal_event
            ON pending_mutations(calendar_event_id)
            WHERE calendar_event_id IS NOT NULL;

        CREATE UNIQUE INDEX IF NOT EXISTS uniq_pending_mutations_cal
            ON pending_mutations(account_id, calendar_event_id, mutation_type)
            WHERE calendar_event_id IS NOT NULL;

        INSERT OR IGNORE INTO schema_version (version) VALUES (38);",
    )?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusqlite::Connection;

    fn setup_test_db() -> Connection {
        let conn = Connection::open_in_memory().unwrap();
        conn.pragma_update(None, "key", "test-key-not-for-production")
            .unwrap();
        conn.execute_batch("PRAGMA journal_mode=WAL;").unwrap();
        conn.execute_batch("PRAGMA foreign_keys=ON;").unwrap();
        run_migrations(&conn).unwrap();
        conn
    }

    #[test]
    fn test_v4_migration_adds_password_column() {
        let conn = setup_test_db();

        // Verify password column exists
        let col_count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM pragma_table_info('accounts') WHERE name = 'password'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(col_count, 1, "password column should exist");
    }

    #[test]
    fn test_v4_migration_adds_oauth2_access_token_column() {
        let conn = setup_test_db();

        // Verify oauth2_access_token column exists
        let col_count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM pragma_table_info('accounts') WHERE name = 'oauth2_access_token'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(col_count, 1, "oauth2_access_token column should exist");
    }

    #[test]
    fn test_password_storage_and_retrieval() {
        let conn = setup_test_db();

        // Insert account with password
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, password)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
            rusqlite::params![
                "acc1",
                "test@example.com",
                "Test User",
                "imap.example.com",
                993,
                "Tls",
                "smtp.example.com",
                587,
                "Tls",
                "test@example.com",
                "password",
                "secret_password_123"
            ],
        ).unwrap();

        // Retrieve password
        let password: Option<String> = conn
            .query_row(
                "SELECT password FROM accounts WHERE id = ?1",
                ["acc1"],
                |row| row.get(0),
            )
            .unwrap();

        assert_eq!(password, Some("secret_password_123".to_string()));
    }

    #[test]
    fn test_oauth2_access_token_storage_and_retrieval() {
        let conn = setup_test_db();

        // Insert account with OAuth2 access token
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, oauth2_access_token, oauth2_refresh_token, oauth2_client_id)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)",
            rusqlite::params![
                "acc2",
                "oauth@example.com",
                "OAuth User",
                "imap.gmail.com",
                993,
                "Tls",
                "smtp.gmail.com",
                587,
                "Tls",
                "oauth@example.com",
                "oauth2_gmail",
                "ya29.access_token_secret",
                "refresh_token_secret",
                "client_id_123"
            ],
        ).unwrap();

        // Retrieve access token
        let access_token: Option<String> = conn
            .query_row(
                "SELECT oauth2_access_token FROM accounts WHERE id = ?1",
                ["acc2"],
                |row| row.get(0),
            )
            .unwrap();

        assert_eq!(access_token, Some("ya29.access_token_secret".to_string()));
    }

    #[test]
    fn test_null_secrets_allowed() {
        let conn = setup_test_db();

        // Insert account without secrets (e.g., during initial setup)
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, password, oauth2_access_token)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)",
            rusqlite::params![
                "acc3",
                "empty@example.com",
                "Empty User",
                "imap.example.com",
                993,
                "Tls",
                "smtp.example.com",
                587,
                "Tls",
                "empty@example.com",
                "password",
                Option::<String>::None,
                Option::<String>::None
            ],
        ).unwrap();

        // Verify NULL values
        let (password, access_token): (Option<String>, Option<String>) = conn
            .query_row(
                "SELECT password, oauth2_access_token FROM accounts WHERE id = ?1",
                ["acc3"],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .unwrap();

        assert!(password.is_none());
        assert!(access_token.is_none());
    }

    #[test]
    fn test_secrets_preserved_on_account_update() {
        let conn = setup_test_db();

        // Insert account with password
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, password)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
            rusqlite::params![
                "acc4",
                "update@example.com",
                "Update User",
                "imap.example.com",
                993,
                "Tls",
                "smtp.example.com",
                587,
                "Tls",
                "update@example.com",
                "password",
                "original_password"
            ],
        ).unwrap();

        // Update display name (not password)
        conn.execute(
            "UPDATE accounts SET display_name = ?1 WHERE id = ?2",
            rusqlite::params!["Updated Name", "acc4"],
        )
        .unwrap();

        // Verify password is preserved
        let password: Option<String> = conn
            .query_row(
                "SELECT password FROM accounts WHERE id = ?1",
                ["acc4"],
                |row| row.get(0),
            )
            .unwrap();

        assert_eq!(password, Some("original_password".to_string()));
    }

    #[test]
    fn test_v17_migration_adds_performance_indexes() {
        let conn = setup_test_db();

        // Helper function to check if index exists
        let index_exists = |idx_name: &str| -> bool {
            conn.query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = ?1",
                [idx_name],
                |row| row.get::<_, i64>(0),
            )
            .unwrap()
                > 0
        };

        // Verify all v17 indexes exist
        assert!(
            index_exists("idx_emails_is_read"),
            "idx_emails_is_read index should exist"
        );
        assert!(
            index_exists("idx_emails_is_starred"),
            "idx_emails_is_starred index should exist"
        );
        assert!(
            index_exists("idx_emails_in_reply_to"),
            "idx_emails_in_reply_to index should exist"
        );
        assert!(
            index_exists("idx_emails_folder_id"),
            "idx_emails_folder_id index should exist"
        );
        assert!(
            index_exists("idx_emails_from_address"),
            "idx_emails_from_address index should exist"
        );
    }

    #[test]
    fn test_v18_migration_creates_pending_mutations_table() {
        let conn = setup_test_db();

        // Verify pending_mutations table exists
        let table_exists: bool = conn
            .query_row(
                "SELECT COUNT(*) > 0 FROM sqlite_master WHERE type = 'table' AND name = 'pending_mutations'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert!(table_exists, "pending_mutations table should exist");

        // Verify UNIQUE constraint works via INSERT OR REPLACE
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES ('acc1', 'test@test.com', 'Test', 'imap.test.com', 993, 'Tls', 'smtp.test.com', 587, 'Tls', 'test@test.com')",
            [],
        ).unwrap();

        conn.execute(
            "INSERT OR REPLACE INTO pending_mutations (id, account_id, email_uid, folder_path, mutation_type)
             VALUES ('mut1', 'acc1', 100, 'INBOX', 'star')",
            [],
        ).unwrap();

        conn.execute(
            "INSERT OR REPLACE INTO pending_mutations (id, account_id, email_uid, folder_path, mutation_type)
             VALUES ('mut2', 'acc1', 100, 'INBOX', 'star')",
            [],
        ).unwrap();

        let count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM pending_mutations WHERE account_id = 'acc1' AND email_uid = 100 AND mutation_type = 'star'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(
            count, 1,
            "UNIQUE constraint should keep only 1 row per (account_id, email_uid, mutation_type)"
        );

        // Verify CHECK constraint rejects invalid mutation_type
        let invalid_result = conn.execute(
            "INSERT INTO pending_mutations (id, account_id, email_uid, folder_path, mutation_type)
             VALUES ('mut3', 'acc1', 101, 'INBOX', 'invalid_type')",
            [],
        );
        assert!(
            invalid_result.is_err(),
            "CHECK constraint should reject invalid mutation_type"
        );
    }

    #[test]
    fn test_v22_migration_adds_send_later_columns() {
        let conn = setup_test_db();

        let col_exists = |col: &str| -> bool {
            conn.query_row(
                "SELECT COUNT(*) FROM pragma_table_info('send_later') WHERE name = ?1",
                [col],
                |row| row.get::<_, i64>(0),
            )
            .unwrap()
                > 0
        };

        assert!(
            col_exists("attachments_json"),
            "attachments_json column should exist"
        );
        assert!(
            col_exists("read_receipt"),
            "read_receipt column should exist"
        );
        assert!(col_exists("in_reply_to"), "in_reply_to column should exist");
        assert!(
            col_exists("email_references"),
            "email_references column should exist"
        );
    }

    #[test]
    fn test_send_later_roundtrip_with_new_fields() {
        let conn = setup_test_db();

        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES ('acc1', 'test@example.com', 'Test', 'imap.example.com', 993, 'Tls', 'smtp.example.com', 587, 'Tls', 'test@example.com')",
            [],
        ).unwrap();

        let attachments_json =
            r#"[{"filename":"file.pdf","mime_type":"application/pdf","data":"AAAA"}]"#;
        let in_reply_to = "<original@example.com>";
        let email_references = "<ref1@example.com> <ref2@example.com>";

        conn.execute(
            "INSERT INTO send_later (id, account_id, to_addresses, subject, scheduled_at, attachments_json, read_receipt, in_reply_to, email_references)
             VALUES ('sl1', 'acc1', '[\"to@example.com\"]', 'Hello', '2099-01-01T00:00:00Z', ?1, 1, ?2, ?3)",
            rusqlite::params![attachments_json, in_reply_to, email_references],
        ).unwrap();

        let (att_json, rr, irt, refs): (Option<String>, Option<i64>, Option<String>, Option<String>) = conn
            .query_row(
                "SELECT attachments_json, read_receipt, in_reply_to, email_references FROM send_later WHERE id = 'sl1'",
                [],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?)),
            )
            .unwrap();

        assert_eq!(att_json.as_deref(), Some(attachments_json));
        assert_eq!(rr, Some(1));
        assert_eq!(irt.as_deref(), Some(in_reply_to));
        assert_eq!(refs.as_deref(), Some(email_references));

        let parsed: Vec<serde_json::Value> =
            serde_json::from_str(att_json.unwrap().as_str()).unwrap();
        assert_eq!(parsed.len(), 1);
        assert_eq!(parsed[0]["filename"], "file.pdf");
    }

    #[test]
    fn test_v23_migration_adds_draft_attachments_json_column() {
        let conn = setup_test_db();

        let col_count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM pragma_table_info('emails') WHERE name = 'draft_attachments_json'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(
            col_count, 1,
            "draft_attachments_json column should exist on emails table"
        );
    }

    #[test]
    fn test_v23_safe_for_existing_rows() {
        let conn = setup_test_db();

        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES ('acc1', 'test@example.com', 'Test', 'imap.example.com', 993, 'Tls', 'smtp.example.com', 587, 'Tls', 'test@example.com')",
            [],
        ).unwrap();

        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type)
             VALUES ('folder1', 'acc1', 'Drafts', 'Drafts', 'drafts')",
            [],
        )
        .unwrap();

        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, subject, from_address, to_addresses, date, snippet, is_draft)
             VALUES ('email1', 'acc1', 'folder1', 'Draft Email', '', '[]', '2024-01-01 00:00:00', 'test', 1)",
            [],
        ).unwrap();

        let draft_attachments: Option<String> = conn
            .query_row(
                "SELECT draft_attachments_json FROM emails WHERE id = 'email1'",
                [],
                |row| row.get(0),
            )
            .unwrap();

        assert!(
            draft_attachments.is_none(),
            "Existing rows should have NULL for draft_attachments_json after v23 migration"
        );
    }

    #[test]
    fn test_v31_unescapes_imap_quoted_subject_and_addresses() {
        let conn = setup_test_db();

        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES ('acc1', 'test@example.com', 'Test', 'imap.example.com', 993, 'Tls', 'smtp.example.com', 587, 'Tls', 'test@example.com')",
            [],
        ).unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type)
             VALUES ('folder1', 'acc1', 'INBOX', 'INBOX', 'inbox')",
            [],
        )
        .unwrap();
        conn.execute(
            r#"INSERT INTO emails (id, account_id, folder_id, subject, from_name, from_address, to_addresses, cc_addresses, bcc_addresses, date, snippet, is_draft)
               VALUES ('e1', 'acc1', 'folder1', 'Ordered: \"Ikari Drain Cleaner\"', 'Foo \"Bar\" Baz', 'foo@example.com', 'a@b.com', 'c@d.com', 'e@f.com', '2024-01-01', 's', 0)"#,
            [],
        ).unwrap();

        migrate_v31(&conn).unwrap();

        let subject: String = conn
            .query_row("SELECT subject FROM emails WHERE id = 'e1'", [], |row| {
                row.get(0)
            })
            .unwrap();
        let from_name: String = conn
            .query_row("SELECT from_name FROM emails WHERE id = 'e1'", [], |row| {
                row.get(0)
            })
            .unwrap();

        assert_eq!(subject, r#"Ordered: "Ikari Drain Cleaner""#);
        assert_eq!(from_name, r#"Foo "Bar" Baz"#);

        let version: i64 = conn
            .query_row("SELECT MAX(version) FROM schema_version", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert!(version >= 31);
    }

    #[test]
    fn test_v31_preserves_clean_subjects() {
        let conn = setup_test_db();

        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES ('acc1', 'test@example.com', 'Test', 'imap.example.com', 993, 'Tls', 'smtp.example.com', 587, 'Tls', 'test@example.com')",
            [],
        ).unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type)
             VALUES ('folder1', 'acc1', 'INBOX', 'INBOX', 'inbox')",
            [],
        )
        .unwrap();
        conn.execute(
            r#"INSERT INTO emails (id, account_id, folder_id, subject, from_name, from_address, to_addresses, date, snippet, is_draft)
               VALUES ('e1', 'acc1', 'folder1', 'Hello World', 'Alice', 'a@e.com', '[]', '2024-01-01', 's', 0)"#,
            [],
        ).unwrap();

        migrate_v31(&conn).unwrap();

        let subject: String = conn
            .query_row("SELECT subject FROM emails WHERE id = 'e1'", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert_eq!(subject, "Hello World");
    }

    #[test]
    fn test_migrate_v49_adds_retry_columns() {
        let conn = setup_test_db();

        let has_retry_count: bool = conn
            .prepare("SELECT 1 FROM pragma_table_info('send_later') WHERE name = 'retry_count'")
            .and_then(|mut s| s.exists([]))
            .unwrap_or(false);
        assert!(has_retry_count);

        let has_max_retries: bool = conn
            .prepare("SELECT 1 FROM pragma_table_info('send_later') WHERE name = 'max_retries'")
            .and_then(|mut s| s.exists([]))
            .unwrap_or(false);
        assert!(has_max_retries);

        let has_last_error: bool = conn
            .prepare("SELECT 1 FROM pragma_table_info('send_later') WHERE name = 'last_error'")
            .and_then(|mut s| s.exists([]))
            .unwrap_or(false);
        assert!(has_last_error);

        let version: i64 = conn
            .query_row("SELECT MAX(version) FROM schema_version", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert!(
            version >= 49,
            "schema_version should be >= 49, got {}",
            version
        );
    }

    #[test]
    fn test_migrate_v50_adds_use_browser_key_column() {
        let conn = setup_test_db();

        let has_col: bool = conn
            .prepare("SELECT 1 FROM pragma_table_info('ai_configs') WHERE name = 'use_browser_key'")
            .and_then(|mut s| s.exists([]))
            .unwrap_or(false);
        assert!(
            has_col,
            "ai_configs.use_browser_key column must exist after v50"
        );

        conn.execute(
            "INSERT INTO ai_configs (id, provider, model, feature) VALUES ('cfg1', 'openai', 'gpt-4o', 'default')",
            [],
        )
        .unwrap();
        let default_flag: i64 = conn
            .query_row(
                "SELECT use_browser_key FROM ai_configs WHERE id = 'cfg1'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(default_flag, 0, "existing/new rows default to 0");

        let version: i64 = conn
            .query_row("SELECT MAX(version) FROM schema_version", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert!(
            version >= 50,
            "schema_version should be >= 50, got {}",
            version
        );
    }

    #[test]
    fn test_migrate_v48_allows_calendar_rsvp_reply() {
        let conn = setup_test_db();

        // Insert a mock account to satisfy FK if needed
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username)
             VALUES ('acc-v48', 'v48@example.com', 'V48 Test', 'imap.example.com', 993, 'Tls', 'smtp.example.com', 587, 'Tls', 'v48@example.com')",
            [],
        ).unwrap();

        // Should not violate CHECK constraint
        let result = conn.execute(
            "INSERT INTO pending_mutations (id, account_id, mutation_type, calendar_event_id, payload_json)
             VALUES ('test-id-v48', 'acc-v48', 'calendar_rsvp_reply', 'evt-1', '{}')",
            [],
        );
        assert!(
            result.is_ok(),
            "calendar_rsvp_reply should be allowed by CHECK: {:?}",
            result
        );

        // Verify schema version
        let version: i64 = conn
            .query_row("SELECT MAX(version) FROM schema_version", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert!(
            version >= 48,
            "schema_version should be >= 48, got {}",
            version
        );

        // Verify existing types still work
        let result2 = conn.execute(
            "INSERT INTO pending_mutations (id, account_id, mutation_type, calendar_event_id, payload_json)
             VALUES ('test-id-v48-rsvp', 'acc-v48', 'calendar_rsvp', 'evt-2', '{}')",
            [],
        );
        assert!(
            result2.is_ok(),
            "calendar_rsvp should still be allowed: {:?}",
            result2
        );

        // Verify invalid types still rejected
        let invalid = conn.execute(
            "INSERT INTO pending_mutations (id, account_id, mutation_type, calendar_event_id, payload_json)
             VALUES ('test-id-v48-bad', 'acc-v48', 'invalid_type', 'evt-3', '{}')",
            [],
        );
        assert!(
            invalid.is_err(),
            "invalid mutation_type should still be rejected"
        );
    }

    #[test]
    fn test_migrate_v48_adds_sequence_column() {
        let conn = setup_test_db();
        let has_sequence: bool = conn
            .prepare("SELECT 1 FROM pragma_table_info('calendar_events') WHERE name = 'sequence'")
            .and_then(|mut s| s.exists([]))
            .unwrap_or(false);
        assert!(
            has_sequence,
            "calendar_events.sequence column must exist after v48"
        );
    }

    #[test]
    fn test_migrate_v48_is_idempotent_on_partial_rerun() {
        let conn = setup_test_db();

        // Simulate crashed state: pending_mutations_new leftover from a partial v48 run
        conn.execute_batch(
            "CREATE TABLE pending_mutations_new (
                id TEXT PRIMARY KEY,
                account_id TEXT NOT NULL,
                mutation_type TEXT NOT NULL
            );",
        )
        .unwrap();

        // Re-run v48 — must not fail thanks to DROP TABLE IF EXISTS
        super::migrate_v48(&conn).expect("v48 must be idempotent on re-run");

        // Original table still exists and is functional
        let count: i64 = conn
            .query_row("SELECT COUNT(*) FROM pending_mutations", [], |r| r.get(0))
            .unwrap();
        assert!(count >= 0);
    }

    #[test]
    fn test_migrate_v52_adds_attachment_part_id_and_folder_uid_uniqueness() {
        let conn = setup_test_db();

        let has_part_id: bool = conn
            .prepare("SELECT 1 FROM pragma_table_info('attachments') WHERE name = 'part_id'")
            .and_then(|mut s| s.exists([]))
            .unwrap_or(false);
        assert!(
            has_part_id,
            "attachments.part_id column must exist after v52"
        );

        let has_folder_uid_index: bool = conn
            .prepare("SELECT 1 FROM sqlite_master WHERE type = 'index' AND name = 'idx_emails_folder_uid'")
            .and_then(|mut s| s.exists([]))
            .unwrap_or(false);
        assert!(
            has_folder_uid_index,
            "emails(folder_id, uid) unique index must exist after v52"
        );
    }

    #[test]
    fn test_migrate_v52_backfills_attachment_part_id_from_legacy_id() {
        let conn = Connection::open_in_memory().unwrap();
        conn.pragma_update(None, "key", "test-key-not-for-production")
            .unwrap();
        conn.execute_batch("PRAGMA journal_mode=WAL;").unwrap();
        conn.execute_batch("PRAGMA foreign_keys=ON;").unwrap();

        migrate_v1(&conn).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, smtp_host, username)
             VALUES ('acc1', 'a@example.com', 'A', 'imap.example.com', 'smtp.example.com', 'a@example.com')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type)
             VALUES ('fold1', 'acc1', 'INBOX', 'INBOX', 'inbox')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, subject, from_address, date)
             VALUES ('em1', 'acc1', 'fold1', 1, 'Subject', 'sender@example.com', '2026-08-01T00:00:00Z')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO attachments (id, email_id, filename, mime_type, size) VALUES ('2.1', 'em1', 'f.pdf', 'application/pdf', 1)",
            [],
        )
        .unwrap();

        migrate_v52(&conn).unwrap();

        let (id, part_id): (String, String) = conn
            .query_row(
                "SELECT id, part_id FROM attachments WHERE email_id = 'em1'",
                [],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .unwrap();
        assert_eq!(id, "2.1");
        assert_eq!(part_id, "2.1");
    }
}

fn migrate_v31(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        r#"UPDATE emails SET
            subject = REPLACE(REPLACE(subject, '\"', '"'), '\\', '\'),
            from_name = REPLACE(REPLACE(from_name, '\"', '"'), '\\', '\'),
            to_addresses = REPLACE(REPLACE(to_addresses, '\"', '"'), '\\', '\'),
            cc_addresses = REPLACE(REPLACE(cc_addresses, '\"', '"'), '\\', '\'),
            bcc_addresses = REPLACE(REPLACE(bcc_addresses, '\"', '"'), '\\', '\')
        WHERE subject LIKE '%\%'
           OR from_name LIKE '%\%'
           OR to_addresses LIKE '%\%'
           OR cc_addresses LIKE '%\%'
           OR bcc_addresses LIKE '%\%';

        INSERT OR IGNORE INTO schema_version (version) VALUES (31);"#,
    )?;
    Ok(())
}

fn migrate_v37(conn: &Connection) -> Result<(), AppError> {
    // 1.1 Recurring event expansion + 1.2 Timezone handling
    // Try adding columns (ignore errors if columns already exist, though they shouldn't)
    let _ = conn.execute("ALTER TABLE calendar_events ADD COLUMN start_tz TEXT;", []);
    let _ = conn.execute("ALTER TABLE calendar_events ADD COLUMN end_tz TEXT;", []);
    let _ = conn.execute(
        "ALTER TABLE calendar_events ADD COLUMN recurring_event_id TEXT;",
        [],
    );
    let _ = conn.execute(
        "ALTER TABLE calendar_events ADD COLUMN attendees_json TEXT;",
        [],
    );
    let _ = conn.execute(
        "ALTER TABLE calendar_events ADD COLUMN reminders_json TEXT;",
        [],
    );
    let _ = conn.execute("ALTER TABLE calendar_events ADD COLUMN color TEXT;", []);
    let _ = conn.execute(
        "ALTER TABLE calendar_events ADD COLUMN hangout_link TEXT;",
        [],
    );
    let _ = conn.execute("CREATE TABLE IF NOT EXISTS calendar_event_snoozes (event_id TEXT PRIMARY KEY, snooze_until TEXT);", []);

    // 1.3 Offline queue support for pending_mutations
    // Check if pending_mutations table exists, if so recreate it
    let table_exists: bool = conn.query_row(
        "SELECT COUNT(*) > 0 FROM sqlite_master WHERE type = 'table' AND name = 'pending_mutations'",
        [],
        |row| row.get(0),
    ).unwrap_or(false);

    if table_exists {
        conn.execute_batch(
            r#"
            CREATE TABLE pending_mutations_new (
                id TEXT PRIMARY KEY,
                account_id TEXT NOT NULL,
                email_uid INTEGER,
                folder_path TEXT,
                mutation_type TEXT NOT NULL CHECK(mutation_type IN ('mark_read', 'mark_unread', 'star', 'unstar', 'move', 'delete', 'calendar_rsvp', 'calendar_create', 'calendar_update', 'calendar_delete')),
                target_folder TEXT,
                created_at TEXT NOT NULL DEFAULT (datetime('now')),
                UNIQUE(account_id, email_uid, mutation_type)
            );
            INSERT INTO pending_mutations_new (id, account_id, email_uid, folder_path, mutation_type, target_folder, created_at)
            SELECT id, account_id, email_uid, folder_path, mutation_type, target_folder, created_at FROM pending_mutations;
            DROP TABLE pending_mutations;
            ALTER TABLE pending_mutations_new RENAME TO pending_mutations;
            CREATE INDEX IF NOT EXISTS idx_pending_mutations_account ON pending_mutations(account_id);
            "#
        )?;
    }

    conn.execute_batch(
        r#"DELETE FROM calendar_sync_state;
        DELETE FROM calendar_events WHERE source='google';
        INSERT OR IGNORE INTO schema_version (version) VALUES (37);"#,
    )?;
    Ok(())
}

fn migrate_v33(conn: &Connection) -> Result<(), AppError> {
    // 1.3.1 OAuth scope upgrade: force re-consent
    let _ = conn.execute(
        "DELETE FROM encrypted_credentials WHERE credential_type IN ('oauth2_access_token', 'oauth2_refresh_token');",
        []
    );
    let _ = conn.execute(
        "UPDATE accounts SET oauth2_access_token = NULL, oauth2_refresh_token = NULL WHERE auth_type = 'oauth2_gmail';",
        []
    );
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (33);")?;
    Ok(())
}

fn migrate_v34(conn: &Connection) -> Result<(), AppError> {
    // 2.5 Attendees storage
    let _ = conn.execute(
        "ALTER TABLE calendar_events ADD COLUMN attendees_json TEXT;",
        [],
    );
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (34);")?;
    Ok(())
}

fn migrate_v35(conn: &Connection) -> Result<(), AppError> {
    // 3.1 Ingest Google reminders
    let _ = conn.execute(
        "ALTER TABLE calendar_events ADD COLUMN reminders_json TEXT;",
        [],
    );
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (35);")?;
    Ok(())
}

fn migrate_v36(conn: &Connection) -> Result<(), AppError> {
    // 5.4 Per-event color override
    let _ = conn.execute("ALTER TABLE calendar_events ADD COLUMN color TEXT;", []);
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (36);")?;
    Ok(())
}

fn migrate_v32(conn: &Connection) -> Result<(), AppError> {
    // Heal stale dev databases where the agent tables (introduced in v25)
    // were not created because the schema_version row had already advanced
    // past 25 before the v25 migration body included these CREATE TABLE
    // statements. CREATE TABLE IF NOT EXISTS is idempotent and harmless on
    // healthy databases.
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS agent_sessions (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            title TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS agent_messages (
            id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(session_id) REFERENCES agent_sessions(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS auto_drafts (
            id TEXT PRIMARY KEY,
            email_id TEXT NOT NULL UNIQUE,
            draft_content TEXT NOT NULL,
            status TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(email_id) REFERENCES emails(id) ON DELETE CASCADE
        );

        INSERT OR IGNORE INTO schema_version (version) VALUES (32);",
    )?;
    Ok(())
}

fn migrate_v39(conn: &Connection) -> Result<(), AppError> {
    // Add nullable tool_calls TEXT column to agent_messages table
    let _ = conn.execute("ALTER TABLE agent_messages ADD COLUMN tool_calls TEXT;", []);
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (39);")?;
    Ok(())
}

fn migrate_v40(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS app_settings (
            key TEXT PRIMARY KEY NOT NULL,
            value TEXT NOT NULL,
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        );

         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.hide_weekends', 'false');
         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.hide_declined', 'false');
         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.default_calendar_id', '');
         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.show_holidays', 'true');
         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.show_birthdays', 'true');
         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.snap_minutes', '15');
         INSERT OR IGNORE INTO schema_version (version) VALUES (40);"
    )?;
    Ok(())
}

fn migrate_v41(conn: &Connection) -> Result<(), AppError> {
    let _ = conn.execute("ALTER TABLE calendar_events ADD COLUMN category TEXT;", []);
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS calendar_categories (
            id TEXT PRIMARY KEY NOT NULL,
            account_id TEXT NOT NULL,
            name TEXT NOT NULL,
            color TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            UNIQUE(account_id, name),
            FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        );
        INSERT OR IGNORE INTO schema_version (version) VALUES (41);",
    )?;
    Ok(())
}

fn migrate_v42(conn: &Connection) -> Result<(), AppError> {
    let _ = conn.execute(
        "ALTER TABLE calendar_events ADD COLUMN travel_time_minutes INTEGER;",
        [],
    );
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (42);")?;
    Ok(())
}

fn migrate_v43(conn: &Connection) -> Result<(), AppError> {
    let has_birthday: bool = conn
        .prepare("SELECT 1 FROM pragma_table_info('contacts') WHERE name = 'birthday'")
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);
    if !has_birthday {
        let _ = conn.execute("ALTER TABLE contacts ADD COLUMN birthday TEXT;", []);
    }

    conn.execute_batch(
        "CREATE VIEW IF NOT EXISTS contact_birthdays AS
         SELECT 
            'birthday-' || id AS id,
            account_id,
            NULL AS email_id,
            'birthday-' || id AS uid,
            '🎂 ' || COALESCE(name, email) || '''s birthday' AS summary,
            NULL AS description,
            birthday AS dtstart,
            birthday AS dtend,
            NULL AS location,
            NULL AS organizer,
            'confirmed' AS status,
            'accepted' AS rsvp_status,
            'FREQ=YEARLY' AS recurrence_rule,
            1 AS all_day,
            datetime('now') AS created_at,
            datetime('now') AS updated_at,
            NULL AS google_calendar_id,
            NULL AS start_tz,
            NULL AS end_tz,
            NULL AS recurring_event_id,
            NULL AS attendees_json,
            NULL AS reminders_json,
            '#db2777' AS color,
            NULL AS hangout_link
         FROM contacts
         WHERE birthday IS NOT NULL AND birthday != '';
         
         INSERT OR IGNORE INTO schema_version (version) VALUES (43);",
    )?;
    Ok(())
}

fn migrate_v44(conn: &Connection) -> Result<(), AppError> {
    let _ = conn.execute(
        "ALTER TABLE calendar_events ADD COLUMN event_type TEXT;",
        [],
    );
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (44);")?;
    Ok(())
}

fn migrate_v45(conn: &Connection) -> Result<(), AppError> {
    conn.execute_batch(
        "INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.working_hours_start', '09:00');
         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.working_hours_end', '18:00');
         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.preview_zones', '[]');
         INSERT OR IGNORE INTO app_settings (key, value) VALUES ('calendar.default_reminder_minutes', '10');
         INSERT OR IGNORE INTO schema_version (version) VALUES (45);"
    )?;
    Ok(())
}

fn migrate_v46(conn: &Connection) -> Result<(), AppError> {
    let has_reminders: bool = conn
        .prepare("SELECT 1 FROM pragma_table_info('calendar_events') WHERE name = 'reminders_json'")
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);
    if !has_reminders {
        let _ = conn.execute(
            "ALTER TABLE calendar_events ADD COLUMN reminders_json TEXT;",
            [],
        );
    }
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (46);")?;
    Ok(())
}

fn migrate_v47(conn: &Connection) -> Result<(), AppError> {
    let _ = conn.execute(
        "DELETE FROM pending_mutations WHERE mutation_type LIKE 'calendar_%' AND created_at < datetime('now', '-30 days');",
        [],
    );
    conn.execute_batch("INSERT OR IGNORE INTO schema_version (version) VALUES (47);")?;
    Ok(())
}

pub(crate) fn migrate_v48(conn: &Connection) -> Result<(), AppError> {
    // D1: Add calendar_events.sequence column for RFC 5546 SEQUENCE propagation
    let has_sequence: bool = conn
        .prepare("SELECT 1 FROM pragma_table_info('calendar_events') WHERE name = 'sequence'")
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);
    if !has_sequence {
        let _ = conn.execute(
            "ALTER TABLE calendar_events ADD COLUMN sequence TEXT DEFAULT '0';",
            [],
        );
    }

    // D2: Expand pending_mutations CHECK to allow 'calendar_rsvp_reply' for RFC 5546 REPLY emails
    // sent from update_rsvp on non-Google (ICS/iCloud/Outlook) events.
    // Wrapped in transaction with idempotent DROP for crash-restart safety.
    conn.execute_batch(
        "DROP TABLE IF EXISTS pending_mutations_new;
        CREATE TABLE pending_mutations_new (
            id TEXT PRIMARY KEY,
            account_id TEXT NOT NULL,
            email_uid INTEGER,
            folder_path TEXT,
            mutation_type TEXT NOT NULL CHECK(mutation_type IN (
                'mark_read', 'mark_unread', 'star', 'unstar', 'move', 'delete',
                'calendar_rsvp', 'calendar_rsvp_reply',
                'calendar_create', 'calendar_update', 'calendar_delete'
            )),
            target_folder TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            calendar_event_id TEXT,
            payload_json TEXT,
            UNIQUE(account_id, email_uid, mutation_type)
        );
        INSERT INTO pending_mutations_new (id, account_id, email_uid, folder_path, mutation_type, target_folder, created_at, calendar_event_id, payload_json)
            SELECT id, account_id, email_uid, folder_path, mutation_type, target_folder, created_at, calendar_event_id, payload_json
            FROM pending_mutations;
        DROP TABLE pending_mutations;
        ALTER TABLE pending_mutations_new RENAME TO pending_mutations;
        CREATE INDEX IF NOT EXISTS idx_pending_mutations_account ON pending_mutations(account_id);
        CREATE INDEX IF NOT EXISTS idx_pending_mutations_cal_event ON pending_mutations(calendar_event_id) WHERE calendar_event_id IS NOT NULL;
        CREATE UNIQUE INDEX IF NOT EXISTS uniq_pending_mutations_cal ON pending_mutations(account_id, calendar_event_id, mutation_type) WHERE calendar_event_id IS NOT NULL;
        INSERT OR IGNORE INTO schema_version (version) VALUES (48);"
    )?;
    Ok(())
}

pub(crate) fn migrate_v49(conn: &Connection) -> Result<(), AppError> {
    let has_retry_count: bool = conn
        .prepare("SELECT 1 FROM pragma_table_info('send_later') WHERE name = 'retry_count'")
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);
    if !has_retry_count {
        let _ = conn.execute(
            "ALTER TABLE send_later ADD COLUMN retry_count INTEGER NOT NULL DEFAULT 0;",
            [],
        );
    }

    let has_max_retries: bool = conn
        .prepare("SELECT 1 FROM pragma_table_info('send_later') WHERE name = 'max_retries'")
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);
    if !has_max_retries {
        let _ = conn.execute(
            "ALTER TABLE send_later ADD COLUMN max_retries INTEGER NOT NULL DEFAULT 3;",
            [],
        );
    }

    let has_last_error: bool = conn
        .prepare("SELECT 1 FROM pragma_table_info('send_later') WHERE name = 'last_error'")
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);
    if !has_last_error {
        let _ = conn.execute("ALTER TABLE send_later ADD COLUMN last_error TEXT;", []);
    }

    conn.execute(
        "INSERT OR IGNORE INTO schema_version (version) VALUES (49);",
        [],
    )?;

    Ok(())
}

pub(crate) fn migrate_v50(conn: &Connection) -> Result<(), AppError> {
    let has_use_browser_key: bool = conn
        .prepare("SELECT 1 FROM pragma_table_info('ai_configs') WHERE name = 'use_browser_key'")
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);
    if !has_use_browser_key {
        let _ = conn.execute(
            "ALTER TABLE ai_configs ADD COLUMN use_browser_key INTEGER NOT NULL DEFAULT 0;",
            [],
        );
    }

    conn.execute(
        "INSERT OR IGNORE INTO schema_version (version) VALUES (50);",
        [],
    )?;

    Ok(())
}

pub(crate) fn migrate_v51(conn: &Connection) -> Result<(), AppError> {
    conn.execute("UPDATE folders SET last_synced_uid = 0", [])?;
    conn.execute(
        "INSERT OR IGNORE INTO schema_version (version) VALUES (51);",
        [],
    )?;
    Ok(())
}

pub(crate) fn migrate_v52(conn: &Connection) -> Result<(), AppError> {
    let has_part_id: bool = conn
        .prepare("SELECT 1 FROM pragma_table_info('attachments') WHERE name = 'part_id'")
        .and_then(|mut s| s.exists([]))
        .unwrap_or(false);

    if !has_part_id {
        conn.execute_batch(
            "ALTER TABLE attachments RENAME TO attachments_old;
             CREATE TABLE attachments (
                 id TEXT PRIMARY KEY,
                 email_id TEXT NOT NULL,
                 part_id TEXT NOT NULL DEFAULT '',
                 filename TEXT,
                 mime_type TEXT NOT NULL DEFAULT 'application/octet-stream',
                 size INTEGER NOT NULL DEFAULT 0,
                 content_id TEXT,
                 FOREIGN KEY (email_id) REFERENCES emails(id) ON DELETE CASCADE,
                 UNIQUE(email_id, part_id)
             );
             INSERT INTO attachments (id, email_id, part_id, filename, mime_type, size, content_id)
             SELECT id, email_id, id, filename, mime_type, size, content_id FROM attachments_old;
             DROP TABLE attachments_old;",
        )?;
    }

    conn.execute("UPDATE attachments SET part_id = id WHERE part_id = ''", [])?;

    conn.execute(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_emails_folder_uid ON emails(folder_id, uid)",
        [],
    )?;

    conn.execute(
        "INSERT OR IGNORE INTO schema_version (version) VALUES (52);",
        [],
    )?;
    Ok(())
}
