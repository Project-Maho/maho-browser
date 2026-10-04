use std::ffi::{CStr, CString};
use std::path::{Path, PathBuf};

use maho_core::maho_core::MahoCore;
use maho_ffi::import::{
    maho_import_string_free, maho_password_import_job_commit_json, maho_password_import_job_free,
    maho_password_import_job_new, maho_password_import_job_preview_path_json,
    MahoPasswordImportJob,
};
use serde_json::Value;

const MASTER: &str = "correct horse battery staple";
const RECOVERY: &str = "import-job-recovery";
const SECRET: &str = "SYNTH-FFI-IMPORT-PASSWORD";

struct TestCore {
    core: *mut MahoCore,
    profile: PathBuf,
}

impl TestCore {
    fn new() -> Self {
        maho_storage::sqlite::set_sqlcipher_key("import-audit-ffi-test-key")
            .expect("configure SQLCipher key");
        let profile =
            std::env::temp_dir().join(format!("maho-import-audit-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir_all(&profile).expect("create profile dir");
        let database = profile.join("vault.sqlite");
        let database = database.to_str().expect("temporary path is UTF-8");
        let core = unsafe { maho_ffi::maho_core_new_with_storage(c_string(database).as_ptr()) };
        assert!(!core.is_null());
        Self { core, profile }
    }

    fn initialize(&self) {
        let init = request(serde_json::json!({
            "masterPassphrase": MASTER,
            "recoverySecret": RECOVERY,
        }));
        let response = read_core_json(unsafe {
            maho_ffi::maho_vault_initialize_json(self.core, init.as_ptr())
        });
        assert_eq!(response["ok"], true);
    }
}

impl Drop for TestCore {
    fn drop(&mut self) {
        if !self.core.is_null() {
            unsafe { maho_ffi::maho_core_free(self.core) };
            self.core = std::ptr::null_mut();
        }
        let _ = std::fs::remove_dir_all(&self.profile);
    }
}

struct ImportJob {
    raw: *mut MahoPasswordImportJob,
}

impl ImportJob {
    fn new() -> Self {
        let raw = unsafe { maho_password_import_job_new() };
        assert!(!raw.is_null());
        Self { raw }
    }
}

impl Drop for ImportJob {
    fn drop(&mut self) {
        unsafe { maho_password_import_job_free(self.raw) };
    }
}

fn c_string(value: &str) -> CString {
    CString::new(value).expect("test string contains no NUL")
}

fn request(value: Value) -> CString {
    c_string(&value.to_string())
}

fn read_import_json(pointer: *mut std::ffi::c_char) -> Value {
    assert!(!pointer.is_null(), "import job ABI returned null");
    let raw = unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .expect("import job ABI returns UTF-8")
        .to_owned();
    unsafe { maho_import_string_free(pointer) };
    serde_json::from_str(&raw).expect("import job ABI returns JSON")
}

fn read_core_json(pointer: *mut std::ffi::c_char) -> Value {
    assert!(!pointer.is_null(), "core ABI returned null");
    let raw = unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .expect("core ABI returns UTF-8")
        .to_owned();
    unsafe { maho_ffi::maho_string_free(pointer) };
    serde_json::from_str(&raw).expect("core ABI returns JSON")
}

fn write_export(dir: &Path, name: &str, origin: &str, secret: &str) -> PathBuf {
    let path = dir.join(name);
    std::fs::write(
        &path,
        format!(
            "Title,Website,Username,Password,One-time password,Favorite status,Archived status,Tags,Notes\n\
             Example,{origin},user@example.test,{secret},,false,false,synthetic,Synthetic note\n"
        ),
    )
    .expect("write synthetic export");
    path
}

fn preview(job: &ImportJob, path: &Path) -> Value {
    let request = request(serde_json::json!({
        "sourceFormat": "one_password_csv",
        "path": path.to_string_lossy(),
    }));
    read_import_json(unsafe {
        maho_password_import_job_preview_path_json(job.raw, request.as_ptr())
    })
}

fn commit(job: &ImportJob, core: *mut MahoCore, token: &str) -> Value {
    let request = request(serde_json::json!({ "previewToken": token }));
    read_import_json(unsafe {
        maho_password_import_job_commit_json(job.raw, core, request.as_ptr())
    })
}

fn audit_rows(core: *mut MahoCore) -> Vec<maho_storage::sqlite::VaultAuditRow> {
    assert!(!core.is_null(), "core pointer is live");
    // SAFETY: TestCore owns this pointer for the duration of the test.
    let core = unsafe { &*core };
    core.storage_ref()
        .expect("storage configured")
        .list_vault_audit_events()
        .expect("read vault audit rows")
}

fn assert_audit_rows_are_payload_safe(rows: &[maho_storage::sqlite::VaultAuditRow]) {
    let rendered = format!("{rows:?}");
    assert!(
        !rendered.contains(SECRET),
        "audit rows must not contain imported secrets"
    );
    for row in rows {
        assert!(row.item_id.is_none(), "audit item_id must stay empty");
        assert!(row.item_alias.is_none(), "audit item_alias must stay empty");
    }
}

#[test]
fn vault_import_commit_ffi_emits_success_failed_and_denied_audit_rows_without_secrets() {
    let fixture = TestCore::new();
    fixture.initialize();
    let success_export = write_export(
        &fixture.profile,
        "audit-success.csv",
        "https://audit-success.example",
        SECRET,
    );
    let success_job = ImportJob::new();
    let success_preview = preview(&success_job, &success_export);
    let success_token = success_preview["data"]["previewToken"].as_str().unwrap();

    let committed = commit(&success_job, fixture.core, success_token);

    assert_eq!(committed["ok"], true);
    let stale_first = write_export(
        &fixture.profile,
        "audit-stale-first.csv",
        "https://audit-stale-first.example",
        "SYNTH-FFI-IMPORT-PASSWORD-stale-1",
    );
    let stale_second = write_export(
        &fixture.profile,
        "audit-stale-second.csv",
        "https://audit-stale-second.example",
        "SYNTH-FFI-IMPORT-PASSWORD-stale-2",
    );
    let stale_job = ImportJob::new();
    let stale_preview = preview(&stale_job, &stale_first);
    let stale_token = stale_preview["data"]["previewToken"].as_str().unwrap();
    let _replacement_preview = preview(&stale_job, &stale_second);

    let stale = commit(&stale_job, fixture.core, stale_token);

    assert_eq!(stale["ok"], false);
    assert_eq!(stale["error"]["code"], "stale_preview_token");
    let locked_export = write_export(
        &fixture.profile,
        "audit-locked.csv",
        "https://audit-locked.example",
        "SYNTH-FFI-IMPORT-PASSWORD-locked",
    );
    let locked_job = ImportJob::new();
    let locked_preview = preview(&locked_job, &locked_export);
    let locked_token = locked_preview["data"]["previewToken"].as_str().unwrap();
    let locked = read_core_json(unsafe { maho_ffi::maho_vault_lock_json(fixture.core) });
    assert_eq!(locked["ok"], true);

    let denied = commit(&locked_job, fixture.core, locked_token);

    assert_eq!(denied["ok"], false);
    assert_eq!(denied["error"]["code"], "locked");
    let rows = audit_rows(fixture.core);
    let import_commit_rows = rows
        .iter()
        .filter(|row| row.operation == "import_commit")
        .collect::<Vec<_>>();
    assert_eq!(
        import_commit_rows
            .iter()
            .map(|row| row.decision.as_str())
            .collect::<Vec<_>>(),
        vec!["allowed", "failed", "denied"]
    );
    assert!(rows.iter().any(|row| row.operation == "item_created"));
    assert_audit_rows_are_payload_safe(&rows);
}
