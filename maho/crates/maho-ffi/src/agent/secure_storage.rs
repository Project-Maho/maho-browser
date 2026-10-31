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

pub(crate) struct SecureKeyStrings(pub(crate) MahoAgentSecureKey);

pub(crate) fn invoke_permission(
    callback: unsafe extern "C" fn(
        *mut c_void,
        *const c_char,
        *const c_char,
    ) -> MahoAgentPermissionDecision,
    user_data: usize,
    tool: String,
    arguments: String,
) -> maho_agent::PermissionDecision {
    let (Ok(tool), Ok(arguments)) = (
        std::ffi::CString::new(tool),
        std::ffi::CString::new(arguments),
    ) else {
        return maho_agent::PermissionDecision::Deny;
    };
    // SAFETY: the caller holds the callback lease throughout this synchronous invocation.
    match tokio::task::block_in_place(|| unsafe {
        callback(user_data as *mut c_void, tool.as_ptr(), arguments.as_ptr())
    }) {
        MahoAgentPermissionDecision::Allow => maho_agent::PermissionDecision::Allow,
        MahoAgentPermissionDecision::Deny => maho_agent::PermissionDecision::Deny,
    }
}

impl Drop for SecureKeyStrings {
    fn drop(&mut self) {
        if let Some(free) = self.0.cstring_free_fn {
            for pointer in [self.0.base_url, self.0.model] {
                if !pointer.is_null() {
                    // SAFETY: the callback transfers these allocations to this guard exactly once.
                    unsafe { free(pointer) };
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;
    use std::sync::atomic::{AtomicUsize, Ordering};

    #[test]
    fn permission_nul_arguments_deny_without_calling_host() {
        unsafe extern "C" fn allow(
            _: *mut c_void,
            _: *const c_char,
            _: *const c_char,
        ) -> MahoAgentPermissionDecision {
            MahoAgentPermissionDecision::Allow
        }
        assert!(matches!(
            invoke_permission(allow, 0, "tool".into(), "real\0args".into()),
            maho_agent::PermissionDecision::Deny
        ));
    }

    #[tokio::test(flavor = "multi_thread", worker_threads = 1)]
    async fn permission_host_callback_can_block_on_runtime_work() {
        unsafe extern "C" fn callback(
            _: *mut c_void,
            _: *const c_char,
            _: *const c_char,
        ) -> MahoAgentPermissionDecision {
            tokio::runtime::Handle::current().block_on(async {
                tokio::task::yield_now().await;
            });
            MahoAgentPermissionDecision::Allow
        }
        assert!(matches!(
            invoke_permission(callback, 0, "tool".into(), "{}".into()),
            maho_agent::PermissionDecision::Allow
        ));
    }

    #[test]
    fn secure_key_strings_freed_on_early_return() {
        static FREED: AtomicUsize = AtomicUsize::new(0);
        unsafe extern "C" fn free(pointer: *mut c_char) {
            unsafe {
                drop(CString::from_raw(pointer));
            }
            FREED.fetch_add(1, Ordering::SeqCst);
        }
        let key = MahoAgentSecureKey {
            ptr: std::ptr::null_mut(),
            len: 0,
            free_fn: None,
            base_url: CString::new("url").unwrap().into_raw(),
            model: CString::new("model").unwrap().into_raw(),
            cstring_free_fn: Some(free),
        };
        let consume = || {
            let _guard = SecureKeyStrings(key);
            if key.ptr.is_null() {
                return false;
            }
            true
        };
        assert!(!consume());
        assert_eq!(FREED.load(Ordering::SeqCst), 2);
    }
}
