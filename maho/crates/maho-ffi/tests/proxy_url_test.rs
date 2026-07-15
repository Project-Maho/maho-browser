use std::ffi::CStr;

#[test]
fn test_proxy_url_behavior() {
    // 1. Test returns env var when set
    std::env::set_var("MAHO_PROXY_URL", "http://localhost:8787/v1");
    let ptr = maho_ffi::maho_core_managed_proxy_url();
    let s = unsafe { CStr::from_ptr(ptr).to_str().unwrap() };
    assert_eq!(s, "http://localhost:8787/v1");
    unsafe {
        maho_ffi::maho_core_free_string(ptr);
    }

    // 2. Test returns default fallback when unset
    std::env::remove_var("MAHO_PROXY_URL");
    let ptr = maho_ffi::maho_core_managed_proxy_url();
    let s = unsafe { CStr::from_ptr(ptr).to_str().unwrap() };
    assert_eq!(s, "https://proxy.maho.co/v1");
    unsafe {
        maho_ffi::maho_core_free_string(ptr);
    }
}
