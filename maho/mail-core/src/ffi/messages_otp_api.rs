// Copyright 2026 Maho Browser. All rights reserved.

//! One-time-code extraction from the macOS Messages store (iMessage + SMS
//! relayed through Text Message Forwarding).
//!
//! The store is `~/Library/Messages/chat.db`, owned by `imagent`. It is opened
//! read-only and `query_only`, so this module never writes to it. Reading it
//! needs Full Disk Access; without it the open fails and the call resolves to
//! an empty result, exactly like "no code found", instead of an error.

use std::ffi::c_void;
use std::os::raw::c_char;
use std::path::{Path, PathBuf};
use std::sync::atomic::AtomicPtr;

use chrono::{DateTime, Utc};
use serde_json::json;

use crate::ffi::non_empty;
use crate::ffi::read_api::{accept_read_call, spawn_read_task, MahoMailReadCallback};
use crate::otp::{best_otp_candidate, OtpCandidate, OtpMessage};

#[cfg(test)]
#[path = "messages_otp_api_tests.rs"]
mod tests;

/// Seconds between the Unix epoch and Apple's reference date (2001-01-01).
const APPLE_EPOCH_OFFSET_SECS: i64 = 978_307_200;
/// Upper bound on scanned messages; OTP windows are minutes long.
const MAX_SCANNED_MESSAGES: i64 = 50;

/// Scans recent incoming Messages for a one-time code and reports it with the
/// same JSON shape as `MahoMailExtractOtp`: a two-entry field mapping, or `[]`.
/// `page_origin` null/empty means the requesting page is unknown.
#[no_mangle]
pub extern "C" fn MahoMessagesExtractOtp(
    max_age_seconds: i64,
    page_origin: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Some(callback) = callback else {
            return false;
        };
        if max_age_seconds < 0 {
            return false;
        }
        let origin = if page_origin.is_null() {
            None
        } else {
            non_empty(page_origin, "page_origin").ok()
        };
        let Some(rt) = crate::runtime::runtime() else {
            return false;
        };
        let db_path = messages_db_path();
        let fut = Box::pin(async move {
            tokio::task::spawn_blocking(move || {
                let value = match db_path {
                    Some(path) => {
                        messages_otp_json(&path, max_age_seconds, origin.as_deref(), Utc::now())
                    }
                    None => json!([]),
                };
                serde_json::to_string(&value).map_err(crate::error::MailFfiError::from)
            })
            .await
            .map_err(|e| crate::error::MailFfiError::Internal(format!("messages join error: {e}")))?
        });
        spawn_read_task(rt, callback, AtomicPtr::new(user_data), fut);
        true
    })
}

#[cfg(test)]
pub(crate) static TEST_DB_PATH: std::sync::Mutex<Option<PathBuf>> = std::sync::Mutex::new(None);

#[cfg(test)]
fn messages_db_path() -> Option<PathBuf> {
    TEST_DB_PATH
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .clone()
}

#[cfg(all(not(test), target_os = "macos"))]
fn messages_db_path() -> Option<PathBuf> {
    let home = std::env::var_os("HOME")?;
    Some(PathBuf::from(home).join("Library/Messages/chat.db"))
}

#[cfg(all(not(test), not(target_os = "macos")))]
fn messages_db_path() -> Option<PathBuf> {
    None
}

/// Returns the field mapping for the best code in `db_path`, or `[]` when the
/// store is missing, unreadable, or holds no usable code.
pub(crate) fn messages_otp_json(
    db_path: &Path,
    max_age_seconds: i64,
    page_origin: Option<&str>,
    now: DateTime<Utc>,
) -> serde_json::Value {
    match best_messages_candidate(db_path, max_age_seconds, page_origin, now) {
        Ok(Some(cand)) => json!([
            {"field": "otp", "name": "one-time-code", "value": cand.code},
            {"field": "code", "name": "one-time-code", "value": cand.code}
        ]),
        Ok(None) => json!([]),
        Err(err) => {
            // Typically missing Full Disk Access. Only the SQLite error is
            // logged; message bodies and codes never are.
            log::warn!("[mail-ffi] Messages OTP store unreadable: {err}");
            json!([])
        }
    }
}

fn best_messages_candidate(
    db_path: &Path,
    max_age_seconds: i64,
    page_origin: Option<&str>,
    now: DateTime<Utc>,
) -> rusqlite::Result<Option<OtpCandidate>> {
    if !db_path.exists() {
        return Ok(None);
    }
    // Plain read-only rather than `immutable=1`: chat.db runs in WAL mode and
    // the newest messages - the ones carrying a fresh code - often live only in
    // the WAL until the next checkpoint, which `immutable` would skip.
    let conn = rusqlite::Connection::open_with_flags(
        db_path,
        rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY | rusqlite::OpenFlags::SQLITE_OPEN_NO_MUTEX,
    )?;
    conn.pragma_update(None, "query_only", true)?;

    let cutoff_unix = now.timestamp().saturating_sub(max_age_seconds);
    let cutoff_apple_ns = cutoff_unix
        .saturating_sub(APPLE_EPOCH_OFFSET_SECS)
        .saturating_mul(1_000_000_000);

    let mut stmt = conn.prepare(
        "SELECT m.text, m.attributedBody, m.date, h.id
         FROM message m
         LEFT JOIN handle h ON m.handle_id = h.ROWID
         WHERE m.is_from_me = 0 AND m.date >= ?1
         ORDER BY m.date DESC
         LIMIT ?2",
    )?;
    let rows = stmt.query_map(rusqlite::params![cutoff_apple_ns, MAX_SCANNED_MESSAGES], |row| {
        Ok((
            row.get::<_, Option<String>>(0)?,
            row.get::<_, Option<Vec<u8>>>(1)?,
            row.get::<_, i64>(2)?,
            row.get::<_, Option<String>>(3)?,
        ))
    })?;

    let mut best: Option<OtpCandidate> = None;
    for row in rows {
        let (text, attributed, date, sender) = row?;
        let body = match text.filter(|t| !t.trim().is_empty()) {
            Some(text) => text,
            None => match attributed.as_deref().and_then(attributed_body_text) {
                Some(text) => text,
                None => continue,
            },
        };
        let Some(received_at) = DateTime::from_timestamp(apple_date_to_unix(date), 0) else {
            continue;
        };
        let msg = OtpMessage {
            sender_email: sender.as_deref(),
            recipient_email: None,
            page_origin,
            subject: "",
            body_text: &body,
            body_html: "",
            received_at,
            now,
        };
        if let Some(cand) = best_otp_candidate(&msg) {
            // Rows are newest first, so a tie keeps the most recent code.
            if best.as_ref().is_none_or(|cur| cand.confidence > cur.confidence) {
                best = Some(cand);
            }
        }
    }
    Ok(best)
}

/// `message.date` is nanoseconds since 2001-01-01 on macOS 10.13+, seconds on
/// older stores.
fn apple_date_to_unix(apple_date: i64) -> i64 {
    let secs = if apple_date.abs() > 10_000_000_000 {
        apple_date / 1_000_000_000
    } else {
        apple_date
    };
    secs + APPLE_EPOCH_OFFSET_SECS
}

/// Recovers the plain string from a `message.attributedBody` typedstream
/// (an archived `NSAttributedString`). Recent macOS versions store many
/// incoming messages only there, leaving `message.text` NULL.
///
/// Layout after the `NSString` class name: a few type bytes, the `+` string
/// marker, then a length - one byte, or `0x81` + u16 LE, or `0x82` + u32 LE -
/// followed by that many UTF-8 bytes.
pub(crate) fn attributed_body_text(blob: &[u8]) -> Option<String> {
    const CLASS: &[u8] = b"NSString";
    let class_end = blob.windows(CLASS.len()).position(|w| w == CLASS)? + CLASS.len();
    let rest = blob.get(class_end..)?;
    let marker = rest.iter().take(16).position(|&b| b == b'+')?;
    let mut idx = marker + 1;
    let lead = *rest.get(idx)?;
    idx += 1;
    let len = match lead {
        0x81 => {
            let bytes = rest.get(idx..idx + 2)?;
            idx += 2;
            usize::from(u16::from_le_bytes([bytes[0], bytes[1]]))
        }
        0x82 => {
            let bytes = rest.get(idx..idx + 4)?;
            idx += 4;
            usize::try_from(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])).ok()?
        }
        n => usize::from(n),
    };
    let text = std::str::from_utf8(rest.get(idx..idx.checked_add(len)?)?).ok()?;
    (!text.trim().is_empty()).then(|| text.to_string())
}
