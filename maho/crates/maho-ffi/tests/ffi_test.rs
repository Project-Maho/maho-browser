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
fn handle_event_invalid_json_returns_null() {
    let ptr = maho_ffi::maho_core_new();
    let bad = CString::new("not json").unwrap();
    let result = unsafe { maho_ffi::maho_core_handle_event(ptr, bad.as_ptr()) };
    assert!(result.is_null());
    unsafe { maho_ffi::maho_core_free(ptr) };
}

#[test]
fn get_tab_view_models_empty() {
    let ptr = maho_ffi::maho_core_new();
    let result = unsafe { maho_ffi::maho_core_get_tab_view_models(ptr) };
    assert!(!result.is_null());
    let json = unsafe { CStr::from_ptr(result) }.to_str().unwrap();
    assert_eq!(json, "[]");
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
