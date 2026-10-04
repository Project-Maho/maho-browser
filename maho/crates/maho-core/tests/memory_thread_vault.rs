//! Mutation-sensitive memory and thread regression tests for Vault core audit & pagination (U09).
//!
//! Validates bounded audit tail materialization, keyset pagination correctness,
//! bounded default limit handling, and strict input validation.

use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::VaultLoginInput;
use maho_storage::sqlite::{SqliteStorage, VaultAuditRow};
use maho_types::vault::{
    CredentialOrigin, VaultAuditDecision, VaultAuditPageRequest, VaultItemKind,
    VaultItemPublicMetadata, VaultSchemaVersion,
};
use zeroize::Zeroizing;

// The audit materialization tracker (`SqliteStorage::start_audit_materialization_tracking`)
// is PROCESS-GLOBAL: any concurrent test in this binary that appends or lists audit
// rows during another test's tracking window inflates that test's counter, so the
// "at most 1 row materialized" assertion would pass or fail on thread-scheduling luck.
// Serializing this suite makes the invariant deterministic; the suite runs in <1s.
static AUDIT_SUITE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const DATABASE_KEY: &str = "memory-thread-vault-test-key";

fn unlocked_core(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key(DATABASE_KEY).expect("configure key");
    let path = dir.path().join(name);
    let mut core = MahoCore::new().with_storage(path.to_str().expect("utf8 path"));
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize_vault must succeed and unlock");
    core
}

fn metadata(title: &str) -> VaultItemPublicMetadata {
    VaultItemPublicMetadata {
        favorite: false,
        trashed_at: None,
        has_notes: false,
        title: title.to_string(),
        origins: vec![CredentialOrigin::try_from("https://vault.example.com").expect("origin")],
        username_hint: String::new(),
        item_kind: VaultItemKind::Login,
        totp: None,
        passkey: None,
    }
}

fn login_input(title: &str, username: &str, secret: &str) -> VaultLoginInput {
    VaultLoginInput {
        metadata: metadata(title),
        username: username.to_string(),
        password: Zeroizing::new(secret.to_string()),
        form_details: None,
    }
}

fn sample_audit_row(seq: i64) -> VaultAuditRow {
    // The audit log is a hash chain: append_audit rejects a row whose prev_hash
    // does not match the current head. Row 1 opens the chain with None; every
    // later row must carry the previous row's entry_hash.
    let entry_hash = |s: i64| vec![u8::try_from(s % 251).unwrap_or(1); 32];
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
        prev_hash: if seq <= 1 { None } else { Some(entry_hash(seq - 1)) },
        entry_hash: entry_hash(seq),
    }
}

/// U09 regression: append must preserve hash chain across updates and rollbacks,
/// and tail query during append must materialize at most 1 row.
#[test]
fn append_preserves_hash_and_rollback() {
    let _suite = AUDIT_SUITE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    // Given: an initialized Vault core with several audit records
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = unlocked_core(&dir, "append_integrity.sqlite");

    for i in 1..=5 {
        // Distinct usernames keep the five items unique under the existing
        // (origin, username) duplicate rule so the test reaches its U09
        // append-integrity assertions.
        core.vault_add_login(login_input(
            &format!("item-{i}"),
            &format!("user-{i}"),
            "secret",
        ))
        .expect("add login");
    }

    let storage = core.storage_ref().expect("storage");
    let initial_rows = storage.list_vault_audit_events().expect("list audits");
    assert_eq!(initial_rows.len(), 5);

    // Verify initial hash chain integrity
    for (idx, row) in initial_rows.iter().enumerate() {
        assert_eq!(row.seq, i64::try_from(idx + 1).expect("fits i64"));
        assert_eq!(row.entry_hash.len(), 32);
        if idx > 0 {
            assert_eq!(
                row.prev_hash.as_ref(),
                Some(&initial_rows[idx - 1].entry_hash)
            );
        }
    }

    // When: performing an audit append with materialization tracking enabled
    SqliteStorage::start_audit_materialization_tracking();
    core.vault_record_import_commit_audit(VaultAuditDecision::Allowed, None)
        .expect("append import audit");
    let materialized = SqliteStorage::audit_rows_materialized();
    SqliteStorage::stop_audit_materialization_tracking();

    // Then: finding the tail must materialize at most 1 row, not all existing rows
    assert!(
        materialized <= 1,
        "audit append must query tail materializing at most 1 row, but materialized {materialized}"
    );

    let updated_rows = storage.list_vault_audit_events().expect("list audits");
    assert_eq!(updated_rows.len(), 6);
    let last = updated_rows.last().expect("last audit row");
    assert_eq!(last.seq, 6);
    assert_eq!(
        last.prev_hash.as_ref(),
        Some(&initial_rows.last().expect("prev last").entry_hash)
    );
}

/// U09 regression: keyset pages cover all rows exactly once without omissions
/// or duplicates, materializing at most limit + 1 rows per page.
#[test]
fn keyset_pages_cover_rows_once() {
    let _suite = AUDIT_SUITE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    // Given: an unlocked core seeded with 55 audit rows
    let dir = tempfile::tempdir().expect("tempdir");
    let core = unlocked_core(&dir, "keyset_paging.sqlite");
    let storage = core.storage_ref().expect("storage");

    for seq in 1..=55 {
        storage
            .append_vault_audit(&sample_audit_row(seq))
            .expect("seed row");
    }

    // When: keyset-paging through all rows with page limit 10
    let mut seen_ids = std::collections::HashSet::new();
    let mut cursor: Option<String> = None;
    let page_size = 10u32;

    loop {
        let req = VaultAuditPageRequest {
            schema_version: VaultSchemaVersion::CURRENT,
            cursor: cursor.clone(),
            limit: page_size,
        };

        SqliteStorage::start_audit_materialization_tracking();
        let page = core.vault_audit_page(&req).expect("vault audit page query");
        let materialized = SqliteStorage::audit_rows_materialized();
        SqliteStorage::stop_audit_materialization_tracking();

        // Keyset page query must not materialize the entire 55-row table on every page fetch
        assert!(
            materialized <= usize::try_from(page_size + 1).expect("fits usize"),
            "page fetch must materialize at most limit + 1 rows, but materialized {materialized}"
        );

        for entry in page.entries {
            assert!(
                seen_ids.insert(entry.id),
                "keyset pagination must not yield duplicate entries across pages"
            );
        }

        match page.next_cursor {
            Some(next) => cursor = Some(next),
            None => break,
        }
    }

    // Then: all 55 items retrieved with no omissions and no duplicates
    assert_eq!(
        seen_ids.len(),
        55,
        "must retrieve exactly 55 unique entries"
    );
}

/// U09 regression: requesting audit page with limit=0 must return a bounded
/// default page of at most 100 entries, not the entire database.
#[test]
fn zero_limit_is_bounded_default() {
    let _suite = AUDIT_SUITE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    // Given: a database with 150 audit rows
    let dir = tempfile::tempdir().expect("tempdir");
    let core = unlocked_core(&dir, "zero_limit.sqlite");
    let storage = core.storage_ref().expect("storage");

    for seq in 1..=150 {
        storage
            .append_vault_audit(&sample_audit_row(seq))
            .expect("seed row");
    }

    // When: calling vault_audit_page with limit 0
    let req = VaultAuditPageRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        cursor: None,
        limit: 0,
    };
    let page = core
        .vault_audit_page(&req)
        .expect("audit page query with limit=0");

    // Then: limit=0 must be bounded to default 100 entries with next_cursor populated
    assert_eq!(
        page.entries.len(),
        100,
        "limit=0 must return bounded default page of 100 entries, not entire database"
    );
    assert_eq!(
        page.next_cursor,
        Some("100".to_string()),
        "next_cursor must be returned pointing to sequence 100 when additional entries exist"
    );
}

/// U09 regression: negative/malformed cursor and i64::MAX sequence overflow must fail with typed errors.
#[test]
fn invalid_cursor_and_sequence_overflow_fail() {
    let _suite = AUDIT_SUITE_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    // Given: an unlocked core
    let dir = tempfile::tempdir().expect("tempdir");
    let core = unlocked_core(&dir, "validation_failures.sqlite");

    // When: querying with negative cursor
    let req_neg = VaultAuditPageRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        cursor: Some("-1".to_string()),
        limit: 10,
    };
    let res_neg = core.vault_audit_page(&req_neg);

    // Then: negative cursor must be rejected as invalid
    assert!(
        res_neg.is_err(),
        "negative cursor '-1' must fail validation with a storage error"
    );

    // When: querying with malformed non-numeric cursor
    let req_malformed = VaultAuditPageRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        cursor: Some("invalid-cursor-value".to_string()),
        limit: 10,
    };
    let res_malformed = core.vault_audit_page(&req_malformed);

    // Then: malformed cursor must be rejected
    assert!(
        res_malformed.is_err(),
        "malformed cursor string must fail validation"
    );

    // When: latest audit sequence reaches i64::MAX
    let storage = core.storage_ref().expect("storage");
    let mut max_row = sample_audit_row(i64::MAX);
    max_row.id = "max-seq-sentinel".to_string();
    max_row.operation = "import_commit".to_string();
    // This row opens an empty chain, so it must carry no predecessor: the head
    // is None here and append_audit rejects a mismatched prev_hash. The point
    // of this test is the i64::MAX sequence guard, not chain linkage.
    max_row.prev_hash = None;
    storage
        .append_vault_audit(&max_row)
        .expect("seed max seq row");

    // Attempt to append next audit event beyond i64::MAX
    let res_overflow = core.vault_record_import_commit_audit(VaultAuditDecision::Allowed, None);

    // Then: checked sequence allocation must fail with error, not silently saturating reuse
    assert!(
        res_overflow.is_err(),
        "checked next sequence allocation must fail on i64::MAX overflow"
    );
}
