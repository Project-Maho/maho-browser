// Copyright 2026 Maho Browser. All rights reserved.

use chrono::{DateTime, Utc};
use serde_json::json;
use std::ffi::c_void;
use std::os::raw::c_char;

use crate::error::MailFfiError;
use crate::ffi::non_empty;
use crate::ffi::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use crate::otp::{best_otp_candidate, OtpMessage};

#[cfg(test)]
#[path = "otp_api_tests.rs"]
mod tests;


#[no_mangle]
pub extern "C" fn MahoMailExtractOtp(
    account_id: *const c_char,
    folder_id: *const c_char,
    query: *const c_char,
    max_age_seconds: i64,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let account_filter = if account_id.is_null() {
            None
        } else {
            non_empty(account_id, "account_id").ok()
        };
        let folder_filter = if folder_id.is_null() {
            None
        } else {
            non_empty(folder_id, "folder_id").ok()
        };
        let query_filter = if query.is_null() {
            None
        } else {
            non_empty(query, "query").ok()
        };

        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;

                // Fetch emails newer than max_age_seconds
                let cutoff = Utc::now() - chrono::Duration::seconds(max_age_seconds);
                let cutoff_str = cutoff.to_rfc3339();

                let mut sql = "SELECT subject, snippet, body_text, body_html, date, from_address 
                               FROM emails 
                               WHERE date >= ?1"
                    .to_string();

                let mut params: Vec<rusqlite::types::Value> =
                    vec![rusqlite::types::Value::Text(cutoff_str)];

                if let Some(ref acc) = account_filter {
                    sql.push_str(" AND account_id = ?2");
                    params.push(rusqlite::types::Value::Text(acc.clone()));
                } else {
                    sql.push_str(" AND (1=1 OR ?2 IS NULL)"); // placeholder to keep index simple
                    params.push(rusqlite::types::Value::Null);
                }

                if let Some(ref fld) = folder_filter {
                    sql.push_str(" AND folder_id = ?3");
                    params.push(rusqlite::types::Value::Text(fld.clone()));
                } else {
                    sql.push_str(" AND (1=1 OR ?3 IS NULL)");
                    params.push(rusqlite::types::Value::Null);
                }

                if let Some(ref q) = query_filter {
                    sql.push_str(" AND (subject LIKE ?4 OR snippet LIKE ?4 OR body_text LIKE ?4)");
                    params.push(rusqlite::types::Value::Text(format!("%{}%", q)));
                } else {
                    sql.push_str(" AND (1=1 OR ?4 IS NULL)");
                    params.push(rusqlite::types::Value::Null);
                }

                sql.push_str(" ORDER BY date DESC LIMIT 20");

                let mut stmt = conn
                    .prepare(&sql)
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                let rows = stmt
                    .query_map(rusqlite::params_from_iter(params), |row| {
                        let subject: String = row.get(0)?;
                        let snippet: String = row.get(1)?;
                        let body_text: Option<String> = row.get(2)?;
                        let body_html: Option<String> = row.get(3)?;
                        let date_str: String = row.get(4)?;
                        let from_address: Option<String> = row.get(5)?;
                        Ok((
                            subject,
                            snippet,
                            body_text,
                            body_html,
                            date_str,
                            from_address,
                        ))
                    })
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;

                let mut best_candidate = None;

                for row_res in rows {
                    if let Ok((subject, snippet, body_text, body_html, date_str, from_address)) =
                        row_res
                    {
                        let parsed_date = DateTime::parse_from_rfc3339(&date_str)
                            .map(|dt| dt.with_timezone(&Utc))
                            .unwrap_or(cutoff);

                        let body_t = body_text.unwrap_or(snippet);
                        let body_h = body_html.unwrap_or_default();

                        let msg = OtpMessage {
                            sender_email: from_address.as_deref(),
                            recipient_email: None,
                            page_origin: None,
                            subject: &subject,
                            body_text: &body_t,
                            body_html: &body_h,
                            received_at: parsed_date,
                            now: Utc::now(),
                        };

                        if let Some(cand) = best_otp_candidate(&msg) {
                            match best_candidate {
                                None => best_candidate = Some(cand),
                                Some(ref current) if cand.confidence > current.confidence => {
                                    best_candidate = Some(cand);
                                }
                                _ => {}
                            }
                        }
                    }
                }

                if let Some(cand) = best_candidate {
                    let mappings = json!([
                        {"field": "otp", "name": "one-time-code", "value": cand.code},
                        {"field": "code", "name": "one-time-code", "value": cand.code}
                    ]);
                    Ok(mappings)
                } else {
                    Ok(json!([]))
                }
            })
        })
    })
}
