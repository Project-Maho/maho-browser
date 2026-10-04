use std::ffi::{c_char, CStr, CString};
use std::ptr;
use std::sync::{Arc, Mutex};
use zeroize::Zeroize;

use maho_agent::{MutexAgentStorage, SessionRuntime};
use maho_core::maho_core::MahoCore;

use crate::agent::callback_lease::{
    clone_callback_lease, AcceptedLeasedTurns, CallbackReleasePolicy, SendableCallback,
    SendableUserData, SessionCallbackLease, TurnCallbackLease,
};
use crate::agent::secure_storage::{MahoAgentPermissionDecision, MahoAgentSecureKey};
use crate::agent::session::{load_runtime_config_from_store, MahoAgentSession};
use crate::common::to_json_cstring;
use crate::ffi_safe;
use crate::import_gate;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct MahoAgentToolResult {
    pub json_ptr: *mut std::os::raw::c_char,
    pub error_ptr: *mut std::os::raw::c_char,
    pub free_fn: Option<unsafe extern "C" fn(*mut std::os::raw::c_char)>,
    pub error_code_ptr: *mut std::os::raw::c_char,
    pub error_retryable: bool,
}

struct FfiBrowserToolBridge {
    cb: SendableCallback<
        unsafe extern "C" fn(
            *mut std::ffi::c_void,
            *const c_char,
            *const c_char,
        ) -> MahoAgentToolResult,
    >,
    user_data: SendableUserData,
    _session_callback_lease: Option<Arc<SessionCallbackLease>>,
    _turn_callback_lease: Option<Arc<TurnCallbackLease>>,
}

impl FfiBrowserToolBridge {
    fn call_browser_callback(
        &self,
        request: &str,
        args_json: &str,
    ) -> Result<String, maho_agent::BrowserToolBridgeError> {
        let request = CString::new(request).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "invalid_request",
                format!("browser callback request contains NUL: {error}"),
                false,
            )
        })?;
        let args = CString::new(args_json).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "invalid_arguments",
                format!("browser callback arguments contain NUL: {error}"),
                false,
            )
        })?;
        let callback = self.cb.0;
        let user_data = self.user_data.0 as *mut std::ffi::c_void;
        // SAFETY: the callback and user data remain owned by the session/turn
        // lease, and both C strings remain live for the duration of this call.
        let result = unsafe { callback(user_data, request.as_ptr(), args.as_ptr()) };

        let read_and_free = |pointer: *mut c_char| {
            if pointer.is_null() {
                return None;
            }
            // SAFETY: the embedder transfers one NUL-terminated allocation in
            // each non-null result field. Copy it before invoking its matching
            // deallocator exactly once.
            let value = unsafe { CStr::from_ptr(pointer) }
                .to_string_lossy()
                .into_owned();
            if let Some(free_fn) = result.free_fn {
                // SAFETY: `free_fn` is the deallocator supplied for this result.
                unsafe { free_fn(pointer) };
            }
            Some(value)
        };
        let json = read_and_free(result.json_ptr);
        let error = read_and_free(result.error_ptr);
        let error_code = read_and_free(result.error_code_ptr);

        match (json, error) {
            (_, Some(message)) => Err(maho_agent::BrowserToolBridgeError::new(
                error_code.as_deref().unwrap_or("callback_error"),
                message,
                error_code.is_none() || result.error_retryable,
            )),
            (Some(json), None) => Ok(json),
            (None, None) => Err(maho_agent::BrowserToolBridgeError::new(
                "bridge_offline",
                "browser callback returned no result",
                true,
            )),
        }
    }
}

fn decode_ffi_browser_descriptors(
    response_json: &str,
) -> Result<Vec<maho_agent::BrowserToolDescriptor>, maho_agent::BrowserToolBridgeError> {
    let response: serde_json::Value = serde_json::from_str(response_json).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_discovery",
            format!("browser tools/list returned invalid JSON: {error}"),
            false,
        )
    })?;
    let tools = response.get("tools").cloned().ok_or_else(|| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_discovery",
            "browser tools/list response is missing typed tools",
            false,
        )
    })?;
    serde_json::from_value(tools).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_discovery",
            format!("browser tools/list returned invalid descriptors: {error}"),
            false,
        )
    })
}

fn decode_ffi_browser_execution(
    capability_id: &str,
    response_json: &str,
) -> Result<maho_agent::BrowserToolExecution, maho_agent::BrowserToolBridgeError> {
    let result: serde_json::Value = serde_json::from_str(response_json).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_execution",
            format!("browser execution returned invalid JSON: {error}"),
            false,
        )
    })?;
    let is_typed_envelope = result
        .as_object()
        .is_some_and(|object| object.contains_key("outputJson") || object.contains_key("receipt"));
    if is_typed_envelope {
        let execution = serde_json::from_value::<maho_agent::BrowserToolExecution>(result)
            .map_err(|error| {
                maho_agent::BrowserToolBridgeError::new(
                    "malformed_execution",
                    format!("browser execution returned an invalid typed receipt: {error}"),
                    false,
                )
            })?;
        if execution.receipt.capability_id != capability_id {
            return Err(maho_agent::BrowserToolBridgeError::new(
                "receipt_mismatch",
                "browser execution receipt capability does not match the requested capability",
                false,
            ));
        }
        if execution.receipt.execution_id.trim().is_empty() {
            return Err(maho_agent::BrowserToolBridgeError::new(
                "malformed_execution",
                "browser execution receipt is missing an execution ID",
                false,
            ));
        }
        return Ok(execution);
    }

    let output_json = serde_json::to_string(&result).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_execution",
            format!("browser execution result could not be serialized: {error}"),
            false,
        )
    })?;
    Ok(maho_agent::BrowserToolExecution {
        output_json,
        receipt: maho_agent::BrowserToolExecutionReceipt {
            capability_id: capability_id.to_string(),
            execution_id: uuid::Uuid::new_v4().to_string(),
            metadata: serde_json::json!({"source": "maho-desktop-ffi-legacy-result"}),
        },
    })
}

#[async_trait::async_trait]
impl maho_agent::BrowserToolBridge for FfiBrowserToolBridge {
    async fn list_tool_descriptors(
        &self,
    ) -> Result<Vec<maho_agent::BrowserToolDescriptor>, maho_agent::BrowserToolBridgeError> {
        let response = self.call_browser_callback("tools/list", "{}")?;
        decode_ffi_browser_descriptors(&response)
    }

    async fn execute_tool(
        &self,
        capability_id: &str,
        args_json: &str,
    ) -> Result<maho_agent::BrowserToolExecution, maho_agent::BrowserToolBridgeError> {
        serde_json::from_str::<serde_json::Value>(args_json).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "invalid_arguments",
                format!("browser tool arguments are not valid JSON: {error}"),
                false,
            )
        })?;
        let response = self.call_browser_callback(capability_id, args_json)?;
        decode_ffi_browser_execution(capability_id, &response)
    }
}

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

    #[tokio::test]
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

    #[tokio::test]
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

    #[tokio::test]
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

    #[tokio::test]
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

    #[tokio::test]
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

    #[tokio::test]
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

    #[tokio::test]
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
}

fn leased_permission_callback(
    callback: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            arguments_json: *const c_char,
        ) -> MahoAgentPermissionDecision,
    >,
    callback_user_data: *mut std::ffi::c_void,
    turn_callback_lease: &Option<Arc<TurnCallbackLease>>,
) -> Option<maho_agent::PermissionCallback> {
    callback.map(|callback| {
        let callback = SendableCallback(callback);
        let callback_user_data = SendableUserData(callback_user_data as usize);
        let turn_callback_lease = clone_callback_lease(turn_callback_lease);
        Box::new(move |request: maho_agent::PermissionRequest| {
            let _ = &turn_callback_lease;
            let decision = match (
                CString::new(request.tool_name),
                CString::new(request.arguments),
            ) {
                (Ok(tool_name), Ok(arguments)) => {
                    // SAFETY: the leased FFI turn owns `callback_user_data` through
                    // `turn_callback_lease`, and C-string arguments remain live for this call.
                    unsafe {
                        (callback.0)(
                            callback_user_data.0 as *mut std::ffi::c_void,
                            tool_name.as_ptr(),
                            arguments.as_ptr(),
                        )
                    }
                }
                _ => MahoAgentPermissionDecision::Deny,
            };
            let decision = match decision {
                MahoAgentPermissionDecision::Allow => maho_agent::PermissionDecision::Allow,
                MahoAgentPermissionDecision::Deny => maho_agent::PermissionDecision::Deny,
            };
            Box::pin(async move { decision })
                as std::pin::Pin<
                    Box<dyn std::future::Future<Output = maho_agent::PermissionDecision> + Send>,
                >
        }) as maho_agent::PermissionCallback
    })
}

fn leased_browser_tool_bridge(
    callback: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            args_json: *const c_char,
        ) -> MahoAgentToolResult,
    >,
    callback_user_data: *mut std::ffi::c_void,
    turn_callback_lease: &Option<Arc<TurnCallbackLease>>,
) -> Option<Arc<dyn maho_agent::BrowserToolBridge>> {
    callback.map(|callback| {
        Arc::new(FfiBrowserToolBridge {
            cb: SendableCallback(callback),
            user_data: SendableUserData(callback_user_data as usize),
            _session_callback_lease: None,
            _turn_callback_lease: clone_callback_lease(turn_callback_lease),
        }) as Arc<dyn maho_agent::BrowserToolBridge>
    })
}

pub(crate) unsafe fn maho_agent_create_session_impl(
    core: *mut MahoCore,
    session_id: *const c_char,
    workspace_root: *const c_char,
    allow_insecure_key_storage: bool,
    permission_cb: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            arguments_json: *const c_char,
        ) -> MahoAgentPermissionDecision,
    >,
    permission_user_data: *mut std::ffi::c_void,
    secure_storage_cb: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            provider: *const c_char,
        ) -> MahoAgentSecureKey,
    >,
    secure_storage_user_data: *mut std::ffi::c_void,
    browser_tool_cb: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            tool_name: *const c_char,
            args_json: *const c_char,
        ) -> MahoAgentToolResult,
    >,
    browser_tool_user_data: *mut std::ffi::c_void,
    space_id: *const c_char,
    callback_release_policy: Option<CallbackReleasePolicy>,
) -> *mut MahoAgentSession {
    ffi_safe!(
        {
            if core.is_null() || session_id.is_null() {
                tracing::debug!(
                    "[maho_agent_create_session] FAIL: null pointer args (core_null={}, session_id_null={})",
                    core.is_null(),
                    session_id.is_null(),
                );
                return std::ptr::null_mut();
            }
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                tracing::debug!(
                    "[maho_agent_create_session] FAIL: import_gate is active (data import in progress)"
                );
                return ptr::null_mut();
            }
            let Some(core_ptr) = std::ptr::NonNull::new(core) else {
                return std::ptr::null_mut();
            };
            // SAFETY: `core` was checked for null above and is required by this FFI
            // contract to point to a live `MahoCore` for the session lifetime.
            let core_ref = unsafe { core_ptr.as_ref() };
            let sid = match CStr::from_ptr(session_id).to_str() {
                Ok(s) => s.to_string(),
                Err(e) => {
                    tracing::debug!(
                        "[maho_agent_create_session] FAIL: session_id is not valid UTF-8: {e}"
                    );
                    return std::ptr::null_mut();
                }
            };

            let db_path = match core_ref.sqlite_db_path() {
                Some(p) => p,
                None => {
                    tracing::debug!(
                        "[maho_agent_create_session] FAIL: core.sqlite_db_path() is None \
                     (MahoCore was initialized without SQLite storage; session_id={sid})"
                    );
                    return std::ptr::null_mut();
                }
            };

            let workspace_path = if workspace_root.is_null() {
                let cwd = std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."));
                if cwd == std::path::Path::new("/") || cwd.as_os_str().is_empty() {
                    std::path::Path::new(db_path)
                        .parent()
                        .map(|p| p.to_path_buf())
                        .unwrap_or_else(|| {
                            std::env::var("HOME").map(std::path::PathBuf::from).unwrap_or(cwd)
                        })
                } else {
                    cwd
                }
            } else {
                match CStr::from_ptr(workspace_root).to_str() {
                    Ok(s) => std::path::PathBuf::from(s),
                    Err(_) => {
                        std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."))
                    }
                }
            };
            let session_workspace_root = workspace_path.clone();

            let storage = match maho_storage::sqlite::SqliteStorage::open(db_path) {
                Ok(s) => s,
                Err(e) => {
                    tracing::debug!(
                        "[maho_agent_create_session] FAIL: SqliteStorage::open failed \
                     (db_path={db_path:?}, session_id={sid}): {e}"
                    );
                    return std::ptr::null_mut();
                }
            };

            // Wave 1A session-open load: restore the persisted runtime-config
            // row into the initial Mutex value. Absent row or storage failure
            // falls back to the plumbing defaults (never blocks session open).
            let runtime_config = load_runtime_config_from_store(&storage, &sid);

            let isolation = match SessionRuntime::new() {
                Ok(i) => Arc::new(i),
                Err(e) => {
                    tracing::debug!(
                        "[maho_agent_create_session] FAIL: SessionRuntime::new() failed \
                     (Tokio runtime build error; session_id={sid}): {e}"
                    );
                    return std::ptr::null_mut();
                }
            };

            let agent_storage = MutexAgentStorage(Arc::new(Mutex::new(storage)));
            let callback_lease = callback_release_policy
                .map(|policy| SessionCallbackLease::new(Some(policy.callback), policy.user_data));

            let session_permission_callback = permission_cb;
            let permission_callback = session_permission_callback.map(|cb| {
                let user_data_ptr = SendableUserData(permission_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let callback_lease = clone_callback_lease(&callback_lease);
                let callback: Box<
                    dyn Fn(
                            maho_agent::PermissionRequest,
                        ) -> std::pin::Pin<
                            Box<
                                dyn std::future::Future<Output = maho_agent::PermissionDecision>
                                    + Send,
                            >,
                        > + Send
                        + Sync
                        + 'static,
                > = Box::new(move |req| {
                    let _ = &callback_lease;
                    let cb = cb_sendable.0;
                    let user_data = user_data_ptr.0 as *mut std::ffi::c_void;
                    let tool_name_c = match CString::new(req.tool_name) {
                        Ok(s) => s,
                        Err(_) => CString::new("unknown").unwrap_or_default(),
                    };
                    let arguments_c = match CString::new(req.arguments) {
                        Ok(s) => s,
                        Err(_) => CString::new("{}").unwrap_or_default(),
                    };
                    let decision =
                        unsafe { cb(user_data, tool_name_c.as_ptr(), arguments_c.as_ptr()) };
                    let p_decision = if decision == MahoAgentPermissionDecision::Allow {
                        maho_agent::PermissionDecision::Allow
                    } else {
                        maho_agent::PermissionDecision::Deny
                    };
                    Box::pin(async move { p_decision })
                });
                callback
            });

            let runtime = maho_agent::omo::factory::create_agent_runtime(
                Arc::new(agent_storage),
                permission_callback,
                workspace_path.clone(),
                allow_insecure_key_storage,
            );
            // Automatically attach durable session journal with disk write-through persistence
            runtime.attach_session_journal(&sid, &workspace_path);

            if let Some(cb) = secure_storage_cb {
                let user_data_ptr = SendableUserData(secure_storage_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let callback_lease = clone_callback_lease(&callback_lease);
                let callback = Box::new(move |provider: &str| {
                    let _ = &callback_lease;
                    let cb = cb_sendable.0;
                    let user_data = user_data_ptr.0 as *mut std::ffi::c_void;
                    let provider_c = match CString::new(provider) {
                        Ok(s) => s,
                        Err(_) => {
                            return Err(maho_agent::AgentError::ExecutionError(
                                "Invalid provider name".to_string(),
                            ));
                        }
                    };
                    let secure_key = unsafe { cb(user_data, provider_c.as_ptr()) };
                    if secure_key.ptr.is_null() {
                        return Err(maho_agent::CredentialError::ProviderNotConfigured.into());
                    }
                    if secure_key.len == 0 {
                        if let Some(free_fn) = secure_key.free_fn {
                            // SAFETY: the secure-storage callback transferred this non-null
                            // allocation and its declared length to the FFI adapter.
                            unsafe { free_fn(secure_key.ptr, secure_key.len) };
                        }
                        return Err(maho_agent::CredentialError::CredentialUnusable.into());
                    }
                    let key_slice = unsafe {
                        std::slice::from_raw_parts(secure_key.ptr as *const u8, secure_key.len)
                    };
                    let key_bytes = key_slice.to_vec();

                    unsafe {
                        std::ptr::write_bytes(secure_key.ptr, 0, secure_key.len);
                    }

                    if let Some(free_fn) = secure_key.free_fn {
                        unsafe {
                            free_fn(secure_key.ptr, secure_key.len);
                        }
                    }

                    let key_str = match String::from_utf8(key_bytes) {
                        Ok(s) => s,
                        Err(error) => {
                            let mut bytes = error.into_bytes();
                            bytes.zeroize();
                            return Err(maho_agent::CredentialError::CredentialDecryptFailed.into());
                        }
                    };

                    let base_url_opt = if !secure_key.base_url.is_null() {
                        let s = unsafe { CStr::from_ptr(secure_key.base_url) }
                            .to_string_lossy()
                            .into_owned();
                        if let Some(free_fn) = secure_key.cstring_free_fn {
                            unsafe {
                                free_fn(secure_key.base_url);
                            }
                        }
                        Some(s)
                    } else {
                        None
                    };

                    let model_opt = if !secure_key.model.is_null() {
                        let s = unsafe { CStr::from_ptr(secure_key.model) }
                            .to_string_lossy()
                            .into_owned();
                        if let Some(free_fn) = secure_key.cstring_free_fn {
                            unsafe {
                                free_fn(secure_key.model);
                            }
                        }
                        Some(s)
                    } else {
                        None
                    };

                    Ok(maho_agent::SecureKeyBundle {
                        key: maho_agent::ZeroizedString::new(key_str),
                        base_url: base_url_opt,
                        model: model_opt,
                    })
                });
                if runtime.set_secure_storage_callback(callback).is_err() {
                    return std::ptr::null_mut();
                }
            }

            let session_browser_tool_callback = browser_tool_cb;
            if let Some(cb) = session_browser_tool_callback {
                let user_data_ptr = SendableUserData(browser_tool_user_data as usize);
                let cb_sendable = SendableCallback(cb);
                let bridge: std::sync::Arc<dyn maho_agent::BrowserToolBridge> =
                    std::sync::Arc::new(FfiBrowserToolBridge {
                        cb: cb_sendable,
                        user_data: user_data_ptr,
                        _session_callback_lease: clone_callback_lease(&callback_lease),
                        _turn_callback_lease: None,
                    });
                if runtime.set_browser_tool_bridge(bridge).is_err() {
                    return std::ptr::null_mut();
                }
            }

            let active_space_id = if space_id.is_null() {
                None
            } else {
                match CStr::from_ptr(space_id).to_str() {
                    Ok(sid) if !sid.is_empty() => Some(sid.to_string()),
                    Ok(_) | Err(_) => None,
                }
            };
            if let Some(sid) = active_space_id.as_ref() {
                runtime.set_active_space_id(Some(sid.clone()));
            }

            core_ref.mark_conversation_session_active(&sid);
            let session = Box::new(MahoAgentSession {
                runtime,
                isolation,
                callback_lease,
                accepted_leased_turns: Arc::new(AcceptedLeasedTurns {
                    current: Mutex::new(None),
                }),
                permission_callback: session_permission_callback,
                browser_tool_callback: session_browser_tool_callback,
                session_id: sid.clone(),
                active_conversation_registry: core_ref.active_conversation_registry(),
                core: core_ptr,
                workspace_root: session_workspace_root,
                active_space_id,
                artifact_created_cb: None,
                artifact_created_user_data: 0,
                unified_event_registration: Arc::new(Mutex::new(None)),
                turn_controller: Arc::new(Mutex::new(
                    maho_agent::turn_control::SessionTurnController::new(&sid),
                )),
                wait_registry: Arc::new(Mutex::new(maho_agent::event_wait::WaitRegistry::new())),
                event_seq_counter: Arc::new(std::sync::atomic::AtomicU64::new(1)),
                notification_permission_granted: Arc::new(std::sync::atomic::AtomicBool::new(true)),
                runtime_config: Mutex::new(runtime_config),
            });
            if let Some(callback_lease) = session.callback_lease.as_ref() {
                callback_lease.arm();
            }
            Box::into_raw(session)
        },
        std::ptr::null_mut()
    )
}

/// # Safety
/// `core` must be a valid pointer to a `MahoCore`.
/// `session_id` must be a null-terminated C string.

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
pub unsafe extern "C" fn maho_agent_list_tools(session: *mut MahoAgentSession) -> *mut c_char {
    ffi_safe!(
        {
            if session.is_null() {
                return std::ptr::null_mut();
            }
            let session = &*session;
            let runtime = Arc::clone(&session.runtime);

            let isolation = Arc::clone(&session.isolation);
            let tools_res = isolation.block_on(async move { runtime.list_tools().await });

            let tools = match tools_res {
                Ok(Ok(t)) => t,
                _ => return std::ptr::null_mut(),
            };

            to_json_cstring(&tools)
        },
        std::ptr::null_mut()
    )
}

/// # Safety
/// `session` must be a valid pointer returned by a `maho_agent_create_session*`
/// function. `path` must be a null-terminated UTF-8 string, or null to clear.
/// Sets the session's artifact storage root; the fs_write tool and artifact
/// index resolve relative paths against it.
pub unsafe extern "C" fn maho_agent_set_artifact_root(
    session: *mut MahoAgentSession,
    path: *const c_char,
) {
    ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            let session = &*session;
            let root = if path.is_null() {
                None
            } else {
                match CStr::from_ptr(path).to_str() {
                    Ok(s) => Some(std::path::PathBuf::from(s)),
                    Err(_) => return,
                }
            };
            session.runtime.set_artifact_root(root);
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer returned by a `maho_agent_create_session*`
/// function. Returns a newly allocated JSON array of ArtifactInfo for the
/// session (caller frees via `maho_free_string`), or null on error.
pub unsafe extern "C" fn maho_agent_list_artifacts(session: *mut MahoAgentSession) -> *mut c_char {
    ffi_safe!(
        {
            if session.is_null() {
                return std::ptr::null_mut();
            }
            let session = &*session;
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();
            let isolation = Arc::clone(&session.isolation);
            let res = isolation.block_on(async move { runtime.list_artifacts(&session_id).await });
            let artifacts = match res {
                Ok(Ok(a)) => a,
                _ => return std::ptr::null_mut(),
            };
            to_json_cstring(&artifacts)
        },
        std::ptr::null_mut()
    )
}

/// # Safety
/// `session` must be a valid pointer returned by a `maho_agent_create_session*`
/// function. `artifact_id` must be a null-terminated UTF-8 string. Returns a
/// newly allocated root-relative path string (caller frees via
/// `maho_free_string`), or null for an unknown id or error. Opaque ids cross
/// the boundary; the resolved path is only produced here.
pub unsafe extern "C" fn maho_agent_artifact_path(
    session: *mut MahoAgentSession,
    artifact_id: *const c_char,
) -> *mut c_char {
    ffi_safe!(
        {
            if session.is_null() || artifact_id.is_null() {
                return std::ptr::null_mut();
            }
            let session = &*session;
            let artifact_id = match CStr::from_ptr(artifact_id).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return std::ptr::null_mut(),
            };
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();
            let isolation = Arc::clone(&session.isolation);
            let res = isolation.block_on(async move {
                runtime
                    .artifact_storage_rel_path(&session_id, &artifact_id)
                    .await
            });
            match res {
                Ok(Ok(Some(path))) => CString::new(path)
                    .map(|c| c.into_raw())
                    .unwrap_or(std::ptr::null_mut()),
                _ => std::ptr::null_mut(),
            }
        },
        std::ptr::null_mut()
    )
}
