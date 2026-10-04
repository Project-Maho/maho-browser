//! Failing-first commercial compose/storage regressions. No provider traffic.
use super::*;
use crate::ffi::organize_api::*;
use serde_json::{json, Value};
use std::ffi::{CStr, CString};
use std::sync::{mpsc, Arc};
use std::time::Duration;

#[path = "compose_smtp_review_tests.rs"]
mod smtp_wire;

struct FixtureDir(PathBuf);

impl FixtureDir {
    fn new() -> Self {
        let path = std::env::temp_dir().join(format!("compose-review-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir(&path).unwrap();
        Self(path)
    }
}

impl Drop for FixtureDir {
    fn drop(&mut self) {
        if let Err(error) = std::fs::remove_dir_all(&self.0) {
            if std::thread::panicking() {
                eprintln!("fixture cleanup failed: {error}");
            } else {
                panic!("fixture cleanup failed: {error}");
            }
        }
    }
}

struct TestContext {
    ctx: Option<Arc<crate::state::AppCtx>>,
    database: PathBuf,
    _cache_root: FixtureDir,
}

impl std::ops::Deref for TestContext {
    type Target = Arc<crate::state::AppCtx>;

    fn deref(&self) -> &Self::Target {
        self.ctx.as_ref().unwrap()
    }
}

impl Drop for TestContext {
    fn drop(&mut self) {
        crate::state::clear_ctx_for_test();
        drop(self.ctx.take());
        for suffix in ["", "-wal", "-shm", "-journal"] {
            let path = PathBuf::from(format!("{}{suffix}", self.database.display()));
            if let Err(error) = std::fs::remove_file(path) {
                if error.kind() != std::io::ErrorKind::NotFound {
                    if std::thread::panicking() {
                        eprintln!("database cleanup failed: {error}");
                    } else {
                        panic!("database cleanup failed: {error}");
                    }
                }
            }
        }
    }
}

unsafe extern "C" fn capture(ok: bool, payload: *const c_char, data: *mut c_void) {
    let sender = unsafe { Box::from_raw(data.cast::<mpsc::Sender<(bool, String)>>()) };
    let text = unsafe { CStr::from_ptr(payload) }
        .to_string_lossy()
        .into_owned();
    // A late callback after the test's bounded timeout must not unwind through C.
    if let Err(error) = sender.send((ok, text)) {
        eprintln!("FFI callback receiver already failed: {error}");
    }
}

fn call(action: impl FnOnce(MahoMailReadCallback, *mut c_void) -> bool) -> (bool, Value) {
    let (sender, receiver) = mpsc::channel::<(bool, String)>();
    let data = Box::into_raw(Box::new(sender)).cast::<c_void>();
    if !action(Some(capture), data) {
        unsafe {
            drop(Box::from_raw(data.cast::<mpsc::Sender<(bool, String)>>()));
        }
        panic!("FFI call was not accepted");
    }
    let (ok, text) = receiver
        .recv_timeout(Duration::from_secs(15))
        .expect("exact FFI completion");
    // The real FFI contract has JSON success payloads and plain-text errors.
    let payload = if ok {
        serde_json::from_str(&text).unwrap()
    } else {
        Value::String(text)
    };
    (ok, payload)
}

fn setup() -> TestContext {
    crate::runtime::runtime().unwrap();
    let pool = crate::test_support::pool_with_seeded_data();
    let database = PathBuf::from(pool.get().unwrap().path().unwrap());
    let mut ctx = crate::test_support::ctx(pool);
    let cache_root = FixtureDir::new();
    ctx.db_path = cache_root.0.join("mail.db");
    let ctx = Arc::new(ctx);
    crate::state::set_ctx(ctx.clone()).unwrap();
    ctx.pool.get().unwrap().execute("INSERT INTO folders (id, account_id, name, path, folder_type) VALUES ('drafts', 'acc1', 'Drafts', 'Drafts', 'drafts')", []).unwrap();
    TestContext {
        ctx: Some(ctx),
        database,
        _cache_root: cache_root,
    }
}

fn request() -> Value {
    json!({"account_id":"acc1", "to":["to@example.test"], "cc":["cc@example.test"],
        "bcc":["hidden@example.test"], "subject":"subject", "body_text":"text",
        "body_html":"<b>html</b>", "in_reply_to":"<parent@example.test>",
        "references":"<root@example.test> <parent@example.test>", "read_receipt":true,
        "attachments":[{"filename":"note.txt","mime_type":"text/plain","data":"aGVsbG8="}]})
}

fn draft_roundtrip(update: bool) {
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let req = request();
    let initial = CString::new(if update {
        json!({"account_id":"acc1","to":[],"subject":"initial"}).to_string()
    } else {
        req.to_string()
    })
    .unwrap();
    let (ok, id) = call(|cb, data| MahoMailSaveDraft(initial.as_ptr(), cb, data));
    assert!(ok, "{id}");
    let id = id.as_str().unwrap();
    if update {
        let id_c = CString::new(id).unwrap();
        let raw = CString::new(req.to_string()).unwrap();
        let (ok, result) =
            call(|cb, data| MahoMailUpdateDraft(id_c.as_ptr(), raw.as_ptr(), cb, data));
        assert!(ok, "{result}");
    }
    let conn = ctx.pool.get().unwrap();
    let (bcc, reply, html, attachments): (Option<String>, Option<String>, Option<String>, Option<String>) = conn.query_row(
        "SELECT bcc_addresses, in_reply_to, body_html, draft_attachments_json FROM emails WHERE id=?1", [id],
        |r| Ok((r.get(0)?,r.get(1)?,r.get(2)?,r.get(3)?))).unwrap();
    assert_eq!(
        bcc.as_deref()
            .map(|s| serde_json::from_str::<Value>(s).unwrap()),
        Some(req["bcc"].clone())
    );
    assert_eq!(reply.as_deref(), req["in_reply_to"].as_str());
    assert_eq!(html.as_deref(), req["body_html"].as_str());
    assert_eq!(
        serde_json::from_str::<Value>(&attachments.unwrap()).unwrap(),
        req["attachments"]
    );
    let (email, _, needs_fetch) = maho_core::services::email::get_email_from_db(&conn, id).unwrap();
    assert!(!needs_fetch, "local draft must not fetch UID 0 from IMAP");
    let detail = serde_json::to_value(email).unwrap();
    assert_eq!(
        detail["draft_attachments_json"]
            .as_str()
            .map(|s| serde_json::from_str::<Value>(s).unwrap()),
        Some(req["attachments"].clone())
    );
    assert_eq!(detail["email_references"], req["references"]);
    assert_eq!(detail["read_receipt"], req["read_receipt"]);
    drop(conn);
    let reopened = rusqlite::Connection::open(&ctx.database).unwrap();
    reopened
        .pragma_update(None, "key", &ctx.sqlcipher_key)
        .unwrap();
    let (persisted, _, needs_fetch) =
        maho_core::services::email::get_email_from_db(&reopened, id).unwrap();
    assert!(!needs_fetch);
    assert_eq!(serde_json::to_value(persisted).unwrap(), detail);
    let id_c = CString::new(id).unwrap();
    let (ok, wire) =
        call(|cb, data| crate::ffi::read_api::MahoMailGetEmail(id_c.as_ptr(), cb, data));
    assert!(ok, "{wire}");
    assert_eq!(wire["email"], detail);
}

#[test]
fn cc04_save_complete_draft() {
    draft_roundtrip(false);
}
#[test]
fn cc04_update_complete_draft() {
    draft_roundtrip(true);
}

#[test]
fn cc01_due_schedule_reaches_durable_delivery_attempt() {
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    {
        let conn = ctx.pool.get().unwrap();
        // Missing credentials fail before any transport. The scheduled request remains
        // durable, but its due delivery must be attempted and the failure made visible.
        conn.execute("UPDATE accounts SET password = NULL WHERE id = 'acc1'", [])
            .unwrap();
        conn.execute(
            "DELETE FROM encrypted_credentials WHERE account_id = 'acc1'",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO send_later (id,account_id,to_addresses,subject,body_text,scheduled_at,attachments_json,read_receipt,in_reply_to,email_references)
             VALUES ('due','acc1','[\"to@example.test\"]','encrypted schedule','CIPHERTEXT','2026-09-05T01:00:00Z',?1,1,'<parent@example.test>','<root@example.test>')",
            [request()["attachments"].to_string()],
        ).unwrap();
    }
    let now = chrono::DateTime::parse_from_rfc3339("2026-09-05T12:00:00Z")
        .unwrap()
        .with_timezone(&chrono::Utc);
    crate::runtime::runtime()
        .unwrap()
        .block_on(run_scheduler_once(&ctx, now))
        .unwrap();
    let (status, body, error): (String, String, Option<String>) = ctx
        .pool
        .get()
        .unwrap()
        .query_row(
            "SELECT status,body_text,last_error FROM send_later WHERE id='due'",
            [],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        )
        .unwrap();
    assert_eq!(body, "CIPHERTEXT");
    assert_eq!(
        status, "failed",
        "due schedule must attempt delivery, never fabricate sent"
    );
    assert!(error.is_some());
}

#[test]
fn s10_scheduler_tick_compares_rfc3339_instants() {
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    ctx.pool.get().unwrap().execute("UPDATE emails SET snoozed_until='2026-09-05T09:00:00+02:00', reminder_at='2026-09-05T09:00:00+02:00' WHERE id='em1'", []).unwrap();
    let now = chrono::DateTime::parse_from_rfc3339("2026-09-05T08:00:00Z")
        .unwrap()
        .with_timezone(&chrono::Utc);
    crate::runtime::runtime()
        .unwrap()
        .block_on(run_scheduler_once(&ctx, now))
        .unwrap();
    let values: (Option<String>, Option<String>) = ctx
        .pool
        .get()
        .unwrap()
        .query_row(
            "SELECT snoozed_until,reminder_at FROM emails WHERE id='em1'",
            [],
            |row| Ok((row.get(0)?, row.get(1)?)),
        )
        .unwrap();
    assert_eq!(values, (None, None));
}

#[test]
fn s10_scheduler_tick_preserves_future_and_invalid_timestamps() {
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let now = chrono::DateTime::parse_from_rfc3339("2026-09-05T08:00:00.500Z")
        .unwrap()
        .with_timezone(&chrono::Utc);
    for (timestamp, expires) in [
        ("2026-09-05T10:00:00.500+02:00", true),
        ("2026-09-05T08:00:00.750Z", false),
        ("2026-09-05T07:30:00-01:00", false),
        ("not-a-timestamp", false),
    ] {
        ctx.pool
            .get()
            .unwrap()
            .execute(
                "UPDATE emails SET snoozed_until=?1, reminder_at=?1 WHERE id='em1'",
                [timestamp],
            )
            .unwrap();
        crate::runtime::runtime()
            .unwrap()
            .block_on(run_scheduler_once(&ctx, now))
            .unwrap();
        let values: (Option<String>, Option<String>) = ctx
            .pool
            .get()
            .unwrap()
            .query_row(
                "SELECT snoozed_until,reminder_at FROM emails WHERE id='em1'",
                [],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .unwrap();
        let expected = (!expires).then(|| timestamp.to_owned());
        assert_eq!(values, (expected.clone(), expected), "{timestamp}");
    }
}

#[test]
fn s06_category_callback_is_array_not_json_string() {
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    ctx.pool
        .get()
        .unwrap()
        .execute(
            "UPDATE emails SET smart_category='personal' WHERE id='em1'",
            [],
        )
        .unwrap();
    let account = CString::new("acc1").unwrap();
    let category = CString::new("personal").unwrap();
    let (ok, result) = call(|cb, data| {
        MahoMailListBySmartCategory(account.as_ptr(), category.as_ptr(), 50, 0, cb, data)
    });
    assert!(ok, "{result}");
    assert!(result.is_array(), "wire value must be an array: {result}");
    assert_eq!(result[0]["id"], "em1");
}

#[test]
fn s10_scheduled_list_orders_instants_not_offset_strings() {
    let _guard = crate::test_support::global_ctx_guard();
    let _ctx = setup();
    for (subject, time) in [
        ("later", "2999-01-01T01:00:00-10:00"),
        ("earlier", "2999-01-01T05:00:00+10:00"),
    ] {
        let mut req = request();
        req["subject"] = json!(subject);
        req["scheduled_at"] = json!(time);
        let raw = CString::new(req.to_string()).unwrap();
        let (ok, result) = call(|cb, data| MahoMailScheduleSend(raw.as_ptr(), cb, data));
        assert!(ok, "{result}");
    }
    let (ok, result) = call(|cb, data| MahoMailListScheduledSends(std::ptr::null(), cb, data));
    assert!(ok, "{result}");
    assert_eq!(result[0]["subject"], "earlier");
}

#[test]
fn cc06_interrupted_send_is_visible_as_uncertain_not_retried() {
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    ctx.pool.get().unwrap().execute("INSERT INTO outbox (id,account_id,to_addresses,subject,status) VALUES ('interrupted','acc1','[\"to@example.test\"]','subject','sending')", []).unwrap();
    let (ok, result) = call(|cb, data| MahoMailFlushOutbox(cb, data));
    assert!(ok, "{result}");
    let (status, retries): (String, i64) = ctx
        .pool
        .get()
        .unwrap()
        .query_row(
            "SELECT status,retry_count FROM outbox WHERE id='interrupted'",
            [],
            |r| Ok((r.get(0)?, r.get(1)?)),
        )
        .unwrap();
    assert_eq!(status, "uncertain");
    assert_eq!(
        retries, 0,
        "interrupted DATA acknowledgement must not trigger blind replay"
    );
}

#[test]
fn cc05_legacy_cache_cannot_bypass_folder_epoch_identity() {
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let cache = attachment_cache_dir(&ctx).unwrap();
    std::fs::create_dir_all(&cache).unwrap();
    let stale = cache.join("acc1_1_2_note.txt");
    std::fs::write(&stale, b"bytes from a different folder or epoch").unwrap();
    // Existing account + nonexistent folder must fail before transport or stale-cache return.
    let account = CString::new("acc1").unwrap();
    let folder = CString::new("removed-folder-epoch").unwrap();
    let part = CString::new("2").unwrap();
    let filename = CString::new("note.txt").unwrap();
    let (ok, result) = call(|cb, data| {
        MahoMailDownloadAttachment(
            account.as_ptr(),
            1,
            folder.as_ptr(),
            part.as_ptr(),
            filename.as_ptr(),
            cb,
            data,
        )
    });
    assert!(
        !ok,
        "stale cache must not satisfy an unknown folder/epoch: {result}"
    );
}

#[test]
fn cc08_smtp_permanent_rejection_is_not_successfully_queued() {
    use tokio::io::AsyncWriteExt;
    let _guard = crate::test_support::global_ctx_guard();
    let ctx = setup();
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    {
        let conn = ctx.pool.get().unwrap();
        conn.execute("UPDATE accounts SET smtp_host='127.0.0.1',smtp_port=?1,smtp_encryption='StartTls' WHERE id='acc1'", [port]).unwrap();
        crate::credentials::store_encrypted_credential(
            &conn,
            &ctx.credential_key,
            "acc1",
            "password",
            "fixture",
        )
        .unwrap();
    }
    // Reject at the SMTP greeting, before STARTTLS: real lettre transport, no cert override.
    listener.set_nonblocking(true).unwrap();
    let rt = crate::runtime::runtime().unwrap();
    let server = rt.spawn(async move {
        let listener = tokio::net::TcpListener::from_std(listener).unwrap();
        let (mut socket, _) = tokio::time::timeout(Duration::from_secs(10), listener.accept())
            .await
            .unwrap()
            .unwrap();
        tokio::time::timeout(
            Duration::from_secs(5),
            socket.write_all(b"554 5.7.1 Service unavailable\r\n"),
        )
        .await
        .unwrap()
        .unwrap();
    });
    let raw = CString::new(request().to_string()).unwrap();
    let (ok, result) = call(|cb, data| MahoMailSendEmail(raw.as_ptr(), cb, data));
    rt.block_on(async {
        tokio::time::timeout(Duration::from_secs(15), server)
            .await
            .unwrap()
            .unwrap();
    });
    let count: i64 = ctx
        .pool
        .get()
        .unwrap()
        .query_row("SELECT count(*) FROM outbox", [], |r| r.get(0))
        .unwrap();
    assert!(
        !ok,
        "permanent SMTP rejection is not delivery or offline queue success: {result}"
    );
    assert_eq!(count, 0);
}
