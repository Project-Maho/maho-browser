use chrono::{NaiveDate, NaiveDateTime};
use rusqlite::{params, Connection};

use crate::error::AppError;

pub(super) fn migrate_v55(conn: &Connection) -> Result<(), AppError> {
    let mut statement = conn.prepare(
        "SELECT id, dtstart, dtend FROM calendar_events
         WHERE length(dtstart) IN (8, 15, 16) OR length(dtend) IN (8, 15, 16)",
    )?;
    let events = statement.query_map([], |row| {
        Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?, row.get::<_, Option<String>>(2)?))
    })?.collect::<rusqlite::Result<Vec<_>>>()?;
    for (id, start, end) in events {
        let normalized_start = normalize_compact_date(&start);
        let normalized_end = end.as_deref().and_then(normalize_compact_date);
        if normalized_start.is_some() || normalized_end.is_some() {
            conn.execute(
                "UPDATE calendar_events SET dtstart=?1, dtend=?2 WHERE id=?3",
                params![normalized_start.as_deref().unwrap_or(&start),
                    normalized_end.as_deref().or(end.as_deref()), id],
            )?;
        }
    }
    conn.execute("INSERT INTO schema_version (version) VALUES (55)", [])?;
    Ok(())
}

fn normalize_compact_date(value: &str) -> Option<String> {
    if value.len() == 8 && value.bytes().all(|byte| byte.is_ascii_digit()) {
        return NaiveDate::parse_from_str(value, "%Y%m%d")
            .ok().map(|date| date.format("%Y-%m-%d").to_string());
    }
    let (local, suffix) = match value.strip_suffix('Z') {
        Some(local) => (local, "Z"),
        None => (value, ""),
    };
    if local.len() != 15 || !local.bytes().enumerate().all(|(index, byte)| {
        if index == 8 { byte == b'T' } else { byte.is_ascii_digit() }
    }) {
        return None;
    }
    NaiveDateTime::parse_from_str(local, "%Y%m%dT%H%M%S")
        .ok().map(|date| format!("{}{suffix}", date.format("%Y-%m-%dT%H:%M:%S")))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn migration_preserves_noncompact_and_invalid_data() {
        let conn = Connection::open_in_memory().unwrap();
        conn.execute_batch(
            "CREATE TABLE schema_version(version INTEGER PRIMARY KEY);
             CREATE TABLE calendar_events(id TEXT PRIMARY KEY, dtstart TEXT, dtend TEXT);
             INSERT INTO calendar_events VALUES
             ('iso', '2026-09-05T10:00:00Z', '2026-09-05T11:00:00Z'),
             ('invalid', '20260230', '20260905T250000Z'),
             ('end-only', '2026-09-05T10:00:00', '20260905T110000');"
        ).unwrap();

        migrate_v55(&conn).unwrap();

        let rows: Vec<(String, String, String)> = conn.prepare(
            "SELECT id, dtstart, dtend FROM calendar_events ORDER BY id"
        ).unwrap().query_map([], |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)))
            .unwrap().collect::<rusqlite::Result<_>>().unwrap();
        assert_eq!(rows, [
            ("end-only".into(), "2026-09-05T10:00:00".into(), "2026-09-05T11:00:00".into()),
            ("invalid".into(), "20260230".into(), "20260905T250000Z".into()),
            ("iso".into(), "2026-09-05T10:00:00Z".into(), "2026-09-05T11:00:00Z".into()),
        ]);
    }
}
