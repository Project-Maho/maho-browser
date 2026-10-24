use maho_storage::sqlite_kv::SqliteKvStorage;

#[test]
fn sqlite_kv_round_trips_like_lmdb() {
    let dir = tempfile::tempdir().unwrap();
    let db = SqliteKvStorage::open(dir.path()).unwrap();
    assert!(db.get("tabs").unwrap().is_none());
    db.put("tabs", b"first").unwrap();
    db.put("tabs", b"second").unwrap();
    assert_eq!(db.get("tabs").unwrap().unwrap(), b"second");
    assert!(db.delete("tabs").unwrap());
    assert!(!db.delete("tabs").unwrap());
}

#[test]
fn sqlite_kv_batch_and_snapshot_are_key_sorted() {
    let dir = tempfile::tempdir().unwrap();
    let db = SqliteKvStorage::open(dir.path()).unwrap();
    db.put_batch(&[("spaces", b"s"), ("account", b"a"), ("tabs", b"t")]).unwrap();
    let keys: Vec<Vec<u8>> = db.snapshot_entries().unwrap().into_iter().map(|(k, _)| k).collect();
    assert_eq!(keys, vec![b"account".to_vec(), b"spaces".to_vec(), b"tabs".to_vec()]);
}

#[test]
fn sqlite_kv_persists_across_reopen() {
    let dir = tempfile::tempdir().unwrap();
    SqliteKvStorage::open(dir.path()).unwrap().save_preview("t1", b"png").unwrap();
    let reopened = SqliteKvStorage::open(dir.path()).unwrap();
    assert_eq!(reopened.load_preview("t1").unwrap().unwrap(), b"png");
}
