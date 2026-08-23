use std::ffi::{c_char, c_void};

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum MahoAgentPermissionDecision {
    Allow = 0,
    Deny = 1,
}

pub type MahoAgentPermissionCallback = extern "C" fn(
    user_data: *mut std::ffi::c_void,
    tool_name: *const c_char,
    arguments: *const c_char,
) -> MahoAgentPermissionDecision;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct MahoAgentSecureKey {
    pub ptr: *mut std::os::raw::c_char,
    pub len: usize,
    pub free_fn: Option<unsafe extern "C" fn(*mut std::os::raw::c_char, usize)>,
    pub base_url: *mut std::os::raw::c_char,
    pub model: *mut std::os::raw::c_char,
    pub cstring_free_fn: Option<unsafe extern "C" fn(*mut std::os::raw::c_char)>,
}

pub type MahoAgentSecureStorageCallback =
    extern "C" fn(user_data: *mut std::ffi::c_void, provider: *const c_char) -> MahoAgentSecureKey;
