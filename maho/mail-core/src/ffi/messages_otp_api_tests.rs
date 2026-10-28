use super::*;
use std::ffi::{CStr, CString};
use std::sync::mpsc;
use std::time::Duration;

/// Serializes tests that swap `TEST_DB_PATH`, which the FFI entrypoint reads.
fn db_path_guard() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock().unwrap_or_else(std::sync::PoisonError::into_inner)
}

fn temp_db_path(tag: &str) -> PathBuf {
    std::env::temp_dir().join(format!(
        "maho-messages-otp-{tag}-{}-{}.db",
        std::process::id(),
        Utc::now().timestamp_nanos_opt().unwrap_or_default()
    ))
}

fn apple_ns(at: DateTime<Utc>) -> i64 {
    (at.timestamp() - APPLE_EPOCH_OFFSET_SECS) * 1_000_000_000
}

/// Fixture message row: (text, attributedBody, date ns, is_from_me).
type FixtureRow<'a> = (Option<&'a str>, Option<Vec<u8>>, i64, i64);

fn fixture_db(tag: &str, rows: &[FixtureRow<'_>]) -> PathBuf {
    let path = temp_db_path(tag);
    let conn = rusqlite::Connection::open(&path).unwrap();
    conn.execute_batch(
        "CREATE TABLE handle (ROWID INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT NOT NULL);
         CREATE TABLE message (
             ROWID INTEGER PRIMARY KEY AUTOINCREMENT,
             text TEXT,
             attributedBody BLOB,
             handle_id INTEGER DEFAULT 0,
             date INTEGER,
             is_from_me INTEGER DEFAULT 0);
         INSERT INTO handle (ROWID, id) VALUES (1, '+15555550100');",
    )
    .unwrap();
    for (text, attributed, date, is_from_me) in rows {
        conn.execute(
            "INSERT INTO message (text, attributedBody, handle_id, date, is_from_me)
             VALUES (?1, ?2, 1, ?3, ?4)",
            rusqlite::params![text, attributed, date, is_from_me],
        )
        .unwrap();
    }
    path
}

fn code_mapping(code: &str) -> serde_json::Value {
    json!([
        {"field": "otp", "name": "one-time-code", "value": code},
        {"field": "code", "name": "one-time-code", "value": code}
    ])
}

/// typedstream fragment shaped like an archived NSAttributedString.
fn attributed_blob(text: &str) -> Vec<u8> {
    let mut blob = b"\x04\x0bstreamtyped\x81\xe8\x03\x84\x01@\x84\x84\x84\x12NSAttributedString\x00\x84\x84\x08NSObject\x00\x85\x92\x84\x84\x84\x08NSString\x01\x94\x84\x01+".to_vec();
    blob.push(u8::try_from(text.len()).unwrap());
    blob.extend_from_slice(text.as_bytes());
    blob.extend_from_slice(b"\x86\x84\x02iI");
    blob
}

#[test]
fn messages_text_code_is_returned() {
    let now = Utc::now();
    let path = fixture_db(
        "text",
        &[(Some("Your verification code is 123456"), None, apple_ns(now), 0)],
    );
    let value = messages_otp_json(&path, 600, None, now);
    std::fs::remove_file(&path).ok();
    assert_eq!(value, code_mapping("123456"));
}

#[test]
fn messages_attributed_body_fallback_is_returned() {
    let now = Utc::now();
    let blob = attributed_blob("Your verification code is 654321");
    assert_eq!(
        attributed_body_text(&blob).as_deref(),
        Some("Your verification code is 654321")
    );
    let path = fixture_db("attributed", &[(None, Some(blob), apple_ns(now), 0)]);
    let value = messages_otp_json(&path, 600, None, now);
    std::fs::remove_file(&path).ok();
    assert_eq!(value, code_mapping("654321"));
}

#[test]
fn messages_outgoing_and_stale_rows_are_ignored() {
    let now = Utc::now();
    let path = fixture_db(
        "ignored",
        &[
            (Some("Your verification code is 111111"), None, apple_ns(now), 1),
            (
                Some("Your verification code is 222222"),
                None,
                apple_ns(now - chrono::Duration::seconds(3600)),
                0,
            ),
        ],
    );
    let value = messages_otp_json(&path, 600, None, now);
    std::fs::remove_file(&path).ok();
    assert_eq!(value, json!([]));
}

#[test]
fn messages_page_origin_requires_trusted_binding() {
    // A phone-number sender has no domain, so with a known page origin the
    // code is only usable when the message links that origin (LinkExact);
    // otherwise domain.rs classifies it Unrelated and extract.rs drops it.
    let now = Utc::now();
    let path = fixture_db(
        "origin",
        &[
            (Some("Your verification code is 333333"), None, apple_ns(now), 0),
            (
                Some("Your example.com verification code is 444444 https://example.com/verify"),
                None,
                apple_ns(now) - 1_000_000_000,
                0,
            ),
        ],
    );
    let bound = messages_otp_json(&path, 600, Some("https://example.com"), now);
    let unrelated = messages_otp_json(&path, 600, Some("https://other.test"), now);
    std::fs::remove_file(&path).ok();
    assert_eq!(bound, code_mapping("444444"));
    assert_eq!(unrelated, json!([]));
}

#[test]
fn messages_missing_db_is_empty() {
    let path = temp_db_path("missing");
    assert_eq!(messages_otp_json(&path, 600, None, Utc::now()), json!([]));
}

#[test]
fn messages_unreadable_db_is_empty() {
    let path = temp_db_path("garbage");
    std::fs::write(&path, b"not a sqlite database at all, just bytes").unwrap();
    let value = messages_otp_json(&path, 600, None, Utc::now());
    std::fs::remove_file(&path).ok();
    assert_eq!(value, json!([]));
}

#[derive(Debug)]
struct Captured {
    ok: bool,
    payload: Vec<u8>,
}

unsafe extern "C" fn capture(ok: bool, payload: *const c_char, data: *mut c_void) {
    // The callback owns the sender, including if the receiver times out.
    let sender = unsafe { Box::from_raw(data.cast::<mpsc::Sender<Captured>>()) };
    let payload = unsafe { CStr::from_ptr(payload) }.to_bytes().to_owned();
    // A late callback after a failed bounded wait just drops its sender.
    let _ = sender.send(Captured { ok, payload });
}

fn ffi_extract(db: Option<PathBuf>, page_origin: Option<&str>) -> serde_json::Value {
    *TEST_DB_PATH
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner) = db;
    let origin = page_origin.map(|o| CString::new(o).unwrap());
    let (sender, receiver) = mpsc::channel::<Captured>();
    let data = Box::into_raw(Box::new(sender));
    let accepted = MahoMessagesExtractOtp(
        600,
        origin.as_ref().map_or(std::ptr::null(), |o| o.as_ptr()),
        Some(capture),
        data.cast(),
    );
    if !accepted {
        // Rejected calls never transfer ownership to a callback.
        drop(unsafe { Box::from_raw(data) });
    }
    assert!(accepted);
    let Captured { ok, payload } = receiver
        .recv_timeout(Duration::from_secs(10))
        .expect("messages OTP callback");
    let payload = String::from_utf8(payload).expect("UTF-8 callback payload");
    assert!(ok, "{payload}");
    serde_json::from_str(&payload).expect("one JSON parse")
}

#[test]
fn messages_ffi_returns_code_from_fixture() {
    let _guard = db_path_guard();
    let now = Utc::now();
    let path = fixture_db(
        "ffi",
        &[(Some("Your verification code is 123456"), None, apple_ns(now), 0)],
    );
    let value = ffi_extract(Some(path.clone()), Some(""));
    std::fs::remove_file(&path).ok();
    assert_eq!(value, code_mapping("123456"));
}

#[test]
fn messages_ffi_missing_db_is_empty_array() {
    let _guard = db_path_guard();
    assert_eq!(ffi_extract(Some(temp_db_path("ffi-missing")), None), json!([]));
    assert_eq!(ffi_extract(None, None), json!([]));
}

#[test]
fn messages_ffi_rejects_missing_callback() {
    assert!(!MahoMessagesExtractOtp(600, std::ptr::null(), None, std::ptr::null_mut()));
}
