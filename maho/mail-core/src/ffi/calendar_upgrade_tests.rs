use super::Fixture;

fn upgrade_dates(start: &str, end: Option<&str>, expected_start: &str, expected_end: Option<&str>) {
    let fixture = Fixture::new();
    let event = fixture.import("DTSTART;TZID=America/New_York:20260905T100000");
    {
        let db = fixture.ctx.pool.get().unwrap();
        db.execute(
            "UPDATE calendar_events SET dtstart=?1, dtend=?2, all_day=?3 WHERE id=?4",
            rusqlite::params![start, end, start.len() == 8, event.id],
        ).unwrap();
        db.execute("ALTER TABLE folders DROP COLUMN reconciliation_uid", []).unwrap();
        db.execute("ALTER TABLE folders DROP COLUMN reconciliation_version", []).unwrap();
        db.execute("DELETE FROM schema_version WHERE version > 54", []).unwrap();

        maho_core::db::migrations::run_migrations(&db).unwrap();
        maho_core::db::migrations::run_migrations(&db).unwrap();
    }

    let events = fixture.list("2026-09-01", "2026-09-30");
    assert_eq!(events.len(), 1);
    assert_eq!(events[0].id, event.id);
    assert_eq!(events[0].dtstart, expected_start);
    assert_eq!(events[0].dtend.as_deref(), expected_end);
    assert_eq!(events[0].all_day, start.len() == 8);
    assert_eq!(events[0].start_tz, event.start_tz);
}

#[test]
fn cc09_upgrade_restores_utc_date_visibility() {
    upgrade_dates("20260905T100000Z", Some("20260905T110000Z"),
        "2026-09-05T10:00:00Z", Some("2026-09-05T11:00:00Z"));
}

#[test]
fn cc09_upgrade_restores_floating_date_visibility() {
    upgrade_dates("20260905T100000", None, "2026-09-05T10:00:00", None);
}

#[test]
fn cc09_upgrade_restores_exclusive_date_end() {
    upgrade_dates("20260905", Some("20260907"), "2026-09-05", Some("2026-09-07"));
}
