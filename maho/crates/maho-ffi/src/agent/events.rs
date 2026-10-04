use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;
use std::sync::Arc;

use maho_agent::AgentRuntime;
use maho_core::maho_core::MahoCore;

use crate::agent::callback_lease::{
    dispatch_unified_event, CallbackReleasePolicy, MahoAgentReleaseCallback, SendableCallback,
    SendableUserData, SessionCallbackLease, UnifiedEventCallbackRegistration,
};
use crate::agent::session::{
    agent_error_callback_text, apply_agent_skill_system_prompt, maho_agent_send_message_impl,
    ActiveConversationGuard, MahoAgentSession,
};
use crate::common::{cstring_or_fallback, to_c_string};
use crate::ffi_safe;
use crate::import_gate;

/// ABI v2 Agent Event Kind
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum MahoAgentEventKindV2 {
    Token = 1,
    Thinking = 2,
    ToolCall = 3,
    ToolResult = 4,
    ArtifactCreated = 5,
    Completed = 6,
    Error = 7,
    InteractionRequest = 8,
    Status = 9,
}

/// ABI v2 Agent Event Structure for unified event streaming across FFI
#[repr(C)]
#[derive(Debug, Clone)]
pub struct MahoAgentEventV2 {
    pub abi_version: u32,
    pub sequence: u64,
    pub session_id: *const c_char,
    pub turn_id: *const c_char,
    pub kind: MahoAgentEventKindV2,
    pub payload: *const c_char,
}

pub type MahoAgentEventCallbackV2 =
    Option<unsafe extern "C" fn(user_data: *mut c_void, event: *const MahoAgentEventV2)>;

/// Unified Agent Event Envelope (Wave 0 consolidated event envelope)
///
/// Carries monotonic event sequence and run identity across FFI boundaries
/// for continuous streaming, reconnect replay, and crash recovery.
#[repr(C)]
#[derive(Debug, Clone)]
pub struct MahoUnifiedAgentEventEnvelope {
    pub abi_version: u32,
    pub run_id: *const c_char,
    pub event_seq: u64,
    pub kind: MahoAgentEventKindV2,
    pub payload_json: *const c_char,
}

pub type MahoAgentUnifiedEventEnvelope = MahoUnifiedAgentEventEnvelope;

pub type MahoUnifiedAgentEventCallback = Option<
    unsafe extern "C" fn(user_data: *mut c_void, event: *const MahoUnifiedAgentEventEnvelope),
>;

pub type MahoAgentUnifiedEventCallback = MahoUnifiedAgentEventCallback;

#[repr(C)]
#[derive(Debug, Clone)]
pub struct MahoChatConfig {
    pub api_key: *const c_char,
    pub endpoint: *const c_char,
    pub model: *const c_char,
    pub system_instruction: *const c_char,
}

/// # Safety
/// All pointers in `config` must be null-terminated C strings.
pub unsafe extern "C" fn maho_core_chat_session_new(
    config: *const MahoChatConfig,
) -> *mut maho_core::chat_session::ChatSession {
    ffi_safe!(
        {
            if config.is_null() {
                return std::ptr::null_mut();
            }
            let config = &*config;
            let api_key = if config.api_key.is_null() {
                String::new()
            } else {
                CStr::from_ptr(config.api_key)
                    .to_string_lossy()
                    .into_owned()
            };
            let endpoint = if config.endpoint.is_null() {
                String::new()
            } else {
                CStr::from_ptr(config.endpoint)
                    .to_string_lossy()
                    .into_owned()
            };
            let model = if config.model.is_null() {
                String::new()
            } else {
                CStr::from_ptr(config.model).to_string_lossy().into_owned()
            };
            let system_instruction = if config.system_instruction.is_null() {
                String::new()
            } else {
                CStr::from_ptr(config.system_instruction)
                    .to_string_lossy()
                    .into_owned()
            };

            let chat_config = maho_core::chat_session::ChatConfig {
                api_key,
                endpoint,
                model,
                system_instruction,
            };

            Box::into_raw(Box::new(maho_core::chat_session::ChatSession::new(
                chat_config,
            )))
        },
        ptr::null_mut()
    )
}

/// # Safety
/// `ptr` must be a valid pointer returned by `maho_core_chat_session_new`, or null.
pub unsafe extern "C" fn maho_core_chat_session_free(
    ptr: *mut maho_core::chat_session::ChatSession,
) {
    ffi_safe!(
        {
            if !ptr.is_null() {
                drop(Box::from_raw(ptr));
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `name`, `description`, and `schema_json` must be null-terminated C strings.
pub unsafe extern "C" fn maho_core_chat_register_tool(
    ptr: *mut maho_core::chat_session::ChatSession,
    name: *const c_char,
    description: *const c_char,
    schema_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || name.is_null() || description.is_null() || schema_json.is_null() {
                return false;
            }
            let session = &mut *ptr;
            let name_str = match CStr::from_ptr(name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let desc_str = match CStr::from_ptr(description).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let schema_str = match CStr::from_ptr(schema_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            session.register_tool(name_str, desc_str, schema_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
pub unsafe extern "C" fn maho_core_chat_set_event_sink(
    ptr: *mut maho_core::chat_session::ChatSession,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            let session = &mut *ptr;
            let sink = maho_core::chat_session::MahoChatEventSink {
                on_token,
                on_thinking,
                on_complete,
                on_error,
                user_data,
            };
            session.set_event_sink(sink);
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `message` must be a valid null-terminated C string.
pub unsafe extern "C" fn maho_core_chat_send_user_turn(
    ptr: *mut maho_core::chat_session::ChatSession,
    message: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || message.is_null() {
                return false;
            }
            let session = &mut *ptr;
            let msg_str = match CStr::from_ptr(message).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            session.send_user_turn(msg_str)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `tool_call_id`, `tool_name`, and `result` must be null-terminated C strings.
pub unsafe extern "C" fn maho_core_chat_send_tool_result(
    ptr: *mut maho_core::chat_session::ChatSession,
    tool_call_id: *const c_char,
    tool_name: *const c_char,
    result: *const c_char,
    trigger: bool,
) -> bool {
    ffi_safe!(
        {
            if ptr.is_null() || tool_call_id.is_null() || tool_name.is_null() || result.is_null() {
                return false;
            }
            let session = &mut *ptr;
            let tc_id = match CStr::from_ptr(tool_call_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let t_name = match CStr::from_ptr(tool_name).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let res = match CStr::from_ptr(result).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            session.send_tool_result(tc_id, t_name, res, trigger)
        },
        false
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
pub unsafe extern "C" fn maho_core_chat_cancel(ptr: *mut maho_core::chat_session::ChatSession) {
    ffi_safe!(
        {
            if !ptr.is_null() {
                let session = &mut *ptr;
                session.cancel();
            }
        },
        ()
    )
}

/// # Safety
/// `ptr` must be a valid pointer to a `ChatSession`.
/// `content` must be a null-terminated C string.
pub unsafe extern "C" fn maho_core_chat_append_user_message(
    ptr: *mut maho_core::chat_session::ChatSession,
    content: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            let session = &mut *ptr;
            let content_str = if content.is_null() {
                ""
            } else {
                match CStr::from_ptr(content).to_str() {
                    Ok(s) => s,
                    Err(_) => return,
                }
            };
            session.append_user_message(content_str);
        },
        ()
    )
}

/// `ptr` must be a valid pointer to a `ChatSession`.
/// `content` and `tool_calls_json` must be null-terminated C strings.
pub unsafe extern "C" fn maho_core_chat_append_assistant_message(
    ptr: *mut maho_core::chat_session::ChatSession,
    content: *const c_char,
    tool_calls_json: *const c_char,
) {
    ffi_safe!(
        {
            if ptr.is_null() {
                return;
            }
            let session = &mut *ptr;
            let content_str = if content.is_null() {
                ""
            } else {
                match CStr::from_ptr(content).to_str() {
                    Ok(s) => s,
                    Err(_) => return,
                }
            };
            let tcs_str = if tool_calls_json.is_null() {
                ""
            } else {
                match CStr::from_ptr(tool_calls_json).to_str() {
                    Ok(s) => s,
                    Err(_) => return,
                }
            };
            session.append_assistant_message(content_str, tcs_str);
        },
        ()
    )
}

// ============================================================
// Conversation CRUD FFI functions
// ============================================================

/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `mime` must be a valid
/// NUL-terminated C string; `data` must point to at least `data_len` bytes if non-null.
pub unsafe extern "C" fn maho_chat_send_image(
    core: *mut MahoCore,
    mime: *const c_char,
    data: *const u8,
    data_len: usize,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() {
                return false;
            }
            let mime_str = if mime.is_null() {
                "application/octet-stream"
            } else {
                match CStr::from_ptr(mime).to_str() {
                    Ok(s) => s,
                    Err(_) => return false,
                }
            };
            let bytes: Vec<u8> = if data.is_null() || data_len == 0 {
                Vec::new()
            } else {
                std::slice::from_raw_parts(data, data_len).to_vec()
            };
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *core;
            use maho_types::chat::{ChatRequestContext, ChatRequestMode};
            use maho_types::events::shell_event::ShellEvent;
            let message = format!("[image:{} {} bytes]", mime_str, bytes.len());
            core.handle_event(ShellEvent::ChatMessage {
                message,
                context: ChatRequestContext::new(ChatRequestMode::GeneralChat),
            });
            true
        },
        false
    )
}

/// Send a text+image chat message. `text` is the user's text (may be null/empty).
/// Returns true on success.
///
/// # Safety
/// `core` must be a valid `*mut MahoCore` produced by `maho_core_new*`; `text` and `mime` may
/// be null; `data` must point to at least `data_len` bytes if non-null.
pub unsafe extern "C" fn maho_chat_send_text_with_image(
    core: *mut MahoCore,
    text: *const c_char,
    mime: *const c_char,
    data: *const u8,
    data_len: usize,
) -> bool {
    ffi_safe!(
        {
            if core.is_null() {
                return false;
            }
            let text_str = if text.is_null() {
                String::new()
            } else {
                CStr::from_ptr(text).to_str().unwrap_or("").to_string()
            };
            let mime_str = if mime.is_null() {
                "application/octet-stream"
            } else {
                CStr::from_ptr(mime)
                    .to_str()
                    .unwrap_or("application/octet-stream")
            };
            let byte_count = if data.is_null() { 0 } else { data_len };
            // SAFETY(H15): Reject call while import is active to prevent data race on MahoCore.
            if import_gate::is_active() {
                return false;
            }
            let core = &mut *core;
            use maho_types::chat::{ChatRequestContext, ChatRequestMode};
            use maho_types::events::shell_event::ShellEvent;
            let message = if text_str.is_empty() {
                format!("[image:{} {} bytes]", mime_str, byte_count)
            } else {
                format!("{} [image:{} {} bytes]", text_str, mime_str, byte_count)
            };
            core.handle_event(ShellEvent::ChatMessage {
                message,
                context: ChatRequestContext::new(ChatRequestMode::GeneralChat),
            });
            true
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `message` must be a null-terminated C string.
pub unsafe extern "C" fn maho_agent_send_message(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
    on_tool_call: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            args: *const c_char,
        ),
    >,
    on_tool_result: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            result: *const c_char,
            success: bool,
        ),
    >,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) -> bool {
    // SAFETY: this legacy ABI forwards its validated caller contract without a lease policy.
    unsafe {
        maho_agent_send_message_impl(
            session,
            message,
            on_token,
            on_thinking,
            on_tool_call,
            on_tool_result,
            on_complete,
            on_error,
            user_data,
            None,
        )
    }
}

/// # Safety
/// Same pointer requirements as `maho_agent_send_message`. When this function returns `true`,
/// `on_release` receives `release_user_data` exactly once after the terminal callback and every
/// turn callback closure are dropped. The callback must not unwind.
pub unsafe extern "C" fn maho_agent_send_message_leased(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
    on_tool_call: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            args: *const c_char,
        ),
    >,
    on_tool_result: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            id: *const c_char,
            name: *const c_char,
            result: *const c_char,
            success: bool,
        ),
    >,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    callback_user_data: *mut std::ffi::c_void,
    on_release: MahoAgentReleaseCallback,
    release_user_data: *mut c_void,
) -> bool {
    // SAFETY: the leased ABI transfers the dedicated release context only after the private
    // implementation accepts the turn and creates its turn callback lease.
    unsafe {
        maho_agent_send_message_impl(
            session,
            message,
            on_token,
            on_thinking,
            on_tool_call,
            on_tool_result,
            on_complete,
            on_error,
            callback_user_data,
            Some(CallbackReleasePolicy {
                callback: on_release,
                user_data: release_user_data,
            }),
        )
    }
}

/// # Safety
/// Fast-path variant of `maho_agent_send_message`. Skips all tool registration
/// and dispatches straight to the LLM. Callers MUST NOT rely on tool_calls
/// being processed. Same pointer safety requirements as `maho_agent_send_message`.
pub unsafe extern "C" fn maho_agent_send_message_simple(
    session: *mut MahoAgentSession,
    message: *const c_char,
    on_token: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, token: *const c_char)>,
    on_thinking: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, thinking: *const c_char),
    >,
    on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut std::ffi::c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    on_error: Option<unsafe extern "C" fn(user_data: *mut std::ffi::c_void, error: *const c_char)>,
    user_data: *mut std::ffi::c_void,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || message.is_null() {
                return false;
            }
            let session = &*session;
            let msg_str = match CStr::from_ptr(message).to_str() {
                Ok(s) => s.to_string(),
                Err(_) => return false,
            };
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();
            apply_agent_skill_system_prompt(session, runtime.as_ref());

            let on_token_cb = on_token.map(|cb| {
                let cb_sendable = SendableCallback(cb);
                let user_data_val = SendableUserData(user_data as usize);
                let callback: Box<dyn Fn(&str) + Send + Sync> = Box::new(move |token| {
                    let cb = cb_sendable.0;
                    let udata = user_data_val.0 as *mut std::ffi::c_void;
                    let token_c = match CString::new(token) {
                        Ok(s) => s,
                        Err(_) => return,
                    };
                    unsafe {
                        cb(udata, token_c.as_ptr());
                    }
                });
                callback
            });

            let on_thinking_sendable = SendableCallback(on_thinking);
            let on_event_cb = if on_thinking.is_some() {
                let user_data_val = SendableUserData(user_data as usize);
                let callback = move |event: maho_agent::AgentStreamEvent| {
                    let udata = user_data_val.0 as *mut std::ffi::c_void;
                    match event {
                        maho_agent::AgentStreamEvent::Thinking(thinking) => {
                            if let Some(cb) = on_thinking_sendable.0 {
                                if let Ok(thinking_c) = CString::new(thinking) {
                                    unsafe {
                                        cb(udata, thinking_c.as_ptr());
                                    }
                                }
                            }
                        }
                        _ => {}
                    }
                };
                Some(Arc::new(callback)
                    as Arc<
                        dyn Fn(maho_agent::AgentStreamEvent) + Send + Sync + 'static,
                    >)
            } else {
                None
            };

            let on_complete = SendableCallback(on_complete);
            let on_error = SendableCallback(on_error);
            let user_data_val = SendableUserData(user_data as usize);

            let active_guard = ActiveConversationGuard::new(
                Arc::clone(&session.active_conversation_registry),
                session_id.clone(),
            );

            let fut = async move {
                let _active_guard = active_guard;
                let user_chat_msg = maho_types::chat::ChatMessage::user(
                    maho_types::chat::ChatContent::text(msg_str),
                );
                match runtime
                    .run_turn_simple(&session_id, user_chat_msg, on_token_cb, on_event_cb)
                    .await
                {
                    Ok(response) => {
                        let text = match &response.content {
                            maho_types::chat::ChatContent::Text(t) => t.clone(),
                            _ => String::new(),
                        };
                        let Some(text_c) = cstring_or_fallback(&text, "") else {
                            return;
                        };
                        let Some(empty_json) = cstring_or_fallback("[]", "[]") else {
                            return;
                        };
                        if let Some(cb) = on_complete.0 {
                            cb(
                                user_data_val.0 as *mut std::ffi::c_void,
                                text_c.as_ptr(),
                                empty_json.as_ptr(),
                            );
                        }
                    }
                    Err(e) => {
                        let error_text = agent_error_callback_text(&e);
                        let Some(err_c) = cstring_or_fallback(&error_text, "unknown error") else {
                            return;
                        };
                        if let Some(cb) = on_error.0 {
                            cb(user_data_val.0 as *mut std::ffi::c_void, err_c.as_ptr());
                        }
                    }
                }
            };

            let isolation = Arc::clone(&session.isolation);
            let _handle = isolation.block_on(async move {
                tokio::spawn(fut);
            });

            true
        },
        false
    )
}

/// function. `cb` (nullable) receives a JSON-serialized `ArtifactInfo` string
/// for each artifact created during a turn; `user_data` is passed back verbatim.
/// ABI-stable: callers that never install this still stream token/tool/complete
/// events unchanged (the artifact arm is only added when a callback is set).
pub unsafe extern "C" fn maho_agent_set_artifact_created_callback(
    session: *mut MahoAgentSession,
    cb: Option<
        unsafe extern "C" fn(user_data: *mut std::ffi::c_void, artifact_json: *const c_char),
    >,
    user_data: *mut std::ffi::c_void,
) {
    ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            let session = &mut *session;
            session.artifact_created_cb = cb;
            session.artifact_created_user_data = user_data as usize;
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_register_unified_event_callback(
    session: *mut MahoAgentSession,
    callback: MahoUnifiedAgentEventCallback,
    user_data: *mut c_void,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let session = &*session;
            let mut reg = session
                .unified_event_registration
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            *reg = Some(UnifiedEventCallbackRegistration {
                callback,
                user_data: user_data as usize,
                lease: None,
            });
            true
        },
        false
    )
}

/// # Safety
/// Same pointer requirements as `maho_agent_register_unified_event_callback`.
/// `on_release` is invoked with `release_user_data` exactly once after the callback
/// registration is unregistered or when the session is destroyed. If `on_release` is
/// `None`, no release callback is invoked on release/drop.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_register_unified_event_callback_leased(
    session: *mut MahoAgentSession,
    callback: MahoUnifiedAgentEventCallback,
    user_data: *mut c_void,
    on_release: Option<unsafe extern "C" fn(user_data: *mut c_void)>,
    release_user_data: *mut c_void,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let session = &*session;
            let lease = SessionCallbackLease::new(on_release, release_user_data);
            lease.arm();
            let mut reg = session
                .unified_event_registration
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            *reg = Some(UnifiedEventCallbackRegistration {
                callback,
                user_data: user_data as usize,
                lease: Some(lease),
            });
            true
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_unregister_unified_event_callback(
    session: *mut MahoAgentSession,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let session = &*session;
            let mut reg = session
                .unified_event_registration
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            *reg = None;
            true
        },
        false
    )
}

pub(crate) fn parse_interaction_answer(
    answer_str: &str,
) -> maho_agent::interaction::InteractionAnswer {
    use maho_agent::interaction::InteractionAnswer;
    if let Ok(typed) = serde_json::from_str::<InteractionAnswer>(answer_str) {
        return typed;
    }
    if let Ok(val) = serde_json::from_str::<serde_json::Value>(answer_str) {
        if let Some(b) = val.as_bool() {
            return if b {
                InteractionAnswer::Confirmed
            } else {
                InteractionAnswer::Denied
            };
        }
        if let Some(s) = val.as_str() {
            return match s.to_ascii_lowercase().as_str() {
                "confirmed" | "confirm" | "yes" | "true" | "allow" => InteractionAnswer::Confirmed,
                "denied" | "deny" | "no" | "false" | "cancel" => InteractionAnswer::Denied,
                _ => InteractionAnswer::Text(s.to_string()),
            };
        }
        if let Some(obj) = val.as_object() {
            if let Some(b) = obj.get("confirmed").and_then(|v| v.as_bool()) {
                return if b {
                    InteractionAnswer::Confirmed
                } else {
                    InteractionAnswer::Denied
                };
            }
            if let Some(opt) = obj.get("selected_option").and_then(|v| v.as_str()) {
                return InteractionAnswer::SelectedOption(opt.to_string());
            }
            if let Some(opt) = obj.get("option").and_then(|v| v.as_str()) {
                return InteractionAnswer::SelectedOption(opt.to_string());
            }
            if let Some(t) = obj.get("text").and_then(|v| v.as_str()) {
                return InteractionAnswer::Text(t.to_string());
            }
            if let Some(t) = obj.get("answer").and_then(|v| v.as_str()) {
                return InteractionAnswer::Text(t.to_string());
            }
            if let Some(t) = obj.get("value").and_then(|v| v.as_str()) {
                return InteractionAnswer::Text(t.to_string());
            }
            if let Some(t) = obj.get("0").and_then(|v| v.as_str()) {
                return InteractionAnswer::Text(t.to_string());
            }
            if let Some(action) = obj.get("action").and_then(|v| v.as_str()) {
                return match action.to_ascii_lowercase().as_str() {
                    "confirm" | "confirmed" | "allow" => InteractionAnswer::Confirmed,
                    "deny" | "denied" | "cancel" => InteractionAnswer::Denied,
                    _ => InteractionAnswer::Text(action.to_string()),
                };
            }
            if let Some(kind) = obj.get("answer_kind").and_then(|v| v.as_str()) {
                return match kind.to_ascii_lowercase().as_str() {
                    "confirmed" | "confirm" => InteractionAnswer::Confirmed,
                    "denied" | "deny" => InteractionAnswer::Denied,
                    _ => {
                        if let Some(t) = obj.get("0").and_then(|v| v.as_str()) {
                            InteractionAnswer::Text(t.to_string())
                        } else if let Some(t) = obj.get("text").and_then(|v| v.as_str()) {
                            InteractionAnswer::Text(t.to_string())
                        } else if let Some(t) = obj.get("value").and_then(|v| v.as_str()) {
                            InteractionAnswer::Text(t.to_string())
                        } else {
                            InteractionAnswer::Text(kind.to_string())
                        }
                    }
                };
            }
        }
    }
    InteractionAnswer::Text(answer_str.to_string())
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `request_id` and `answer_json` must be valid null-terminated UTF-8 C strings.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_interaction_resolve(
    session: *mut MahoAgentSession,
    request_id: *const c_char,
    answer_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || request_id.is_null() || answer_json.is_null() {
                return false;
            }
            let session = &*session;
            let req_id_str = match CStr::from_ptr(request_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let answer_str = match CStr::from_ptr(answer_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };

            let answer = parse_interaction_answer(answer_str);
            let req_id = maho_agent::interaction::InteractionRequestId::new(req_id_str);
            let _ = &req_id;
            session
                .runtime
                .resolve_interaction(req_id_str, answer)
                .is_ok()
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `request_id` must be a valid null-terminated UTF-8 C string.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_interaction_timeout(
    session: *mut MahoAgentSession,
    request_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || request_id.is_null() {
                return false;
            }
            let session = &*session;
            let req_id_str = match CStr::from_ptr(request_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            session.runtime.timeout_interaction(req_id_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession` developments.
/// `request_id` must be a valid null-terminated UTF-8 C string.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_interaction_cancel(
    session: *mut MahoAgentSession,
    request_id: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || request_id.is_null() {
                return false;
            }
            let session = &*session;
            let req_id_str = match CStr::from_ptr(request_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            session.runtime.cancel_interaction(req_id_str).is_ok()
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `run_id` must be a valid null-terminated UTF-8 C string.
/// `out_json` must be a non-null pointer to a `*mut c_char` location.
/// Caller owns the allocated string and must free it via `maho_core_free_string` or `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_get_events_after(
    session: *mut MahoAgentSession,
    run_id: *const c_char,
    after_seq: u64,
    out_json: *mut *mut c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || run_id.is_null() || out_json.is_null() {
                return false;
            }
            let session = &*session;
            let run_id_str = match CStr::from_ptr(run_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };

            let journal_lock = session.runtime.run_journal();
            let journal = journal_lock.lock().unwrap_or_else(|e| e.into_inner());
            let run_id_obj = maho_agent::run_journal::AgentRunId::new(run_id_str);

            let (events, gap, is_terminal, can_auto_resume) = match journal.get_entries(&run_id_obj)
            {
                Ok(entries) => {
                    let is_terminal = journal.is_terminal(&run_id_obj).unwrap_or(false);
                    let resume_plan = journal.resume_plan(&run_id_obj).ok();
                    let can_auto_resume = resume_plan
                        .as_ref()
                        .map(|p| p.can_auto_resume)
                        .unwrap_or(false);

                    let oldest_seq = entries.first().map(|e| e.event_seq).unwrap_or(1);
                    // Bounded by journal retention: if after_seq is requested earlier than the
                    // retention window start (oldest available seq), return an explicit gap marker.
                    let gap = !entries.is_empty() && after_seq + 1 < oldest_seq;

                    let filtered: Vec<&maho_agent::run_journal::RunJournalEntry> =
                        entries.iter().filter(|e| e.event_seq > after_seq).collect();

                    (filtered, gap, is_terminal, can_auto_resume)
                }
                Err(_) => (Vec::new(), true, false, false),
            };

            let response_val = serde_json::json!({
                "run_id": run_id_str,
                "after_seq": after_seq,
                "gap": gap,
                "is_terminal": is_terminal,
                "can_auto_resume": can_auto_resume,
                "events": events.iter().map(|e| serde_json::json!({
                    "run_id": e.run_id.as_str(),
                    "event_seq": e.event_seq,
                    "event_name": e.event_name,
                    "payload_json": e.payload_json,
                    "timestamp": e.timestamp,
                })).collect::<Vec<_>>(),
            });

            let response_str = response_val.to_string();
            let c_str = to_c_string(&response_str);
            if c_str.is_null() {
                return false;
            }
            *out_json = c_str;
            true
        },
        false
    )
}

pub(crate) fn parse_task_category(category_str: &str) -> maho_agent::model_routing::TaskCategory {
    use maho_agent::model_routing::TaskCategory;
    let s = category_str.trim().to_ascii_lowercase();
    match s.as_str() {
        "chat" => TaskCategory::Chat,
        "general_chat" | "generalchat" | "general" => TaskCategory::GeneralChat,
        "code" => TaskCategory::Code,
        "coding" => TaskCategory::Coding,
        "research" => TaskCategory::Research,
        "deep_research" | "deepresearch" => TaskCategory::DeepResearch,
        "fast_reasoning" | "fastreasoning" | "reasoning" | "fast" => TaskCategory::FastReasoning,
        "vision" | "image" | "multimodal" => TaskCategory::Vision,
        "tab_automation" | "tabautomation" | "tabs" => TaskCategory::TabAutomation,
        "tool_use" | "tooluse" | "tools" => TaskCategory::ToolUse,
        _ => TaskCategory::GeneralChat,
    }
}

pub(crate) fn parse_event_kind(s: &str) -> maho_agent::event_wait::EventKind {
    use maho_agent::event_wait::EventKind;
    match s.trim().to_ascii_lowercase().as_str() {
        "notification" => EventKind::Notification,
        "inbox_message" | "inbox" | "message" => EventKind::InboxMessage,
        "webhook" => EventKind::Webhook,
        "routine_trigger" | "routine" | "trigger" => EventKind::RoutineTrigger,
        "system_signal" | "signal" | "system" => EventKind::SystemSignal,
        other => EventKind::Custom(other.to_string()),
    }
}

pub(crate) fn parse_event_filter(filter_str: &str) -> maho_agent::event_wait::EventFilter {
    use maho_agent::event_wait::{EventFilter, EventKind};
    if let Ok(filter) = serde_json::from_str::<EventFilter>(filter_str) {
        return filter;
    }
    if let Ok(val) = serde_json::from_str::<serde_json::Value>(filter_str) {
        if let Some(s) = val.as_str() {
            let kind = parse_event_kind(s);
            return EventFilter::new(kind);
        }
        if let Some(obj) = val.as_object() {
            let kind = obj
                .get("kind")
                .or_else(|| obj.get("event_type"))
                .or_else(|| obj.get("event_kind"))
                .and_then(|v| v.as_str())
                .map(parse_event_kind)
                .unwrap_or(EventKind::Notification);

            let sender = obj
                .get("sender")
                .and_then(|v| v.as_str())
                .map(ToString::to_string);
            let source = obj
                .get("source")
                .and_then(|v| v.as_str())
                .map(ToString::to_string);
            let topic = obj
                .get("topic")
                .and_then(|v| v.as_str())
                .map(ToString::to_string);

            return EventFilter {
                kind,
                sender,
                source,
                topic,
            };
        }
    }
    EventFilter::new(parse_event_kind(filter_str))
}

pub(crate) fn parse_wake_event(
    event_str: &str,
    run_id_opt: Option<&str>,
) -> maho_agent::event_wait::WakeEvent {
    use maho_agent::event_wait::{EventKind, WakeEvent};
    use maho_agent::run_journal::AgentRunId;
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64;

    if let Ok(mut ev) = serde_json::from_str::<WakeEvent>(event_str) {
        if ev.run_id.is_none() {
            ev.run_id = run_id_opt.map(AgentRunId::new);
        }
        if ev.timestamp == 0 {
            ev.timestamp = now;
        }
        return ev;
    }

    if let Ok(val) = serde_json::from_str::<serde_json::Value>(event_str) {
        if let Some(obj) = val.as_object() {
            let kind = obj
                .get("kind")
                .or_else(|| obj.get("event_type"))
                .and_then(|v| v.as_str())
                .map(parse_event_kind)
                .unwrap_or(EventKind::Notification);

            let sender = obj
                .get("sender")
                .and_then(|v| v.as_str())
                .map(ToString::to_string);
            let source = obj
                .get("source")
                .and_then(|v| v.as_str())
                .map(ToString::to_string);
            let topic = obj
                .get("topic")
                .and_then(|v| v.as_str())
                .map(ToString::to_string);
            let payload = obj
                .get("payload")
                .cloned()
                .unwrap_or(serde_json::Value::Null);
            let timestamp = obj.get("timestamp").and_then(|v| v.as_u64()).unwrap_or(now);
            let run_id = obj
                .get("run_id")
                .and_then(|v| v.as_str())
                .or(run_id_opt)
                .map(AgentRunId::new);

            return WakeEvent {
                run_id,
                kind,
                sender,
                source,
                topic,
                payload,
                timestamp,
            };
        }
    }

    WakeEvent {
        run_id: run_id_opt.map(AgentRunId::new),
        kind: parse_event_kind(event_str),
        sender: None,
        source: None,
        topic: None,
        payload: serde_json::Value::String(event_str.to_string()),
        timestamp: now,
    }
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `message_json` must be a valid null-terminated UTF-8 C string.
/// `run_ctx` and `intent` are optional null-terminated UTF-8 C strings (nullable).
#[no_mangle]
pub unsafe extern "C" fn maho_agent_turn_submit(
    session: *mut MahoAgentSession,
    run_ctx: *const c_char,
    message_json: *const c_char,
    intent: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || message_json.is_null() {
                return false;
            }
            let session = &*session;
            let raw_msg = match CStr::from_ptr(message_json).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };

            let msg_text = if let Ok(val) = serde_json::from_str::<serde_json::Value>(raw_msg) {
                if let Some(obj) = val.as_object() {
                    obj.get("message")
                        .or_else(|| obj.get("text"))
                        .or_else(|| obj.get("content"))
                        .or_else(|| obj.get("prompt"))
                        .and_then(|v| v.as_str())
                        .map(ToString::to_string)
                        .unwrap_or_else(|| raw_msg.to_string())
                } else if let Some(s) = val.as_str() {
                    s.to_string()
                } else {
                    raw_msg.to_string()
                }
            } else {
                raw_msg.to_string()
            };

            let run_id = if !run_ctx.is_null() {
                CStr::from_ptr(run_ctx)
                    .to_str()
                    .unwrap_or(&session.session_id)
            } else {
                &session.session_id
            };

            let behavior = if intent.is_null() {
                maho_agent::turn_control::FollowUpBehavior::Queue
            } else {
                let intent_addr = intent as usize;
                if intent_addr == 1 {
                    maho_agent::turn_control::FollowUpBehavior::Queue
                } else if intent_addr == 2 {
                    maho_agent::turn_control::FollowUpBehavior::Steer
                } else if intent_addr == 3 {
                    maho_agent::turn_control::FollowUpBehavior::Interrupt
                } else if intent_addr == 4 {
                    maho_agent::turn_control::FollowUpBehavior::Continue
                } else if let Ok(intent_str) = CStr::from_ptr(intent).to_str() {
                    match intent_str.trim().to_ascii_lowercase().as_str() {
                        "queue" | "queued" => maho_agent::turn_control::FollowUpBehavior::Queue,
                        "steer" | "steering" => maho_agent::turn_control::FollowUpBehavior::Steer,
                        "interrupt" | "cancel" | "abort" => {
                            maho_agent::turn_control::FollowUpBehavior::Interrupt
                        }
                        "continue" => maho_agent::turn_control::FollowUpBehavior::Continue,
                        _ => maho_agent::turn_control::FollowUpBehavior::Queue,
                    }
                } else {
                    maho_agent::turn_control::FollowUpBehavior::Queue
                }
            };

            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_millis() as u64;

            // Compute the follow-up outcome while holding the turn-controller
            // lock, but dispatch the unified event AFTER the guard is dropped:
            // the event callback may re-enter the FFI (e.g.
            // maho_agent_turn_queue_depth) and would deadlock re-locking the
            // same non-reentrant mutex.
            let outcome: Option<String> = {
                let mut controller = session
                    .turn_controller
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                match controller.submit_follow_up(msg_text.clone(), behavior, now) {
                    Ok(maho_agent::turn_control::FollowUpSubmissionResult::Queued { id, position }) => {
                        Some(serde_json::json!({
                            "event": "turn_queued",
                            "follow_up_id": id,
                            "position": position,
                            "message": msg_text,
                            "intent": "queue",
                        })
                        .to_string())
                    }
                    Ok(maho_agent::turn_control::FollowUpSubmissionResult::Steered { guidance }) => {
                        Some(serde_json::json!({
                            "event": "turn_steered",
                            "guidance": guidance,
                            "intent": "steer",
                        })
                        .to_string())
                    }
                    Ok(maho_agent::turn_control::FollowUpSubmissionResult::Interrupted {
                        cancelled_turn_id,
                    }) => {
                        Some(serde_json::json!({
                            "event": "turn_interrupted",
                            "cancelled_turn_id": cancelled_turn_id,
                            "intent": "interrupt",
                        })
                        .to_string())
                    }
                    Ok(maho_agent::turn_control::FollowUpSubmissionResult::Continued) => {
                        Some(serde_json::json!({
                            "event": "turn_continued",
                            "message": msg_text,
                            "intent": "continue",
                        })
                        .to_string())
                    }
                    Err(_) => std::option::Option::None,
                }
            };

            let reg = session
                .unified_event_registration
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .clone();
            let seq = session
                .event_seq_counter
                .fetch_add(1, std::sync::atomic::Ordering::SeqCst);

            match outcome {
                std::option::Option::Some(payload) => {
                    dispatch_unified_event(
                        &reg,
                        &None,
                        &session.callback_lease,
                        run_id,
                        seq,
                        MahoAgentEventKindV2::Status,
                        &payload,
                    );
                    true
                }
                std::option::Option::None => false,
            }
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `out_u32` must be a non-null pointer to a `u32`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_turn_queue_depth(
    session: *mut MahoAgentSession,
    out_u32: *mut u32,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || out_u32.is_null() {
                return false;
            }
            let session = &*session;
            let controller = session
                .turn_controller
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            *out_u32 = controller.queue.len() as u32;
            true
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `run_id` must be a valid null-terminated UTF-8 C string.
/// `filter_json` is an optional null-terminated UTF-8 C string (nullable).
/// Returns an allocated null-terminated C string representing the wait handle ID,
/// or null on failure. Caller owns the returned string and must free it via `maho_core_free_string` or `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_wait_register(
    session: *mut MahoAgentSession,
    run_id: *const c_char,
    filter_json: *const c_char,
    timeout_ms: u64,
) -> *mut c_char {
    ffi_safe!(
        {
            if session.is_null() || run_id.is_null() {
                return std::ptr::null_mut();
            }
            let session = &*session;
            let run_id_str = match CStr::from_ptr(run_id).to_str() {
                Ok(s) => s,
                Err(_) => return std::ptr::null_mut(),
            };

            let filter = if !filter_json.is_null() {
                match CStr::from_ptr(filter_json).to_str() {
                    Ok(s) => parse_event_filter(s),
                    Err(_) => return std::ptr::null_mut(),
                }
            } else {
                maho_agent::event_wait::EventFilter::new(
                    maho_agent::event_wait::EventKind::Notification,
                )
            };

            let registered = {
                let mut registry = session
                    .wait_registry
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                let now = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_millis() as u64;
                let deadline = now.saturating_add(timeout_ms.max(1));

                let run_id_obj = maho_agent::run_journal::AgentRunId::new(run_id_str);
                registry
                    .register(run_id_obj, filter, deadline)
                    .ok()
                    .map(|handle| (handle, deadline))
            };
            // The registry guard is dropped above on purpose: dispatching the
            // wait_registered event while holding it would deadlock any callback
            // that re-enters the FFI (e.g. maho_agent_wait_poll re-locks the
            // same non-reentrant mutex).
            match registered {
                std::option::Option::Some((handle, deadline)) => {
                    let reg = session
                        .unified_event_registration
                        .lock()
                        .unwrap_or_else(|e| e.into_inner())
                        .clone();
                    let seq = session
                        .event_seq_counter
                        .fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                    let payload = serde_json::json!({
                        "event": "wait_registered",
                        "run_id": run_id_str,
                        "handle": handle.as_str(),
                        "timeout_ms": timeout_ms,
                        "deadline": deadline,
                    })
                    .to_string();
                    dispatch_unified_event(
                        &reg,
                        &None,
                        &session.callback_lease,
                        run_id_str,
                        seq,
                        MahoAgentEventKindV2::Status,
                        &payload,
                    );
                    to_c_string(handle.as_str())
                }
                std::option::Option::None => std::ptr::null_mut(),
            }
        },
        std::ptr::null_mut()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `run_id` is an optional null-terminated UTF-8 C string (nullable).
/// `event_json` is an optional null-terminated UTF-8 C string (nullable).
#[no_mangle]
pub unsafe extern "C" fn maho_agent_wait_wake(
    session: *mut MahoAgentSession,
    run_id: *const c_char,
    event_json: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let session = &*session;
            let run_id_opt = if !run_id.is_null() {
                match CStr::from_ptr(run_id).to_str() {
                    Ok(s) => Some(s),
                    Err(_) => return false,
                }
            } else {
                None
            };

            let wake_event = if !event_json.is_null() {
                match CStr::from_ptr(event_json).to_str() {
                    Ok(s) => parse_wake_event(s, run_id_opt),
                    Err(_) => return false,
                }
            } else {
                let now = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_millis() as u64;
                maho_agent::event_wait::WakeEvent {
                    run_id: run_id_opt.map(maho_agent::run_journal::AgentRunId::new),
                    kind: maho_agent::event_wait::EventKind::Notification,
                    sender: None,
                    source: None,
                    topic: None,
                    payload: serde_json::Value::Null,
                    timestamp: now,
                }
            };

            // Drop the registry guard before dispatching: wait_woken callbacks
            // may re-enter the FFI (maho_agent_wait_poll re-locks the same
            // non-reentrant mutex) and would deadlock if the guard were held.
            let woken = {
                let mut registry = session
                    .wait_registry
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                registry.wake(&wake_event)
            };
            if !woken.is_empty() {
                let reg = session
                    .unified_event_registration
                    .lock()
                    .unwrap_or_else(|e| e.into_inner())
                    .clone();
                for token in &woken {
                    let seq = session
                        .event_seq_counter
                        .fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                    let payload = serde_json::json!({
                        "event": "wait_woken",
                        "run_id": token.run_id.as_str(),
                        "handle": token.handle.as_str(),
                    })
                    .to_string();
                    dispatch_unified_event(
                        &reg,
                        &None,
                        &session.callback_lease,
                        token.run_id.as_str(),
                        seq,
                        MahoAgentEventKindV2::Status,
                        &payload,
                    );
                }
                true
            } else {
                false
            }
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `out_granted` must be a non-null pointer to a `bool`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_wait_notification_permission(
    session: *mut MahoAgentSession,
    out_granted: *mut bool,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || out_granted.is_null() {
                return false;
            }
            let session = &*session;
            *out_granted = session
                .notification_permission_granted
                .load(std::sync::atomic::Ordering::SeqCst);
            true
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_wait_set_notification_permission(
    session: *mut MahoAgentSession,
    granted: bool,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let session = &*session;
            session
                .notification_permission_granted
                .store(granted, std::sync::atomic::Ordering::SeqCst);
            true
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `run_id` must be a valid null-terminated UTF-8 C string.
/// `out_json` must be a non-null pointer to a `*mut c_char`.
/// Caller owns the allocated string and must free it via `maho_core_free_string` or `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_wait_poll(
    session: *mut MahoAgentSession,
    run_id: *const c_char,
    out_json: *mut *mut c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || run_id.is_null() || out_json.is_null() {
                return false;
            }
            let session = &*session;
            let run_id_str = match CStr::from_ptr(run_id).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let run_id_obj = maho_agent::run_journal::AgentRunId::new(run_id_str);
            let mut registry = session
                .wait_registry
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            match registry.poll(&run_id_obj) {
                Ok(status) => {
                    let status_val = match &status {
                        maho_agent::event_wait::WaitStatus::Suspended { handle, timeout_at } => {
                            serde_json::json!({
                                "status": "suspended",
                                "handle": handle.as_str(),
                                "timeout_at": timeout_at,
                                "run_id": run_id_str,
                            })
                        }
                        maho_agent::event_wait::WaitStatus::Woken { token, event } => {
                            serde_json::json!({
                                "status": "woken",
                                "handle": token.handle.as_str(),
                                "run_id": token.run_id.as_str(),
                                "event": event,
                            })
                        }
                        maho_agent::event_wait::WaitStatus::TimedOut { handle, deadline } => {
                            serde_json::json!({
                                "status": "timed_out",
                                "handle": handle.as_str(),
                                "deadline": deadline,
                                "run_id": run_id_str,
                            })
                        }
                        maho_agent::event_wait::WaitStatus::Cancelled { handle } => {
                            serde_json::json!({
                                "status": "cancelled",
                                "handle": handle.as_str(),
                                "run_id": run_id_str,
                            })
                        }
                    };
                    let c_str = to_c_string(&status_val.to_string());
                    if c_str.is_null() {
                        return false;
                    }
                    *out_json = c_str;
                    true
                }
                Err(_) => false,
            }
        },
        false
    )
}

/// # Safety
/// `session` is an optional pointer to a `MahoAgentSession` (nullable).
/// `category` must be a valid null-terminated UTF-8 C string.
/// `preference_opt_json` is an optional null-terminated UTF-8 C string (nullable).
/// `available_opt_json` is an optional null-terminated UTF-8 C string (nullable).
/// `out_json` must be a non-null pointer to a `*mut c_char`.
/// Caller owns the allocated string and must free it via `maho_core_free_string` or `maho_string_free`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_resolve_model(
    _session: *mut MahoAgentSession,
    category: *const c_char,
    preference_opt_json: *const c_char,
    available_opt_json: *const c_char,
    out_json: *mut *mut c_char,
) -> bool {
    ffi_safe!(
        {
            if category.is_null() || out_json.is_null() {
                return false;
            }
            let category_str = match CStr::from_ptr(category).to_str() {
                Ok(s) => s,
                Err(_) => return false,
            };
            let task_category = parse_task_category(category_str);

            let mut explicit_preference: Option<String> = None;
            let mut explicit_available: Option<Vec<String>> = None;

            if !preference_opt_json.is_null() {
                if let Ok(pref_str) = CStr::from_ptr(preference_opt_json).to_str() {
                    let pref_trimmed = pref_str.trim();
                    if !pref_trimmed.is_empty() {
                        if let Ok(val) = serde_json::from_str::<serde_json::Value>(pref_trimmed) {
                            if let Some(obj) = val.as_object() {
                                if let Some(p) = obj
                                    .get("preference")
                                    .or_else(|| obj.get("model"))
                                    .or_else(|| obj.get("preferred_model"))
                                    .and_then(|v| v.as_str())
                                {
                                    explicit_preference = Some(p.to_string());
                                }
                                if let Some(arr) = obj
                                    .get("available")
                                    .or_else(|| obj.get("available_models"))
                                    .and_then(|v| v.as_array())
                                {
                                    let mut list = Vec::new();
                                    for item in arr {
                                        if let Some(s) = item.as_str() {
                                            list.push(s.to_string());
                                        }
                                    }
                                    explicit_available = Some(list);
                                }
                            } else if let Some(s) = val.as_str() {
                                explicit_preference = Some(s.to_string());
                            } else {
                                explicit_preference = Some(pref_trimmed.to_string());
                            }
                        } else {
                            explicit_preference = Some(pref_trimmed.to_string());
                        }
                    }
                }
            }

            if !available_opt_json.is_null() {
                if let Ok(avail_str) = CStr::from_ptr(available_opt_json).to_str() {
                    let avail_trimmed = avail_str.trim();
                    if !avail_trimmed.is_empty() {
                        if let Ok(val) = serde_json::from_str::<serde_json::Value>(avail_trimmed) {
                            if let Some(arr) = val.as_array() {
                                let mut list = Vec::new();
                                for item in arr {
                                    if let Some(s) = item.as_str() {
                                        list.push(s.to_string());
                                    }
                                }
                                explicit_available = Some(list);
                            } else if let Some(s) = val.as_str() {
                                explicit_available = Some(vec![s.to_string()]);
                            }
                        }
                    }
                }
            }

            let policy = maho_agent::model_routing::RoutingPolicy::default();
            let decision_res = match explicit_available {
                Some(ref available_list) => {
                    let available_refs: Vec<&str> =
                        available_list.iter().map(|s| s.as_str()).collect();
                    policy.resolve_with_availability(
                        task_category,
                        explicit_preference.as_deref(),
                        &available_refs,
                    )
                }
                None => {
                    // Null availability -> use category default without availability filtering
                    let default_model = policy
                        .config
                        .default_model_by_category
                        .get(&task_category)
                        .cloned()
                        .unwrap_or_else(|| "google/gemini-3-flash-lite:free".to_string());

                    let (model, is_fallback, fallback_model, reason, confidence) =
                        if let Some(pref) = explicit_preference {
                            (
                                pref.clone(),
                                false,
                                None,
                                format!(
                                    "Selected preferred model '{}' for category {:?}",
                                    pref, task_category
                                ),
                                1.0,
                            )
                        } else {
                            (
                                default_model.clone(),
                                false,
                                None,
                                format!(
                                    "Selected default model '{}' for category {:?}",
                                    default_model, task_category
                                ),
                                1.0,
                            )
                        };

                    Ok(maho_agent::model_routing::RoutingDecision {
                        category: task_category,
                        model: model.clone(),
                        selected_model: model,
                        fallback_model,
                        reason,
                        confidence,
                        is_fallback,
                    })
                }
            };

            match decision_res {
                Ok(decision) => {
                    let out_val = serde_json::json!({
                        "model": decision.model,
                        "selected_model": decision.selected_model,
                        "fallback_model": decision.fallback_model,
                        "reason": decision.reason,
                        "category": format!("{:?}", decision.category).to_ascii_lowercase(),
                        "confidence": decision.confidence,
                        "is_fallback": decision.is_fallback,
                    });
                    let c_str = to_c_string(&out_val.to_string());
                    if c_str.is_null() {
                        return false;
                    }
                    *out_json = c_str;
                    true
                }
                Err(err) => {
                    let out_val = serde_json::json!({
                        "error": err.to_string(),
                        "category": format!("{:?}", task_category).to_ascii_lowercase(),
                    });
                    let c_str = to_c_string(&out_val.to_string());
                    if c_str.is_null() {
                        return false;
                    }
                    *out_json = c_str;
                    false
                }
            }
        },
        false
    )
}

/// Returns metadata for the last context compaction event in the session's run journal.
///
/// Output JSON shape:
/// `{"triggered_at": <u64>, "dropped_turns": [<usize>, ...], "kept_markers": [<string>, ...], "session_id": "<string>"}`
///
/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `out_json` must be a non-null pointer to a `*mut c_char` location.
/// Caller owns the allocated string and must free it via `maho_string_free` or `maho_core_free_string`.
#[no_mangle]
pub unsafe extern "C" fn maho_agent_get_last_compaction_info(
    session: *mut MahoAgentSession,
    out_json: *mut *mut c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() || out_json.is_null() {
                return false;
            }
            *out_json = ptr::null_mut();
            let session = &*session;

            let journal_lock = session.runtime.run_journal();
            let journal = journal_lock.lock().unwrap_or_else(|e| e.into_inner());

            let info = journal.last_compaction_info_for_session(&session.session_id);
            match info {
                Some(compaction_info) => {
                    let json_str = match serde_json::to_string(&compaction_info) {
                        Ok(s) => s,
                        Err(_) => return false,
                    };
                    let c_str = to_c_string(&json_str);
                    if c_str.is_null() {
                        return false;
                    }
                    *out_json = c_str;
                    true
                }
                None => false,
            }
        },
        false
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_agent::interaction::{InteractionAnswer, InteractionRequestId};
    use maho_agent::run_journal::{AgentRunId, RunCheckpointKind, RunJournal};
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    use std::sync::{Arc, Mutex};
    include!("review_runtime.rs");

    #[derive(Debug, Clone)]
    struct CapturedEnvelope {
        abi_version: u32,
        run_id: String,
        event_seq: u64,
        kind: MahoAgentEventKindV2,
        payload_json: String,
    }

    unsafe extern "C" fn test_unified_callback(
        user_data: *mut c_void,
        event: *const MahoUnifiedAgentEventEnvelope,
    ) {
        if user_data.is_null() || event.is_null() {
            return;
        }
        let list = &*(user_data as *const Mutex<Vec<CapturedEnvelope>>);
        let ev = &*event;
        let run_id = if ev.run_id.is_null() {
            String::new()
        } else {
            CStr::from_ptr(ev.run_id).to_string_lossy().into_owned()
        };
        let payload_json = if ev.payload_json.is_null() {
            String::new()
        } else {
            CStr::from_ptr(ev.payload_json)
                .to_string_lossy()
                .into_owned()
        };

        list.lock().unwrap().push(CapturedEnvelope {
            abi_version: ev.abi_version,
            run_id,
            event_seq: ev.event_seq,
            kind: ev.kind,
            payload_json,
        });
    }

    unsafe extern "C" fn test_release_callback(user_data: *mut c_void) {
        if !user_data.is_null() {
            let counter = &*(user_data as *const AtomicUsize);
            counter.fetch_add(1, Ordering::SeqCst);
        }
    }

    fn create_test_session() -> Box<MahoAgentSession> {
        MahoAgentSession::create_for_test("test-session-1")
    }

    /// F3-R4 regression guard: the exported runtime-config setter (crate-root
    /// `maho_agent_set_runtime_config`) and the exported unified-event plumbing
    /// (agent::events) MUST operate on one and the same session layout.
    ///
    /// Historically two struct definitions existed (crate root vs the old
    /// agent::session duplicate). The exported create path built the root
    /// struct while the event FFI locked fields of the other definition at
    /// different offsets — inside the browser that locked String/fn-pointer
    /// bytes as a pthread mutex: "failed to lock mutex: Invalid argument
    /// (os error 22)" + panic=abort -> SIGABRT at every agent turn start,
    /// before any LLM request. See .omo/evidence/f3-r4-mutex-panic-rca.md.
    #[test]
    fn test_runtime_config_and_unified_event_ffi_seams_share_one_session() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        // Seam 1 (crate root): exported runtime-config setter.
        let tier = CString::new("read_only").unwrap();
        unsafe {
            crate::maho_agent_set_runtime_config(session_ptr, tier.as_ptr(), false, true);
        }
        {
            let cfg = session
                .runtime_config
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            assert_eq!(cfg.permission_tier, "read_only");
            assert!(!cfg.final_confirm);
            assert!(cfg.proactive_mode);
        }

        // Seam 2 (agent::events): exported unified-event registration + turn
        // queueing on the SAME pointer must lock real mutexes and round-trip
        // an event to the registered callback.
        let unified_events = Arc::new(Mutex::new(Vec::<CapturedEnvelope>::new()));
        unsafe {
            assert!(maho_agent_register_unified_event_callback(
                session_ptr,
                Some(test_unified_callback),
                Arc::as_ptr(&unified_events) as *mut c_void,
            ));
        }
        {
            let mut controller = session.turn_controller.lock().unwrap();
            let _token = controller.start_turn("layout-unify-turn").unwrap();
        }
        let run_id_c = CString::new("run-layout-unify-1").unwrap();
        let msg_c = CString::new(r#"{"message":"layout probe"}"#).unwrap();
        let intent_queue_c = CString::new("queue").unwrap();
        unsafe {
            assert!(maho_agent_turn_submit(
                session_ptr,
                run_id_c.as_ptr(),
                msg_c.as_ptr(),
                intent_queue_c.as_ptr(),
            ));
        }
        let captured = unified_events.lock().unwrap().clone();
        assert_eq!(captured.len(), 1);
        assert_eq!(captured[0].abi_version, 2);
        assert_eq!(captured[0].kind, MahoAgentEventKindV2::Status);
        assert!(captured[0].payload_json.contains("turn_queued"));
        assert!(captured[0].payload_json.contains("layout probe"));
    }

    #[test]
    fn test_unified_event_envelope_dispatch_monotonic() {
        let events = Arc::new(Mutex::new(Vec::<CapturedEnvelope>::new()));
        let reg = Some(UnifiedEventCallbackRegistration {
            callback: Some(test_unified_callback),
            user_data: Arc::as_ptr(&events) as usize,
            lease: None,
        });

        let run_id = "run-test-monotonic-123";
        for seq in 1..=5 {
            dispatch_unified_event(
                &reg,
                &None,
                &None,
                run_id,
                seq,
                match seq {
                    1 => MahoAgentEventKindV2::Token,
                    2 => MahoAgentEventKindV2::Thinking,
                    3 => MahoAgentEventKindV2::ToolCall,
                    4 => MahoAgentEventKindV2::ToolResult,
                    _ => MahoAgentEventKindV2::Completed,
                },
                &format!(r#"{{"step":{seq}}}"#),
            );
        }

        let captured = events.lock().unwrap().clone();
        assert_eq!(captured.len(), 5);
        for (i, ev) in captured.iter().enumerate() {
            let expected_seq = (i + 1) as u64;
            assert_eq!(ev.abi_version, 2);
            assert_eq!(ev.run_id, run_id);
            assert_eq!(ev.event_seq, expected_seq);
            assert!(ev
                .payload_json
                .contains(&format!(r#""step":{expected_seq}"#)));
        }
        assert_eq!(captured[0].kind, MahoAgentEventKindV2::Token);
        assert_eq!(captured[1].kind, MahoAgentEventKindV2::Thinking);
        assert_eq!(captured[2].kind, MahoAgentEventKindV2::ToolCall);
        assert_eq!(captured[3].kind, MahoAgentEventKindV2::ToolResult);
        assert_eq!(captured[4].kind, MahoAgentEventKindV2::Completed);
    }

    #[test]
    fn test_unified_callback_lease_releases_exactly_once() {
        let release_counter = Arc::new(AtomicUsize::new(0));
        let lease = SessionCallbackLease::new(
            Some(test_release_callback),
            Arc::as_ptr(&release_counter) as *mut c_void,
        );
        lease.arm();

        let events = Arc::new(Mutex::new(Vec::<CapturedEnvelope>::new()));
        let mut reg = Some(UnifiedEventCallbackRegistration {
            callback: Some(test_unified_callback),
            user_data: Arc::as_ptr(&events) as usize,
            lease: Some(lease),
        });

        assert_eq!(release_counter.load(Ordering::SeqCst), 0);

        // Dispatching while leased works
        dispatch_unified_event(
            &reg,
            &None,
            &None,
            "run-1",
            1,
            MahoAgentEventKindV2::Token,
            r#"{"token":"hi"}"#,
        );
        assert_eq!(events.lock().unwrap().len(), 1);
        assert_eq!(release_counter.load(Ordering::SeqCst), 0);

        // Dropping registration releases the lease exactly once
        reg = None;
        assert_eq!(release_counter.load(Ordering::SeqCst), 1);

        // Further dispatches do not crash or emit
        dispatch_unified_event(
            &reg,
            &None,
            &None,
            "run-1",
            2,
            MahoAgentEventKindV2::Token,
            r#"{"token":"hi2"}"#,
        );
        assert_eq!(events.lock().unwrap().len(), 1);
        assert_eq!(release_counter.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn test_unified_callback_lease_none_release_skips_cleanly() {
        let lease = SessionCallbackLease::new(None, std::ptr::null_mut());
        lease.arm();

        let events = Arc::new(Mutex::new(Vec::<CapturedEnvelope>::new()));
        let mut reg = Some(UnifiedEventCallbackRegistration {
            callback: Some(test_unified_callback),
            user_data: Arc::as_ptr(&events) as usize,
            lease: Some(lease),
        });

        dispatch_unified_event(
            &reg,
            &None,
            &None,
            "run-none",
            1,
            MahoAgentEventKindV2::Token,
            r#"{"token":"hi"}"#,
        );
        assert_eq!(events.lock().unwrap().len(), 1);

        // Dropping registration with None release lease skips release callback without issue
        reg = None;
        assert!(reg.is_none());
    }

    #[test]
    fn test_parse_interaction_answer_variants() {
        let a1 = parse_interaction_answer(r#"{"answer_kind":"confirmed"}"#);
        assert_eq!(a1, InteractionAnswer::Confirmed);

        let a2 = parse_interaction_answer(r#"{"answer_kind":"denied"}"#);
        assert_eq!(a2, InteractionAnswer::Denied);

        let a3 = parse_interaction_answer(r#"{"answer_kind":"text","0":"custom answer"}"#);
        assert!(matches!(a3, InteractionAnswer::Text(s) if s == "custom answer"));

        let a4 = parse_interaction_answer(r#"{"confirmed":true}"#);
        assert_eq!(a4, InteractionAnswer::Confirmed);

        let a5 = parse_interaction_answer(r#"{"action":"deny"}"#);
        assert_eq!(a5, InteractionAnswer::Denied);

        let a6 = parse_interaction_answer(r#"{"option":"opt-blue"}"#);
        assert_eq!(
            a6,
            InteractionAnswer::SelectedOption("opt-blue".to_string())
        );

        let a7 = parse_interaction_answer("plain string response");
        assert_eq!(
            a7,
            InteractionAnswer::Text("plain string response".to_string())
        );
    }

    #[test]
    fn test_interaction_resolve_and_channels() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        // Set up pending question in interaction broker
        let req_id = {
            let broker_lock = session.runtime.interaction_broker();
            let mut broker = broker_lock.lock().unwrap();
            broker
                .ask_user_question(
                    "Pick one",
                    vec![maho_agent::interaction::InteractionOption::new(
                        "opt1", "Option 1",
                    )],
                )
                .unwrap()
        };

        let req_id_c = CString::new(req_id.as_str()).unwrap();
        let answer_c = CString::new(r#"{"answer_kind":"confirmed"}"#).unwrap();

        unsafe {
            let ok =
                maho_agent_interaction_resolve(session_ptr, req_id_c.as_ptr(), answer_c.as_ptr());
            assert!(ok);
        }

        let broker_lock = session.runtime.interaction_broker();
        let broker = broker_lock.lock().unwrap();
        let req = broker.get(&req_id).expect("request must exist");
        assert!(req.state.is_resolved());
        assert_eq!(
            req.state,
            maho_agent::interaction::InteractionState::Resolved(InteractionAnswer::Confirmed)
        );
    }

    #[test]
    fn test_get_events_after_and_retention_gap() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;
        let run_id = AgentRunId::new("run-replay-test");

        // 1. Initialize run and populate 5 events in journal
        {
            let journal_lock = session.runtime.run_journal();
            let mut journal = journal_lock.lock().unwrap();
            journal.start_run(run_id.clone(), "session-1").unwrap();
            for i in 1..=5 {
                journal
                    .append_event(&run_id, format!("event_{i}"), format!(r#"{{"i":{i}}}"#))
                    .unwrap();
            }
            journal
                .record_terminal(&run_id, RunCheckpointKind::Completed, None)
                .unwrap();
        }

        // 2. Fetch events after seq 2 -> should return events 3, 4, 5 without duplicates and gap: false
        let run_id_c = CString::new("run-replay-test").unwrap();
        let mut out_json: *mut c_char = ptr::null_mut();
        unsafe {
            let ok = maho_agent_get_events_after(session_ptr, run_id_c.as_ptr(), 2, &mut out_json);
            assert!(ok);
            assert!(!out_json.is_null());
            let json_str = CStr::from_ptr(out_json).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(json_str).unwrap();
            assert_eq!(val["run_id"], "run-replay-test");
            assert_eq!(val["gap"], false);
            assert_eq!(val["is_terminal"], true);
            let events = val["events"].as_array().unwrap();
            assert_eq!(events.len(), 3);
            assert_eq!(events[0]["event_seq"], 3);
            assert_eq!(events[1]["event_seq"], 4);
            assert_eq!(events[2]["event_seq"], 5);
            crate::common::maho_core_free_string(out_json);
        }

        // 3. Query unknown run -> explicit gap marker
        let unknown_run_c = CString::new("run-non-existent").unwrap();
        let mut out_gap_json: *mut c_char = ptr::null_mut();
        unsafe {
            let ok = maho_agent_get_events_after(
                session_ptr,
                unknown_run_c.as_ptr(),
                0,
                &mut out_gap_json,
            );
            assert!(ok);
            assert!(!out_gap_json.is_null());
            let json_str = CStr::from_ptr(out_gap_json).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(json_str).unwrap();
            assert_eq!(val["gap"], true);
            assert_eq!(val["events"].as_array().unwrap().len(), 0);
            crate::common::maho_core_free_string(out_gap_json);
        }
    }

    #[test]
    fn test_get_events_after_retention_window_gap_marker() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;
        // Simulate a run loaded from storage where old entries 1..9 were pruned from retention window,
        // leaving entries 10, 11, 12 in the journal.
        let journal_json = serde_json::json!({
            "runs": {
                "run-pruned-retention": {
                    "run_id": "run-pruned-retention",
                    "session_id": "session-pruned",
                    "next_seq": 13,
                    "entries": [
                        {
                            "run_id": "run-pruned-retention",
                            "event_seq": 10,
                            "event_name": "event_10",
                            "payload_json": "{}",
                            "timestamp": 0
                        },
                        {
                            "run_id": "run-pruned-retention",
                            "event_seq": 11,
                            "event_name": "event_11",
                            "payload_json": "{}",
                            "timestamp": 0
                        },
                        {
                            "run_id": "run-pruned-retention",
                            "event_seq": 12,
                            "event_name": "event_12",
                            "payload_json": "{}",
                            "timestamp": 0
                        }
                    ],
                    "checkpoints": [],
                    "terminal_kind": "completed"
                }
            }
        })
        .to_string();

        let restored_journal = RunJournal::from_json(&journal_json).unwrap();
        {
            let journal_lock = session.runtime.run_journal();
            let mut journal = journal_lock.lock().unwrap();
            *journal = restored_journal;
        }

        // Case A: Querying with after_seq = 2 (older than oldest retained seq 10) -> returns explicit gap marker
        let run_id_c = CString::new("run-pruned-retention").unwrap();
        let mut out_gap_json: *mut c_char = ptr::null_mut();
        unsafe {
            let ok =
                maho_agent_get_events_after(session_ptr, run_id_c.as_ptr(), 2, &mut out_gap_json);
            assert!(ok);
            assert!(!out_gap_json.is_null());
            let json_str = CStr::from_ptr(out_gap_json).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(json_str).unwrap();
            assert_eq!(
                val["gap"], true,
                "Replay before oldest retained event must produce explicit gap marker"
            );
            crate::common::maho_core_free_string(out_gap_json);
        }

        // Case B: Querying with after_seq = 10 (within retention window) -> returns entries 11 and 12, gap: false
        let mut out_valid_json: *mut c_char = ptr::null_mut();
        unsafe {
            let ok = maho_agent_get_events_after(
                session_ptr,
                run_id_c.as_ptr(),
                10,
                &mut out_valid_json,
            );
            assert!(ok);
            assert!(!out_valid_json.is_null());
            let json_str = CStr::from_ptr(out_valid_json).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(json_str).unwrap();
            assert_eq!(val["gap"], false);
            let events = val["events"].as_array().unwrap();
            assert_eq!(events.len(), 2);
            assert_eq!(events[0]["event_seq"], 11);
            assert_eq!(events[1]["event_seq"], 12);
            crate::common::maho_core_free_string(out_valid_json);
        }
    }

    #[test]
    fn test_interaction_resolve_same_turn_channel_resume() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        // Put request into broker and channel into runtime channels
        let req_id = {
            let broker_lock = session.runtime.interaction_broker();
            let mut broker = broker_lock.lock().unwrap();
            broker
                .ask_user_question(
                    "Should we proceed?",
                    vec![maho_agent::interaction::InteractionOption::new(
                        "yes", "Yes",
                    )],
                )
                .unwrap()
        };

        let req_id_c = CString::new(req_id.as_str()).unwrap();
        let answer_c = CString::new(r#"{"text":"user resolved text"}"#).unwrap();

        unsafe {
            let ok =
                maho_agent_interaction_resolve(session_ptr, req_id_c.as_ptr(), answer_c.as_ptr());
            assert!(ok);
        }

        let broker_lock = session.runtime.interaction_broker();
        let broker = broker_lock.lock().unwrap();
        let req = broker.get(&req_id).unwrap();
        assert!(req.state.is_resolved());
        assert_eq!(
            req.state,
            maho_agent::interaction::InteractionState::Resolved(InteractionAnswer::Text(
                "user resolved text".to_string()
            ))
        );
    }

    #[test]
    fn review_synchronous_callbacks_can_query_session() {
        const CHILD: &str = "MAHO_REENTRY_REGRESSION_CHILD";
        if std::env::var_os(CHILD).is_none() {
            let child = std::process::Command::new(std::env::current_exe().unwrap())
                .args(["--exact", "agent::events::tests::review_synchronous_callbacks_can_query_session", "--nocapture"])
                .env(CHILD, "1")
                .spawn().unwrap();
            let pid = child.id();
            let (tx, rx) = std::sync::mpsc::channel();
            let waiter = std::thread::spawn(move || {
                let mut child = child;
                tx.send(child.wait().unwrap()).unwrap();
            });
            let result = rx.recv_timeout(std::time::Duration::from_secs(10));
            if result.is_err() {
                std::process::Command::new("kill").args(["-KILL", &pid.to_string()]).status().unwrap();
            }
            waiter.join().unwrap();
            assert!(result.expect("callback-side state query deadlocked").success());
            return;
        }
        unsafe extern "C" fn query(user_data: *mut c_void, event: *const MahoUnifiedAgentEventEnvelope) {
            let session = user_data.cast::<MahoAgentSession>();
            let payload = CStr::from_ptr((*event).payload_json).to_string_lossy();
            if payload.contains("turn_queued") {
                let mut depth = 0;
                maho_agent_turn_queue_depth(session, &mut depth);
            } else {
                let mut output = ptr::null_mut();
                maho_agent_wait_poll(session, (*event).run_id, &mut output);
                crate::common::maho_core_free_string(output);
            }
        }
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;
        let run = CString::new("reentry-run").unwrap();
        let message = CString::new("follow up").unwrap();
        unsafe {
            assert!(maho_agent_register_unified_event_callback(session_ptr, Some(query), session_ptr.cast()));
            assert!(maho_agent_turn_submit(session_ptr, run.as_ptr(), message.as_ptr(), ptr::null()));
            let handle = maho_agent_wait_register(session_ptr, run.as_ptr(), ptr::null(), 10000);
            assert!(!handle.is_null());
            crate::common::maho_core_free_string(handle);
            assert!(maho_agent_wait_wake(session_ptr, run.as_ptr(), ptr::null()));
        }
    }

    #[test]
    fn test_legacy_and_unified_callbacks_coexistence() {
        let mut session = create_test_session();
        session.runtime = Arc::new(ReviewRuntime::default());
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        let unified_events = Arc::new(Mutex::new(Vec::<CapturedEnvelope>::new()));
        unsafe {
            assert!(maho_agent_register_unified_event_callback(
                session_ptr,
                Some(test_unified_callback),
                Arc::as_ptr(&unified_events) as *mut c_void,
            ));
        }

        // One shared context for both legacy callbacks: the leased ABI passes a
        // single user_data to every per-turn callback, so each callback must
        // interpret the SAME object. (The previous wiring passed the token-list
        // Arc here while the complete callback cast it to an AtomicBool — a
        // type-confused store that also made the assertion unfalsifiable.)
        struct LegacyCapture {
            tokens: Mutex<Vec<String>>,
            completed: AtomicBool,
        }
        let legacy_ctx = Arc::new(LegacyCapture {
            tokens: Mutex::new(Vec::new()),
            completed: AtomicBool::new(false),
        });
        unsafe extern "C" fn legacy_token_cb(user_data: *mut c_void, token: *const c_char) {
            if !user_data.is_null() && !token.is_null() {
                let ctx = &*(user_data as *const LegacyCapture);
                ctx.tokens
                    .lock()
                    .unwrap()
                    .push(CStr::from_ptr(token).to_str().unwrap().to_string());
            }
        }

        unsafe extern "C" fn legacy_complete_cb(
            user_data: *mut c_void,
            _full_text: *const c_char,
            _tool_calls_json: *const c_char,
        ) {
            if !user_data.is_null() {
                let ctx = &*(user_data as *const LegacyCapture);
                ctx.completed.store(true, Ordering::SeqCst);
            }
        }

        let (release_tx, release_rx) = std::sync::mpsc::channel::<()>();
        unsafe extern "C" fn released(data: *mut c_void) {
            let tx = &*data.cast::<std::sync::mpsc::Sender<()>>();
            tx.send(()).unwrap();
        }
        let message_c = CString::new("Hello agent").unwrap();
        unsafe {
            let ok = maho_agent_send_message_leased(
                session_ptr,
                message_c.as_ptr(),
                Some(legacy_token_cb),
                None,
                None,
                None,
                Some(legacy_complete_cb),
                None,
                Arc::as_ptr(&legacy_ctx) as *mut c_void,
                released,
                (&release_tx as *const std::sync::mpsc::Sender<()>).cast_mut().cast(),
            );
            assert!(ok);
        }

        release_rx.recv_timeout(std::time::Duration::from_secs(10)).unwrap();
        assert!(
            legacy_ctx.completed.load(Ordering::SeqCst),
            "completion callback did not signal its context"
        );
    }

    #[test]
    fn test_turn_submit_during_active_run_emits_queued_then_executes() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        let unified_events = Arc::new(Mutex::new(Vec::<CapturedEnvelope>::new()));
        unsafe {
            assert!(maho_agent_register_unified_event_callback(
                session_ptr,
                Some(test_unified_callback),
                Arc::as_ptr(&unified_events) as *mut c_void,
            ));
        }

        // Start active turn
        {
            let mut controller = session.turn_controller.lock().unwrap();
            let _token = controller.start_turn("active-turn-1").unwrap();
        }

        let run_id_c = CString::new("run-turn-ctrl-1").unwrap();
        let msg_c = CString::new(r#"{"message":"Follow up question"}"#).unwrap();
        let intent_queue_c = CString::new("queue").unwrap();

        unsafe {
            let ok = maho_agent_turn_submit(
                session_ptr,
                run_id_c.as_ptr(),
                msg_c.as_ptr(),
                intent_queue_c.as_ptr(),
            );
            assert!(ok);
        }

        // Check queue depth
        let mut depth: u32 = 0;
        unsafe {
            let ok = maho_agent_turn_queue_depth(session_ptr, &mut depth);
            assert!(ok);
            assert_eq!(depth, 1);
        }

        // Check unified event envelope
        let captured = unified_events.lock().unwrap().clone();
        assert_eq!(captured.len(), 1);
        assert_eq!(captured[0].kind, MahoAgentEventKindV2::Status);
        assert_eq!(captured[0].event_seq, 1);
        assert!(captured[0].payload_json.contains("turn_queued"));
        assert!(captured[0].payload_json.contains("Follow up question"));

        // Complete current active turn and pop next queued item
        let next_item = {
            let mut controller = session.turn_controller.lock().unwrap();
            controller.complete_current_turn().unwrap()
        };
        assert_eq!(next_item.message, "Follow up question");

        let mut depth_after: u32 = 0;
        unsafe {
            assert!(maho_agent_turn_queue_depth(session_ptr, &mut depth_after));
            assert_eq!(depth_after, 0);
        }

        // Submit another turn while inactive with queue behavior
        let msg2_c = CString::new("Second question").unwrap();
        unsafe {
            let ok = maho_agent_turn_submit(
                session_ptr,
                run_id_c.as_ptr(),
                msg2_c.as_ptr(),
                intent_queue_c.as_ptr(),
            );
            assert!(ok);
            assert!(maho_agent_turn_queue_depth(session_ptr, &mut depth_after));
            assert_eq!(depth_after, 1);
        }

        // Check monotonic sequence
        let captured2 = unified_events.lock().unwrap().clone();
        assert_eq!(captured2.len(), 2);
        assert_eq!(captured2[1].event_seq, 2);
        assert!(captured2[1].payload_json.contains("Second question"));
    }

    #[test]
    fn test_wait_register_and_wake_resolves_and_resumes() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        let unified_events = Arc::new(Mutex::new(Vec::<CapturedEnvelope>::new()));
        unsafe {
            assert!(maho_agent_register_unified_event_callback(
                session_ptr,
                Some(test_unified_callback),
                Arc::as_ptr(&unified_events) as *mut c_void,
            ));
        }

        let run_id_c = CString::new("run-wait-wake-1").unwrap();
        let filter_c = CString::new(r#"{"kind":"notification","sender":"alice"}"#).unwrap();

        // 1. Register wait
        let handle_ptr = unsafe {
            maho_agent_wait_register(session_ptr, run_id_c.as_ptr(), filter_c.as_ptr(), 10000)
        };
        assert!(!handle_ptr.is_null());
        let handle_str = unsafe { CStr::from_ptr(handle_ptr).to_str().unwrap() };
        assert_eq!(handle_str, "wait-handle-1");
        unsafe { crate::common::maho_core_free_string(handle_ptr) };

        // Poll status: suspended
        let mut poll_json: *mut c_char = ptr::null_mut();
        unsafe {
            let ok = maho_agent_wait_poll(session_ptr, run_id_c.as_ptr(), &mut poll_json);
            assert!(ok);
            assert!(!poll_json.is_null());
            let poll_str = CStr::from_ptr(poll_json).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(poll_str).unwrap();
            assert_eq!(val["status"], "suspended");
            crate::common::maho_core_free_string(poll_json);
        }

        // 2. Wake injection with matching event
        let wake_c =
            CString::new(r#"{"kind":"notification","sender":"alice","payload":{"msg":"hello"}}"#)
                .unwrap();
        unsafe {
            let ok = maho_agent_wait_wake(session_ptr, run_id_c.as_ptr(), wake_c.as_ptr());
            assert!(ok);
        }

        // Poll status: woken
        let mut poll_json2: *mut c_char = ptr::null_mut();
        unsafe {
            let ok = maho_agent_wait_poll(session_ptr, run_id_c.as_ptr(), &mut poll_json2);
            assert!(ok);
            assert!(!poll_json2.is_null());
            let poll_str = CStr::from_ptr(poll_json2).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(poll_str).unwrap();
            assert_eq!(val["status"], "woken");
            crate::common::maho_core_free_string(poll_json2);
        }

        // Notification permission query/set
        let mut granted = false;
        unsafe {
            assert!(maho_agent_wait_notification_permission(
                session_ptr,
                &mut granted
            ));
            assert!(granted);
            assert!(maho_agent_wait_set_notification_permission(
                session_ptr,
                false
            ));
            assert!(maho_agent_wait_notification_permission(
                session_ptr,
                &mut granted
            ));
            assert!(!granted);
        }

        // Verify events emitted with monotonic sequence
        let captured = unified_events.lock().unwrap().clone();
        assert_eq!(captured.len(), 2);
        assert_eq!(captured[0].event_seq, 1);
        assert!(captured[0].payload_json.contains("wait_registered"));
        assert_eq!(captured[1].event_seq, 2);
        assert!(captured[1].payload_json.contains("wait_woken"));
    }

    #[test]
    fn test_wait_timeout_via_mock_clock_expires_fail_closed() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        let mock_clock = Arc::new(maho_agent::event_wait::MockClock::new(1000));
        {
            let mut reg = session.wait_registry.lock().unwrap();
            *reg = maho_agent::event_wait::WaitRegistry::with_clock(mock_clock.clone());
        }

        let run_id = maho_agent::run_journal::AgentRunId::new("run-timeout-probe");
        {
            let mut reg = session.wait_registry.lock().unwrap();
            reg.register(
                run_id.clone(),
                maho_agent::event_wait::EventFilter::new(
                    maho_agent::event_wait::EventKind::Notification,
                ),
                1500,
            )
            .unwrap();
        }

        // Advance mock clock past deadline (1000 + 600 = 1600 > 1500)
        mock_clock.advance(600);

        let run_id_c = CString::new("run-timeout-probe").unwrap();
        let mut poll_json: *mut c_char = ptr::null_mut();
        unsafe {
            let ok = maho_agent_wait_poll(session_ptr, run_id_c.as_ptr(), &mut poll_json);
            assert!(ok);
            assert!(!poll_json.is_null());
            let poll_str = CStr::from_ptr(poll_json).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(poll_str).unwrap();
            assert_eq!(val["status"], "timed_out");
            crate::common::maho_core_free_string(poll_json);
        }

        // Attempting to wake expired wait must fail closed
        let wake_c = CString::new(r#"{"kind":"notification"}"#).unwrap();
        unsafe {
            let ok = maho_agent_wait_wake(session_ptr, run_id_c.as_ptr(), wake_c.as_ptr());
            assert!(!ok, "Expired wait must fail-closed and reject wake");
        }
    }

    #[test]
    fn test_resolve_model_returns_reason_on_fallback() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        let category_c = CString::new("code").unwrap();
        let pref_c = CString::new("anthropic/claude-3-7-sonnet").unwrap();
        let avail_c = CString::new(r#"["google/gemini-3-flash-lite:free"]"#).unwrap();

        let mut out_json: *mut c_char = ptr::null_mut();
        unsafe {
            let ok = maho_agent_resolve_model(
                session_ptr,
                category_c.as_ptr(),
                pref_c.as_ptr(),
                avail_c.as_ptr(),
                &mut out_json,
            );
            assert!(ok);
            assert!(!out_json.is_null());
            let json_str = CStr::from_ptr(out_json).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(json_str).unwrap();

            assert_eq!(val["model"], "google/gemini-3-flash-lite:free");
            assert_eq!(val["is_fallback"], true);
            let reason = val["reason"].as_str().unwrap();
            assert!(
                reason.contains("Preferred model 'anthropic/claude-3-7-sonnet' is unavailable in active provider list; fell back to 'google/gemini-3-flash-lite:free'"),
                "Reason must explain the fallback, got: {reason}"
            );
            crate::common::maho_core_free_string(out_json);
        }
    }

    #[test]
    fn test_resolve_model_no_availability_uses_category_default() {
        let mut session = create_test_session();
        let session_ptr = session.as_mut() as *mut MahoAgentSession;

        let category_c = CString::new("coding").unwrap();
        let mut out_json: *mut c_char = ptr::null_mut();
        unsafe {
            let ok = maho_agent_resolve_model(
                session_ptr,
                category_c.as_ptr(),
                ptr::null(),
                ptr::null(),
                &mut out_json,
            );
            assert!(ok);
            assert!(!out_json.is_null());
            let json_str = CStr::from_ptr(out_json).to_str().unwrap();
            let val: serde_json::Value = serde_json::from_str(json_str).unwrap();

            assert_eq!(val["model"], "anthropic/claude-3-7-sonnet");
            assert_eq!(val["is_fallback"], false);
            assert!(val["reason"].as_str().unwrap().contains(
                "Selected default model 'anthropic/claude-3-7-sonnet' for category Coding"
            ));
            crate::common::maho_core_free_string(out_json);
        }
    }

    #[test]
    fn test_null_safety_boundary_checks() {
        unsafe {
            assert!(!maho_agent_register_unified_event_callback(
                ptr::null_mut(),
                None,
                ptr::null_mut()
            ));
            assert!(!maho_agent_register_unified_event_callback_leased(
                ptr::null_mut(),
                None,
                ptr::null_mut(),
                Some(test_release_callback),
                ptr::null_mut(),
            ));
            assert!(!maho_agent_register_unified_event_callback_leased(
                ptr::null_mut(),
                None,
                ptr::null_mut(),
                None,
                ptr::null_mut(),
            ));
            assert!(!maho_agent_unregister_unified_event_callback(
                ptr::null_mut()
            ));
            assert!(!maho_agent_interaction_resolve(
                ptr::null_mut(),
                ptr::null(),
                ptr::null()
            ));
            assert!(!maho_agent_interaction_timeout(
                ptr::null_mut(),
                ptr::null()
            ));
            assert!(!maho_agent_interaction_cancel(ptr::null_mut(), ptr::null()));
            assert!(!maho_agent_get_events_after(
                ptr::null_mut(),
                ptr::null(),
                0,
                ptr::null_mut()
            ));

            // Turn control null probes
            assert!(!maho_agent_turn_submit(
                ptr::null_mut(),
                ptr::null(),
                ptr::null(),
                ptr::null()
            ));
            assert!(!maho_agent_turn_queue_depth(
                ptr::null_mut(),
                ptr::null_mut()
            ));

            // Event wait null probes
            assert!(
                maho_agent_wait_register(ptr::null_mut(), ptr::null(), ptr::null(), 0).is_null()
            );
            assert!(!maho_agent_wait_wake(
                ptr::null_mut(),
                ptr::null(),
                ptr::null()
            ));
            assert!(!maho_agent_wait_notification_permission(
                ptr::null_mut(),
                ptr::null_mut()
            ));
            assert!(!maho_agent_wait_set_notification_permission(
                ptr::null_mut(),
                true
            ));
            assert!(!maho_agent_wait_poll(
                ptr::null_mut(),
                ptr::null(),
                ptr::null_mut()
            ));

            // Model routing null probes
            assert!(!maho_agent_resolve_model(
                ptr::null_mut(),
                ptr::null(),
                ptr::null(),
                ptr::null(),
                ptr::null_mut()
            ));
        }
    }

    #[test]
    fn test_maho_chat_config_abi_layout() {
        use std::mem::{align_of, offset_of, size_of};
        let ptr_size = size_of::<*const c_char>();
        assert_eq!(size_of::<MahoChatConfig>(), 4 * ptr_size);
        assert_eq!(align_of::<MahoChatConfig>(), align_of::<*const c_char>());
        assert_eq!(offset_of!(MahoChatConfig, api_key), 0);
        assert_eq!(offset_of!(MahoChatConfig, endpoint), ptr_size);
        assert_eq!(offset_of!(MahoChatConfig, model), 2 * ptr_size);
        assert_eq!(offset_of!(MahoChatConfig, system_instruction), 3 * ptr_size);
    }
}
