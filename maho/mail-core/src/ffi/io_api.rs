// Copyright 2026 Maho Browser. All rights reserved.

use std::ffi::c_void;
use std::io::{BufRead, BufReader};
use std::os::raw::c_char;

use chrono::Utc;
use rusqlite::TransactionBehavior;
use serde_json::{Map, Value};
use uuid::Uuid;

use maho_core::models::email::Email;

use crate::error::{MailFfiError, Result};
use crate::ffi::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};

fn c_string(ptr: *const c_char, name: &'static str) -> Result<String> {
    if ptr.is_null() {
        return Err(MailFfiError::InvalidArg(name));
    }
    let value = unsafe { std::ffi::CStr::from_ptr(ptr) };
    value
        .to_str()
        .map(str::to_owned)
        .map_err(|_| MailFfiError::InvalidArg(name))
}

fn non_empty(ptr: *const c_char, name: &'static str) -> Result<String> {
    let value = c_string(ptr, name)?;
    if value.trim().is_empty() {
        return Err(MailFfiError::InvalidArg(name));
    }
    Ok(value)
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ImportResult {
    pub imported: u32,
    pub skipped: u32,
    pub failed: u32,
    pub errors: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
#[serde(rename_all = "snake_case")]
enum BehaviorUpdateStatus {
    Applied,
    Conflict,
    Invalid,
}

#[derive(Debug, Clone, PartialEq, serde::Serialize, serde::Deserialize)]
struct BehaviorSnapshot {
    #[serde(default)]
    revision: u64,
    #[serde(flatten)]
    values: Map<String, Value>,
}

#[derive(Debug, Clone, PartialEq, serde::Serialize)]
struct BehaviorUpdateResult {
    status: BehaviorUpdateStatus,
    snapshot: BehaviorSnapshot,
}

fn update_behavior_setting_atomic(
    pool: &maho_core::db::SqlitePool,
    expected_revision: u64,
    key: &str,
    value_json: &str,
) -> Result<BehaviorUpdateResult> {
    let value: Value = serde_json::from_str(value_json)?;
    let valid_scalar = matches!(
        value,
        Value::Bool(_) | Value::Number(_) | Value::String(_) | Value::Array(_)
    );
    let valid_preview = key != "notification_preview"
        || matches!(
            value.as_str(),
            Some("sender_subject" | "sender_only" | "generic")
        );
    let valid_boolean =
        !matches!(key, "desktop_notifications" | "unread_badge_enabled") || value.is_boolean();

    let mut db = pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
    let transaction = db.transaction_with_behavior(TransactionBehavior::Immediate)?;
    let current_json: Option<String> = transaction
        .query_row(
            "SELECT value FROM app_settings WHERE key = ?1",
            ["mail_behavior_prefs"],
            |row| row.get(0),
        )
        .ok();
    let mut snapshot: BehaviorSnapshot =
        serde_json::from_str(current_json.as_deref().unwrap_or("{}"))?;

    if snapshot.revision != expected_revision {
        transaction.commit()?;
        return Ok(BehaviorUpdateResult {
            status: BehaviorUpdateStatus::Conflict,
            snapshot,
        });
    }
    if key == "version" || key == "revision" || !valid_scalar || !valid_preview || !valid_boolean {
        transaction.commit()?;
        return Ok(BehaviorUpdateResult {
            status: BehaviorUpdateStatus::Invalid,
            snapshot,
        });
    }

    snapshot.revision += 1;
    snapshot.values.insert(key.to_string(), value);
    let persisted = serde_json::to_string(&snapshot)?;
    transaction.execute(
        "INSERT INTO app_settings (key, value, updated_at)
         VALUES (?1, ?2, datetime('now'))
         ON CONFLICT(key) DO UPDATE SET
            value = excluded.value,
            updated_at = excluded.updated_at",
        rusqlite::params!["mail_behavior_prefs", persisted],
    )?;
    transaction.commit()?;
    Ok(BehaviorUpdateResult {
        status: BehaviorUpdateStatus::Applied,
        snapshot,
    })
}

fn extract_first_addr(addr: Option<&mail_parser::Address>) -> (String, Option<String>) {
    match addr {
        Some(mail_parser::Address::List(list)) if !list.is_empty() => {
            let a = &list[0];
            let email = a.address().unwrap_or_default().to_string();
            let name = a.name().map(|s| s.to_string());
            (email, name)
        }
        Some(mail_parser::Address::Group(groups)) if !groups.is_empty() => {
            if let Some(a) = groups[0].addresses.first() {
                let email = a.address().unwrap_or_default().to_string();
                let name = a.name().map(|s| s.to_string());
                (email, name)
            } else {
                (String::new(), None)
            }
        }
        _ => (String::new(), None),
    }
}

fn extract_all_addrs(addr: Option<&mail_parser::Address>) -> Vec<String> {
    match addr {
        Some(mail_parser::Address::List(list)) => list
            .iter()
            .filter_map(|a| {
                let s = a.address().unwrap_or_default().to_string();
                if s.is_empty() {
                    None
                } else {
                    Some(s)
                }
            })
            .collect(),
        Some(mail_parser::Address::Group(groups)) => groups
            .iter()
            .flat_map(|g| g.addresses.iter())
            .filter_map(|a| {
                let s = a.address().unwrap_or_default().to_string();
                if s.is_empty() {
                    None
                } else {
                    Some(s)
                }
            })
            .collect(),
        _ => Vec::new(),
    }
}

fn addrs_to_json(addr: Option<&mail_parser::Address>) -> String {
    let list = extract_all_addrs(addr);
    serde_json::to_string(&list).unwrap_or_else(|_| "[]".to_string())
}

fn addrs_to_optional_json(addr: Option<&mail_parser::Address>) -> Option<String> {
    let list = extract_all_addrs(addr);
    if list.is_empty() {
        None
    } else {
        Some(serde_json::to_string(&list).unwrap_or_else(|_| "[]".to_string()))
    }
}

fn parse_eml_bytes(
    raw: &[u8],
    account_id: &str,
    folder_id: &str,
) -> std::result::Result<Email, maho_core::error::AppError> {
    let parsed = mail_parser::MessageParser::default()
        .parse(raw)
        .ok_or_else(|| {
            maho_core::error::AppError::Validation("Failed to parse EML content".to_string())
        })?;

    let (from_addr, from_name) = extract_first_addr(parsed.from());

    let to_addresses = addrs_to_json(parsed.to());
    let cc_addresses = addrs_to_optional_json(parsed.cc());
    let bcc_addresses = addrs_to_optional_json(parsed.bcc());

    let subject = parsed.subject().unwrap_or("(no subject)").to_string();

    let message_id = parsed
        .message_id()
        .map(|s| format!("<{}>", s))
        .unwrap_or_else(|| format!("<imported-{}@maho>", Uuid::new_v4()));

    let in_reply_to = parsed
        .in_reply_to()
        .as_text_list()
        .and_then(|list| list.first().map(|s| format!("<{}>", s)));

    let date = parsed
        .date()
        .map(|d| {
            format!(
                "{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
                d.year, d.month, d.day, d.hour, d.minute, d.second
            )
        })
        .unwrap_or_else(|| Utc::now().format("%Y-%m-%d %H:%M:%S").to_string());

    let body_text = parsed.body_text(0).map(|s| s.to_string());
    let body_html = parsed.body_html(0).map(|s| s.to_string());

    let snippet = body_text
        .as_deref()
        .unwrap_or("")
        .chars()
        .take(200)
        .collect::<String>()
        .replace('\n', " ")
        .replace('\r', "");

    let has_attachments = parsed.attachment_count() > 0;
    let raw_size = raw.len() as i64;
    let now = Utc::now().format("%Y-%m-%d %H:%M:%S").to_string();

    Ok(Email {
        id: Uuid::new_v4().to_string(),
        account_id: account_id.to_string(),
        folder_id: folder_id.to_string(),
        uid: 0,
        message_id,
        in_reply_to,
        subject,
        from_address: from_addr,
        from_name,
        to_addresses,
        cc_addresses,
        bcc_addresses,
        date,
        snippet,
        is_read: true,
        is_starred: false,
        is_draft: false,
        has_attachments,
        body_text,
        body_html,
        raw_size,
        created_at: now.clone(),
        body_fetched_at: Some(now),
        draft_attachments_json: None,
        email_references: None,
        read_receipt: None,
        mdn_requested: None,
    })
}

fn message_id_exists(
    db: &rusqlite::Connection,
    message_id: &str,
    account_id: &str,
    folder_id: &str,
) -> bool {
    db.query_row(
        "SELECT 1 FROM emails WHERE message_id = ?1 AND account_id = ?2 AND folder_id = ?3 LIMIT 1",
        rusqlite::params![message_id, account_id, folder_id],
        |_row| Ok(()),
    )
    .is_ok()
}

fn insert_email(
    db: &rusqlite::Connection,
    email: &Email,
) -> std::result::Result<(), maho_core::error::AppError> {
    db.execute(
        "INSERT INTO emails
            (id, account_id, folder_id, uid, message_id, in_reply_to, subject,
             from_address, from_name, to_addresses, cc_addresses, bcc_addresses,
             date, snippet, is_read, is_starred, is_draft, has_attachments,
             body_text, body_html, raw_size, created_at, body_fetched_at)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
                 ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23)",
        rusqlite::params![
            email.id,
            email.account_id,
            email.folder_id,
            email.uid,
            email.message_id,
            email.in_reply_to,
            email.subject,
            email.from_address,
            email.from_name,
            email.to_addresses,
            email.cc_addresses,
            email.bcc_addresses,
            email.date,
            email.snippet,
            email.is_read,
            email.is_starred,
            email.is_draft,
            email.has_attachments,
            email.body_text,
            email.body_html,
            email.raw_size,
            email.created_at,
            email.body_fetched_at,
        ],
    )?;
    Ok(())
}

#[no_mangle]
pub extern "C" fn MahoMailImportEmlContent(
    content_base64: *const c_char,
    account_id: *const c_char,
    folder_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(b64) = non_empty(content_base64, "content_base64") else {
            return false;
        };
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(folder_id) = non_empty(folder_id, "folder_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                use base64::Engine;
                let raw = base64::prelude::BASE64_STANDARD.decode(&b64).map_err(|e| {
                    MailFfiError::Internal(format!("Failed to decode base64 EML: {}", e))
                })?;
                let email = parse_eml_bytes(&raw, &account_id, &folder_id)?;
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                if message_id_exists(&db, &email.message_id, &account_id, &folder_id) {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        format!(
                            "Email with message_id {} already exists in this folder",
                            email.message_id
                        ),
                    )));
                }
                insert_email(&db, &email)?;
                log::info!("[IMPORT] Imported EML content as {}", email.id);
                Ok(email)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailImportMboxContent(
    content_base64: *const c_char,
    account_id: *const c_char,
    folder_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(b64) = non_empty(content_base64, "content_base64") else {
            return false;
        };
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(folder_id) = non_empty(folder_id, "folder_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                use base64::Engine;
                let raw = base64::prelude::BASE64_STANDARD.decode(&b64).map_err(|e| {
                    MailFfiError::Internal(format!("Failed to decode base64 MBOX: {}", e))
                })?;
                let reader = BufReader::new(raw.as_slice());

                let mut imported = 0u32;
                let mut skipped = 0u32;
                let mut failed = 0u32;
                let mut errors: Vec<String> = Vec::new();
                let mut current_message: Vec<u8> = Vec::new();
                let mut message_index = 0u32;

                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                let process_message = |msg_bytes: &[u8],
                                       idx: u32,
                                       db: &rusqlite::Connection,
                                       errors: &mut Vec<String>,
                                       imported: &mut u32,
                                       skipped: &mut u32,
                                       failed: &mut u32| {
                    if msg_bytes.is_empty() {
                        return;
                    }
                    match parse_eml_bytes(msg_bytes, &account_id, &folder_id) {
                        Ok(email) => {
                            if message_id_exists(db, &email.message_id, &account_id, &folder_id) {
                                *skipped += 1;
                                return;
                            }
                            match insert_email(db, &email) {
                                Ok(()) => *imported += 1,
                                Err(e) => {
                                    *failed += 1;
                                    let msg =
                                        format!("Message {}: DB insert failed: {}", idx + 1, e);
                                    log::warn!("[IMPORT] {}", msg);
                                    errors.push(msg);
                                }
                            }
                        }
                        Err(e) => {
                            *failed += 1;
                            let msg = format!("Message {}: Parse failed: {}", idx + 1, e);
                            log::warn!("[IMPORT] {}", msg);
                            errors.push(msg);
                        }
                    }
                };

                for line_result in reader.lines() {
                    let line = line_result.map_err(|e| {
                        MailFfiError::Internal(format!("Failed to read line: {}", e))
                    })?;
                    if line.starts_with("From ") && !current_message.is_empty() {
                        process_message(
                            &current_message,
                            message_index,
                            &db,
                            &mut errors,
                            &mut imported,
                            &mut skipped,
                            &mut failed,
                        );
                        message_index += 1;
                        current_message.clear();
                    } else {
                        let unescaped = if line.starts_with(">From ") {
                            &line[1..]
                        } else {
                            &line
                        };
                        current_message.extend_from_slice(unescaped.as_bytes());
                        current_message.push(b'\n');
                    }
                }

                if !current_message.is_empty() {
                    process_message(
                        &current_message,
                        message_index,
                        &db,
                        &mut errors,
                        &mut imported,
                        &mut skipped,
                        &mut failed,
                    );
                    message_index += 1;
                }

                log::info!(
                    "[IMPORT] MBOX content import complete: {} imported, {} skipped, {} failed out of {}",
                    imported, skipped, failed, message_index
                );

                Ok(ImportResult {
                    imported,
                    skipped,
                    failed,
                    errors,
                })
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGetAppSetting(
    key: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(key) = non_empty(key, "key") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let value: Option<String> = db
                    .query_row(
                        "SELECT value FROM app_settings WHERE key = ?1",
                        [&key],
                        |row| row.get(0),
                    )
                    .ok();
                Ok(value)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSetAppSetting(
    key: *const c_char,
    value: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(key) = non_empty(key, "key") else {
            return false;
        };
        let Ok(value) = c_string(value, "value") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                db.execute(
                    "INSERT INTO app_settings (key, value, updated_at)
                     VALUES (?1, ?2, datetime('now'))
                     ON CONFLICT(key) DO UPDATE SET
                        value = excluded.value,
                        updated_at = excluded.updated_at",
                    [&key, &value],
                )?;
                Ok(())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateBehaviorSetting(
    expected_revision: u64,
    key: *const c_char,
    value_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(key) = non_empty(key, "key") else {
            return false;
        };
        let Ok(value_json) = c_string(value_json, "value_json") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                update_behavior_setting_atomic(&ctx.pool, expected_revision, &key, &value_json)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailMd5Hash(
    input: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(input) = c_string(input, "input") else {
            return false;
        };
        accept_read(callback, user_data, move |_ctx| {
            blocking_json(move || {
                use md5::{Digest, Md5};
                let mut hasher = Md5::new();
                hasher.update(input.as_bytes());
                let hash = format!("{:x}", hasher.finalize());
                Ok(hash)
            })
        })
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::{CStr, CString};
    use std::sync::{Arc, Barrier};

    struct Capture {
        slot: std::sync::Mutex<Option<(bool, String)>>,
        cv: std::sync::Condvar,
    }

    impl Capture {
        fn new() -> Self {
            Self {
                slot: std::sync::Mutex::new(None),
                cv: std::sync::Condvar::new(),
            }
        }

        fn wait(&self) -> (bool, String) {
            let mut guard = self.slot.lock().unwrap();
            while guard.is_none() {
                guard = self.cv.wait(guard).unwrap();
            }
            guard.clone().unwrap()
        }

        fn as_user_data(&self) -> *mut c_void {
            std::ptr::from_ref(self).cast::<c_void>().cast_mut()
        }
    }

    unsafe extern "C" fn test_callback(ok: bool, json: *const c_char, user_data: *mut c_void) {
        let capture = unsafe { &*user_data.cast::<Capture>() };
        let payload = if json.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(json) }
                .to_string_lossy()
                .into_owned()
        };
        *capture.slot.lock().unwrap() = Some((ok, payload));
        capture.cv.notify_all();
    }

    #[test]
    fn test_md5_hash() {
        let _ctx_guard = crate::test_support::global_ctx_guard();
        let capture = Capture::new();
        let input = CString::new("hello").unwrap();
        let _rt = crate::runtime::runtime().unwrap();
        let pool = crate::test_support::pool_with_seeded_data();
        crate::state::set_ctx(crate::test_support::ctx_arc(pool));

        let success = MahoMailMd5Hash(input.as_ptr(), Some(test_callback), capture.as_user_data());
        assert!(success);
        let (ok, hash) = capture.wait();
        assert!(ok);
        assert_eq!(hash, "\"5d41402abc4b2a76b9719d911017c592\"");
    }

    #[test]
    fn test_app_settings() {
        let _ctx_guard = crate::test_support::global_ctx_guard();
        let _rt = crate::runtime::runtime().unwrap();
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx_arc(pool);
        crate::state::set_ctx(ctx);

        // Set setting
        {
            let capture = Capture::new();
            let key = CString::new("test_setting_key").unwrap();
            let val = CString::new("test_setting_value").unwrap();
            let success = MahoMailSetAppSetting(
                key.as_ptr(),
                val.as_ptr(),
                Some(test_callback),
                capture.as_user_data(),
            );
            assert!(success);
            let (ok, _) = capture.wait();
            assert!(ok);
        }

        // Get setting
        {
            let capture = Capture::new();
            let key = CString::new("test_setting_key").unwrap();
            let success =
                MahoMailGetAppSetting(key.as_ptr(), Some(test_callback), capture.as_user_data());
            assert!(success);
            let (ok, val) = capture.wait();
            assert!(ok);
            assert_eq!(val, "\"test_setting_value\"");
        }
    }

    #[test]
    fn behavior_settings_compare_and_swap_is_atomic_for_concurrent_writers() {
        let pool = crate::test_support::pool_with_seeded_data();
        {
            let db = pool.get().unwrap();
            db.execute(
                "INSERT INTO app_settings (key, value, updated_at) VALUES (?1, ?2, datetime('now'))",
                rusqlite::params![
                    "mail_behavior_prefs",
                    r#"{"revision":7,"block_remote_images":true,"block_trackers":true}"#,
                ],
            )
            .unwrap();
        }

        let barrier = Arc::new(Barrier::new(3));
        let writers = [
            ("block_remote_images", "false"),
            ("block_trackers", "false"),
        ]
        .map(|(key, value)| {
            let pool = pool.clone();
            let barrier = Arc::clone(&barrier);
            std::thread::spawn(move || {
                barrier.wait();
                update_behavior_setting_atomic(&pool, 7, key, value).unwrap()
            })
        });

        barrier.wait();
        let results = writers.map(|writer| writer.join().unwrap());
        assert_eq!(
            results
                .iter()
                .filter(|result| result.status == BehaviorUpdateStatus::Applied)
                .count(),
            1
        );
        assert_eq!(
            results
                .iter()
                .filter(|result| result.status == BehaviorUpdateStatus::Conflict)
                .count(),
            1
        );
        assert!(results.iter().all(|result| result.snapshot.revision == 8));

        let db = pool.get().unwrap();
        let persisted: String = db
            .query_row(
                "SELECT value FROM app_settings WHERE key = ?1",
                ["mail_behavior_prefs"],
                |row| row.get(0),
            )
            .unwrap();
        let persisted: BehaviorSnapshot = serde_json::from_str(&persisted).unwrap();
        assert_eq!(persisted.revision, 8);
        assert_eq!(persisted, results[0].snapshot);
        assert_eq!(persisted, results[1].snapshot);
    }
}

// === [W-J] ===

#[no_mangle]
pub extern "C" fn MahoMailExportMailTransferArtifact(
    artifact_path: *const c_char,
    passphrase: *const c_char,
    database_copy_path: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(artifact_path) = non_empty(artifact_path, "artifact_path") else {
            return false;
        };
        let Ok(passphrase) = c_string(passphrase, "passphrase") else {
            return false;
        };
        let database_copy_path = if database_copy_path.is_null() {
            None
        } else {
            c_string(database_copy_path, "database_copy_path").ok()
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db_path = &ctx.db_path;
                let art_path = std::path::PathBuf::from(artifact_path);
                let db_copy_path = database_copy_path.map(std::path::PathBuf::from);

                maho_core::transfer::export_runtime_transfer_artifact(
                    db_path,
                    &art_path,
                    &passphrase,
                    db_copy_path.as_deref(),
                    &ctx.sqlcipher_key,
                    &ctx.credential_key,
                )
                .map_err(|e| {
                    MailFfiError::Core(maho_core::error::AppError::Internal(e.to_string()))
                })?;

                Ok("{}".to_string())
            })
        })
    })
}

/// Truncate `value` to at most `max_chars` characters without splitting a
/// multi-byte character. Guards the T11b regression: `&id[..8]` panics when the
/// id is shorter than 8 bytes or the boundary falls inside a UTF-8 sequence.
fn safe_id_prefix(value: &str, max_chars: usize) -> String {
    value.chars().take(max_chars).collect()
}

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod ulw_t11b_tests {
    use super::safe_id_prefix;

    #[test]
    fn short_id_does_not_panic() {
        assert_eq!(safe_id_prefix("abc", 8), "abc");
    }

    #[test]
    fn multibyte_id_is_char_boundary_safe() {
        let s = safe_id_prefix("日本語ですよかね", 8);
        assert_eq!(s.chars().count(), 8);
    }
}
