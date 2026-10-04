// Copyright 2026 Maho Browser. All rights reserved.

use rusqlite::params;
use serde::{Deserialize, Serialize};
use std::ffi::{c_void, CStr};
use std::os::raw::c_char;
use uuid::Uuid;

use crate::error::{MailFfiError, Result};
use crate::ffi::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};

// Helpers cloned from ffi.rs to keep this module self-contained and clean
fn c_string(ptr: *const c_char, name: &'static str) -> Result<String> {
    if ptr.is_null() {
        return Err(MailFfiError::InvalidArg(name));
    }
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

// Contacts Model definitions matching Tauri commands

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Contact {
    pub id: String,
    pub account_id: String,
    pub email: String,
    pub name: Option<String>,
    pub frequency: i64,
    pub last_contacted_at: Option<String>,
    pub is_vip: bool,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ContactGroup {
    pub id: String,
    pub account_id: String,
    pub name: String,
    pub member_emails: Vec<String>,
    pub created_at: String,
    pub updated_at: String,
}

fn map_contact(row: &rusqlite::Row) -> rusqlite::Result<Contact> {
    Ok(Contact {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        email: row.get("email")?,
        name: row.get("name")?,
        frequency: row.get("frequency")?,
        last_contacted_at: row.get("last_contacted_at")?,
        is_vip: row.get("is_vip")?,
        created_at: row.get("created_at")?,
        updated_at: row.get("updated_at")?,
    })
}

// Contact FFI Functions

#[no_mangle]
pub extern "C" fn MahoMailSearchContacts(
    account_id: *const c_char,
    query: *const c_char,
    limit: i64,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(query) = c_string(query, "query") else {
            return false;
        };
        let limit = if limit > 0 { limit } else { 10 };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let pattern = format!("%{}%", query);
                let mut stmt = conn.prepare(
                    "SELECT id, account_id, email, name, frequency, last_contacted_at, is_vip, created_at, updated_at
                     FROM contacts
                     WHERE account_id = ?1 AND (email LIKE ?2 OR name LIKE ?2)
                     ORDER BY frequency DESC, name ASC
                     LIMIT ?3",
                )?;
                let rows = stmt
                    .query_map(params![account_id, pattern, limit], map_contact)?
                    .collect::<std::result::Result<Vec<_>, rusqlite::Error>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailToggleVip(
    contact_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(contact_id) = non_empty(contact_id, "contact_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE contacts SET is_vip = CASE WHEN is_vip = 1 THEN 0 ELSE 1 END, updated_at = datetime('now') WHERE id = ?1",
                    params![contact_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListVipContacts(
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
                    "SELECT id, account_id, email, name, frequency, last_contacted_at, is_vip, created_at, updated_at
                     FROM contacts
                     WHERE account_id = ?1 AND is_vip = 1
                     ORDER BY name ASC",
                )?;
                let rows = stmt
                    .query_map(params![account_id], map_contact)?
                    .collect::<std::result::Result<Vec<_>, rusqlite::Error>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailPopulateContactsFromHistory(
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
                    "SELECT from_address, from_name, COUNT(*) as freq, MAX(date) as last_date
                     FROM emails
                     WHERE account_id = ?1 AND from_address != '' AND from_address IS NOT NULL
                     GROUP BY from_address",
                )?;

                let senders: Vec<(String, Option<String>, i64, Option<String>)> = stmt
                    .query_map(params![account_id], |row| {
                        Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?))
                    })?
                    .filter_map(|r| r.ok())
                    .collect();

                let mut inserted = 0usize;

                for (email, name, freq, last_date) in &senders {
                    let exists: bool = conn
                        .query_row(
                            "SELECT COUNT(*) > 0 FROM contacts WHERE account_id = ?1 AND email = ?2",
                            params![account_id, email],
                            |row| row.get(0),
                        )
                        .unwrap_or(false);

                    if exists {
                        conn.execute(
                            "UPDATE contacts SET frequency = ?1, last_contacted_at = ?2, updated_at = datetime('now')
                             WHERE account_id = ?3 AND email = ?4",
                            params![freq, last_date, account_id, email],
                        )?;
                    } else {
                        let id = Uuid::new_v4().to_string();
                        conn.execute(
                            "INSERT INTO contacts (id, account_id, email, name, frequency, last_contacted_at, is_vip)
                             VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0)",
                            params![id, account_id, email, name, freq, last_date],
                        )?;
                        inserted += 1;
                    }
                }

                Ok(inserted)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListContactGroups(
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
                    "SELECT id, account_id, name, member_emails_json, created_at, updated_at FROM contact_groups WHERE account_id = ?1 ORDER BY name ASC"
                )?;
                let rows = stmt
                    .query_map(params![account_id], |row| {
                        let emails_json: String = row.get("member_emails_json")?;
                        let member_emails: Vec<String> =
                            serde_json::from_str(&emails_json).unwrap_or_default();
                        Ok(ContactGroup {
                            id: row.get("id")?,
                            account_id: row.get("account_id")?,
                            name: row.get("name")?,
                            member_emails,
                            created_at: row.get("created_at")?,
                            updated_at: row.get("updated_at")?,
                        })
                    })?
                    .collect::<std::result::Result<Vec<_>, rusqlite::Error>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSearchContactGroups(
    account_id: *const c_char,
    query: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(query) = c_string(query, "query") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let pattern = format!("%{}%", query);
                let mut stmt = conn.prepare(
                    "SELECT id, account_id, name, member_emails_json, created_at, updated_at FROM contact_groups WHERE account_id = ?1 AND name LIKE ?2 ORDER BY name ASC LIMIT 10"
                )?;
                let rows = stmt
                    .query_map(params![account_id, pattern], |row| {
                        let emails_json: String = row.get("member_emails_json")?;
                        let member_emails: Vec<String> =
                            serde_json::from_str(&emails_json).unwrap_or_default();
                        Ok(ContactGroup {
                            id: row.get("id")?,
                            account_id: row.get("account_id")?,
                            name: row.get("name")?,
                            member_emails,
                            created_at: row.get("created_at")?,
                            updated_at: row.get("updated_at")?,
                        })
                    })?
                    .collect::<std::result::Result<Vec<_>, rusqlite::Error>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCreateContactGroup(
    account_id: *const c_char,
    name: *const c_char,
    member_emails_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(name) = non_empty(name, "name") else {
            return false;
        };
        let Ok(raw_emails) = c_string(member_emails_json, "member_emails_json") else {
            return false;
        };
        let Ok(member_emails) = serde_json::from_str::<Vec<String>>(&raw_emails) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let id = Uuid::new_v4().to_string();
                let emails_json = serde_json::to_string(&member_emails)?;
                conn.execute(
                    "INSERT INTO contact_groups (id, account_id, name, member_emails_json) VALUES (?1, ?2, ?3, ?4)",
                    params![id, account_id, name, emails_json],
                )?;
                let group = conn.query_row(
                    "SELECT id, account_id, name, member_emails_json, created_at, updated_at FROM contact_groups WHERE id = ?1",
                    params![id],
                    |row| {
                        let emails_json: String = row.get("member_emails_json")?;
                        let member_emails: Vec<String> = serde_json::from_str(&emails_json).unwrap_or_default();
                        Ok(ContactGroup {
                            id: row.get("id")?,
                            account_id: row.get("account_id")?,
                            name: row.get("name")?,
                            member_emails,
                            created_at: row.get("created_at")?,
                            updated_at: row.get("updated_at")?,
                        })
                    },
                )?;
                Ok(group)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateContactGroup(
    group_id: *const c_char,
    name: *const c_char,
    member_emails_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(group_id) = non_empty(group_id, "group_id") else {
            return false;
        };
        let Ok(name) = non_empty(name, "name") else {
            return false;
        };
        let Ok(raw_emails) = c_string(member_emails_json, "member_emails_json") else {
            return false;
        };
        let Ok(member_emails) = serde_json::from_str::<Vec<String>>(&raw_emails) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let emails_json = serde_json::to_string(&member_emails)?;
                conn.execute(
                    "UPDATE contact_groups SET name = ?1, member_emails_json = ?2, updated_at = datetime('now') WHERE id = ?3",
                    params![name, emails_json, group_id],
                )?;
                let group = conn.query_row(
                    "SELECT id, account_id, name, member_emails_json, created_at, updated_at FROM contact_groups WHERE id = ?1",
                    params![group_id],
                    |row| {
                        let emails_json: String = row.get("member_emails_json")?;
                        let member_emails: Vec<String> = serde_json::from_str(&emails_json).unwrap_or_default();
                        Ok(ContactGroup {
                            id: row.get("id")?,
                            account_id: row.get("account_id")?,
                            name: row.get("name")?,
                            member_emails,
                            created_at: row.get("created_at")?,
                            updated_at: row.get("updated_at")?,
                        })
                    },
                )?;
                Ok(group)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteContactGroup(
    group_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(group_id) = non_empty(group_id, "group_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "DELETE FROM contact_groups WHERE id = ?1",
                    params![group_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

// Signatures Model definitions matching Tauri commands

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Signature {
    pub id: String,
    pub account_id: Option<String>,
    pub name: String,
    pub body_html: String,
    pub body_text: String,
    pub is_default: bool,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct CreateSignatureRequest {
    pub account_id: Option<String>,
    pub name: String,
    pub body_html: String,
    pub body_text: String,
    pub is_default: Option<bool>,
}

fn map_signature(row: &rusqlite::Row) -> rusqlite::Result<Signature> {
    Ok(Signature {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        name: row.get("name")?,
        body_html: row.get("body_html")?,
        body_text: row.get("body_text")?,
        is_default: row.get("is_default")?,
        created_at: row.get("created_at")?,
        updated_at: row.get("updated_at")?,
    })
}

// Signature FFI Functions

#[no_mangle]
pub extern "C" fn MahoMailListSignatures(
    account_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_id_opt = if account_id.is_null() {
            None
        } else {
            let s = c_string(account_id, "account_id").ok();
            s.filter(|val| !val.trim().is_empty())
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                if let Some(ref aid) = account_id_opt {
                    let mut stmt = conn.prepare(
                        "SELECT id, account_id, name, body_html, body_text, is_default, created_at, updated_at
                         FROM signatures WHERE account_id = ?1 OR account_id IS NULL ORDER BY is_default DESC, name ASC",
                    )?;
                    let rows = stmt
                        .query_map(params![aid], map_signature)?
                        .collect::<std::result::Result<Vec<_>, rusqlite::Error>>()?;
                    Ok(rows)
                } else {
                    let mut stmt = conn.prepare(
                        "SELECT id, account_id, name, body_html, body_text, is_default, created_at, updated_at
                         FROM signatures ORDER BY is_default DESC, name ASC",
                    )?;
                    let rows = stmt
                        .query_map([], map_signature)?
                        .collect::<std::result::Result<Vec<_>, rusqlite::Error>>()?;
                    Ok(rows)
                }
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCreateSignature(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<CreateSignatureRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let id = Uuid::new_v4().to_string();
                let is_default = request.is_default.unwrap_or(false);
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                if is_default {
                    conn.execute(
                        "UPDATE signatures SET is_default = 0 WHERE account_id IS ?1",
                        params![request.account_id],
                    )?;
                }

                conn.execute(
                    "INSERT INTO signatures (id, account_id, name, body_html, body_text, is_default) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
                    params![id, request.account_id, request.name, request.body_html, request.body_text, is_default],
                )?;

                let sig = conn.query_row(
                    "SELECT id, account_id, name, body_html, body_text, is_default, created_at, updated_at FROM signatures WHERE id = ?1",
                    params![id],
                    map_signature,
                )?;
                Ok(sig)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateSignature(
    id: *const c_char,
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<CreateSignatureRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let is_default = request.is_default.unwrap_or(false);
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                if is_default {
                    conn.execute(
                        "UPDATE signatures SET is_default = 0 WHERE account_id IS ?1",
                        params![request.account_id],
                    )?;
                }

                conn.execute(
                    "UPDATE signatures SET account_id = ?1, name = ?2, body_html = ?3, body_text = ?4, is_default = ?5, updated_at = datetime('now') WHERE id = ?6",
                    params![request.account_id, request.name, request.body_html, request.body_text, is_default, id],
                )?;

                let sig = conn.query_row(
                    "SELECT id, account_id, name, body_html, body_text, is_default, created_at, updated_at FROM signatures WHERE id = ?1",
                    params![id],
                    map_signature,
                )?;
                Ok(sig)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteSignature(
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
                conn.execute("DELETE FROM signatures WHERE id = ?1", params![id])?;
                Ok("{}".to_string())
            })
        })
    })
}

// Templates Model definitions matching Tauri commands

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct EmailTemplate {
    pub id: String,
    pub name: String,
    pub subject: String,
    pub body_html: String,
    pub body_text: String,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct CreateTemplateRequest {
    pub name: String,
    pub subject: String,
    pub body_html: String,
    pub body_text: String,
}

fn map_template(row: &rusqlite::Row) -> rusqlite::Result<EmailTemplate> {
    Ok(EmailTemplate {
        id: row.get("id")?,
        name: row.get("name")?,
        subject: row.get("subject")?,
        body_html: row.get("body_html")?,
        body_text: row.get("body_text")?,
        created_at: row.get("created_at")?,
        updated_at: row.get("updated_at")?,
    })
}

// Template FFI Functions

#[no_mangle]
pub extern "C" fn MahoMailListTemplates(
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = conn.prepare(
                    "SELECT id, name, subject, body_html, body_text, created_at, updated_at
                     FROM email_templates ORDER BY name ASC",
                )?;
                let rows = stmt
                    .query_map([], map_template)?
                    .collect::<std::result::Result<Vec<_>, rusqlite::Error>>()?;
                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCreateTemplate(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<CreateTemplateRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let id = Uuid::new_v4().to_string();
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "INSERT INTO email_templates (id, name, subject, body_html, body_text) VALUES (?1, ?2, ?3, ?4, ?5)",
                    params![id, request.name, request.subject, request.body_html, request.body_text],
                )?;

                let tpl = conn.query_row(
                    "SELECT id, name, subject, body_html, body_text, created_at, updated_at FROM email_templates WHERE id = ?1",
                    params![id],
                    map_template,
                )?;
                Ok(tpl)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateTemplate(
    id: *const c_char,
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(request) = serde_json::from_str::<CreateTemplateRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "UPDATE email_templates SET name = ?1, subject = ?2, body_html = ?3, body_text = ?4, updated_at = datetime('now') WHERE id = ?5",
                    params![request.name, request.subject, request.body_html, request.body_text, id],
                )?;

                let tpl = conn.query_row(
                    "SELECT id, name, subject, body_html, body_text, created_at, updated_at FROM email_templates WHERE id = ?1",
                    params![id],
                    map_template,
                )?;
                Ok(tpl)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteTemplate(
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
                conn.execute("DELETE FROM email_templates WHERE id = ?1", params![id])?;
                Ok("{}".to_string())
            })
        })
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::{Condvar, Mutex};
    use std::time::{Duration, Instant};

    struct Capture {
        slot: Mutex<Option<(bool, String)>>,
        count: AtomicUsize,
        cv: Condvar,
    }

    impl Capture {
        fn new() -> Self {
            Self {
                slot: Mutex::new(None),
                count: AtomicUsize::new(0),
                cv: Condvar::new(),
            }
        }

        fn wait(&self, ms: u64) -> Option<(bool, String)> {
            let deadline = Instant::now() + Duration::from_millis(ms);
            let mut guard = self.slot.lock().unwrap();
            while guard.is_none() {
                let now = Instant::now();
                if now >= deadline {
                    break;
                }
                let (next, _) = self.cv.wait_timeout(guard, deadline - now).unwrap();
                guard = next;
            }
            guard.clone()
        }

        fn as_user_data(&self) -> *mut c_void {
            std::ptr::from_ref(self).cast::<c_void>().cast_mut()
        }
    }

    unsafe extern "C" fn capture_cb(ok: bool, json: *const c_char, user_data: *mut c_void) {
        let capture = unsafe { &*user_data.cast::<Capture>() };
        let payload = if json.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(json) }
                .to_string_lossy()
                .into_owned()
        };
        capture.count.fetch_add(1, Ordering::SeqCst);
        *capture.slot.lock().unwrap() = Some((ok, payload));
        capture.cv.notify_all();
    }

    fn setup_test_ctx() -> std::sync::MutexGuard<'static, ()> {
        let guard = crate::test_support::global_ctx_guard();
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx_arc(pool);
        let _ = crate::state::set_ctx(ctx);
        guard
    }

    #[test]
    fn test_signatures_crud() {
        let _ctx_guard = setup_test_ctx();

        // 1. List initially empty
        let capture = Capture::new();
        let c_acc = std::ffi::CString::new("acc1").unwrap();
        assert!(MahoMailListSignatures(
            c_acc.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let signatures: Vec<Signature> = serde_json::from_str(&json).unwrap();
        assert!(signatures.is_empty());

        // 2. Create signature
        let req = CreateSignatureRequest {
            account_id: Some("acc1".to_string()),
            name: "Work Signature".to_string(),
            body_html: "<p>Best regards</p>".to_string(),
            body_text: "Best regards".to_string(),
            is_default: Some(true),
        };
        let req_json = std::ffi::CString::new(serde_json::to_string(&req).unwrap()).unwrap();
        let capture = Capture::new();
        assert!(MahoMailCreateSignature(
            req_json.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let created: Signature = serde_json::from_str(&json).unwrap();
        assert_eq!(created.name, "Work Signature");
        assert!(created.is_default);

        // 3. Update signature
        let req_update = CreateSignatureRequest {
            account_id: Some("acc1".to_string()),
            name: "Work Signature v2".to_string(),
            body_html: "<p>Best regards v2</p>".to_string(),
            body_text: "Best regards v2".to_string(),
            is_default: Some(false),
        };
        let req_update_json =
            std::ffi::CString::new(serde_json::to_string(&req_update).unwrap()).unwrap();
        let id_c = std::ffi::CString::new(created.id.clone()).unwrap();
        let capture = Capture::new();
        assert!(MahoMailUpdateSignature(
            id_c.as_ptr(),
            req_update_json.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let updated: Signature = serde_json::from_str(&json).unwrap();
        assert_eq!(updated.name, "Work Signature v2");
        assert!(!updated.is_default);

        // 4. Delete signature
        let capture = Capture::new();
        assert!(MahoMailDeleteSignature(
            id_c.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, _) = capture.wait(5000).expect("callback fired");
        assert!(ok);
    }

    #[test]
    fn test_templates_crud() {
        let _ctx_guard = setup_test_ctx();

        // 1. List initially empty
        let capture = Capture::new();
        assert!(MahoMailListTemplates(
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let templates: Vec<EmailTemplate> = serde_json::from_str(&json).unwrap();
        assert!(templates.is_empty());

        // 2. Create template
        let req = CreateTemplateRequest {
            name: "Welcome".to_string(),
            subject: "Welcome to Maho!".to_string(),
            body_html: "<p>Hi!</p>".to_string(),
            body_text: "Hi!".to_string(),
        };
        let req_json = std::ffi::CString::new(serde_json::to_string(&req).unwrap()).unwrap();
        let capture = Capture::new();
        assert!(MahoMailCreateTemplate(
            req_json.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let created: EmailTemplate = serde_json::from_str(&json).unwrap();
        assert_eq!(created.name, "Welcome");

        // 3. Update template
        let req_update = CreateTemplateRequest {
            name: "Welcome v2".to_string(),
            subject: "Welcome to Maho! v2".to_string(),
            body_html: "<p>Hi v2!</p>".to_string(),
            body_text: "Hi v2!".to_string(),
        };
        let req_update_json =
            std::ffi::CString::new(serde_json::to_string(&req_update).unwrap()).unwrap();
        let id_c = std::ffi::CString::new(created.id.clone()).unwrap();
        let capture = Capture::new();
        assert!(MahoMailUpdateTemplate(
            id_c.as_ptr(),
            req_update_json.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let updated: EmailTemplate = serde_json::from_str(&json).unwrap();
        assert_eq!(updated.name, "Welcome v2");

        // 4. Delete template
        let capture = Capture::new();
        assert!(MahoMailDeleteTemplate(
            id_c.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, _) = capture.wait(5000).expect("callback fired");
        assert!(ok);
    }

    #[test]
    fn test_contacts_crud() {
        let _ctx_guard = setup_test_ctx();

        // 1. Create a contact group
        let member_emails = vec!["a@example.com".to_string(), "b@example.com".to_string()];
        let member_emails_json =
            std::ffi::CString::new(serde_json::to_string(&member_emails).unwrap()).unwrap();
        let acc_c = std::ffi::CString::new("acc1").unwrap();
        let name_c = std::ffi::CString::new("Test Group").unwrap();

        let capture = Capture::new();
        assert!(MahoMailCreateContactGroup(
            acc_c.as_ptr(),
            name_c.as_ptr(),
            member_emails_json.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let created: ContactGroup = serde_json::from_str(&json).unwrap();
        assert_eq!(created.name, "Test Group");
        assert_eq!(created.member_emails, member_emails);

        // 2. Search contact groups
        let query_c = std::ffi::CString::new("Test").unwrap();
        let capture = Capture::new();
        assert!(MahoMailSearchContactGroups(
            acc_c.as_ptr(),
            query_c.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let groups: Vec<ContactGroup> = serde_json::from_str(&json).unwrap();
        assert_eq!(groups.len(), 1);
        assert_eq!(groups[0].id, created.id);

        // 3. Update contact group
        let updated_emails = vec!["c@example.com".to_string()];
        let updated_emails_json =
            std::ffi::CString::new(serde_json::to_string(&updated_emails).unwrap()).unwrap();
        let new_name_c = std::ffi::CString::new("Updated Group").unwrap();
        let id_c = std::ffi::CString::new(created.id.clone()).unwrap();

        let capture = Capture::new();
        assert!(MahoMailUpdateContactGroup(
            id_c.as_ptr(),
            new_name_c.as_ptr(),
            updated_emails_json.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, json) = capture.wait(5000).expect("callback fired");
        assert!(ok);
        let updated: ContactGroup = serde_json::from_str(&json).unwrap();
        assert_eq!(updated.name, "Updated Group");
        assert_eq!(updated.member_emails, updated_emails);

        // 4. Delete contact group
        let capture = Capture::new();
        assert!(MahoMailDeleteContactGroup(
            id_c.as_ptr(),
            Some(capture_cb),
            capture.as_user_data()
        ));
        let (ok, _) = capture.wait(5000).expect("callback fired");
        assert!(ok);
    }
}
