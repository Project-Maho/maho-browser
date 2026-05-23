//! C-ABI surface for the ACP (Agent Client Protocol) adapter.
//!
//! This module exposes `maho-acp`'s async Rust client through a stable C-ABI
//! so that the Chromium C++ shim (Phase 4) can drive ACP sessions without any
//! Rust knowledge beyond the generated header.
//!
//! # Event blob format
//!
//! Events are delivered to the C side as a **length-prefixed JSON blob**:
//!
//! ```text
//! [4 bytes: little-endian u32 payload length][payload: UTF-8 JSON]
//! ```
//!
//! The JSON payload is a `serde_json` serialization of [`FfiEvent`] using
//! `#[serde(tag = "kind", content = "data")]` — i.e. an externally-tagged
//! object like:
//!
//! ```json
//! {"kind":"InitializeOk","data":{"protocol_version":1,...}}
//! ```
//!
//! Use [`decode_ffi_event`] on the Rust side (test harness) to deserialize
//! the blob back into an `FfiEvent`.
//!
//! # Threading
//!
//! All events for a given handle are delivered serially from a single dedicated
//! tokio task. The C side must NOT assume any particular thread identity; it
//! must trampoline onto its own runner (e.g. `base::SequencedTaskRunner`)
//! before touching shared state.
//!
//! # Error codes
//!
//! All `-> i32` functions return 0 on success. Non-zero values:
//!
//! | Code | Meaning |
//! |------|---------|
//! | 0 | Success |
//! | 1 | Null handle |
//! | 2 | Invalid argument (null string, bad UTF-8) |
//! | 3 | Not started / not initialized |
//! | 4 | Already started |
//! | 5 | Spawn / I/O error |
//! | 6 | Internal error |

use std::ffi::{c_char, c_void, CStr};
use std::path::{Path, PathBuf};
use std::ptr;
use std::sync::{Arc, Mutex as StdMutex};

use serde::{Deserialize, Serialize};
use tokio::runtime::Runtime;
use tokio::sync::Mutex;

use maho_acp::{AcpClient, AcpUpdate, PermissionOutcome};

/// Success.
pub const MAHO_ACP_OK: i32 = 0;
/// Null handle passed.
pub const MAHO_ACP_ERR_NULL_HANDLE: i32 = 1;
/// Invalid argument (null string pointer, bad UTF-8, etc.).
pub const MAHO_ACP_ERR_INVALID_ARG: i32 = 2;
/// Operation requires a started client but it has not been started.
pub const MAHO_ACP_ERR_NOT_STARTED: i32 = 3;
/// Client is already started.
pub const MAHO_ACP_ERR_ALREADY_STARTED: i32 = 4;
/// Spawn or I/O error.
pub const MAHO_ACP_ERR_SPAWN: i32 = 5;
/// Internal / unexpected error.
pub const MAHO_ACP_ERR_INTERNAL: i32 = 6;

/// Tagged event delivered to the C side via the event callback.
///
/// Serialized with `tag = "kind"`, `content = "data"` so the JSON shape is:
/// ```json
/// {"kind":"VariantName","data":{...}}
/// ```
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "kind", content = "data")]
pub enum FfiEvent {
    InitializeOk {
        protocol_version: u32,
        agent_capabilities: serde_json::Value,
        auth_methods: serde_json::Value,
    },
    InitializeErr {
        code: i64,
        message: String,
    },
    SessionCreated {
        session_id: String,
        models: serde_json::Value,
        modes: serde_json::Value,
        config_options: serde_json::Value,
    },
    SessionCreateErr {
        code: i64,
        message: String,
    },
    SessionClosed {
        session_id: String,
    },
    SessionCloseErr {
        code: i64,
        message: String,
    },
    Update(FfiAcpUpdate),
    PermissionRequest {
        json_rpc_id: i64,
        session_id: String,
        tool_call: serde_json::Value,
        options: serde_json::Value,
    },
    PromptComplete {
        session_id: String,
        stop_reason: String,
        usage: serde_json::Value,
    },
    PromptErr {
        session_id: String,
        code: i64,
        message: String,
        auth_setup_command: Option<String>,
    },
    TransportError {
        message: String,
    },
    ChildExited {
        exit_code: Option<i32>,
    },
}

/// Mirror of `maho_acp::AcpUpdate` that is `Serialize + Deserialize`.
/// We re-encode the Rust enum into this FFI-friendly shape so the C side
/// gets a single stable JSON schema.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "update_kind", content = "update_data")]
pub enum FfiAcpUpdate {
    AgentMessageChunk {
        message_id: String,
        text: String,
    },
    AgentThoughtChunk {
        message_id: String,
        text: String,
    },
    UserMessageChunk {
        message_id: String,
        text: String,
    },
    Plan {
        entries: serde_json::Value,
    },
    ToolCall {
        id: String,
        title: String,
        kind: String,
        status: String,
        locations: serde_json::Value,
        raw_input: serde_json::Value,
    },
    ToolCallUpdate {
        id: String,
        status: String,
        content_text: Option<String>,
        raw_output: Option<serde_json::Value>,
    },
    AvailableCommandsUpdate {
        commands: serde_json::Value,
    },
    ConfigOptionUpdate {
        options: serde_json::Value,
    },
    UsageUpdate {
        used: Option<u64>,
        size: Option<u64>,
        cost: Option<serde_json::Value>,
    },
    Other {
        method: String,
        raw_params: serde_json::Value,
    },
}

fn acp_update_to_ffi(update: &AcpUpdate) -> FfiAcpUpdate {
    match update {
        AcpUpdate::AgentMessageChunk { message_id, text } => FfiAcpUpdate::AgentMessageChunk {
            message_id: message_id.clone(),
            text: text.clone(),
        },
        AcpUpdate::AgentThoughtChunk { message_id, text } => FfiAcpUpdate::AgentThoughtChunk {
            message_id: message_id.clone(),
            text: text.clone(),
        },
        AcpUpdate::UserMessageChunk { message_id, text } => FfiAcpUpdate::UserMessageChunk {
            message_id: message_id.clone(),
            text: text.clone(),
        },
        AcpUpdate::Plan { entries } => FfiAcpUpdate::Plan {
            entries: serde_json::to_value(entries).unwrap_or_default(),
        },
        AcpUpdate::ToolCall {
            id,
            title,
            kind,
            status,
            locations,
            raw_input,
        } => FfiAcpUpdate::ToolCall {
            id: id.clone(),
            title: title.clone(),
            kind: format!("{:?}", kind).to_lowercase(),
            status: format!("{:?}", status).to_lowercase(),
            locations: serde_json::to_value(locations).unwrap_or_default(),
            raw_input: raw_input.clone(),
        },
        AcpUpdate::ToolCallUpdate {
            id,
            status,
            content_text,
            raw_output,
        } => FfiAcpUpdate::ToolCallUpdate {
            id: id.clone(),
            status: format!("{:?}", status).to_lowercase(),
            content_text: content_text.clone(),
            raw_output: raw_output.clone(),
        },
        AcpUpdate::AvailableCommandsUpdate { commands } => {
            FfiAcpUpdate::AvailableCommandsUpdate {
                commands: serde_json::to_value(commands).unwrap_or_default(),
            }
        }
        AcpUpdate::ConfigOptionUpdate { options } => FfiAcpUpdate::ConfigOptionUpdate {
            options: options.clone(),
        },
        AcpUpdate::UsageUpdate { used, size, cost } => FfiAcpUpdate::UsageUpdate {
            used: *used,
            size: *size,
            cost: cost
                .as_ref()
                .and_then(|c| serde_json::to_value(c).ok()),
        },
        AcpUpdate::Other { method, raw_params } => FfiAcpUpdate::Other {
            method: method.clone(),
            raw_params: raw_params.clone(),
        },
    }
}

/// Returns a copy-pasteable shell command for setting up provider auth in the
/// isolated opencode instance, if the error message indicates a missing/invalid
/// auth credential. Returns `None` for unrelated errors.
fn detect_auth_command(message: &str, xdg_root: &Path) -> Option<String> {
    let lower = message.to_lowercase();
    let is_auth_error = lower.contains("auth")
        || lower.contains("unauthorized")
        || lower.contains("credentials")
        || lower.contains("api key")
        || lower.contains("not authenticated")
        || lower.contains("401");
    if !is_auth_error {
        return None;
    }
    let root = xdg_root.display();
    Some(format!(
        "XDG_CONFIG_HOME='{root}/config' \\\n\
         XDG_DATA_HOME='{root}/data' \\\n\
         XDG_STATE_HOME='{root}/state' \\\n\
         XDG_CACHE_HOME='{root}/cache' \\\n\
         opencode auth login"
    ))
}

/// Encode an [`FfiEvent`] into the length-prefixed blob format.
///
/// Format: `[4 bytes LE u32 length][JSON payload]`
pub fn encode_ffi_event(event: &FfiEvent) -> Vec<u8> {
    let json = serde_json::to_vec(event).expect("FfiEvent serialization cannot fail");
    let len = json.len() as u32;
    let mut buf = Vec::with_capacity(4 + json.len());
    buf.extend_from_slice(&len.to_le_bytes());
    buf.extend_from_slice(&json);
    buf
}

/// Decode a length-prefixed blob back into an [`FfiEvent`].
///
/// This is the inverse of [`encode_ffi_event`] and is intended for use in
/// test harnesses that collect raw callback blobs.
pub fn decode_ffi_event(bytes: &[u8]) -> Result<FfiEvent, String> {
    if bytes.len() < 4 {
        return Err("blob too short for length prefix".to_string());
    }
    let len = u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]) as usize;
    if bytes.len() < 4 + len {
        return Err(format!(
            "blob truncated: expected {} payload bytes, got {}",
            len,
            bytes.len() - 4
        ));
    }
    let json_slice = &bytes[4..4 + len];
    serde_json::from_slice(json_slice).map_err(|e| format!("JSON decode error: {}", e))
}

type EventCallback = extern "C" fn(*mut c_void, *const u8, usize);

struct CallbackState {
    fn_ptr: EventCallback,
    user_data: *mut c_void,
}

// SAFETY: The user_data pointer is opaque and only passed back to the C
// callback. The C side is responsible for thread safety of user_data.
unsafe impl Send for CallbackState {}
unsafe impl Sync for CallbackState {}

struct HandleInner {
    runtime: Runtime,
    client: Mutex<Option<AcpClient>>,
    callback: StdMutex<Option<CallbackState>>,
    active_session: StdMutex<Option<String>>,
    auth_config: StdMutex<Option<maho_acp::process::AuthConfig>>,
}

/// Opaque handle exposed to C. Created by [`maho_acp_create`], freed by
/// [`maho_acp_destroy`].
pub struct MahoAcpHandle {
    inner: Arc<HandleInner>,
}

impl HandleInner {
    fn fire_event(&self, event: &FfiEvent) {
        let guard = self.callback.lock().unwrap();
        if let Some(cb) = guard.as_ref() {
            let blob = encode_ffi_event(event);
            (cb.fn_ptr)(cb.user_data, blob.as_ptr(), blob.len());
        }
    }
}

/// Allocate a new ACP handle. Returns null on failure (should not happen).
#[no_mangle]
pub extern "C" fn maho_acp_create() -> *mut MahoAcpHandle {
    let runtime = match Runtime::new() {
        Ok(rt) => rt,
        Err(_) => return ptr::null_mut(),
    };
    let handle = MahoAcpHandle {
        inner: Arc::new(HandleInner {
            runtime,
            client: Mutex::new(None),
            callback: StdMutex::new(None),
            active_session: StdMutex::new(None),
            auth_config: StdMutex::new(None),
        }),
    };
    Box::into_raw(Box::new(handle))
}

/// Destroy an ACP handle. Calls shutdown if still running. Safe to call
/// with null (no-op).
///
/// # Safety
/// `handle` must be a valid pointer returned by [`maho_acp_create`], or null.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_destroy(handle: *mut MahoAcpHandle) {
    if handle.is_null() {
        return;
    }
    // SAFETY: handle was allocated by Box::into_raw in maho_acp_create.
    let boxed = Box::from_raw(handle);
    // Attempt shutdown if client is still alive.
    let inner = boxed.inner.clone();
    inner.runtime.block_on(async {
        let client = inner.client.lock().await.take();
        if let Some(c) = client {
            let _ = c.shutdown().await;
        }
    });
    // Runtime and all resources are dropped here.
}

/// Register the event callback. Only one callback per handle; subsequent
/// calls replace the previous one.
///
/// # Safety
/// `handle` must be valid. `fn_ptr` must be a valid function pointer that
/// remains valid for the lifetime of the handle.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_set_event_callback(
    handle: *mut MahoAcpHandle,
    fn_ptr: EventCallback,
    user_data: *mut c_void,
) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    // SAFETY: handle is valid per caller contract.
    let h = &*handle;
    let mut guard = h.inner.callback.lock().unwrap();
    *guard = Some(CallbackState { fn_ptr, user_data });
    MAHO_ACP_OK
}

/// Spawn the `opencode acp` child process. If `command_path` is null or
/// empty, falls back to `"opencode"`.
///
/// # Safety
/// `handle` must be valid. `command_path` must be a valid C string or null.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_start(
    handle: *mut MahoAcpHandle,
    command_path: *const c_char,
) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    // SAFETY: handle is valid per caller contract.
    let h = &*handle;

    let cmd = if command_path.is_null() {
        "opencode".to_string()
    } else {
        // SAFETY: command_path is a valid C string per caller contract.
        match CStr::from_ptr(command_path).to_str() {
            Ok("") => "opencode".to_string(),
            Ok(s) => s.to_string(),
            Err(_) => return MAHO_ACP_ERR_INVALID_ARG,
        }
    };

    let inner = h.inner.clone();
    inner.runtime.block_on(async {
        let mut guard = inner.client.lock().await;
        if guard.is_some() {
            return MAHO_ACP_ERR_ALREADY_STARTED;
        }
        let auth = inner.auth_config.lock().unwrap().clone();
        match AcpClient::start_with_auth(&cmd, auth.as_ref()).await {
            Ok(client) => {
                *guard = Some(client);
                MAHO_ACP_OK
            }
            Err(_) => MAHO_ACP_ERR_SPAWN,
        }
    })
}

/// # Safety
/// `handle` must be valid. `json_str` must be a valid C string or null.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_set_auth_config(
    handle: *mut MahoAcpHandle,
    json_str: *const c_char,
) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    let h = &*handle;

    if json_str.is_null() {
        let mut guard = h.inner.auth_config.lock().unwrap();
        *guard = None;
        return MAHO_ACP_OK;
    }

    let s = match CStr::from_ptr(json_str).to_str() {
        Ok(s) => s,
        Err(_) => return MAHO_ACP_ERR_INVALID_ARG,
    };

    if s.is_empty() {
        let mut guard = h.inner.auth_config.lock().unwrap();
        *guard = None;
        return MAHO_ACP_OK;
    }

    match serde_json::from_str::<maho_acp::process::AuthConfig>(s) {
        Ok(config) => {
            let mut guard = h.inner.auth_config.lock().unwrap();
            *guard = Some(config);
            MAHO_ACP_OK
        }
        Err(_) => MAHO_ACP_ERR_INVALID_ARG,
    }
}

/// Graceful shutdown: close stdin, SIGTERM, SIGKILL.
///
/// # Safety
/// `handle` must be valid.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_shutdown(handle: *mut MahoAcpHandle) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    // SAFETY: handle is valid per caller contract.
    let h = &*handle;
    let inner = h.inner.clone();
    inner.runtime.block_on(async {
        let client = inner.client.lock().await.take();
        match client {
            Some(c) => match c.shutdown().await {
                Ok(()) => MAHO_ACP_OK,
                Err(_) => MAHO_ACP_ERR_INTERNAL,
            },
            None => MAHO_ACP_ERR_NOT_STARTED,
        }
    })
}

/// Send `initialize` to the agent. Result delivered as `InitializeOk` or
/// `InitializeErr` via the event callback.
///
/// # Safety
/// `handle` must be valid.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_initialize(handle: *mut MahoAcpHandle) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    // SAFETY: handle is valid per caller contract.
    let h = &*handle;
    let inner = h.inner.clone();
    let rt = inner.runtime.handle().clone();

    rt.spawn(async move {
        let guard = inner.client.lock().await;
        let client = match guard.as_ref() {
            Some(c) => c,
            None => {
                inner.fire_event(&FfiEvent::InitializeErr {
                    code: MAHO_ACP_ERR_NOT_STARTED as i64,
                    message: "client not started".to_string(),
                });
                return;
            }
        };
        match client.initialize().await {
            Ok(result) => {
                inner.fire_event(&FfiEvent::InitializeOk {
                    protocol_version: result.protocol_version,
                    agent_capabilities: result.agent_capabilities.clone(),
                    auth_methods: serde_json::to_value(&result.auth_methods)
                        .unwrap_or_default(),
                });
            }
            Err(e) => {
                let (code, message) = match &e {
                    maho_acp::Error::JsonRpc { code, message } => (*code, message.clone()),
                    other => (-1, other.to_string()),
                };
                inner.fire_event(&FfiEvent::InitializeErr { code, message });
            }
        }
    });

    MAHO_ACP_OK
}

/// Create a new session with `cwd = current dir, mcpServers = []`.
///
/// # Safety
/// `handle` must be valid.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_create_session(handle: *mut MahoAcpHandle) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    // SAFETY: handle is valid per caller contract.
    let h = &*handle;
    let inner = h.inner.clone();
    let rt = inner.runtime.handle().clone();

    rt.spawn(async move {
        let guard = inner.client.lock().await;
        let client = match guard.as_ref() {
            Some(c) => c,
            None => {
                inner.fire_event(&FfiEvent::SessionCreateErr {
                    code: MAHO_ACP_ERR_NOT_STARTED as i64,
                    message: "client not started".to_string(),
                });
                return;
            }
        };
        let cwd = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
        match client.new_session(cwd, vec![]).await {
            Ok(result) => {
                {
                    let mut session = inner.active_session.lock().unwrap();
                    *session = Some(result.session_id.clone());
                }
                inner.fire_event(&FfiEvent::SessionCreated {
                    session_id: result.session_id,
                    models: result.models,
                    modes: result.modes,
                    config_options: result.config_options,
                });
            }
            Err(e) => {
                let (code, message) = match &e {
                    maho_acp::Error::JsonRpc { code, message } => (*code, message.clone()),
                    other => (-1, other.to_string()),
                };
                inner.fire_event(&FfiEvent::SessionCreateErr { code, message });
            }
        }
    });

    MAHO_ACP_OK
}

/// Close a session.
///
/// # Safety
/// `handle` and `session_id` must be valid.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_close_session(
    handle: *mut MahoAcpHandle,
    session_id: *const c_char,
) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    if session_id.is_null() {
        return MAHO_ACP_ERR_INVALID_ARG;
    }
    // SAFETY: handle and session_id are valid per caller contract.
    let h = &*handle;
    let sid = match CStr::from_ptr(session_id).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return MAHO_ACP_ERR_INVALID_ARG,
    };
    let inner = h.inner.clone();
    let rt = inner.runtime.handle().clone();

    rt.spawn(async move {
        let guard = inner.client.lock().await;
        let client = match guard.as_ref() {
            Some(c) => c,
            None => {
                inner.fire_event(&FfiEvent::SessionCloseErr {
                    code: MAHO_ACP_ERR_NOT_STARTED as i64,
                    message: "client not started".to_string(),
                });
                return;
            }
        };
        match client.close_session(&sid).await {
            Ok(()) => {
                inner.fire_event(&FfiEvent::SessionClosed {
                    session_id: sid,
                });
            }
            Err(e) => {
                let (code, message) = match &e {
                    maho_acp::Error::JsonRpc { code, message } => (*code, message.clone()),
                    other => (-1, other.to_string()),
                };
                inner.fire_event(&FfiEvent::SessionCloseErr { code, message });
            }
        }
    });

    MAHO_ACP_OK
}

/// Submit a prompt. Streaming updates and the final result are delivered
/// via the event callback.
///
/// # Safety
/// `handle`, `session_id`, and `text` must be valid C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_submit_prompt(
    handle: *mut MahoAcpHandle,
    session_id: *const c_char,
    text: *const c_char,
) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    if session_id.is_null() || text.is_null() {
        return MAHO_ACP_ERR_INVALID_ARG;
    }
    // SAFETY: all pointers valid per caller contract.
    let h = &*handle;
    let sid = match CStr::from_ptr(session_id).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return MAHO_ACP_ERR_INVALID_ARG,
    };
    let prompt_text = match CStr::from_ptr(text).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return MAHO_ACP_ERR_INVALID_ARG,
    };
    let inner = h.inner.clone();
    let rt = inner.runtime.handle().clone();

    rt.spawn(async move {
        let guard = inner.client.lock().await;
        let client = match guard.as_ref() {
            Some(c) => c,
            None => {
                let msg = "client not started".to_string();
                let xdg_root = maho_acp::process::maho_opencode_xdg_root()
                    .unwrap_or_else(|_| PathBuf::from(""));
                inner.fire_event(&FfiEvent::PromptErr {
                    session_id: sid,
                    code: MAHO_ACP_ERR_NOT_STARTED as i64,
                    message: msg.clone(),
                    auth_setup_command: detect_auth_command(&msg, &xdg_root),
                });
                return;
            }
        };

        let prompt_handle = match client.submit_prompt(&sid, prompt_text).await {
            Ok(h) => h,
            Err(e) => {
                let (code, message) = match &e {
                    maho_acp::Error::JsonRpc { code, message } => (*code, message.clone()),
                    other => (-1, other.to_string()),
                };
                let xdg_root = maho_acp::process::maho_opencode_xdg_root()
                    .unwrap_or_else(|_| PathBuf::from(""));
                inner.fire_event(&FfiEvent::PromptErr {
                    session_id: sid,
                    code,
                    message: message.clone(),
                    auth_setup_command: detect_auth_command(&message, &xdg_root),
                });
                return;
            }
        };

        // We need to drop the client lock before consuming the prompt handle,
        // because the prompt handle's completion task also needs the client
        // internals.
        drop(guard);

        let mut events = prompt_handle.events;
        let mut perm_rx = prompt_handle.permission_requests;
        let completion = prompt_handle.completion;

        let inner_events = inner.clone();
        let inner_perms = inner.clone();
        let sid_events = sid.clone();

        // Spawn a task to forward streaming updates
        let event_forwarder = tokio::spawn(async move {
            while let Some(update) = events.recv().await {
                let ffi_update = acp_update_to_ffi(&update);
                inner_events.fire_event(&FfiEvent::Update(ffi_update));
            }
        });

        // Spawn a task to forward permission requests
        let perm_forwarder = tokio::spawn(async move {
            while let Some(req) = perm_rx.recv().await {
                inner_perms.fire_event(&FfiEvent::PermissionRequest {
                    json_rpc_id: req.json_rpc_id,
                    session_id: req.session_id.clone(),
                    tool_call: serde_json::to_value(&req.tool_call).unwrap_or_default(),
                    options: serde_json::to_value(&req.options).unwrap_or_default(),
                });
            }
        });

        // Wait for prompt completion
        match completion.await {
            Ok(Ok(result)) => {
                let stop_reason = match &result.stop_reason {
                    maho_acp::StopReason::EndTurn => "end_turn",
                    maho_acp::StopReason::MaxTokens => "max_tokens",
                    maho_acp::StopReason::MaxTurnRequests => "max_turn_requests",
                    maho_acp::StopReason::Refusal => "refusal",
                    maho_acp::StopReason::Cancelled => "cancelled",
                    maho_acp::StopReason::Unknown(s) => s.as_str(),
                };
                let usage = result
                    .usage
                    .as_ref()
                    .and_then(|u| serde_json::to_value(u).ok())
                    .unwrap_or(serde_json::Value::Null);
                inner.fire_event(&FfiEvent::PromptComplete {
                    session_id: sid_events,
                    stop_reason: stop_reason.to_string(),
                    usage,
                });
            }
            Ok(Err(e)) => {
                let (code, message) = match &e {
                    maho_acp::Error::JsonRpc { code, message } => (*code, message.clone()),
                    other => (-1, other.to_string()),
                };
                let xdg_root = maho_acp::process::maho_opencode_xdg_root()
                    .unwrap_or_else(|_| PathBuf::from(""));
                inner.fire_event(&FfiEvent::PromptErr {
                    session_id: sid_events,
                    code,
                    message: message.clone(),
                    auth_setup_command: detect_auth_command(&message, &xdg_root),
                });
            }
            Err(_join_err) => {
                let msg = "prompt task panicked".to_string();
                let xdg_root = maho_acp::process::maho_opencode_xdg_root()
                    .unwrap_or_else(|_| PathBuf::from(""));
                inner.fire_event(&FfiEvent::PromptErr {
                    session_id: sid_events,
                    code: MAHO_ACP_ERR_INTERNAL as i64,
                    message: msg.clone(),
                    auth_setup_command: detect_auth_command(&msg, &xdg_root),
                });
            }
        }

        // Clean up forwarder tasks
        event_forwarder.abort();
        perm_forwarder.abort();
    });

    MAHO_ACP_OK
}

/// Cancel the current turn for a session.
///
/// # Safety
/// `handle` and `session_id` must be valid.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_cancel_turn(
    handle: *mut MahoAcpHandle,
    session_id: *const c_char,
) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    if session_id.is_null() {
        return MAHO_ACP_ERR_INVALID_ARG;
    }
    // SAFETY: pointers valid per caller contract.
    let h = &*handle;
    let sid = match CStr::from_ptr(session_id).to_str() {
        Ok(s) => s.to_string(),
        Err(_) => return MAHO_ACP_ERR_INVALID_ARG,
    };

    let inner = h.inner.clone();
    let rt = inner.runtime.handle().clone();
    rt.spawn(async move {
        let guard = inner.client.lock().await;
        if let Some(client) = guard.as_ref() {
            let _ = client
                .send_notification(
                    "session/cancel",
                    Some(serde_json::json!({ "sessionId": sid })),
                )
                .await;
        }
    });

    MAHO_ACP_OK
}

/// Reply to a permission request.
///
/// `outcome_kind`: 0 = Cancelled, 1 = Selected.
/// When Selected, `option_id` must be non-null.
///
/// # Safety
/// `handle` must be valid. `option_id` must be valid when `outcome_kind == 1`.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_reply_permission(
    handle: *mut MahoAcpHandle,
    json_rpc_id: i64,
    outcome_kind: u32,
    option_id: *const c_char,
) -> i32 {
    if handle.is_null() {
        return MAHO_ACP_ERR_NULL_HANDLE;
    }
    // SAFETY: handle valid per caller contract.
    let h = &*handle;

    let outcome = match outcome_kind {
        0 => PermissionOutcome::Cancelled,
        1 => {
            if option_id.is_null() {
                return MAHO_ACP_ERR_INVALID_ARG;
            }
            // SAFETY: option_id is valid per caller contract.
            let oid = match CStr::from_ptr(option_id).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return MAHO_ACP_ERR_INVALID_ARG,
            };
            PermissionOutcome::Selected { option_id: oid }
        }
        _ => return MAHO_ACP_ERR_INVALID_ARG,
    };

    let inner = h.inner.clone();
    let rt = inner.runtime.handle().clone();
    rt.spawn(async move {
        let guard = inner.client.lock().await;
        if let Some(client) = guard.as_ref() {
            let _ = client.reply_permission(json_rpc_id, outcome).await;
        }
    });

    MAHO_ACP_OK
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_event_roundtrip() {
        let event = FfiEvent::InitializeOk {
            protocol_version: 1,
            agent_capabilities: serde_json::json!({"foo": true}),
            auth_methods: serde_json::json!([]),
        };
        let blob = encode_ffi_event(&event);
        let decoded = decode_ffi_event(&blob).unwrap();
        match decoded {
            FfiEvent::InitializeOk {
                protocol_version, ..
            } => assert_eq!(protocol_version, 1),
            other => panic!("expected InitializeOk, got {:?}", other),
        }
    }

    #[test]
    fn ffi_event_update_roundtrip() {
        let update = FfiAcpUpdate::AgentMessageChunk {
            message_id: "m1".to_string(),
            text: "hello".to_string(),
        };
        let event = FfiEvent::Update(update);
        let blob = encode_ffi_event(&event);
        let decoded = decode_ffi_event(&blob).unwrap();
        match decoded {
            FfiEvent::Update(FfiAcpUpdate::AgentMessageChunk { text, .. }) => {
                assert_eq!(text, "hello");
            }
            other => panic!("expected Update(AgentMessageChunk), got {:?}", other),
        }
    }

    #[test]
    fn decode_truncated_blob_fails() {
        assert!(decode_ffi_event(&[0, 0]).is_err());
        assert!(decode_ffi_event(&[10, 0, 0, 0, 1, 2]).is_err());
    }

    #[test]
    fn create_and_destroy_handle() {
        let handle = maho_acp_create();
        assert!(!handle.is_null());
        // SAFETY: handle was just created.
        unsafe { maho_acp_destroy(handle) };
    }

    #[test]
    fn destroy_null_is_noop() {
        // SAFETY: null is explicitly allowed.
        unsafe { maho_acp_destroy(ptr::null_mut()) };
    }

    #[test]
    fn operations_on_null_handle_return_error() {
        // SAFETY: testing null handle behavior.
        unsafe {
            assert_eq!(maho_acp_start(ptr::null_mut(), ptr::null()), MAHO_ACP_ERR_NULL_HANDLE);
            assert_eq!(maho_acp_shutdown(ptr::null_mut()), MAHO_ACP_ERR_NULL_HANDLE);
            assert_eq!(maho_acp_initialize(ptr::null_mut()), MAHO_ACP_ERR_NULL_HANDLE);
            assert_eq!(maho_acp_create_session(ptr::null_mut()), MAHO_ACP_ERR_NULL_HANDLE);
        }
    }

    #[test]
    fn all_ffi_event_variants_serialize() {
        let events = vec![
            FfiEvent::InitializeOk {
                protocol_version: 1,
                agent_capabilities: serde_json::Value::Null,
                auth_methods: serde_json::Value::Null,
            },
            FfiEvent::InitializeErr {
                code: -1,
                message: "fail".to_string(),
            },
            FfiEvent::SessionCreated {
                session_id: "s1".to_string(),
                models: serde_json::Value::Null,
                modes: serde_json::Value::Null,
                config_options: serde_json::Value::Null,
            },
            FfiEvent::SessionCreateErr {
                code: -1,
                message: "fail".to_string(),
            },
            FfiEvent::SessionClosed {
                session_id: "s1".to_string(),
            },
            FfiEvent::SessionCloseErr {
                code: -1,
                message: "fail".to_string(),
            },
            FfiEvent::Update(FfiAcpUpdate::AgentMessageChunk {
                message_id: "m1".to_string(),
                text: "hi".to_string(),
            }),
            FfiEvent::PermissionRequest {
                json_rpc_id: 42,
                session_id: "s1".to_string(),
                tool_call: serde_json::Value::Null,
                options: serde_json::Value::Null,
            },
            FfiEvent::PromptComplete {
                session_id: "s1".to_string(),
                stop_reason: "end_turn".to_string(),
                usage: serde_json::Value::Null,
            },
            FfiEvent::PromptErr {
                session_id: "s1".to_string(),
                code: -1,
                message: "fail".to_string(),
                auth_setup_command: None,
            },
            FfiEvent::TransportError {
                message: "broken".to_string(),
            },
            FfiEvent::ChildExited {
                exit_code: Some(0),
            },
        ];
        for event in &events {
            let blob = encode_ffi_event(event);
            let decoded = decode_ffi_event(&blob).unwrap();
            // Just verify it round-trips without panic
            let _ = format!("{:?}", decoded);
        }
    }

    #[test]
    fn detect_auth_command_auth_keywords() {
        let root = PathBuf::from("/tmp/test-xdg");
        assert!(detect_auth_command("Unauthorized", &root).is_some());
        assert!(detect_auth_command("Invalid API key provided", &root).is_some());
        assert!(detect_auth_command("Auth required", &root).is_some());
        assert!(detect_auth_command("Not authenticated", &root).is_some());
        assert!(detect_auth_command("HTTP 401 error", &root).is_some());
        assert!(detect_auth_command("Missing credentials", &root).is_some());
    }

    #[test]
    fn detect_auth_command_non_auth_returns_none() {
        let root = PathBuf::from("/tmp/test-xdg");
        assert!(detect_auth_command("Network timeout", &root).is_none());
        assert!(detect_auth_command("Model not found", &root).is_none());
    }

    #[test]
    fn detect_auth_command_contains_xdg_vars() {
        let root = PathBuf::from("/home/user/Library/Application Support/Maho/opencode");
        let cmd = detect_auth_command("Unauthorized", &root).unwrap();
        assert!(cmd.contains("XDG_CONFIG_HOME="));
        assert!(cmd.contains("XDG_DATA_HOME="));
        assert!(cmd.contains("XDG_STATE_HOME="));
        assert!(cmd.contains("XDG_CACHE_HOME="));
        assert!(cmd.contains("opencode auth login"));
    }

    #[test]
    fn set_auth_config_valid_json() {
        let handle = maho_acp_create();
        assert!(!handle.is_null());
        let json = std::ffi::CString::new(
            r#"{"provider":"anthropic","base_url":"","api_key":"sk-test","model":"claude-sonnet-4-20250514"}"#,
        ).unwrap();
        unsafe {
            let rc = maho_acp_set_auth_config(handle, json.as_ptr());
            assert_eq!(rc, MAHO_ACP_OK);
            maho_acp_destroy(handle);
        }
    }

    #[test]
    fn set_auth_config_null_clears() {
        let handle = maho_acp_create();
        let json = std::ffi::CString::new(
            r#"{"provider":"openai","base_url":"","api_key":"k","model":""}"#,
        ).unwrap();
        unsafe {
            maho_acp_set_auth_config(handle, json.as_ptr());
            let rc = maho_acp_set_auth_config(handle, std::ptr::null());
            assert_eq!(rc, MAHO_ACP_OK);
            maho_acp_destroy(handle);
        }
    }

    #[test]
    fn set_auth_config_invalid_json() {
        let handle = maho_acp_create();
        let json = std::ffi::CString::new("not json").unwrap();
        unsafe {
            let rc = maho_acp_set_auth_config(handle, json.as_ptr());
            assert_eq!(rc, MAHO_ACP_ERR_INVALID_ARG);
            maho_acp_destroy(handle);
        }
    }
}
