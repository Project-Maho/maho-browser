//! Tests that the `ffi_safe!` macro prevents panics from propagating across FFI.

use std::ptr;

// Import the macro from the crate
use maho_ffi::ffi_safe;

/// Simulates an FFI function that panics internally but uses ffi_safe!
/// to catch the panic and return a fallback value.
#[test]
fn panic_does_not_propagate_across_ffi() {
    // Test *mut c_char fallback (null_mut)
    let result: *mut std::ffi::c_char = ffi_safe!(
        {
            panic!("simulated panic in FFI returning *mut c_char");
        },
        ptr::null_mut()
    );
    assert!(
        result.is_null(),
        "Expected null_mut fallback on panic for *mut c_char"
    );

    // Test bool fallback (false)
    let result: bool = ffi_safe!(
        {
            panic!("simulated panic in FFI returning bool");
        },
        false
    );
    assert!(!result, "Expected false fallback on panic for bool");

    // Test i32 fallback (0)
    let result: i32 = ffi_safe!(
        {
            panic!("simulated panic in FFI returning i32");
        },
        0
    );
    assert_eq!(result, 0, "Expected 0 fallback on panic for i32");

    // Test usize fallback (0)
    let result: usize = ffi_safe!(
        {
            panic!("simulated panic in FFI returning usize");
        },
        0
    );
    assert_eq!(result, 0, "Expected 0 fallback on panic for usize");

    // Test f64 fallback (0.0)
    let result: f64 = ffi_safe!(
        {
            panic!("simulated panic in FFI returning f64");
        },
        0.0
    );
    assert_eq!(result, 0.0, "Expected 0.0 fallback on panic for f64");

    // Test () fallback (noop)
    let result: () = ffi_safe!(
        {
            panic!("simulated panic in FFI returning ()");
        },
        ()
    );
    assert_eq!(result, (), "Expected () fallback on panic for void");

    // Test that non-panicking code passes through normally
    let result: *mut std::ffi::c_char = ffi_safe!(
        {
            // Doesn't panic - should return the computed value
            std::ffi::CString::new("hello").unwrap().into_raw()
        },
        ptr::null_mut()
    );
    assert!(
        !result.is_null(),
        "Non-panicking code should return computed value"
    );
    // Clean up
    unsafe {
        drop(std::ffi::CString::from_raw(result));
    }
}

#[test]
fn ffi_panic_returns_fallback_in_release() {
    let before = maho_ffi::maho_core_contained_panic_count();
    let result: i64 = ffi_safe!(
        {
            let values: Vec<i64> = Vec::new();
            values[3]
        },
        -1
    );
    assert_eq!(result, -1);
    assert!(maho_ffi::maho_core_contained_panic_count() > before);
}

#[test]
fn contained_panic_is_recorded_to_the_profile_log_with_location() {
    let profile = tempfile::tempdir().unwrap();
    maho_ffi::panic_record::set_profile_dir(profile.path());
    let _: bool = ffi_safe!(
        {
            panic!("recorded panic for the log");
        },
        false
    );
    let log = std::fs::read_to_string(
        profile
            .path()
            .join("logs")
            .join(maho_ffi::panic_record::LOG_FILE),
    )
    .unwrap();
    let entry: serde_json::Value = serde_json::from_str(
        log.lines()
            .find(|l| l.contains("recorded panic for the log"))
            .expect("panic line"),
    )
    .unwrap();
    assert!(entry["location"]
        .as_str()
        .unwrap()
        .contains("panic_safety.rs"));
}

#[cfg(feature = "debug-test-hooks")]
#[test]
fn debug_induce_panic_export_is_contained() {
    assert!(!maho_ffi::maho_core_debug_induce_panic());
}

#[test]
fn release_profile_unwinds() {
    let manifest = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../../Cargo.toml")).unwrap();
    let release = manifest
        .split("[profile.release]")
        .nth(1)
        .expect("workspace release profile")
        .split("\n[")
        .next()
        .unwrap();
    assert!(release.contains("panic = \"unwind\""), "{release}");
    if !cfg!(debug_assertions) {
        assert!(cfg!(panic = "unwind"), "this release test binary was built with panic=abort");
    }
}

/// Every exported FFI function in the compiled lib.rs must contain a panic
/// before it reaches C++: either its body is wrapped in ffi_safe!/catch_unwind,
/// or it forwards to a wrapped implementation, or it is on the reviewed list
/// of trivially panic-free exports below.
#[test]
fn every_no_mangle_export_contains_panics() {
    let source = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/src/lib.rs")).unwrap();
    let unwrapped = unwrapped_exports(&source);
    assert!(
        unwrapped.is_empty(),
        "exports without ffi_safe!/catch_unwind: {unwrapped:?}"
    );
}

#[test]
fn audit_flags_an_unwrapped_export() {
    let fixture = "#[no_mangle]\npub extern \"C\" fn maho_new_unwrapped() -> bool {\n    true\n}\n";
    assert_eq!(unwrapped_exports(fixture), vec!["maho_new_unwrapped".to_string()]);
}

const REVIEWED_PANIC_FREE: &[&str] = &[
    "maho_ffi_serialization_scope_exit",
    "maho_core_dispatch_shell_event",
    "maho_core_free_string",
    "maho_trace_recorder_destroy",
    "maho_vault_free_secret_buffer",
    "maho_vault_enable_platform_device_protector",
    "maho_agent_create_session",
    "maho_agent_create_session_leased",
    "maho_agent_send_message",
    "maho_agent_send_message_leased",
];

fn unwrapped_exports(source: &str) -> Vec<String> {
    let mut found = Vec::new();
    let mut rest = source;
    while let Some(at) = rest.find("#[no_mangle]") {
        rest = &rest[at + "#[no_mangle]".len()..];
        let Some(fn_at) = rest.find("fn ") else { break };
        let name: String = rest[fn_at + 3..]
            .chars()
            .take_while(|c| c.is_alphanumeric() || *c == '_')
            .collect();
        let Some(open) = rest.find('{') else { break };
        let mut depth = 0usize;
        let mut end = open;
        for (i, ch) in rest[open..].char_indices() {
            match ch {
                '{' => depth += 1,
                '}' => {
                    depth -= 1;
                    if depth == 0 {
                        end = open + i;
                        break;
                    }
                }
                _ => {}
            }
        }
        let body = &rest[open..end];
        if !body.contains("ffi_safe!")
            && !body.contains("catch_unwind")
            && !REVIEWED_PANIC_FREE.contains(&name.as_str())
        {
            found.push(name);
        }
    }
    found
}
