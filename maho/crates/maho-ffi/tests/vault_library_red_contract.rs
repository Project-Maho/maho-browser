use std::ffi::{c_char, c_void, CStr, CString};
use std::path::{Path, PathBuf};

use maho_core::maho_core::MahoCore;
use serde_json::Value;

const MASTER: &str = "correct horse battery staple";
const RECOVERY: &str = "library-red-contract-recovery";
const SECRET_SENTINEL: &str = "S3NTINEL-maho-vault-library-red";

unsafe extern "C" {
    fn maho_vault_use_secret_json(core: *mut c_void, request_json: *const c_char) -> *mut c_char;
}

struct TestCore {
    core: *mut MahoCore,
    profile: PathBuf,
}

impl TestCore {
    fn new() -> Self {
        maho_storage::sqlite::set_sqlcipher_key("vault-library-red-test-key")
            .expect("configure SQLCipher key");
        let profile = std::env::temp_dir().join(format!("maho-vault-red-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir_all(&profile).expect("create test profile");
        let database = profile.join("vault.sqlite");
        let database = database.to_str().expect("temporary path is UTF-8");
        let core = Box::into_raw(Box::new(MahoCore::new().with_storage(database)));
        Self { core, profile }
    }
}

impl Drop for TestCore {
    fn drop(&mut self) {
        if !self.core.is_null() {
            // SAFETY: this fixture owns the core pointer exactly once.
            unsafe { maho_ffi::maho_core_free(self.core) };
            self.core = std::ptr::null_mut();
        }
        let _ = std::fs::remove_dir_all(&self.profile);
    }
}

fn request(value: Value) -> CString {
    CString::new(value.to_string()).expect("test JSON contains no NUL")
}

fn read_owned_json(pointer: *mut c_char) -> Value {
    assert!(!pointer.is_null(), "Vault JSON ABI returned null");
    // SAFETY: Vault JSON ABI returns a NUL-terminated string owned by the caller.
    let raw = unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .expect("Vault JSON ABI returns UTF-8")
        .to_owned();
    // SAFETY: this test consumes the returned allocation exactly once.
    unsafe { maho_ffi::maho_string_free(pointer) };
    serde_json::from_str(&raw).expect("Vault JSON ABI returns JSON")
}

fn initialize(core: *mut MahoCore) {
    let init = request(serde_json::json!({
        "masterPassphrase": MASTER,
        "recoverySecret": RECOVERY,
    }));
    // SAFETY: the fixture core and request are live for this FFI call.
    let response =
        read_owned_json(unsafe { maho_ffi::maho_vault_initialize_json(core, init.as_ptr()) });
    assert_eq!(response["ok"], true);
}

fn add_login(core: *mut MahoCore) -> Value {
    let add = request(serde_json::json!({
        "metadata": {
            "title": "Library red account",
            "origins": ["https://red.example"],
            "usernameHint": "ignored-by-core",
            "itemKind": "login",
            "totp": null,
            "passkey": null,
        },
        "username": "person@example.test",
        "password": SECRET_SENTINEL,
    }));
    // SAFETY: the fixture core and request are live for this FFI call.
    read_owned_json(unsafe { maho_ffi::maho_vault_add_login_json(core, add.as_ptr()) })
}

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(3)
        .expect("maho workspace root")
        .to_path_buf()
}

fn read_workspace_file(relative: &str) -> String {
    std::fs::read_to_string(workspace_root().join(relative)).expect("read workspace contract file")
}

fn assert_metadata_only_json(value: &Value) {
    assert!(
        !value.to_string().contains(SECRET_SENTINEL),
        "public Vault JSON leaked a secret sentinel"
    );
    assert!(value.get("password").is_none());
    assert!(value.get("secret").is_none());
}

// --- Red group: planned library-facing UseVaultSecret contract -------------

#[test]
fn vault_library_red_mojom_declares_secret_action_and_use_vault_secret() {
    // Given the sanctioned WebUI boundary is the settings mojom.
    let mojom =
        read_workspace_file("maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom");

    // When Task 7/8 are implemented, this contract names actions but never returns plaintext.
    assert!(
        mojom.contains("enum SecretAction"),
        "red: SecretAction must be added to the settings mojom"
    );
    assert!(
        mojom.contains("UseVaultSecret(") && mojom.contains("=> (VaultOperationResult result)"),
        "red: UseVaultSecret must return only VaultOperationResult"
    );
}

#[test]
fn vault_library_red_secret_use_ffi_returns_operation_result_without_plaintext() {
    // Given the library surface stores a login through the profile-owned Vault ABI.
    let fixture = TestCore::new();
    initialize(fixture.core);
    let added = add_login(fixture.core);
    assert_eq!(added["ok"], true);
    let use_secret = request(serde_json::json!({
        "itemId": added["data"]["id"],
        "expectedRevision": added["data"]["revision"],
        "action": "copy",
    }));

    // When the planned secret-use ABI is called, the response is a metadata-only operation result.
    // SAFETY: this is the planned Task 7 ABI; the test intentionally fails to link until implemented.
    let result = read_owned_json(unsafe {
        maho_vault_use_secret_json(fixture.core.cast::<c_void>(), use_secret.as_ptr())
    });

    // Then the browser-facing response reports success without returning the secret itself.
    assert_eq!(result["ok"], true);
    assert_metadata_only_json(&result);
}

#[test]
fn vault_library_red_handler_uses_status_only_secret_route_without_webui_plaintext_reveal() {
    // Given Settings WebUI must not expose a Mojo route that returns password strings.
    let handler = read_workspace_file(
        "maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc",
    );
    let helpers = read_workspace_file(
        "maho-chromium/browser/ui/webui/maho_settings/maho_settings_password_helpers.cc",
    );
    let legacy_reveal_method = ["Reveal", "Password"].concat();

    // When the library surface uses secrets, it must route through the status-only Vault path.
    assert!(
        handler.contains("MahoSettingsPageHandler::UseVaultSecret"),
        "red: handler must expose UseVaultSecret as the browser-process secret-use path"
    );
    assert!(
        !handler.contains(&legacy_reveal_method),
        "red: handler must not expose a plaintext password-returning reveal route"
    );
    assert!(
        helpers.contains("UseVaultSecretInCore"),
        "red: helper must call the Vault secret-use FFI instead of maho_core_reveal_password"
    );
}
