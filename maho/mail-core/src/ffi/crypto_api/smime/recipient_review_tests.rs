//! C07 native RED: real SQLCipher, real PKCS12 import, real FFI callbacks.
//! Generated keys are test identities only; no provider traffic or crypto mocks.
use crate::ffi::crypto_api::*;
use ::openssl::asn1::Asn1Time;
use ::openssl::bn::BigNum;
use ::openssl::hash::MessageDigest;
use ::openssl::pkcs12::Pkcs12;
use ::openssl::pkcs7::{Pkcs7, Pkcs7Flags};
use ::openssl::pkey::{PKey, Private};
use ::openssl::rsa::Rsa;
use ::openssl::stack::Stack;
use ::openssl::x509::extension::{
    BasicConstraints, ExtendedKeyUsage, KeyUsage, SubjectAlternativeName,
};
use ::openssl::x509::store::X509StoreBuilder;
use ::openssl::x509::{X509NameBuilder, X509};
use base64::Engine as _;
use mail_parser::MimeHeaders;
use serde_json::{json, Value};
use std::ffi::{CStr, CString};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{mpsc, Arc};
use std::time::Duration;

const MAILBOXES: [&str; 3] = ["to@example.test", "cc@example.test", "bcc@example.test"];
const BINARY: &[u8] = &[0, 1, 2, 127, 128, 255];
// CMS identifies recipients by issuer + serial, including identities sharing a CN.
static NEXT_SERIAL: AtomicU32 = AtomicU32::new(1);

struct Identity {
    cert: X509,
    key: PKey<Private>,
}

impl Identity {
    fn new(emails: &[&str], kind: &str) -> Self {
        let key = PKey::from_rsa(Rsa::generate(2048).unwrap()).unwrap();
        let mut name = X509NameBuilder::new().unwrap();
        name.append_entry_by_text("CN", emails[0]).unwrap();
        let name = name.build();
        let mut cert = X509::builder().unwrap();
        cert.set_version(2).unwrap();
        let serial = NEXT_SERIAL.fetch_add(1, Ordering::Relaxed);
        cert.set_serial_number(&BigNum::from_u32(serial).unwrap().to_asn1_integer().unwrap())
            .unwrap();
        cert.set_subject_name(&name).unwrap();
        cert.set_issuer_name(&name).unwrap();
        cert.set_pubkey(&key).unwrap();
        // Deliberately wide, fixed validity; only validity tests use invalid windows.
        cert.set_not_before(
            &Asn1Time::from_str_x509(if kind == "future" {
                "20900101000000Z"
            } else {
                "20200101000000Z"
            })
            .unwrap(),
        )
        .unwrap();
        cert.set_not_after(
            &Asn1Time::from_str_x509(if kind == "expired" {
                "20210101000000Z"
            } else {
                "20990101000000Z"
            })
            .unwrap(),
        )
        .unwrap();
        let mut constraints = BasicConstraints::new();
        constraints.critical();
        if kind == "ca" {
            constraints.ca();
        }
        cert.append_extension(constraints.build().unwrap()).unwrap();
        let mut usage = KeyUsage::new();
        usage.digital_signature();
        if kind != "sign-only" {
            usage.key_encipherment();
        }
        cert.append_extension(usage.build().unwrap()).unwrap();
        let mut eku = ExtendedKeyUsage::new();
        if kind == "wrong-eku" {
            eku.server_auth();
        } else {
            eku.email_protection();
        }
        cert.append_extension(eku.build().unwrap()).unwrap();
        if kind != "cn-only" {
            let mut san = SubjectAlternativeName::new();
            for email in emails {
                san.email(email);
            }
            let extension = san.build(&cert.x509v3_context(None, None)).unwrap();
            cert.append_extension(extension).unwrap();
        }
        cert.sign(&key, MessageDigest::sha256()).unwrap();
        Self {
            cert: cert.build(),
            key,
        }
    }

    fn pem(&self) -> String {
        String::from_utf8(self.cert.to_pem().unwrap()).unwrap()
    }

    fn decrypt(&self, smime: &str) -> Vec<u8> {
        let (cms, _) = Pkcs7::from_smime(smime.as_bytes()).expect("real CMS MIME output");
        cms.decrypt(&self.key, &self.cert, Pkcs7Flags::empty())
            .expect("intended non-sender independently decrypts")
    }
}

struct Fixture {
    ctx: Option<Arc<crate::state::AppCtx>>,
    database: PathBuf,
    profile: PathBuf,
}
impl Fixture {
    fn new() -> Self {
        crate::runtime::runtime().unwrap();
        let pool = crate::test_support::pool_with_seeded_data();
        let database = PathBuf::from(pool.get().unwrap().path().unwrap());
        let profile = std::env::temp_dir().join(format!("smime-review-{}", Uuid::new_v4()));
        std::fs::create_dir_all(profile.join("MahoMail")).unwrap();
        let mut ctx = crate::test_support::ctx(pool);
        ctx.db_path = profile.join("MahoMail/maho_mail.db");
        let ctx = Arc::new(ctx);
        crate::state::set_ctx(ctx.clone()).unwrap();
        let fixture = Self {
            ctx: Some(ctx),
            database,
            profile,
        };
        let sender = Identity::new(&["user@example.com"], "valid");
        let id = fixture.import(&sender, "acc1");
        fixture
            .ctx
            .as_ref()
            .unwrap()
            .pool
            .get()
            .unwrap()
            .execute(
                "UPDATE smime_identities SET is_default=1 WHERE id=?1",
                [&id],
            )
            .unwrap();
        fixture
    }

    fn import(&self, identity: &Identity, account: &str) -> String {
        let mut builder = Pkcs12::builder();
        builder
            .name("C07 fixture")
            .pkey(&identity.key)
            .cert(&identity.cert);
        let p12 = builder
            .build2("fixture-password")
            .unwrap()
            .to_der()
            .unwrap();
        let request = json!({"account_id":account,"password":"fixture-password",
            "p12_data":base64::prelude::BASE64_STANDARD.encode(p12)});
        let raw = CString::new(request.to_string()).unwrap();
        let (ok, result) = call(|cb, data| MahoMailImportSmimeIdentity(raw.as_ptr(), cb, data));
        assert!(ok, "real PKCS12 import failed: {result}");
        result["id"].as_str().unwrap().to_owned()
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        crate::state::clear_ctx_for_test();
        drop(self.ctx.take());
        for suffix in ["", "-wal", "-shm", "-journal"] {
            let path = PathBuf::from(format!("{}{suffix}", self.database.display()));
            if let Err(error) = std::fs::remove_file(path) {
                if error.kind() != std::io::ErrorKind::NotFound {
                    eprintln!("fixture database cleanup: {error}");
                }
            }
        }
        if let Err(error) = std::fs::remove_dir_all(&self.profile) {
            eprintln!("fixture profile cleanup: {error}");
        }
    }
}

unsafe extern "C" fn capture(ok: bool, payload: *const c_char, data: *mut c_void) {
    let sender = unsafe { Box::from_raw(data.cast::<mpsc::Sender<(bool, String)>>()) };
    let text = unsafe { CStr::from_ptr(payload) }
        .to_string_lossy()
        .into_owned();
    if let Err(error) = sender.send((ok, text)) {
        eprintln!("callback receiver unavailable: {error}");
    }
}
fn call(action: impl FnOnce(MahoMailReadCallback, *mut c_void) -> bool) -> (bool, Value) {
    let (sender, receiver) = mpsc::channel::<(bool, String)>();
    let data = Box::into_raw(Box::new(sender)).cast::<c_void>();
    if !action(Some(capture), data) {
        unsafe {
            drop(Box::from_raw(data.cast::<mpsc::Sender<(bool, String)>>()));
        }
        return (false, Value::Null);
    }
    let (ok, text) = receiver
        .recv_timeout(Duration::from_secs(15))
        .expect("exact FFI callback");
    (
        ok,
        if ok {
            serde_json::from_str(&text).unwrap()
        } else {
            Value::String(text)
        },
    )
}
fn encrypt(request: Value) -> (bool, Value) {
    let raw = CString::new(request.to_string()).unwrap();
    call(|cb, data| MahoMailEncryptEmailSmime(raw.as_ptr(), cb, data))
}
fn request() -> Value {
    json!({"account_id":"acc1", "recipient_emails":MAILBOXES,
        "recipient_certs_pem":[], "body":"private text", "body_html":"<b>private HTML</b>",
        "attachments":[{"filename":"secret.bin","mime_type":"application/octet-stream","data":"AAECf4D/"},
            {"filename":"empty.bin","mime_type":"application/octet-stream","data":""}], "sign":false})
}
fn require_rejection(request: Value) {
    let (ok, response) = encrypt(request);
    assert!(
        !ok,
        "must reject before sender-only or wrong-recipient encryption; got {response}"
    );
}

#[test]
fn c07_missing_recipient_emails_rejected() {
    let _guard = crate::test_support::global_ctx_guard();
    let _fixture = Fixture::new();
    let mut req = request();
    req.as_object_mut().unwrap().remove("recipient_emails");
    require_rejection(req);
}
#[test]
fn c07_empty_recipient_emails_rejected() {
    let _guard = crate::test_support::global_ctx_guard();
    let _fixture = Fixture::new();
    let mut req = request();
    req["recipient_emails"] = json!([]);
    require_rejection(req);
}
macro_rules! missing_recipient {
    ($name:ident, $position:expr) => {
        #[test]
        fn $name() {
            let _guard = crate::test_support::global_ctx_guard();
            let fixture = Fixture::new();
            for (index, email) in MAILBOXES.iter().enumerate() {
                if index != $position {
                    fixture.import(&Identity::new(&[email], "valid"), "acc1");
                }
            }
            require_rejection(request());
        }
    };
}
missing_recipient!(c07_missing_to_certificate_rejected, 0);
missing_recipient!(c07_missing_cc_certificate_rejected, 1);
missing_recipient!(c07_missing_bcc_certificate_rejected, 2);

#[test]
fn c07_other_account_cannot_resolve_recipient() {
    let _guard = crate::test_support::global_ctx_guard();
    let fixture = Fixture::new();
    fixture.import(&Identity::new(&[MAILBOXES[0]], "valid"), "acc2");
    let mut req = request();
    req["recipient_emails"] = json!([MAILBOXES[0]]);
    require_rejection(req);
}
#[test]
fn c07_forged_database_email_cannot_resolve_recipient() {
    let _guard = crate::test_support::global_ctx_guard();
    let fixture = Fixture::new();
    let id = fixture.import(&Identity::new(&["unrelated@example.test"], "valid"), "acc1");
    fixture
        .ctx
        .as_ref()
        .unwrap()
        .pool
        .get()
        .unwrap()
        .execute(
            "UPDATE smime_identities SET email=?1 WHERE id=?2",
            params![MAILBOXES[0], id],
        )
        .unwrap();
    let mut req = request();
    req["recipient_emails"] = json!([MAILBOXES[0]]);
    require_rejection(req);
}
#[test]
fn c07_explicit_unrelated_certificate_rejected() {
    let _guard = crate::test_support::global_ctx_guard();
    let _fixture = Fixture::new();
    let mut req = request();
    req["recipient_certs_pem"] = json!([Identity::new(&["unrelated@example.test"], "valid").pem()]);
    require_rejection(req);
}
macro_rules! unusable_certificate {
    ($name:ident, $kind:expr) => {
        #[test]
        fn $name() {
            let _guard = crate::test_support::global_ctx_guard();
            let _fixture = Fixture::new();
            let mut req = request();
            req["recipient_emails"] = json!([MAILBOXES[0]]);
            req["recipient_certs_pem"] = json!([Identity::new(&[MAILBOXES[0]], $kind).pem()]);
            require_rejection(req);
        }
    };
}
unusable_certificate!(c07_expired_certificate_rejected, "expired");
unusable_certificate!(c07_future_certificate_rejected, "future");
unusable_certificate!(c07_sign_only_certificate_rejected, "sign-only");
unusable_certificate!(c07_wrong_eku_certificate_rejected, "wrong-eku");
unusable_certificate!(c07_cn_only_certificate_rejected, "cn-only");
unusable_certificate!(c07_ca_certificate_rejected, "ca");
#[test]
fn c07_malformed_mailboxes_rejected() {
    let _guard = crate::test_support::global_ctx_guard();
    let _fixture = Fixture::new();
    for mailbox in [
        "",
        "not-a-mailbox",
        "Name <to@example.test>",
        "*@example.test",
        "to@example.test\r\nBcc: other@example.test",
    ] {
        let mut req = request();
        req["recipient_emails"] = json!([mailbox]);
        require_rejection(req);
    }
}

fn assert_inner_mime(bytes: &[u8], html: &str, inline: bool) {
    let message = mail_parser::MessageParser::default()
        .parse(bytes)
        .expect("decrypted MIME");
    assert_eq!(message.body_text(0).as_deref(), Some("private text"));
    if !inline {
        assert_eq!(message.body_html(0).as_deref(), Some(html));
    } else {
        let body = message.body_html(0).unwrap();
        assert!(
            body.contains("cid:"),
            "inline image must be embedded in protected MIME"
        );
        assert!(!body.contains("data:image"));
    }
    let parts: Vec<_> = message.attachments().collect();
    let binary = parts
        .iter()
        .find(|part| part.attachment_name() == Some("secret.bin"))
        .expect("protected binary attachment");
    assert_eq!(binary.contents(), BINARY);
    let empty = parts
        .iter()
        .find(|part| part.attachment_name() == Some("empty.bin"))
        .expect("protected empty attachment");
    assert!(empty.contents().is_empty());
    if inline {
        assert!(parts
            .iter()
            .any(|part| part.contents() == b"inline-image-bytes"));
    }
}
fn protected_delivery(explicit: bool, secondary_san: bool, sign: bool, inline: bool) {
    let _guard = crate::test_support::global_ctx_guard();
    let fixture = Fixture::new();
    let identities: Vec<_> = MAILBOXES
        .iter()
        .map(|email| {
            Identity::new(
                &if secondary_san {
                    vec!["primary@example.test", *email]
                } else {
                    vec![*email]
                },
                "valid",
            )
        })
        .collect();
    let mut req = request();
    if explicit {
        req["recipient_certs_pem"] =
            json!(identities.iter().map(Identity::pem).collect::<Vec<_>>());
    } else {
        for identity in &identities {
            fixture.import(identity, "acc1");
        }
    }
    req["sign"] = json!(sign);
    if inline {
        req["body_html"] = json!(
            "<b>private HTML</b><img src=\"data:image/png;base64,aW5saW5lLWltYWdlLWJ5dGVz\">"
        );
    }
    let (ok, response) = encrypt(req.clone());
    assert!(ok, "{response}");
    let smime = response["smime"].as_str().unwrap();
    let outer = mail_parser::MessageParser::default()
        .parse(smime.as_bytes())
        .unwrap();
    assert!(outer.body_html(0).is_none());
    assert!(!smime.contains("secret.bin"));
    for identity in &identities {
        let mut plaintext = identity.decrypt(smime);
        if sign {
            let (signature, content) = Pkcs7::from_smime(&plaintext).expect("signed inner entity");
            let mut verified = Vec::new();
            signature
                .verify(
                    &Stack::new().unwrap(),
                    &X509StoreBuilder::new().unwrap().build(),
                    content.as_deref(),
                    Some(&mut verified),
                    Pkcs7Flags::NOVERIFY,
                )
                .expect("independent signature verification");
            plaintext = verified;
        }
        assert_inner_mime(&plaintext, req["body_html"].as_str().unwrap(), inline);
    }
}
#[test]
fn c07_imported_recipients_decrypt_complete_mime() {
    protected_delivery(false, false, false, false);
}
#[test]
fn c07_explicit_recipients_decrypt_complete_mime() {
    protected_delivery(true, false, false, false);
}
#[test]
fn c07_secondary_san_resolves_recipient() {
    protected_delivery(false, true, false, false);
}
#[test]
fn c07_inline_images_are_inside_encryption() {
    protected_delivery(true, false, false, true);
}
#[test]
fn c07_sign_then_encrypt_covers_complete_mime() {
    protected_delivery(true, false, true, false);
}
