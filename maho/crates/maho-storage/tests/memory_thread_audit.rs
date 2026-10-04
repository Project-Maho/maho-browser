//! Mutation-sensitive memory and thread regression tests for Vault audit storage (U09).
//!
//! Validates bounded row materialization for audit tail queries, keyset pagination
//! index coverage, and serialized transaction ordering during concurrent appends.

use std::sync::{Arc, Barrier};
use std::thread;

use maho_storage::sqlite::{SqliteStorage, VaultAuditRow};

fn test_key() -> String {
    "memory-thread-vault-audit-storage-key".to_string()
}

fn sample_row(seq: i64, prev_hash: Option<Vec<u8>>, entry_hash: Vec<u8>) -> VaultAuditRow {
    VaultAuditRow {
        id: format!("audit-record-{seq}"),
        schema_version: 1,
        seq,
        timestamp: "2026-09-06T12:00:00Z".to_string(),
        session_id: None,
        task_id: None,
        profile_id: "test-profile".to_string(),
        workspace_id: "test-workspace".to_string(),
        top_origin: None,
        frame_origin: None,
        item_id: None,
        item_alias: None,
        operation: "fill".to_string(),
        policy: None,
        decision: "allowed".to_string(),
        reason: None,
        device_name: "test-device".to_string(),
        prev_hash,
        entry_hash,
    }
}

/// U09 storage regression: finding the audit tail must materialize at most 1 row,
/// avoiding quadratic O(N) memory allocation and table scanning on append.
#[test]
fn tail_materializes_one_row() {
    // Given: an isolated storage database seeded with 100 chained audit rows
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("tail_bench.sqlite");
    let path_str = path.to_str().expect("valid utf8");
    let storage = SqliteStorage::open_with_key(path_str, &test_key()).expect("open storage");

    let mut prev: Option<Vec<u8>> = None;
    for seq in 1..=100 {
        let entry = vec![u8::try_from(seq % 251).unwrap_or(1); 32];
        let row = sample_row(seq, prev, entry.clone());
        storage.append_vault_audit(&row).expect("append audit row");
        prev = Some(entry);
    }

    // When: querying the audit tail with materialization tracking enabled
    SqliteStorage::start_audit_materialization_tracking();
    let tail = storage.vault_audit_tail().expect("fetch audit tail");
    let materialized = SqliteStorage::audit_rows_materialized();
    SqliteStorage::stop_audit_materialization_tracking();

    // Then: the tail returns the last committed sequence and hash
    let (tail_seq, tail_hash) = tail.expect("tail must exist");
    assert_eq!(tail_seq, 100, "tail sequence must match last committed row");
    assert_eq!(
        tail_hash,
        prev.expect("last entry hash"),
        "tail hash must match last committed entry_hash"
    );

    // Behavioral assertion: finding the tail must not materialize full history
    assert!(
        materialized <= 1,
        "tail query must materialize at most 1 row, but materialized {materialized}"
    );
}

/// U09 storage regression: keyset page query must use idx_vault_audit_seq index
/// and limit + 1 bounded row materialization rather than table-scanning history.
#[test]
fn page_uses_seq_index_and_limit_plus_one() {
    // Given: an isolated storage database with 200 chained audit rows
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("page_index.sqlite");
    let path_str = path.to_str().expect("valid utf8");
    let storage = SqliteStorage::open_with_key(path_str, &test_key()).expect("open storage");

    let mut prev: Option<Vec<u8>> = None;
    for seq in 1..=200 {
        let entry = vec![u8::try_from(seq % 251).unwrap_or(1); 32];
        let row = sample_row(seq, prev, entry.clone());
        storage.append_vault_audit(&row).expect("append audit row");
        prev = Some(entry);
    }

    // When: inspecting SQLite's query plan for keyset pagination on seq
    let plan = storage
        .explain_query_plan(
            "SELECT id, seq, entry_hash FROM vault_audit_events WHERE seq > 50 ORDER BY seq ASC LIMIT 11",
        )
        .expect("explain query plan");

    // Then: query plan must utilize idx_vault_audit_seq
    assert!(
        plan.contains("USING INDEX idx_vault_audit_seq"),
        "keyset page query must use idx_vault_audit_seq, got: {plan}"
    );

    // When: requesting page with limit 10 after seq 50 with materialization tracking
    SqliteStorage::start_audit_materialization_tracking();
    let (rows, next_cursor) = storage
        .page_vault_audit_events(Some(50), 10)
        .expect("page vault audit events");
    let materialized = SqliteStorage::audit_rows_materialized();
    SqliteStorage::stop_audit_materialization_tracking();

    // Then: exactly 10 entries returned, correctly indexed, with next cursor pointing to seq 60
    assert_eq!(rows.len(), 10, "page must return exactly 10 rows");
    assert_eq!(rows[0].seq, 51, "first page item seq must be 51");
    assert_eq!(rows[9].seq, 60, "last page item seq must be 60");
    assert_eq!(
        next_cursor,
        Some(60),
        "next cursor must point to sequence 60"
    );

    // Keyset pagination must query limit + 1 (11) rows in SQL, not materialize all 200 rows
    assert!(
        materialized <= 11,
        "keyset page with limit 10 must materialize at most 11 rows, but materialized {materialized}"
    );
}

/// U09 storage regression: concurrent appends across connections must serialize
/// sequence allocation and hash chaining, preventing sequence collision or forks.
#[test]
fn concurrent_append_keeps_single_chain() {
    // Given: an initialized shared database file
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("concurrent.sqlite");
    let path_str = path.to_str().expect("valid utf8").to_string();

    {
        let _init = SqliteStorage::open_with_key(&path_str, &test_key()).expect("init db");
    }

    // When: multiple concurrent threads append audit events
    let thread_count = 4;
    let ops_per_thread = 10;
    let barrier = Arc::new(Barrier::new(thread_count + 1));
    let mut handles = Vec::new();

    for t in 0..thread_count {
        let p = path_str.clone();
        let b = barrier.clone();
        handles.push(thread::spawn(move || {
            let storage = SqliteStorage::open_with_key(&p, &test_key()).expect("thread store");
            // Wait for simultaneous start signal
            b.wait();
            for i in 0..ops_per_thread {
                // Select and construct the final event inside the writer reservation.
                storage
                    .vault_audit_transaction(|tx| {
                        let tail = tx.audit_tail()?;
                        let (seq, prev_hash) = match tail {
                            Some((s, h)) => (s.checked_add(1).expect("seq fits"), Some(h)),
                            None => (1, None),
                        };
                        let entry_hash = {
                            let mut b = vec![0u8; 32];
                            b[0] = u8::try_from(t).unwrap_or(0);
                            b[1] = u8::try_from(i).unwrap_or(0);
                            b
                        };
                        let row = sample_row(seq, prev_hash, entry_hash);
                        tx.append_audit(&row)
                    })
                    .expect("append final audit row unchanged");
            }
        }));
    }

    // Release all threads
    barrier.wait();
    for h in handles {
        h.join().expect("thread join");
    }

    // Then: verify that the resulting audit chain is strictly monotonic with no gaps or duplicate seq
    let verify = SqliteStorage::open_with_key(&path_str, &test_key()).expect("open verify");
    let events = verify.list_vault_audit_events().expect("list all events");

    let expected_total = thread_count * ops_per_thread;
    assert_eq!(
        events.len(),
        expected_total,
        "concurrent appends must preserve all events without silent loss; expected {expected_total}, got {}",
        events.len()
    );

    for (idx, row) in events.iter().enumerate() {
        let expected_seq = i64::try_from(idx + 1).expect("fits i64");
        assert_eq!(
            row.seq, expected_seq,
            "row at index {idx} has sequence {}, expected {expected_seq} (collision or gap detected)",
            row.seq
        );
        if idx > 0 {
            assert_eq!(
                row.prev_hash.as_ref(),
                Some(&events[idx - 1].entry_hash),
                "hash chain broken at index {idx}, seq {}",
                row.seq
            );
        }
    }
}
