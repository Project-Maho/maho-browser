// maho/crates/maho-ffi/tests/content_blocker_ffi.rs
//! Boundary + exactly-once-ownership tests for the content-blocking C FFI.
//!
//! Runs as `cargo test -p maho-ffi content_blocker`.
//!
//! These pin the L5 FFI contract against the L1 `ContentBlockingMode`
//! (including the forward-compatible `Unknown`) and the Wave 2 candidate-aware
//! snapshot/compile/install lifecycle. Ownership is proven only through safe
//! observable seams: every opaque handle receives exactly ONE terminal action
//! (install OR free) in a given test, so no double-free/UB is ever invoked.

use std::ffi::{c_char, CStr, CString};
use std::ptr;

use serde_json::Value;

const ADS_URL: &str = "https://ads.example.com/tracker.js";
const SITE_SOURCE: &str = "https://site.test/";
const REQ_TYPE: &str = "script";

fn cstr(value: &str) -> CString {
    CString::new(value).expect("test string has no interior NUL")
}

fn take_json(ptr: *mut c_char) -> Value {
    assert!(!ptr.is_null());
    let json = unsafe {
        // SAFETY: `ptr` is a non-null string returned by an FFI JSON getter.
        CStr::from_ptr(ptr)
            .to_str()
            .expect("FFI JSON must be valid UTF-8")
            .to_owned()
    };
    unsafe {
        // SAFETY: `ptr` is consumed exactly once after copying its contents.
        maho_ffi::maho_string_free(ptr);
    }
    serde_json::from_str(&json).expect("FFI JSON must parse")
}

/// Apply a filter-list update through the FFI, returning the raw bool.
unsafe fn apply_update(core: *mut maho_core::maho_core::MahoCore, json: &str) -> bool {
    let c = cstr(json);
    maho_ffi::maho_core_apply_filter_list_update_json(core, c.as_ptr())
}

unsafe fn apply_update_result(core: *mut maho_core::maho_core::MahoCore, json: &str) -> Value {
    let c = cstr(json);
    take_json(maho_ffi::maho_core_apply_filter_list_update_result_json(
        core,
        c.as_ptr(),
    ))
}

unsafe fn should_block_ads(core: *mut maho_core::maho_core::MahoCore) -> bool {
    let url = cstr(ADS_URL);
    let src = cstr(SITE_SOURCE);
    let ty = cstr(REQ_TYPE);
    maho_ffi::maho_core_should_block_request(core, url.as_ptr(), src.as_ptr(), ty.as_ptr())
}

#[test]
fn test_null_core_pointers_return_safe_defaults() {
    unsafe {
        // Getter reports the error sentinel, never a mode that enables blocking.
        assert_eq!(
            maho_ffi::maho_core_get_content_blocking_mode(ptr::null_mut()),
            -1
        );
        // Setter rejects a null core.
        assert!(!maho_ffi::maho_core_set_content_blocking_mode(
            ptr::null_mut(),
            0
        ));
        // State JSON returns null (no allocation to free).
        assert!(maho_ffi::maho_core_get_content_blocker_state_json(ptr::null_mut()).is_null());
        // Applying an update against a null core is a rejected no-op.
        let payload = cstr(r#"{"listId":"easylist","statusCode":200,"body":"||ads.example.com^"}"#);
        assert!(!maho_ffi::maho_core_apply_filter_list_update_json(
            ptr::null_mut(),
            payload.as_ptr()
        ));
        // Snapshot creation on a null core yields a null handle.
        assert!(maho_ffi::maho_core_create_compile_snapshot(ptr::null_mut()).is_null());
        // Request checks fail open (do not block) on a null core.
        assert!(!should_block_ads(ptr::null_mut()));
    }
}

#[test]
fn test_null_free_and_dispose_are_safe() {
    unsafe {
        // Every free/dispose entrypoint tolerates null without crashing.
        maho_ffi::maho_string_free(ptr::null_mut());
        maho_ffi::maho_compile_snapshot_free(ptr::null_mut());
        maho_ffi::maho_content_engine_free(ptr::null_mut());
        // Install with both null is a rejected no-op.
        assert!(!maho_ffi::maho_content_engine_install(
            ptr::null_mut(),
            ptr::null_mut()
        ));
    }
}

#[test]
fn test_install_with_null_core_consumes_handle_returns_false() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        let snapshot = maho_ffi::maho_core_create_compile_snapshot(core);
        assert!(!snapshot.is_null());
        // compile_engine_from_snapshot takes ownership of the snapshot handle.
        let handle = maho_ffi::maho_compile_engine_from_snapshot(snapshot);
        assert!(!handle.is_null());
        // A null core must still consume (free) the compiled handle and return
        // false. We deliberately do NOT free `handle` afterwards: doing so would
        // be the double-free this contract forbids.
        assert!(!maho_ffi::maho_content_engine_install(
            ptr::null_mut(),
            handle
        ));
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_invalid_utf8_update_payload_returns_false() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        // 0xFF/0xFE are not valid UTF-8; CStr::to_str() must reject before parse.
        let bad_bytes: [u8; 3] = [0xff, 0xfe, 0x00];
        let rejected = maho_ffi::maho_core_apply_filter_list_update_json(
            core,
            bad_bytes.as_ptr() as *const c_char,
        );
        assert!(!rejected);
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_invalid_json_update_payload_returns_false() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        // Valid UTF-8, invalid JSON: serde parse fails, function returns false.
        assert!(!apply_update(core, "not json {"));
        // Well-formed JSON of the wrong shape is also rejected.
        assert!(!apply_update(core, r#"{"unexpected":true}"#));
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_mutation_results_report_invalid_urls_and_duplicate_ids() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        let id = cstr("custom");
        let name = cstr("Custom");
        let insecure_url = cstr("http://example.test/list.txt");

        let invalid_url = take_json(maho_ffi::maho_core_add_filter_list_result_json(
            core,
            id.as_ptr(),
            name.as_ptr(),
            insecure_url.as_ptr(),
        ));
        assert_eq!(invalid_url["success"], false);
        assert_eq!(invalid_url["error"]["code"], "insecure_filter_list_url");

        let duplicate_id = cstr("easylist");
        let duplicate = take_json(maho_ffi::maho_core_add_filter_list_result_json(
            core,
            duplicate_id.as_ptr(),
            name.as_ptr(),
            cstr("https://example.test/list.txt").as_ptr(),
        ));
        assert_eq!(duplicate["success"], false);
        assert_eq!(duplicate["error"]["code"], "duplicate_filter_list_id");
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_candidate_rejection_and_sha_mismatch_are_typed_and_do_not_require_compile() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        let hostile = apply_update_result(
            core,
            r#"{"listId":"easylist","statusCode":200,"body":"<html>not a filter list</html>"}"#,
        );
        assert_eq!(hostile["success"], false);
        assert_eq!(hostile["error"]["code"], "invalid_update_body");
        assert_eq!(hostile["compileRequired"], false);

        let mismatch = apply_update_result(
            core,
            r#"{"listId":"easylist","statusCode":200,"body":"||ads.example.com^","sha256":"deadbeef"}"#,
        );
        assert_eq!(mismatch["success"], false);
        assert_eq!(mismatch["error"]["code"], "invalid_update_body");
        assert_eq!(mismatch["compileRequired"], false);
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_mode_numeric_round_trip_and_invalid_codes() {
    // SAFETY: The test creates a fresh MahoCore with maho_core_new, checks that
    // the returned pointer is non-null before use, passes only valid primitive
    // mode codes across the FFI boundary, and frees the core exactly once at the
    // end of the test.
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        // Fresh core defaults to Native (0).
        assert_eq!(maho_ffi::maho_core_get_content_blocking_mode(core), 0);
        // Each valid code round-trips through set -> get.
        assert!(maho_ffi::maho_core_set_content_blocking_mode(core, 2));
        assert_eq!(maho_ffi::maho_core_get_content_blocking_mode(core), 2);
        assert!(maho_ffi::maho_core_set_content_blocking_mode(core, 1));
        assert_eq!(maho_ffi::maho_core_get_content_blocking_mode(core), 1);
        assert!(maho_ffi::maho_core_set_content_blocking_mode(core, 0));
        assert_eq!(maho_ffi::maho_core_get_content_blocking_mode(core), 0);
        // Unknown (3), the error sentinel (-1), and out-of-range codes are not
        // user-settable: the setter rejects them and the mode is unchanged.
        assert!(!maho_ffi::maho_core_set_content_blocking_mode(core, 3));
        assert!(!maho_ffi::maho_core_set_content_blocking_mode(core, -1));
        assert!(!maho_ffi::maho_core_set_content_blocking_mode(core, 99));
        assert_eq!(maho_ffi::maho_core_get_content_blocking_mode(core), 0);
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_not_modified_304_apply_returns_no_rebuild() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        // A fresh core holds no list content, so a 304 cannot confirm anything:
        // it is recorded as a failed update and never triggers a rebuild.
        assert!(!apply_update(
            core,
            r#"{"listId":"easylist","statusCode":304}"#
        ));
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_candidate_apply_returns_rebuild_without_active_install() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        // Baseline: the seeded engine has no rule content, so nothing blocks.
        assert!(!should_block_ads(core));
        // A 200 with new content is held pending and signals a rebuild is needed.
        assert!(apply_update(
            core,
            r#"{"listId":"easylist","statusCode":200,"body":"||ads.example.com^"}"#
        ));
        // Crucially, the candidate is NOT active until a compiled engine installs.
        assert!(!should_block_ads(core));
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_candidate_apply_result_truthfully_requires_worker_compile() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        let result = apply_update_result(
            core,
            r#"{"listId":"easylist","statusCode":200,"body":"||ads.example.com^"}"#,
        );
        assert_eq!(result["success"], true);
        assert_eq!(result["compileRequired"], true);
        assert_eq!(result["error"], Value::Null);
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_snapshot_includes_pending_candidate_and_install_promotes() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        assert!(apply_update(
            core,
            r#"{"listId":"easylist","statusCode":200,"body":"||ads.example.com^"}"#
        ));
        // The candidate-aware snapshot must carry the pending body forward.
        let snapshot = maho_ffi::maho_core_create_compile_snapshot(core);
        assert!(!snapshot.is_null());
        // compile consumes the snapshot handle; install consumes the compiled one.
        let handle = maho_ffi::maho_compile_engine_from_snapshot(snapshot);
        assert!(!handle.is_null());
        // Successful install returns true and consumes the handle exactly once.
        assert!(maho_ffi::maho_content_engine_install(core, handle));
        // Proof the snapshot included the candidate: the rule is now enforced.
        assert!(should_block_ads(core));
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_stale_install_returns_false_and_consumes_handle() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        // Snapshot at the current generation.
        let snapshot = maho_ffi::maho_core_create_compile_snapshot(core);
        assert!(!snapshot.is_null());
        // Mutate the blocker so the snapshot's generation goes stale.
        let origin = cstr("example.org");
        maho_ffi::maho_core_add_site_exception(core, origin.as_ptr());
        // Compile the now-stale snapshot (consumes the snapshot handle).
        let handle = maho_ffi::maho_compile_engine_from_snapshot(snapshot);
        assert!(!handle.is_null());
        // Stale generation must be rejected; the handle is still consumed exactly
        // once inside the call. We do not free it again.
        assert!(!maho_ffi::maho_content_engine_install(core, handle));
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_stale_install_result_reports_rejection_and_consumes_handle() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        let snapshot = maho_ffi::maho_core_create_compile_snapshot(core);
        assert!(!snapshot.is_null());
        let origin = cstr("example.org");
        maho_ffi::maho_core_add_site_exception(core, origin.as_ptr());
        let handle = maho_ffi::maho_compile_engine_from_snapshot(snapshot);
        assert!(!handle.is_null());

        let result = take_json(maho_ffi::maho_content_engine_install_result_json(
            core, handle,
        ));
        assert_eq!(result["success"], false);
        assert_eq!(result["error"]["code"], "install_rejected");
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_legacy_raw_content_update_cannot_bypass_candidate_promotion() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        let id = cstr("easylist");
        let raw_content = cstr("||ads.example.com^");
        maho_ffi::maho_core_update_filter_list_content(core, id.as_ptr(), raw_content.as_ptr());

        let snapshot = maho_ffi::maho_core_create_compile_snapshot(core);
        assert!(!snapshot.is_null());
        let handle = maho_ffi::maho_compile_engine_from_snapshot(snapshot);
        assert!(!handle.is_null());
        assert!(maho_ffi::maho_content_engine_install(core, handle));
        assert!(!should_block_ads(core));
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_snapshot_and_engine_free_paths_consume_handles() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());
        // A snapshot may be discarded directly (exactly-once free).
        let snapshot = maho_ffi::maho_core_create_compile_snapshot(core);
        assert!(!snapshot.is_null());
        maho_ffi::maho_compile_snapshot_free(snapshot);
        // A compiled engine may be discarded without installing (exactly-once free).
        let snapshot2 = maho_ffi::maho_core_create_compile_snapshot(core);
        assert!(!snapshot2.is_null());
        let handle = maho_ffi::maho_compile_engine_from_snapshot(snapshot2);
        assert!(!handle.is_null());
        maho_ffi::maho_content_engine_free(handle);
        maho_ffi::maho_core_free(core);
    }
}

#[test]
fn test_state_json_exposes_per_list_retry_timestamp_through_ffi() {
    unsafe {
        let core = maho_ffi::maho_core_new();
        assert!(!core.is_null());

        let healthy_state = take_json(maho_ffi::maho_core_get_content_blocker_state_json(core));
        let healthy_lists = healthy_state["lists"].as_array().expect("lists array");
        assert!(!healthy_lists.is_empty());
        for list in healthy_lists {
            assert!(list.get("nextRetryTimestamp").is_some());
            assert_eq!(list["nextRetryTimestamp"], Value::Null);
            assert!(list.get("next_retry_timestamp").is_none());
        }

        assert!(!apply_update(
            core,
            r#"{"listId":"easylist","statusCode":500}"#
        ));
        let failed_state = take_json(maho_ffi::maho_core_get_content_blocker_state_json(core));
        let lists = failed_state["lists"].as_array().expect("lists array");
        let failed_list = lists
            .iter()
            .find(|list| list["id"] == "easylist")
            .expect("failed easylist");
        let failed_object = failed_list.as_object().expect("failed list object");
        let last_attempt = failed_list["lastAttemptTimestamp"]
            .as_i64()
            .expect("failed list last attempt timestamp");
        assert_eq!(failed_list["failureCount"], 1);
        assert_eq!(
            failed_object.get("nextRetryTimestamp"),
            Some(&Value::from(last_attempt + 15 * 60))
        );
        assert!(!failed_object.contains_key("next_retry_timestamp"));

        let healthy_list = lists
            .iter()
            .find(|list| list["id"] == "easyprivacy")
            .expect("healthy easyprivacy");
        assert_eq!(healthy_list["nextRetryTimestamp"], Value::Null);

        maho_ffi::maho_core_free(core);
    }
}
