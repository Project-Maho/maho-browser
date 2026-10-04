use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::sync::{Mutex, OnceLock};

use maho_ffi::*;

static LAST_CALLED_JSON: OnceLock<Mutex<Option<String>>> = OnceLock::new();

fn get_last_called_json() -> &'static Mutex<Option<String>> {
    LAST_CALLED_JSON.get_or_init(|| Mutex::new(None))
}

extern "C" fn test_callback(json: *const c_char) {
    if !json.is_null() {
        let s = unsafe { CStr::from_ptr(json).to_string_lossy().into_owned() };
        let mut guard = get_last_called_json().lock().unwrap();
        *guard = Some(s);
    }
}

#[test]
fn test_ffi_split_view_integration() {
    unsafe {
        // 1. Create a new core
        let core = maho_core_new();
        assert!(!core.is_null());

        // 2. Register callback
        let token = maho_core_register_split_change_callback(core, test_callback);
        assert!(token > 0);

        // Clear last called status
        {
            let mut guard = get_last_called_json().lock().unwrap();
            *guard = None;
        }

        // 3. Create a split via handle_event
        let event_json = r#"{
            "kind": "create_split",
            "window_id": "win-1",
            "tab_ids": ["tab-1", "tab-2"],
            "orientation": "horizontal"
        }"#;
        let c_event = CString::new(event_json).unwrap();
        let result_ptr = maho_core_handle_event(core, c_event.as_ptr());
        assert!(!result_ptr.is_null());
        maho_string_free(result_ptr);

        // 4. Assert callback was invoked
        {
            let guard = get_last_called_json().lock().unwrap();
            assert!(guard.is_some(), "Callback should be invoked");
            let json_str = guard.as_ref().unwrap();
            assert!(json_str.contains("tab-1"));
            assert!(json_str.contains("tab-2"));
            assert!(json_str.contains("window_id"));
            assert!(json_str.contains("schema_version"));
        }

        // 5. Get split view config via FFI
        let c_win = CString::new("win-1").unwrap();
        let config_ptr = maho_core_get_split_view_config(core, c_win.as_ptr());
        assert!(!config_ptr.is_null());
        let config_str = CStr::from_ptr(config_ptr).to_string_lossy().into_owned();
        assert!(config_str.contains("tab-1"));
        maho_string_free(config_ptr);

        // 6. Window closed
        maho_core_window_closed(core, c_win.as_ptr());

        // 7. Verify config is gone
        let config_ptr_post = maho_core_get_split_view_config(core, c_win.as_ptr());
        assert!(config_ptr_post.is_null());

        // 8. Unregister callback
        maho_core_unregister_split_change_callback(core, token);

        // 9. Free core
        maho_core_free(core);
    }
}
