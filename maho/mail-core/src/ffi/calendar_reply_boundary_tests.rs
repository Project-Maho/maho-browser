use super::super::{build_ics_reply, IcsReplyParams};

fn params() -> IcsReplyParams {
    IcsReplyParams {
        uid: "event@example.invalid".into(), sequence: "3".into(),
        dtstart: "2026-09-05T10:00:00Z".into(), dtend: None,
        start_tz: None, end_tz: None, summary: "Planning".into(),
        organizer_line: "mailto:organizer@example.invalid".into(),
        attendee_email: "attendee@example.invalid".into(), attendee_name: None,
        partstat: "ACCEPTED".into(),
    }
}

#[test]
fn rejects_newlines_in_every_raw_reply_property() {
    let injected = "value\r\nX-UNTRUSTED:forged";
    for field in ["uid", "sequence", "dtstart", "dtend", "organizer", "attendee", "name", "partstat"] {
        let mut input = params();
        match field {
            "uid" => input.uid = injected.into(),
            "sequence" => input.sequence = injected.into(),
            "dtstart" => input.dtstart = injected.into(),
            "dtend" => input.dtend = Some(injected.into()),
            "organizer" => input.organizer_line = injected.into(),
            "attendee" => input.attendee_email = injected.into(),
            "name" => input.attendee_name = Some(injected.into()),
            "partstat" => input.partstat = injected.into(),
            _ => unreachable!(),
        }
        assert!(build_ics_reply(input).is_err(), "field {field}");
    }
}

#[test]
fn valid_reply_preserves_multiline_summary_as_text() {
    let mut input = params();
    input.summary = "Planning\r\nX-UNTRUSTED:text".into();
    input.attendee_name = Some("Meeting Guest".into());
    let wire = build_ics_reply(input).unwrap();
    assert_eq!(wire.lines().filter(|line| line.starts_with("UID:")).count(), 1);
    assert!(wire.lines().any(|line| line == "UID:event@example.invalid"));
    assert!(wire.lines().any(|line| line == r"SUMMARY:Planning\nX-UNTRUSTED:text"));
    assert!(!wire.lines().any(|line| line.starts_with("X-UNTRUSTED:")));
}
