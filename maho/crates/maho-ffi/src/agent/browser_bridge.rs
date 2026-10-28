#[cfg(test)]
use crate::{leased_browser_tool_bridge, FfiBrowserToolBridge, SendableCallback, SendableUserData};
#[cfg(test)]
use std::ffi::{c_char, CStr, CString};
#[cfg(test)]
use std::ptr;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct MahoAgentToolResult {
    pub json_ptr: *mut std::os::raw::c_char,
    pub error_ptr: *mut std::os::raw::c_char,
    pub free_fn: Option<unsafe extern "C" fn(*mut std::os::raw::c_char)>,
    pub error_code_ptr: *mut std::os::raw::c_char,
    pub error_retryable: bool,
}

pub use crate::{
    maho_agent_artifact_path, maho_agent_list_artifacts, maho_agent_list_tools,
    maho_agent_set_artifact_root,
};

#[cfg(test)]
mod ffi_browser_tool_bridge_tests {
    use super::*;
    use maho_agent::BrowserToolBridge;
    use std::collections::VecDeque;
    use std::ffi::c_void;

    #[derive(Default)]
    struct FakeBrowserCallback {
        responses: std::sync::Mutex<VecDeque<Result<String, String>>>,
        calls: std::sync::Mutex<Vec<(String, String)>>,
    }

    unsafe extern "C" fn free_fake_result(pointer: *mut c_char) {
        if !pointer.is_null() {
            // SAFETY: every fake result pointer is created by CString::into_raw
            // below and returned to the bridge exactly once.
            drop(unsafe { CString::from_raw(pointer) });
        }
    }

    unsafe extern "C" fn fake_browser_callback(
        user_data: *mut c_void,
        request: *const c_char,
        args_json: *const c_char,
    ) -> MahoAgentToolResult {
        if user_data.is_null() || request.is_null() || args_json.is_null() {
            return MahoAgentToolResult {
                json_ptr: ptr::null_mut(),
                error_ptr: ptr::null_mut(),
                free_fn: Some(free_fake_result),
                error_code_ptr: ptr::null_mut(),
                error_retryable: false,
            };
        }
        // SAFETY: the test bridge passes this context and live C strings.
        let context = unsafe { &*(user_data as *const FakeBrowserCallback) };
        let request = unsafe { CStr::from_ptr(request) }
            .to_string_lossy()
            .into_owned();
        let args_json = unsafe { CStr::from_ptr(args_json) }
            .to_string_lossy()
            .into_owned();
        context
            .calls
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .push((request, args_json));
        let response = context
            .responses
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .pop_front();
        let mut result = MahoAgentToolResult {
            json_ptr: ptr::null_mut(),
            error_ptr: ptr::null_mut(),
            free_fn: Some(free_fake_result),
            error_code_ptr: ptr::null_mut(),
            error_retryable: false,
        };
        match response {
            Some(Ok(json)) => {
                result.json_ptr = CString::new(json)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut());
            }
            Some(Err(error)) => {
                result.error_ptr = CString::new(error)
                    .map(CString::into_raw)
                    .unwrap_or(ptr::null_mut());
            }
            None => {}
        }
        result
    }

    fn bridge_with_responses(
        responses: impl IntoIterator<Item = Result<serde_json::Value, String>>,
    ) -> (FfiBrowserToolBridge, Box<FakeBrowserCallback>) {
        let context = Box::new(FakeBrowserCallback {
            responses: std::sync::Mutex::new(
                responses
                    .into_iter()
                    .map(|response| response.map(|value| value.to_string()))
                    .collect(),
            ),
            calls: std::sync::Mutex::new(Vec::new()),
        });
        let bridge = FfiBrowserToolBridge {
            cb: SendableCallback(fake_browser_callback),
            user_data: SendableUserData((&*context as *const FakeBrowserCallback) as usize),
            _session_callback_lease: None,
            _turn_callback_lease: None,
        };
        (bridge, context)
    }

    fn descriptor(capability_id: &str, name: &str) -> serde_json::Value {
        serde_json::json!({
            "capabilityId": capability_id,
            "name": name,
            "description": format!("Desktop {name}"),
            "inputSchema": {"type": "object", "additionalProperties": false},
            "schemaVersion": 1,
            "policy": {"sensitive": false, "permission": "auto_approve"}
        })
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn desktop_discovery_requests_tools_list_and_decodes_two_descriptors() {
        let (bridge, context) = bridge_with_responses([Ok(serde_json::json!({
            "tools": [
                descriptor("browser.tabs.list", "browser_list_tabs"),
                descriptor("browser.page.read", "browser_read_page")
            ]
        }))]);

        let descriptors = bridge
            .list_tool_descriptors()
            .await
            .expect("typed discovery succeeds");
        assert_eq!(descriptors.len(), 2);
        assert_eq!(descriptors[0].capability_id, "browser.tabs.list");
        assert_eq!(descriptors[1].name, "browser_read_page");
        assert_eq!(
            *context
                .calls
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner),
            vec![("tools/list".to_string(), "{}".to_string())]
        );
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn desktop_discovery_fails_closed_on_malformed_or_missing_metadata() {
        for malformed in [
            serde_json::json!({"tools": [descriptor("browser.tabs.list", "browser_list_tabs"), {"name": "missing_metadata"}]}),
            serde_json::json!({"notTools": []}),
        ] {
            let (bridge, _context) = bridge_with_responses([Ok(malformed)]);
            let error = bridge
                .list_tool_descriptors()
                .await
                .expect_err("malformed discovery is rejected");
            assert_eq!(error.code, "malformed_discovery");
            assert!(!error.retryable);
        }
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn desktop_execution_uses_canonical_capability_id_and_preserves_typed_receipt() {
        let (bridge, context) = bridge_with_responses([Ok(serde_json::json!({
            "outputJson": "{\"ok\":true}",
            "receipt": {
                "capabilityId": "browser.tabs.list",
                "executionId": "desktop-execution-1",
                "metadata": {"source": "desktop"}
            }
        }))]);

        let execution = bridge
            .execute_tool("browser.tabs.list", r#"{"windowId":7}"#)
            .await
            .expect("typed execution succeeds");
        assert_eq!(execution.receipt.execution_id, "desktop-execution-1");
        assert_eq!(
            *context
                .calls
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner),
            vec![(
                "browser.tabs.list".to_string(),
                r#"{"windowId":7}"#.to_string()
            )]
        );
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn desktop_execution_rejects_receipt_mismatch_and_malformed_typed_metadata() {
        for (response, expected_code) in [
            (
                serde_json::json!({
                    "outputJson": "{}",
                    "receipt": {
                        "capabilityId": "browser.page.read",
                        "executionId": "wrong-capability",
                        "metadata": {}
                    }
                }),
                "receipt_mismatch",
            ),
            (
                serde_json::json!({
                    "outputJson": "{}",
                    "receipt": {
                        "capabilityId": "browser.tabs.list",
                        "executionId": "missing-metadata"
                    }
                }),
                "malformed_execution",
            ),
        ] {
            let (bridge, _context) = bridge_with_responses([Ok(response)]);
            let error = bridge
                .execute_tool("browser.tabs.list", "{}")
                .await
                .expect_err("invalid typed receipt is rejected");
            assert_eq!(error.code, expected_code);
        }
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn desktop_execution_wraps_legacy_raw_result_with_safe_boundary_receipt() {
        let (bridge, _context) =
            bridge_with_responses([Ok(serde_json::json!({"tabs": [{"id": 7}]}))]);
        let execution = bridge
            .execute_tool("browser.tabs.list", "{}")
            .await
            .expect("legacy result remains compatible");

        assert_eq!(execution.receipt.capability_id, "browser.tabs.list");
        assert!(!execution.receipt.execution_id.is_empty());
        assert_eq!(
            execution.receipt.metadata,
            serde_json::json!({"source": "maho-desktop-ffi-legacy-result"})
        );
        assert_eq!(execution.output_json, r#"{"tabs":[{"id":7}]}"#);
    }

    static TYPED_ERROR_FREE_COUNT: std::sync::atomic::AtomicUsize =
        std::sync::atomic::AtomicUsize::new(0);

    unsafe extern "C" fn free_typed_error_result(pointer: *mut c_char) {
        if !pointer.is_null() {
            TYPED_ERROR_FREE_COUNT.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            // SAFETY: each field is independently allocated with CString::into_raw.
            drop(unsafe { CString::from_raw(pointer) });
        }
    }

    unsafe extern "C" fn typed_policy_error_browser_callback(
        _user_data: *mut c_void,
        _request: *const c_char,
        _args_json: *const c_char,
    ) -> MahoAgentToolResult {
        MahoAgentToolResult {
            json_ptr: ptr::null_mut(),
            error_ptr: CString::new("Browser action denied by approval policy")
                .expect("test error contains no NUL")
                .into_raw(),
            free_fn: Some(free_typed_error_result),
            error_code_ptr: CString::new("policy_denied")
                .expect("test code contains no NUL")
                .into_raw(),
            error_retryable: false,
        }
    }

    unsafe extern "C" fn typed_transport_error_browser_callback(
        _user_data: *mut c_void,
        _request: *const c_char,
        _args_json: *const c_char,
    ) -> MahoAgentToolResult {
        MahoAgentToolResult {
            json_ptr: ptr::null_mut(),
            error_ptr: CString::new("Browser callback transport is unavailable")
                .expect("test error contains no NUL")
                .into_raw(),
            free_fn: Some(free_typed_error_result),
            error_code_ptr: CString::new("transport_unavailable")
                .expect("test code contains no NUL")
                .into_raw(),
            error_retryable: true,
        }
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn desktop_callback_preserves_typed_error_kind_and_frees_every_field() {
        for (callback, expected_code, expected_retryable) in [
            (
                typed_policy_error_browser_callback
                    as unsafe extern "C" fn(
                        *mut c_void,
                        *const c_char,
                        *const c_char,
                    ) -> MahoAgentToolResult,
                "policy_denied",
                false,
            ),
            (
                typed_transport_error_browser_callback
                    as unsafe extern "C" fn(
                        *mut c_void,
                        *const c_char,
                        *const c_char,
                    ) -> MahoAgentToolResult,
                "transport_unavailable",
                true,
            ),
        ] {
            TYPED_ERROR_FREE_COUNT.store(0, std::sync::atomic::Ordering::SeqCst);
            let bridge = FfiBrowserToolBridge {
                cb: SendableCallback(callback),
                user_data: SendableUserData(0),
                _session_callback_lease: None,
                _turn_callback_lease: None,
            };

            let error = bridge
                .execute_tool("browser.click", "{}")
                .await
                .expect_err("typed callback error propagates");
            assert_eq!(error.code, expected_code);
            assert_eq!(error.retryable, expected_retryable);
            assert_eq!(
                TYPED_ERROR_FREE_COUNT.load(std::sync::atomic::Ordering::SeqCst),
                2,
                "message and code allocations must each be freed exactly once"
            );
        }
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn desktop_callback_error_and_empty_result_fail_closed() {
        let (error_bridge, _context) =
            bridge_with_responses([Err("desktop callback unavailable".to_string())]);
        let error = error_bridge
            .list_tool_descriptors()
            .await
            .expect_err("callback error propagates");
        assert_eq!(error.code, "callback_error");
        assert!(error.retryable);

        let (offline_bridge, _context) = bridge_with_responses([]);
        let error = offline_bridge
            .list_tool_descriptors()
            .await
            .expect_err("empty callback result is offline");
        assert_eq!(error.code, "bridge_offline");
        assert!(error.retryable);
    }

    #[test]
    fn missing_desktop_callback_installs_no_browser_bridge() {
        assert!(leased_browser_tool_bridge(None, ptr::null_mut(), &None).is_none());
    }

    #[tokio::test(flavor = "multi_thread", worker_threads = 1)]
    async fn browser_host_callback_can_block_on_runtime_work() {
        unsafe extern "C" fn callback(
            _: *mut c_void,
            _: *const c_char,
            _: *const c_char,
        ) -> MahoAgentToolResult {
            tokio::runtime::Handle::current().block_on(async {
                tokio::task::yield_now().await;
            });
            MahoAgentToolResult {
                json_ptr: CString::new("{\"tools\":[]}").unwrap().into_raw(),
                error_ptr: ptr::null_mut(),
                free_fn: Some(free_fake_result),
                error_code_ptr: ptr::null_mut(),
                error_retryable: false,
            }
        }
        let bridge = FfiBrowserToolBridge {
            cb: SendableCallback(callback),
            user_data: SendableUserData(0),
            _session_callback_lease: None,
            _turn_callback_lease: None,
        };
        assert!(bridge.list_tool_descriptors().await.unwrap().is_empty());
    }
}
