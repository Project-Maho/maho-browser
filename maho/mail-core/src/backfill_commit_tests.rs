use super::*;
use std::sync::atomic::Ordering;

fn registered(registry: &SyncRegistry) -> Arc<AtomicBool> {
    let stop = Arc::new(AtomicBool::new(false));
    registry.insert_backfill("acc".into(), WorkerHandle {
        stop: Arc::clone(&stop),
        tasks: vec![],
        wake: None,
    });
    stop
}

#[test]
fn current_identity_persists_under_registry_lock() {
    let registry = SyncRegistry::default();
    let stop = registered(&registry);
    let result = registry.with_backfill_commit("acc", &stop, || {
        assert!(matches!(registry.backfill.try_lock(), Err(std::sync::TryLockError::WouldBlock)));
        Ok(7)
    }).unwrap();
    assert_eq!(result, Some(7));
}

#[test]
fn missing_worker_cannot_persist() {
    let registry = SyncRegistry::default();
    let stop = Arc::new(AtomicBool::new(false));
    let result = registry.with_backfill_commit("acc", &stop, || -> Result<()> {
        panic!("missing worker admitted")
    }).unwrap();
    assert_eq!(result, None);
}

#[test]
fn stopped_worker_cannot_persist() {
    let registry = SyncRegistry::default();
    let stop = registered(&registry);
    stop.store(true, Ordering::SeqCst);
    let result = registry.with_backfill_commit("acc", &stop, || -> Result<()> {
        panic!("stopped worker admitted")
    }).unwrap();
    assert_eq!(result, None);
}

#[test]
fn removed_worker_cannot_persist() {
    let registry = SyncRegistry::default();
    let stop = registered(&registry);
    registry.stop_account("acc");
    let result = registry.with_backfill_commit("acc", &stop, || -> Result<()> {
        panic!("removed worker admitted")
    }).unwrap();
    assert_eq!(result, None);
}

#[test]
fn wrong_identity_cannot_persist_into_current_worker() {
    let registry = SyncRegistry::default();
    let _current = registered(&registry);
    let wrong = Arc::new(AtomicBool::new(false));
    let result = registry.with_backfill_commit("acc", &wrong, || -> Result<()> {
        panic!("foreign worker admitted")
    }).unwrap();
    assert_eq!(result, None);
}

#[test]
fn replacement_rejects_old_identity_and_admits_new_identity() {
    let registry = SyncRegistry::default();
    let old = registered(&registry);
    let current = registered(&registry);
    let result = registry.with_backfill_commit("acc", &old, || -> Result<()> {
        panic!("replaced worker admitted")
    }).unwrap();
    assert_eq!(result, None);
    assert_eq!(registry.with_backfill_commit("acc", &current, || Ok(9)).unwrap(), Some(9));
}

#[test]
fn persistence_failure_is_propagated() {
    let registry = SyncRegistry::default();
    let stop = registered(&registry);
    let result = registry.with_backfill_commit("acc", &stop, || -> Result<()> {
        Err(MailFfiError::NotInitialized)
    });
    assert!(matches!(result, Err(MailFfiError::NotInitialized)));
}
