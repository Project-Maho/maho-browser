use std::ffi::{CStr, CString};
use std::path::{Path, PathBuf};

use maho_core::maho_core::MahoCore;
use maho_ffi::import::{
    maho_import_string_free, maho_password_import_job_cancel, maho_password_import_job_commit_json,
    maho_password_import_job_free, maho_password_import_job_new,
    maho_password_import_job_preview_path_json, MahoPasswordImportJob,
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
        maho_storage::sqlite::set_sqlcipher_key("import-job-ffi-test-key")
            .expect("configure SQLCipher key");
        let profile =
            std::env::temp_dir().join(format!("maho-import-job-{}", uuid::Uuid::new_v4()));
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

fn list_logins(core: *mut MahoCore) -> Value {
    let request = request(serde_json::json!({
        "schemaVersion": 1,
        "provider": null,
        "kinds": ["login"],
        "cursor": null,
        "limit": 50,
    }));
    read_core_json(unsafe { maho_ffi::maho_vault_list_items_json(core, request.as_ptr()) })
}

#[test]
fn import_preview_ffi_is_secret_free_and_writes_only_after_commit() {
    let fixture = TestCore::new();
    fixture.initialize();
    let export = write_export(
        &fixture.profile,
        "onepassword.csv",
        "https://ffi.example",
        SECRET,
    );
    let job = ImportJob::new();

    let previewed = preview(&job, &export);

    assert_eq!(previewed["ok"], true);
    assert!(!previewed.to_string().contains(SECRET));
    assert_eq!(
        list_logins(fixture.core)["data"].as_array().unwrap().len(),
        0
    );

    let token = previewed["data"]["previewToken"].as_str().unwrap();
    let committed = commit(&job, fixture.core, token);

    assert_eq!(committed["ok"], true);
    assert_eq!(committed["data"]["committed"], 1);
    assert_eq!(committed["data"]["terminalResultCount"], 1);
    let listed = list_logins(fixture.core);
    assert_eq!(listed["data"].as_array().unwrap().len(), 1);
    assert!(!listed.to_string().contains(SECRET));
}

#[test]
fn import_preview_ffi_reselect_makes_previous_token_stale() {
    let fixture = TestCore::new();
    fixture.initialize();
    let first = write_export(
        &fixture.profile,
        "first.csv",
        "https://first.example",
        SECRET,
    );
    let second = write_export(
        &fixture.profile,
        "second.csv",
        "https://second.example",
        "SYNTH-FFI-IMPORT-PASSWORD-2",
    );
    let job = ImportJob::new();

    let first_preview = preview(&job, &first);
    let second_preview = preview(&job, &second);
    let old_token = first_preview["data"]["previewToken"].as_str().unwrap();
    let new_token = second_preview["data"]["previewToken"].as_str().unwrap();

    let stale = commit(&job, fixture.core, old_token);
    assert_eq!(stale["ok"], false);
    assert_eq!(stale["error"]["code"], "stale_preview_token");
    assert_eq!(
        list_logins(fixture.core)["data"].as_array().unwrap().len(),
        0
    );

    let committed = commit(&job, fixture.core, new_token);
    assert_eq!(committed["ok"], true);
    assert_eq!(
        list_logins(fixture.core)["data"].as_array().unwrap().len(),
        1
    );
}

#[test]
fn import_preview_ffi_cancel_clears_pending_preview() {
    let fixture = TestCore::new();
    fixture.initialize();
    let export = write_export(
        &fixture.profile,
        "cancel.csv",
        "https://cancel.example",
        SECRET,
    );
    let job = ImportJob::new();
    let previewed = preview(&job, &export);
    let token = previewed["data"]["previewToken"].as_str().unwrap();

    let cancelled = read_import_json(unsafe { maho_password_import_job_cancel(job.raw) });
    let committed = commit(&job, fixture.core, token);

    assert_eq!(cancelled["ok"], true);
    assert_eq!(cancelled["data"]["terminalResultCount"], 1);
    assert_eq!(committed["ok"], false);
    assert_eq!(committed["error"]["code"], "no_preview");
    assert_eq!(
        list_logins(fixture.core)["data"].as_array().unwrap().len(),
        0
    );
}

#[test]
fn import_preview_ffi_locked_vault_at_commit_time_writes_nothing() {
    let fixture = TestCore::new();
    fixture.initialize();
    let export = write_export(
        &fixture.profile,
        "locked.csv",
        "https://locked.example",
        SECRET,
    );
    let job = ImportJob::new();
    let previewed = preview(&job, &export);
    let token = previewed["data"]["previewToken"].as_str().unwrap();
    let locked = read_core_json(unsafe { maho_ffi::maho_vault_lock_json(fixture.core) });
    assert_eq!(locked["ok"], true);

    let committed = commit(&job, fixture.core, token);

    assert_eq!(committed["ok"], false);
    assert_eq!(committed["error"]["code"], "locked");
    let unlock = request(serde_json::json!({ "masterPassphrase": MASTER }));
    assert_eq!(
        read_core_json(unsafe { maho_ffi::maho_vault_unlock_json(fixture.core, unlock.as_ptr()) })
            ["ok"],
        true
    );
    assert_eq!(
        list_logins(fixture.core)["data"].as_array().unwrap().len(),
        0
    );
}
