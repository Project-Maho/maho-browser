use super::super::{build_ics_reply, IcsReplyParams, MahoMailGenerateRsvpReply};
use super::{call, Fixture};
use std::ffi::CString;

fn generated_reply_dates(dates: &str, expected: &[&str]) {
    let fixture = Fixture::new();
    let event = fixture.import(dates);
    let id = CString::new(event.id).unwrap();
    let status = CString::new("accepted").unwrap();
    let account_email = CString::new("user@example.com").unwrap();

    let (ok, payload) = call(|callback, data| MahoMailGenerateRsvpReply(
        id.as_ptr(), status.as_ptr(), account_email.as_ptr(), callback, data,
    ));

    assert!(ok, "{payload}");
    let wire: String = serde_json::from_str(&payload).unwrap();
    let actual: Vec<_> = wire.lines().filter(|line| {
        line.starts_with("DTSTART") || line.starts_with("DTEND")
    }).collect();
    assert_eq!(actual, expected);
}

#[test]
fn mail_fix_export_utc_omits_timezone() {
    let fixture = Fixture::new();
    let event = fixture.import("DTSTART:20260905T100000Z\r\nDTEND:20260905T110000Z");
    fixture.ctx.pool.get().unwrap().execute(
        "UPDATE calendar_events SET start_tz='America/New_York', end_tz='America/New_York' WHERE id=?1", [&event.id],
    ).unwrap();
    let request = CString::new(r#"{"account_id":"acc1"}"#).unwrap();
    let (ok, wire) = call(|callback, data| super::super::MahoMailExportCalendarIcs(request.as_ptr(), callback, data));
    assert!(ok, "{wire}");
    assert!(wire.contains("DTSTART:20260905T100000Z"), "{wire}");
    assert!(wire.contains("DTEND:20260905T110000Z"), "{wire}");
    assert!(!wire.contains("TZID"), "{wire}");
}

#[test]
fn cc09_generated_reply_retains_timezone_dates() {
    generated_reply_dates(
        "DTSTART;TZID=America/New_York:20260905T100000\r\nDTEND;TZID=America/New_York:20260905T110000",
        &["DTSTART;TZID=America/New_York:20260905T100000", "DTEND;TZID=America/New_York:20260905T110000"],
    );
}

#[test]
fn cc09_generated_reply_retains_exclusive_date_end() {
    generated_reply_dates(
        "DTSTART;VALUE=DATE:20260905\r\nDTEND;VALUE=DATE:20260907",
        &["DTSTART;VALUE=DATE:20260905", "DTEND;VALUE=DATE:20260907"],
    );
}

#[test]
fn cc09_generated_reply_retains_utc_dates() {
    generated_reply_dates(
        "DTSTART:20260905T100000Z\r\nDTEND:20260905T110000Z",
        &["DTSTART:20260905T100000Z", "DTEND:20260905T110000Z"],
    );
}

#[test]
fn cc09_generated_reply_retains_floating_dates() {
    generated_reply_dates("DTSTART:20260905T100000", &["DTSTART:20260905T100000"]);
}

fn timezone_reply(zone: &str, value: &str) -> Result<String, String> {
    build_ics_reply(IcsReplyParams {
        uid: "date-boundary".into(), sequence: "0".into(),
        dtstart: value.into(), dtend: None, start_tz: Some(zone.into()), end_tz: None,
        summary: "Date boundary".into(), organizer_line: "mailto:organizer@example.invalid".into(),
        attendee_email: "attendee@example.invalid".into(), attendee_name: None, partstat: "ACCEPTED".into(),
    })
}

#[test]
fn timezone_boundary_rejects_property_injection() {
    assert!(timezone_reply("America/New_York\r\nX-INJECTED:value", "20260905T100000").is_err());
}

#[test]
fn timezone_boundary_retains_valid_quoted_parameter() {
    let wire = timezone_reply(r#""America/New_York""#, "20260905T100000").unwrap();
    assert!(wire.lines().any(|line| line == r#"DTSTART;TZID="America/New_York":20260905T100000"#));
}

#[test]
fn timezone_boundary_keeps_utc_without_parameter() {
    let wire = timezone_reply("America/New_York", "20260905T100000Z").unwrap();
    assert!(wire.lines().any(|line| line == "DTSTART:20260905T100000Z"));
}

#[test]
fn timezone_boundary_preserves_quoted_delimiters_on_import() {
    generated_reply_dates(
        r#"DTSTART;TZID="Custom;Zone:One":20260905T100000"#,
        &[r#"DTSTART;TZID="Custom;Zone:One":20260905T100000"#],
    );
}

#[test]
fn timezone_boundary_quotes_raw_comma_parameter() {
    let wire = timezone_reply("Custom,Zone", "20260905T100000").unwrap();
    assert!(wire.lines().any(|line| line == r#"DTSTART;TZID="Custom,Zone":20260905T100000"#));
}

#[test]
fn timezone_boundary_rejects_partial_quote() {
    assert!(timezone_reply(r#""Custom"#, "20260905T100000").is_err());
}
