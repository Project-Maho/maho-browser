use maho_core::vault_manager::VaultCrudError;
use maho_core::vault_runtime::session::{VaultBackendSecretAction, VaultBackendSessionError};
use maho_types::vault::{VaultItemId, VaultRevision};

use super::{
    assert_chain, assert_reopened_items, audits, clear_fault, core, fault_connection, items, login,
    reject_audit, SECRET,
};

#[test]
fn browser_session_audit_failure_does_not_release_secret_or_persist_use() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core(&dir);
    let item = core.vault_add_login(login()).expect("create login");
    let session = core
        .create_vault_backend_session()
        .expect("browser backend session");
    let before_items = items(&core);
    let before_audits = audits(&core);
    let connection = fault_connection(&core);
    reject_audit(&connection, true);

    assert!(matches!(
        session.resolve_login_secret(item.id, item.revision, VaultBackendSecretAction::Fill),
        Err(VaultBackendSessionError::Batch(VaultCrudError::Storage(_)))
    ));
    assert_eq!(items(&core), before_items);
    assert_eq!(audits(&core), before_audits);
    assert_eq!(core.vault_status().item_count, 1);
    assert_reopened_items(&core, &before_items);

    clear_fault(&connection);
    let secret = session
        .resolve_login_secret(item.id, item.revision, VaultBackendSecretAction::Fill)
        .expect("browser fill after audit fault removed");
    assert_eq!(secret.as_str(), SECRET);
    assert_eq!(items(&core)[0].revision, 2);
    let rows = audits(&core);
    assert_eq!(rows.len(), before_audits.len() + 1);
    assert_eq!(rows.last().expect("fill audit").operation, "fill");
    assert_chain(&rows);
}

#[test]
fn browser_session_audits_success_stale_missing_and_locked_outcomes() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core(&dir);
    let item = core.vault_add_login(login()).expect("create login");
    let session = core
        .create_vault_backend_session()
        .expect("browser backend session");
    let baseline = audits(&core);

    // This is the production worker-session surface, not the core convenience wrapper.
    let (completed, completion) = std::sync::mpsc::sync_channel(1);
    let worker = std::thread::spawn(move || {
        let result =
            session.resolve_login_secret(item.id, item.revision, VaultBackendSecretAction::Fill);
        assert!(
            completed.send((session, result)).is_ok(),
            "signal worker result"
        );
    });
    let (session, result) = completion
        .recv_timeout(std::time::Duration::from_secs(10))
        .expect("worker result within deadline");
    worker.join().expect("worker completed");
    assert_eq!(result.expect("worker fill").as_str(), SECRET);
    assert!(matches!(
        session.resolve_login_secret(item.id, item.revision, VaultBackendSecretAction::Fill),
        Err(VaultBackendSessionError::Batch(
            VaultCrudError::RevisionConflict {
                expected: 1,
                actual: 2
            }
        ))
    ));
    let missing = VaultItemId::new();
    assert!(matches!(
        session.resolve_login_secret(
            missing,
            VaultRevision::new(1),
            VaultBackendSecretAction::Fill
        ),
        Err(VaultBackendSessionError::Batch(
            VaultCrudError::ItemNotFound
        ))
    ));
    core.lock_vault().expect("lock vault");
    assert!(matches!(
        session.resolve_login_secret(
            item.id,
            VaultRevision::new(2),
            VaultBackendSecretAction::Fill
        ),
        Err(VaultBackendSessionError::Batch(VaultCrudError::Locked))
    ));

    let rows = audits(&core);
    let outcomes = rows[baseline.len()..]
        .iter()
        .map(|row| {
            (
                row.operation.as_str(),
                row.decision.as_str(),
                row.reason.as_deref(),
            )
        })
        .collect::<Vec<_>>();
    assert_eq!(
        outcomes,
        vec![
            ("fill", "allowed", None),
            ("fill", "failed", Some("revision_conflict")),
            ("fill", "failed", Some("not_found")),
            ("fill", "denied", Some("locked")),
        ]
    );
    assert!(rows
        .iter()
        .all(|row| row.profile_id == baseline[0].profile_id));
    assert!(!format!("{rows:?}").contains(&item.id.to_string()));
    assert!(!format!("{rows:?}").contains(&missing.to_string()));
    assert_chain(&rows);
    assert_eq!(
        items(&core)[0].revision,
        2,
        "only successful fill mutates the item"
    );
}

#[test]
fn browser_session_does_not_silently_ignore_failed_denial_audits() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core(&dir);
    let item = core.vault_add_login(login()).expect("create login");
    let session = core
        .create_vault_backend_session()
        .expect("browser backend session");
    let connection = fault_connection(&core);
    let before_items = items(&core);
    let before_audits = audits(&core);
    reject_audit(&connection, false);

    assert!(matches!(
        session.resolve_login_secret(
            item.id,
            VaultRevision::new(99),
            VaultBackendSecretAction::Fill
        ),
        Err(VaultBackendSessionError::Batch(VaultCrudError::Storage(_)))
    ));
    core.lock_vault().expect("lock vault");
    assert!(matches!(
        session.resolve_login_secret(item.id, item.revision, VaultBackendSecretAction::Fill),
        Err(VaultBackendSessionError::Batch(VaultCrudError::Storage(_)))
    ));
    assert_eq!(items(&core), before_items);
    assert_eq!(audits(&core), before_audits);
}
