use super::run_migrations;
use crate::services::offline_queue::queue_mutation;
use rusqlite::Connection;

struct TestDatabase(std::path::PathBuf);

impl Drop for TestDatabase {
    fn drop(&mut self) {
        if let Err(error) = std::fs::remove_file(&self.0) {
            if error.kind() != std::io::ErrorKind::NotFound {
                eprintln!("queue test database cleanup failed: {error}");
            }
        }
    }
}

#[test]
fn queue_identity_upgrade_preserves_mail_and_calendar() {
    let database = TestDatabase(std::env::temp_dir().join(
        format!("maho-queue-upgrade-{}.sqlite", uuid::Uuid::new_v4()),
    ));
    let path = &database.0;
    let conn = Connection::open(&path).unwrap();
    conn.execute_batch(
        "CREATE TABLE schema_version(version INTEGER PRIMARY KEY);
         INSERT INTO schema_version VALUES(53);
         CREATE TABLE calendar_events(id TEXT PRIMARY KEY, dtstart TEXT NOT NULL, dtend TEXT);
         CREATE TABLE folders(id TEXT PRIMARY KEY, account_id TEXT NOT NULL, path TEXT NOT NULL,
             uid_validity INTEGER NOT NULL DEFAULT 0);
         INSERT INTO folders VALUES('sent','acc1','Sent',1234);
         CREATE TABLE pending_mutations (
           id TEXT PRIMARY KEY, account_id TEXT NOT NULL, email_uid INTEGER,
           folder_path TEXT, mutation_type TEXT NOT NULL CHECK(mutation_type IN (
             'mark_read','mark_unread','star','unstar','move','delete',
             'calendar_rsvp','calendar_rsvp_reply','calendar_create',
             'calendar_update','calendar_delete')),
           target_folder TEXT, created_at TEXT NOT NULL DEFAULT(datetime('now')),
           calendar_event_id TEXT, payload_json TEXT,
           UNIQUE(account_id,email_uid,mutation_type));
         CREATE INDEX idx_pending_mutations_account ON pending_mutations(account_id);
         CREATE INDEX idx_pending_mutations_cal_event ON pending_mutations(calendar_event_id)
           WHERE calendar_event_id IS NOT NULL;
         CREATE UNIQUE INDEX uniq_pending_mutations_cal
           ON pending_mutations(account_id,calendar_event_id,mutation_type)
           WHERE calendar_event_id IS NOT NULL;
         INSERT INTO pending_mutations VALUES
           ('mail-row','acc1',7,'INBOX','move','Archive','2026-01-01',NULL,'mail-payload'),
           ('calendar-row','acc1',NULL,NULL,'calendar_rsvp_reply',NULL,'2026-01-02','event1','calendar-payload');"
    ).unwrap();

    run_migrations(&conn).unwrap();
    run_migrations(&conn).unwrap();
    drop(conn);
    let conn = Connection::open(&path).unwrap();
    let rows: Vec<(String, String, String)> = conn.prepare(
        "SELECT id,created_at,payload_json FROM pending_mutations ORDER BY id"
    ).unwrap().query_map([], |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)))
        .unwrap().collect::<rusqlite::Result<_>>().unwrap();
    assert_eq!(rows, [
        ("calendar-row".into(), "2026-01-02".into(), "calendar-payload".into()),
        ("mail-row".into(), "2026-01-01".into(), "mail-payload".into()),
    ]);

    queue_mutation(&conn, "acc1", 7, "Sent", "move", Some("Trash")).unwrap();
    queue_mutation(&conn, "acc1", 7, "Sent", "move", Some("Archive")).unwrap();
    let folders: Vec<String> = conn.prepare(
        "SELECT folder_path FROM pending_mutations WHERE email_uid=7 ORDER BY folder_path"
    ).unwrap().query_map([], |row| row.get(0)).unwrap()
        .collect::<rusqlite::Result<_>>().unwrap();
    assert_eq!(folders, ["INBOX", "Sent"]);
    assert!(conn.execute(
        "INSERT INTO pending_mutations(id,account_id,mutation_type,calendar_event_id)
         VALUES('duplicate','acc1','calendar_rsvp_reply','event1')", []
    ).is_err());
    assert!(conn.execute(
        "INSERT INTO pending_mutations(id,account_id,mutation_type)
         VALUES('invalid','acc1','not-a-mutation')", []
    ).is_err());
}
