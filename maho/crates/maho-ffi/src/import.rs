//! FFI surface for browser data import.
//!
//! Phase 2 surface (post-cleanup): 5 functions
//! - `maho_import_detect_browsers` — enumerate installed browsers
//! - `maho_import_orchestrator_start` — kick off the async import session
//! - `maho_import_orchestrator_cancel` — cooperative cancel
//! - `maho_import_orchestrator_free` — free session handle after terminal callback
//! - `maho_import_string_free` — free strings returned by detect_browsers

use std::ffi::{c_char, CStr, CString};
use std::ptr;

use maho_core::vault_manager::VaultLoginInput;
use maho_import::detect::detect_installed_browsers;
use maho_import::{
    PasswordImportCommitDestination, PasswordImportCommitError, PasswordImportInput,
    PasswordImportJob, PasswordImportJobConfig, PasswordImportJobError,
    PasswordImportPreviewReceipt, PasswordImportSource, PasswordImportSourceFormat,
};
use maho_types::vault::{
    CredentialOrigin, VaultAuditDecision, VaultErrorCode, VaultItemKind, VaultItemPublicMetadata,
    VaultLockState,
};

fn to_json_cstring<T: serde::Serialize>(val: &T) -> *mut c_char {
    match serde_json::to_string(val) {
        Ok(json) => CString::new(json)
            .map(CString::into_raw)
            .unwrap_or(ptr::null_mut()),
        Err(_) => ptr::null_mut(),
    }
}

/// Detect installed browsers on the system. Returns a JSON array; caller MUST
/// free the returned string via `maho_import_string_free`.
///
/// # Safety
/// No pointer arguments. Returns a heap-allocated JSON string; caller must free it via
/// `maho_import_string_free`. Returns null on allocation failure.
#[no_mangle]
pub unsafe extern "C" fn maho_import_detect_browsers() -> *mut c_char {
    crate::ffi_safe!(
        {
            let browsers = detect_installed_browsers();
            to_json_cstring(&browsers)
        },
        ptr::null_mut()
    )
}

/// Free a string returned by `maho_import_detect_browsers`.
///
/// # Safety
/// `s` must be a pointer returned by `maho_import_detect_browsers`, or null (no-op). Must not
/// be called more than once per pointer.
#[no_mangle]
pub unsafe extern "C" fn maho_import_string_free(s: *mut c_char) {
    crate::ffi_safe!(
        {
            if !s.is_null() {
                drop(CString::from_raw(s));
            }
        },
        ()
    )
}

use std::ffi::c_void;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::Arc;
use std::sync::Mutex;
use std::thread;

use maho_core::maho_core::MahoCore;
use maho_import::orchestrator::{ImportProgress, ImportType, Orchestrator, IMPORT_ESSENTIAL_BIT};
use maho_import::{DetectedBrowser, ImportServices};

use crate::import_destination::MahoCoreDestination;

/// Opaque handle for an in-flight import session. Created by
/// `maho_import_orchestrator_start`, freed by `maho_import_orchestrator_free`.
/// The caller MUST call `free` after receiving the terminal callback
/// (`AllComplete` or `Error`).
pub struct MahoImportSession {
    cancel_token: Arc<AtomicBool>,
    thread: Option<thread::JoinHandle<()>>,
}

pub struct MahoPasswordImportJob {
    job: Mutex<PasswordImportJob>,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct PasswordImportPreviewPathRequest {
    source_format: String,
    path: String,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct PasswordImportCommitRequest {
    preview_token: String,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct PasswordImportFfiResponse<T> {
    ok: bool,
    data: Option<T>,
    error: Option<PasswordImportFfiError>,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct PasswordImportFfiError {
    code: &'static str,
    message: String,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct PasswordImportPreviewData {
    preview_token: String,
    source: &'static str,
    file_format: &'static str,
    source_format: &'static str,
    source_label: &'static str,
    imported: usize,
    skipped: usize,
    duplicates: usize,
    blank_passwords: usize,
    unsupported_fields: usize,
    safe_messages: Vec<String>,
    safe_errors: Vec<String>,
    terminal_result_count: u8,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct PasswordImportCommitData {
    committed: usize,
    failed: usize,
    terminal_result_count: u8,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct PasswordImportCancelData {
    terminal_result_count: u8,
}

struct VaultPasswordImportDestination<'a> {
    core: &'a mut MahoCore,
}

impl PasswordImportCommitDestination for VaultPasswordImportDestination<'_> {
    fn add_login(
        &mut self,
        credential: maho_import::PasswordImportCredential,
    ) -> Result<(), PasswordImportCommitError> {
        let origin_url = credential.origin_url;
        let origin = CredentialOrigin::try_from(origin_url.as_str()).map_err(|_| {
            PasswordImportCommitError::DestinationRejected("invalid_origin".to_string())
        })?;
        let username = credential.username;
        let input = VaultLoginInput {
            metadata: VaultItemPublicMetadata {
                favorite: false,
                trashed_at: None,
                has_notes: false,
                title: origin_url,
                origins: vec![origin],
                username_hint: username.clone(),
                item_kind: VaultItemKind::Login,
                totp: None,
                passkey: None,
            },
            username,
            password: credential.password,
            form_details: None,
        };
        self.core
            .vault_add_login(input)
            .map(|_| ())
            .map_err(|error| {
                if matches!(error, maho_core::vault_manager::VaultCrudError::Locked) {
                    PasswordImportCommitError::DestinationLocked
                } else {
                    PasswordImportCommitError::DestinationRejected(error.to_string())
                }
            })
    }
}

/// Pending user-provided passwords CSV path, set via
/// `maho_import_orchestrator_set_password_csv` before `..._start`.
///
/// A process-global is sound here because the import gate allows only one
/// import session at a time; `start` takes (clears) the value so it is consumed
/// exactly once per session.
static PENDING_PASSWORD_CSV: std::sync::OnceLock<std::sync::Mutex<Option<std::path::PathBuf>>> =
    std::sync::OnceLock::new();

fn pending_password_csv_slot() -> &'static std::sync::Mutex<Option<std::path::PathBuf>> {
    PENDING_PASSWORD_CSV.get_or_init(|| std::sync::Mutex::new(None))
}

fn take_pending_password_csv() -> Option<std::path::PathBuf> {
    pending_password_csv_slot()
        .lock()
        .ok()
        .and_then(|mut g| g.take())
}

#[no_mangle]
pub unsafe extern "C" fn maho_password_import_job_new() -> *mut MahoPasswordImportJob {
    crate::ffi_safe!(
        {
            Box::into_raw(Box::new(MahoPasswordImportJob {
                job: Mutex::new(PasswordImportJob::default()),
            }))
        },
        ptr::null_mut()
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_password_import_job_free(handle: *mut MahoPasswordImportJob) {
    crate::ffi_safe!(
        {
            if !handle.is_null() {
                drop(Box::from_raw(handle));
            }
        },
        ()
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_password_import_job_cancel(
    handle: *mut MahoPasswordImportJob,
) -> *mut c_char {
    crate::ffi_safe!(
        {
            let Some(handle) = handle.as_ref() else {
                return password_import_error("invalid_handle", "import job handle is required");
            };
            let Ok(mut job) = handle.job.lock() else {
                return password_import_error(
                    "state_unavailable",
                    "import job state is unavailable",
                );
            };
            let summary = job.cancel();
            password_import_ok(PasswordImportCancelData {
                terminal_result_count: summary.terminal_result_count,
            })
        },
        ptr::null_mut()
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_password_import_job_preview_path_json(
    handle: *mut MahoPasswordImportJob,
    request_json: *const c_char,
) -> *mut c_char {
    crate::ffi_safe!(
        {
            let Some(handle) = handle.as_ref() else {
                return password_import_error("invalid_handle", "import job handle is required");
            };
            let request = match parse_password_import_request::<PasswordImportPreviewPathRequest>(
                request_json,
            ) {
                Ok(request) => request,
                Err(response) => return response,
            };
            let Some(source_format) = source_format_from_token(&request.source_format) else {
                return password_import_error("invalid_source_format", "unsupported source format");
            };
            let config = PasswordImportJobConfig::new(
                source_format,
                PasswordImportInput::Path(std::path::PathBuf::from(request.path)),
            );
            let Ok(mut job) = handle.job.lock() else {
                return password_import_error(
                    "state_unavailable",
                    "import job state is unavailable",
                );
            };
            match job.preview(config) {
                Ok(receipt) => password_import_ok(preview_data(receipt)),
                Err(error) => password_import_job_error(error),
            }
        },
        ptr::null_mut()
    )
}

#[no_mangle]
pub unsafe extern "C" fn maho_password_import_job_commit_json(
    handle: *mut MahoPasswordImportJob,
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    crate::ffi_safe!(
        {
            let Some(handle) = handle.as_ref() else {
                return password_import_error("invalid_handle", "import job handle is required");
            };
            if crate::import_gate::is_active() {
                return password_import_error("import_active", "browser import is already active");
            }
            let Some(core) = core.as_mut() else {
                return password_import_error("core_unavailable", "core is unavailable");
            };
            if core.vault_status().lock_state != VaultLockState::Unlocked {
                if let Err(response) = record_import_commit_audit(
                    core,
                    VaultAuditDecision::Denied,
                    Some(VaultErrorCode::Locked),
                ) {
                    return response;
                }
                return password_import_error("locked", "Vault is locked");
            }
            let request =
                match parse_password_import_request::<PasswordImportCommitRequest>(request_json) {
                    Ok(request) => request,
                    Err(response) => {
                        if let Err(audit_response) = record_import_commit_audit(
                            core,
                            VaultAuditDecision::Failed,
                            Some(VaultErrorCode::InvalidGrant),
                        ) {
                            return audit_response;
                        }
                        return response;
                    }
                };
            let Ok(mut job) = handle.job.lock() else {
                if let Err(response) = record_import_commit_audit(
                    core,
                    VaultAuditDecision::Failed,
                    Some(VaultErrorCode::StorageFailure),
                ) {
                    return response;
                }
                return password_import_error(
                    "state_unavailable",
                    "import job state is unavailable",
                );
            };
            let commit_result = {
                let mut destination = VaultPasswordImportDestination { core };
                job.commit(&request.preview_token, &mut destination)
            };
            match commit_result {
                Ok(summary) => {
                    if let Err(response) =
                        record_import_commit_audit(core, VaultAuditDecision::Allowed, None)
                    {
                        return response;
                    }
                    password_import_ok(PasswordImportCommitData {
                        committed: summary.committed,
                        failed: summary.failed,
                        terminal_result_count: summary.terminal_result_count,
                    })
                }
                Err(error) => {
                    let (decision, reason) = password_import_audit_outcome(&error);
                    if let Err(response) = record_import_commit_audit(core, decision, reason) {
                        return response;
                    }
                    password_import_job_error(error)
                }
            }
        },
        ptr::null_mut()
    )
}

fn record_import_commit_audit(
    core: &MahoCore,
    decision: VaultAuditDecision,
    reason: Option<VaultErrorCode>,
) -> Result<(), *mut c_char> {
    core.vault_record_import_commit_audit(decision, reason)
        .map_err(|error| password_import_error("audit_failed", error.to_string()))
}

fn password_import_audit_outcome(
    error: &PasswordImportJobError,
) -> (VaultAuditDecision, Option<VaultErrorCode>) {
    match error {
        PasswordImportJobError::DestinationLocked => {
            (VaultAuditDecision::Denied, Some(VaultErrorCode::Locked))
        }
        PasswordImportJobError::NoPreview | PasswordImportJobError::StalePreviewToken => (
            VaultAuditDecision::Failed,
            Some(VaultErrorCode::InvalidGrant),
        ),
        PasswordImportJobError::InvalidFile(_) | PasswordImportJobError::CommitFailed(_) => (
            VaultAuditDecision::Failed,
            Some(VaultErrorCode::StorageFailure),
        ),
    }
}

fn password_import_ok<T: serde::Serialize>(data: T) -> *mut c_char {
    to_json_cstring(&PasswordImportFfiResponse {
        ok: true,
        data: Some(data),
        error: None,
    })
}

fn password_import_error(code: &'static str, message: impl Into<String>) -> *mut c_char {
    to_json_cstring(&PasswordImportFfiResponse::<()> {
        ok: false,
        data: None,
        error: Some(PasswordImportFfiError {
            code,
            message: message.into(),
        }),
    })
}

fn password_import_job_error(error: PasswordImportJobError) -> *mut c_char {
    match error {
        PasswordImportJobError::InvalidFile(message) => {
            password_import_error("invalid_file", message)
        }
        PasswordImportJobError::NoPreview => password_import_error("no_preview", error.to_string()),
        PasswordImportJobError::StalePreviewToken => {
            password_import_error("stale_preview_token", error.to_string())
        }
        PasswordImportJobError::DestinationLocked => {
            password_import_error("locked", error.to_string())
        }
        PasswordImportJobError::CommitFailed(message) => {
            password_import_error("commit_failed", message)
        }
    }
}

unsafe fn parse_password_import_request<T: serde::de::DeserializeOwned>(
    request_json: *const c_char,
) -> Result<T, *mut c_char> {
    if request_json.is_null() {
        return Err(password_import_error(
            "invalid_request",
            "request JSON is required",
        ));
    }
    let request = match CStr::from_ptr(request_json).to_str() {
        Ok(value) => value,
        Err(_) => return Err(password_import_error("invalid_request", "invalid UTF-8")),
    };
    serde_json::from_str(request)
        .map_err(|error| password_import_error("invalid_request", error.to_string()))
}

fn preview_data(receipt: PasswordImportPreviewReceipt) -> PasswordImportPreviewData {
    PasswordImportPreviewData {
        preview_token: receipt.preview_token,
        source: source_token(receipt.source),
        file_format: file_format_token(receipt.file_format),
        source_format: source_format_token(receipt.source_format),
        source_label: receipt.source_label,
        imported: receipt.preview.imported,
        skipped: receipt.preview.skipped,
        duplicates: receipt.preview.duplicates,
        blank_passwords: receipt.preview.blank_passwords,
        unsupported_fields: receipt.preview.unsupported_fields,
        safe_messages: receipt.preview.safe_messages,
        safe_errors: receipt.preview.safe_errors,
        terminal_result_count: receipt.terminal_result_count,
    }
}

fn source_format_from_token(token: &str) -> Option<PasswordImportSourceFormat> {
    match token {
        "one_password_csv" => Some(PasswordImportSourceFormat::OnePasswordCsv),
        "one_password_1pux" => Some(PasswordImportSourceFormat::OnePasswordPux),
        "bitwarden_individual_csv" => Some(PasswordImportSourceFormat::BitwardenIndividualCsv),
        "bitwarden_organization_csv" => Some(PasswordImportSourceFormat::BitwardenOrganizationCsv),
        "bitwarden_json" => Some(PasswordImportSourceFormat::BitwardenJson),
        "apple_passwords_csv" => Some(PasswordImportSourceFormat::ApplePasswordsCsv),
        "keepassxc_csv" => Some(PasswordImportSourceFormat::KeePassXcCsv),
        "keepass_classic_csv" => Some(PasswordImportSourceFormat::KeePassClassicCsv),
        _ => None,
    }
}

fn source_format_token(source_format: PasswordImportSourceFormat) -> &'static str {
    match source_format {
        PasswordImportSourceFormat::OnePasswordCsv => "one_password_csv",
        PasswordImportSourceFormat::OnePasswordPux => "one_password_1pux",
        PasswordImportSourceFormat::BitwardenIndividualCsv => "bitwarden_individual_csv",
        PasswordImportSourceFormat::BitwardenOrganizationCsv => "bitwarden_organization_csv",
        PasswordImportSourceFormat::BitwardenJson => "bitwarden_json",
        PasswordImportSourceFormat::ApplePasswordsCsv => "apple_passwords_csv",
        PasswordImportSourceFormat::KeePassXcCsv => "keepassxc_csv",
        PasswordImportSourceFormat::KeePassClassicCsv => "keepass_classic_csv",
    }
}

fn source_token(source: PasswordImportSource) -> &'static str {
    match source {
        PasswordImportSource::OnePassword => "one_password",
        PasswordImportSource::Bitwarden => "bitwarden",
        PasswordImportSource::ApplePasswords => "apple_passwords",
        PasswordImportSource::KeePassXc => "keepassxc",
        PasswordImportSource::KeePassClassic => "keepass_classic",
    }
}

fn file_format_token(file_format: maho_import::PasswordImportFileFormat) -> &'static str {
    match file_format {
        maho_import::PasswordImportFileFormat::Csv => "csv",
        maho_import::PasswordImportFileFormat::OnePux => "1pux",
        maho_import::PasswordImportFileFormat::Json => "json",
    }
}

/// Progress callback type invoked from the import background thread.
///
/// Parameters:
///  - `kind`:  0=Starting, 1=Update, 2=TypeComplete, 3=AllComplete, 4=Error
///  - `import_type`: bitmask bit identifying the import type
///       (1=History, 2=Bookmarks, 4=Passwords, 8=Autofill, 16=Cookies, 32=Workspaces, 64=Favicons, 128=Essential)
///  - `count`: item count (for Update/TypeComplete) or 0
///  - `message`: null-terminated status message or NULL
///  - `user_data`: opaque pointer passed back unchanged
///
/// SAFETY: callback must be safe to invoke from a non-main thread. `message`
/// pointer is only valid for the duration of the callback call. The caller is
/// responsible for keeping `user_data` valid until the terminal event fires.
pub type MahoImportProgressCallback = unsafe extern "C" fn(
    kind: u32,
    import_type: u32,
    count: i32,
    message: *const c_char,
    user_data: *mut c_void,
);

pub type MahoImportCookieCallback = unsafe extern "C" fn(
    host: *const c_char,
    name: *const c_char,
    value: *const c_char,
    path: *const c_char,
    expires: i64,
    is_secure: bool,
    is_httponly: bool,
    same_site: i32,
    user_data: *mut c_void,
);

pub type MahoImportAutofillCallback = unsafe extern "C" fn(
    field_name: *const c_char,
    value: *const c_char,
    times_used: i32,
    first_used: i64,
    last_used: i64,
    user_data: *mut c_void,
);

pub type MahoImportFaviconCallback = unsafe extern "C" fn(
    url: *const c_char,
    png_bytes: *const u8,
    png_len: usize,
    user_data: *mut c_void,
);

/// Start an import orchestrator session on a background thread.
///
/// # Parameters
/// - `core_ptr`: valid `MahoCore*` (from `maho_core_new_with_storage`). Must
///   remain valid and unaccessed by other threads until the terminal callback.
/// - `browser_json`: JSON-serialized `DetectedBrowser` entry.
/// - `items_bitmask`: which import types to run (OR of `ImportServices` bits).
/// - `essentials_json`: JSON array of URL strings for essential import, or NULL.
/// - `callback`: progress callback invoked from the background thread.
/// - `cookie_cb`: callback invoked when a cookie is imported.
/// - `autofill_cb`: callback invoked when an autofill entry is imported.
/// - `favicon_cb`: callback invoked when a favicon is imported.
/// - `user_data`: opaque pointer forwarded to callback.
///
/// # Safety
/// All pointer parameters (except `user_data`) must be valid for the duration
/// of this call. `core_ptr` must remain valid until the terminal callback fires.
#[no_mangle]
pub unsafe extern "C" fn maho_import_orchestrator_start(
    core_ptr: *mut MahoCore,
    browser_json: *const c_char,
    items_bitmask: u32,
    essentials_json: *const c_char,
    callback: MahoImportProgressCallback,
    cookie_cb: Option<
        unsafe extern "C" fn(
            host: *const c_char,
            name: *const c_char,
            value: *const c_char,
            path: *const c_char,
            expires: i64,
            is_secure: bool,
            is_httponly: bool,
            same_site: i32,
            user_data: *mut c_void,
        ),
    >,
    autofill_cb: Option<
        unsafe extern "C" fn(
            field_name: *const c_char,
            value: *const c_char,
            times_used: i32,
            first_used: i64,
            last_used: i64,
            user_data: *mut c_void,
        ),
    >,
    favicon_cb: Option<
        unsafe extern "C" fn(
            url: *const c_char,
            png_bytes: *const u8,
            png_len: usize,
            user_data: *mut c_void,
        ),
    >,
    user_data: *mut c_void,
) -> *mut MahoImportSession {
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if core_ptr.is_null() || browser_json.is_null() {
            return ptr::null_mut();
        }

        // SAFETY(H15): Acquire the import gate so concurrent FFI calls on MahoCore
        // are rejected for the duration of this import session.
        if !crate::with_ffi_serialization(crate::import_gate::enter) {
            // Another import is already active.
            return ptr::null_mut();
        }

        let browser_str = match CStr::from_ptr(browser_json).to_str() {
            Ok(s) => s,
            Err(_) => {
                crate::import_gate::exit();
                return ptr::null_mut();
            }
        };
        let browser: DetectedBrowser = match serde_json::from_str(browser_str) {
            Ok(b) => b,
            Err(_) => {
                crate::import_gate::exit();
                return ptr::null_mut();
            }
        };

        let essentials: Vec<String> = if essentials_json.is_null() {
            Vec::new()
        } else {
            match CStr::from_ptr(essentials_json).to_str() {
                Ok(s) => serde_json::from_str(s).unwrap_or_default(),
                Err(_) => Vec::new(),
            }
        };

        let destination = Arc::new(MahoCoreDestination::new_with_callbacks(
            core_ptr,
            cookie_cb,
            autofill_cb,
            favicon_cb,
            user_data,
        ));
        let cancel_token = Arc::new(AtomicBool::new(false));
        let cancel_clone = cancel_token.clone();
        let ctx = CallbackContext {
            callback,
            user_data,
        };
        let password_csv_path = take_pending_password_csv();

        let handle = thread::Builder::new()
            .name("maho-import-orchestrator".into())
            .spawn(move || {
                let gate = crate::import_gate::GateGuard;
                let mut orchestrator = Orchestrator::new(destination);
                if !essentials.is_empty() {
                    orchestrator.set_selected_essentials(essentials);
                }
                if let Some(path) = password_csv_path {
                    orchestrator.set_password_csv_path(path);
                }
                let orchestrator = Arc::new(orchestrator);
                let orchestrator_worker = orchestrator.clone();

                let (tx, rx) = mpsc::channel();
                let browser_for_worker = browser.clone();

                let worker = thread::Builder::new()
                    .name("maho-import-worker".into())
                    .spawn(move || {
                        let _ = orchestrator_worker.start_import(
                            &browser_for_worker,
                            items_bitmask,
                            tx,
                        );
                    })
                    .ok();

                let terminal_event: Option<ImportProgress> = loop {
                    if cancel_clone.load(Ordering::SeqCst) {
                        orchestrator.cancel();
                        break Some(ImportProgress::AllComplete);
                    }
                    match rx.recv_timeout(std::time::Duration::from_millis(100)) {
                        Ok(progress) => {
                            if matches!(
                                progress,
                                ImportProgress::AllComplete | ImportProgress::Error { .. }
                            ) {
                                // Terminal progress is held back: the gate must
                                // be released BEFORE the caller learns the import
                                // ended, or its very next FFI call is rejected.
                                break Some(progress);
                            }
                            ctx.forward_progress(&progress);
                        }
                        Err(mpsc::RecvTimeoutError::Timeout) => {
                            // Loop again to check cancellation
                        }
                        Err(mpsc::RecvTimeoutError::Disconnected) => {
                            // Drain remaining events and exit
                            let mut terminal = None;
                            for progress in rx.try_iter() {
                                if matches!(
                                    progress,
                                    ImportProgress::AllComplete | ImportProgress::Error { .. }
                                ) {
                                    terminal = Some(progress);
                                } else {
                                    ctx.forward_progress(&progress);
                                }
                            }
                            break terminal;
                        }
                    }
                };

                if let Some(h) = worker {
                    let _ = h.join();
                }

                // Release the import gate before the terminal callback fires:
                // C++ resumes FFI calls as soon as it sees the terminal event,
                // and every one of them would short-circuit on is_active() if
                // the GateGuard were still held here (it would only drop at
                // thread exit, after the callback).
                drop(gate);
                match terminal_event {
                    Some(progress) => ctx.forward_progress(&progress),
                    // Defensive: no terminal progress arrived (e.g. the worker
                    // panicked mid-import). The caller still needs one terminal
                    // callback, and it must fire with the gate released.
                    std::option::Option::None => ctx.invoke(3, 0, 0, ptr::null()),
                }
            })
            .ok();

        match handle {
            Some(h) => {
                let session = Box::new(MahoImportSession {
                    cancel_token,
                    thread: Some(h),
                });
                Box::into_raw(session)
            }
            std::option::Option::None => {
                crate::import_gate::exit();
                ptr::null_mut()
            }
        }
    })) {
        Ok(session) => session,
        Err(_) => {
            crate::import_gate::exit();
            ptr::null_mut()
        }
    }
}

/// Cancel an in-flight import. Safe to call from any thread. The cancellation
/// is cooperative — the orchestrator checks the flag between import types.
///
/// # Safety
/// `handle` must be a valid pointer returned by `maho_import_orchestrator_start`,
/// or NULL (no-op).
#[no_mangle]
pub unsafe extern "C" fn maho_import_orchestrator_cancel(handle: *mut MahoImportSession) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        let session = &*handle;
        session.cancel_token.store(true, Ordering::SeqCst);
    }));
}

/// Free the import session handle. MUST be called after the terminal callback
/// (`AllComplete` or `Error`) has fired. Blocks until the background thread
/// finishes (should be near-instant after terminal event).
///
/// # Safety
/// `handle` must be a valid pointer returned by `maho_import_orchestrator_start`,
/// or NULL (no-op). Must not be called more than once per handle.
#[no_mangle]
pub unsafe extern "C" fn maho_import_orchestrator_free(handle: *mut MahoImportSession) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        let mut session = Box::from_raw(handle);
        if let Some(thread) = session.thread.take() {
            let _ = thread.join();
        }
    }));
}

/// Set the user-provided passwords CSV path for the next import session.
///
/// Additive companion to `maho_import_orchestrator_start`: call this BEFORE
/// `..._start` when the user selects a Safari-exported passwords CSV. Passing
/// NULL clears any pending path. The value is consumed by the next `..._start`.
///
/// # Safety
/// `path` must be a valid null-terminated UTF-8 C string, or NULL (clears).
#[no_mangle]
pub unsafe extern "C" fn maho_import_orchestrator_set_password_csv(path: *const c_char) {
    crate::ffi_safe!(
        {
            let slot = pending_password_csv_slot();
            let Ok(mut guard) = slot.lock() else {
                return;
            };
            if path.is_null() {
                *guard = None;
                return;
            }
            match CStr::from_ptr(path).to_str() {
                Ok(s) => *guard = Some(std::path::PathBuf::from(s)),
                Err(_) => *guard = None,
            }
        },
        ()
    )
}

struct CallbackContext {
    callback: MahoImportProgressCallback,
    user_data: *mut c_void,
}

// SAFETY: caller guarantees `user_data` validity until the terminal event.
// Function pointers are trivially thread-safe.
unsafe impl Send for CallbackContext {}

impl CallbackContext {
    fn invoke(&self, kind: u32, import_type: u32, count: i32, message: *const c_char) {
        unsafe {
            (self.callback)(kind, import_type, count, message, self.user_data);
        }
    }

    fn forward_progress(&self, progress: &ImportProgress) {
        match progress {
            ImportProgress::Starting { import_type } => {
                self.invoke(0, import_type_to_bit(*import_type), 0, ptr::null());
            }
            ImportProgress::Update {
                import_type,
                imported,
                status,
            } => {
                let msg = CString::new(status.as_str()).ok();
                let msg_ptr = msg.as_ref().map(|m| m.as_ptr()).unwrap_or(ptr::null());
                self.invoke(
                    1,
                    import_type_to_bit(*import_type),
                    *imported as i32,
                    msg_ptr,
                );
            }
            ImportProgress::TypeComplete { import_type, count } => {
                self.invoke(
                    2,
                    import_type_to_bit(*import_type),
                    *count as i32,
                    ptr::null(),
                );
            }
            ImportProgress::AllComplete => {
                self.invoke(3, 0, 0, ptr::null());
            }
            ImportProgress::Error { message } => {
                let msg = CString::new(message.as_str()).ok();
                let msg_ptr = msg.as_ref().map(|m| m.as_ptr()).unwrap_or(ptr::null());
                self.invoke(4, 0, 0, msg_ptr);
            }
        }
    }
}

#[cfg(test)]
mod review_tests {
    use super::*;

    #[test]
    fn review_terminal_import_callback_releases_gate() {
        unsafe extern "C" fn progress(kind: u32, _: u32, _: i32, _: *const c_char, data: *mut c_void) {
            if kind == 3 || kind == 4 {
                let tx = &*data.cast::<mpsc::Sender<bool>>();
                tx.send(!crate::import_gate::is_active()).unwrap();
            }
        }
        let mut core = Box::new(MahoCore::new());
        let browser = CString::new(serde_json::json!({
            "browser_type": "Chrome", "display_name": "fixture", "profile_path": "/unused",
            "services_supported": 0, "requires_full_disk_access": false
        }).to_string()).unwrap();
        let (tx, rx) = mpsc::channel::<bool>();
        unsafe {
            let handle = maho_import_orchestrator_start(
                core.as_mut(), browser.as_ptr(), 0, ptr::null(), progress,
                None, None, None, (&tx as *const mpsc::Sender<bool>).cast_mut().cast(),
            );
            assert!(!handle.is_null());
            let released = rx.recv_timeout(std::time::Duration::from_secs(10));
            maho_import_orchestrator_free(handle);
            assert!(released.unwrap(), "terminal callback fired before import gate release");
            assert!(rx.try_recv().is_err(), "terminal callback must fire exactly once");
        }
    }
}

fn import_type_to_bit(t: ImportType) -> u32 {
    match t {
        ImportType::History => ImportServices::HISTORY,
        ImportType::Bookmarks => ImportServices::BOOKMARKS,
        ImportType::Passwords => ImportServices::PASSWORDS,
        ImportType::Cookies => ImportServices::COOKIES,
        ImportType::Autofill => ImportServices::AUTOFILL,
        ImportType::Favicons => ImportServices::FAVICONS,
        ImportType::Workspaces => ImportServices::WORKSPACES,
        ImportType::Essential => IMPORT_ESSENTIAL_BIT,
    }
}
