// Copyright 2026 Maho Browser. All rights reserved.

use std::ffi::c_void;
use std::os::raw::c_char;

use chrono::Utc;
use rusqlite::params;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::account::resolve_account_auth;
use crate::error::{MailFfiError, Result};
use crate::ffi::read_api::{accept_read, accept_read_call, blocking_json, MahoMailReadCallback};
use crate::state::AppCtx;

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

fn opt_string(ptr: *const c_char, name: &'static str) -> Result<Option<String>> {
    if ptr.is_null() {
        Ok(None)
    } else {
        let val = c_string(ptr, name)?;
        if val.trim().is_empty() {
            Ok(None)
        } else {
            Ok(Some(val))
        }
    }
}

// --- Google Calendar API Structs ---
#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct GoogleCalendarSyncResult {
    pub calendars_synced: u32,
    pub events_upserted: u32,
    pub events_deleted: u32,
    pub reauthorize_required: bool,
}

#[derive(Debug, Deserialize)]
struct GCalListResponse {
    items: Vec<GCalListItem>,
}

#[derive(Debug, Deserialize, Clone)]
#[allow(dead_code)]
struct GCalListItem {
    id: String,
    #[serde(default)]
    summary: Option<String>,
    #[serde(default)]
    primary: Option<bool>,
    #[serde(default, rename = "accessRole")]
    access_role: Option<String>,
    #[serde(default, rename = "backgroundColor")]
    background_color: Option<String>,
    #[serde(default, rename = "foregroundColor")]
    foreground_color: Option<String>,
}

#[derive(Debug, Deserialize)]
struct GEventsResponse {
    #[serde(default)]
    items: Vec<GEvent>,
    #[serde(default, rename = "nextPageToken")]
    next_page_token: Option<String>,
    #[serde(default, rename = "nextSyncToken")]
    next_sync_token: Option<String>,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub(crate) struct GEvent {
    pub id: String,
    #[serde(default)]
    pub status: Option<String>,
    #[serde(default)]
    pub summary: Option<String>,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub location: Option<String>,
    #[serde(default)]
    pub organizer: Option<GOrganizer>,
    #[serde(default)]
    pub start: Option<GTime>,
    #[serde(default)]
    pub end: Option<GTime>,
    #[serde(default)]
    pub recurrence: Option<Vec<String>>,
    #[serde(default, rename = "iCalUID")]
    pub ical_uid: Option<String>,
    #[serde(default)]
    pub attendees: Option<Vec<GAttendee>>,
    #[serde(default, rename = "recurringEventId")]
    pub recurring_event_id: Option<String>,
    #[serde(default, rename = "colorId")]
    pub color_id: Option<String>,
    #[serde(default, rename = "hangoutLink")]
    pub hangout_link: Option<String>,
    #[serde(default)]
    pub reminders: Option<GReminders>,
    #[serde(default, rename = "eventType")]
    pub event_type: Option<String>,
    #[serde(default)]
    pub updated: Option<String>,
    #[serde(default)]
    pub sequence: Option<i64>,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub(crate) struct GOrganizer {
    #[serde(default)]
    pub email: Option<String>,
    #[serde(default, rename = "displayName")]
    pub display_name: Option<String>,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub(crate) struct GTime {
    #[serde(default, rename = "dateTime")]
    pub date_time: Option<String>,
    #[serde(default)]
    pub date: Option<String>,
    #[serde(default, rename = "timeZone")]
    pub time_zone: Option<String>,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub(crate) struct GAttendee {
    #[serde(default)]
    pub email: Option<String>,
    #[serde(default, rename = "displayName")]
    pub display_name: Option<String>,
    #[serde(default, rename = "self")]
    pub is_self: Option<bool>,
    #[serde(default, rename = "responseStatus")]
    pub response_status: Option<String>,
    #[serde(default)]
    pub organizer: Option<bool>,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub(crate) struct GReminders {
    #[serde(default, rename = "useDefault")]
    pub use_default: Option<bool>,
    #[serde(default)]
    pub overrides: Option<Vec<GReminderOverride>>,
}

#[derive(Debug, Deserialize, Serialize, Clone)]
pub(crate) struct GReminderOverride {
    pub method: String,
    pub minutes: i32,
}

#[derive(Debug, Clone, Serialize)]
pub struct GoogleCalendarEntry {
    pub id: String,
    pub account_id: String,
    pub calendar_id: String,
    pub summary: String,
    pub background_color: Option<String>,
    pub foreground_color: Option<String>,
    pub is_primary: bool,
    pub access_role: Option<String>,
    pub visible: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CalendarCategoryRecord {
    pub id: String,
    pub account_id: String,
    pub name: String,
    pub color: String,
    pub created_at: String,
}

// --- Request Structs for JSON Deserialization ---
#[derive(Debug, Clone, Deserialize)]
pub struct CreateEventRequest {
    pub account_id: String,
    pub summary: String,
    pub description: Option<String>,
    pub dtstart: String,
    pub dtend: Option<String>,
    pub location: Option<String>,
    pub all_day: bool,
    pub add_meet: Option<bool>,
    pub attendees: Option<Vec<String>>,
    pub color: Option<String>,
    pub time_zone: Option<String>,
    pub calendar_id: Option<String>,
    pub category: Option<String>,
    pub travel_time_minutes: Option<i64>,
    pub event_type: Option<String>,
    pub reminders: Option<serde_json::Value>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct UpdateEventRequest {
    pub event_id: String,
    pub summary: String,
    pub description: Option<String>,
    pub dtstart: String,
    pub dtend: Option<String>,
    pub location: Option<String>,
    pub all_day: bool,
    pub attendees: Option<Vec<String>>,
    pub color: Option<String>,
    pub time_zone: Option<String>,
    pub edit_scope: Option<String>,
    pub target_calendar_id: Option<String>,
    pub category: Option<String>,
    pub travel_time_minutes: Option<i64>,
    pub event_type: Option<String>,
    pub reminders: Option<serde_json::Value>,
}

#[derive(Debug, Deserialize)]
pub struct ExportCalendarIcsRequest {
    pub account_id: String,
    pub calendar_ids: Option<Vec<String>>,
    pub from_date: Option<String>,
    pub to_date: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct GoogleCalendarFreeBusyRequest {
    pub account_id: String,
    pub emails: Vec<String>,
    pub time_min: String,
    pub time_max: String,
}

// --- RSVP Structs ---
#[derive(Debug, Clone)]
pub(crate) struct IcsRsvpContext {
    pub account_id: String,
    pub event_id: String,
    pub rsvp_status: String,
    pub uid: String,
    pub sequence: String,
    pub dtstart: String,
    pub dtend: Option<String>,
    pub start_tz: Option<String>,
    pub end_tz: Option<String>,
    pub summary: String,
    pub organizer_line: String,
    pub organizer_email: String,
    pub attendee_email: String,
    pub attendee_name: Option<String>,
    pub email_id: Option<String>,
}

pub(crate) struct IcsReplyParams {
    pub uid: String,
    pub sequence: String,
    pub dtstart: String,
    pub dtend: Option<String>,
    pub start_tz: Option<String>,
    pub end_tz: Option<String>,
    pub summary: String,
    pub organizer_line: String,
    pub attendee_email: String,
    pub attendee_name: Option<String>,
    pub partstat: String,
}

// --- URL Encoding Helper ---
fn url_encode(s: &str) -> String {
    let mut encoded = String::new();
    for b in s.bytes() {
        match b {
            b'a'..=b'z' | b'A'..=b'Z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                encoded.push(b as char);
            }
            _ => {
                encoded.push_str(&format!("%{:02X}", b));
            }
        }
    }
    encoded
}

// --- Time / ICS Formatting and RSVP Helpers ---
fn map_rsvp(google_status: &str) -> Option<String> {
    match google_status {
        "accepted" => Some("accepted".into()),
        "declined" => Some("declined".into()),
        "tentative" => Some("tentative".into()),
        "needsAction" => Some("needs_action".into()),
        _ => None,
    }
}

fn pick_start(t: &GTime) -> (Option<String>, bool) {
    if let Some(dt) = &t.date_time {
        (Some(dt.clone()), false)
    } else if let Some(d) = &t.date {
        (Some(d.clone()), true)
    } else {
        (None, false)
    }
}

fn pick_end(t: &GTime) -> Option<String> {
    t.date_time.clone().or_else(|| t.date.clone())
}

fn normalize_datetime(dt_str: &str) -> String {
    if let Ok(dt) = chrono::DateTime::parse_from_rfc3339(dt_str) {
        dt.with_timezone(&chrono::Utc)
            .to_rfc3339_opts(chrono::SecondsFormat::Secs, true)
    } else {
        dt_str.to_string()
    }
}

fn shift_datetime_by_1h(dt_str: &str) -> String {
    if let Ok(dt) = chrono::DateTime::parse_from_rfc3339(dt_str) {
        let shifted = dt + chrono::Duration::hours(1);
        shifted.to_rfc3339()
    } else if let Ok(ndt) = chrono::NaiveDateTime::parse_from_str(dt_str, "%Y-%m-%dT%H:%M:%S") {
        let shifted = ndt + chrono::Duration::hours(1);
        shifted.format("%Y-%m-%dT%H:%M:%S").to_string()
    } else if let Ok(ndt) = chrono::NaiveDateTime::parse_from_str(dt_str, "%Y-%m-%dT%H:%M:%SZ") {
        let shifted = ndt + chrono::Duration::hours(1);
        shifted.format("%Y-%m-%dT%H:%M:%SZ").to_string()
    } else if dt_str.len() == 10 {
        if let Ok(date) = chrono::NaiveDate::parse_from_str(dt_str, "%Y-%m-%d") {
            let shifted = date + chrono::Duration::days(1);
            shifted.format("%Y-%m-%d").to_string()
        } else {
            dt_str.to_string()
        }
    } else {
        dt_str.to_string()
    }
}

fn truncate(s: &str, max_len: usize) -> String {
    if s.chars().count() <= max_len {
        s.to_string()
    } else {
        let trunc: String = s.chars().take(max_len.saturating_sub(1)).collect();
        format!("{}…", trunc)
    }
}

pub(crate) fn is_all_day(dtstart: &str) -> bool {
    dtstart.len() == 8 && dtstart.chars().all(|c| c.is_ascii_digit())
}

fn strip_html_tags(input: &str) -> String {
    let mut result = String::with_capacity(input.len());
    let mut in_tag = false;
    for ch in input.chars() {
        match ch {
            '<' => in_tag = true,
            '>' => in_tag = false,
            _ if !in_tag => result.push(ch),
            _ => {}
        }
    }
    result
}

fn extract_ics_from_text(body: &str, blocks: &mut Vec<String>) {
    let mut search_from = 0;
    while let Some(begin) = body[search_from..].find("BEGIN:VCALENDAR") {
        let abs_begin = search_from + begin;
        if let Some(end) = body[abs_begin..].find("END:VCALENDAR") {
            let abs_end = abs_begin + end + "END:VCALENDAR".len();
            blocks.push(body[abs_begin..abs_end].to_string());
            search_from = abs_end;
        } else {
            break;
        }
    }

    if blocks.is_empty() {
        let mut search_from_ev = 0;
        while let Some(begin) = body[search_from_ev..].find("BEGIN:VEVENT") {
            let abs_begin = search_from_ev + begin;
            if let Some(end) = body[abs_begin..].find("END:VEVENT") {
                let abs_end = abs_begin + end + "END:VEVENT".len();
                blocks.push(body[abs_begin..abs_end].to_string());
                search_from_ev = abs_end;
            } else {
                break;
            }
        }
    }
}

fn extract_ics_blocks(body_html: Option<&str>, body_text: Option<&str>) -> Vec<String> {
    let mut blocks = Vec::new();
    if let Some(text) = body_text {
        extract_ics_from_text(text, &mut blocks);
    }
    if blocks.is_empty() {
        if let Some(html) = body_html {
            let stripped = strip_html_tags(html);
            extract_ics_from_text(&stripped, &mut blocks);
        }
    }
    blocks
}

pub(crate) fn apply_pending_rsvp_guard(
    conn: &rusqlite::Connection,
    local_id: &str,
    incoming: Option<String>,
) -> Option<String> {
    let pending_rsvp: Option<String> = conn
        .query_row(
            "SELECT payload_json FROM pending_mutations \
             WHERE calendar_event_id = ?1 \
             AND mutation_type IN ('calendar_rsvp','calendar_rsvp_reply') \
             ORDER BY created_at DESC LIMIT 1",
            params![local_id],
            |row| row.get(0),
        )
        .ok()
        .flatten();

    match pending_rsvp {
        Some(raw) => {
            let extracted = match serde_json::from_str::<serde_json::Value>(&raw) {
                Ok(v) => v
                    .get("rsvp_status")
                    .and_then(|s| s.as_str())
                    .map(String::from)
                    .unwrap_or(raw),
                Err(_) => raw,
            };
            Some(extracted)
        }
        None => incoming,
    }
}

pub(crate) fn escape_ics_qstring(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}

pub(crate) fn normalize_organizer_cal_address(raw: &str) -> String {
    let s = raw.trim();
    if s.is_empty() {
        return String::new();
    }
    if s.to_ascii_lowercase().contains("mailto:") {
        return s.to_string();
    }
    if let (Some(lt), Some(gt)) = (s.rfind('<'), s.rfind('>')) {
        if gt > lt + 1 {
            let name = s[..lt].trim().trim_end_matches(':').trim();
            let email = s[lt + 1..gt].trim();
            if !name.is_empty() && !email.is_empty() {
                let escaped_name = escape_ics_qstring(name);
                return format!("CN=\"{}\":mailto:{}", escaped_name, email);
            }
            if !email.is_empty() {
                return format!("mailto:{}", email);
            }
        }
    }
    format!("mailto:{}", s)
}

fn escape_ics_text(s: &str) -> String {
    s.replace('\\', "\\\\")
        .replace("\r\n", "\\n")
        .replace('\r', "\\n")
        .replace('\n', "\\n")
        .replace(';', "\\;")
        .replace(',', "\\,")
}

fn escape_ics_value(val: &str) -> String {
    val.replace('\\', "\\\\")
        .replace('\n', "\\n")
        .replace(',', "\\,")
        .replace(';', "\\;")
}

fn format_ics_datetime(iso_str: &str) -> String {
    iso_str
        .replace('-', "")
        .replace(':', "")
        .replace(".000", "")
}

fn format_ics_date_property(name: &str, value: &str, zone: Option<&str>) -> std::result::Result<String, String> {
    let value = format_ics_datetime(value);
    if is_all_day(&value) {
        return Ok(format!("{name};VALUE=DATE:{value}"));
    }
    if !value.ends_with('Z') {
        if let Some(zone) = zone.filter(|zone| !zone.is_empty()) {
            let quoted = zone.strip_prefix('"').and_then(|zone| zone.strip_suffix('"'));
            let content = quoted.unwrap_or(zone);
            if content.chars().any(|ch| ch.is_control() || ch == '"') {
                return Err("Invalid ICS timezone parameter".into());
            }
            if quoted.is_some() || content.contains([';', ':', ',']) {
                return Ok(format!("{name};TZID=\"{content}\":{value}"));
            }
            return Ok(format!("{name};TZID={content}:{value}"));
        }
    }
    Ok(format!("{name}:{value}"))
}

fn fold_line(line: &str) -> String {
    let bytes = line.as_bytes();
    if bytes.len() <= 75 {
        let mut out = line.to_string();
        out.push_str("\r\n");
        return out;
    }

    let mut result = String::new();
    let mut pos = 0;
    let mut line_start = true;

    while pos < bytes.len() {
        let limit = if line_start { 75 } else { 74 };
        let remaining = bytes.len() - pos;
        let chunk_len = if remaining <= limit {
            remaining
        } else {
            let mut end = pos + limit;
            while end > pos && (bytes[end] & 0b1100_0000) == 0b1000_0000 {
                end -= 1;
            }
            if end == pos {
                end = pos + limit;
            }
            end - pos
        };

        if !line_start {
            result.push(' ');
        }
        result.push_str(&line[pos..pos + chunk_len]);
        pos += chunk_len;

        if pos < bytes.len() {
            result.push_str("\r\n");
        }
        line_start = false;
    }

    result.push_str("\r\n");
    result
}

pub(crate) fn build_ics_reply(params: IcsReplyParams) -> std::result::Result<String, String> {
    let raw_fields = [
        params.uid.as_str(),
        params.sequence.as_str(),
        params.dtstart.as_str(),
        params.dtend.as_deref().unwrap_or_default(),
        params.organizer_line.as_str(),
        params.attendee_email.as_str(),
        params.attendee_name.as_deref().unwrap_or_default(),
        params.partstat.as_str(),
    ];
    if raw_fields.iter().any(|value| value.chars().any(char::is_control)) {
        return Err("Invalid control character in calendar reply property".into());
    }
    let valid_partstats = ["ACCEPTED", "TENTATIVE", "DECLINED"];
    if !valid_partstats.contains(&params.partstat.as_str()) {
        return Err(format!("invalid partstat: {}", params.partstat));
    }

    let dtstamp = Utc::now().format("%Y%m%dT%H%M%SZ").to_string();
    let sequence = if params.sequence.is_empty() {
        "0"
    } else {
        params.sequence.as_str()
    };

    let attendee_line = {
        let cn_part = match &params.attendee_name {
            Some(name) if !name.is_empty() => format!(";CN=\"{}\"", escape_ics_qstring(name)),
            _ => String::new(),
        };
        format!(
            "ATTENDEE;PARTSTAT={}{}:mailto:{}",
            params.partstat, cn_part, params.attendee_email
        )
    };

    let mut lines: Vec<String> = Vec::new();
    lines.push("BEGIN:VCALENDAR".to_string());
    lines.push("VERSION:2.0".to_string());
    lines.push("PRODID:-//Maho Mail//RSVP//EN".to_string());
    lines.push("METHOD:REPLY".to_string());
    lines.push("BEGIN:VEVENT".to_string());
    lines.push(format!("UID:{}", params.uid));
    lines.push(format!("DTSTAMP:{}", dtstamp));
    lines.push(format_ics_date_property("DTSTART", &params.dtstart, params.start_tz.as_deref())?);
    if let Some(ref dtend) = params.dtend {
        lines.push(format_ics_date_property("DTEND", dtend, params.end_tz.as_deref())?);
    }
    lines.push(format!("SEQUENCE:{}", sequence));
    let organizer_value = normalize_organizer_cal_address(&params.organizer_line);
    if !organizer_value.is_empty() {
        lines.push(format!("ORGANIZER:{}", organizer_value));
    }
    lines.push(attendee_line);
    lines.push(format!("SUMMARY:{}", escape_ics_text(&params.summary)));
    lines.push("END:VEVENT".to_string());
    lines.push("END:VCALENDAR".to_string());

    let mut output = String::new();
    for line in &lines {
        output.push_str(&fold_line(line));
    }

    Ok(output)
}

pub(crate) fn partstat_from_rsvp_status(status: &str) -> Option<&'static str> {
    match status {
        "accepted" => Some("ACCEPTED"),
        "tentative" => Some("TENTATIVE"),
        "declined" => Some("DECLINED"),
        _ => None,
    }
}

pub(crate) fn extract_bare_email(organizer_line: &str) -> String {
    let s = organizer_line.trim();
    if let Some(idx) = s.rfind(":mailto:") {
        return s[idx + ":mailto:".len()..]
            .trim()
            .trim_end_matches('>')
            .to_string();
    }
    if let Some(idx) = s.rfind("mailto:") {
        return s[idx + "mailto:".len()..]
            .trim()
            .trim_end_matches('>')
            .to_string();
    }
    if let (Some(lt), Some(gt)) = (s.rfind('<'), s.rfind('>')) {
        if gt > lt {
            return s[lt + 1..gt].trim().to_string();
        }
    }
    s.to_string()
}

pub(crate) fn subject_prefix_for_partstat(partstat: &str) -> &'static str {
    match partstat {
        "ACCEPTED" => "Accepted",
        "TENTATIVE" => "Tentative",
        "DECLINED" => "Declined",
        _ => "RSVP",
    }
}

// --- SMTP / RSVP Send Helper ---
#[path = "calendar_replay.rs"]
pub(crate) mod calendar_replay;

pub(crate) async fn send_ics_rsvp_reply(ctx: &AppCtx, rsvp_ctx: IcsRsvpContext) -> Result<()> {
    let _guard = calendar_replay::lock(ctx, &rsvp_ctx.account_id).await?;
    let partstat = partstat_from_rsvp_status(&rsvp_ctx.rsvp_status).ok_or_else(|| {
        maho_core::error::AppError::Validation(format!(
            "invalid rsvp_status for reply: {}",
            rsvp_ctx.rsvp_status
        ))
    })?;

    let ics_text = build_ics_reply(IcsReplyParams {
        uid: rsvp_ctx.uid.clone(),
        sequence: rsvp_ctx.sequence.clone(),
        dtstart: rsvp_ctx.dtstart.clone(),
        dtend: rsvp_ctx.dtend.clone(),
        start_tz: rsvp_ctx.start_tz.clone(),
        end_tz: rsvp_ctx.end_tz.clone(),
        summary: rsvp_ctx.summary.clone(),
        organizer_line: rsvp_ctx.organizer_line.clone(),
        attendee_email: rsvp_ctx.attendee_email.clone(),
        attendee_name: rsvp_ctx.attendee_name.clone(),
        partstat: partstat.to_string(),
    })
    .map_err(|e| maho_core::error::AppError::Validation(e))?;

    let prefix = subject_prefix_for_partstat(partstat);
    let subject = format!("{}: {}", prefix, rsvp_ctx.summary);

    let (in_reply_to_header, references_header, display_name) = {
        let db = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;

        let (in_reply_to_header, references_header) = if let Some(ref eid) = rsvp_ctx.email_id {
            let row: Option<(Option<String>, Option<String>)> = db
                .query_row(
                    "SELECT message_id, in_reply_to FROM emails WHERE id = ?1",
                    params![eid],
                    |row| Ok((row.get(0)?, row.get(1)?)),
                )
                .ok();
            match row {
                Some((msg_id, existing_refs)) => {
                    let in_reply_to = msg_id.clone();
                    let references = crate::ffi::compose_api::build_references_chain(
                        existing_refs.as_deref(),
                        msg_id.as_deref(),
                    );
                    (in_reply_to, references)
                }
                None => (None, None),
            }
        } else {
            (None, None)
        };

        let display_name: Option<String> = db.query_row(
            "SELECT display_name FROM accounts WHERE id = ?1",
            params![rsvp_ctx.account_id],
            |row| row.get(0),
        )?;
        (in_reply_to_header, references_header, display_name)
    };

    let payload = serde_json::json!({
        "ics_text": ics_text,
        "organizer_email": rsvp_ctx.organizer_email,
        "subject": subject,
        "attendee_email": rsvp_ctx.attendee_email,
        "attendee_name": rsvp_ctx.attendee_name,
        "display_name": display_name,
        "in_reply_to": in_reply_to_header,
        "references": references_header,
        "rsvp_status": rsvp_ctx.rsvp_status,
    })
    .to_string();
    let mutation = {
        let mut db = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        let tx = db.transaction()?;
        calendar_replay::check_existing(&tx, &rsvp_ctx.account_id, &rsvp_ctx.event_id)?;
        tx.execute(
            "UPDATE calendar_events SET rsvp_status = ?1, updated_at = datetime('now') WHERE id = ?2 AND account_id = ?3",
            params![rsvp_ctx.rsvp_status, rsvp_ctx.event_id, rsvp_ctx.account_id],
        )?;
        maho_core::services::offline_queue::queue_calendar_mutation(
            &tx,
            &rsvp_ctx.account_id,
            "calendar_rsvp_reply",
            &rsvp_ctx.event_id,
            Some(&payload),
        )?;
        let mutation =
            maho_core::services::offline_queue::list_pending_mutations(&tx, &rsvp_ctx.account_id)?
                .into_iter()
                .find(|m| {
                    m.mutation_type == "calendar_rsvp_reply"
                        && m.calendar_event_id.as_deref() == Some(&rsvp_ctx.event_id)
                })
                .ok_or_else(|| {
                    maho_core::error::AppError::Validation("RSVP intent was not persisted".into())
                })?;
        tx.commit()?;
        mutation
    };
    calendar_replay::replay_locked(ctx, &mutation).await?;
    Ok(())
}

/// Serialize and reload one persisted RSVP before attempting delivery.
pub(crate) async fn replay_calendar_mutation(
    ctx: &AppCtx,
    mutation: &maho_core::services::offline_queue::PendingMutation,
) -> Result<()> {
    let _guard = calendar_replay::lock(ctx, &mutation.account_id).await?;
    calendar_replay::replay_locked(ctx, mutation).await?;
    Ok(())
}

/// Transport only; durable state and acknowledgment belong to calendar_replay.
async fn deliver_calendar_mutation(
    ctx: &AppCtx,
    mutation: &maho_core::services::offline_queue::PendingMutation,
) -> Result<()> {
    match mutation.mutation_type.as_str() {
        "calendar_rsvp_reply" => {},
        "calendar_delete" | "calendar_create" | "calendar_move" => {
            return deliver_google_calendar_mutation(ctx, mutation).await;
        }
        _ => return Err(maho_core::error::AppError::Validation(format!(
            "Unsupported calendar replay: {}", mutation.mutation_type
        )).into()),
    }
    let payload: serde_json::Value =
        serde_json::from_str(mutation.payload_json.as_deref().unwrap_or(""))?;
    let required = |key: &str| -> Result<String> {
        payload
            .get(key)
            .and_then(|v| v.as_str())
            .map(str::to_owned)
            .ok_or_else(|| {
                maho_core::error::AppError::Validation(format!("Missing RSVP payload field: {key}"))
                    .into()
            })
    };
    let attendee_email = required("attendee_email")?;
    let organizer_email = required("organizer_email")?;
    let subject = required("subject")?;
    let cal_alt = maho_core::smtp_client::CalendarAlternativePart {
        content_type: "text/calendar; method=REPLY; charset=UTF-8".into(),
        ics_text: required("ics_text")?,
    };
    let display_name = payload.get("display_name").and_then(|v| v.as_str());
    let in_reply_to_header = payload
        .get("in_reply_to")
        .and_then(|v| v.as_str())
        .map(str::to_owned);
    let references_header = payload
        .get("references")
        .and_then(|v| v.as_str())
        .map(str::to_owned);
    let creds = crate::ffi::compose_api::smtp_credentials_async(ctx, &mutation.account_id).await?;
    let use_xoauth2 = matches!(&creds.auth, crate::account::ImapAuth::OAuth2 { .. });
    let from = match display_name.filter(|s| !s.is_empty()) {
        Some(name) => format!("\"{}\" <{}>", name.replace('"', "\\\""), attendee_email),
        None => attendee_email,
    };

    let body_text = subject.clone();
    calendar_replay::mark_sending(ctx, mutation)?;

    let send_result = crate::ffi::compose_api::send_smtp_message(
        creds,
        use_xoauth2,
        from,
        vec![organizer_email],
        vec![],
        vec![],
        subject.clone(),
        Some(body_text),
        None,
        in_reply_to_header.clone(),
        references_header.clone(),
        None,
        vec![],
        Some(cal_alt),
    )
    .await;

    send_result
}

// --- Google Calendar Client & Write Helpers ---
async fn deliver_google_calendar_mutation(
    ctx: &AppCtx,
    mutation: &maho_core::services::offline_queue::PendingMutation,
) -> Result<()> {
    let payload: serde_json::Value =
        serde_json::from_str(mutation.payload_json.as_deref().unwrap_or("{}"))?;
    let required = |key: &str| -> Result<&str> {
        payload.get(key).and_then(serde_json::Value::as_str)
            .filter(|value| !value.is_empty())
            .ok_or_else(|| maho_core::error::AppError::Validation(
                format!("Missing {} payload field: {key}", mutation.mutation_type)
            ).into())
    };
    if mutation.mutation_type == "calendar_delete" {
        return google_calendar_delete_event_internal(
            ctx, &mutation.account_id, required("calendar_id")?, required("external_id")?,
        ).await;
    }
    let event_id = mutation.calendar_event_id.as_deref().ok_or_else(|| {
        maho_core::error::AppError::Validation("Missing calendar event ID".into())
    })?;
    let (calendar_id, event) = if mutation.mutation_type == "calendar_create" {
        let calendar_id: String = {
            let db = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
            db.query_row(
                "SELECT google_calendar_id FROM calendar_events WHERE id=?1 AND account_id=?2",
                params![event_id, mutation.account_id], |row| row.get(0),
            )?
        };
        let event = google_calendar_create_event_internal(
            ctx, &mutation.account_id, &calendar_id, payload,
        ).await?;
        (calendar_id, event)
    } else {
        let destination = required("destination_calendar_id")?;
        let event = google_calendar_move_event_internal(
            ctx, &mutation.account_id, required("calendar_id")?, required("external_id")?, destination,
        ).await?;
        (destination.to_owned(), event)
    };
    let mut db = ctx.pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
    let tx = db.transaction()?;
    let account_email: String = tx.query_row(
        "SELECT email FROM accounts WHERE id=?1", [&mutation.account_id], |row| row.get(0),
    )?;
    // Bind the remote identity before upsert so it updates the local event,
    // preserving its ID and local-only metadata rather than inserting a duplicate.
    tx.execute(
        "UPDATE calendar_events SET source='google', external_id=?1, google_calendar_id=?2 WHERE id=?3 AND account_id=?4",
        params![event.id, calendar_id, event_id, mutation.account_id],
    )?;
    upsert_event(&tx, &mutation.account_id, Some(&account_email), &calendar_id, &event)?;
    tx.commit()?;
    Ok(())
}

async fn get_client_and_token(
    ctx: &AppCtx,
    account_id: &str,
) -> std::result::Result<(reqwest::Client, String), MailFfiError> {
    let pool = ctx.pool.clone();
    let credential_key = ctx.credential_key;
    let account_id = account_id.to_owned();
    let resolved = tokio::task::spawn_blocking(move || {
        let db = pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
        resolve_account_auth(&db, &credential_key, &account_id)
    })
    .await
    .map_err(|e| MailFfiError::Internal(format!("calendar auth task: {e}")))??;
    let access_token = match resolved.auth {
        crate::account::ImapAuth::OAuth2 { access_token } => access_token,
        _ => {
            return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                "Google Calendar sync is only available for Gmail OAuth accounts".to_string(),
            )))
        }
    };
    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(30))
        .build()
        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Network(e.to_string())))?;
    Ok((client, access_token))
}

pub(crate) async fn google_calendar_create_event_internal(
    ctx: &AppCtx,
    account_id: &str,
    calendar_id: &str,
    body: serde_json::Value,
) -> std::result::Result<GEvent, MailFfiError> {
    let (client, token) = get_client_and_token(ctx, account_id).await?;
    let enc_cal_id = url_encode(calendar_id);
    let url = format!(
        "https://www.googleapis.com/calendar/v3/calendars/{}/events?conferenceDataVersion=1",
        enc_cal_id
    );

    let resp = client
        .post(&url)
        .bearer_auth(&token)
        .json(&body)
        .send()
        .await
        .map_err(|e| {
            MailFfiError::Core(maho_core::error::AppError::Network(format!(
                "create event request: {}",
                e
            )))
        })?;

    let status = resp.status();
    let text = resp.text().await.unwrap_or_default();
    if !status.is_success() {
        return Err(MailFfiError::Core(maho_core::error::AppError::Network(
            format!("create event HTTP {}: {}", status, text),
        )));
    }

    let parsed: GEvent = serde_json::from_str(&text).map_err(|e| {
        MailFfiError::Core(maho_core::error::AppError::Network(format!(
            "create event JSON: {}",
            e
        )))
    })?;

    Ok(parsed)
}

pub(crate) async fn google_calendar_update_event_internal(
    ctx: &AppCtx,
    account_id: &str,
    calendar_id: &str,
    external_id: &str,
    patch: serde_json::Value,
) -> std::result::Result<GEvent, MailFfiError> {
    let (client, token) = get_client_and_token(ctx, account_id).await?;
    let enc_cal_id = url_encode(calendar_id);
    let enc_event_id = url_encode(external_id);
    let url = format!(
        "https://www.googleapis.com/calendar/v3/calendars/{}/events/{}",
        enc_cal_id, enc_event_id
    );

    let resp = client
        .patch(&url)
        .bearer_auth(&token)
        .json(&patch)
        .send()
        .await
        .map_err(|e| {
            MailFfiError::Core(maho_core::error::AppError::Network(format!(
                "patch event request: {}",
                e
            )))
        })?;

    let status = resp.status();
    let text = resp.text().await.unwrap_or_default();
    if !status.is_success() {
        return Err(MailFfiError::Core(maho_core::error::AppError::Network(
            format!("patch event HTTP {}: {}", status, text),
        )));
    }

    let parsed: GEvent = serde_json::from_str(&text).map_err(|e| {
        MailFfiError::Core(maho_core::error::AppError::Network(format!(
            "patch event JSON: {}",
            e
        )))
    })?;

    Ok(parsed)
}

pub(crate) async fn google_calendar_delete_event_internal(
    ctx: &AppCtx,
    account_id: &str,
    calendar_id: &str,
    external_id: &str,
) -> std::result::Result<(), MailFfiError> {
    let (client, token) = get_client_and_token(ctx, account_id).await?;
    let enc_cal_id = url_encode(calendar_id);
    let enc_event_id = url_encode(external_id);
    let url = format!(
        "https://www.googleapis.com/calendar/v3/calendars/{}/events/{}",
        enc_cal_id, enc_event_id
    );

    let resp = client
        .delete(&url)
        .bearer_auth(&token)
        .send()
        .await
        .map_err(|e| {
            MailFfiError::Core(maho_core::error::AppError::Network(format!(
                "delete event request: {}",
                e
            )))
        })?;

    let status = resp.status();
    if !status.is_success() && status.as_u16() != 404 {
        let text = resp.text().await.unwrap_or_default();
        return Err(MailFfiError::Core(maho_core::error::AppError::Network(
            format!("delete event HTTP {}: {}", status, text),
        )));
    }

    Ok(())
}

pub(crate) fn merge_rsvp_into_attendees(
    existing: &[serde_json::Value],
    my_email: &str,
    status: &str,
) -> Vec<serde_json::Value> {
    let mut found_self = false;
    let mut merged: Vec<serde_json::Value> = existing
        .iter()
        .map(|att| {
            let is_self = att.get("self").and_then(|s| s.as_bool()).unwrap_or(false)
                || att
                    .get("email")
                    .and_then(|e| e.as_str())
                    .map(|e| e.eq_ignore_ascii_case(my_email))
                    .unwrap_or(false);
            if is_self {
                found_self = true;
                let mut obj = att.as_object().cloned().unwrap_or_default();
                obj.insert(
                    "responseStatus".to_string(),
                    serde_json::Value::String(status.to_string()),
                );
                serde_json::Value::Object(obj)
            } else {
                att.clone()
            }
        })
        .collect();

    if !found_self {
        merged.push(serde_json::json!({
            "email": my_email,
            "responseStatus": status,
        }));
    }

    merged
}

pub(crate) async fn google_calendar_update_rsvp_internal(
    ctx: &AppCtx,
    account_id: &str,
    calendar_id: &str,
    external_id: &str,
    my_email: &str,
    status: &str,
) -> std::result::Result<GEvent, MailFfiError> {
    let (client, token) = get_client_and_token(ctx, account_id).await?;
    let enc_cal_id = url_encode(calendar_id);
    let enc_event_id = url_encode(external_id);
    let get_url = format!(
        "https://www.googleapis.com/calendar/v3/calendars/{}/events/{}",
        enc_cal_id, enc_event_id
    );

    let get_resp = client
        .get(&get_url)
        .bearer_auth(&token)
        .send()
        .await
        .map_err(|e| {
            MailFfiError::Core(maho_core::error::AppError::Network(format!(
                "get event request: {}",
                e
            )))
        })?;
    let get_status = get_resp.status();
    let get_text = get_resp.text().await.unwrap_or_default();
    if !get_status.is_success() {
        return Err(MailFfiError::Core(maho_core::error::AppError::Network(
            format!("get event HTTP {}: {}", get_status, get_text),
        )));
    }

    let raw: serde_json::Value = serde_json::from_str(&get_text).map_err(|e| {
        MailFfiError::Core(maho_core::error::AppError::Network(format!(
            "get event JSON: {}",
            e
        )))
    })?;

    let existing_attendees = raw
        .get("attendees")
        .and_then(|a| a.as_array())
        .cloned()
        .unwrap_or_default();

    let merged = merge_rsvp_into_attendees(&existing_attendees, my_email, status);
    let patch = serde_json::json!({ "attendees": merged });
    google_calendar_update_event_internal(ctx, account_id, calendar_id, external_id, patch).await
}

pub(crate) async fn google_calendar_move_event_internal(
    ctx: &AppCtx,
    account_id: &str,
    source_calendar_id: &str,
    external_id: &str,
    destination_calendar_id: &str,
) -> std::result::Result<GEvent, MailFfiError> {
    let (client, token) = get_client_and_token(ctx, account_id).await?;
    let enc_src_cal_id = url_encode(source_calendar_id);
    let enc_event_id = url_encode(external_id);
    let enc_dest_cal_id = url_encode(destination_calendar_id);
    let url = format!(
        "https://www.googleapis.com/calendar/v3/calendars/{}/events/{}/move?destination={}",
        enc_src_cal_id, enc_event_id, enc_dest_cal_id
    );

    let resp = client
        .post(&url)
        .bearer_auth(&token)
        .send()
        .await
        .map_err(|e| {
            MailFfiError::Core(maho_core::error::AppError::Network(format!(
                "move event request: {}",
                e
            )))
        })?;

    let status = resp.status();
    let text = resp.text().await.unwrap_or_default();
    if !status.is_success() {
        return Err(MailFfiError::Core(maho_core::error::AppError::Network(
            format!("move event HTTP {}: {}", status, text),
        )));
    }

    let parsed: GEvent = serde_json::from_str(&text).map_err(|e| {
        MailFfiError::Core(maho_core::error::AppError::Network(format!(
            "move event JSON: {}",
            e
        )))
    })?;

    Ok(parsed)
}

// --- Google Calendar Sync Internal Functions ---
async fn fetch_calendars(
    client: &reqwest::Client,
    access_token: &str,
) -> std::result::Result<Vec<GCalListItem>, MailFfiError> {
    let resp = client
        .get("https://www.googleapis.com/calendar/v3/users/me/calendarList?fields=items(id,summary,primary,accessRole,backgroundColor,foregroundColor)")
        .bearer_auth(access_token)
        .send()
        .await
        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Network(format!("calendarList request: {}", e))))?;

    let status = resp.status();
    let text = resp.text().await.unwrap_or_default();
    if !status.is_success() {
        return Err(MailFfiError::Core(maho_core::error::AppError::Network(
            format!("calendarList HTTP {}: {}", status, truncate(&text, 300)),
        )));
    }
    let parsed: GCalListResponse = serde_json::from_str(&text).map_err(|e| {
        MailFfiError::Core(maho_core::error::AppError::Network(format!(
            "calendarList JSON: {}",
            e
        )))
    })?;
    Ok(parsed
        .items
        .into_iter()
        .filter(|c| {
            matches!(
                c.access_role.as_deref(),
                Some("owner") | Some("writer") | Some("reader") | Some("freeBusyReader") | None
            )
        })
        .collect())
}

/// Calendar-level fields of an events.list response.
#[derive(Debug, Deserialize)]
struct GEventsCalendarMeta {
    #[serde(default)]
    summary: Option<String>,
    #[serde(default, rename = "accessRole")]
    access_role: Option<String>,
}

/// Calendar-list entry for the user's primary calendar. Its ID is the account
/// email, as calendarList reports it; "primary" is the API alias otherwise.
fn primary_calendar_entry(account_email: Option<&str>, meta: GEventsCalendarMeta) -> GCalListItem {
    GCalListItem {
        id: account_email
            .filter(|email| !email.is_empty())
            .unwrap_or("primary")
            .to_string(),
        summary: meta.summary,
        primary: Some(true),
        access_role: meta.access_role,
        background_color: None,
        foreground_color: None,
    }
}

/// The user's primary calendar, read through events.list: calendar.events
/// authorizes it, while calendarList needs a calendar-list scope.
async fn fetch_primary_calendar(
    client: &reqwest::Client,
    access_token: &str,
    account_email: Option<&str>,
) -> std::result::Result<GCalListItem, MailFfiError> {
    let resp = client
        .get("https://www.googleapis.com/calendar/v3/calendars/primary/events?maxResults=1&fields=summary,accessRole")
        .bearer_auth(access_token)
        .send()
        .await
        .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Network(format!("primary calendar request: {}", e))))?;

    let status = resp.status();
    let text = resp.text().await.unwrap_or_default();
    if !status.is_success() {
        return Err(MailFfiError::Core(maho_core::error::AppError::Network(
            format!("primary calendar HTTP {}: {}", status, truncate(&text, 300)),
        )));
    }
    let meta: GEventsCalendarMeta = serde_json::from_str(&text).map_err(|e| {
        MailFfiError::Core(maho_core::error::AppError::Network(format!(
            "primary calendar JSON: {}",
            e
        )))
    })?;
    Ok(primary_calendar_entry(account_email, meta))
}

#[derive(Debug)]
enum FetchEventsOutcome {
    Done {
        events: Vec<GEvent>,
        next_sync_token: Option<String>,
    },
    ResyncRequired,
}

async fn fetch_events(
    client: &reqwest::Client,
    access_token: &str,
    calendar_id: &str,
    sync_token: Option<&str>,
) -> std::result::Result<FetchEventsOutcome, MailFfiError> {
    let mut all_events: Vec<GEvent> = Vec::new();
    let mut page_token: Option<String> = None;
    let enc_cal_id = url_encode(calendar_id);

    loop {
        let mut url = format!(
            "https://www.googleapis.com/calendar/v3/calendars/{}/events?singleEvents=true&orderBy=startTime&showDeleted=true&maxResults=250",
            enc_cal_id
        );
        if let Some(token) = sync_token {
            url.push_str("&syncToken=");
            url.push_str(&url_encode(token));
        }
        if let Some(pt) = &page_token {
            url.push_str("&pageToken=");
            url.push_str(&url_encode(pt));
        }

        let resp = client
            .get(&url)
            .bearer_auth(access_token)
            .send()
            .await
            .map_err(|e| {
                MailFfiError::Core(maho_core::error::AppError::Network(format!(
                    "events.list request: {}",
                    e
                )))
            })?;

        let status = resp.status();
        if status.as_u16() == 410 {
            return Ok(FetchEventsOutcome::ResyncRequired);
        }
        let text = resp.text().await.unwrap_or_default();
        if !status.is_success() {
            return Err(MailFfiError::Core(maho_core::error::AppError::Network(
                format!("events.list HTTP {}: {}", status, truncate(&text, 300)),
            )));
        }

        let parsed: GEventsResponse = serde_json::from_str(&text).map_err(|e| {
            MailFfiError::Core(maho_core::error::AppError::Network(format!(
                "events.list JSON: {}",
                e
            )))
        })?;

        all_events.extend(parsed.items);
        if let Some(npt) = parsed.next_page_token {
            page_token = Some(npt);
            continue;
        }
        return Ok(FetchEventsOutcome::Done {
            events: all_events,
            next_sync_token: parsed.next_sync_token,
        });
    }
}

pub(crate) fn upsert_event(
    db: &rusqlite::Connection,
    account_id: &str,
    account_email: Option<&str>,
    calendar_id: &str,
    event: &GEvent,
) -> Result<UpsertOutcome> {
    if event.status.as_deref() == Some("cancelled") {
        let affected = db.execute(
            "DELETE FROM calendar_events WHERE account_id = ?1 AND source = 'google' AND external_id = ?2",
            params![account_id, event.id],
        )?;
        return Ok(if affected > 0 {
            UpsertOutcome::Deleted
        } else {
            UpsertOutcome::Skipped
        });
    }

    let start = event.start.as_ref();
    let end = event.end.as_ref();
    let (dtstart_opt, all_day) = start.map(pick_start).unwrap_or((None, false));
    let dtstart = match dtstart_opt {
        Some(s) => {
            if all_day {
                s
            } else {
                normalize_datetime(&s)
            }
        }
        None => return Ok(UpsertOutcome::Skipped),
    };
    let dtend_raw = end.and_then(pick_end);
    let dtend = dtend_raw.map(|s| if all_day { s } else { normalize_datetime(&s) });

    let start_tz = start.and_then(|t| t.time_zone.clone());
    let end_tz = end.and_then(|t| t.time_zone.clone());

    let organizer = event
        .organizer
        .as_ref()
        .and_then(|o| o.email.clone().or_else(|| o.display_name.clone()));

    let rrule = event
        .recurrence
        .as_ref()
        .and_then(|lines| lines.iter().find(|l| l.to_uppercase().starts_with("RRULE")))
        .cloned();

    let mut rsvp_status = account_email.and_then(|me| {
        event.attendees.as_ref().and_then(|atts| {
            atts.iter()
                .find(|a| {
                    a.is_self == Some(true)
                        || a.email
                            .as_deref()
                            .map(|e| e.eq_ignore_ascii_case(me))
                            .unwrap_or(false)
                })
                .and_then(|a| a.response_status.as_deref().and_then(map_rsvp))
        })
    });

    let summary = event.summary.clone().unwrap_or_default();
    let status = event
        .status
        .clone()
        .unwrap_or_else(|| "confirmed".to_string());
    let uid = event.ical_uid.clone().unwrap_or_else(|| event.id.clone());

    let attendees_json = event
        .attendees
        .as_ref()
        .and_then(|a| serde_json::to_string(a).ok());
    let reminders_json = event
        .reminders
        .as_ref()
        .and_then(|r| serde_json::to_string(r).ok());
    let sequence_str = event.sequence.unwrap_or(0).to_string();

    let now = chrono::Utc::now().to_rfc3339();
    let existing_rsvp_and_id: Option<(Option<String>, String)> = db
        .query_row(
            "SELECT rsvp_status, id FROM calendar_events WHERE account_id = ?1 AND source = 'google' AND external_id = ?2",
            params![account_id, event.id],
            |row| Ok((row.get::<_, Option<String>>(0)?, row.get::<_, String>(1)?)),
        )
        .ok();

    let mut is_auto_declined = false;
    if rsvp_status.as_deref() == Some("declined") {
        if let Some((ref old_rsvp, ref local_uuid)) = existing_rsvp_and_id {
            if old_rsvp.as_deref() != Some("declined") {
                let is_local_update = db.query_row(
                    "SELECT 1 FROM pending_mutations WHERE account_id = ?1 AND calendar_event_id = ?2 AND mutation_type = 'calendar_update'",
                    params![account_id, local_uuid],
                    |_row| Ok(true),
                ).unwrap_or(false);

                if !is_local_update {
                    let time_ok = if let Some(ref upd_str) = event.updated {
                        if let Ok(upd_dt) = chrono::DateTime::parse_from_rfc3339(upd_str) {
                            let diff = chrono::Utc::now()
                                .signed_duration_since(upd_dt.with_timezone(&chrono::Utc));
                            diff.num_minutes().abs() < 5
                        } else {
                            true
                        }
                    } else {
                        true
                    };
                    if time_ok {
                        is_auto_declined = true;
                    }
                }
            }
        }
    }

    if let Some((_, ref local_id)) = existing_rsvp_and_id {
        rsvp_status = apply_pending_rsvp_guard(db, local_id, rsvp_status);
    }

    if let Some((_, id)) = existing_rsvp_and_id {
        db.execute(
            "UPDATE calendar_events SET
                summary = ?1, description = ?2, dtstart = ?3, dtend = ?4,
                location = ?5, organizer = ?6, status = ?7, rsvp_status = ?8,
                recurrence_rule = ?9, all_day = ?10, uid = ?11, updated_at = ?12,
                google_calendar_id = ?13, start_tz = ?14, end_tz = ?15, recurring_event_id = ?16,
                attendees_json = ?17, reminders_json = ?18, color = ?19, hangout_link = ?20, event_type = ?21, sequence = ?22
             WHERE id = ?23",
            params![
                summary,
                event.description,
                dtstart,
                dtend,
                event.location,
                organizer,
                status,
                rsvp_status,
                rrule,
                all_day as i64,
                uid,
                now,
                calendar_id,
                start_tz,
                end_tz,
                event.recurring_event_id,
                attendees_json,
                reminders_json,
                event.color_id,
                event.hangout_link,
                event.event_type,
                sequence_str,
                id
            ],
        )?;
        if is_auto_declined {
            return Ok(UpsertOutcome::AutoDeclined);
        }
        return Ok(UpsertOutcome::Updated);
    }

    let new_id = Uuid::new_v4().to_string();
    db.execute(
        "INSERT INTO calendar_events
            (id, account_id, email_id, uid, summary, description, dtstart, dtend, location, organizer,
             status, rsvp_status, recurrence_rule, all_day, created_at, updated_at, source, external_id,
             google_calendar_id, start_tz, end_tz, recurring_event_id, attendees_json, reminders_json, color, hangout_link, event_type, sequence)
         VALUES (?1, ?2, NULL, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?14, 'google', ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25)",
        params![
            new_id,
            account_id,
            uid,
            summary,
            event.description,
            dtstart,
            dtend,
            event.location,
            organizer,
            status,
            rsvp_status,
            rrule,
            all_day as i64,
            now,
            event.id,
            calendar_id,
            start_tz,
            end_tz,
            event.recurring_event_id,
            attendees_json,
            reminders_json,
            event.color_id,
            event.hangout_link,
            event.event_type,
            sequence_str
        ],
    )?;
    Ok(UpsertOutcome::Inserted)
}

#[derive(Debug)]
pub(crate) enum UpsertOutcome {
    Inserted,
    Updated,
    Deleted,
    Skipped,
    AutoDeclined,
}

fn upsert_calendar(db: &rusqlite::Connection, account_id: &str, cal: &GCalListItem) -> Result<()> {
    let summary = cal.summary.clone().unwrap_or_default();
    let is_primary = cal.primary.unwrap_or(false);
    let now = chrono::Utc::now().to_rfc3339();

    let existing_id: Option<String> = db
        .query_row(
            "SELECT id FROM google_calendars WHERE account_id = ?1 AND calendar_id = ?2",
            params![account_id, cal.id],
            |row| row.get(0),
        )
        .ok();

    if let Some(id) = existing_id {
        db.execute(
            "UPDATE google_calendars SET
                summary = ?1, background_color = ?2, foreground_color = ?3,
                is_primary = ?4, access_role = ?5, updated_at = ?6
             WHERE id = ?7",
            params![
                summary,
                cal.background_color,
                cal.foreground_color,
                is_primary as i64,
                cal.access_role,
                now,
                id
            ],
        )?;
    } else {
        let new_id = Uuid::new_v4().to_string();
        db.execute(
            "INSERT INTO google_calendars
                (id, account_id, calendar_id, summary, background_color, foreground_color,
                 is_primary, access_role, visible, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 1, ?9, ?9)",
            params![
                new_id,
                account_id,
                cal.id,
                summary,
                cal.background_color,
                cal.foreground_color,
                is_primary as i64,
                cal.access_role,
                now
            ],
        )?;
    }
    Ok(())
}

pub(crate) async fn sync_internal(
    ctx: &AppCtx,
    account_id: &str,
) -> std::result::Result<GoogleCalendarSyncResult, MailFfiError> {
    {
        let db = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        db.execute_batch(
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
        let _ = db.execute_batch(
            "ALTER TABLE calendar_events ADD COLUMN source TEXT NOT NULL DEFAULT 'local';",
        );
        let _ = db.execute_batch("ALTER TABLE calendar_events ADD COLUMN external_id TEXT;");
        let _ = db.execute_batch(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_calendar_events_external
                ON calendar_events(account_id, source, external_id)
                WHERE external_id IS NOT NULL;",
        );
    }

    let (auth_type, account_email): (String, Option<String>) = {
        let db = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        db.query_row(
            "SELECT auth_type, email FROM accounts WHERE id = ?1",
            params![account_id],
            |row| {
                Ok((
                    row.get::<_, Option<String>>(0)?.unwrap_or_default(),
                    row.get::<_, Option<String>>(1)?,
                ))
            },
        )
        .map_err(|_| MailFfiError::AccountNotFound(account_id.to_string()))?
    };
    if auth_type != "oauth2_gmail" {
        return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
            "Google Calendar sync is only available for Gmail OAuth accounts".to_string(),
        )));
    }

    let (client, access_token) = get_client_and_token(ctx, account_id).await?;

    let calendars = match fetch_calendars(&client, &access_token).await {
        // calendarList needs a calendar-list scope that Maho does not request,
        // while calendar.events still covers events.* on the primary calendar.
        // Re-consent would grant the same scopes, so sync the primary calendar
        // instead of asking the user to reauthorize.
        Err(MailFfiError::Core(maho_core::error::AppError::Network(msg)))
            if msg.contains("HTTP 403") =>
        {
            fetch_primary_calendar(&client, &access_token, account_email.as_deref())
                .await
                .map(|primary| vec![primary])
        }
        other => other,
    };
    let calendars = match calendars {
        Ok(c) => c,
        Err(MailFfiError::Core(maho_core::error::AppError::Network(msg)))
            if msg.contains("HTTP 401") || msg.contains("HTTP 403") =>
        {
            let db = ctx
                .pool
                .get()
                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
            let _ = db.execute(
                "INSERT INTO calendar_sync_state (account_id, calendar_id, sync_token, last_synced_at, last_error)
                 VALUES (?1, '*', NULL, ?2, ?3)
                 ON CONFLICT(account_id, calendar_id) DO UPDATE SET last_error = excluded.last_error, last_synced_at = excluded.last_synced_at",
                params![account_id, chrono::Utc::now().to_rfc3339(), msg],
            );
            return Ok(GoogleCalendarSyncResult {
                calendars_synced: 0,
                events_upserted: 0,
                events_deleted: 0,
                reauthorize_required: true,
            });
        }
        Err(e) => return Err(e),
    };

    let mut events_upserted: u32 = 0;
    let mut events_deleted: u32 = 0;

    for cal in &calendars {
        {
            let db = ctx
                .pool
                .get()
                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
            let _ = upsert_calendar(&db, account_id, cal);
        }

        let stored_token: Option<String> = {
            let db = ctx
                .pool
                .get()
                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
            db.query_row(
                "SELECT sync_token FROM calendar_sync_state WHERE account_id = ?1 AND calendar_id = ?2",
                params![account_id, cal.id],
                |row| row.get(0),
            )
            .ok()
            .flatten()
        };

        let outcome = match fetch_events(&client, &access_token, &cal.id, stored_token.as_deref())
            .await
        {
            Ok(o) => o,
            Err(e) => {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let _ = db.execute(
                    "INSERT INTO calendar_sync_state (account_id, calendar_id, sync_token, last_synced_at, last_error)
                     VALUES (?1, ?2, ?3, ?4, ?5)
                     ON CONFLICT(account_id, calendar_id) DO UPDATE SET last_synced_at = excluded.last_synced_at, last_error = excluded.last_error",
                    params![account_id, cal.id, stored_token, chrono::Utc::now().to_rfc3339(), e.to_string()],
                );
                continue;
            }
        };

        let (events, next_sync_token) = match outcome {
            FetchEventsOutcome::Done {
                events,
                next_sync_token,
            } => (events, next_sync_token),
            FetchEventsOutcome::ResyncRequired => {
                match fetch_events(&client, &access_token, &cal.id, None).await {
                    Ok(FetchEventsOutcome::Done {
                        events,
                        next_sync_token,
                    }) => (events, next_sync_token),
                    _ => continue,
                }
            }
        };

        {
            let db = ctx
                .pool
                .get()
                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
            for event in &events {
                match upsert_event(&db, account_id, account_email.as_deref(), &cal.id, event) {
                    Ok(UpsertOutcome::Inserted)
                    | Ok(UpsertOutcome::Updated)
                    | Ok(UpsertOutcome::AutoDeclined) => events_upserted += 1,
                    Ok(UpsertOutcome::Deleted) => events_deleted += 1,
                    _ => {}
                }
            }

            let now = chrono::Utc::now().to_rfc3339();
            db.execute(
                "INSERT INTO calendar_sync_state (account_id, calendar_id, sync_token, last_synced_at, last_error)
                 VALUES (?1, ?2, ?3, ?4, NULL)
                 ON CONFLICT(account_id, calendar_id) DO UPDATE SET
                    sync_token = excluded.sync_token,
                    last_synced_at = excluded.last_synced_at,
                    last_error = NULL",
                params![account_id, cal.id, next_sync_token, now],
            )?;
        }
    }

    Ok(GoogleCalendarSyncResult {
        calendars_synced: calendars.len() as u32,
        events_upserted,
        events_deleted,
        reauthorize_required: false,
    })
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CalendarEventRecord {
    pub id: String,
    pub account_id: String,
    pub email_id: Option<String>,
    pub uid: String,
    pub summary: String,
    pub description: Option<String>,
    pub dtstart: String,
    pub dtend: Option<String>,
    pub location: Option<String>,
    pub organizer: Option<String>,
    pub status: String,
    pub rsvp_status: Option<String>,
    pub recurrence_rule: Option<String>,
    pub all_day: bool,
    pub created_at: String,
    pub updated_at: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub google_calendar_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub calendar_color: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub calendar_summary: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub start_tz: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub end_tz: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub recurring_event_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub attendees_json: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reminders_json: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub color: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hangout_link: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub category: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub travel_time_minutes: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub event_type: Option<String>,
}

fn map_calendar_event(row: &rusqlite::Row) -> rusqlite::Result<CalendarEventRecord> {
    Ok(CalendarEventRecord {
        id: row.get("id")?,
        account_id: row.get("account_id")?,
        email_id: row.get("email_id")?,
        uid: row.get("uid")?,
        summary: row.get("summary")?,
        description: row.get("description")?,
        dtstart: row.get("dtstart")?,
        dtend: row.get("dtend")?,
        location: row.get("location")?,
        organizer: row.get("organizer")?,
        status: row.get("status")?,
        rsvp_status: row.get("rsvp_status")?,
        recurrence_rule: row.get("recurrence_rule")?,
        all_day: row.get::<_, i64>("all_day")? != 0,
        created_at: row.get("created_at")?,
        updated_at: row.get("updated_at")?,
        google_calendar_id: row.get("google_calendar_id").ok(),
        calendar_color: row.get("calendar_color").ok(),
        calendar_summary: row.get("calendar_summary").ok(),
        start_tz: row.get("start_tz").ok(),
        end_tz: row.get("end_tz").ok(),
        recurring_event_id: row.get("recurring_event_id").ok(),
        attendees_json: row.get("attendees_json").ok(),
        reminders_json: row.get("reminders_json").ok(),
        color: row.get("color").ok(),
        hangout_link: row.get("hangout_link").ok(),
        category: row.get("category").ok(),
        travel_time_minutes: row.get("travel_time_minutes").ok(),
        event_type: row.get("event_type").ok(),
    })
}

#[derive(Debug, Default)]
pub(crate) struct ParsedVEvent {
    pub(crate) uid: Option<String>,
    pub(crate) summary: Option<String>,
    pub(crate) description: Option<String>,
    pub(crate) dtstart: Option<String>,
    pub(crate) dtend: Option<String>,
    pub(crate) location: Option<String>,
    pub(crate) organizer: Option<String>,
    pub(crate) status: Option<String>,
    pub(crate) rrule: Option<String>,
    pub(crate) method: Option<String>,
    pub(crate) attendees: Vec<(String, Option<String>)>,
    pub(crate) sequence: Option<String>,
    pub(crate) start_tz: Option<String>,
    pub(crate) end_tz: Option<String>,
}

pub(crate) fn parse_ics(ics_data: &str) -> Vec<ParsedVEvent> {
    let mut events: Vec<ParsedVEvent> = Vec::new();
    let mut current: Option<ParsedVEvent> = None;
    let mut calendar_method: Option<String> = None;

    let unfolded = unfold_ics_lines(ics_data);

    for line in unfolded.lines() {
        let line = line.trim_end_matches('\r');
        if line.is_empty() {
            continue;
        }

        if line.eq_ignore_ascii_case("BEGIN:VEVENT") {
            current = Some(ParsedVEvent::default());
            continue;
        }

        if line.eq_ignore_ascii_case("END:VEVENT") {
            if let Some(mut ev) = current.take() {
                if ev.method.is_none() {
                    ev.method = calendar_method.clone();
                }
                events.push(ev);
            }
            continue;
        }

        if current.is_none() {
            if let Some(val) = extract_ics_value(line, "METHOD") {
                calendar_method = Some(val.to_uppercase());
            }
            continue;
        }

        if let Some(ev) = current.as_mut() {
            if let Some(val) = extract_ics_value(line, "UID") {
                ev.uid = Some(val);
            } else if let Some(val) = extract_ics_value(line, "SUMMARY") {
                ev.summary = Some(unescape_ics(val));
            } else if let Some(val) = extract_ics_value(line, "DESCRIPTION") {
                ev.description = Some(unescape_ics(val));
            } else if let Some(val) = extract_ics_value(line, "DTSTART") {
                ev.dtstart = Some(val);
                ev.start_tz = extract_param(line, "TZID");
            } else if let Some(val) = extract_ics_value(line, "DTEND") {
                ev.dtend = Some(val);
                ev.end_tz = extract_param(line, "TZID");
            } else if let Some(val) = extract_ics_value(line, "LOCATION") {
                ev.location = Some(unescape_ics(val));
            } else if let Some(val) = extract_ics_value(line, "ORGANIZER") {
                ev.organizer = Some(extract_mailto(&val));
            } else if let Some(val) = extract_ics_value(line, "STATUS") {
                ev.status = Some(val.to_lowercase());
            } else if let Some(val) = extract_ics_value(line, "RRULE") {
                ev.rrule = Some(val);
            } else if let Some(val) = extract_ics_value(line, "SEQUENCE") {
                ev.sequence = Some(val);
            } else if line.starts_with("ATTENDEE") {
                let partstat = extract_param(line, "PARTSTAT");
                let email = extract_mailto_from_line(line);
                if let Some(e) = email {
                    ev.attendees.push((e, partstat));
                }
            }
        }
    }

    events
}

fn unfold_ics_lines(input: &str) -> String {
    let mut result = String::with_capacity(input.len());
    for line in input.lines() {
        if line.starts_with(' ') || line.starts_with('\t') {
            result.push_str(&line[1..]);
        } else {
            if !result.is_empty() {
                result.push('\n');
            }
            result.push_str(line);
        }
    }
    result
}

fn extract_ics_value(line: &str, key: &str) -> Option<String> {
    let upper_line = line.to_uppercase();
    let upper_key = key.to_uppercase();

    if !upper_line.starts_with(&upper_key) {
        return None;
    }

    let after_key = &line[key.len()..];
    if let Some(stripped) = after_key.strip_prefix(':') {
        Some(stripped.to_string())
    } else if after_key.starts_with(';') {
        let mut quoted = false;
        after_key
            .find(|ch| {
                if ch == '"' { quoted = !quoted; }
                ch == ':' && !quoted
            })
            .map(|colon_pos| after_key[colon_pos + 1..].to_string())
    } else {
        None
    }
}

fn extract_param(line: &str, param: &str) -> Option<String> {
    let mut quoted = false;
    let mut start = 0;
    for (index, ch) in line.char_indices() {
        if ch == '"' { quoted = !quoted; }
        if !quoted && matches!(ch, ';' | ':') {
            if let Some((name, value)) = line[start..index].split_once('=') {
                if name.eq_ignore_ascii_case(param) {
                    return Some(value.to_string());
                }
            }
            if ch == ':' { return None; }
            start = index + 1;
        }
    }
    None
}

fn extract_mailto(value: &str) -> String {
    if let Some(idx) = value.find("mailto:") {
        value[idx + 7..].to_string()
    } else if let Some(idx) = value.find("MAILTO:") {
        value[idx + 7..].to_string()
    } else {
        value.to_string()
    }
}

fn extract_mailto_from_line(line: &str) -> Option<String> {
    let lower = line.to_lowercase();
    if let Some(idx) = lower.find("mailto:") {
        let rest = &line[idx + 7..];
        Some(rest.trim().to_string())
    } else {
        line.rfind(':')
            .map(|colon| line[colon + 1..].trim().to_string())
    }
}

fn unescape_ics(value: String) -> String {
    value
        .replace("\\n", "\n")
        .replace("\\N", "\n")
        .replace("\\,", ",")
        .replace("\\;", ";")
        .replace("\\\\", "\\")
}

fn normalize_ics_date(value: &str) -> std::result::Result<String, maho_core::error::AppError> {
    let normalized = if value.len() == 8 {
        chrono::NaiveDate::parse_from_str(value, "%Y%m%d")
            .map(|date| date.format("%Y-%m-%d").to_string())
    } else {
        let (local, suffix) = match value.strip_suffix('Z') {
            Some(local) => (local, "Z"),
            None => (value, ""),
        };
        chrono::NaiveDateTime::parse_from_str(local, "%Y%m%dT%H%M%S")
            .map(|date| format!("{}{suffix}", date.format("%Y-%m-%dT%H:%M:%S")))
    };
    normalized.map_err(|error| {
        maho_core::error::AppError::Validation(format!("Invalid ICS date {value}: {error}"))
    })
}

fn insert_event(
    db: &rusqlite::Connection,
    account_id: &str,
    email_id: Option<&str>,
    ev: &ParsedVEvent,
) -> std::result::Result<CalendarEventRecord, maho_core::error::AppError> {
    let id = Uuid::new_v4().to_string();
    let uid = ev.uid.clone().unwrap_or_else(|| Uuid::new_v4().to_string());
    let summary = ev.summary.clone().unwrap_or_default();
    let raw_start = ev.dtstart.as_deref().unwrap_or_default();
    let all_day = is_all_day(raw_start);
    let dtstart = normalize_ics_date(raw_start)?;
    let dtend = ev.dtend.as_deref().map(normalize_ics_date).transpose()?;
    let status = ev.status.clone().unwrap_or_else(|| "confirmed".to_string());

    let existing_id: Option<String> = db
        .query_row(
            "SELECT id FROM calendar_events WHERE account_id = ?1 AND uid = ?2",
            params![account_id, uid],
            |row| row.get(0),
        )
        .ok();

    let row_id = if let Some(eid) = existing_id {
        db.execute(
            "UPDATE calendar_events SET \
                email_id = COALESCE(?1, email_id), \
                summary = ?2, \
                description = ?3, \
                dtstart = ?4, \
                dtend = ?5, \
                location = ?6, \
                organizer = ?7, \
                status = ?8, \
                recurrence_rule = ?9, \
                all_day = ?10, \
                sequence = ?11, \
                start_tz = ?13, end_tz = ?14, \
                updated_at = datetime('now') \
             WHERE id = ?12",
            params![
                email_id,
                summary,
                ev.description,
                dtstart,
                dtend,
                ev.location,
                ev.organizer,
                status,
                ev.rrule,
                all_day as i64,
                ev.sequence.as_deref().unwrap_or("0"),
                eid,
                ev.start_tz,
                ev.end_tz,
            ],
        )?;
        eid
    } else {
        db.execute(
            "INSERT INTO calendar_events (id, account_id, email_id, uid, summary, description, dtstart, dtend, location, organizer, status, recurrence_rule, all_day, sequence, start_tz, end_tz) \
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)",
            params![
                id,
                account_id,
                email_id,
                uid,
                summary,
                ev.description,
                dtstart,
                dtend,
                ev.location,
                ev.organizer,
                status,
                ev.rrule,
                all_day as i64,
                ev.sequence.as_deref().unwrap_or("0"),
                ev.start_tz,
                ev.end_tz,
            ],
        )?;
        id
    };

    let record = db.query_row(
        "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                NULL AS calendar_color, NULL AS calendar_summary, \
                ce.category, ce.travel_time_minutes, ce.event_type \
         FROM calendar_events ce WHERE ce.id = ?1",
        params![row_id],
        map_calendar_event,
    )?;

    Ok(record)
}

#[no_mangle]
pub extern "C" fn MahoMailImportCalendarEvent(
    account_id: *const c_char,
    email_id: *const c_char,
    ics_data: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(email_id) = opt_string(email_id, "email_id") else {
            return false;
        };
        let Ok(ics_data) = non_empty(ics_data, "ics_data") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let events = parse_ics(&ics_data);
                if events.is_empty() {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        "No VEVENT found in ICS data".to_string(),
                    )));
                }

                let ev = &events[0];
                if ev.dtstart.is_none() {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        "VEVENT missing required DTSTART".to_string(),
                    )));
                }

                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let record = insert_event(&db, &account_id, email_id.as_deref(), ev)?;
                Ok(record)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListCalendarEvents(
    account_id: *const c_char,
    from_date: *const c_char,
    to_date: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(from_date) = opt_string(from_date, "from_date") else {
            return false;
        };
        let Ok(to_date) = opt_string(to_date, "to_date") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let show_birthdays = {
                    let value: Option<String> = db
                        .query_row(
                            "SELECT value FROM app_settings WHERE key = 'calendar.show_birthdays'",
                            [],
                            |row| row.get(0),
                        )
                        .ok();
                    value.map(|v| v == "true").unwrap_or(true)
                };

                let hide_declined = {
                    let value: Option<String> = db
                        .query_row(
                            "SELECT value FROM app_settings WHERE key = 'calendar.hide_declined'",
                            [],
                            |row| row.get(0),
                        )
                        .ok();
                    value.map(|v| v == "true").unwrap_or(false)
                };

                let mut sql = String::from(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            gc.background_color AS calendar_color, \
                            gc.summary AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce \
                     LEFT JOIN google_calendars gc \
                       ON gc.account_id = ce.account_id \
                      AND gc.calendar_id = ce.google_calendar_id \
                     WHERE ce.account_id = ?1 \
                       AND (gc.id IS NULL OR gc.visible = 1)",
                );
                if hide_declined {
                    sql.push_str(" AND (ce.rsvp_status IS NULL OR ce.rsvp_status != 'declined')");
                }
                let mut param_values: Vec<Box<dyn rusqlite::types::ToSql>> =
                    vec![Box::new(account_id.clone()) as Box<dyn rusqlite::types::ToSql>];
                if let Some(from) = &from_date {
                    sql.push_str(&format!(" AND ce.dtstart >= ?{}", param_values.len() + 1));
                    param_values.push(Box::new(from.clone()));
                }
                if let Some(to) = &to_date {
                    if chrono::NaiveDate::parse_from_str(to, "%Y-%m-%d").is_ok() {
                        sql.push_str(&format!(
                            " AND substr(ce.dtstart, 1, 10) <= ?{}",
                            param_values.len() + 1
                        ));
                    } else {
                        sql.push_str(&format!(" AND ce.dtstart <= ?{}", param_values.len() + 1));
                    }
                    param_values.push(Box::new(to.clone()));
                }
                sql.push_str(" ORDER BY ce.dtstart ASC");

                let mut stmt = db.prepare(&sql)?;
                let param_refs: Vec<&dyn rusqlite::types::ToSql> =
                    param_values.iter().map(|p| p.as_ref()).collect();
                let mut rows = stmt
                    .query_map(param_refs.as_slice(), map_calendar_event)?
                    .collect::<std::result::Result<Vec<_>, _>>()?;

                if show_birthdays {
                    let mut birthdays_stmt = db.prepare(
                        "SELECT id, name, email, birthday FROM contacts WHERE account_id = ?1 AND birthday IS NOT NULL AND birthday != ''"
                    )?;
                    let contacts_with_birthdays = birthdays_stmt
                        .query_map([&account_id], |row| {
                            Ok((
                                row.get::<_, String>(0)?,
                                row.get::<_, Option<String>>(1)?,
                                row.get::<_, String>(2)?,
                                row.get::<_, String>(3)?,
                            ))
                        })?
                        .collect::<std::result::Result<Vec<_>, _>>()?;

                    let start_year = from_date
                        .as_ref()
                        .and_then(|d| d.get(0..4))
                        .and_then(|s| s.parse::<i32>().ok())
                        .unwrap_or(2026);
                    let end_year = to_date
                        .as_ref()
                        .and_then(|d| d.get(0..4))
                        .and_then(|s| s.parse::<i32>().ok())
                        .unwrap_or(2026);

                    for (id, name, email, bday) in contacts_with_birthdays {
                        let mm_dd = if bday.len() >= 10 {
                            &bday[5..10]
                        } else if bday.len() == 5 {
                            &bday[0..5]
                        } else {
                            continue;
                        };

                        for year in start_year..=end_year {
                            let bday_date = format!("{}-{}", year, mm_dd);
                            let in_range = match (&from_date, &to_date) {
                                (Some(f), Some(t)) => bday_date >= *f && bday_date <= *t,
                                (Some(f), None) => bday_date >= *f,
                                (None, Some(t)) => bday_date <= *t,
                                (None, None) => true,
                            };
                            if in_range {
                                let summary =
                                    format!("🎂 {}'s birthday", name.as_deref().unwrap_or(&email));
                                let record = CalendarEventRecord {
                                    id: format!("birthday-{}-{}", id, year),
                                    account_id: account_id.clone(),
                                    email_id: None,
                                    uid: format!("birthday-{}-{}", id, year),
                                    summary,
                                    description: None,
                                    dtstart: bday_date.clone(),
                                    dtend: Some(bday_date.clone()),
                                    location: None,
                                    organizer: None,
                                    status: "confirmed".to_string(),
                                    rsvp_status: Some("accepted".to_string()),
                                    recurrence_rule: Some("FREQ=YEARLY".to_string()),
                                    all_day: true,
                                    created_at: "".to_string(),
                                    updated_at: "".to_string(),
                                    google_calendar_id: Some("birthday".to_string()),
                                    calendar_color: Some("#db2777".to_string()),
                                    calendar_summary: Some("Birthdays".to_string()),
                                    start_tz: None,
                                    end_tz: None,
                                    recurring_event_id: None,
                                    attendees_json: None,
                                    reminders_json: None,
                                    color: Some("#db2777".to_string()),
                                    hangout_link: None,
                                    category: None,
                                    travel_time_minutes: None,
                                    event_type: None,
                                };
                                rows.push(record);
                            }
                        }
                    }
                }

                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGetCalendarEvent(
    event_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(event_id) = non_empty(event_id, "event_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let record = db.query_row(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            gc.background_color AS calendar_color, \
                            gc.summary AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce \
                     LEFT JOIN google_calendars gc \
                       ON gc.account_id = ce.account_id \
                      AND gc.calendar_id = ce.google_calendar_id \
                     WHERE ce.id = ?1",
                    params![event_id],
                    map_calendar_event,
                ).map_err(|_| MailFfiError::Core(maho_core::error::AppError::NotFound(format!("Calendar event {event_id} not found"))))?;
                Ok(record)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateRsvp(
    event_id: *const c_char,
    rsvp_status: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(event_id) = non_empty(event_id, "event_id") else {
            return false;
        };
        let Ok(rsvp_status) = non_empty(rsvp_status, "rsvp_status") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let valid = ["accepted", "declined", "tentative"];
                if !valid.contains(&rsvp_status.as_str()) {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        format!(
                        "Invalid RSVP status '{}'. Must be one of: accepted, declined, tentative",
                        rsvp_status
                    ),
                    )));
                }

                type RsvpRow = (
                    String,
                    Option<String>,
                    String,
                    Option<String>,
                    String,
                    String,
                    String,
                    Option<String>,
                    Option<String>,
                    Option<String>,
                    Option<String>,
                    Option<String>,
                    Option<String>,
                );
                let row: Option<RsvpRow> = {
                    let db = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    db.query_row(
                        "SELECT account_id, google_calendar_id, source, external_id, uid, summary, dtstart, dtend, organizer, email_id, sequence, start_tz, end_tz FROM calendar_events WHERE id = ?1",
                        params![event_id],
                        |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?, row.get(4)?, row.get(5)?, row.get(6)?, row.get(7)?, row.get(8)?, row.get(9)?, row.get(10)?, row.get(11)?, row.get(12)?)),
                    ).ok()
                };

                if let Some((
                    account_id,
                    google_calendar_id,
                    source,
                    external_id,
                    uid,
                    summary,
                    dtstart,
                    dtend,
                    organizer,
                    email_id,
                    sequence,
                    start_tz,
                    end_tz,
                )) = row
                {
                    if source == "google" {
                        if let Some(cal_id) = google_calendar_id {
                            if let Some(ext_id) = external_id {
                                let account_email: String = {
                                    let db = ctx
                                        .pool
                                        .get()
                                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                    db.query_row(
                                        "SELECT email FROM accounts WHERE id = ?1",
                                        [&account_id],
                                        |row| row.get(0),
                                    )?
                                };

                                let gevent_result = google_calendar_update_rsvp_internal(
                                    &ctx,
                                    &account_id,
                                    &cal_id,
                                    &ext_id,
                                    &account_email,
                                    &rsvp_status,
                                )
                                .await;

                                let db = ctx
                                    .pool
                                    .get()
                                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                match gevent_result {
                                    Ok(gevent) => {
                                        let _ = db.execute(
                                            "DELETE FROM pending_mutations WHERE account_id = ?1 AND calendar_event_id = ?2 AND mutation_type IN ('calendar_rsvp', 'calendar_rsvp_reply')",
                                            params![account_id, event_id],
                                        );
                                        upsert_event(
                                            &db,
                                            &account_id,
                                            Some(&account_email),
                                            &cal_id,
                                            &gevent,
                                        )?;
                                    }
                                    Err(MailFfiError::Core(
                                        maho_core::error::AppError::Network(_),
                                    )) => {
                                        maho_core::services::offline_queue::queue_calendar_mutation(
                                            &db,
                                            &account_id,
                                            "calendar_rsvp",
                                            &event_id,
                                            Some(&rsvp_status),
                                        )?;
                                        db.execute(
                                            "UPDATE calendar_events SET rsvp_status = ?1, updated_at = datetime('now') WHERE id = ?2",
                                            params![rsvp_status, event_id],
                                        )?;
                                    }
                                    Err(e) => return Err(e),
                                }
                            }
                        }
                    } else {
                        let rsvp_ctx = {
                            let db = ctx
                                .pool
                                .get()
                                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                            let organizer_line = match organizer.filter(|s| !s.trim().is_empty()) {
                                Some(o) => o,
                                None => {
                                    db.execute(
                                        "UPDATE calendar_events SET rsvp_status = ?1, updated_at = datetime('now') WHERE id = ?2 AND account_id = ?3",
                                        params![rsvp_status, event_id, account_id],
                                    )?;
                                    let record = db.query_row(
                                        "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                                                ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                                                ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                                                ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                                                ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                                                NULL AS calendar_color, NULL AS calendar_summary, \
                                                ce.category, ce.travel_time_minutes, ce.event_type \
                                         FROM calendar_events ce WHERE ce.id = ?1",
                                        params![event_id],
                                        map_calendar_event,
                                    )?;
                                    let json_res = serde_json::to_string(&record)?;
                                    return Ok(json_res);
                                }
                            };

                            let organizer_email = extract_bare_email(&organizer_line);

                            let (attendee_email, attendee_name): (String, Option<String>) = db
                                .query_row(
                                    "SELECT email, display_name FROM accounts WHERE id = ?1",
                                    [&account_id],
                                    |row| Ok((row.get(0)?, row.get(1)?)),
                                )?;

                            IcsRsvpContext {
                                account_id: account_id.clone(),
                                event_id: event_id.clone(),
                                rsvp_status: rsvp_status.clone(),
                                uid,
                                sequence: sequence.unwrap_or_else(|| "0".to_string()),
                                dtstart,
                                dtend,
                                start_tz,
                                end_tz,
                                summary,
                                organizer_line,
                                organizer_email,
                                attendee_email,
                                attendee_name,
                                email_id,
                            }
                        };

                        send_ics_rsvp_reply(&ctx, rsvp_ctx).await?;
                    }
                } else {
                    return Err(MailFfiError::Core(maho_core::error::AppError::NotFound(
                        format!("Calendar event {event_id} not found"),
                    )));
                }

                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let record = db.query_row(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            NULL AS calendar_color, NULL AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce WHERE ce.id = ?1",
                    params![event_id],
                    map_calendar_event,
                )?;
                let json_res = serde_json::to_string(&record)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGenerateRsvpReply(
    event_id: *const c_char,
    rsvp_status: *const c_char,
    account_email: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(event_id) = non_empty(event_id, "event_id") else {
            return false;
        };
        let Ok(rsvp_status) = non_empty(rsvp_status, "rsvp_status") else {
            return false;
        };
        let Ok(account_email) = non_empty(account_email, "account_email") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let valid = ["accepted", "declined", "tentative"];
                if !valid.contains(&rsvp_status.as_str()) {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                        format!(
                        "Invalid RSVP status '{}'. Must be one of: accepted, declined, tentative",
                        rsvp_status
                    ),
                    )));
                }

                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let record = db.query_row(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            NULL AS calendar_color, NULL AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce WHERE ce.id = ?1",
                    params![event_id],
                    map_calendar_event,
                ).map_err(|_| MailFfiError::Core(maho_core::error::AppError::NotFound(format!("Calendar event {event_id} not found"))))?;

                let params = IcsReplyParams {
                    uid: record.uid,
                    sequence: String::new(),
                    dtstart: record.dtstart,
                    dtend: record.dtend,
                    start_tz: record.start_tz,
                    end_tz: record.end_tz,
                    summary: record.summary,
                    organizer_line: record.organizer.unwrap_or_default(),
                    attendee_email: account_email,
                    attendee_name: None,
                    partstat: rsvp_status.to_uppercase(),
                };

                build_ics_reply(params)
                    .map_err(|e| MailFfiError::Core(maho_core::error::AppError::Validation(e)))
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteCalendarEvent(
    event_id: *const c_char,
    delete_scope: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(event_id) = non_empty(event_id, "event_id") else {
            return false;
        };
        let Ok(delete_scope) = opt_string(delete_scope, "delete_scope") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let row: Option<(
                    String,
                    Option<String>,
                    String,
                    Option<String>,
                    Option<String>,
                )> = {
                    let db = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    db.query_row(
                        "SELECT account_id, google_calendar_id, source, external_id, recurring_event_id FROM calendar_events WHERE id = ?1",
                        params![event_id],
                        |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?, row.get(4)?)),
                    ).ok()
                };

                if let Some((
                    account_id,
                    google_calendar_id,
                    source,
                    external_id,
                    recurring_event_id,
                )) = row
                {
                    if source == "google" {
                        if let Some(cal_id) = google_calendar_id {
                            if let Some(ext_id) = external_id {
                                let access_role: Option<String> = {
                                    let db = ctx
                                        .pool
                                        .get()
                                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                    db.query_row(
                                        "SELECT access_role FROM google_calendars WHERE account_id = ?1 AND calendar_id = ?2",
                                        [&account_id, &cal_id],
                                        |row| row.get(0),
                                    ).ok().flatten()
                                };

                                if !matches!(access_role.as_deref(), Some("owner") | Some("writer"))
                                {
                                    return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                                        "This event is on a read-only calendar and cannot be deleted."
                                            .to_string(),
                                    )));
                                }

                                let target_id = if delete_scope.as_deref() == Some("all") {
                                    recurring_event_id.unwrap_or(ext_id)
                                } else {
                                    ext_id
                                };

                                match google_calendar_delete_event_internal(
                                    &ctx,
                                    &account_id,
                                    &cal_id,
                                    &target_id,
                                )
                                .await
                                {
                                    Ok(()) => {}
                                    Err(MailFfiError::Core(
                                        maho_core::error::AppError::Network(_),
                                    )) => {
                                        let db = ctx
                                            .pool
                                            .get()
                                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                        maho_core::services::offline_queue::queue_calendar_mutation(
                                            &db,
                                            &account_id,
                                            "calendar_delete",
                                            &event_id,
                                            Some(&serde_json::json!({
                                                "calendar_id": cal_id,
                                                "external_id": target_id,
                                            }).to_string()),
                                        )?;
                                    }
                                    Err(e) => return Err(e),
                                }
                            }
                        }
                    }
                }

                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                db.execute(
                    "DELETE FROM calendar_events WHERE id = ?1",
                    params![event_id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailAutoImportCalendarEvents(
    account_id: *const c_char,
    email_id: *const c_char,
    body_html: *const c_char,
    body_text: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(email_id) = non_empty(email_id, "email_id") else {
            return false;
        };
        let Ok(body_html) = opt_string(body_html, "body_html") else {
            return false;
        };
        let Ok(body_text) = opt_string(body_text, "body_text") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let blocks = extract_ics_blocks(body_html.as_deref(), body_text.as_deref());

                if blocks.is_empty() {
                    return Ok(Vec::new());
                }

                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut results = Vec::new();

                for block in &blocks {
                    let events = parse_ics(block);
                    for ev in &events {
                        if ev.dtstart.is_none() {
                            continue;
                        }
                        match insert_event(&db, &account_id, Some(&email_id), ev) {
                            Ok(record) => results.push(record),
                            Err(e) => {
                                log::warn!(
                                    "Failed to import calendar event from email {}: {}",
                                    email_id,
                                    e
                                );
                            }
                        }
                    }
                }

                Ok(results)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCreateCalendarEvent(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<CreateEventRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let calendar_id_res = match req.calendar_id.clone() {
                    Some(cid) if !cid.is_empty() => Some(cid),
                    _ => {
                        let db = ctx
                            .pool
                            .get()
                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                        db.query_row(
                            "SELECT calendar_id FROM google_calendars WHERE account_id = ?1 AND is_primary = 1",
                            [&req.account_id],
                            |row| row.get(0),
                        )
                        .ok()
                    }
                };

                if let Some(ref cal_id) = calendar_id_res {
                    let access_role: Option<String> = {
                        let db = ctx
                            .pool
                            .get()
                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                        db.query_row(
                            "SELECT access_role FROM google_calendars WHERE account_id = ?1 AND calendar_id = ?2",
                            [&req.account_id, cal_id],
                            |row| row.get(0),
                        ).ok().flatten()
                    };

                    let is_writable =
                        matches!(access_role.as_deref(), Some("owner") | Some("writer"));
                    if is_writable {
                        let start_body = if req.all_day {
                            serde_json::json!({ "date": req.dtstart })
                        } else if let Some(ref tz) = req.time_zone {
                            serde_json::json!({ "dateTime": req.dtstart, "timeZone": tz })
                        } else {
                            serde_json::json!({ "dateTime": req.dtstart })
                        };
                        let end_body = if let Some(ref de) = req.dtend {
                            if req.all_day {
                                serde_json::json!({ "date": de })
                            } else if let Some(ref tz) = req.time_zone {
                                serde_json::json!({ "dateTime": de, "timeZone": tz })
                            } else {
                                serde_json::json!({ "dateTime": de })
                            }
                        } else {
                            start_body.clone()
                        };
                        let mut body = serde_json::json!({
                            "summary": req.summary,
                            "description": req.description,
                            "location": req.location,
                            "start": start_body,
                            "end": end_body,
                        });

                        if let Some(ref et) = req.event_type {
                            if let Some(obj) = body.as_object_mut() {
                                obj.insert("eventType".to_string(), serde_json::json!(et));
                                if et == "outOfOffice" {
                                    obj.insert(
                                        "outOfOfficeProperties".to_string(),
                                        serde_json::json!({
                                            "autoDeclineMode": "declineAllConflictingInvitations",
                                            "declineMessage": "Declined because I am out of office."
                                        }),
                                    );
                                }
                            }
                        }

                        if req.add_meet.unwrap_or(false) {
                            if let Some(obj) = body.as_object_mut() {
                                let req_id = uuid::Uuid::new_v4().to_string();
                                obj.insert(
                                    "conferenceData".to_string(),
                                    serde_json::json!({
                                        "createRequest": {
                                            "requestId": req_id,
                                            "conferenceSolutionKey": {
                                                "type": "hangoutsMeet"
                                            }
                                        }
                                    }),
                                );
                            }
                        }

                        if let Some(ref atts) = req.attendees {
                            if let Some(obj) = body.as_object_mut() {
                                let g_atts: Vec<serde_json::Value> = atts
                                    .iter()
                                    .map(|email| serde_json::json!({ "email": email }))
                                    .collect();
                                obj.insert("attendees".to_string(), serde_json::json!(g_atts));
                            }
                        }

                        if let Some(ref rems) = req.reminders {
                            if let Some(obj) = body.as_object_mut() {
                                obj.insert("reminders".to_string(), rems.clone());
                            }
                        }

                        let gevent = match google_calendar_create_event_internal(
                            &ctx,
                            &req.account_id,
                            cal_id,
                            body.clone(),
                        )
                        .await
                        {
                            Ok(g) => g,
                            Err(MailFfiError::Core(maho_core::error::AppError::Network(_))) => {
                                let id = Uuid::new_v4().to_string();
                                let uid = format!("maho-{}", Uuid::new_v4());
                                let db = ctx
                                    .pool
                                    .get()
                                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                db.execute(
                                    "INSERT INTO calendar_events (id, account_id, email_id, uid, summary, description, dtstart, dtend, location, organizer, status, rsvp_status, recurrence_rule, all_day, color, google_calendar_id, start_tz, category, travel_time_minutes, event_type, reminders_json) \
                                     VALUES (?1, ?2, NULL, ?3, ?4, ?5, ?6, ?7, ?8, NULL, 'confirmed', NULL, NULL, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)",
                                    params![id, req.account_id, uid, req.summary, req.description, req.dtstart, req.dtend, req.location, req.all_day as i64, req.color, cal_id, req.time_zone, req.category, req.travel_time_minutes, req.event_type, req.reminders.as_ref().map(|v| v.to_string())],
                                )?;
                                maho_core::services::offline_queue::queue_calendar_mutation(
                                    &db,
                                    &req.account_id,
                                    "calendar_create",
                                    &id,
                                    Some(&body.to_string()),
                                )?;
                                let record = db.query_row(
                                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                                            NULL AS calendar_color, NULL AS calendar_summary, \
                                            ce.category, ce.travel_time_minutes, ce.event_type \
                                     FROM calendar_events ce WHERE ce.id = ?1",
                                    params![id],
                                    map_calendar_event,
                                )?;
                                let json_res = serde_json::to_string(&record)?;
                                return Ok(json_res);
                            }
                            Err(e) => return Err(e),
                        };
                        let account_email: String = {
                            let db = ctx
                                .pool
                                .get()
                                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                            db.query_row(
                                "SELECT email FROM accounts WHERE id = ?1",
                                [&req.account_id],
                                |row| row.get(0),
                            )?
                        };

                        let db = ctx
                            .pool
                            .get()
                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                        upsert_event(&db, &req.account_id, Some(&account_email), cal_id, &gevent)?;

                        db.execute(
                            "UPDATE calendar_events SET category = ?1, travel_time_minutes = ?2, event_type = ?3 WHERE account_id = ?4 AND source = 'google' AND external_id = ?5",
                            params![req.category, req.travel_time_minutes, req.event_type, req.account_id, gevent.id],
                        )?;

                        let record = db.query_row(
                            "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                                    ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                                    ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                                    ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                                    ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                                    NULL AS calendar_color, NULL AS calendar_summary, \
                                    ce.category, ce.travel_time_minutes, ce.event_type \
                             FROM calendar_events ce WHERE ce.account_id = ?1 AND ce.source = 'google' AND ce.external_id = ?2",
                            params![req.account_id, gevent.id],
                            map_calendar_event,
                        )?;
                        let json_res = serde_json::to_string(&record)?;
                        return Ok(json_res);
                    }
                }

                let id = Uuid::new_v4().to_string();
                let uid = format!("maho-{}", Uuid::new_v4());
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                db.execute(
                    "INSERT INTO calendar_events (id, account_id, email_id, uid, summary, description, dtstart, dtend, location, organizer, status, rsvp_status, recurrence_rule, all_day, color, category, travel_time_minutes, event_type) \
                     VALUES (?1, ?2, NULL, ?3, ?4, ?5, ?6, ?7, ?8, NULL, 'confirmed', NULL, NULL, ?9, ?10, ?11, ?12, ?13)",
                    params![id, req.account_id, uid, req.summary, req.description, req.dtstart, req.dtend, req.location, req.all_day as i64, req.color, req.category, req.travel_time_minutes, req.event_type],
                )?;
                let record = db.query_row(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            NULL AS calendar_color, NULL AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce WHERE ce.id = ?1",
                    params![id],
                    map_calendar_event,
                )?;
                let json_res = serde_json::to_string(&record)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateCalendarEvent(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<UpdateEventRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let row: Option<(
                    String,
                    Option<String>,
                    String,
                    Option<String>,
                    Option<String>,
                )> = {
                    let db = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    db.query_row(
                        "SELECT account_id, google_calendar_id, source, external_id, recurring_event_id FROM calendar_events WHERE id = ?1",
                        params![req.event_id],
                        |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?, row.get(4)?)),
                    ).ok()
                };

                if let Some((
                    account_id,
                    google_calendar_id,
                    source,
                    external_id,
                    recurring_event_id,
                )) = row
                {
                    let mut final_category = req.category.clone();
                    if let Some(ref target_cal_id) = req.target_calendar_id {
                        if let Ok(db) = ctx.pool.get() {
                            let exists_on_same_account: bool = db
                                .query_row(
                                    "SELECT 1 FROM google_calendars WHERE calendar_id = ?1 AND account_id = ?2",
                                    [target_cal_id, &account_id],
                                    |_row| Ok(true),
                                )
                                .unwrap_or(false);
                            if !exists_on_same_account {
                                final_category = None;
                            }
                        }
                    }

                    let target_id = if req.edit_scope.as_deref() == Some("all") {
                        recurring_event_id
                            .clone()
                            .unwrap_or_else(|| external_id.clone().unwrap_or_default())
                    } else {
                        external_id.clone().unwrap_or_default()
                    };

                    if source == "google" {
                        let active_cal_id = if let (Some(target_cal_id), Some(cal_id)) =
                            (&req.target_calendar_id, &google_calendar_id)
                        {
                            if target_cal_id != cal_id {
                                match google_calendar_move_event_internal(
                                    &ctx,
                                    &account_id,
                                    cal_id,
                                    &target_id,
                                    target_cal_id,
                                )
                                .await
                                {
                                    Ok(_) => target_cal_id.clone(),
                                    Err(MailFfiError::Core(
                                        maho_core::error::AppError::Network(_),
                                    )) => {
                                        let db = ctx
                                            .pool
                                            .get()
                                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                        maho_core::services::offline_queue::queue_calendar_mutation(
                                            &db,
                                            &account_id,
                                            "calendar_move",
                                            &req.event_id,
                                            Some(&serde_json::json!({
                                                "calendar_id": cal_id,
                                                "external_id": target_id,
                                                "destination_calendar_id": target_cal_id,
                                            }).to_string()),
                                        )?;
                                        db.execute(
                                            "UPDATE calendar_events SET google_calendar_id = ?1 WHERE id = ?2",
                                            params![target_cal_id, req.event_id],
                                        )?;
                                        target_cal_id.clone()
                                    }
                                    Err(e) => return Err(e),
                                }
                            } else {
                                cal_id.clone()
                            }
                        } else {
                            google_calendar_id.clone().unwrap_or_default()
                        };

                        if !active_cal_id.is_empty() && !target_id.is_empty() {
                            let access_role: Option<String> = {
                                let db = ctx
                                    .pool
                                    .get()
                                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                db.query_row(
                                    "SELECT access_role FROM google_calendars WHERE account_id = ?1 AND calendar_id = ?2",
                                    [&account_id, &active_cal_id],
                                    |row| row.get(0),
                                ).ok().flatten()
                            };

                            if !matches!(access_role.as_deref(), Some("owner") | Some("writer")) {
                                return Err(MailFfiError::Core(maho_core::error::AppError::Validation(
                                    "This event is on a read-only calendar and cannot be modified.".to_string(),
                                )));
                            }

                            let start_body = if req.all_day {
                                serde_json::json!({ "date": req.dtstart })
                            } else if let Some(ref tz) = req.time_zone {
                                serde_json::json!({ "dateTime": req.dtstart, "timeZone": tz })
                            } else {
                                serde_json::json!({ "dateTime": req.dtstart })
                            };
                            let end_body = if let Some(ref de) = req.dtend {
                                if req.all_day {
                                    serde_json::json!({ "date": de })
                                } else if let Some(ref tz) = req.time_zone {
                                    serde_json::json!({ "dateTime": de, "timeZone": tz })
                                } else {
                                    serde_json::json!({ "dateTime": de })
                                }
                            } else {
                                start_body.clone()
                            };
                            let mut patch = serde_json::json!({
                                "summary": req.summary,
                                "description": req.description,
                                "location": req.location,
                                "start": start_body,
                                "end": end_body,
                            });

                            if let Some(ref et) = req.event_type {
                                if let Some(obj) = patch.as_object_mut() {
                                    obj.insert("eventType".to_string(), serde_json::json!(et));
                                    if et == "outOfOffice" {
                                        obj.insert(
                                            "outOfOfficeProperties".to_string(),
                                            serde_json::json!({
                                                "autoDeclineMode": "declineAllConflictingInvitations",
                                                "declineMessage": "Declined because I am out of office."
                                            }),
                                        );
                                    }
                                }
                            }

                            if let Some(ref atts) = req.attendees {
                                if let Some(obj) = patch.as_object_mut() {
                                    let g_atts: Vec<serde_json::Value> = atts
                                        .iter()
                                        .map(|email| serde_json::json!({ "email": email }))
                                        .collect();
                                    obj.insert("attendees".to_string(), serde_json::json!(g_atts));
                                }
                            }

                            if let Some(ref rems) = req.reminders {
                                if let Some(obj) = patch.as_object_mut() {
                                    obj.insert("reminders".to_string(), rems.clone());
                                }
                            }

                            let gevent = match google_calendar_update_event_internal(
                                &ctx,
                                &account_id,
                                &active_cal_id,
                                &target_id,
                                patch.clone(),
                            )
                            .await
                            {
                                Ok(g) => g,
                                Err(MailFfiError::Core(maho_core::error::AppError::Network(_))) => {
                                    let db = ctx
                                        .pool
                                        .get()
                                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                    db.execute(
                                        "UPDATE calendar_events SET summary = ?1, description = ?2, dtstart = ?3, dtend = ?4, location = ?5, all_day = ?6, color = ?7, start_tz = ?8, end_tz = ?8, category = ?9, travel_time_minutes = ?10, event_type = ?11, reminders_json = ?12, updated_at = datetime('now') WHERE id = ?13",
                                        params![req.summary, req.description, req.dtstart, req.dtend, req.location, req.all_day as i64, req.color, req.time_zone, final_category, req.travel_time_minutes, req.event_type, req.reminders.as_ref().map(|v| v.to_string()), req.event_id],
                                    )?;
                                    maho_core::services::offline_queue::queue_calendar_mutation(
                                        &db,
                                        &account_id,
                                        "calendar_update",
                                        &req.event_id,
                                        Some(&patch.to_string()),
                                    )?;
                                    let record = db.query_row(
                                        "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                                                ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                                                ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                                                ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                                                ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                                                NULL AS calendar_color, NULL AS calendar_summary, \
                                                ce.category, ce.travel_time_minutes, ce.event_type \
                                         FROM calendar_events ce WHERE ce.id = ?1",
                                        params![req.event_id],
                                        map_calendar_event,
                                    )?;
                                    let json_res = serde_json::to_string(&record)?;
                                    return Ok(json_res);
                                }
                                Err(e) => return Err(e),
                            };
                            let account_email: String = {
                                let db = ctx
                                    .pool
                                    .get()
                                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                db.query_row(
                                    "SELECT email FROM accounts WHERE id = ?1",
                                    [&account_id],
                                    |row| row.get(0),
                                )?
                            };

                            let db = ctx
                                .pool
                                .get()
                                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                            upsert_event(
                                &db,
                                &account_id,
                                Some(&account_email),
                                &active_cal_id,
                                &gevent,
                            )?;

                            db.execute(
                                "UPDATE calendar_events SET category = ?1, travel_time_minutes = ?2, event_type = ?3 WHERE id = ?4",
                                params![final_category, req.travel_time_minutes, req.event_type, req.event_id],
                            )?;
                        }
                    } else {
                        let db = ctx
                            .pool
                            .get()
                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                        db.execute(
                            "UPDATE calendar_events SET summary = ?1, description = ?2, dtstart = ?3, dtend = ?4, location = ?5, all_day = ?6, color = ?7, category = ?8, travel_time_minutes = ?9, event_type = ?10, reminders_json = ?11, updated_at = datetime('now') WHERE id = ?12",
                            params![req.summary, req.description, req.dtstart, req.dtend, req.location, req.all_day as i64, req.color, final_category, req.travel_time_minutes, req.event_type, req.reminders.as_ref().map(|v| v.to_string()), req.event_id],
                        )?;
                    }
                } else {
                    return Err(MailFfiError::Core(maho_core::error::AppError::NotFound(
                        format!(
                            "Calendar event {event_id} not found",
                            event_id = req.event_id
                        ),
                    )));
                }

                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let record = db.query_row(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            NULL AS calendar_color, NULL AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce WHERE ce.id = ?1",
                    params![req.event_id],
                    map_calendar_event,
                )?;
                let json_res = serde_json::to_string(&record)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSearchCalendarEvents(
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
        let Ok(query) = non_empty(query, "query") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let limit_val = if limit <= 0 { 50 } else { limit as usize };
                let like_query = format!("%{}%", query);

                let mut stmt = db.prepare(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            gc.background_color AS calendar_color, \
                            gc.summary AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce \
                     LEFT JOIN google_calendars gc \
                       ON gc.account_id = ce.account_id \
                      AND gc.calendar_id = ce.google_calendar_id \
                     WHERE ce.account_id = ?1 \
                       AND (ce.summary LIKE ?2 OR ce.description LIKE ?2 OR ce.location LIKE ?2) \
                     ORDER BY ce.dtstart DESC \
                     LIMIT ?3",
                )?;

                let rows = stmt.query_map(
                    params![account_id, like_query, limit_val as i64],
                    map_calendar_event,
                )?;

                let mut result = Vec::new();
                for row in rows {
                    result.push(row?);
                }
                Ok(result)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCheckEventConflict(
    account_id: *const c_char,
    dtstart: *const c_char,
    dtend: *const c_char,
    exclude_event_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(dtstart) = non_empty(dtstart, "dtstart") else {
            return false;
        };
        let Ok(dtend) = non_empty(dtend, "dtend") else {
            return false;
        };
        let Ok(exclude_event_id) = opt_string(exclude_event_id, "exclude_event_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut sql = String::from(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            gc.background_color AS calendar_color, \
                            gc.summary AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce \
                     LEFT JOIN google_calendars gc \
                       ON gc.account_id = ce.account_id \
                      AND gc.calendar_id = ce.google_calendar_id \
                     WHERE ce.account_id = ?1 \
                       AND ce.all_day = 0 \
                       AND (gc.id IS NULL OR gc.visible = 1) \
                       AND ce.dtstart < ?2 \
                       AND ce.dtend > ?3",
                );

                let mut params_vec: Vec<Box<dyn rusqlite::types::ToSql>> = vec![
                    Box::new(account_id.clone()) as Box<dyn rusqlite::types::ToSql>,
                    Box::new(dtend.clone()) as Box<dyn rusqlite::types::ToSql>,
                    Box::new(dtstart.clone()) as Box<dyn rusqlite::types::ToSql>,
                ];

                if let Some(ref ex_id) = exclude_event_id {
                    sql.push_str(&format!(" AND ce.id != ?{}", params_vec.len() + 1));
                    params_vec.push(Box::new(ex_id.clone()));
                }

                let mut stmt = db.prepare(&sql)?;
                let param_refs: Vec<&dyn rusqlite::types::ToSql> =
                    params_vec.iter().map(|p| p.as_ref()).collect();
                let rows = stmt
                    .query_map(param_refs.as_slice(), map_calendar_event)?
                    .collect::<std::result::Result<Vec<_>, _>>()?;

                Ok(rows)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDuplicateCalendarEvent(
    event_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(event_id) = non_empty(event_id, "event_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let event = {
                    let db = ctx
                        .pool
                        .get()
                        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                    db.query_row(
                        "SELECT id, account_id, email_id, uid, summary, description, dtstart, dtend, location, organizer, status, rsvp_status, recurrence_rule, all_day, created_at, updated_at, google_calendar_id, start_tz, end_tz, recurring_event_id, attendees_json, reminders_json, color, hangout_link, category, travel_time_minutes, event_type \
                         FROM calendar_events WHERE id = ?1",
                        params![event_id],
                        |row| {
                            Ok((
                                row.get::<_, String>(1)?,
                                row.get::<_, String>(4)?,
                                row.get::<_, Option<String>>(5)?,
                                row.get::<_, String>(6)?,
                                row.get::<_, Option<String>>(7)?,
                                row.get::<_, Option<String>>(8)?,
                                row.get::<_, i64>(13)? != 0,
                                row.get::<_, Option<String>>(16)?,
                                row.get::<_, Option<String>>(17)?,
                                row.get::<_, Option<String>>(20)?,
                                row.get::<_, Option<String>>(24)?,
                                row.get::<_, Option<i64>>(25)?,
                                row.get::<_, Option<String>>(26)?,
                                row.get::<_, Option<String>>(21)?,
                            ))
                        }
                    )?
                };

                let shifted_start = shift_datetime_by_1h(&event.3);
                let shifted_end = event.4.map(|e| shift_datetime_by_1h(&e));

                let atts: Option<Vec<String>> = event.9.and_then(|json| {
                    let parsed: std::result::Result<Vec<serde_json::Value>, _> =
                        serde_json::from_str(&json);
                    parsed.ok().map(|vec| {
                        vec.into_iter()
                            .filter_map(|val| {
                                val.get("email").and_then(|e| e.as_str().map(String::from))
                            })
                            .collect()
                    })
                });

                let reminders: Option<serde_json::Value> =
                    event.13.and_then(|json| serde_json::from_str(&json).ok());

                let req = CreateEventRequest {
                    account_id: event.0,
                    summary: event.1,
                    description: event.2,
                    dtstart: shifted_start,
                    dtend: shifted_end,
                    location: event.5,
                    all_day: event.6,
                    add_meet: Some(false),
                    attendees: atts,
                    color: None,
                    time_zone: event.8,
                    calendar_id: event.7,
                    category: event.10,
                    travel_time_minutes: event.11,
                    event_type: event.12,
                    reminders,
                };

                let calendar_id_res = match req.calendar_id.clone() {
                    Some(cid) if !cid.is_empty() => Some(cid),
                    _ => {
                        let db = ctx
                            .pool
                            .get()
                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                        db.query_row(
                            "SELECT calendar_id FROM google_calendars WHERE account_id = ?1 AND is_primary = 1",
                            [&req.account_id],
                            |row| row.get(0),
                        )
                        .ok()
                    }
                };

                if let Some(ref cal_id) = calendar_id_res {
                    let access_role: Option<String> = {
                        let db = ctx
                            .pool
                            .get()
                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                        db.query_row(
                            "SELECT access_role FROM google_calendars WHERE account_id = ?1 AND calendar_id = ?2",
                            [&req.account_id, cal_id],
                            |row| row.get(0),
                        ).ok().flatten()
                    };

                    let is_writable =
                        matches!(access_role.as_deref(), Some("owner") | Some("writer"));
                    if is_writable {
                        let start_body = if req.all_day {
                            serde_json::json!({ "date": req.dtstart })
                        } else if let Some(ref tz) = req.time_zone {
                            serde_json::json!({ "dateTime": req.dtstart, "timeZone": tz })
                        } else {
                            serde_json::json!({ "dateTime": req.dtstart })
                        };
                        let end_body = if let Some(ref de) = req.dtend {
                            if req.all_day {
                                serde_json::json!({ "date": de })
                            } else if let Some(ref tz) = req.time_zone {
                                serde_json::json!({ "dateTime": de, "timeZone": tz })
                            } else {
                                serde_json::json!({ "dateTime": de })
                            }
                        } else {
                            start_body.clone()
                        };
                        let mut body = serde_json::json!({
                            "summary": req.summary,
                            "description": req.description,
                            "location": req.location,
                            "start": start_body,
                            "end": end_body,
                        });

                        if let Some(ref et) = req.event_type {
                            if let Some(obj) = body.as_object_mut() {
                                obj.insert("eventType".to_string(), serde_json::json!(et));
                                if et == "outOfOffice" {
                                    obj.insert(
                                        "outOfOfficeProperties".to_string(),
                                        serde_json::json!({
                                            "autoDeclineMode": "declineAllConflictingInvitations",
                                            "declineMessage": "Declined because I am out of office."
                                        }),
                                    );
                                }
                            }
                        }

                        if req.add_meet.unwrap_or(false) {
                            if let Some(obj) = body.as_object_mut() {
                                let req_id = uuid::Uuid::new_v4().to_string();
                                obj.insert(
                                    "conferenceData".to_string(),
                                    serde_json::json!({
                                        "createRequest": {
                                            "requestId": req_id,
                                            "conferenceSolutionKey": {
                                                "type": "hangoutsMeet"
                                            }
                                        }
                                    }),
                                );
                            }
                        }

                        if let Some(ref atts) = req.attendees {
                            if let Some(obj) = body.as_object_mut() {
                                let g_atts: Vec<serde_json::Value> = atts
                                    .iter()
                                    .map(|email| serde_json::json!({ "email": email }))
                                    .collect();
                                obj.insert("attendees".to_string(), serde_json::json!(g_atts));
                            }
                        }

                        if let Some(ref rems) = req.reminders {
                            if let Some(obj) = body.as_object_mut() {
                                obj.insert("reminders".to_string(), rems.clone());
                            }
                        }

                        let gevent = match google_calendar_create_event_internal(
                            &ctx,
                            &req.account_id,
                            cal_id,
                            body.clone(),
                        )
                        .await
                        {
                            Ok(g) => g,
                            Err(MailFfiError::Core(maho_core::error::AppError::Network(_))) => {
                                let id = Uuid::new_v4().to_string();
                                let uid = format!("maho-{}", Uuid::new_v4());
                                let db = ctx
                                    .pool
                                    .get()
                                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                                db.execute(
                                    "INSERT INTO calendar_events (id, account_id, email_id, uid, summary, description, dtstart, dtend, location, organizer, status, rsvp_status, recurrence_rule, all_day, color, google_calendar_id, start_tz, category, travel_time_minutes, event_type, reminders_json) \
                                     VALUES (?1, ?2, NULL, ?3, ?4, ?5, ?6, ?7, ?8, NULL, 'confirmed', NULL, NULL, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)",
                                    params![id, req.account_id, uid, req.summary, req.description, req.dtstart, req.dtend, req.location, req.all_day as i64, req.color, cal_id, req.time_zone, req.category, req.travel_time_minutes, req.event_type, req.reminders.as_ref().map(|v| v.to_string())],
                                )?;
                                maho_core::services::offline_queue::queue_calendar_mutation(
                                    &db,
                                    &req.account_id,
                                    "calendar_create",
                                    &id,
                                    Some(&body.to_string()),
                                )?;
                                let record = db.query_row(
                                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                                            NULL AS calendar_color, NULL AS calendar_summary, \
                                            ce.category, ce.travel_time_minutes, ce.event_type \
                                     FROM calendar_events ce WHERE ce.id = ?1",
                                    params![id],
                                    map_calendar_event,
                                )?;
                                let json_res = serde_json::to_string(&record)?;
                                return Ok(json_res);
                            }
                            Err(e) => return Err(e),
                        };
                        let account_email: String = {
                            let db = ctx
                                .pool
                                .get()
                                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                            db.query_row(
                                "SELECT email FROM accounts WHERE id = ?1",
                                [&req.account_id],
                                |row| row.get(0),
                            )?
                        };

                        let db = ctx
                            .pool
                            .get()
                            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                        upsert_event(&db, &req.account_id, Some(&account_email), cal_id, &gevent)?;

                        db.execute(
                            "UPDATE calendar_events SET category = ?1, travel_time_minutes = ?2, event_type = ?3 WHERE account_id = ?4 AND source = 'google' AND external_id = ?5",
                            params![req.category, req.travel_time_minutes, req.event_type, req.account_id, gevent.id],
                        )?;

                        let record = db.query_row(
                            "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                                    ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                                    ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                                    ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                                    ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                                    NULL AS calendar_color, NULL AS calendar_summary, \
                                    ce.category, ce.travel_time_minutes, ce.event_type \
                             FROM calendar_events ce WHERE ce.account_id = ?1 AND ce.source = 'google' AND ce.external_id = ?2",
                            params![req.account_id, gevent.id],
                            map_calendar_event,
                        )?;
                        let json_res = serde_json::to_string(&record)?;
                        return Ok(json_res);
                    }
                }

                let id = Uuid::new_v4().to_string();
                let uid = format!("maho-{}", Uuid::new_v4());
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                db.execute(
                    "INSERT INTO calendar_events (id, account_id, email_id, uid, summary, description, dtstart, dtend, location, organizer, status, rsvp_status, recurrence_rule, all_day, color, category, travel_time_minutes, event_type) \
                     VALUES (?1, ?2, NULL, ?3, ?4, ?5, ?6, ?7, ?8, NULL, 'confirmed', NULL, NULL, ?9, ?10, ?11, ?12, ?13)",
                    params![id, req.account_id, uid, req.summary, req.description, req.dtstart, req.dtend, req.location, req.all_day as i64, req.color, req.category, req.travel_time_minutes, req.event_type],
                )?;
                let record = db.query_row(
                    "SELECT ce.id, ce.account_id, ce.email_id, ce.uid, ce.summary, ce.description, \
                            ce.dtstart, ce.dtend, ce.location, ce.organizer, ce.status, ce.rsvp_status, \
                            ce.recurrence_rule, ce.all_day, ce.created_at, ce.updated_at, \
                            ce.google_calendar_id, ce.start_tz, ce.end_tz, ce.recurring_event_id, \
                            ce.attendees_json, ce.reminders_json, ce.color, ce.hangout_link, \
                            NULL AS calendar_color, NULL AS calendar_summary, \
                            ce.category, ce.travel_time_minutes, ce.event_type \
                     FROM calendar_events ce WHERE ce.id = ?1",
                    params![id],
                    map_calendar_event,
                )?;
                let json_res = serde_json::to_string(&record)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGoogleCalendarMoveEvent(
    account_id: *const c_char,
    calendar_id: *const c_char,
    event_id: *const c_char,
    destination_calendar_id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(calendar_id) = non_empty(calendar_id, "calendar_id") else {
            return false;
        };
        let Ok(event_id) = non_empty(event_id, "event_id") else {
            return false;
        };
        let Ok(destination_calendar_id) =
            non_empty(destination_calendar_id, "destination_calendar_id")
        else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let gevent = google_calendar_move_event_internal(
                    &ctx,
                    &account_id,
                    &calendar_id,
                    &event_id,
                    &destination_calendar_id,
                )
                .await?;
                let json_res = serde_json::to_string(&gevent)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailExportCalendarIcs(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<ExportCalendarIcsRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut sql = String::from(
                    "SELECT id, account_id, email_id, uid, summary, description, dtstart, dtend, location, organizer, status, rsvp_status, recurrence_rule, all_day, created_at, updated_at, google_calendar_id, start_tz, end_tz, recurring_event_id, attendees_json, reminders_json, color, hangout_link, category, travel_time_minutes, event_type \
                     FROM calendar_events \
                     WHERE account_id = ?1"
                );
                let mut params_vec: Vec<Box<dyn rusqlite::types::ToSql>> =
                    vec![Box::new(req.account_id.clone()) as Box<dyn rusqlite::types::ToSql>];

                if let Some(ref from) = req.from_date {
                    sql.push_str(&format!(" AND dtstart >= ?{}", params_vec.len() + 1));
                    params_vec.push(Box::new(from.clone()));
                }
                if let Some(ref to) = req.to_date {
                    sql.push_str(&format!(" AND dtstart <= ?{}", params_vec.len() + 1));
                    params_vec.push(Box::new(to.clone()));
                }

                let mut stmt = db.prepare(&sql)?;
                let param_refs: Vec<&dyn rusqlite::types::ToSql> =
                    params_vec.iter().map(|p| p.as_ref()).collect();
                let mut events = stmt
                    .query_map(param_refs.as_slice(), map_calendar_event)?
                    .collect::<std::result::Result<Vec<CalendarEventRecord>, _>>()?;

                if let Some(ref cal_ids) = req.calendar_ids {
                    events.retain(|e| {
                        if let Some(ref g_cal_id) = e.google_calendar_id {
                            cal_ids.contains(g_cal_id)
                        } else {
                            false
                        }
                    });
                }

                let mut ics = String::from(
                    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Maho Mail//Calendar//EN\r\n",
                );
                for ev in events {
                    ics.push_str("BEGIN:VEVENT\r\n");
                    ics.push_str(&format!("UID:{}\r\n", ev.uid));
                    ics.push_str(&format!("SUMMARY:{}\r\n", escape_ics_value(&ev.summary)));
                    if let Some(ref desc) = ev.description {
                        ics.push_str(&format!("DESCRIPTION:{}\r\n", escape_ics_value(desc)));
                    }
                    if ev.all_day {
                        let start_date = ev
                            .dtstart
                            .replace("-", "")
                            .chars()
                            .take(8)
                            .collect::<String>();
                        ics.push_str(&format!("DTSTART;VALUE=DATE:{}\r\n", start_date));
                        if let Some(ref dtend) = ev.dtend {
                            let end_date =
                                dtend.replace("-", "").chars().take(8).collect::<String>();
                            ics.push_str(&format!("DTEND;VALUE=DATE:{}\r\n", end_date));
                        }
                    } else {
                        ics.push_str(&format_ics_date_property(
                            "DTSTART", &ev.dtstart, ev.start_tz.as_deref(),
                        ).map_err(maho_core::error::AppError::Validation)?);
                        ics.push_str("\r\n");
                        if let Some(ref dtend) = ev.dtend {
                            ics.push_str(&format_ics_date_property(
                                "DTEND", dtend, ev.end_tz.as_deref(),
                            ).map_err(maho_core::error::AppError::Validation)?);
                            ics.push_str("\r\n");
                        }
                    }
                    if let Some(ref loc) = ev.location {
                        ics.push_str(&format!("LOCATION:{}\r\n", escape_ics_value(loc)));
                    }
                    if let Some(ref rrule) = ev.recurrence_rule {
                        ics.push_str(&format!("RRULE:{}\r\n", rrule));
                    }
                    ics.push_str("END:VEVENT\r\n");
                }
                ics.push_str("END:VCALENDAR\r\n");

                Ok(ics)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListCalendarCategories(
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
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = db.prepare(
                    "SELECT id, account_id, name, color, created_at FROM calendar_categories WHERE account_id = ?1"
                )?;
                let rows = stmt
                    .query_map([&account_id], |row| {
                        Ok(CalendarCategoryRecord {
                            id: row.get(0)?,
                            account_id: row.get(1)?,
                            name: row.get(2)?,
                            color: row.get(3)?,
                            created_at: row.get(4)?,
                        })
                    })?
                    .collect::<std::result::Result<Vec<_>, _>>()?;
                let json_res = serde_json::to_string(&rows)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailCreateCalendarCategory(
    account_id: *const c_char,
    name: *const c_char,
    color: *const c_char,
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
        let Ok(color) = non_empty(color, "color") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let id = Uuid::new_v4().to_string();
                db.execute(
                    "INSERT INTO calendar_categories (id, account_id, name, color, created_at) VALUES (?1, ?2, ?3, ?4, datetime('now'))",
                    params![id, account_id, name, color],
                )?;
                let record = CalendarCategoryRecord {
                    id,
                    account_id,
                    name,
                    color,
                    created_at: "".to_string(),
                };
                let json_res = serde_json::to_string(&record)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailUpdateCalendarCategory(
    id: *const c_char,
    name: *const c_char,
    color: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        let Ok(name) = non_empty(name, "name") else {
            return false;
        };
        let Ok(color) = non_empty(color, "color") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                db.execute(
                    "UPDATE calendar_categories SET name = ?1, color = ?2 WHERE id = ?3",
                    params![name, color, id],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailDeleteCalendarCategory(
    id: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(id) = non_empty(id, "id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                db.execute(
                    "UPDATE calendar_events SET category = NULL WHERE category = (SELECT name FROM calendar_categories WHERE id = ?1)",
                    params![id],
                )?;
                db.execute("DELETE FROM calendar_categories WHERE id = ?1", params![id])?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSyncGoogleCalendar(
    account_id: *const c_char,
    _full_sync: bool,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let result = sync_internal(&ctx, &account_id).await?;
                let json_res = serde_json::to_string(&result)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailListAccountCalendars(
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
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                let mut stmt = db.prepare(
                    "SELECT id, account_id, calendar_id, summary, background_color, foreground_color, \
                            is_primary, access_role, visible \
                     FROM google_calendars \
                     WHERE account_id = ?1 \
                     ORDER BY is_primary DESC, summary COLLATE NOCASE ASC",
                )?;
                let rows = stmt
                    .query_map(params![account_id], |row| {
                        Ok(GoogleCalendarEntry {
                            id: row.get(0)?,
                            account_id: row.get(1)?,
                            calendar_id: row.get(2)?,
                            summary: row.get(3)?,
                            background_color: row.get(4)?,
                            foreground_color: row.get(5)?,
                            is_primary: row.get::<_, i64>(6)? != 0,
                            access_role: row.get(7)?,
                            visible: row.get::<_, i64>(8)? != 0,
                        })
                    })?
                    .collect::<std::result::Result<Vec<_>, _>>()?;
                let json_res = serde_json::to_string(&rows)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSetCalendarVisibility(
    calendar_row_id: *const c_char,
    visible: bool,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(calendar_row_id) = non_empty(calendar_row_id, "calendar_row_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let db = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                db.execute(
                    "UPDATE google_calendars SET visible = ?1, updated_at = ?2 WHERE id = ?3",
                    params![
                        visible as i64,
                        chrono::Utc::now().to_rfc3339(),
                        calendar_row_id
                    ],
                )?;
                Ok("{}".to_string())
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailSubscribeHolidayCalendar(
    account_id: *const c_char,
    locale_code: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(account_id) = non_empty(account_id, "account_id") else {
            return false;
        };
        let Ok(locale_code) = non_empty(locale_code, "locale_code") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let (client, access_token) = get_client_and_token(&ctx, &account_id).await?;
                let calendar_id = format!("{}#holiday@group.v.calendar.google.com", locale_code);
                let body = serde_json::json!({ "id": calendar_id });

                let resp = client
                    .post("https://www.googleapis.com/calendar/v3/users/me/calendarList")
                    .bearer_auth(&access_token)
                    .json(&body)
                    .send()
                    .await
                    .map_err(|e| {
                        MailFfiError::Core(maho_core::error::AppError::Network(format!(
                            "calendarList.insert request: {}",
                            e
                        )))
                    })?;

                let status = resp.status();
                if !status.is_success() && status.as_u16() != 409 {
                    let text = resp.text().await.unwrap_or_default();
                    return Err(MailFfiError::Core(maho_core::error::AppError::Network(
                        format!("calendarList.insert HTTP {}: {}", status, text),
                    )));
                }

                let sync_res = sync_internal(&ctx, &account_id).await?;
                let json_res = serde_json::to_string(&sync_res)?;
                Ok(json_res)
            })
        })
    })
}

#[no_mangle]
pub extern "C" fn MahoMailGoogleCalendarFreeBusy(
    request_json: *const c_char,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(raw) = c_string(request_json, "request_json") else {
            return false;
        };
        let Ok(req) = serde_json::from_str::<GoogleCalendarFreeBusyRequest>(&raw) else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            Box::pin(async move {
                let (client, token) = get_client_and_token(&ctx, &req.account_id).await?;
                let url = "https://www.googleapis.com/calendar/v3/freeBusy";

                let items: Vec<serde_json::Value> = req
                    .emails
                    .into_iter()
                    .map(|email| serde_json::json!({ "id": email }))
                    .collect();
                let body = serde_json::json!({
                    "timeMin": req.time_min,
                    "timeMax": req.time_max,
                    "items": items
                });

                let resp = client
                    .post(url)
                    .bearer_auth(&token)
                    .json(&body)
                    .send()
                    .await
                    .map_err(|e| {
                        MailFfiError::Core(maho_core::error::AppError::Network(format!(
                            "freeBusy request: {}",
                            e
                        )))
                    })?;

                let status = resp.status();
                let text = resp.text().await.unwrap_or_default();
                if !status.is_success() {
                    return Err(MailFfiError::Core(maho_core::error::AppError::Network(
                        format!("freeBusy HTTP {}: {}", status, text),
                    )));
                }

                let parsed: serde_json::Value = serde_json::from_str(&text).map_err(|e| {
                    MailFfiError::Core(maho_core::error::AppError::Network(format!(
                        "freeBusy JSON: {}",
                        e
                    )))
                })?;
                let json_res = serde_json::to_string(&parsed)?;
                Ok(json_res)
            })
        })
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn basic_params() -> IcsReplyParams {
        IcsReplyParams {
            uid: "evt-abc-123".into(),
            sequence: "".into(),
            dtstart: "20260715T100000Z".into(),
            dtend: Some("20260715T110000Z".into()),
            start_tz: None,
            end_tz: None,
            summary: "Weekly Standup".into(),
            organizer_line: "CN=Alice:mailto:alice@x.com".into(),
            attendee_email: "bob@example.com".into(),
            attendee_name: Some("Bob".into()),
            partstat: "ACCEPTED".into(),
        }
    }

    #[test]
    fn test_build_ics_reply_basic() {
        let output = build_ics_reply(basic_params()).unwrap();
        assert!(output.contains("METHOD:REPLY\r\n"));
        let attendee_count = output.matches("\r\nATTENDEE").count();
        assert_eq!(attendee_count, 1, "expected exactly one ATTENDEE line");
        assert!(output.contains("PARTSTAT=ACCEPTED"));
        assert!(!output.contains("RSVP=TRUE"));
        let has_dtstamp = output.lines().any(|l| {
            let l = l.trim_end_matches('\r');
            l.starts_with("DTSTAMP:") && {
                let val = &l[8..];
                val.len() == 16
                    && val.chars().enumerate().all(|(i, c)| match i {
                        0..=7 => c.is_ascii_digit(),
                        8 => c == 'T',
                        9..=14 => c.is_ascii_digit(),
                        15 => c == 'Z',
                        _ => false,
                    })
            }
        });
        assert!(has_dtstamp, "DTSTAMP must match YYYYMMDDTHHMMSSz format");
        assert!(output.contains("SEQUENCE:0\r\n"));
        assert!(output.contains("PRODID:-//Maho Mail//RSVP//EN\r\n"));
        assert!(!output.contains("\n\n"));
        let crlf_count = output.matches("\r\n").count();
        assert!(
            crlf_count > 10,
            "expected >10 CRLF separators, got {}",
            crlf_count
        );
        assert!(!output.contains("VALARM"));
        assert!(!output.contains("RRULE"));
        assert!(!output.contains("EXDATE"));
        assert!(output.contains("ORGANIZER:CN=Alice:mailto:alice@x.com\r\n"));
    }

    #[test]
    fn test_build_ics_reply_partstat_variants() {
        let mut params = basic_params();
        params.partstat = "TENTATIVE".into();
        let output = build_ics_reply(params).unwrap();
        assert!(output.contains("PARTSTAT=TENTATIVE"));

        let mut params = basic_params();
        params.partstat = "DECLINED".into();
        let output = build_ics_reply(params).unwrap();
        assert!(output.contains("PARTSTAT=DECLINED"));
    }

    #[test]
    fn test_build_ics_reply_rejects_invalid_partstat() {
        let mut params = basic_params();
        params.partstat = "NEEDS-ACTION".into();
        let result = build_ics_reply(params);
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("invalid partstat"));

        let mut params = basic_params();
        params.partstat = "".into();
        let result = build_ics_reply(params);
        assert!(result.is_err());
    }

    #[test]
    fn test_build_ics_reply_escapes_summary() {
        let mut params = basic_params();
        params.summary = "Meet, greet; discuss\\stuff".into();
        let output = build_ics_reply(params).unwrap();
        assert!(
            output.contains("SUMMARY:Meet\\, greet\\; discuss\\\\stuff"),
            "escaped summary not found in output: {}",
            output
        );
    }

    #[test]
    fn test_build_ics_reply_folds_long_lines() {
        let mut params = basic_params();
        params.summary = "x".repeat(200);
        let output = build_ics_reply(params).unwrap();
        assert!(
            output.contains("\r\n "),
            "expected fold sequence (CRLF+SPACE)"
        );
        for segment in output.split("\r\n") {
            if segment.is_empty() {
                continue;
            }
            assert!(
                segment.len() <= 75,
                "line exceeds 75 octets ({} bytes): {:?}",
                segment.len(),
                &segment[..segment.len().min(80)]
            );
        }
    }

    #[test]
    fn test_build_ics_reply_no_dtend() {
        let mut params = basic_params();
        params.dtend = None;
        let output = build_ics_reply(params).unwrap();
        assert!(!output.contains("DTEND"));
    }

    mod pending_rsvp_guard_tests {
        use super::super::apply_pending_rsvp_guard;
        use rusqlite::Connection;

        fn setup_db() -> Connection {
            let conn = Connection::open_in_memory().unwrap();
            conn.execute_batch(
                "CREATE TABLE pending_mutations (
                    id TEXT PRIMARY KEY,
                    account_id TEXT NOT NULL,
                    email_uid INTEGER,
                    folder_path TEXT,
                    mutation_type TEXT NOT NULL,
                    target_folder TEXT,
                    calendar_event_id TEXT,
                    payload_json TEXT,
                    created_at TEXT NOT NULL DEFAULT (datetime('now'))
                );",
            )
            .unwrap();
            conn
        }

        #[test]
        fn test_sync_does_not_overwrite_pending_rsvp_plain() {
            let conn = setup_db();
            conn.execute(
                "INSERT INTO pending_mutations (id, account_id, mutation_type, calendar_event_id, payload_json)
                 VALUES ('mut-1', 'acc-1', 'calendar_rsvp', 'local-uuid-1', 'declined')",
                [],
            ).unwrap();

            let result = apply_pending_rsvp_guard(&conn, "local-uuid-1", Some("accepted".into()));
            assert_eq!(result, Some("declined".into()));
        }

        #[test]
        fn test_sync_does_not_overwrite_pending_rsvp_json() {
            let conn = setup_db();
            conn.execute(
                r#"INSERT INTO pending_mutations (id, account_id, mutation_type, calendar_event_id, payload_json)
                 VALUES ('mut-2', 'acc-1', 'calendar_rsvp_reply', 'local-uuid-2', '{"rsvp_status":"tentative"}')"#,
                [],
            ).unwrap();

            let result = apply_pending_rsvp_guard(&conn, "local-uuid-2", Some("accepted".into()));
            assert_eq!(result, Some("tentative".into()));
        }

        #[test]
        fn test_sync_preserves_server_value_when_no_pending() {
            let conn = setup_db();
            let result = apply_pending_rsvp_guard(&conn, "local-uuid-3", Some("accepted".into()));
            assert_eq!(result, Some("accepted".into()));
        }
    }

    #[test]
    fn test_partstat_from_rsvp_status_valid() {
        assert_eq!(partstat_from_rsvp_status("accepted"), Some("ACCEPTED"));
        assert_eq!(partstat_from_rsvp_status("tentative"), Some("TENTATIVE"));
        assert_eq!(partstat_from_rsvp_status("declined"), Some("DECLINED"));
    }

    #[test]
    fn test_partstat_from_rsvp_status_invalid() {
        assert_eq!(partstat_from_rsvp_status("needsAction"), None);
        assert_eq!(partstat_from_rsvp_status(""), None);
        assert_eq!(partstat_from_rsvp_status("ACCEPTED"), None);
    }

    #[test]
    fn test_subject_prefix_matches_rfc_english() {
        assert_eq!(subject_prefix_for_partstat("ACCEPTED"), "Accepted");
        assert_eq!(subject_prefix_for_partstat("TENTATIVE"), "Tentative");
        assert_eq!(subject_prefix_for_partstat("DECLINED"), "Declined");
        assert_eq!(subject_prefix_for_partstat("unknown"), "RSVP");
    }

    #[test]
    fn test_ics_reply_full_pipeline_produces_valid_body() {
        use base64::Engine as _;

        let params = IcsReplyParams {
            uid: "googlecal-invite-xyz@google.com".into(),
            sequence: "1".into(),
            dtstart: "20260720T140000Z".into(),
            dtend: Some("20260720T150000Z".into()),
            start_tz: None,
            end_tz: None,
            summary: "Q3 Planning".into(),
            organizer_line: "CN=Alice Johnson:mailto:alice@corp.io".into(),
            attendee_email: "bob@example.com".into(),
            attendee_name: Some("Bob Smith".into()),
            partstat: "TENTATIVE".into(),
        };

        let ics_text = build_ics_reply(params).unwrap();
        let encoded = base64::engine::general_purpose::STANDARD.encode(ics_text.as_bytes());
        let decoded_bytes = base64::engine::general_purpose::STANDARD
            .decode(&encoded)
            .unwrap();
        let decoded = String::from_utf8(decoded_bytes).unwrap();
        assert_eq!(decoded, ics_text);

        assert!(ics_text.contains("METHOD:REPLY\r\n"));
        let attendee_count = ics_text.matches("\r\nATTENDEE").count();
        assert_eq!(attendee_count, 1);
        assert!(ics_text.contains("PARTSTAT=TENTATIVE"));

        let has_dtstamp = ics_text.lines().any(|l| {
            let l = l.trim_end_matches('\r');
            l.starts_with("DTSTAMP:")
        });
        assert!(has_dtstamp);
        assert!(!ics_text.contains("VALARM"));
        assert_eq!(subject_prefix_for_partstat("TENTATIVE"), "Tentative");
    }

    mod google_rsvp_merge_tests {
        use super::super::merge_rsvp_into_attendees;
        use serde_json::json;

        #[test]
        fn test_s4_google_rsvp_preserves_other_attendees_via_self_flag() {
            let existing = vec![
                json!({ "email": "me@test.com", "self": true, "responseStatus": "needsAction" }),
                json!({ "email": "alice@test.com", "responseStatus": "accepted" }),
                json!({ "email": "bob@test.com", "responseStatus": "tentative" }),
            ];
            let merged = merge_rsvp_into_attendees(&existing, "me@test.com", "accepted");

            assert_eq!(merged.len(), 3);
            assert_eq!(merged[0]["email"], "me@test.com");
            assert_eq!(merged[0]["responseStatus"], "accepted");
            assert_eq!(merged[0]["self"], true);
            assert_eq!(merged[1]["email"], "alice@test.com");
            assert_eq!(merged[1]["responseStatus"], "accepted");
            assert_eq!(merged[2]["email"], "bob@test.com");
            assert_eq!(merged[2]["responseStatus"], "tentative");
        }

        #[test]
        fn test_s4_google_rsvp_identifies_self_by_email_case_insensitive() {
            let existing = vec![
                json!({ "email": "ME@Test.COM", "responseStatus": "needsAction" }),
                json!({ "email": "other@test.com", "responseStatus": "declined" }),
            ];
            let merged = merge_rsvp_into_attendees(&existing, "me@test.com", "tentative");

            assert_eq!(merged.len(), 2);
            assert_eq!(merged[0]["responseStatus"], "tentative");
            assert_eq!(merged[1]["responseStatus"], "declined");
        }

        #[test]
        fn test_s4_google_rsvp_appends_when_self_missing() {
            let existing = vec![json!({ "email": "alice@test.com", "responseStatus": "accepted" })];
            let merged = merge_rsvp_into_attendees(&existing, "me@test.com", "accepted");

            assert_eq!(merged.len(), 2);
            assert_eq!(merged[0]["email"], "alice@test.com");
            assert_eq!(merged[0]["responseStatus"], "accepted");
            assert_eq!(merged[1]["email"], "me@test.com");
            assert_eq!(merged[1]["responseStatus"], "accepted");
            assert!(merged[1].get("self").is_none());
        }

        #[test]
        fn test_s4_google_rsvp_empty_attendees() {
            let existing: Vec<serde_json::Value> = vec![];
            let merged = merge_rsvp_into_attendees(&existing, "me@test.com", "accepted");

            assert_eq!(merged.len(), 1);
            assert_eq!(merged[0]["email"], "me@test.com");
            assert_eq!(merged[0]["responseStatus"], "accepted");
        }

        #[test]
        fn test_s4_google_rsvp_preserves_extra_fields_on_self() {
            let existing = vec![json!({
                "email": "me@test.com",
                "self": true,
                "responseStatus": "needsAction",
                "displayName": "Me User",
                "organizer": false,
            })];
            let merged = merge_rsvp_into_attendees(&existing, "me@test.com", "declined");

            assert_eq!(merged[0]["responseStatus"], "declined");
            assert_eq!(merged[0]["displayName"], "Me User");
            assert_eq!(merged[0]["organizer"], false);
            assert_eq!(merged[0]["self"], true);
        }
    }

    mod extract_bare_email_tests {
        use super::super::extract_bare_email;

        #[test]
        fn test_extract_bare_email_mailto_with_cn() {
            assert_eq!(
                extract_bare_email("CN=Alice:mailto:alice@x.com"),
                "alice@x.com"
            );
        }

        #[test]
        fn test_extract_bare_email_plain_mailto() {
            assert_eq!(extract_bare_email("mailto:bob@y.com"), "bob@y.com");
        }

        #[test]
        fn test_extract_bare_email_angle_bracket_form() {
            assert_eq!(extract_bare_email("Alice <alice@x.com>"), "alice@x.com");
        }

        #[test]
        fn test_extract_bare_email_bare() {
            assert_eq!(extract_bare_email("charlie@z.com"), "charlie@z.com");
        }
    }

    mod offline_queue_replay_tests {
        #[test]
        fn test_calendar_rsvp_reply_payload_roundtrip() {
            let payload_json = serde_json::json!({
                "ics_text": "BEGIN:VCALENDAR\r\nMETHOD:REPLY\r\nEND:VCALENDAR",
                "organizer_email": "alice@example.com",
                "subject": "Accepted: Weekly Standup",
                "attendee_email": "bob@example.com",
                "display_name": "Bob Smith",
                "in_reply_to": "<original-msg-id@example.com>",
                "references": "<ref1@example.com> <ref2@example.com>",
                "body_text": "Bob Smith has accepted the invitation."
            });

            let serialized = serde_json::to_string(&payload_json).unwrap();
            let parsed: serde_json::Value = serde_json::from_str(&serialized).unwrap();

            assert_eq!(
                parsed
                    .get("ics_text")
                    .and_then(|v| v.as_str())
                    .unwrap_or_default(),
                "BEGIN:VCALENDAR\r\nMETHOD:REPLY\r\nEND:VCALENDAR"
            );
            assert_eq!(
                parsed
                    .get("organizer_email")
                    .and_then(|v| v.as_str())
                    .unwrap_or_default(),
                "alice@example.com"
            );
            assert_eq!(
                parsed
                    .get("subject")
                    .and_then(|v| v.as_str())
                    .unwrap_or_default(),
                "Accepted: Weekly Standup"
            );
            assert_eq!(
                parsed
                    .get("attendee_email")
                    .and_then(|v| v.as_str())
                    .unwrap_or_default(),
                "bob@example.com"
            );
            assert_eq!(
                parsed.get("display_name").and_then(|v| v.as_str()),
                Some("Bob Smith")
            );
        }
    }

    mod organizer_normalization_tests {
        use super::super::{build_ics_reply, escape_ics_qstring, normalize_organizer_cal_address};
        use super::basic_params;

        #[test]
        fn test_organizer_bare_email_gets_mailto_prefix() {
            let mut params = basic_params();
            params.organizer_line = "alice@x.com".into();
            let output = build_ics_reply(params).unwrap();
            assert!(
                output.contains("ORGANIZER:mailto:alice@x.com"),
                "got: {}",
                output
            );
        }

        #[test]
        fn test_organizer_with_mailto_kept_as_is() {
            let mut params = basic_params();
            params.organizer_line = "CN=Alice:mailto:alice@x.com".into();
            let output = build_ics_reply(params).unwrap();
            assert!(
                output.contains("ORGANIZER:CN=Alice:mailto:alice@x.com"),
                "got: {}",
                output
            );
        }

        #[test]
        fn test_organizer_angle_bracket_form_normalized() {
            let mut params = basic_params();
            params.organizer_line = "Alice <alice@x.com>".into();
            let output = build_ics_reply(params).unwrap();
            assert!(
                output.contains("ORGANIZER:CN=\"Alice\":mailto:alice@x.com"),
                "got: {}",
                output
            );
        }

        #[test]
        fn test_organizer_empty_produces_no_line() {
            let mut params = basic_params();
            params.organizer_line = "".into();
            let output = build_ics_reply(params).unwrap();
            assert!(!output.contains("ORGANIZER"), "got: {}", output);
        }

        #[test]
        fn test_normalize_organizer_case_insensitive_mailto() {
            let result = normalize_organizer_cal_address("CN=A:MAILTO:a@x");
            assert_eq!(result, "CN=A:MAILTO:a@x");
        }

        #[test]
        fn test_escape_ics_qstring_dquote_and_backslash() {
            assert_eq!(
                escape_ics_qstring(r#"Bob "The Man" Smith"#),
                r#"Bob \"The Man\" Smith"#
            );
            assert_eq!(escape_ics_qstring(r#"back\slash"#), r#"back\\slash"#);
        }

        #[test]
        fn test_escape_ics_text_crlf_normalization() {
            let input = "Title\r\nINJECTED:VAL\rLONE_CR\nLF;semi,comma\\slash";
            let escaped = super::super::escape_ics_text(input);
            assert!(
                !escaped.contains('\r'),
                "escaped text must not contain raw CR: {escaped}"
            );
            assert_eq!(
                escaped,
                "Title\\nINJECTED:VAL\\nLONE_CR\\nLF\\;semi\\,comma\\\\slash"
            );
        }
    }

    mod attendee_cn_escape_tests {
        use super::super::{build_ics_reply, IcsReplyParams};

        #[test]
        fn test_attendee_cn_escapes_dquote() {
            let params = IcsReplyParams {
                uid: "evt-test".into(),
                sequence: "0".into(),
                dtstart: "20260715T100000Z".into(),
                dtend: Some("20260715T110000Z".into()),
                start_tz: None,
                end_tz: None,
                summary: "Meeting".into(),
                organizer_line: "mailto:org@x.com".into(),
                attendee_email: "bob@x.com".into(),
                attendee_name: Some(r#"Bob "The Man" Smith"#.into()),
                partstat: "ACCEPTED".into(),
            };
            let output = build_ics_reply(params).unwrap();
            assert!(
                output.contains(r#"CN="Bob \"The Man\" Smith""#),
                "got: {}",
                output
            );
        }
    }

    mod sequence_tests {
        use super::super::parse_ics;

        #[test]
        fn test_parse_ics_captures_sequence() {
            let ics = "BEGIN:VCALENDAR\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\nUID:test-uid-123\r\nSUMMARY:Team Sync\r\nDTSTART:20260715T100000Z\r\nSEQUENCE:3\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
            let events = parse_ics(ics);
            assert_eq!(events.len(), 1);
            assert_eq!(events[0].sequence, Some("3".to_string()));
        }

        #[test]
        fn test_parse_ics_defaults_sequence_to_zero() {
            let ics = "BEGIN:VCALENDAR\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\nUID:test-uid-456\r\nSUMMARY:No Sequence Event\r\nDTSTART:20260715T100000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
            let events = parse_ics(ics);
            assert_eq!(events.len(), 1);
            assert_eq!(events[0].sequence, None);
        }
    }

    #[test]
    fn primary_calendar_entry_maps_events_list_metadata() {
        let meta: GEventsCalendarMeta =
            serde_json::from_str(r#"{"summary":"Work","accessRole":"owner"}"#).unwrap();
        let entry = primary_calendar_entry(Some("user@example.com"), meta);
        assert_eq!(entry.id, "user@example.com");
        assert_eq!(entry.summary.as_deref(), Some("Work"));
        assert_eq!(entry.primary, Some(true));
        assert_eq!(entry.access_role.as_deref(), Some("owner"));

        let entry = primary_calendar_entry(None, serde_json::from_str("{}").unwrap());
        assert_eq!(entry.id, "primary");
        assert_eq!(entry.access_role, None);
    }
}

#[cfg(test)]
#[path = "calendar_review_tests.rs"]
mod calendar_review_tests;

#[cfg(test)]
#[path = "calendar_replay_tests.rs"]
mod calendar_replay_tests;

// === [W-I] ===

#[no_mangle]
pub extern "C" fn MahoMailSnoozeCalendarEvent(
    event_id: *const c_char,
    minutes: i64,
    callback: MahoMailReadCallback,
    user_data: *mut c_void,
) -> bool {
    accept_read_call(move || {
        let Ok(event_id) = non_empty(event_id, "event_id") else {
            return false;
        };
        accept_read(callback, user_data, move |ctx| {
            blocking_json(move || {
                let snooze_until =
                    (chrono::Utc::now() + chrono::Duration::minutes(minutes)).to_rfc3339();
                let conn = ctx
                    .pool
                    .get()
                    .map_err(|e| MailFfiError::Pool(e.to_string()))?;
                conn.execute(
                    "INSERT OR REPLACE INTO calendar_event_snoozes (event_id, snooze_until) VALUES (?1, ?2)",
                    rusqlite::params![event_id, snooze_until],
                ).map_err(|e| MailFfiError::Core(maho_core::error::AppError::Database(e)))?;
                Ok("{}".to_string())
            })
        })
    })
}
