use super::*;
use std::ffi::{CStr, CString};
use std::sync::mpsc;
use std::time::Duration;

#[path = "calendar_upgrade_tests.rs"]
mod upgrade;
#[path = "calendar_date_wire_tests.rs"]
mod date_wire;
#[path = "calendar_reply_boundary_tests.rs"]
mod reply_boundary;

unsafe extern "C" fn capture(ok: bool, payload: *const c_char, data: *mut c_void) {
    let sender = unsafe { Box::from_raw(data.cast::<mpsc::Sender<(bool, Vec<u8>)>>()) };
    let payload = unsafe { CStr::from_ptr(payload) }.to_bytes().to_vec();
    if sender.send((ok, payload)).is_err() {
        // Receiver already failed its bounded callback wait.
        return;
    }
}

fn call(invoke: impl FnOnce(MahoMailReadCallback, *mut c_void) -> bool) -> (bool, String) {
    let (sender, receiver) = mpsc::channel::<(bool, Vec<u8>)>();
    let data = Box::into_raw(Box::new(sender));
    let accepted = invoke(Some(capture), data.cast());
    if !accepted {
        drop(unsafe { Box::from_raw(data) });
    }
    assert!(accepted, "FFI rejected valid input");
    let (ok, payload) = receiver
        .recv_timeout(Duration::from_secs(10))
        .expect("calendar callback");
    (
        ok,
        String::from_utf8(payload).expect("calendar callback UTF-8"),
    )
}

struct Fixture {
    ctx: std::sync::Arc<AppCtx>,
    _guard: std::sync::MutexGuard<'static, ()>,
}

impl Fixture {
    fn new() -> Self {
        let guard = crate::test_support::global_ctx_guard();
        let ctx = crate::test_support::ctx_arc(crate::test_support::pool_with_seeded_data());
        // Missing credentials guarantee failure before any transport is opened.
        ctx.pool
            .get()
            .unwrap()
            .execute("UPDATE accounts SET password = NULL WHERE id = 'acc1'", [])
            .unwrap();
        crate::state::set_ctx(ctx.clone()).unwrap();
        Self { ctx, _guard: guard }
    }

    fn import(&self, dates: &str) -> CalendarEventRecord {
        let account = CString::new("acc1").unwrap();
        let ics = CString::new(format!("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:calendar-review\r\nSUMMARY:Review\r\nORGANIZER:mailto:organizer@example.invalid\r\n{dates}\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")).unwrap();
        let (ok, payload) = call(|cb, data| {
            MahoMailImportCalendarEvent(account.as_ptr(), std::ptr::null(), ics.as_ptr(), cb, data)
        });
        assert!(ok, "{payload}");
        serde_json::from_str(&payload).unwrap()
    }

    fn list(&self, from: &str, to: &str) -> Vec<CalendarEventRecord> {
        let account = CString::new("acc1").unwrap();
        let from = CString::new(from).unwrap();
        let to = CString::new(to).unwrap();
        let (ok, payload) = call(|cb, data| {
            MahoMailListCalendarEvents(account.as_ptr(), from.as_ptr(), to.as_ptr(), cb, data)
        });
        assert!(ok, "{payload}");
        serde_json::from_str(&payload).unwrap()
    }

    fn rsvp(&self, event: &CalendarEventRecord) -> (bool, String) {
        let id = CString::new(event.id.as_str()).unwrap();
        let status = CString::new("accepted").unwrap();
        call(|cb, data| MahoMailUpdateRsvp(id.as_ptr(), status.as_ptr(), cb, data))
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        crate::state::clear_ctx_for_test();
    }
}

#[test]
fn cc09_utc_import_is_visible_in_iso_date_range() {
    let f = Fixture::new();
    let event = f.import("DTSTART:20260905T100000Z\r\nDTEND:20260905T110000Z");
    assert_eq!(f.list("2026-09-01", "2026-09-30").len(), 1);
    assert_eq!(event.dtstart, "2026-09-05T10:00:00Z");
    assert_eq!(event.dtend.as_deref(), Some("2026-09-05T11:00:00Z"));
    assert!(!event.all_day);
}

#[test]
fn cc09_tzid_import_preserves_wall_time_and_zone() {
    let f = Fixture::new();
    let event = f.import("DTSTART;TZID=America/New_York:20260905T100000\r\nDTEND;TZID=America/New_York:20260905T110000");
    assert_eq!(event.start_tz.as_deref(), Some("America/New_York"));
    assert_eq!(event.end_tz.as_deref(), Some("America/New_York"));
    assert_eq!(event.dtstart, "2026-09-05T10:00:00");
    assert_eq!(event.dtend.as_deref(), Some("2026-09-05T11:00:00"));
    assert_eq!(f.list("2026-09-01", "2026-09-30").len(), 1);
    assert!(!event.all_day);
}

#[test]
fn cc09_all_day_import_preserves_exclusive_end_date() {
    let f = Fixture::new();
    let event = f.import("DTSTART;VALUE=DATE:20260905\r\nDTEND;VALUE=DATE:20260907");
    assert_eq!(event.dtstart, "2026-09-05");
    assert_eq!(event.dtend.as_deref(), Some("2026-09-07"));
    assert!(event.all_day);
    assert_eq!(f.list("2026-09-01", "2026-09-30").len(), 1);
}

#[test]
fn cc09_floating_time_stays_floating() {
    let f = Fixture::new();
    let event = f.import("DTSTART:20260905T100000");
    assert_eq!(event.dtstart, "2026-09-05T10:00:00");
    assert_eq!(event.start_tz, None);
    assert!(!event.all_day);
}

#[test]
fn cc09_date_only_upper_bound_includes_timed_events_on_that_day() {
    let f = Fixture::new();
    let event = f.import("DTSTART:20260930T100000Z");
    // Isolate query semantics from the separate normalization regression.
    f.ctx
        .pool
        .get()
        .unwrap()
        .execute(
            "UPDATE calendar_events SET dtstart = '2026-09-30T10:00:00Z' WHERE id = ?1",
            [&event.id],
        )
        .unwrap();
    assert_eq!(f.list("2026-09-30", "2026-09-30").len(), 1);
    assert!(f.list("2026-10-01", "2026-10-01").is_empty());
}

#[test]
fn cc10_missing_credentials_reports_delivery_failure() {
    let f = Fixture::new();
    let event = f.import("DTSTART:20260905T100000Z");
    let (ok, payload) = f.rsvp(&event);
    assert!(!ok, "undelivered RSVP reported success: {payload}");
}

#[test]
fn cc10_missing_credentials_retains_durable_rsvp_intent() {
    let f = Fixture::new();
    let event = f.import("DTSTART:20260905T100000Z");
    let _result = f.rsvp(&event);
    let db = f.ctx.pool.get().unwrap();
    let pending = maho_core::services::offline_queue::list_pending_mutations(&db, "acc1").unwrap();
    assert_eq!(pending.len(), 1, "delivery failure lost recovery intent");
    assert_eq!(
        pending[0].calendar_event_id.as_deref(),
        Some(event.id.as_str())
    );
    assert_eq!(pending[0].mutation_type, "calendar_rsvp_reply");
    let payload: serde_json::Value =
        serde_json::from_str(pending[0].payload_json.as_deref().unwrap()).unwrap();
    assert_eq!(payload["rsvp_status"], "accepted");
    assert_eq!(payload["organizer_email"], "organizer@example.invalid");
    assert_eq!(
        db.query_row(
            "SELECT rsvp_status FROM calendar_events WHERE id = ?1",
            [&event.id],
            |row| row.get::<_, String>(0)
        )
        .unwrap(),
        "accepted"
    );
}

#[test]
fn cc09_rsvp_retains_tzid_in_durable_wire_payload() {
    let f = Fixture::new();
    let event = f.import("DTSTART;TZID=America/New_York:20260905T100000\r\nDTEND;TZID=America/New_York:20260905T110000");
    let (ok, _) = f.rsvp(&event);
    assert!(!ok);
    let pending = maho_core::services::offline_queue::list_pending_mutations(
        &f.ctx.pool.get().unwrap(),
        "acc1",
    )
    .unwrap();
    let payload: serde_json::Value =
        serde_json::from_str(pending[0].payload_json.as_deref().unwrap()).unwrap();
    let wire = payload["ics_text"].as_str().unwrap();
    assert!(wire
        .lines()
        .any(|line| line == "DTSTART;TZID=America/New_York:20260905T100000"));
    assert!(wire
        .lines()
        .any(|line| line == "DTEND;TZID=America/New_York:20260905T110000"));
}

#[test]
fn cc09_rsvp_retains_all_day_in_durable_wire_payload() {
    let f = Fixture::new();
    let event = f.import("DTSTART;VALUE=DATE:20260905\r\nDTEND;VALUE=DATE:20260907");
    let (ok, _) = f.rsvp(&event);
    assert!(!ok);
    let pending = maho_core::services::offline_queue::list_pending_mutations(
        &f.ctx.pool.get().unwrap(),
        "acc1",
    )
    .unwrap();
    let payload: serde_json::Value =
        serde_json::from_str(pending[0].payload_json.as_deref().unwrap()).unwrap();
    let wire = payload["ics_text"].as_str().unwrap();
    assert!(wire
        .lines()
        .any(|line| line == "DTSTART;VALUE=DATE:20260905"));
    assert!(wire.lines().any(|line| line == "DTEND;VALUE=DATE:20260907"));
}

#[test]
fn cc02_flush_cannot_report_success_for_undeliverable_calendar_reply() {
    let f = Fixture::new();
    let event = f.import("DTSTART:20260905T100000Z");
    let payload = serde_json::json!({"rsvp_status":"accepted", "organizer_email":"organizer@example.invalid", "attendee_email":"user@example.com", "subject":"Accepted: Review", "ics_text":"BEGIN:VCALENDAR\r\nMETHOD:REPLY\r\nEND:VCALENDAR\r\n"}).to_string();
    maho_core::services::offline_queue::queue_calendar_mutation(
        &f.ctx.pool.get().unwrap(),
        "acc1",
        "calendar_rsvp_reply",
        &event.id,
        Some(&payload),
    )
    .unwrap();
    let account = CString::new("acc1").unwrap();
    let (ok, payload) = call(|cb, data| {
        crate::ffi::write_api::MahoMailFlushPendingMutations(account.as_ptr(), cb, data)
    });
    assert!(
        !ok,
        "flush hid an undeliverable pending calendar reply: {payload}"
    );
    assert_eq!(
        maho_core::services::offline_queue::list_pending_mutations(
            &f.ctx.pool.get().unwrap(),
            "acc1"
        )
        .unwrap()
        .len(),
        1
    );
}
