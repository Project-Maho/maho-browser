use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use serde_json::Value;

#[test]
fn create_and_free() {
    let ptr = maho_ffi::maho_core_new();
    assert!(!ptr.is_null());
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn free_null_is_safe() {
    unsafe { maho_ffi::maho_core_free(std::ptr::null_mut()) };
}

fn take_json(ptr: *mut c_char) -> Value {
    assert!(!ptr.is_null());
    let value = serde_json::from_str(unsafe { CStr::from_ptr(ptr) }.to_str().unwrap()).unwrap();
    unsafe { maho_ffi::maho_string_free(ptr) };
    value
}

#[test]
fn profile_update_ffi_persists_target_metadata_and_archive_without_switching_active() {
    let key =
        CString::new("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789").unwrap();
    assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });
    let dir = std::env::temp_dir().join(format!(
        "maho-ffi-profile-update-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    std::fs::create_dir_all(&dir).unwrap();
    let db_path = CString::new(dir.join("profile.sqlite").to_string_lossy().as_bytes()).unwrap();

    let ptr = unsafe { maho_ffi::maho_core_new_with_storage(db_path.as_ptr()) };
    assert!(!ptr.is_null());
    let active_name = CString::new("Profile A").unwrap();
    let active =
        take_json(unsafe { maho_ffi::maho_core_create_profile(ptr, active_name.as_ptr()) });
    let active_id = CString::new(active["id"].as_str().unwrap()).unwrap();
    assert!(unsafe { maho_ffi::maho_core_switch_profile(ptr, active_id.as_ptr()) });
    let target_name = CString::new("Profile B").unwrap();
    let target =
        take_json(unsafe { maho_ffi::maho_core_create_profile(ptr, target_name.as_ptr()) });
    let target_id = CString::new(target["id"].as_str().unwrap()).unwrap();

    let updated_name = CString::new("  Profile B Updated  ").unwrap();
    let updated_color = CString::new("#af52de").unwrap();
    let updated = take_json(unsafe {
        maho_ffi::maho_core_update_profile(
            ptr,
            target_id.as_ptr(),
            updated_name.as_ptr(),
            updated_color.as_ptr(),
        )
    });
    assert_eq!(updated["ok"], true);
    assert_eq!(updated["profile"]["name"], "Profile B Updated");
    assert_eq!(updated["profile"]["avatarColor"], "#AF52DE");

    let archive = take_json(unsafe {
        maho_ffi::maho_core_set_profile_archive_timeout(ptr, target_id.as_ptr(), 0)
    });
    assert_eq!(archive["ok"], true);
    let archive = take_json(unsafe {
        maho_ffi::maho_core_get_profile_archive_timeout(ptr, target_id.as_ptr())
    });
    assert_eq!(archive["ok"], true);
    assert_eq!(archive["timeout_hours"], 0.0);

    let profiles = take_json(unsafe { maho_ffi::maho_core_list_profiles(ptr) });
    let active_before_reopen = take_json(unsafe { maho_ffi::maho_core_get_active_profile_id(ptr) });
    assert_eq!(active_before_reopen, active["id"]);
    assert_eq!(
        profiles
            .as_array()
            .unwrap()
            .iter()
            .find(|profile| profile["id"] == active["id"])
            .unwrap()["name"],
        "Profile A"
    );
    unsafe { maho_ffi::maho_core_free(ptr) };

    let reopened = unsafe { maho_ffi::maho_core_new_with_storage(db_path.as_ptr()) };
    assert!(!reopened.is_null());
    assert_eq!(unsafe { maho_ffi::maho_core_load_state(reopened) }, 1);
    let profiles = take_json(unsafe { maho_ffi::maho_core_list_profiles(reopened) });
    let restored = profiles
        .as_array()
        .unwrap()
        .iter()
        .find(|profile| profile["id"] == target["id"])
        .unwrap();
    assert_eq!(restored["name"], "Profile B Updated");
    assert_eq!(restored["avatarColor"], "#AF52DE");
    assert!(restored["archiveTimeoutHours"].is_null());
    assert_eq!(
        take_json(unsafe { maho_ffi::maho_core_get_active_profile_id(reopened) }),
        active["id"]
    );
    unsafe { maho_ffi::maho_core_free(reopened) };
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn profile_update_ffi_returns_structured_validation_and_lookup_errors() {
    let ptr = maho_ffi::maho_core_new();
    let first_name = CString::new("Work").unwrap();
    let first = take_json(unsafe { maho_ffi::maho_core_create_profile(ptr, first_name.as_ptr()) });
    let second_name = CString::new("Personal").unwrap();
    let second =
        take_json(unsafe { maho_ffi::maho_core_create_profile(ptr, second_name.as_ptr()) });
    let second_id = CString::new(second["id"].as_str().unwrap()).unwrap();
    let duplicate = CString::new("  wOrK  ").unwrap();
    let color = CString::new("#007AFF").unwrap();
    let duplicate_result = take_json(unsafe {
        maho_ffi::maho_core_update_profile(
            ptr,
            second_id.as_ptr(),
            duplicate.as_ptr(),
            color.as_ptr(),
        )
    });
    assert_eq!(duplicate_result["ok"], false);
    assert_eq!(duplicate_result["error"]["code"], "PROFILE_NAME_DUPLICATE");

    let missing_id = CString::new("missing-profile").unwrap();
    let missing = take_json(unsafe {
        maho_ffi::maho_core_update_profile(
            ptr,
            missing_id.as_ptr(),
            first_name.as_ptr(),
            color.as_ptr(),
        )
    });
    assert_eq!(missing["ok"], false);
    assert_eq!(missing["error"]["code"], "PROFILE_NOT_FOUND");
    let missing_archive = take_json(unsafe {
        maho_ffi::maho_core_get_profile_archive_timeout(ptr, missing_id.as_ptr())
    });
    assert_eq!(missing_archive["error"]["code"], "PROFILE_NOT_FOUND");
    let invalid_archive = take_json(unsafe {
        maho_ffi::maho_core_set_profile_archive_timeout(ptr, second_id.as_ptr(), 13)
    });
    assert_eq!(
        invalid_archive["error"]["code"],
        "PROFILE_ARCHIVE_TIMEOUT_INVALID"
    );

    let profiles = take_json(unsafe { maho_ffi::maho_core_list_profiles(ptr) });
    assert_eq!(
        profiles
            .as_array()
            .unwrap()
            .iter()
            .find(|profile| profile["id"] == second["id"])
            .unwrap()["name"],
        "Personal"
    );
    assert!(profiles
        .as_array()
        .unwrap()
        .iter()
        .any(|profile| profile["id"] == first["id"]));
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn profile_delete_ffi_preserves_success_shape_and_adds_structured_outcome() {
    let ptr = maho_ffi::maho_core_new();
    let profiles = take_json(unsafe { maho_ffi::maho_core_list_profiles(ptr) });
    let default_id = profiles[0]["id"].as_str().unwrap();
    let default_id = CString::new(default_id).unwrap();

    let protected =
        take_json(unsafe { maho_ffi::maho_core_delete_profile(ptr, default_id.as_ptr()) });
    assert_eq!(protected["success"], false);
    assert!(protected["profile_id"].is_null());
    assert_eq!(protected["outcome"], "protected");
    assert_eq!(protected["error_code"], "PROFILE_PROTECTED");

    let missing_id = CString::new("malformed/not-a-real-profile").unwrap();
    let missing =
        take_json(unsafe { maho_ffi::maho_core_delete_profile(ptr, missing_id.as_ptr()) });
    assert_eq!(missing["success"], false);
    assert_eq!(missing["outcome"], "not_found");
    assert_eq!(missing["error_code"], "PROFILE_NOT_FOUND");

    let name = CString::new("Disposable").unwrap();
    let created = take_json(unsafe { maho_ffi::maho_core_create_profile(ptr, name.as_ptr()) });
    let created_id = CString::new(created["id"].as_str().unwrap()).unwrap();
    let deleted =
        take_json(unsafe { maho_ffi::maho_core_delete_profile(ptr, created_id.as_ptr()) });
    assert_eq!(deleted["success"], true);
    assert_eq!(deleted["profile_id"], created["id"]);
    assert_eq!(deleted["outcome"], "deleted");
    assert!(deleted["error_code"].is_null());

    unsafe { maho_ffi::maho_core_free(ptr) };
}

// Regression for G-DL6: C++ BuildStartJson writes total_bytes as a double, so
// base::JSONWriter emits "5000.0". serde_json parses that as f64, and as_u64()
// returns None -> total_bytes was stored as 0. The FFI now falls back to as_f64().
#[test]
fn download_total_bytes_double_json_parses_as_integer() {
    let ptr = maho_ffi::maho_core_new();
    let start_json = r#"{"filename":"a.avif","url":"https://example.com/a.avif","total_bytes":5000.0,"file_path":"/tmp/a.avif","chromium_guid":"guid-abc"}"#;
    let c = CString::new(start_json).unwrap();
    let id_res = unsafe { maho_ffi::maho_core_start_download(ptr, c.as_ptr()) };
    assert!(!id_res.is_null());
    unsafe { maho_ffi::maho_string_free(id_res) };

    let vm_res = unsafe { maho_ffi::maho_core_get_download_view_models(ptr) };
    assert!(!vm_res.is_null());
    let vm_str = unsafe { CStr::from_ptr(vm_res) }.to_str().unwrap();
    let list: Vec<Value> = serde_json::from_str(vm_str).unwrap();
    unsafe { maho_ffi::maho_string_free(vm_res) };

    assert_eq!(list.len(), 1);
    assert_eq!(
        list[0].get("totalBytes").and_then(|v| v.as_u64()),
        Some(5000),
        "total_bytes double must round-trip to 5000, not 0 (G-DL6)"
    );
    unsafe { maho_ffi::maho_core_free(ptr) };
}

// D0: chromium_guid mapping lets the bridge re-attach a Chromium DownloadItem to
// its persisted Maho row after restart. find_download_by_chromium_guid must return
// the maho_id for a known guid and null for an unknown one.
#[test]
fn find_download_by_chromium_guid_roundtrip() {
    let ptr = maho_ffi::maho_core_new();
    let start_json = r#"{"filename":"b.zip","url":"https://example.com/b.zip","total_bytes":10,"chromium_guid":"guid-xyz"}"#;
    let c = CString::new(start_json).unwrap();
    let id_res = unsafe { maho_ffi::maho_core_start_download(ptr, c.as_ptr()) };
    assert!(!id_res.is_null());
    let maho_id: String =
        serde_json::from_str(unsafe { CStr::from_ptr(id_res) }.to_str().unwrap()).unwrap();
    unsafe { maho_ffi::maho_string_free(id_res) };

    let guid_c = CString::new("guid-xyz").unwrap();
    let found = unsafe { maho_ffi::maho_core_find_download_by_chromium_guid(ptr, guid_c.as_ptr()) };
    assert!(!found.is_null(), "known guid must resolve to a maho_id");
    let found_id: String =
        serde_json::from_str(unsafe { CStr::from_ptr(found) }.to_str().unwrap()).unwrap();
    unsafe { maho_ffi::maho_string_free(found) };
    assert_eq!(found_id, maho_id);

    let unknown = CString::new("no-such-guid").unwrap();
    let none_res =
        unsafe { maho_ffi::maho_core_find_download_by_chromium_guid(ptr, unknown.as_ptr()) };
    assert!(none_res.is_null(), "unknown guid must return null");
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn handle_create_tab_event() {
    let ptr = maho_ffi::maho_core_new();
    let event_json = r#"{"kind":"create_tab","space_id":"space-1","url":"https://example.com","parent_id":null}"#;
    let c_event = CString::new(event_json).unwrap();
    let result = unsafe { maho_ffi::maho_core_handle_event(ptr, c_event.as_ptr()) };
    assert!(!result.is_null());
    let result_str = unsafe { CStr::from_ptr(result) }.to_str().unwrap();
    assert!(result_str.starts_with('['));
    unsafe { maho_ffi::maho_string_free(result) };
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn handle_update_space_config_event() {
    let ptr = maho_ffi::maho_core_new();
    let create_json = r#"{"kind":"create_space","name":"Config Space","color":{"hue":0.5,"saturation":0.8,"brightness":0.9},"profile_id":"default"}"#;
    let c_create = CString::new(create_json).unwrap();
    let create_result = unsafe { maho_ffi::maho_core_handle_event(ptr, c_create.as_ptr()) };
    assert!(!create_result.is_null());
    let create_str = unsafe { CStr::from_ptr(create_result) }.to_str().unwrap();
    let created: Vec<Value> = serde_json::from_str(create_str).unwrap();
    let space_id = created
        .iter()
        .find_map(|item| {
            item.get("space")
                .and_then(|space| space.get("id"))
                .and_then(|id| id.as_str())
                .map(str::to_owned)
        })
        .expect("space id from create_space update");
    unsafe { maho_ffi::maho_string_free(create_result) };
    let event_json = format!(
        r#"{{"kind":"update_space_config","changes":{{"spaceId":"{}","name":"Config Space","color":{{"hue":0.5,"saturation":0.8,"brightness":0.9}},"icon":null,"profileId":"default"}}}}"#,
        space_id
    );
    let c_event = CString::new(event_json).unwrap();
    let result = unsafe { maho_ffi::maho_core_handle_event(ptr, c_event.as_ptr()) };
    assert!(!result.is_null());
    let result_str = unsafe { CStr::from_ptr(result) }.to_str().unwrap();
    assert!(result_str.contains("space_config_updated"));
    unsafe { maho_ffi::maho_string_free(result) };
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn handle_event_null_ptr_returns_null() {
    let event_json = CString::new(r#"{"kind":"app_launched"}"#).unwrap();
    let result =
        unsafe { maho_ffi::maho_core_handle_event(std::ptr::null_mut(), event_json.as_ptr()) };
    assert!(result.is_null());
}

#[test]
fn handle_event_invalid_json_returns_error_payload() {
    let ptr = maho_ffi::maho_core_new();
    let bad = CString::new("not json").unwrap();
    let result = unsafe { maho_ffi::maho_core_handle_event(ptr, bad.as_ptr()) };
    assert!(
        !result.is_null(),
        "malformed JSON must return a non-null error payload"
    );
    let json = unsafe { CStr::from_ptr(result) }.to_str().unwrap();
    let value: Value = serde_json::from_str(json).unwrap();
    assert_eq!(value["error"]["kind"], "parse");
    unsafe { maho_ffi::maho_string_free(result) };
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn get_tab_view_models_returns_seeded_first_run_tabs() {
    let ptr = maho_ffi::maho_core_new();
    // SAFETY: ptr was just returned by maho_core_new() and has not been freed.
    unsafe { &mut *ptr }.seed_first_run_tabs();
    let result = unsafe { maho_ffi::maho_core_get_tab_view_models(ptr) };
    assert!(!result.is_null());
    let json = unsafe { CStr::from_ptr(result) }.to_str().unwrap();
    let tabs: Vec<Value> = serde_json::from_str(json).unwrap();
    assert_eq!(tabs.len(), 2);
    assert!(tabs
        .iter()
        .any(|tab| tab.get("isPinned") == Some(&Value::Bool(true))));
    assert!(tabs
        .iter()
        .any(|tab| tab.get("isFavorite") == Some(&Value::Bool(true))));
    unsafe { maho_ffi::maho_string_free(result) };
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn get_space_view_models() {
    let ptr = maho_ffi::maho_core_new();
    let result = unsafe { maho_ffi::maho_core_get_space_view_models(ptr) };
    assert!(!result.is_null());
    let json = unsafe { CStr::from_ptr(result) }.to_str().unwrap();
    assert!(json.starts_with('['));
    unsafe { maho_ffi::maho_string_free(result) };
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn get_active_space_id() {
    let ptr = maho_ffi::maho_core_new();
    let result = unsafe { maho_ffi::maho_core_get_active_space_id(ptr) };
    assert!(!result.is_null());
    let json = unsafe { CStr::from_ptr(result) }.to_str().unwrap();
    assert!(!json.is_empty());
    unsafe { maho_ffi::maho_string_free(result) };
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn string_free_null_is_safe() {
    unsafe { maho_ffi::maho_string_free(std::ptr::null_mut()) };
}

#[test]
fn tick_returns_valid_json() {
    let ptr = maho_ffi::maho_core_new();
    let result = unsafe { maho_ffi::maho_core_tick(ptr) };
    assert!(!result.is_null());
    let json = unsafe { CStr::from_ptr(result) }.to_str().unwrap();
    assert!(json.starts_with('['));
    unsafe { maho_ffi::maho_string_free(result) };
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn tick_null_ptr_returns_null() {
    let result = unsafe { maho_ffi::maho_core_tick(std::ptr::null_mut()) };
    assert!(result.is_null());
}

#[test]
fn handle_url_scheme_invalid_utf8_returns_null() {
    let ptr = maho_ffi::maho_core_new();
    let bad_url = vec![b'm', b'a', b'h', b'o', b':', b'/', b'/', 0xFF, 0];

    let result =
        unsafe { maho_ffi::maho_core_handle_url_scheme(ptr, bad_url.as_ptr() as *const c_char) };

    assert!(result.is_null());
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn check_pinned_navigation_invalid_utf8_returns_null() {
    let ptr = maho_ffi::maho_core_new();
    let tab_id = CString::new("tab-1").unwrap();
    let bad_url = vec![b'h', b't', b't', b'p', b's', b':', b'/', b'/', 0xFF, 0];

    let result = unsafe {
        maho_ffi::maho_core_check_pinned_navigation(
            ptr,
            tab_id.as_ptr(),
            bad_url.as_ptr() as *const c_char,
        )
    };

    assert!(result.is_null());
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn legacy_preview_abi_matches_preedit() {
    let ptr = maho_ffi::maho_core_new();
    let tab_id = CString::new("tab-1").unwrap();
    let data = vec![1, 2, 3];
    unsafe {
        maho_ffi::maho_core_update_tab_preview(ptr, tab_id.as_ptr(), data.as_ptr(), data.len());
    }
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn desktop_privacy_abi_rejects_private_and_unknown() {
    let ptr = maho_ffi::maho_core_new();

    let unknown_tab = CString::new("unknown-tab").unwrap();
    let data = vec![1, 2, 3];
    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            unknown_tab.as_ptr(),
            false,
            data.as_ptr(),
            data.len(),
        )
    };
    assert!(!ok, "Unknown tab ID must be rejected");

    let normal_tab_id = CString::new("normal-tab").unwrap();
    let create_json = r#"{"kind":"create_tab","space_id":"space-1","url":"https://example.com","parent_id":null,"tab_id":"normal-tab","is_private":false}"#;
    let c_create = CString::new(create_json).unwrap();
    let create_res = unsafe { maho_ffi::maho_core_handle_event(ptr, c_create.as_ptr()) };
    assert!(!create_res.is_null());
    unsafe { maho_ffi::maho_string_free(create_res) };

    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            normal_tab_id.as_ptr(),
            false,
            data.as_ptr(),
            data.len(),
        )
    };
    assert!(ok, "Normal tab ID should be accepted");

    let private_tab_id = CString::new("private-tab").unwrap();
    let create_private_json = r#"{"kind":"create_tab","space_id":"space-1","url":"https://example.com","parent_id":null,"tab_id":"private-tab","is_private":true}"#;
    let c_create_private = CString::new(create_private_json).unwrap();
    let create_res = unsafe { maho_ffi::maho_core_handle_event(ptr, c_create_private.as_ptr()) };
    assert!(!create_res.is_null());
    unsafe { maho_ffi::maho_string_free(create_res) };

    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            private_tab_id.as_ptr(),
            false,
            data.as_ptr(),
            data.len(),
        )
    };
    assert!(!ok, "Private tab in core must be rejected");

    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            normal_tab_id.as_ptr(),
            true,
            data.as_ptr(),
            data.len(),
        )
    };
    assert!(!ok, "is_private=true parameter must be rejected");

    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn null_length_utf8_boundaries_do_not_unwind() {
    let ptr = maho_ffi::maho_core_new();
    let data = vec![1, 2, 3];

    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            std::ptr::null_mut(),
            CString::new("tab-1").unwrap().as_ptr(),
            false,
            data.as_ptr(),
            data.len(),
        )
    };
    assert!(!ok);

    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            std::ptr::null(),
            false,
            data.as_ptr(),
            data.len(),
        )
    };
    assert!(!ok);

    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            CString::new("tab-1").unwrap().as_ptr(),
            false,
            std::ptr::null(),
            data.len(),
        )
    };
    assert!(!ok);

    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            CString::new("tab-1").unwrap().as_ptr(),
            false,
            data.as_ptr(),
            0,
        )
    };
    assert!(!ok);

    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            CString::new("tab-1").unwrap().as_ptr(),
            false,
            data.as_ptr(),
            99 * 1024 * 1024,
        )
    };
    assert!(!ok);

    let bad_tab_id = vec![b't', b'a', b'b', 0xFF, 0];
    let ok = unsafe {
        maho_ffi::maho_core_update_tab_preview_with_privacy(
            ptr,
            bad_tab_id.as_ptr() as *const c_char,
            false,
            data.as_ptr(),
            data.len(),
        )
    };
    assert!(!ok);

    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn test_password_list_omits_plaintext_and_unauthenticated_reveal_fails_closed() {
    let ptr = unsafe { maho_ffi::maho_core_new() };
    assert!(!ptr.is_null());

    let domain = CString::new("example.com").unwrap();
    let username = CString::new("user123").unwrap();
    let secret = CString::new("my-super-secret-password").unwrap();

    let added = unsafe {
        maho_ffi::maho_core_add_password(ptr, domain.as_ptr(), username.as_ptr(), secret.as_ptr())
    };
    assert!(
        !added,
        "legacy unauthenticated add_password must fail closed when Vault is uninitialized"
    );

    // List passwords via search
    let empty_query = CString::new("").unwrap();
    let json_ptr = unsafe { maho_ffi::maho_core_search_passwords(ptr, empty_query.as_ptr()) };
    assert!(!json_ptr.is_null());
    let json_str = unsafe { CStr::from_ptr(json_ptr).to_str().unwrap() };

    // The list JSON MUST NOT contain the password plaintext or field
    assert!(!json_str.contains("my-super-secret-password"));
    assert!(!json_str.contains("\"password\""));

    // Legacy un-authenticated reveal fails closed and returns null / empty
    let id_c = CString::new("legacy-test-id").unwrap();
    let revealed_ptr = unsafe { maho_ffi::maho_core_reveal_password(ptr, id_c.as_ptr()) };
    assert!(revealed_ptr.is_null());

    // Clean up
    unsafe {
        maho_ffi::maho_string_free(json_ptr);
        maho_ffi::maho_core_free(ptr);
    }
}

#[test]
fn test_legacy_password_ffi_routes_to_vault_when_initialized() {
    let key =
        CString::new("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef").unwrap();
    assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });
    let dir = std::env::temp_dir().join(format!(
        "maho-ffi-legacy-password-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    std::fs::create_dir_all(&dir).unwrap();
    let db_path = CString::new(
        dir.join("legacy-password-ffi.sqlite")
            .to_string_lossy()
            .as_bytes(),
    )
    .unwrap();
    let ptr = unsafe { maho_ffi::maho_core_new_with_storage(db_path.as_ptr()) };
    assert!(!ptr.is_null());

    let init = CString::new(
        r#"{"masterPassphrase":"correct passphrase","recoverySecret":"recovery key"}"#,
    )
    .unwrap();
    let init_ptr = unsafe { maho_ffi::maho_vault_initialize_json(ptr, init.as_ptr()) };
    assert!(!init_ptr.is_null());
    let init_json = unsafe { CStr::from_ptr(init_ptr).to_str().unwrap() };
    let init_value: Value = serde_json::from_str(init_json).unwrap();
    assert_eq!(init_value.get("ok").and_then(Value::as_bool), Some(true));
    unsafe { maho_ffi::maho_string_free(init_ptr) };

    let domain = CString::new("https://example.com").unwrap();
    let username = CString::new("user@example.com").unwrap();
    let secret = CString::new("my-super-secret-password").unwrap();
    assert!(unsafe {
        maho_ffi::maho_core_add_password(ptr, domain.as_ptr(), username.as_ptr(), secret.as_ptr())
    });

    let empty_query = CString::new("").unwrap();
    let list_ptr = unsafe { maho_ffi::maho_core_search_passwords(ptr, empty_query.as_ptr()) };
    assert!(!list_ptr.is_null());
    let list_json = unsafe { CStr::from_ptr(list_ptr).to_str().unwrap() };
    assert!(!list_json.contains("my-super-secret-password"));
    let list: Vec<Value> = serde_json::from_str(list_json).unwrap();
    unsafe { maho_ffi::maho_string_free(list_ptr) };
    assert_eq!(list.len(), 1);
    let password_id = list[0].get("id").and_then(Value::as_str).unwrap();
    assert_eq!(
        list[0].get("domain").and_then(Value::as_str),
        Some("https://example.com")
    );
    let listed_username = list[0].get("username").and_then(Value::as_str).unwrap();
    assert!(!listed_username.is_empty());
    assert_ne!(listed_username, "user@example.com");

    let id_c = CString::new(password_id).unwrap();
    let revealed_ptr = unsafe { maho_ffi::maho_core_reveal_password(ptr, id_c.as_ptr()) };
    assert!(!revealed_ptr.is_null());
    let revealed = unsafe { CStr::from_ptr(revealed_ptr).to_str().unwrap() };
    assert_eq!(revealed, "my-super-secret-password");
    unsafe { maho_ffi::maho_string_free(revealed_ptr) };

    let renamed = CString::new("renamed@example.com").unwrap();
    assert!(unsafe {
        maho_ffi::maho_core_update_password_username(ptr, id_c.as_ptr(), renamed.as_ptr())
    });
    let renamed_query = CString::new("example.com").unwrap();
    let renamed_ptr = unsafe { maho_ffi::maho_core_search_passwords(ptr, renamed_query.as_ptr()) };
    assert!(!renamed_ptr.is_null());
    let renamed_json = unsafe { CStr::from_ptr(renamed_ptr).to_str().unwrap() };
    let renamed_list: Vec<Value> = serde_json::from_str(renamed_json).unwrap();
    unsafe { maho_ffi::maho_string_free(renamed_ptr) };
    assert_eq!(renamed_list.len(), 1);
    let renamed_username = renamed_list[0]
        .get("username")
        .and_then(Value::as_str)
        .unwrap();
    assert!(!renamed_username.is_empty());
    assert_ne!(renamed_username, listed_username);
    assert_ne!(renamed_username, "renamed@example.com");

    assert!(unsafe { maho_ffi::maho_core_delete_password(ptr, id_c.as_ptr()) });
    let after_delete_ptr =
        unsafe { maho_ffi::maho_core_search_passwords(ptr, empty_query.as_ptr()) };
    assert!(!after_delete_ptr.is_null());
    let after_delete_json = unsafe { CStr::from_ptr(after_delete_ptr).to_str().unwrap() };
    let after_delete: Vec<Value> = serde_json::from_str(after_delete_json).unwrap();
    unsafe { maho_ffi::maho_string_free(after_delete_ptr) };
    assert!(after_delete.is_empty());

    unsafe { maho_ffi::maho_core_free(ptr) };
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn test_sync_device_disconnect_and_rename() {
    let ptr = unsafe { maho_ffi::maho_core_new() };
    assert!(!ptr.is_null());

    let device_id = CString::new("remote-device-99").unwrap();
    let new_name = CString::new("Super Device").unwrap();

    // Renaming or disconnecting nonexistent device should return false
    let disconnected =
        unsafe { maho_ffi::maho_core_disconnect_sync_device(ptr, device_id.as_ptr()) };
    assert!(!disconnected);

    let renamed = unsafe {
        maho_ffi::maho_core_rename_sync_device(ptr, device_id.as_ptr(), new_name.as_ptr())
    };
    assert!(!renamed);

    unsafe { maho_ffi::maho_core_free(ptr) };
}
