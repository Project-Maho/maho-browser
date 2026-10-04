// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use std::ffi::CString;
use std::sync::Arc;

use super::*;

#[test]
fn null_c_string_is_rejected() {
    assert!(c_string(std::ptr::null(), "arg").is_err());
}

#[test]
fn initialize_accepts_profile_path() {
    let version = CString::new("v1").unwrap();
    let path = CString::new("/tmp/maho-mail-ffi-test").unwrap();
    assert!(MahoMailInitialize(version.as_ptr(), path.as_ptr()));
}

fn ctx_with_password_accounts(ids: &[&str]) -> Arc<AppCtx> {
    let pool = crate::test_support::pool_with_seeded_data();
    {
        let conn = pool.get().unwrap();
        conn.execute("DELETE FROM accounts", []).unwrap();
        for id in ids {
            conn.execute(
                "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, password, created_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)",
                rusqlite::params![id, format!("{id}@example.com"), "Acct", "imap.example.com", 993, "Tls", "smtp.example.com", 587, "StartTls", format!("{id}@example.com"), "password", "pw", "2024-01-01T00:00:00Z"],
            ).unwrap();
        }
    }
    crate::test_support::ctx_arc(pool)
}

#[test]
fn start_all_account_sync_registers_all_accounts_once() {
    let rt = crate::runtime::runtime().expect("runtime");
    let id_a = format!("all-{}", uuid::Uuid::new_v4());
    let id_b = format!("all-{}", uuid::Uuid::new_v4());
    let ctx = ctx_with_password_accounts(&[&id_a, &id_b]);

    let first = start_all_account_sync(&ctx, rt).expect("enumerates accounts");
    assert_eq!(first, 2, "spawns a sync worker for each seeded account");
    assert!(state::registry().is_sync_running(&id_a));
    assert!(state::registry().is_sync_running(&id_b));

    let second = start_all_account_sync(&ctx, rt).expect("second pass ok");
    assert_eq!(second, 0, "live sync workers are not respawned");

    state::registry().stop_account(&id_a);
    state::registry().stop_account(&id_b);
}

use maho_core::models::account::{CreateAccountRequest, Encryption};
use std::sync::{Mutex, OnceLock};

static EVENT_LOG: OnceLock<Mutex<Vec<(String, String)>>> = OnceLock::new();
static NEW_MAIL_LOG: OnceLock<Mutex<Vec<(String, String, String, String, String, u64, u64)>>> =
    OnceLock::new();

unsafe extern "C" fn test_event_cb(
    event_type: *const std::os::raw::c_char,
    account_id: *const std::os::raw::c_char,
    _arg1: *const std::os::raw::c_char,
    _arg2: *const std::os::raw::c_char,
) {
    let ev = std::ffi::CStr::from_ptr(event_type)
        .to_string_lossy()
        .into_owned();
    let acc = std::ffi::CStr::from_ptr(account_id)
        .to_string_lossy()
        .into_owned();
    let log = EVENT_LOG.get_or_init(|| Mutex::new(Vec::new()));
    log.lock().unwrap().push((ev, acc));
}

unsafe extern "C" fn test_new_mail_cb(
    account_id: *const std::os::raw::c_char,
    email_id: *const std::os::raw::c_char,
    message_id: *const std::os::raw::c_char,
    sender: *const std::os::raw::c_char,
    subject: *const std::os::raw::c_char,
    cursor: u64,
    epoch: u64,
) {
    let read = |value| {
        std::ffi::CStr::from_ptr(value)
            .to_string_lossy()
            .into_owned()
    };
    NEW_MAIL_LOG
        .get_or_init(|| Mutex::new(Vec::new()))
        .lock()
        .unwrap()
        .push((
            read(account_id),
            read(email_id),
            read(message_id),
            read(sender),
            read(subject),
            cursor,
            epoch,
        ));
}

static TEST_MUTEX: OnceLock<Mutex<()>> = OnceLock::new();

#[test]
fn accounts_changed_event_emission_and_reconnect() {
    let _lock = TEST_MUTEX.get_or_init(|| Mutex::new(())).lock().unwrap();

    let pool = crate::test_support::pool_with_seeded_data();
    let ctx = crate::test_support::ctx_arc(pool);

    assert!(MahoMailRegisterEventCallback(Some(test_event_cb)));
    assert!(MahoMailRegisterNewMailCallback(Some(test_new_mail_cb)));

    let log = EVENT_LOG.get_or_init(|| Mutex::new(Vec::new()));
    log.lock().unwrap().clear();

    let new_mail = NEW_MAIL_LOG.get_or_init(|| Mutex::new(Vec::new()));
    new_mail.lock().unwrap().clear();
    emit_new_mail_event(
        "arrival-account",
        "email-7",
        "thread-7",
        "Sender",
        "Subject",
        7,
        1234,
    );
    assert_eq!(
        new_mail.lock().unwrap().as_slice(),
        &[(
            "arrival-account".to_string(),
            "email-7".to_string(),
            "thread-7".to_string(),
            "Sender".to_string(),
            "Subject".to_string(),
            7,
            1234,
        )],
        "new mailbox arrivals preserve their typed identity"
    );

    log.lock().unwrap().clear();

    let mut conn = ctx.pool.get().unwrap();
    let req = CreateAccountRequest {
        email: "event@example.com".to_string(),
        display_name: "New User".to_string(),
        auth_type: Some("password".to_string()),
        imap_host: "imap.example.com".to_string(),
        imap_port: 993,
        imap_encryption: Encryption::Tls,
        smtp_host: "smtp.example.com".to_string(),
        smtp_port: 587,
        smtp_encryption: Encryption::StartTls,
        username: "event@example.com".to_string(),
        password: Some("hunter2".to_string()),
        oauth2_client_id: None,
        oauth2_client_secret: None,
        oauth2_access_token: None,
        oauth2_refresh_token: None,
        oauth2_expires_at: None,
    };
    let response = crate::account::create_account(&mut conn, &ctx.credential_key, req).unwrap();

    let events = log.lock().unwrap().clone();
    let match_count = events
        .iter()
        .filter(|(ev, acc)| ev == "accounts-changed" && acc == &response.id)
        .count();
    assert_eq!(
        match_count, 1,
        "exactly one accounts-changed event emitted for create"
    );

    log.lock().unwrap().clear();

    let rt = crate::runtime::runtime().expect("runtime");
    crate::account::reconnect_account_with_runtime(&ctx, &response.id, Some(rt)).unwrap();

    let events = log.lock().unwrap().clone();
    let match_count = events
        .iter()
        .filter(|(ev, acc)| ev == "accounts-changed" && acc == &response.id)
        .count();
    assert_eq!(
        match_count, 1,
        "exactly one accounts-changed event emitted for reconnect"
    );
    assert!(state::registry().is_sync_running(&response.id));

    log.lock().unwrap().clear();

    crate::account::delete_account(&ctx, &response.id).unwrap();
    let events = log.lock().unwrap().clone();
    let match_count = events
        .iter()
        .filter(|(ev, acc)| ev == "accounts-changed" && acc == &response.id)
        .count();
    assert_eq!(
        match_count, 1,
        "exactly one accounts-changed event emitted for delete"
    );
}

#[tokio::test]
async fn prepare_shutdown_drain_stops_all_workers_before_callback() {
    // Mirrors MahoMailHelperImpl::PrepareShutdown (maho_mail_helper_main.cc):
    // no MahoMailPrepareShutdown / MahoMailDrainBackend FFI exists in this
    // crate, so the helper drains via MahoMailStopSync() and only then runs
    // its Mojo shutdown callback. This test proves the drain half by invoking
    // the exact FFI symbol the helper calls: every live sync worker
    // (including its bundled IMAP IDLE loop task) and every live backfill
    // worker must report stopped afterwards. Fully deterministic, no sleeps:
    // the stop flags flip synchronously inside the FFI call.
    let id = format!("shutdown-drain-{}", uuid::Uuid::new_v4());
    let live = || crate::state::WorkerHandle {
        stop: std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false)),
        tasks: vec![tokio::spawn(std::future::pending::<()>())],
        wake: None,
    };
    state::registry().insert_sync(id.clone(), live());
    state::registry().insert_backfill(id.clone(), live());
    assert!(
        state::registry().is_sync_running(&id),
        "sync worker live before drain"
    );
    assert!(
        state::registry().is_backfill_running(&id),
        "backfill worker live before drain"
    );

    assert!(MahoMailStopSync(), "drain FFI reports success");

    assert!(
        !state::registry().is_sync_running(&id),
        "sync worker (incl. IDLE loop) drained before shutdown callback"
    );
    assert!(
        !state::registry().is_backfill_running(&id),
        "backfill worker drained before shutdown callback"
    );
}

#[test]
fn register_callbacks_reject_none() {
    assert!(!MahoMailRegisterEventCallback(None));
    assert!(!MahoMailRegisterNewMailCallback(None));
}

