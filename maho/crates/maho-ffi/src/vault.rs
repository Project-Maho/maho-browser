use std::borrow::Cow;
use std::ffi::{c_char, c_void, CStr, CString};
use std::path::Path;
use std::ptr;
use std::str::FromStr;
use std::sync::atomic::{AtomicU64, Ordering};
use zeroize::{Zeroize, Zeroizing};

use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::VaultPreflightState;
use maho_types::vault::{
    CredentialOrigin, VaultItemId, VaultItemKind, VaultItemListRequest, VaultItemPublicDto,
    VaultItemPublicMetadata, VaultSchemaVersion,
};

use crate::common::{cstr_to_str, to_c_string, to_json_cstring};
use crate::ffi_safe;
use crate::import_gate;
use crate::maho_string_free;

pub enum MahoVaultPreflightState {
    Healthy = 0,
    Locked = 1,
    UnrecoverableKey = 2,
    StructuralCorruption = 3,
    PlaintextResidue = 4,
}

const MAHO_VAULT_PREFLIGHT_STATE_INVALID: i32 = -1;

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultPreflightStatePayload {
    state: &'static str,
    value: i32,
}

fn encode_vault_preflight_state(
    state: VaultPreflightState,
    out_state_json: *mut *mut c_char,
) -> i32 {
    let (state, state_name) = match state {
        VaultPreflightState::Healthy => (MahoVaultPreflightState::Healthy, "healthy"),
        VaultPreflightState::Locked => (MahoVaultPreflightState::Locked, "locked"),
        VaultPreflightState::UnrecoverableKey => (
            MahoVaultPreflightState::UnrecoverableKey,
            "unrecoverableKey",
        ),
        VaultPreflightState::StructuralCorruption => (
            MahoVaultPreflightState::StructuralCorruption,
            "structuralCorruption",
        ),
        VaultPreflightState::PlaintextResidue => (
            MahoVaultPreflightState::PlaintextResidue,
            "plaintextResidue",
        ),
    };
    let value = state as i32;
    if !out_state_json.is_null() {
        // SAFETY: The caller supplied a non-null out pointer. Ownership of the
        // allocated string transfers to the caller and is released by
        // `maho_string_free`, matching the rest of this C ABI.
        unsafe {
            *out_state_json = to_json_cstring(&VaultPreflightStatePayload {
                state: state_name,
                value,
            });
        }
    }
    value
}

/// Performs a read-only Vault database preflight.
///
/// Stable return values are defined by `MahoVaultPreflightState`; `-1` means
/// invalid input, an active import, or a caught panic. When `out_state_json` is
/// non-null, successful calls also return `{"state": ..., "value": ...}` and
/// the caller must free that string with `maho_string_free`.
///
/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `database_path` must be valid null-terminated UTF-8. When
/// `key_store_key_available` is true, `database_key` must also be valid
/// null-terminated UTF-8. `out_state_json` may be null or point to writable
/// storage for one C string pointer.
#[no_mangle]
pub unsafe extern "C" fn maho_core_vault_preflight_state(
    ptr: *mut MahoCore,
    database_path: *const c_char,
    database_key: *const c_char,
    key_store_key_available: bool,
    out_state_json: *mut *mut c_char,
) -> i32 {
    ffi_safe!(
        {
            if !out_state_json.is_null() {
                *out_state_json = ptr::null_mut();
            }
            if ptr.is_null() || database_path.is_null() {
                return MAHO_VAULT_PREFLIGHT_STATE_INVALID;
            }
            if import_gate::is_active() {
                return MAHO_VAULT_PREFLIGHT_STATE_INVALID;
            }
            let database_path = match CStr::from_ptr(database_path).to_str() {
                Ok(path) => Path::new(path),
                Err(_) => return MAHO_VAULT_PREFLIGHT_STATE_INVALID,
            };
            let database_key = if key_store_key_available {
                if database_key.is_null() {
                    return MAHO_VAULT_PREFLIGHT_STATE_INVALID;
                }
                match CStr::from_ptr(database_key).to_str() {
                    Ok(key) => maho_storage::sqlite::VaultDatabaseKey::Available(key),
                    Err(_) => return MAHO_VAULT_PREFLIGHT_STATE_INVALID,
                }
            } else {
                maho_storage::sqlite::VaultDatabaseKey::Unrecoverable
            };
            let state = (&*ptr).vault_preflight_state(database_path, database_key);
            encode_vault_preflight_state(state, out_state_json)
        },
        MAHO_VAULT_PREFLIGHT_STATE_INVALID
    )
}

#[cfg(test)]
mod vault_preflight_ffi_tests {
    use super::*;
    use chrono::{TimeZone, Utc};
    use maho_core::vault_manager::VaultManager;
    use maho_storage::sqlite::{SqliteStorage, VaultMetadataRow};

    const DATABASE_KEY: &str = "vault-preflight-ffi-database-key";
    const MASTER_PASSPHRASE: &[u8] = b"correct horse battery staple";
    const RECOVERY_SECRET: &[u8] = b"deterministic recovery secret";

    fn create_encrypted_database(path: &Path) {
        drop(
            SqliteStorage::open_with_key(path.to_str().expect("UTF-8 test path"), DATABASE_KEY)
                .expect("create encrypted profile database"),
        );
    }

    fn call_preflight_export(
        core: &mut MahoCore,
        path: &Path,
        key: Option<&str>,
    ) -> (i32, serde_json::Value) {
        let path = CString::new(path.to_str().expect("UTF-8 test path")).expect("path CString");
        let key = key.map(|value| CString::new(value).expect("key CString"));
        let mut state_json = ptr::null_mut();
        // SAFETY: All pointers remain valid for the duration of this direct ABI
        // call, and the returned CString is consumed below exactly once.
        let value = unsafe {
            maho_core_vault_preflight_state(
                core,
                path.as_ptr(),
                key.as_ref().map_or(ptr::null(), |value| value.as_ptr()),
                key.is_some(),
                &mut state_json,
            )
        };
        assert!(!state_json.is_null());
        // SAFETY: The ABI returned ownership of one Rust CString.
        let state_json = unsafe { CString::from_raw(state_json) };
        let payload = serde_json::from_str(state_json.to_str().expect("UTF-8 state JSON"))
            .expect("valid state JSON");
        (value, payload)
    }

    #[test]
    fn vault_preflight_export_preserves_every_typed_state() {
        let dir = tempfile::tempdir().expect("tempdir");
        let healthy_path = dir.path().join("healthy.db");
        create_encrypted_database(&healthy_path);

        let locked_path = dir.path().join("locked.db");
        let storage = SqliteStorage::open_with_key(
            locked_path.to_str().expect("UTF-8 test path"),
            DATABASE_KEY,
        )
        .expect("create locked profile database");
        let mut manager = VaultManager::new();
        let now = Utc.with_ymd_and_hms(2026, 8, 12, 12, 0, 0).unwrap();
        let slots = manager
            .initialize(MASTER_PASSPHRASE, RECOVERY_SECRET, now)
            .expect("create valid wrapped-key slots");
        for (slot, payload) in [
            ("kdf_params", slots.kdf_params),
            ("wrapped_user_key", slots.wrapped_user_key),
            ("wrapped_recovery_key", slots.wrapped_recovery_key),
        ] {
            storage
                .save_vault_metadata(&VaultMetadataRow {
                    slot: slot.to_string(),
                    schema_version: 1,
                    payload,
                    updated_at: now.to_rfc3339(),
                })
                .expect("persist wrapped-key slot");
        }
        drop(storage);

        let lost_key_path = dir.path().join("lost-key.db");
        create_encrypted_database(&lost_key_path);

        let corrupt_path = dir.path().join("corrupt.db");
        std::fs::write(&corrupt_path, b"not a sqlite or sqlcipher database")
            .expect("write corrupt database fixture");

        let residue_path = dir.path().join("residue.db");
        create_encrypted_database(&residue_path);
        std::fs::write(
            residue_path.with_extension("db.plain_backup"),
            b"stale plaintext backup residue",
        )
        .expect("write plaintext residue marker");

        let mut core = MahoCore::new();
        for (path, key, expected_value, expected_name) in [
            (healthy_path.as_path(), Some(DATABASE_KEY), 0, "healthy"),
            (locked_path.as_path(), Some(DATABASE_KEY), 1, "locked"),
            (lost_key_path.as_path(), None, 2, "unrecoverableKey"),
            (
                corrupt_path.as_path(),
                Some(DATABASE_KEY),
                3,
                "structuralCorruption",
            ),
            (
                residue_path.as_path(),
                Some(DATABASE_KEY),
                4,
                "plaintextResidue",
            ),
        ] {
            let (value, payload) = call_preflight_export(&mut core, path, key);
            assert_eq!(value, expected_value);
            assert_eq!(payload["state"], expected_name);
            assert_eq!(payload["value"], expected_value);
        }
    }
}



use maho_core::vault_manager::{
    OriginMatchPolicy, VaultCrudError, VaultLoginInput, VaultLoginUpdate, VaultManagerError,
};
use maho_types::vault::{
    VaultAuditPageRequest, VaultErrorCode, VaultItemSearchRequest, VaultPolicy, VaultRevision,
};

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultFfiError {
    code: VaultErrorCode,
    message: String,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultFfiResponse<T> {
    ok: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    data: Option<T>,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<VaultFfiError>,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultMutationResult {
    id: VaultItemId,
    #[serde(serialize_with = "serialize_vault_revision_as_decimal_string")]
    revision: VaultRevision,
}

impl From<VaultItemPublicDto> for VaultMutationResult {
    fn from(item: VaultItemPublicDto) -> Self {
        Self {
            id: item.id,
            revision: item.revision,
        }
    }
}

fn serialize_vault_revision_as_decimal_string<S>(
    revision: &VaultRevision,
    serializer: S,
) -> Result<S::Ok, S::Error>
where
    S: serde::Serializer,
{
    serializer.serialize_str(&revision.value().to_string())
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultInitializeRequest {
    master_passphrase: String,
    recovery_secret: String,
}

impl Drop for VaultInitializeRequest {
    fn drop(&mut self) {
        self.master_passphrase.zeroize();
        self.recovery_secret.zeroize();
    }
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultUnlockRequest {
    master_passphrase: String,
}

impl Drop for VaultUnlockRequest {
    fn drop(&mut self) {
        self.master_passphrase.zeroize();
    }
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultRecoveryUnlockRequest {
    recovery_secret: String,
}

impl Drop for VaultRecoveryUnlockRequest {
    fn drop(&mut self) {
        self.recovery_secret.zeroize();
    }
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultLoginRequest {
    #[serde(default)]
    notes: Option<String>,
    metadata: VaultItemPublicMetadata,
    username: String,
    password: String,
    /// Task 4: optional browser-form detail persisted inside the encrypted
    /// record so a Chromium `PasswordForm` round-trips. Absent for callers that
    /// have no form context (settings, importers).
    #[serde(default)]
    form_details: Option<maho_core::vault_manager::VaultCredentialFormDetails>,
}

impl Drop for VaultLoginRequest {
    fn drop(&mut self) {
        self.password.zeroize();
        if let Some(notes) = &mut self.notes {
            notes.zeroize();
        }
    }
}

fn deserialize_vault_revision<'de, D>(deserializer: D) -> Result<VaultRevision, D::Error>
where
    D: serde::Deserializer<'de>,
{
    struct VaultRevisionVisitor;

    impl serde::de::Visitor<'_> for VaultRevisionVisitor {
        type Value = VaultRevision;

        fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            formatter.write_str("a non-negative integer or canonical unsigned decimal string")
        }

        fn visit_u64<E>(self, value: u64) -> Result<Self::Value, E> {
            Ok(VaultRevision::new(value))
        }

        fn visit_i64<E>(self, value: i64) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            u64::try_from(value)
                .map(VaultRevision::new)
                .map_err(|_| E::custom("Vault revision must not be negative"))
        }

        fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            let canonical = value == "0"
                || (!value.starts_with('0')
                    && !value.is_empty()
                    && value.bytes().all(|byte| byte.is_ascii_digit()));
            if !canonical {
                return Err(E::custom(
                    "Vault revision must be a canonical unsigned decimal string",
                ));
            }
            value
                .parse::<u64>()
                .map(VaultRevision::new)
                .map_err(|_| E::custom("Vault revision is out of range"))
        }
    }

    deserializer.deserialize_any(VaultRevisionVisitor)
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct VaultLoginUpdateRequest {
    #[serde(default)]
    notes: Option<String>,
    id: VaultItemId,
    #[serde(
        default,
        alias = "expected_revision",
        deserialize_with = "deserialize_vault_revision"
    )]
    expected_revision: VaultRevision,
    metadata: VaultItemPublicMetadata,
    username: String,
    #[serde(default)]
    password: Option<String>,
    /// Absent leaves the stored form detail untouched; present replaces it.
    #[serde(default)]
    form_details: Option<maho_core::vault_manager::VaultCredentialFormDetails>,
}

impl Drop for VaultLoginUpdateRequest {
    fn drop(&mut self) {
        if let Some(password) = &mut self.password {
            password.zeroize();
        }
        if let Some(notes) = &mut self.notes {
            notes.zeroize();
        }
    }
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct VaultDeleteRequest {
    id: VaultItemId,
    #[serde(
        default,
        alias = "expected_revision",
        deserialize_with = "deserialize_vault_revision"
    )]
    expected_revision: VaultRevision,
}

#[derive(Clone, Copy, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
enum VaultSecretActionRequest {
    Copy,
    Fill,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultUseSecretRequest {
    item_id: VaultItemId,
    #[serde(alias = "expected_revision")]
    expected_revision: VaultRevision,
    action: VaultSecretActionRequest,
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultUseSecretResponse {}

#[derive(serde::Deserialize)]
#[serde(rename_all = "snake_case")]
enum VaultMatchPolicyRequest {
    Exact,
    Subdomain,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultMatchItemsRequest {
    request: VaultItemSearchRequest,
    policy: VaultMatchPolicyRequest,
}


#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultParityMutationRequest {
    id: VaultItemId,
    #[serde(alias = "expected_revision", deserialize_with = "deserialize_vault_revision")]
    expected_revision: VaultRevision,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultFavoriteRequest {
    id: VaultItemId,
    #[serde(alias = "expected_revision", deserialize_with = "deserialize_vault_revision")]
    expected_revision: VaultRevision,
    favorite: bool,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultNoteRequest {
    title: String,
    notes: String,
}

impl Drop for VaultNoteRequest {
    fn drop(&mut self) {
        self.notes.zeroize();
    }
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultNoteUpdateRequest {
    id: VaultItemId,
    #[serde(alias = "expected_revision", deserialize_with = "deserialize_vault_revision")]
    expected_revision: VaultRevision,
    title: String,
    notes: String,
}

impl Drop for VaultNoteUpdateRequest {
    fn drop(&mut self) {
        self.notes.zeroize();
    }
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultTotpRequest {
    id: VaultItemId,
    #[serde(alias = "expected_revision", deserialize_with = "deserialize_vault_revision")]
    expected_revision: VaultRevision,
    secret: String,
}

impl Drop for VaultTotpRequest {
    fn drop(&mut self) {
        self.secret.zeroize();
    }
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultItemRequest {
    #[serde(alias = "itemId")]
    id: VaultItemId,
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct VaultTotpCodeRequest {
    #[serde(alias = "itemId")]
    id: VaultItemId,
    #[serde(default, alias = "timestamp_secs")]
    timestamp_secs: Option<u64>,
}

#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct VaultStrengthRequest {
    password: String,
}

impl Drop for VaultStrengthRequest {
    fn drop(&mut self) {
        self.password.zeroize();
    }
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct VaultGetNotesResponse {
    notes: String,
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_trash_item_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultParityMutationRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_trash_item(request.id, request.expected_revision) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_restore_item_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultParityMutationRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_restore_item(request.id, request.expected_revision) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_set_favorite_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultFavoriteRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_set_favorite(request.id, request.expected_revision, request.favorite) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_empty_trash_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        match core.vault_empty_trash() {
            Ok(result) => vault_ok(result),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free. The caller must secure-clear returned secrets.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_get_notes_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultItemRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_get_notes(request.id) {
            Ok(result) => vault_ok(VaultGetNotesResponse { notes: result.as_str().to_string() }),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_add_secure_note_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let mut request = match unsafe { vault_parse_json::<VaultNoteRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        let notes = Zeroizing::new(std::mem::take(&mut request.notes));
        match core.vault_add_secure_note(request.title, notes) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_update_secure_note_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let mut request = match unsafe { vault_parse_json::<VaultNoteUpdateRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        let notes = Zeroizing::new(std::mem::take(&mut request.notes));
        match core.vault_update_secure_note(request.id, request.expected_revision, request.title, notes) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_set_login_totp_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultTotpRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match core.vault_set_login_totp(request.id, request.expected_revision, &request.secret) {
            Ok(result) => vault_ok(VaultMutationResult::from(result)),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_totp_code_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        // SAFETY: The caller guarantees a readable NUL-terminated UTF-8 request.
        let request = match unsafe { vault_parse_json::<VaultTotpCodeRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        let timestamp = request.timestamp_secs.unwrap_or_else(|| {
            u64::try_from(chrono::Utc::now().timestamp()).unwrap_or(0)
        });
        match core.vault_totp_code(request.id, timestamp) {
            Ok(result) => vault_ok(result),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and valid UTF-8 C request, when present.
/// Free the returned JSON with maho_string_free.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_health_report_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller owns the core and request for this synchronous FFI call.
        let Some(core) = (unsafe { vault_core(core) }) else { return ptr::null_mut(); };
        match core.vault_health_report() {
            Ok(result) => vault_ok(result),
            Err(error) => vault_crud_error(error),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and readable NUL-terminated request.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_generate_password_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller guarantees the core and request remain valid for this call.
        if unsafe { vault_core(core) }.is_none() { return ptr::null_mut(); }
        // SAFETY: The request is caller-owned readable NUL-terminated UTF-8.
        let request = match unsafe { vault_parse_json::<maho_types::vault_generator::PasswordGeneratorOptions>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        match maho_core::vault_generator::generate_password(&request) {
            Ok(result) => vault_ok(result),
            Err(error) => vault_error(VaultErrorCode::StorageFailure, error.to_string()),
        }
    }, ptr::null_mut())
}

/// # Safety
/// The caller provides a live uniquely borrowed core and readable NUL-terminated request.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_password_strength_json(core: *mut MahoCore, request_json: *const c_char) -> *mut c_char {
    ffi_safe!({
        // SAFETY: The caller guarantees the core and request remain valid for this call.
        if unsafe { vault_core(core) }.is_none() { return ptr::null_mut(); }
        // SAFETY: The request is caller-owned readable NUL-terminated UTF-8.
        let request = match unsafe { vault_parse_json::<VaultStrengthRequest>(request_json) } {
            Ok(request) => request, Err(error) => return error,
        };
        vault_ok(maho_core::vault_generator::estimate_strength(&Zeroizing::new(request.password)))
    }, ptr::null_mut())
}

fn vault_ok<T: serde::Serialize>(data: T) -> *mut c_char {
    to_json_cstring(&VaultFfiResponse {
        ok: true,
        data: Some(data),
        error: None,
    })
}

fn vault_error(code: VaultErrorCode, message: impl Into<String>) -> *mut c_char {
    to_json_cstring(&VaultFfiResponse::<()> {
        ok: false,
        data: None,
        error: Some(VaultFfiError {
            code,
            message: message.into(),
        }),
    })
}

fn vault_lifecycle_error(error: VaultManagerError) -> *mut c_char {
    let code = match error {
        VaultManagerError::VaultLocked => VaultErrorCode::Locked,
        VaultManagerError::Uninitialized => VaultErrorCode::Uninitialized,
        VaultManagerError::AlreadyInitialized => VaultErrorCode::InvalidCredentials,
        VaultManagerError::RateLimited => VaultErrorCode::RateLimited,
        VaultManagerError::InvalidCredentials => VaultErrorCode::InvalidCredentials,
        VaultManagerError::CorruptedVaultData => VaultErrorCode::UnsupportedSchemaVersion,
        // The vault file has no wrapped key slot for the requested unlock kind,
        // so this file cannot be opened that way: same class as CorruptedVaultData.
        VaultManagerError::NoAccountWrap => VaultErrorCode::UnsupportedSchemaVersion,
        VaultManagerError::Storage(_) => VaultErrorCode::StorageFailure,
        VaultManagerError::DeviceProtector(_) => VaultErrorCode::ProviderUnavailable,
        VaultManagerError::DeviceWrapper(_) => VaultErrorCode::StorageFailure,
        VaultManagerError::DeviceBindingMismatch => VaultErrorCode::InvalidCredentials,
        VaultManagerError::Crypto(_) => VaultErrorCode::InvalidCredentials,
    };
    vault_error(code, error.to_string())
}

fn vault_crud_error(error: VaultCrudError) -> *mut c_char {
    let code = match error {
        VaultCrudError::Locked => VaultErrorCode::Locked,
        VaultCrudError::ItemNotFound => VaultErrorCode::NotFound,
        VaultCrudError::RevisionConflict { .. } => VaultErrorCode::RevisionConflict,
        VaultCrudError::Duplicate { .. } => VaultErrorCode::RevisionConflict,
        VaultCrudError::InvalidOrigin { .. } => VaultErrorCode::InvalidOrigin,
        VaultCrudError::ItemKindMismatch | VaultCrudError::FieldMismatch => {
            VaultErrorCode::SecretFieldForbidden
        }
        VaultCrudError::MalformedPayload | VaultCrudError::Crypto => {
            VaultErrorCode::UnsupportedSchemaVersion
        }
        VaultCrudError::Storage(_) => VaultErrorCode::StorageFailure,
        _ => VaultErrorCode::StorageFailure,
    };
    vault_error(code, error.to_string())
}

fn vault_secret_buffer(secret: &Zeroizing<String>) -> *mut c_char {
    if secret.as_bytes().contains(&0) {
        return ptr::null_mut();
    }
    CString::new(secret.as_str())
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `core` must be null or a valid, uniquely borrowed `MahoCore` pointer and
/// `request_json` must be a valid NUL-terminated UTF-8 JSON string.
unsafe fn vault_use_login_secret(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> Result<Zeroizing<String>, *mut c_char> {
    // SAFETY: Category 8 (FFI boundary): the caller contract for this helper
    // requires a null or live uniquely borrowed `MahoCore` pointer.
    let Some(core) = (unsafe { vault_core(core) }) else {
        return Err(ptr::null_mut());
    };
    // SAFETY: Category 8 (FFI boundary): the caller contract requires a valid
    // NUL-terminated UTF-8 JSON request pointer for the duration of this call.
    let request = match unsafe { vault_parse_json::<VaultUseSecretRequest>(request_json) } {
        Ok(request) => request,
        Err(response) => return Err(response),
    };
    match request.action {
        VaultSecretActionRequest::Copy | VaultSecretActionRequest::Fill => core
            .vault_use_login_password_at_revision(request.item_id, request.expected_revision)
            .map_err(vault_crud_error),
    }
}

/// # Safety
/// `response` must be null or a string returned by `to_json_cstring`.
unsafe fn free_vault_error_response(response: *mut c_char) {
    if response.is_null() {
        return;
    }
    // SAFETY: Category 12 (invalid free): `response` comes from this crate's
    // JSON error allocation path and is consumed exactly once here.
    unsafe { maho_string_free(response) };
}

/// # Safety
/// `request_json` must be null or a valid NUL-terminated C string readable for its full length.
unsafe fn vault_parse_json<T: serde::de::DeserializeOwned>(
    request_json: *const c_char,
) -> Result<T, *mut c_char> {
    if request_json.is_null() {
        return Err(vault_error(
            VaultErrorCode::StorageFailure,
            "request JSON is required",
        ));
    }
    // SAFETY: Category 8 (FFI boundary): the public caller contract guarantees a live,
    // NUL-terminated pointer; this helper rejects null and validates UTF-8 before parsing.
    let request = match unsafe { CStr::from_ptr(request_json) }.to_str() {
        Ok(value) => value,
        Err(_) => return Err(vault_error(VaultErrorCode::StorageFailure, "invalid_utf8")),
    };
    serde_json::from_str(request).map_err(|error| {
        vault_error(
            VaultErrorCode::StorageFailure,
            format!("invalid JSON: {error}"),
        )
    })
}

/// # Safety
/// `core` must be null or a valid, uniquely borrowed `MahoCore` pointer owned by the caller.
unsafe fn vault_core(core: *mut MahoCore) -> Option<&'static mut MahoCore> {
    // SAFETY: Category 8 (FFI boundary): the public caller contract guarantees that a non-null
    // `core` points to a live, uniquely borrowed MahoCore for the duration of the call.
    unsafe { core.as_mut() }
}

/// Returns the secret-free Vault status owned by `core`.
///
/// # Safety
/// `core` must be a valid MahoCore pointer. The returned string is freed with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_status_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            vault_ok(core.vault_status())
        },
        ptr::null_mut()
    )
}

/// Returns the effective default agent Vault policy from durable storage.
///
/// # Safety
/// `core` must be a valid MahoCore pointer. The returned string is freed with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_get_policy_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            match core.vault_agent_policy_status() {
                Ok(policy) => vault_ok(policy),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Persists an agent Vault policy override and records a policy-change audit row.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_set_policy_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultPolicy>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_set_agent_policy(request) {
                Ok(policy) => vault_ok(policy),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Returns a paged, public, secret-free Vault audit view.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_audit_page_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultAuditPageRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_audit_page(&request) {
                Ok(page) => vault_ok(page),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Initializes the profile-owned Vault with independent user and recovery secrets.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_initialize_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultInitializeRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.initialize_vault(
                request.master_passphrase.as_bytes(),
                request.recovery_secret.as_bytes(),
            ) {
                Ok(()) => vault_ok(core.vault_status()),
                Err(error) => vault_lifecycle_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Unlocks the profile-owned Vault.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_unlock_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultUnlockRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.unlock_vault(request.master_passphrase.as_bytes()) {
                Ok(()) => vault_ok(core.vault_status()),
                Err(error) => vault_lifecycle_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Unlocks the profile-owned Vault with its independent recovery secret.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_unlock_with_recovery_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultRecoveryUnlockRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.unlock_vault_with_recovery(request.recovery_secret.as_bytes()) {
                Ok(()) => vault_ok(core.vault_status()),
                Err(error) => vault_lifecycle_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Locks the profile-owned Vault and revokes active grants.
///
/// # Safety
/// `core` must be a valid MahoCore pointer.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_lock_json(core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            match core.lock_vault() {
                Ok(()) => vault_ok(core.vault_status()),
                Err(error) => vault_lifecycle_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Adds a durable login record and returns only its public metadata.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_add_login_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultLoginRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
             let mut form_details = request.form_details;
            if let Some(notes) = request.notes {
                let details = form_details.get_or_insert_with(maho_core::vault_manager::VaultCredentialFormDetails::default);
                details.notes.clear();
                if !notes.is_empty() {
                    let mut note = maho_core::vault_manager::VaultCredentialNote::default();
                    note.value = notes;
                    details.notes.push(note);
                }
            }
            let input = VaultLoginInput {
                metadata: request.metadata,
                username: request.username,
                password: zeroize::Zeroizing::new(request.password),
                form_details,
            };
            match core.vault_add_login(input) {
                Ok(item) => vault_ok(item),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Lists public metadata for items in the profile-owned Vault.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_list_items_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultItemListRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_list_items(&request) {
                Ok(items) => vault_ok(items),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Searches exact-origin public item metadata.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_search_items_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultItemSearchRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_search_items(&request) {
                Ok(items) => vault_ok(items),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Matches public item metadata using an explicit origin policy.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_match_items_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultMatchItemsRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            let policy = match request.policy {
                VaultMatchPolicyRequest::Exact => OriginMatchPolicy::Exact,
                VaultMatchPolicyRequest::Subdomain => OriginMatchPolicy::Subdomain,
            };
            match core.vault_match_items(&request.request, policy) {
                Ok(items) => vault_ok(items),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Updates a durable login record and returns its public metadata.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_update_login_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultLoginUpdateRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            let update = VaultLoginUpdate {
                notes: request.notes.map(Zeroizing::new),
                metadata: request.metadata,
                username: request.username,
                password: request.password.map(zeroize::Zeroizing::new),
                form_details: request.form_details,
            };
            match core.vault_update_login(request.id, request.expected_revision, update) {
                Ok(item) => vault_ok(item),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Tombstones a durable Vault item and returns its public metadata.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_delete_item_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            let Some(core) = vault_core(core) else {
                return ptr::null_mut();
            };
            let request = match vault_parse_json::<VaultDeleteRequest>(request_json) {
                Ok(request) => request,
                Err(response) => return response,
            };
            match core.vault_delete_item(request.id, request.expected_revision) {
                Ok(item) => vault_ok(item),
                Err(error) => vault_crud_error(error),
            }
        },
        ptr::null_mut()
    )
}

/// Consumes a login secret for a browser-process action and returns no plaintext.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_use_secret_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            // SAFETY: Category 8 (FFI boundary): this public ABI forwards its
            // documented pointer contracts to the checked helper.
            match unsafe { vault_use_login_secret(core, request_json) } {
                Ok(_secret) => vault_ok(VaultUseSecretResponse {}),
                Err(response) => response,
            }
        },
        ptr::null_mut()
    )
}

/// Returns a short-lived login secret buffer for browser-process copy/fill use.
///
/// The returned buffer is never suitable for WebUI/Mojo responses. The browser
/// process must consume it immediately and release it with
/// [`maho_vault_free_secret_buffer`], which zeroizes before deallocation.
///
/// # Safety
/// `core` must be valid and `request_json` must be a valid NUL-terminated UTF-8 JSON string.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_use_secret_buffer_json(
    core: *mut MahoCore,
    request_json: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            // SAFETY: Category 8 (FFI boundary): this public ABI forwards its
            // documented pointer contracts to the checked helper.
            match unsafe { vault_use_login_secret(core, request_json) } {
                Ok(secret) => vault_secret_buffer(&secret),
                Err(response) => {
                    // SAFETY: Category 12 (invalid free): this path discards a
                    // structured error string allocated by `vault_use_login_secret`
                    // exactly once because this raw-buffer ABI reports errors as null.
                    unsafe { free_vault_error_response(response) };
                    ptr::null_mut()
                }
            }
        },
        ptr::null_mut()
    )
}

/// Frees a secret buffer created by FFI functions, ensuring memory is zeroized before deallocation.
///
/// # Safety
/// `buffer` must be a valid pointer created by Vault secret FFI functions or NUL.
#[no_mangle]
pub unsafe extern "C" fn maho_vault_free_secret_buffer(buffer: *mut c_char) {
    if buffer.is_null() {
        return;
    }
    // SAFETY: Category 12 (invalid free): callers may only pass a pointer
    // returned by Vault secret-buffer FFI functions, transferring ownership
    // back exactly once for zeroization and deallocation.
    unsafe {
        let mut vec = CString::from_raw(buffer).into_bytes();
        vec.zeroize();
    }
}

#[cfg(test)]
mod vault_ffi_tests {
    use super::*;
    use crate::*;
    use std::fs;

    const MASTER: &str = "correct horse battery staple";
    const RECOVERY: &str = "test-only independent recovery material";
    const SECRET: &str = "S3NTINEL-vault-ffi-secret";

    fn request(value: serde_json::Value) -> CString {
        CString::new(value.to_string()).expect("test JSON contains no NUL")
    }

    fn update_request_with_revision(revision: serde_json::Value) -> serde_json::Value {
        serde_json::json!({
            "id": VaultItemId::new(),
            "expectedRevision": revision,
            "metadata": {
                "title": "Revision Test",
                "origins": ["https://revision.example"],
                "usernameHint": "revision@example.test",
                "itemKind": "login",
                "totp": null,
                "passkey": null
            },
            "username": "revision@example.test"
        })
    }

    fn delete_request_with_revision(revision: serde_json::Value) -> serde_json::Value {
        serde_json::json!({
            "id": VaultItemId::new(),
            "expectedRevision": revision
        })
    }

    #[test]
    fn vault_mutation_requests_accept_string_and_legacy_numeric_revisions() {
        for value in [
            serde_json::json!(u64::MAX.to_string()),
            serde_json::json!(42),
        ] {
            let update: VaultLoginUpdateRequest =
                serde_json::from_value(update_request_with_revision(value.clone()))
                    .expect("update request accepts supported revision encoding");
            let delete: VaultDeleteRequest =
                serde_json::from_value(delete_request_with_revision(value))
                    .expect("delete request accepts supported revision encoding");
            let expected = if update.expected_revision.value() == u64::MAX {
                u64::MAX
            } else {
                42
            };
            assert_eq!(update.expected_revision.value(), expected);
            assert_eq!(delete.expected_revision.value(), expected);
        }
    }

    #[test]
    fn vault_mutation_results_expose_lossless_identity() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );

        let add = add_login_request();
        let added = read_owned_json(unsafe { maho_vault_add_login_json(core, add.as_ptr()) });
        assert_eq!(added["ok"], true);
        let id = added["data"]["id"]
            .as_str()
            .expect("add mutation result carries UUID")
            .to_string();
        assert_eq!(added["data"]["revision"], "1");

        let update = request(serde_json::json!({
            "id": id,
            "expectedRevision": "1",
            "metadata": {
                "title": "Updated Example",
                "origins": ["https://example.com"],
                "usernameHint": "ignored-by-core",
                "itemKind": "login",
                "totp": null,
                "passkey": null
            },
            "username": "person@example.com",
            "password": null
        }));
        let updated =
            read_owned_json(unsafe { maho_vault_update_login_json(core, update.as_ptr()) });
        assert_eq!(updated["ok"], true);
        assert_eq!(updated["data"]["id"], id);
        assert_eq!(updated["data"]["revision"], "2");

        free_core(core, &path);
    }

    #[test]
    fn vault_mutation_requests_reject_invalid_revision_encodings() {
        let invalid = [
            serde_json::json!(-1),
            serde_json::json!(1.5),
            serde_json::json!("not-a-number"),
            serde_json::json!("-1"),
            serde_json::json!("1.5"),
            serde_json::json!("01"),
            serde_json::json!("18446744073709551616"),
        ];

        for value in invalid {
            assert!(serde_json::from_value::<VaultLoginUpdateRequest>(
                update_request_with_revision(value.clone())
            )
            .is_err());
            assert!(
                serde_json::from_value::<VaultDeleteRequest>(delete_request_with_revision(value))
                    .is_err()
            );
        }
    }

    fn read_owned_json(pointer: *mut c_char) -> serde_json::Value {
        assert!(!pointer.is_null(), "Vault JSON ABI returned null");
        // SAFETY: the Vault FFI returns a CString allocated by Rust and owned by this test.
        let value = unsafe { CStr::from_ptr(pointer) }
            .to_str()
            .expect("Vault JSON ABI returns UTF-8")
            .to_owned();
        // SAFETY: this test consumes the returned allocation exactly once.
        unsafe { maho_string_free(pointer) };
        serde_json::from_str(&value).expect("Vault JSON ABI returns JSON")
    }

    fn core_with_storage() -> (
        std::sync::MutexGuard<'static, ()>,
        *mut MahoCore,
        std::path::PathBuf,
    ) {
        // Hold the process-wide test-key lock for the caller's whole test:
        // the key is injected here and re-read on every vault FFI storage
        // open below, and parallel key-flipping tests corrupt that.
        let key_lock = crate::common::sqlcipher_test_key_lock();
        maho_storage::sqlite::set_sqlcipher_key("vault-ffi-test-key")
            .expect("test SQLCipher key configures");
        let path = std::env::temp_dir().join(format!("maho-vault-ffi-{}", uuid::Uuid::new_v4()));
        fs::create_dir_all(&path).expect("test profile directory creates");
        let db_path = path.join("vault.sqlite");
        let db = db_path.to_str().expect("temporary path is UTF-8");
        (
            key_lock,
            Box::into_raw(Box::new(MahoCore::new().with_storage(db))),
            path,
        )
    }

    fn free_core(core: *mut MahoCore, path: &std::path::Path) {
        // SAFETY: `core` is uniquely owned by this test and has not been freed.
        unsafe { maho_core_free(core) };
        fs::remove_dir_all(path).expect("test profile directory removes");
    }

    fn initialize_request() -> CString {
        request(serde_json::json!({
            "masterPassphrase": MASTER,
            "recoverySecret": RECOVERY,
        }))
    }

    fn add_login_request() -> CString {
        request(serde_json::json!({
            "metadata": {
                "title": "Example",
                "origins": ["https://example.com"],
                "usernameHint": "ignored-by-core",
                "itemKind": "login",
                "totp": null,
                "passkey": null,
            },
            "username": "person@example.com",
            "password": SECRET,
        }))
    }

    // Task 4: the FFI JSON contract must carry the browser-form detail the C++
    // adapter sends, and hand it back on the backend credential read.
    //
    // Failing-first rationale: `VaultLoginRequest` uses `deny_unknown_fields`, so
    // before Task 4 an add request containing `formDetails` was REJECTED
    // outright; the request below returned ok=false and no detail could ever
    // round-trip.
    #[test]
    fn vault_ffi_add_login_round_trips_form_details_through_the_json_contract() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );

        let add = request(serde_json::json!({
            "metadata": {
                "title": "Detail",
                "origins": ["https://detail.example"],
                "usernameHint": "ignored-by-core",
                "itemKind": "login",
                "totp": null,
                "passkey": null,
            },
            "username": "person@detail.example",
            "password": SECRET,
            "formDetails": {
                "scheme": 1,
                "signonRealm": "https://detail.example",
                "url": "https://detail.example/login",
                "action": "https://detail.example/submit",
                "usernameElement": "user",
                "passwordElement": "pass",
                "blockedByUser": true,
                "matchType": 4,
                "inStore": 1,
                "formData": "cGlja2xl",
            },
        }));
        // SAFETY: the core and request are valid for the duration of this call.
        let added = read_owned_json(unsafe { maho_vault_add_login_json(core, add.as_ptr()) });
        assert_eq!(added["ok"], true);

        // SAFETY: the core is uniquely owned by this test.
        let core_ref = unsafe { &*core };
        let session = core_ref
            .create_vault_backend_session()
            .expect("backend session");
        let credentials = session
            .login_credentials(Some("https://detail.example"), None)
            .expect("backend credential read");
        assert_eq!(1, credentials.len());
        let details = credentials[0]
            .form_details
            .as_ref()
            .expect("form details survive the FFI contract");
        assert_eq!(1, details.scheme);
        assert_eq!("https://detail.example/login", details.url);
        assert_eq!("https://detail.example/submit", details.action);
        assert_eq!("user", details.username_element);
        assert_eq!("pass", details.password_element);
        assert!(details.blocked_by_user);
        assert_eq!(Some(4), details.match_type);
        assert_eq!(1, details.in_store);
        assert_eq!("cGlja2xl", details.form_data);
        // Public list metadata must NOT gain the form detail: it stays inside the
        // encrypted record.
        let list = request(
            serde_json::json!({"schemaVersion": 1, "provider": null, "kinds": [], "cursor": null, "limit": 50}),
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let listed = read_owned_json(unsafe { maho_vault_list_items_json(core, list.as_ptr()) });
        assert!(!listed.to_string().contains("formDetails"));
        assert!(!listed.to_string().contains("passwordElement"));

        drop(session);
        free_core(core, &path);
    }

    // Failing-first rationale: the backend guessed the lookup kind from the
    // STORED records, so a `Basic realm=""` item (whose signon realm equals the
    // plain origin) hijacked HTML lookups for the same site and hid the stored
    // HTML credential. The request now carries the scheme, and the scheme
    // decides HTML-origin versus HTTP-auth-realm resolution.
    #[test]
    fn vault_ffi_login_query_honors_requested_scheme_when_realms_collide() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );

        let collide_realm = "https://collide.example";
        let add = |scheme: u8, username: &str| {
            request(serde_json::json!({
                "metadata": {
                    "title": "Collision",
                    "origins": [collide_realm],
                    "usernameHint": "ignored-by-core",
                    "itemKind": "login",
                    "totp": null,
                    "passkey": null,
                },
                "username": username,
                "password": SECRET,
                "formDetails": {
                    "scheme": scheme,
                    "signonRealm": collide_realm,
                    "url": collide_realm,
                    "action": collide_realm,
                    "usernameElement": "user",
                    "passwordElement": "pass",
                    "blockedByUser": false,
                },
            }))
        };
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_add_login_json(core, add(0, "html-user").as_ptr()) })["ok"],
            true
        );
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_add_login_json(core, add(1, "basic-user").as_ptr()) })["ok"],
            true
        );

        // SAFETY: the core is uniquely owned by this test.
        let core_ref = unsafe { &*core };
        let session = core_ref
            .create_vault_backend_session()
            .expect("backend session");

        // The HTML query must answer with the HTML credential even though a
        // Basic item shares the identical signon realm.
        let html_credentials = session
            .login_credentials(Some(collide_realm), Some(0))
            .expect("html credential read");
        assert_eq!(1, html_credentials.len());
        assert_eq!(
            0,
            html_credentials[0]
                .form_details
                .as_ref()
                .expect("html form details")
                .scheme
        );

        // The Basic query must resolve the HTTP-auth credential, not the HTML
        // one sharing the realm.
        let basic_credentials = session
            .login_credentials(Some(collide_realm), Some(1))
            .expect("basic credential read");
        assert_eq!(1, basic_credentials.len());
        assert_eq!(
            1,
            basic_credentials[0]
                .form_details
                .as_ref()
                .expect("basic form details")
                .scheme
        );

        // Legacy callers without a scheme keep the stored-realm inference.
        let inferred = session
            .login_credentials(Some(collide_realm), None)
            .expect("inferred credential read");
        assert_eq!(1, inferred.len());

        drop(session);
        free_core(core, &path);
    }
    fn vault_secret_buffer_free_accepts_owned_secret_buffer() {
        let secret = CString::new("miri-secret-buffer").expect("test secret has no NUL");
        let raw = secret.into_raw();

        // SAFETY: this test transfers one CString allocation to the Vault secret
        // buffer free function and never uses the pointer again.
        unsafe { maho_vault_free_secret_buffer(raw) };
    }

    #[test]
    fn vault_ffi_contract_removes_process_global_five_function_abi() {
        let source = include_str!("lib.rs");

        assert!(!source.contains(concat!("GLOBAL_", "VAULT_MANAGER")));
        assert!(!source.contains(concat!("fn with_", "vault")));
        assert!(!source.contains(concat!("fn maho_vault_", "initialize(")));
        assert!(!source.contains(concat!("fn maho_vault_", "unlock(")));
        assert!(!source.contains(concat!("fn maho_vault_", "lock()")));
        assert!(!source.contains(concat!("fn maho_vault_", "encrypt_item(")));
    }

    #[test]
    fn vault_ffi_returns_explicit_boundary_failures_for_null_and_invalid_utf8() {
        let init = initialize_request();
        let invalid_utf8 = [0xff_u8 as c_char, 0];

        // SAFETY: null core is an explicitly supported boundary input.
        assert!(unsafe { maho_vault_initialize_json(ptr::null_mut(), init.as_ptr()) }.is_null());

        let (_key_lock, core, path) = core_with_storage();
        // SAFETY: `core` is live and the invalid bytes are NUL-terminated.
        let response =
            read_owned_json(unsafe { maho_vault_initialize_json(core, invalid_utf8.as_ptr()) });
        assert_eq!(response["ok"], false);
        assert_eq!(response["error"]["code"], "storage_failure");
        assert_eq!(response["error"]["message"], "invalid_utf8");
        free_core(core, &path);
    }

    #[test]
    fn vault_ffi_initialization_fails_closed_without_profile_storage() {
        let core = maho_core_new();
        let init = initialize_request();

        // SAFETY: `core` is a live FFI-created core and `init` is NUL-terminated JSON.
        let response = read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) });
        assert_eq!(response["ok"], false);
        assert_eq!(response["error"]["code"], "storage_failure");
        // SAFETY: `core` is uniquely owned by this test.
        unsafe { maho_core_free(core) };
    }

    #[test]
    fn core_tick_at_returns_null_for_null_core() {
        // SAFETY: the function tolerates a null core and must return null.
        assert!(unsafe { maho_core_tick_at(std::ptr::null_mut(), 0) }.is_null());
    }

    #[test]
    fn core_tick_at_drives_vault_auto_lock_deterministically() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );
        let unlock = request(serde_json::json!({"masterPassphrase": MASTER}));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_unlock_json(core, unlock.as_ptr()) })["data"]
                ["lockState"],
            "unlocked"
        );

        let future = chrono::Utc::now().timestamp() + 172_800; // two days out
                                                               // SAFETY: the core is a valid profile-owned core; the returned string is
                                                               // freed by `read_owned_json`.
        let updates = read_owned_json(unsafe { maho_core_tick_at(core, future) });
        assert!(updates.is_array());

        // SAFETY: the core is a valid profile-owned core.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_status_json(core) })["data"]["lockState"],
            "auto_locked"
        );
        free_core(core, &path);
    }

    #[test]
    fn vault_ffi_uses_profile_owned_durable_lifecycle() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );
        // SAFETY: the core is a valid profile-owned core.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_lock_json(core) })["data"]["lockState"],
            "locked"
        );
        let unlock = request(serde_json::json!({"masterPassphrase": MASTER}));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_unlock_json(core, unlock.as_ptr()) })["data"]
                ["lockState"],
            "unlocked"
        );
        // SAFETY: the core is a valid profile-owned core.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_status_json(core) })["data"]["lockState"],
            "unlocked"
        );
        // SAFETY: the core is a valid profile-owned core.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_lock_json(core) })["ok"],
            true
        );
        let recovery = request(serde_json::json!({"recoverySecret": RECOVERY}));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe {
                maho_vault_unlock_with_recovery_json(core, recovery.as_ptr())
            })["data"]["lockState"],
            "unlocked"
        );
        free_core(core, &path);
    }

    #[test]
    fn vault_ffi_crud_returns_metadata_and_keeps_secret_out_of_queries() {
        let (_key_lock, core, path) = core_with_storage();
        let init = initialize_request();
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_initialize_json(core, init.as_ptr()) })["ok"],
            true
        );
        let add = add_login_request();
        // SAFETY: the core and request are valid for the duration of this call.
        let added = read_owned_json(unsafe { maho_vault_add_login_json(core, add.as_ptr()) });
        let id = added["data"]["id"]
            .as_str()
            .expect("add returns ID")
            .to_string();
        let list = request(
            serde_json::json!({"schemaVersion": 1, "provider": null, "kinds": [], "cursor": null, "limit": 50}),
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let listed = read_owned_json(unsafe { maho_vault_list_items_json(core, list.as_ptr()) });
        let search = request(
            serde_json::json!({"schemaVersion": 1, "origin": "https://example.com", "provider": null, "kinds": []}),
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let searched =
            read_owned_json(unsafe { maho_vault_search_items_json(core, search.as_ptr()) });
        let matched = request(
            serde_json::json!({"request": {"schemaVersion": 1, "origin": "https://login.example.com", "provider": null, "kinds": []}, "policy": "subdomain"}),
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let matched =
            read_owned_json(unsafe { maho_vault_match_items_json(core, matched.as_ptr()) });
        assert_eq!(
            listed["data"].as_array().expect("list data is array").len(),
            1
        );
        assert_eq!(
            searched["data"]
                .as_array()
                .expect("search data is array")
                .len(),
            1
        );
        assert_eq!(
            matched["data"]
                .as_array()
                .expect("match data is array")
                .len(),
            1
        );
        assert!(!listed.to_string().contains(SECRET));
        assert!(!searched.to_string().contains(SECRET));

        // SAFETY: the core is valid and the explicit lock transitions it to a denied state.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_lock_json(core) })["ok"],
            true
        );
        // SAFETY: the core and request are valid for the duration of this call.
        let locked = read_owned_json(unsafe { maho_vault_list_items_json(core, list.as_ptr()) });
        assert_eq!(locked["ok"], false);
        assert_eq!(locked["error"]["code"], "locked");
        let unlock = request(serde_json::json!({"masterPassphrase": MASTER}));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_unlock_json(core, unlock.as_ptr()) })["ok"],
            true
        );

        assert!(!include_str!("lib.rs").contains(concat!("maho_vault_", "agent_reveal")));
        assert!(!include_str!("lib.rs").contains(concat!("maho_vault_", "use_login_password_json")));

        let used_revision =
            read_owned_json(unsafe { maho_vault_list_items_json(core, list.as_ptr()) })["data"][0]
                ["revision"]
                .as_u64()
                .expect("use records a revision");
        let update = request(serde_json::json!({
            "id": id,
            "expectedRevision": used_revision,
            "metadata": {
                "title": "Renamed Example",
                "origins": ["https://example.com"],
                "usernameHint": "ignored-by-core",
                "itemKind": "login",
                "totp": null,
                "passkey": null,
            },
            "username": "renamed@example.com",
            "password": null,
        }));
        // SAFETY: the core and request are valid for the duration of this call.
        let updated =
            read_owned_json(unsafe { maho_vault_update_login_json(core, update.as_ptr()) });
        assert_eq!(updated["data"]["id"], id);
        assert!(updated["data"]["revision"].is_string());
        let delete = request(serde_json::json!({
            "id": updated["data"]["id"],
            "expectedRevision": updated["data"]["revision"],
        }));
        // SAFETY: the core and request are valid for the duration of this call.
        assert_eq!(
            read_owned_json(unsafe { maho_vault_delete_item_json(core, delete.as_ptr()) })["ok"],
            true
        );

        free_core(core, &path);
    }
}
