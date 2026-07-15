// Copyright 2026 Maho Browser. All rights reserved.

#![allow(clippy::disallowed_methods)]

use std::ffi::CString;

use super::*;

#[test]
fn null_c_string_is_rejected() {
    assert!(c_string(std::ptr::null(), "arg").is_err());
}

#[test]
fn initialize_accepts_profile_path() {
    let version = CString::new("v1").unwrap();
    let path = CString::new("/tmp/maho-mail-ffi-test").unwrap();
    assert!(MahoMailInitialize(version.as_ptr(), path.as_ptr()));
}
