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
    assert!(!result.is_null(), "malformed JSON must return a non-null error payload");
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
    assert!(tabs.iter().any(|tab| tab.get("isPinned") == Some(&Value::Bool(true))));
    assert!(tabs.iter().any(|tab| tab.get("isFavorite") == Some(&Value::Bool(true))));
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
        maho_ffi::maho_core_update_tab_preview(
            ptr,
            tab_id.as_ptr(),
            data.as_ptr(),
            data.len(),
        );
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
fn test_password_list_omits_plaintext_and_reveal_succeeds() {
    let ptr = unsafe { maho_ffi::maho_core_new() };
    assert!(!ptr.is_null());

    let domain = CString::new("example.com").unwrap();
    let username = CString::new("user123").unwrap();
    let secret = CString::new("my-super-secret-password").unwrap();

    let added = unsafe {
        maho_ffi::maho_core_add_password(
            ptr,
            domain.as_ptr(),
            username.as_ptr(),
            secret.as_ptr(),
        )
    };
    assert!(added);

    // List passwords via search
    let empty_query = CString::new("").unwrap();
    let json_ptr = unsafe { maho_ffi::maho_core_search_passwords(ptr, empty_query.as_ptr()) };
    assert!(!json_ptr.is_null());
    let json_str = unsafe { CStr::from_ptr(json_ptr).to_str().unwrap() };

    // The list JSON MUST NOT contain the password plaintext or field
    assert!(!json_str.contains("my-super-secret-password"));
    assert!(!json_str.contains("\"password\""));

    // Parse the JSON to find the password ID
    let parsed: serde_json::Value = serde_json::from_str(json_str).unwrap();
    let id = parsed.as_array().unwrap()[0].get("id").unwrap().as_str().unwrap();
    let id_c = CString::new(id).unwrap();

    // Reveal password by ID should return the secret
    let revealed_ptr = unsafe { maho_ffi::maho_core_reveal_password(ptr, id_c.as_ptr()) };
    assert!(!revealed_ptr.is_null());
    let revealed_str = unsafe { CStr::from_ptr(revealed_ptr).to_str().unwrap() };
    assert_eq!(revealed_str, "my-super-secret-password");

    // Clean up
    unsafe {
        maho_ffi::maho_string_free(json_ptr);
        maho_ffi::maho_string_free(revealed_ptr);
        maho_ffi::maho_core_free(ptr);
    }
}

#[test]
fn test_sync_device_disconnect_and_rename() {
    let ptr = unsafe { maho_ffi::maho_core_new() };
    assert!(!ptr.is_null());

    let device_id = CString::new("remote-device-99").unwrap();
    let new_name = CString::new("Super Device").unwrap();

    // Renaming or disconnecting nonexistent device should return false
    let disconnected = unsafe {
        maho_ffi::maho_core_disconnect_sync_device(ptr, device_id.as_ptr())
    };
    assert!(!disconnected);

    let renamed = unsafe {
        maho_ffi::maho_core_rename_sync_device(ptr, device_id.as_ptr(), new_name.as_ptr())
    };
    assert!(!renamed);

    unsafe { maho_ffi::maho_core_free(ptr) };
}


