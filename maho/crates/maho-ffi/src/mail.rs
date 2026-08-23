//! Mail domain FFI bindings.

use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;

use maho_core::maho_core::MahoCore;

use crate::common::{cstr_to_str, to_c_string, to_json_cstring};
use crate::ffi_safe;
