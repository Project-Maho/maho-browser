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
