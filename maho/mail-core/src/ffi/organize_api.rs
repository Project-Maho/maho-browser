// Copyright 2026 Maho Browser. All rights reserved.

use rusqlite::params;
use serde::{Deserialize, Serialize};
use std::ffi::{c_void, CStr};
use std::os::raw::c_char;
use uuid::Uuid;

use crate::error::{MailFfiError, Result};
use crate::ffi::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use maho_core::models::email::EmailSummary;

fn c_string(ptr: *const c_char, name: &'static str) -> Result<String> {
    if ptr.is_null() {
        return Err(MailFfiError::InvalidArg(name));
    }
    // SAFETY: ptr is non-null and is guaranteed by caller to be a valid NUL-terminated string
    let value = unsafe { CStr::from_ptr(ptr) };
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

fn opt_c_string(ptr: *const c_char, name: &'static str) -> Result<Option<String>> {
    if ptr.is_null() {
        return Ok(None);
    }
    let s = c_string(ptr, name)?;
    if s.trim().is_empty() {
        Ok(None)
    } else {
        Ok(Some(s))
    }
}

fn map_email_summary(row: &rusqlite::Row) -> rusqlite::Result<EmailSummary> {
    Ok(EmailSummary {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        folder_id: row.get("folder_id")?,
        uid: row.get("uid")?,
        message_id: row.get("message_id")?,
        subject: row.get("subject")?,
        from_address: row.get("from_address")?,
        from_name: row.get("from_name")?,
        date: row.get("date")?,
        snippet: row.get("snippet")?,
        is_read: row.get("is_read")?,
        is_starred: row.get("is_starred")?,
        is_draft: row.get("is_draft")?,
        has_attachments: row.get("has_attachments")?,
    })
}

// ==========================================
// Snooze Commands
// ==========================================

#[no_mangle]
pub extern "C" fn MahoMailSnoozeEmail(
    email_id: *const c_char,
    snooze_until: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        let Ok(snooze_until) = non_empty(snooze_until, "snooze_until") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE emails SET snoozed_until = ?1 WHERE id = ?2",
                    params![snooze_until, email_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUnsnoozeEmail(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE emails SET snoozed_until = NULL WHERE id = ?1",
                    params![email_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListSnoozedEmails(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_id = match opt_c_string(account_id, "account_id") {
            Ok(val) => val,
            Err(_) => return false,
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                if let Some(ref aid) = account_id {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, folder_id, uid, message_id, subject, from_address, from_name,
                                date, snippet, is_read, is_starred, is_draft, has_attachments
                         FROM emails
                         WHERE snoozed_until IS NOT NULL AND account_id = ?1
                         ORDER BY snoozed_until ASC",
                    )?;
                    let rows = s
                        .query_map(params![aid], map_email_summary)?
                        .collect::<rusqlite::Result<Vec<_>>>()?;
                    Ok(rows)
                } else {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, folder_id, uid, message_id, subject, from_address, from_name,
                                date, snippet, is_read, is_starred, is_draft, has_attachments
                         FROM emails
                         WHERE snoozed_until IS NOT NULL
                         ORDER BY snoozed_until ASC",
                    )?;
                    let rows = s
                        .query_map([], map_email_summary)?
                        .collect::<rusqlite::Result<Vec<_>>>()?;
                    Ok(rows)
                }
            })
        })
    })
}

// ==========================================
// Reminders Commands
// ==========================================

#[no_mangle]
pub extern "C" fn MahoMailSetReminder(
    email_id: *const c_char,
    reminder_at: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        let Ok(reminder_at) = non_empty(reminder_at, "reminder_at") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE emails SET reminder_at = ?1 WHERE id = ?2",
                    params![reminder_at, email_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailClearReminder(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE emails SET reminder_at = NULL WHERE id = ?1",
                    params![email_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListReminders(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_id = match opt_c_string(account_id, "account_id") {
            Ok(val) => val,
            Err(_) => return false,
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                if let Some(ref aid) = account_id {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, folder_id, uid, message_id, subject, from_address, from_name,
                                date, snippet, is_read, is_starred, is_draft, has_attachments
                         FROM emails
                         WHERE reminder_at IS NOT NULL AND account_id = ?1
                         ORDER BY reminder_at ASC",
                    )?;
                    let rows = s
                        .query_map(params![aid], map_email_summary)?
                        .collect::<rusqlite::Result<Vec<_>>>()?;
                    Ok(rows)
                } else {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, folder_id, uid, message_id, subject, from_address, from_name,
                                date, snippet, is_read, is_starred, is_draft, has_attachments
                         FROM emails
                         WHERE reminder_at IS NOT NULL
                         ORDER BY reminder_at ASC",
                    )?;
                    let rows = s
                        .query_map([], map_email_summary)?
                        .collect::<rusqlite::Result<Vec<_>>>()?;
                    Ok(rows)
                }
            })
        })
    })
}

// ==========================================
// Mute Commands
// ==========================================

#[derive(Debug, Serialize, Deserialize)]
pub struct MutedThread {
    pub id: String,
    pub account_id: String,
    pub thread_id: String,
    pub created_at: String,
}

#[no_mangle]
pub extern "C" fn MahoMailMuteThread(
    account_id: *const c_char,
    message_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(message_id) = non_empty(message_id, "message_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let id = Uuid::new_v4().to_string();
                conn.execute(
                    "INSERT OR IGNORE INTO muted_threads (id, account_id, thread_id) VALUES (?1, ?2, ?3)",
                    params![id, account_id, message_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUnmuteThread(
    account_id: *const c_char,
    message_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(message_id) = non_empty(message_id, "message_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "DELETE FROM muted_threads WHERE account_id = ?1 AND thread_id = ?2",
                    params![account_id, message_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailIsThreadMuted(
    account_id: *const c_char,
    message_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(message_id) = non_empty(message_id, "message_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let count: i64 = conn.query_row(
                    "SELECT COUNT(*) FROM muted_threads WHERE account_id = ?1 AND thread_id = ?2",
                    params![account_id, message_id],
                    |row| row.get(0),
                )?;
                Ok(count > 0)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListMutedThreads(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = conn.prepare(
                    "SELECT id, account_id, thread_id, created_at FROM muted_threads WHERE account_id = ?1 ORDER BY created_at DESC",
                )?;
                let rows = stmt
                    .query_map(params![account_id], |row| {
                        Ok(MutedThread {
                            id: row.get("id")?,
                            account_id: row.get("account_id")?,
                            thread_id: row.get("thread_id")?,
                            created_at: row.get("created_at")?,
                        })
                    })?
                    .collect::<rusqlite::Result<Vec<_>>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailFilterMutedMessageIds(
    account_id: *const c_char,
    message_ids_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(raw_message_ids) = c_string(message_ids_json, "message_ids_json") else {
            return false;
        };
        let Ok(message_ids) = serde_json::from_str::<Vec<String>>(&raw_message_ids) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let muted: std::collections::HashSet<String> = {
                    let mut stmt =
                        conn.prepare("SELECT thread_id FROM muted_threads WHERE account_id = ?1")?;
                    let rows = stmt.query_map([&account_id], |row| row.get::<_, String>(0))?;
                    rows.filter_map(|r| r.ok()).collect()
                };
                let filtered: Vec<String> = message_ids
                    .into_iter()
                    .filter(|id| !muted.contains(id))
                    .collect();
                Ok(filtered)
            })
        })
    })
}

// ==========================================
// Pin/Unpin Commands
// ==========================================

#[no_mangle]
pub extern "C" fn MahoMailPinEmail(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE emails SET is_pinned = 1 WHERE id = ?1",
                    params![email_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUnpinEmail(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE emails SET is_pinned = 0 WHERE id = ?1",
                    params![email_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListPinnedEmails(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_id = match opt_c_string(account_id, "account_id") {
            Ok(val) => val,
            Err(_) => return false,
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                if let Some(ref aid) = account_id {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, folder_id, uid, message_id, subject, from_address, from_name,
                                date, snippet, is_read, is_starred, is_draft, has_attachments
                         FROM emails
                         WHERE is_pinned = 1 AND account_id = ?1
                         ORDER BY date DESC",
                    )?;
                    let rows = s
                        .query_map(params![aid], map_email_summary)?
                        .collect::<rusqlite::Result<Vec<_>>>()?;
                    Ok(rows)
                } else {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, folder_id, uid, message_id, subject, from_address, from_name,
                                date, snippet, is_read, is_starred, is_draft, has_attachments
                         FROM emails
                         WHERE is_pinned = 1
                         ORDER by date DESC",
                    )?;
                    let rows = s
                        .query_map([], map_email_summary)?
                        .collect::<rusqlite::Result<Vec<_>>>()?;
                    Ok(rows)
                }
            })
        })
    })
}

// ==========================================
// Mail Rules Commands
// ==========================================

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct RuleCondition {
    pub field: String,
    pub operator: String,
    pub value: String,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct RuleAction {
    #[serde(rename = "type")]
    pub action_type: String,
    pub value: Option<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct MailRule {
    pub id: String,
    pub account_id: String,
    pub name: String,
    pub priority: i64,
    pub is_enabled: bool,
    pub conditions: Vec<RuleCondition>,
    pub actions: Vec<RuleAction>,
    pub stop_processing: bool,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreateRuleRequest {
    pub account_id: String,
    pub name: String,
    pub conditions: Vec<RuleCondition>,
    pub actions: Vec<RuleAction>,
    pub stop_processing: Option<bool>,
}

#[derive(Debug, Deserialize)]
pub struct UpdateRuleRequest {
    pub id: String,
    pub name: Option<String>,
    pub is_enabled: Option<bool>,
    pub conditions: Option<Vec<RuleCondition>>,
    pub actions: Option<Vec<RuleAction>>,
    pub stop_processing: Option<bool>,
}

fn map_rule(row: &rusqlite::Row) -> rusqlite::Result<MailRule> {
    let conditions_json: String = row.get("conditions_json")?;
    let actions_json: String = row.get("actions_json")?;
    let is_enabled: i64 = row.get("is_enabled")?;
    let stop_processing: i64 = row.get("stop_processing")?;

    let conditions: Vec<RuleCondition> = serde_json::from_str(&conditions_json)
        .map_err(|e| rusqlite::Error::FromSqlConversionFailure(5, rusqlite::types::Type::Text, Box::new(e)))?;
    let actions: Vec<RuleAction> = serde_json::from_str(&actions_json)
        .map_err(|e| rusqlite::Error::FromSqlConversionFailure(6, rusqlite::types::Type::Text, Box::new(e)))?;

    Ok(MailRule {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        name: row.get("name")?,
        priority: row.get("priority")?,
        is_enabled: is_enabled != 0,
        conditions,
        actions,
        stop_processing: stop_processing != 0,
        created_at: row.get("created_at")?,
        updated_at: row.get("updated_at")?,
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCreateMailRule(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(rule) = serde_json::from_str::<CreateRuleRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let id = Uuid::new_v4().to_string();
                let conditions_json = serde_json::to_string(&rule.conditions)?;
                let actions_json = serde_json::to_string(&rule.actions)?;
                let stop = if rule.stop_processing.unwrap_or(false) {
                    1
                } else {
                    0
                };

                let max_priority: i64 = conn
                    .query_row(
                        "SELECT COALESCE(MAX(priority), -1) FROM mail_rules WHERE account_id = ?1",
                        params![rule.account_id],
                        |row| row.get(0),
                    )
                    .unwrap_or(-1);

                conn.execute(
                    "INSERT INTO mail_rules (id, account_id, name, priority, conditions_json, actions_json, stop_processing)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                    params![
                        id,
                        rule.account_id,
                        rule.name,
                        max_priority + 1,
                        conditions_json,
                        actions_json,
                        stop,
                    ],
                )?;

                let created = conn.query_row(
                    "SELECT id, account_id, name, priority, is_enabled, conditions_json, actions_json, stop_processing, created_at, updated_at
                     FROM mail_rules WHERE id = ?1",
                    params![id],
                    map_rule,
                )?;
                Ok(created)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateMailRule(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(rule) = serde_json::from_str::<UpdateRuleRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                let _exists: String = conn
                    .query_row(
                        "SELECT id FROM mail_rules WHERE id = ?1",
                        params![rule.id],
                        |row| row.get(0),
                    )
                    .map_err(|_| {
                        MailFfiError::Core(maho_core::error::AppError::NotFound(
                            "Rule not found".to_string(),
                        ))
                    })?;

                let mut set_clauses: Vec<String> = Vec::new();
                let mut param_values: Vec<rusqlite::types::Value> = Vec::new();

                if let Some(ref name) = rule.name {
                    set_clauses.push("name = ?".to_string());
                    param_values.push(rusqlite::types::Value::Text(name.clone()));
                }
                if let Some(is_enabled) = rule.is_enabled {
                    set_clauses.push("is_enabled = ?".to_string());
                    param_values.push(rusqlite::types::Value::Integer(if is_enabled {
                        1
                    } else {
                        0
                    }));
                }
                if let Some(ref conditions) = rule.conditions {
                    let json = serde_json::to_string(conditions)?;
                    set_clauses.push("conditions_json = ?".to_string());
                    param_values.push(rusqlite::types::Value::Text(json));
                }
                if let Some(ref actions) = rule.actions {
                    let json = serde_json::to_string(actions)?;
                    set_clauses.push("actions_json = ?".to_string());
                    param_values.push(rusqlite::types::Value::Text(json));
                }
                if let Some(stop) = rule.stop_processing {
                    set_clauses.push("stop_processing = ?".to_string());
                    param_values.push(rusqlite::types::Value::Integer(if stop { 1 } else { 0 }));
                }

                if !set_clauses.is_empty() {
                    set_clauses.push("updated_at = datetime('now')".to_string());
                    let sql = format!(
                        "UPDATE mail_rules SET {} WHERE id = ?",
                        set_clauses.join(", ")
                    );
                    param_values.push(rusqlite::types::Value::Text(rule.id.clone()));
                    let params_refs: Vec<&dyn rusqlite::types::ToSql> = param_values
                        .iter()
                        .map(|v| v as &dyn rusqlite::types::ToSql)
                        .collect();
                    conn.execute(&sql, params_refs.as_slice())?;
                }

                let updated = conn.query_row(
                    "SELECT id, account_id, name, priority, is_enabled, conditions_json, actions_json, stop_processing, created_at, updated_at
                     FROM mail_rules WHERE id = ?1",
                    params![rule.id],
                    map_rule,
                )?;
                Ok(updated)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteMailRule(
    rule_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(rule_id) = non_empty(rule_id, "rule_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute("DELETE FROM mail_rules WHERE id = ?1", params![rule_id])?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListMailRules(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = conn.prepare(
                    "SELECT id, account_id, name, priority, is_enabled, conditions_json, actions_json, stop_processing, created_at, updated_at
                     FROM mail_rules WHERE account_id = ?1 ORDER BY priority ASC",
                )?;
                let rows = stmt
                    .query_map(params![account_id], map_rule)?
                    .collect::<rusqlite::Result<Vec<_>>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailReorderMailRules(
    account_id: *const c_char,
    rule_ids_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(raw_rule_ids) = c_string(rule_ids_json, "rule_ids_json") else {
            return false;
        };
        let Ok(rule_ids) = serde_json::from_str::<Vec<String>>(&raw_rule_ids) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let mut conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let tx = conn.transaction()?;
                for (idx, rule_id) in rule_ids.iter().enumerate() {
                    tx.execute(
                        "UPDATE mail_rules SET priority = ?1, updated_at = datetime('now') WHERE id = ?2 AND account_id = ?3",
                        params![idx as i64, rule_id, account_id],
                    )?;
                }
                tx.commit()?;
                Ok("{}".to_string())
            })
        })
    })
}

pub fn evaluate_rules_for_email_in_tx(
    conn: &rusqlite::Connection,
    account_id: &str,
    email_id: &str,
) -> std::result::Result<(), maho_core::error::AppError> {
    let email = match load_email_for_rules(conn, account_id, email_id) {
        Ok(e) => e,
        Err(maho_core::error::AppError::Database(rusqlite::Error::QueryReturnedNoRows)) => {
            log::warn!(
                "evaluate_rules_for_email: email {} not found, skipping",
                email_id
            );
            return Ok(());
        }
        Err(error) => return Err(error),
    };

    let mut stmt = conn
        .prepare(
            "SELECT id, account_id, name, priority, is_enabled, conditions_json, actions_json, stop_processing, created_at, updated_at
             FROM mail_rules WHERE account_id = ?1 AND is_enabled = 1 ORDER BY priority ASC",
        )
        .map_err(maho_core::error::AppError::Database)?;
    let rules: Vec<MailRule> = stmt
        .query_map(params![account_id], map_rule)
        .map_err(maho_core::error::AppError::Database)?
        .collect::<rusqlite::Result<Vec<_>>>()
        .map_err(maho_core::error::AppError::Database)?;

    for rule in &rules {
        if rule.conditions.is_empty() {
            continue;
        }

        let all_match = rule.conditions.iter().all(|c| check_condition(c, &email));
        if !all_match {
            continue;
        }

        let mut email_deleted = false;
        for action in &rule.actions {
            if execute_action(conn, email_id, action)? {
                email_deleted = true;
                break;
            }
        }

        if email_deleted || rule.stop_processing {
            break;
        }
    }

    Ok(())
}

pub fn evaluate_rules_for_email(
    db: &rusqlite::Connection,
    account_id: &str,
    email_id: &str,
) -> std::result::Result<(), maho_core::error::AppError> {
    let transaction = db.unchecked_transaction()?;
    evaluate_rules_for_email_in_tx(&transaction, account_id, email_id)?;
    transaction.commit()?;
    Ok(())
}

struct EmailForRules {
    from_address: String,
    to_addresses: String,
    subject: String,
    body_text: Option<String>,
    has_attachments: bool,
}

fn load_email_for_rules(
    db: &rusqlite::Connection,
    account_id: &str,
    email_id: &str,
) -> std::result::Result<EmailForRules, maho_core::error::AppError> {
    db.query_row(
        "SELECT from_address, to_addresses, subject, body_text, has_attachments FROM emails WHERE id = ?1 AND account_id = ?2",
        params![email_id, account_id],
        |row| {
            let has_att: i64 = row.get(4)?;
            Ok(EmailForRules {
                from_address: row.get(0)?,
                to_addresses: row.get(1)?,
                subject: row.get(2)?,
                body_text: row.get(3)?,
                has_attachments: has_att != 0,
            })
        },
    )
    .map_err(maho_core::error::AppError::Database)
}

fn check_condition(condition: &RuleCondition, email: &EmailForRules) -> bool {
    if condition.field == "to" {
        let addresses: Vec<String> = serde_json::from_str(&email.to_addresses).unwrap_or_default();
        return addresses
            .iter()
            .any(|addr| match_field_value(addr, &condition.operator, &condition.value));
    }

    let field_value = match condition.field.as_str() {
        "from" => Some(email.from_address.as_str()),
        "subject" => Some(email.subject.as_str()),
        "body" => email.body_text.as_deref(),
        "has_attachment" => {
            let expected = condition.value.to_lowercase() == "true";
            return email.has_attachments == expected;
        }
        _ => None,
    };

    let field_value = match field_value {
        Some(v) => v,
        None => return false,
    };

    match_field_value(field_value, &condition.operator, &condition.value)
}

fn match_field_value(field_value: &str, operator: &str, value: &str) -> bool {
    let field_lower = field_value.to_lowercase();
    let value_lower = value.to_lowercase();

    match operator {
        "contains" => field_lower.contains(&value_lower),
        "not_contains" => !field_lower.contains(&value_lower),
        "equals" => field_lower == value_lower,
        "not_equals" => field_lower != value_lower,
        "starts_with" => field_lower.starts_with(&value_lower),
        "ends_with" => field_lower.ends_with(&value_lower),
        "matches_regex" => {
            log::warn!(
                "matches_regex operator used but regex crate not available — treating as contains"
            );
            field_lower.contains(&value_lower)
        }
        _ => false,
    }
}

fn get_email_scope(
    db: &rusqlite::Connection,
    email_id: &str,
) -> std::result::Result<(String, String, i64), maho_core::error::AppError> {
    db.query_row(
        "SELECT e.account_id, f.path, COALESCE(e.uid, 0) FROM emails e JOIN folders f
         ON e.folder_id=f.id AND e.account_id=f.account_id WHERE e.id=?1",
        params![email_id],
        |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
    )
    .map_err(maho_core::error::AppError::Database)
}

fn execute_action(
    db: &rusqlite::Connection,
    email_id: &str,
    action: &RuleAction,
) -> std::result::Result<bool, maho_core::error::AppError> {
    if matches!(action.action_type.as_str(), "mark_read" | "mark_unread" | "add_star" | "remove_star") {
        db.execute("UPDATE folders SET reconciliation_version=reconciliation_version+1
            WHERE id=(SELECT folder_id FROM emails WHERE id=?1)", [email_id])?;
    }
    match action.action_type.as_str() {
        "mark_read" => {
            let (account_id, folder_path, uid) = get_email_scope(db, email_id)?;
            db.execute(
                "UPDATE emails SET is_read = 1 WHERE id = ?1",
                params![email_id],
            )?;
            if uid > 0 {
                maho_core::services::offline_queue::queue_mutation(
                    db, &account_id, uid, &folder_path, "mark_read", None,
                )?;
            }
        }
        "mark_unread" => {
            let (account_id, folder_path, uid) = get_email_scope(db, email_id)?;
            db.execute(
                "UPDATE emails SET is_read = 0 WHERE id = ?1",
                params![email_id],
            )?;
            if uid > 0 {
                maho_core::services::offline_queue::queue_mutation(
                    db, &account_id, uid, &folder_path, "mark_unread", None,
                )?;
            }
        }
        "add_star" => {
            let (account_id, folder_path, uid) = get_email_scope(db, email_id)?;
            db.execute(
                "UPDATE emails SET is_starred = 1 WHERE id = ?1",
                params![email_id],
            )?;
            if uid > 0 {
                maho_core::services::offline_queue::queue_mutation(
                    db, &account_id, uid, &folder_path, "star", None,
                )?;
            }
        }
        "remove_star" => {
            let (account_id, folder_path, uid) = get_email_scope(db, email_id)?;
            db.execute(
                "UPDATE emails SET is_starred = 0 WHERE id = ?1",
                params![email_id],
            )?;
            if uid > 0 {
                maho_core::services::offline_queue::queue_mutation(
                    db, &account_id, uid, &folder_path, "unstar", None,
                )?;
            }
        }
        "delete" => {
            let (account_id, folder_path, uid) = get_email_scope(db, email_id)?;
            if uid > 0 {
                maho_core::services::offline_queue::queue_mutation(
                    db, &account_id, uid, &folder_path, "delete", None,
                )?;
            }
            db.execute("DELETE FROM attachments WHERE email_id = ?1", params![email_id])?;
            db.execute("DELETE FROM emails WHERE id = ?1", params![email_id])?;
            return Ok(true);
        }
        "move_to_folder" => {
            let folder_name = action.value.as_deref().ok_or_else(|| {
                maho_core::error::AppError::Validation("move_to_folder action missing target folder name".to_string())
            })?;
            let (account_id, current_folder_path, uid) = get_email_scope(db, email_id)?;
            let target_result: rusqlite::Result<(String, String)> = db.query_row(
                "SELECT f.id, f.path FROM folders f
                 WHERE f.account_id = ?1 AND (LOWER(f.name) = LOWER(?2) OR LOWER(f.path) = LOWER(?2))
                 LIMIT 1",
                params![&account_id, folder_name],
                |row| Ok((row.get(0)?, row.get(1)?)),
            );
            match target_result {
                Ok((target_folder_id, target_canonical_path)) => {
                    maho_core::services::email::move_email(db, email_id, &target_folder_id)?;
                    if uid > 0 {
                        maho_core::services::offline_queue::queue_mutation(
                            db,
                            &account_id,
                            uid,
                            &current_folder_path,
                            "move",
                            Some(&target_canonical_path),
                        )?;
                    }
                }
                Err(rusqlite::Error::QueryReturnedNoRows) => {
                    return Err(maho_core::error::AppError::NotFound(format!(
                        "Target folder '{folder_name}' not found for account {account_id}"
                    )));
                }
                Err(e) => return Err(maho_core::error::AppError::Database(e)),
            }
        }
        _ => {
            log::warn!("Unknown rule action type: {}", action.action_type);
        }
    }
    Ok(false)
}

// ==========================================
// Labels Commands
// ==========================================

#[derive(Debug, Serialize, Deserialize)]
pub struct Label {
    pub id: String,
    pub account_id: String,
    pub name: String,
    pub color: String,
    pub created_at: String,
}

#[derive(Debug, Deserialize)]
pub struct CreateLabelRequest {
    pub account_id: String,
    pub name: String,
    pub color: Option<String>,
}

fn map_label(row: &rusqlite::Row) -> rusqlite::Result<Label> {
    Ok(Label {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        name: row.get("name")?,
        color: row.get("color")?,
        created_at: row.get("created_at")?,
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListLabels(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = conn.prepare(
                    "SELECT id, account_id, name, color, created_at FROM labels WHERE account_id = ?1 ORDER BY name ASC",
                )?;
                let rows = stmt
                    .query_map(params![account_id], map_label)?
                    .collect::<rusqlite::Result<Vec<_>>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCreateLabel(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<CreateLabelRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let id = Uuid::new_v4().to_string();
                let color = request.color.unwrap_or_else(|| "#3b82f6".to_string());
                conn.execute(
                    "INSERT INTO labels (id, account_id, name, color) VALUES (?1, ?2, ?3, ?4)",
                    params![id, request.account_id, request.name, color],
                )?;

                let label = conn.query_row(
                    "SELECT id, account_id, name, color, created_at FROM labels WHERE id = ?1",
                    params![id],
                    map_label,
                )?;
                Ok(label)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteLabel(
    id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute("DELETE FROM labels WHERE id = ?1", params![id])?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailAddLabelToEmail(
    email_id: *const c_char,
    label_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        let Ok(label_id) = non_empty(label_id, "label_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "INSERT OR IGNORE INTO email_labels (email_id, label_id) VALUES (?1, ?2)",
                    params![email_id, label_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailRemoveLabelFromEmail(
    email_id: *const c_char,
    label_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        let Ok(label_id) = non_empty(label_id, "label_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "DELETE FROM email_labels WHERE email_id = ?1 AND label_id = ?2",
                    params![email_id, label_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListEmailLabels(
    email_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = conn.prepare(
                    "SELECT l.id, l.account_id, l.name, l.color, l.created_at
                     FROM labels l
                     INNER JOIN email_labels el ON el.label_id = l.id
                     WHERE el.email_id = ?1
                     ORDER BY l.name ASC",
                )?;
                let rows = stmt
                    .query_map(params![email_id], map_label)?
                    .collect::<rusqlite::Result<Vec<_>>>()?;
                Ok(rows)
            })
        })
    })
}

// ==========================================
// Saved Searches Commands
// ==========================================

#[no_mangle]
pub extern "C" fn MahoMailSaveSearch(
    name: *const c_char,
    query: *const c_char,
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(name) = non_empty(name, "name") else {
            return false;
        };
        let Ok(query) = non_empty(query, "query") else {
            return false;
        };
        let account_id = match opt_c_string(account_id, "account_id") {
            Ok(val) => val,
            Err(_) => return false,
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let result = maho_core::services::saved_search::save_search(
                    &conn,
                    &name,
                    &query,
                    account_id.as_deref(),
                )?;
                Ok(result)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListSavedSearches(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_id = match opt_c_string(account_id, "account_id") {
            Ok(val) => val,
            Err(_) => return false,
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let result = maho_core::services::saved_search::list_saved_searches(
                    &conn,
                    account_id.as_deref(),
                )?;
                Ok(result)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteSavedSearch(
    id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                maho_core::services::saved_search::delete_saved_search(&conn, &id)?;
                Ok("{}".to_string())
            })
        })
    })
}

// ==========================================
// Send Later Commands
// ==========================================

fn validate_scheduled_at(scheduled_at: &str) -> Result<()> {
    let dt = chrono::DateTime::parse_from_rfc3339(scheduled_at).map_err(|_| {
        MailFfiError::Core(maho_core::error::AppError::Validation(
            "Invalid scheduled_at format, expected RFC 3339".to_string(),
        ))
    })?;
    if dt.with_timezone(&chrono::Utc) <= chrono::Utc::now() {
        return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
            "Scheduled time must be in the future".to_string(),
        )));
    }
    Ok(())
}

#[derive(Debug, Deserialize)]
pub struct SendLaterRequest {
    pub account_id: String,
    pub to: Vec<String>,
    pub cc: Option<Vec<String>>,
    pub bcc: Option<Vec<String>>,
    pub subject: String,
    pub body_text: Option<String>,
    pub body_html: Option<String>,
    pub scheduled_at: String,
    pub attachments: Option<Vec<maho_core::smtp_client::ComposeAttachment>>,
    pub read_receipt: Option<bool>,
    pub in_reply_to: Option<String>,
    pub references: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct SendLaterItem {
    pub id: String,
    pub account_id: String,
    pub to_addresses: String,
    pub cc_addresses: Option<String>,
    pub bcc_addresses: Option<String>,
    pub subject: String,
    pub body_html: Option<String>,
    pub body_text: Option<String>,
    pub scheduled_at: String,
    pub status: String,
    pub created_at: String,
}

fn map_send_later_item(row: &rusqlite::Row) -> rusqlite::Result<SendLaterItem> {
    Ok(SendLaterItem {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        to_addresses: row.get("to_addresses")?,
        cc_addresses: row.get("cc_addresses")?,
        bcc_addresses: row.get("bcc_addresses")?,
        subject: row.get("subject")?,
        body_html: row.get("body_html")?,
        body_text: row.get("body_text")?,
        scheduled_at: row.get("scheduled_at")?,
        status: row.get("status")?,
        created_at: row.get("created_at")?,
    })
}

#[no_mangle]
pub extern "C" fn MahoMailScheduleSend(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<SendLaterRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                validate_scheduled_at(&request.scheduled_at)?;
                let id = Uuid::new_v4().to_string();
                let to_json = serde_json::to_string(&request.to)?;
                let cc_json = request
                    .cc
                    .as_ref()
                    .map(|v| serde_json::to_string(v).unwrap_or_default());
                let bcc_json = request
                    .bcc
                    .as_ref()
                    .map(|v| serde_json::to_string(v).unwrap_or_default());
                let attachments_json = serde_json::to_string(&request.attachments).ok();
                let read_receipt: Option<i64> = request.read_receipt.map(|v| if v { 1 } else { 0 });

                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "INSERT INTO send_later (id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, scheduled_at, attachments_json, read_receipt, in_reply_to, email_references)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)",
                    params![
                        id,
                        request.account_id,
                        to_json,
                        cc_json,
                        bcc_json,
                        request.subject,
                        request.body_html,
                        request.body_text,
                        request.scheduled_at,
                        attachments_json,
                        read_receipt,
                        request.in_reply_to,
                        request.references,
                    ],
                )?;

                let item = conn.query_row(
                    "SELECT id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, scheduled_at, status, created_at
                     FROM send_later WHERE id = ?1",
                    params![id],
                    map_send_later_item,
                )?;
                Ok(item)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCancelScheduledSend(
    id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE send_later SET status = 'cancelled' WHERE id = ?1 AND status = 'pending'",
                    params![id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListScheduledSends(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_id = match opt_c_string(account_id, "account_id") {
            Ok(val) => val,
            Err(_) => return false,
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                if let Some(ref aid) = account_id {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, scheduled_at, status, created_at
                         FROM send_later WHERE account_id = ?1 AND status = 'pending' ORDER BY julianday(scheduled_at) ASC, id ASC",
                    )?;
                    let rows = s
                        .query_map(params![aid], map_send_later_item)?
                        .collect::<rusqlite::Result<Vec<_>>>()?;
                    Ok(rows)
                } else {
                    let mut s = conn.prepare(
                        "SELECT id, account_id, to_addresses, cc_addresses, bcc_addresses, subject, body_html, body_text, scheduled_at, status, created_at
                         FROM send_later WHERE status = 'pending' ORDER BY julianday(scheduled_at) ASC, id ASC",
                    )?;
                    let rows = s
                        .query_map([], map_send_later_item)?
                        .collect::<rusqlite::Result<Vec<_>>>()?;
                    Ok(rows)
                }
            })
        })
    })
}

// ==========================================
// Scheduler Commands
// ==========================================

#[path = "scheduler_delivery.rs"]
mod scheduler_delivery;

static SCHEDULER_ACTIVE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// One lifecycle tick, also callable with a fixed instant for durable-work tests.
pub(crate) async fn run_scheduler_once(
    ctx: &crate::state::AppCtx,
    now: chrono::DateTime<chrono::Utc>,
) -> Result<()> {
    let now = now.to_rfc3339();
    // The connection guard is `!Send`, so it is scoped to the two updates and
    // dropped before the awaited delivery run.
    ctx.with_db(|conn| {
        conn.execute(
            "UPDATE emails SET snoozed_until = NULL WHERE julianday(snoozed_until) <= julianday(?1)",
            [&now],
        )?;
        conn.execute(
            "UPDATE emails SET reminder_at = NULL WHERE julianday(reminder_at) <= julianday(?1)",
            [&now],
        )?;
        Ok(())
    })?;
    scheduler_delivery::run(ctx, &now).await
}

#[no_mangle]
pub extern "C" fn MahoMailStartScheduler(
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        if SCHEDULER_ACTIVE.swap(true, std::sync::atomic::Ordering::Relaxed) {
            accept_read(callback, user_data, move |_ctx| {
                blocking_json(move || Ok::<_, MailFfiError>("{}"))
            });
            return true;
        }

        accept_read(callback, user_data, move |ctx| {
            let rt = match crate::runtime::runtime() {
                Some(rt) => rt,
                None => {
                    SCHEDULER_ACTIVE.store(false, std::sync::atomic::Ordering::Relaxed);
                    return Box::pin(async {
                        Err(MailFfiError::Internal(
                            "tokio runtime unavailable".to_string(),
                        ))
                    });
                }
            };

            rt.spawn(async move {
                let mut interval = tokio::time::interval(std::time::Duration::from_secs(30));
                while SCHEDULER_ACTIVE.load(std::sync::atomic::Ordering::Relaxed) {
                    interval.tick().await;
                    if !SCHEDULER_ACTIVE.load(std::sync::atomic::Ordering::Relaxed) {
                        break;
                    }

                    if let Err(error) = run_scheduler_once(&ctx, chrono::Utc::now()).await {
                        log::error!("Scheduler tick failed: {error}");
                    }
                }
            });

            Box::pin(async move { Ok("{}".to_string()) })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailStopScheduler(
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        SCHEDULER_ACTIVE.store(false, std::sync::atomic::Ordering::Relaxed);
        accept_read(callback, user_data, move |_ctx| {
            blocking_json(move || Ok::<_, MailFfiError>("{}"))
        })
    })
}

// === [W-E] ===

#[no_mangle]
pub extern "C" fn MahoMailClassifyEmailsSmart(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let emails = {
                    let db = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    let mut stmt = db
                        .prepare(
                            "SELECT id, from_address, subject, snippet FROM emails
                         WHERE account_id = ?1 AND smart_category IS NULL
                         LIMIT 200",
                        )
                        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                    let rows = stmt
                        .query_map([&account_id], |row| {
                            Ok((
                                row.get::<_, String>(0)?,
                                row.get::<_, String>(1)?,
                                row.get::<_, String>(2)?,
                                row.get::<_, String>(3)?,
                            ))
                        })
                        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                    let mut res = Vec::new();
                    for r in rows {
                        if let Ok(val) = r {
                            res.push(val);
                        }
                    }
                    res
                };

                if emails.is_empty() {
                    return Ok("{}".to_string());
                }

                if let Ok(ai_results) = try_ai_classify_impl(&ctx, &emails).await {
                    let db = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    for (id, category) in &ai_results {
                        let _ = db.execute(
                            "UPDATE emails SET smart_category = ?1 WHERE id = ?2",
                            rusqlite::params![category, id],
                        );
                    }

                    let classified_ids: std::collections::HashSet<&str> =
                        ai_results.iter().map(|(id, _)| id.as_str()).collect();
                    let remaining: Vec<_> = emails
                        .iter()
                        .filter(|(id, _, _, _)| !classified_ids.contains(id.as_str()))
                        .collect();

                    if !remaining.is_empty() {
                        for (id, from_addr, subject, _) in remaining {
                            let category = heuristic_classify(from_addr, subject);
                            let _ = db.execute(
                                "UPDATE emails SET smart_category = ?1 WHERE id = ?2",
                                rusqlite::params![category, id],
                            );
                        }
                    }
                } else {
                    let db = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    for (id, from_addr, subject, _) in &emails {
                        let category = heuristic_classify(from_addr, subject);
                        let _ = db.execute(
                            "UPDATE emails SET smart_category = ?1 WHERE id = ?2",
                            rusqlite::params![category, id],
                        );
                    }
                }

                Ok("{}".to_string())
            })
        })
    })
}

async fn try_ai_classify_impl(
    ctx: &crate::state::AppCtx,
    emails: &[(String, String, String, String)],
) -> Result<Vec<(String, String)>> {
    let batch_size = 20;
    let mut results = Vec::new();

    for chunk in emails.chunks(batch_size) {
        let email_list: Vec<String> = chunk
            .iter()
            .map(|(id, from, subject, snippet)| {
                format!(
                    "{{\"id\":\"{}\",\"from\":\"{}\",\"subject\":\"{}\",\"snippet\":\"{}\"}}",
                    id,
                    from.replace('"', "\\\""),
                    subject.replace('"', "\\\""),
                    snippet
                        .chars()
                        .take(80)
                        .collect::<String>()
                        .replace('"', "\\\"")
                )
            })
            .collect();

        let prompt = format!(
            "Classify each email into exactly one category: personal, notification, newsletter, promotion.\n\
             Return ONLY a JSON array of objects with \"id\" and \"category\" fields. No explanation.\n\
             Emails:\n[{}]",
            email_list.join(",")
        );

        let response = crate::ffi::ai_api::call_llm_internal(ctx, &prompt).await?;
        let trimmed = response.trim();
        let json_str = if let Some(start) = trimmed.find('[') {
            if let Some(end) = trimmed.rfind(']') {
                &trimmed[start..=end]
            } else {
                trimmed
            }
        } else {
            trimmed
        };

        if let Ok(parsed) = serde_json::from_str::<Vec<serde_json::Value>>(json_str) {
            for item in parsed {
                if let (Some(id), Some(category)) = (
                    item.get("id").and_then(|v| v.as_str()),
                    item.get("category").and_then(|v| v.as_str()),
                ) {
                    let normalized = match category.to_lowercase().as_str() {
                        "personal" | "notification" | "newsletter" | "promotion" => {
                            category.to_lowercase()
                        }
                        _ => continue,
                    };
                    results.push((id.to_string(), normalized));
                }
            }
        }
    }

    Ok(results)
}

fn heuristic_classify(from_address: &str, subject: &str) -> &'static str {
    let from_lower = from_address.to_lowercase();
    let subj_lower = subject.to_lowercase();

    if from_lower.contains("noreply")
        || from_lower.contains("no-reply")
        || from_lower.contains("notifications@")
        || from_lower.contains("alert@")
        || from_lower.contains("notify@")
        || subj_lower.contains("notification")
        || subj_lower.contains("alert:")
        || subj_lower.contains("security alert")
        || subj_lower.contains("password reset")
        || subj_lower.contains("verification code")
        || subj_lower.contains("confirm your")
    {
        return "notification";
    }

    if from_lower.contains("newsletter")
        || from_lower.contains("digest@")
        || from_lower.contains("weekly@")
        || from_lower.contains("updates@")
        || subj_lower.contains("newsletter")
        || subj_lower.contains("weekly digest")
        || subj_lower.contains("monthly update")
        || subj_lower.contains("unsubscribe")
    {
        return "newsletter";
    }

    if from_lower.contains("promo")
        || from_lower.contains("marketing@")
        || from_lower.contains("deals@")
        || from_lower.contains("offers@")
        || from_lower.contains("sales@")
        || subj_lower.contains("% off")
        || subj_lower.contains("limited time")
        || subj_lower.contains("free shipping")
        || subj_lower.contains("exclusive offer")
        || subj_lower.contains("deal")
        || subj_lower.contains("discount")
        || subj_lower.contains("coupon")
    {
        return "promotion";
    }

    "personal"
}

#[no_mangle]
pub extern "C" fn MahoMailListBySmartCategory(
    account_id: *const c_char,
    category: *const c_char,
    limit: i64,
    offset: i64,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_id = if account_id.is_null() {
            None
        } else {
            non_empty(account_id, "account_id").ok()
        };
        let Ok(category) = non_empty(category, "category") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                let mut sql = "SELECT id, account_id, folder_id, uid, message_id, subject, from_address, from_name,
                                      date, snippet, is_read, is_starred, is_draft, has_attachments
                               FROM emails
                               WHERE smart_category = ?1".to_string();

                let mut params: Vec<rusqlite::types::Value> =
                    vec![rusqlite::types::Value::Text(category)];

                if let Some(ref aid) = account_id {
                    sql.push_str(" AND account_id = ?2");
                    params.push(rusqlite::types::Value::Text(aid.clone()));
                } else {
                    sql.push_str(" AND (1=1 OR ?2 IS NULL)");
                    params.push(rusqlite::types::Value::Null);
                }

                sql.push_str(" ORDER BY date DESC LIMIT ?3 OFFSET ?4");
                params.push(rusqlite::types::Value::Integer(limit));
                params.push(rusqlite::types::Value::Integer(offset));

                let mut stmt = db
                    .prepare(&sql)
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;
                let rows = stmt
                    .query_map(rusqlite::params_from_iter(params), map_email_summary)
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                let mut res = Vec::new();
                for r in rows {
                    if let Ok(val) = r {
                        res.push(val);
                    }
                }
                Ok(res)
            })
        })
    })
}
