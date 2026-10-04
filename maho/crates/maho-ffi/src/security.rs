use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;
use std::str::FromStr;

use maho_core::maho_core::MahoCore;
use maho_types::vault::{
    CredentialOrigin, VaultItemId, VaultItemKind, VaultItemListRequest, VaultItemPublicDto,
    VaultItemPublicMetadata, VaultSchemaVersion,
};

use crate::common::{cstr_to_str, to_c_string, to_json_cstring};
use crate::ffi_safe;
use crate::import_gate;

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
struct PasswordEntryView {
    id: String,
    domain: String,
    username: String,
    created_at: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    last_used: Option<String>,
}

fn credential_origin_from_domain(domain: &str) -> Option<CredentialOrigin> {
    let origin = if domain.starts_with("http://") || domain.starts_with("https://") {
        domain.to_string()
    } else {
        format!("https://{domain}")
    };
    CredentialOrigin::try_from(origin).ok()
}

fn saved_password_from_vault_item(item: &VaultItemPublicDto) -> Option<PasswordEntryView> {
    if item.item_kind != VaultItemKind::Login {
        return None;
    }
    let domain = item.origins.first()?.as_str().to_string();
    Some(PasswordEntryView {
        id: item.id.to_string(),
        domain,
        username: item.username_hint.clone(),
        created_at: item.created_at.to_rfc3339(),
        last_used: item.last_used_at.map(|value| value.to_rfc3339()),
    })
}

fn to_vault_password_entries(items: Vec<VaultItemPublicDto>) -> *mut c_char {
    let views: Vec<PasswordEntryView> = items
        .iter()
        .filter_map(saved_password_from_vault_item)
        .collect();
    to_json_cstring(&views)
}

fn vault_login_items(core: &MahoCore) -> Vec<VaultItemPublicDto> {
    let request = VaultItemListRequest {
        trash: Default::default(),
        favorites_only: false,
        schema_version: VaultSchemaVersion::CURRENT,
        provider: Some(maho_types::passwords::PasswordProviderKind::MahoNative),
        kinds: vec![VaultItemKind::Login],
        cursor: None,
        limit: 0,
    };
    core.vault_list_items(&request).unwrap_or_default()
}

fn vault_login_item_by_id(core: &MahoCore, id: VaultItemId) -> Option<VaultItemPublicDto> {
    vault_login_items(core)
        .into_iter()
        .find(|item| item.id == id)
}

#[no_mangle]
pub unsafe extern "C" fn maho_core_get_password_provider_extension(
    ptr: *mut MahoCore,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let extensions = core.get_installed_extensions();
            if let Some(ext_id) = maho_core::extension_bridge::find_password_provider(&extensions) {
                CString::new(ext_id)
                    .map(|c| c.into_raw())
                    .unwrap_or(ptr::null_mut())
            } else {
                ptr::null_mut()
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_is_native_password_provider_active(ptr: *mut MahoCore) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() {
                return true;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*ptr;
            core.get_settings().autofill.password_provider
                == maho_types::passwords::PasswordProviderKind::MahoNative
        },
        false
    )
}

/// # Safety

/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `query` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_search_passwords(
    ptr: *mut MahoCore,
    query: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || query.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*ptr;
            let query_str = match CStr::from_ptr(query).to_str() {
                Ok(s) => s.trim(),
                Err(_) => return ptr::null_mut(),
            };

            let mut items = vault_login_items(core);
            if !query_str.is_empty() {
                let query_lower = query_str.to_lowercase();
                items.retain(|item| {
                    item.title.to_lowercase().contains(&query_lower)
                        || item.username_hint.to_lowercase().contains(&query_lower)
                        || item
                            .origins
                            .iter()
                            .any(|origin| origin.as_str().to_lowercase().contains(&query_lower))
                });
            }
            to_vault_password_entries(items)
        },
        ptr::null_mut()
    )
}

/// Returns a JSON-serialized list of all password provider descriptors in the registry.
/// Caller must free with `maho_string_free`.
#[no_mangle]
pub extern "C" fn maho_core_get_password_provider_registry() -> *mut c_char {
    let registry = maho_types::passwords::get_provider_registry();
    let json = serde_json::to_string(&registry).unwrap_or_else(|_| "[]".to_string());
    CString::new(json)
        .map(CString::into_raw)
        .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `password_id` must be a valid null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_core_delete_password(
    ptr: *mut MahoCore,
    password_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || password_id.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let password_id = match CStr::from_ptr(password_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let Ok(item_id) = VaultItemId::from_str(password_id) else {
                return false;
            };
            let Some(item) = vault_login_item_by_id(core, item_id) else {
                return false;
            };
            core.vault_delete_item(item_id, item.revision).is_ok()
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `password_id` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_reveal_password(
    ptr: *mut MahoCore,
    password_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if ptr.is_null() || password_id.is_null() {
                return ptr::null_mut();
            }
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &mut *ptr;
            let password_id = match CStr::from_ptr(password_id).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            let Ok(item_id) = VaultItemId::from_str(password_id) else {
                return ptr::null_mut();
            };
            match core.vault_use_login_password(item_id) {
                Ok(password) => CString::new(password.as_str())
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut()),
                Err(_) => ptr::null_mut(),
            }
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `domain` and `username` must be valid null-terminated C strings.
/// `password` may be null.
#[no_mangle]
pub unsafe extern "C" fn maho_core_add_password(
    ptr: *mut MahoCore,
    domain: *const c_char,
    username: *const c_char,
    password: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || domain.is_null() || username.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let domain = match CStr::from_ptr(domain).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let username = match CStr::from_ptr(username).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let password = if password.is_null() {
                None
            } else {
                match CStr::from_ptr(password).to_str() {
                    Ok(s) if !s.is_empty() => Some(s.to_string()),
                    Ok(_) => None,
                    Err(_) => return false,
                }
            };
            let pass = password.unwrap_or_default();
            if let Some(origin) = credential_origin_from_domain(domain) {
                let input = maho_core::vault_manager::VaultLoginInput {
                    metadata: maho_types::vault::VaultItemPublicMetadata {
                        favorite: false,
                        trashed_at: None,
                        has_notes: false,
                        title: domain.to_string(),
                        origins: vec![origin],
                        username_hint: username.to_string(),
                        item_kind: maho_types::vault::VaultItemKind::Login,
                        totp: None,
                        passkey: None,
                    },
                    username: username.to_string(),
                    password: zeroize::Zeroizing::new(pass),
                    form_details: None,
                };
                core.vault_add_login(input).is_ok()
            } else {
                false
            }
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `password_id` and `username` must be valid null-terminated C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_core_update_password_username(
    ptr: *mut MahoCore,
    password_id: *const c_char,
    username: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || password_id.is_null() || username.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *ptr;
            let password_id = match CStr::from_ptr(password_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let username = match CStr::from_ptr(username).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let Ok(item_id) = VaultItemId::from_str(password_id) else {
                return false;
            };
            let Some(item) = vault_login_item_by_id(core, item_id) else {
                return false;
            };
            let metadata = VaultItemPublicMetadata {
                favorite: false,
                trashed_at: None,
                has_notes: false,
                title: item.title,
                origins: item.origins,
                username_hint: username.to_string(),
                item_kind: VaultItemKind::Login,
                totp: item.totp,
                passkey: item.passkey,
            };
            let update = maho_core::vault_manager::VaultLoginUpdate {
                notes: None,
                metadata,
                username: username.to_string(),
                password: None,
                form_details: None,
            };
            core.vault_update_login(item_id, item.revision, update)
                .is_ok()
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_new`.
/// `url` must be a valid null-terminated C string.
/// Caller must free the returned string with `maho_string_free`.
#[no_mangle]

#[no_mangle]
pub unsafe extern "C" fn maho_core_derive_oscrypt_key(
    path_utf8: *const c_char,
    out_key: *mut u8,
    out_status: *mut u32,
) -> bool {
    ffi_safe!(
        {
            if path_utf8.is_null() || out_key.is_null() || out_status.is_null() {
                return false;
            }
            let path_str = match CStr::from_ptr(path_utf8).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return false;
                }
            };
            let out_key_slice = std::slice::from_raw_parts_mut(out_key, 16);
            let mut key = [0u8; 16];
            let ok = maho_core::oscrypt::derive_key(path_str, &mut key, &mut *out_status);
            if ok {
                out_key_slice.copy_from_slice(&key);
            }
            ok
        },
        false
    )
}

/// # Safety
/// `path_utf8` must be a valid null-terminated C string.
/// `out_status` must be a writable pointer to a u32.
#[no_mangle]
pub unsafe extern "C" fn maho_core_oscrypt_key_path_is_secure(
    path_utf8: *const c_char,
    out_status: *mut u32,
) -> bool {
    ffi_safe!(
        {
            if path_utf8.is_null() || out_status.is_null() {
                return false;
            }
            let path_str = match CStr::from_ptr(path_utf8).to_str() {
                Ok(s) => s,
                Err(_) => {
                    *out_status = 2; // InvalidArg
                    return false;
                }
            };
            maho_core::oscrypt::key_path_is_secure(path_str, &mut *out_status)
        },
        false
    )
}

/// # Safety
/// `path_utf8` must be a valid null-terminated C string.
/// `out_status` must be a writable pointer to a u32.
/// The caller is responsible for freeing the returned string with `maho_string_free`.

/// must be valid NUL-terminated C strings. Returns `false` on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_set_key(
    core: *mut MahoCore,
    provider: *const c_char,
    api_key: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || provider.is_null() || api_key.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*core;
            let provider_str = match CStr::from_ptr(provider).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let key_str = match CStr::from_ptr(api_key).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            if let Some(storage) = core.storage_ref() {
                let setting_key = format!("byok:{}", provider_str);
                storage.set_setting(&setting_key, key_str).is_ok()
            } else {
                false
            }
        },
        false
    )
}

/// Get the stored BYOK API key for the given provider. Returns null if not set.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `provider` must be a
/// valid NUL-terminated C string. Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_get_key(
    core: *mut MahoCore,
    provider: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if core.is_null() || provider.is_null() {
                return ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return ptr::null_mut();
            }
            let core = &*core;
            let provider_str = match CStr::from_ptr(provider).to_str() {
                Ok(s) => s,
                Err(_) => return ptr::null_mut(),
            };
            tracing::info!(
                target = "maho_agent::audit",
                event = "byok_key_access",
                provider = %provider_str,
            );
            if let Some(storage) = core.storage_ref() {
                let setting_key = format!("byok:{}", provider_str);
                if let Ok(Some(key)) = storage.get_setting(&setting_key) {
                    return CString::new(key)
                        .map(|s| s.into_raw())
                        .unwrap_or(ptr::null_mut());
                }
            }
            ptr::null_mut()
        },
        ptr::null_mut()
    )
}

/// Delete the BYOK API key for the given provider. Returns true on success.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `provider` must be a
/// valid NUL-terminated C string. Returns `false` on null input or import active.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_delete_key(
    core: *mut MahoCore,
    provider: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() || provider.is_null() {
                return false;
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &*core;
            let provider_str = match CStr::from_ptr(provider).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            if let Some(storage) = core.storage_ref() {
                let setting_key = format!("byok:{}", provider_str);
                // Delete by setting to empty string (sentinel for "not set")
                storage.set_setting(&setting_key, "").is_ok()
            } else {
                false
            }
        },
        false
    )
}

/// Get JSON array of supported provider names. Returns caller-owned string.
///
/// # Safety
/// `_core` may be null (ignored). Caller must free the returned string with `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_get_providers(_core: *mut MahoCore) -> *mut c_char {
    ffi_safe!(
        {
            let providers = serde_json::json!(["openai", "anthropic", "gemini", "ollama"]);
            let json = providers.to_string();
            CString::new(json)
                .map(|s| s.into_raw())
                .unwrap_or(ptr::null_mut())
        },
        ptr::null_mut()
    )
}

/// Validate a BYOK API key for the given provider. Returns true if valid.
/// Note: makes a live network request; call from a background thread.
///
/// # Safety
/// `provider` and `api_key` must be valid NUL-terminated C strings (not null). `_core` is
/// unused and may be null. This call performs a live network request.
#[no_mangle]
pub unsafe extern "C" fn maho_core_byok_validate_key(
    _core: *mut MahoCore,
    provider: *const c_char,
    api_key: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if provider.is_null() || api_key.is_null() {
                return false;
            }
            let provider_str = match CStr::from_ptr(provider).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let key_str = match CStr::from_ptr(api_key).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            maho_core::memory_manager::get_runtime().block_on(
                maho_core::llm_provider::validate_api_key(provider_str, key_str),
            )
        },
        false
    )
}

// ============================================================
// Chat image FFI
// ============================================================

/// Send an image-only chat message. Fires a ShellEvent::ChatMessage with image content.
/// `mime` must be non-null (e.g. "image/png"). `data` is raw bytes, `data_len` is byte count.
/// Returns true on success.
///

#[no_mangle]
pub unsafe extern "C" fn maho_google_sign_in_start(
    client_id: *const c_char,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            id_token: *const c_char,
            nonce: *const c_char,
            error: *const c_char,
        ),
    >,
    user_data: *mut c_void,
) -> *mut c_char {
    ffi_safe!(
        {
            let client_id_str = if client_id.is_null() {
                ""
            } else {
                match CStr::from_ptr(client_id).to_str() {
                    Ok(s) => s,
                    Err(_) => return ptr::null_mut(),
                }
            };

            let sink = GoogleSignInSink {
                on_complete,
                user_data,
            };
            let started = match maho_core::google_identity::start_google_sign_in(
                client_id_str,
                move |result| sink.deliver(result),
            ) {
                Ok(started) => started,
                Err(_) => return ptr::null_mut(),
            };

            to_json_cstring(&serde_json::json!({
                "state": started.state,
                "authUrl": started.auth_url,
            }))
        },
        ptr::null_mut()
    )
}

/// Cancels an in-flight Google Sign-In identified by `state` (the value from the
/// JSON returned by `maho_google_sign_in_start`). Lets the C++ layer end a flow
/// deterministically when the OAuth popup is closed before the redirect, or when
/// a new sign-in supersedes a stale one, freeing the listener thread + bound
/// loopback port instead of leaking them until the backstop timeout. Returns
/// true when a matching pending flow was found; idempotent.
///
/// # Safety
/// `state` must be a null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn maho_google_sign_in_cancel(state: *const c_char) -> bool {
    ffi_safe!(
        {
            if state.is_null() {
                return false;
            }
            let state_str = match CStr::from_ptr(state).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            maho_core::google_identity::cancel_google_sign_in(state_str)
        },
        false
    )
}

/// Carries the C callback across the sign-in thread boundary. The raw
/// `user_data` pointer is not `Send`, so the unsafe impl asserts the C++ owner
/// keeps it alive until `on_complete` fires (documented on the FFI entry point).
struct GoogleSignInSink {
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            id_token: *const c_char,
            nonce: *const c_char,
            error: *const c_char,
        ),
    >,
    user_data: *mut c_void,
}

// SAFETY: the C++ owner guarantees `user_data` outlives the single callback
// invocation; see the safety contract on `maho_google_sign_in_start`.
unsafe impl Send for GoogleSignInSink {}

impl GoogleSignInSink {
    fn deliver(
        &self,
        result: std::result::Result<
            maho_core::google_identity::GoogleSignInResult,
            maho_core::error::CoreError,
        >,
    ) {
        let Some(callback) = self.on_complete else {
            return;
        };
        match result {
            Ok(success) => {
                let Ok(id_token) = CString::new(success.id_token) else {
                    return;
                };
                let Ok(nonce) = CString::new(success.nonce) else {
                    return;
                };
                // SAFETY: both CStrings outlive the call; error is null on success.
                unsafe {
                    callback(
                        self.user_data,
                        id_token.as_ptr(),
                        nonce.as_ptr(),
                        ptr::null(),
                    );
                }
            }
            Err(err) => {
                let Ok(message) = CString::new(err.to_string()) else {
                    return;
                };
                // SAFETY: `message` outlives the call; token fields are null on error.
                unsafe {
                    callback(self.user_data, ptr::null(), ptr::null(), message.as_ptr());
                }
            }
        }
    }
}

