use std::borrow::Cow;
use std::ffi::{c_char, c_void, CStr, CString};
use std::sync::atomic::Ordering;
use std::sync::{Arc, Mutex};

use maho_agent::AgentRuntime;
use maho_core::maho_core::MahoCore;

use crate::agent::browser_bridge::{maho_agent_create_session_impl, MahoAgentToolResult};
use crate::agent::callback_lease::{
    clone_callback_lease, dispatch_unified_event, invoke_with_turn_callback_lease,
    CallbackReleasePolicy, MahoAgentReleaseCallback, SendableCallback, SendableUserData,
    TurnCallbackLease,
};
use crate::agent::events::MahoAgentEventKindV2;
use crate::agent::secure_storage::{MahoAgentPermissionDecision, MahoAgentSecureKey};
use crate::common::cstring_or_fallback;
use crate::ffi_safe;

const AGENT_SKILL_INJECT_BUDGET_CHARS: usize = 2_000;

/// Session-scoped runtime configuration flags (plan row 1, plumbing only).
/// The defaults mirror today's effective broker behavior so carrying them on
/// the session changes no decisions; enforcement lands in later plan rows.
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct AgentRuntimeConfig {
    pub(crate) permission_tier: String,
    pub(crate) final_confirm: bool,
    pub(crate) proactive_mode: bool,
}

impl Default for AgentRuntimeConfig {
    fn default() -> Self {
        Self {
            permission_tier: "guard".to_string(),
            final_confirm: true,
            proactive_mode: false,
        }
    }
}

impl AgentRuntimeConfig {
    /// Fails closed: unknown or empty tiers normalize back to the "guard"
    /// default; only the three canonical tiers pass through.
    pub(crate) fn normalize_tier(raw_tier: &str) -> String {
        match raw_tier {
            "read_only" | "guard" | "full_access" => raw_tier.to_string(),
            _ => "guard".to_string(),
        }
    }

    /// Rebuilds a config from persisted parts (Wave 1A session-open load).
    /// The tier is re-normalized on load so a stale or hand-edited store row
    /// cannot escalate above the canonical tiers.
    pub(crate) fn from_parts(raw_tier: &str, final_confirm: bool, proactive_mode: bool) -> Self {
        Self {
            permission_tier: Self::normalize_tier(raw_tier),
            final_confirm,
            proactive_mode,
        }
    }
}

/// Session-open load (Wave 1A): read the persisted runtime-config row for
/// `session_id` into a fresh `AgentRuntimeConfig`. An absent row — or any
/// storage error — falls back to the plumbing defaults so session creation
/// never fails on config restore.
pub(crate) fn load_runtime_config_from_store(
    storage: &maho_storage::sqlite::SqliteStorage,
    session_id: &str,
) -> AgentRuntimeConfig {
    match storage.load_agent_runtime_config(session_id) {
        Ok(Some(row)) => AgentRuntimeConfig::from_parts(
            &row.permission_tier,
            row.final_confirm,
            row.proactive_mode,
        ),
        Ok(None) => AgentRuntimeConfig::default(),
        Err(e) => {
            tracing::warn!(
                "[runtime_config] session-open load failed, using defaults \
                 (session_id={session_id}): {e}"
            );
            AgentRuntimeConfig::default()
        }
    }
}

/// Write-through persistence for the runtime-config triple (Wave 1A).
/// Best-effort: a missing db path or a storage failure is logged and
/// swallowed — the in-memory session config stays authoritative for the
/// live session and persistence must never break the FFI setter.
pub(crate) fn persist_runtime_config_to_store(
    db_path: Option<&str>,
    session_id: &str,
    config: &AgentRuntimeConfig,
) {
    let Some(db_path) = db_path else {
        tracing::debug!(
            "[runtime_config] core has no SQLite db path; skipping persist \
             (session_id={session_id})"
        );
        return;
    };
    let storage = match maho_storage::sqlite::SqliteStorage::open(db_path) {
        Ok(storage) => storage,
        Err(e) => {
            tracing::warn!(
                "[runtime_config] store open failed, config not persisted \
                 (session_id={session_id}): {e}"
            );
            return;
        }
    };
    if let Err(e) = storage.save_agent_runtime_config(
        session_id,
        &config.permission_tier,
        config.final_confirm,
        config.proactive_mode,
    ) {
        tracing::warn!("[runtime_config] persist failed (session_id={session_id}): {e}");
    }
}

pub(crate) const fn credential_error_envelope(error: maho_agent::CredentialError) -> &'static str {
    match error {
        maho_agent::CredentialError::ProviderNotConfigured => {
            r#"{"version":1,"kind":"credential_error","code":"provider_not_configured"}"#
        }
        maho_agent::CredentialError::CredentialUnusable => {
            r#"{"version":1,"kind":"credential_error","code":"credential_unusable"}"#
        }
        maho_agent::CredentialError::SecureStoreUnavailable => {
            r#"{"version":1,"kind":"credential_error","code":"secure_store_unavailable"}"#
        }
        maho_agent::CredentialError::CredentialDecryptFailed => {
            r#"{"version":1,"kind":"credential_error","code":"credential_decrypt_failed"}"#
        }
        maho_agent::CredentialError::ManagedAuthUnavailable => {
            r#"{"version":1,"kind":"credential_error","code":"managed_auth_unavailable"}"#
        }
        maho_agent::CredentialError::UnsupportedProvider => {
            r#"{"version":1,"kind":"credential_error","code":"unsupported_provider"}"#
        }
    }
}

pub(crate) fn agent_error_callback_text(error: &maho_agent::AgentError) -> Cow<'static, str> {
    match error {
        maho_agent::AgentError::Credential(credential_error) => {
            Cow::Borrowed(credential_error_envelope(*credential_error))
        }
        _ => Cow::Owned(error.to_string()),
    }
}

/// The agent FFI session type. UNIFICATION (F3-R4): this used to be a second,
/// module-local struct definition whose layout diverged from the one the
/// exported `maho_agent_create_session*` symbols construct in the crate root.
/// The event-side FFI symbols exported from `agent::events` operate on THIS
/// name; it now re-exports the single crate-root struct so every exported
/// session-taking symbol shares one layout. Do not reintroduce a duplicate
/// definition here — a second struct means the event FFI locks String/fn-
/// pointer bytes of the create-side struct as pthread mutexes
/// (EINVAL "failed to lock mutex" -> panic=abort SIGABRT; see
/// .omo/evidence/f3-r4-mutex-panic-rca.md).
pub use crate::MahoAgentSession;

impl MahoAgentSession {
    pub fn create_for_test(session_id: &str) -> Box<Self> {
        Self::create_for_test_with_db(session_id, None)
    }

    /// Test constructor with an optional file-backed MahoCore storage handle.
    /// With `db_path`, the core resolves the store through
    /// `core.sqlite_db_path()` (runtime-config write-through) and the
    /// session-open load reads the persisted row — mirroring
    /// `maho_agent_create_session_impl`. The caller must have configured the
    /// SQLCipher key (`maho_storage::sqlite::set_sqlcipher_key`) before use.
    pub fn create_for_test_with_db(session_id: &str, db_path: Option<&str>) -> Box<Self> {
        let sqlite =
            maho_storage::sqlite::SqliteStorage::open_in_memory_with_key("test-key").unwrap();
        let agent_storage: Arc<dyn maho_agent::AgentStorage> =
            Arc::new(maho_agent::MutexAgentStorage(Arc::new(Mutex::new(sqlite))));
        let backend = maho_agent::omo::factory::create_agent_runtime(
            agent_storage,
            None,
            std::path::PathBuf::from("/tmp"),
            true,
        );
        let isolation = Arc::new(maho_agent::SessionRuntime::new().unwrap());
        let mut core = Box::new(match db_path {
            Some(path) => MahoCore::new().with_storage(path),
            None => MahoCore::new(),
        });
        if db_path.is_some() {
            // Loud failure instead of a silently skipped persistence path:
            // with_storage only attaches storage when the db opened, which
            // requires the process-wide SQLCipher key to be configured.
            assert!(
                core.sqlite_db_path().is_some(),
                "create_for_test_with_db: storage failed to attach (SQLCipher key configured?)"
            );
        }
        // Session-open load (Wave 1A): restore the persisted runtime-config
        // row into the initial Mutex value, defaults when absent.
        let runtime_config = match core.storage_ref() {
            Some(storage) => load_runtime_config_from_store(storage, session_id),
            None => AgentRuntimeConfig::default(),
        };
        // Wave 1D panel-side tier binding (test path): mirror the live
        // create_session_impl binding so tests exercise the same kernel-gate
        // posture production sessions get at open.
        backend.set_runtime_tier(Some(runtime_config.permission_tier.clone()));
        backend.set_fs_whitelist_roots(maho_agent::permission::default_fs_whitelist_roots(
            &std::path::PathBuf::from("/tmp"),
        ));
        let runtime: Arc<dyn AgentRuntime> = backend;
        let core_ptr = std::ptr::NonNull::new(core.as_mut() as *mut MahoCore).unwrap();
        std::mem::forget(core);

        Box::new(MahoAgentSession {
            runtime,
            isolation,
            callback_lease: None,
            accepted_leased_turns: Arc::new(crate::agent::callback_lease::AcceptedLeasedTurns {
                current: Mutex::new(None),
            }),
            permission_callback: None,
            browser_tool_callback: None,
            session_id: session_id.to_string(),
            active_conversation_registry: Arc::new(Mutex::new(std::collections::HashMap::new())),
            core: core_ptr,
            workspace_root: std::path::PathBuf::from("/tmp"),
            active_space_id: None,
            artifact_created_cb: None,
            artifact_created_user_data: 0,
            unified_event_registration: Arc::new(Mutex::new(None)),
            turn_controller: Arc::new(Mutex::new(
                maho_agent::turn_control::SessionTurnController::new(session_id),
            )),
            wait_registry: Arc::new(Mutex::new(maho_agent::event_wait::WaitRegistry::new())),
            event_seq_counter: Arc::new(std::sync::atomic::AtomicU64::new(1)),
            notification_permission_granted: Arc::new(std::sync::atomic::AtomicBool::new(true)),
            runtime_config: Mutex::new(runtime_config),
        })
    }
}

pub(crate) struct ActiveConversationGuard {
    registry: Arc<Mutex<std::collections::HashMap<String, usize>>>,
    session_id: String,
}

impl ActiveConversationGuard {
    pub(crate) fn new(
        registry: Arc<Mutex<std::collections::HashMap<String, usize>>>,
        session_id: String,
    ) -> Self {
        let mut sessions = registry.lock().unwrap_or_else(|e| e.into_inner());
        *sessions.entry(session_id.clone()).or_insert(0) += 1;
        drop(sessions);
        Self {
            registry,
            session_id,
        }
    }
}

impl Drop for ActiveConversationGuard {
    fn drop(&mut self) {
        let mut sessions = self.registry.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(count) = sessions.get_mut(&self.session_id) {
            *count -= 1;
            if *count == 0 {
                sessions.remove(&self.session_id);
            }
        }
    }
}

/// D8 (Wave 3B, panel proactive composition): appends the fixed
/// `<proactivity_instruction>` block to the panel-composed prompt with the
/// exact semantics of the CLI path (`compose_system_prompt_proactive`,
/// maho-cli main.rs `agent_task_system_prompt`): when `proactive_mode` is on,
/// "\n\n" + the block; when off, `prompt` is left byte-identical — the flag
/// off must not change the pre-D8 panel composition.
pub(crate) fn append_proactive_block(prompt: &mut String, proactive_mode: bool) {
    if proactive_mode {
        prompt.push_str("\n\n");
        prompt.push_str(maho_agent::system_prompt::PROACTIVITY_INSTRUCTION_TEXT);
    }
}

/// Pure composition tail of `apply_agent_skill_system_prompt`, split out so
/// the proactive gating is unit-testable (runtime exposes no
/// system-prompt getter). Returns the skill-context composition — or the
/// empty fallback when no trusted-origin skill context exists — with the
/// proactive block appended iff `proactive_mode`.
pub(crate) fn compose_panel_system_prompt(
    core: &MahoCore,
    session: &MahoAgentSession,
    proactive_mode: bool,
) -> String {
    let trusted_origin = core.trusted_active_tab_origin();
    let workspace_root = session.workspace_root.to_string_lossy();
    let mut base_system_prompt = core.resolve_agent_base_system_prompt(
        &session.session_id,
        workspace_root.as_ref(),
        session.active_space_id.as_deref(),
    );
    if base_system_prompt.trim().is_empty() {
        base_system_prompt = maho_agent::system_prompt::MAHO_BROWSER_SYSTEM_PROMPT.to_string();
    }
    let mut system_prompt = core
        .compose_agent_system_prompt_with_skill_context(
            &base_system_prompt,
            trusted_origin.as_deref(),
            AGENT_SKILL_INJECT_BUDGET_CHARS,
        )
        .unwrap_or_else(|| base_system_prompt.clone());
    append_proactive_block(&mut system_prompt, proactive_mode);
    system_prompt
}

pub(crate) fn apply_agent_skill_system_prompt(
    session: &MahoAgentSession,
    runtime: &dyn AgentRuntime,
) {
    // SAFETY: `MahoAgentSession` is only constructed by
    // `maho_agent_create_session`, which stores the non-null `MahoCore` pointer
    // supplied by the browser. The browser must keep that core alive for the
    // agent session lifetime.
    let core = unsafe { session.core.as_ref() };
    // D8 (Wave 3B): thread the session runtime-config's proactive_mode into
    // the panel composition so the panel matches the CLI's row-9 gate.
    let proactive_mode = session
        .runtime_config
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .proactive_mode;
    let system_prompt = compose_panel_system_prompt(core, session, proactive_mode);
    runtime.set_system_prompt(&system_prompt);
}

pub unsafe extern "C" fn maho_agent_create_session(
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
) -> *mut MahoAgentSession {
    // SAFETY: this legacy ABI forwards its validated caller contract without a lease policy.
    unsafe {
        maho_agent_create_session_impl(
            core,
            session_id,
            workspace_root,
            allow_insecure_key_storage,
            permission_cb,
            permission_user_data,
            secure_storage_cb,
            secure_storage_user_data,
            browser_tool_cb,
            browser_tool_user_data,
            space_id,
            None,
        )
    }
}

/// # Safety
/// Same pointer requirements as `maho_agent_create_session`. When this function returns a
/// non-null session, `on_session_release` receives `session_release_user_data` exactly once after
/// the session and every runtime-held callback closure release their ownership. The callback must
/// not unwind.
pub unsafe extern "C" fn maho_agent_create_session_leased(
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
    on_session_release: MahoAgentReleaseCallback,
    session_release_user_data: *mut c_void,
) -> *mut MahoAgentSession {
    // SAFETY: the leased ABI transfers the dedicated release context only after the private
    // implementation accepts the session and creates its session callback lease.
    unsafe {
        maho_agent_create_session_impl(
            core,
            session_id,
            workspace_root,
            allow_insecure_key_storage,
            permission_cb,
            permission_user_data,
            secure_storage_cb,
            secure_storage_user_data,
            browser_tool_cb,
            browser_tool_user_data,
            space_id,
            Some(CallbackReleasePolicy {
                callback: on_session_release,
                user_data: session_release_user_data,
            }),
        )
    }
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
pub unsafe extern "C" fn maho_agent_session_free(session: *mut MahoAgentSession) {
    ffi_safe!(
        {
            if !session.is_null() {
                let session_ref = &*session;
                session_ref.accepted_leased_turns.signal_current();
                // SAFETY: the core pointer is required to outlive every agent session.
                unsafe { session_ref.core.as_ref() }
                    .mark_conversation_session_inactive(&session_ref.session_id);
                let runtime = Arc::clone(&session_ref.runtime);
                let session_id = session_ref.session_id.clone();
                let isolation = Arc::clone(&session_ref.isolation);
                let _ = isolation.block_on(async move { runtime.cancel(&session_id).await });
                drop(Box::from_raw(session));
            }
        },
        ()
    )
}

pub(crate) unsafe fn maho_agent_send_message_impl(
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
    callback_release_policy: Option<CallbackReleasePolicy>,
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

            if msg_str.starts_with('/') {
                let cmd = msg_str.split_whitespace().next().unwrap_or("");
                // SAFETY: see `apply_agent_skill_system_prompt`; the same session-owned
                // core pointer is used synchronously before the async turn is spawned.
                let core = unsafe { session.core.as_ref() };
                runtime.set_allowed_tools(core.resolve_agent_slash_allowed_tools(cmd));
            } else {
                runtime.set_allowed_tools(None);
            }
            let turn_callback_lease = callback_release_policy
                .map(|policy| Arc::new(TurnCallbackLease::new(policy.callback, policy.user_data)));
            let session_callback_lease = clone_callback_lease(&session.callback_lease);

            let unified_reg = session
                .unified_event_registration
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .clone();

            let event_seq_counter = Arc::new(std::sync::atomic::AtomicU64::new(1));
            let default_run_id = session_id.clone();

            let on_token_cb = if on_token.is_some() || unified_reg.is_some() {
                let on_token_sendable = SendableCallback(on_token);
                let user_data_val = SendableUserData(user_data as usize);
                let turn_callback_lease = clone_callback_lease(&turn_callback_lease);
                let session_callback_lease = clone_callback_lease(&session_callback_lease);
                let unified_reg = unified_reg.clone();
                let runtime_clone = Arc::clone(&runtime);
                let session_id_clone = session_id.clone();
                let event_seq_counter = Arc::clone(&event_seq_counter);
                let default_run_id = default_run_id.clone();

                let callback: Box<dyn Fn(&str) + Send + Sync> = Box::new(move |token| {
                    let _ = (&turn_callback_lease, &session_callback_lease);
                    if let Some(cb) = on_token_sendable.0 {
                        let udata = user_data_val.0 as *mut std::ffi::c_void;
                        if let Ok(token_c) = CString::new(token) {
                            unsafe {
                                cb(udata, token_c.as_ptr());
                            }
                        }
                    }
                    if unified_reg.is_some() {
                        let run_id = runtime_clone
                            .latest_run_id(&session_id_clone)
                            .map(|r| r.0)
                            .unwrap_or_else(|| default_run_id.clone());
                        let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                        let payload = serde_json::json!({ "token": token }).to_string();
                        dispatch_unified_event(
                            &unified_reg,
                            &turn_callback_lease,
                            &session_callback_lease,
                            &run_id,
                            seq,
                            MahoAgentEventKindV2::Token,
                            &payload,
                        );
                    }
                });
                Some(callback)
            } else {
                None
            };

            let on_event_cb = if on_tool_call.is_some()
                || on_tool_result.is_some()
                || on_thinking.is_some()
                || session.artifact_created_cb.is_some()
                || unified_reg.is_some()
            {
                let on_tool_call_sendable = SendableCallback(on_tool_call);
                let on_tool_result_sendable = SendableCallback(on_tool_result);
                let on_thinking_sendable = SendableCallback(on_thinking);
                let artifact_cb_sendable = SendableCallback(session.artifact_created_cb);
                let artifact_udata = SendableUserData(session.artifact_created_user_data);
                let user_data_val = SendableUserData(user_data as usize);
                let turn_callback_lease = clone_callback_lease(&turn_callback_lease);
                let session_callback_lease = clone_callback_lease(&session_callback_lease);
                let unified_reg = unified_reg.clone();
                let runtime_clone = Arc::clone(&runtime);
                let session_id_clone = session_id.clone();
                let event_seq_counter = Arc::clone(&event_seq_counter);
                let default_run_id = default_run_id.clone();

                let callback = move |event: maho_agent::AgentStreamEvent| {
                    let _ = (&turn_callback_lease, &session_callback_lease);
                    let udata = user_data_val.0 as *mut std::ffi::c_void;
                    let run_id = runtime_clone
                        .latest_run_id(&session_id_clone)
                        .map(|r| r.0)
                        .unwrap_or_else(|| default_run_id.clone());

                    match event {
                        maho_agent::AgentStreamEvent::Thinking(thinking) => {
                            if let Some(cb) = on_thinking_sendable.0 {
                                if let Ok(thinking_c) = CString::new(thinking.as_str()) {
                                    unsafe {
                                        cb(udata, thinking_c.as_ptr());
                                    }
                                }
                            }
                            if unified_reg.is_some() {
                                let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                                let payload =
                                    serde_json::json!({ "thinking": thinking }).to_string();
                                dispatch_unified_event(
                                    &unified_reg,
                                    &turn_callback_lease,
                                    &session_callback_lease,
                                    &run_id,
                                    seq,
                                    MahoAgentEventKindV2::Thinking,
                                    &payload,
                                );
                            }
                        }
                        maho_agent::AgentStreamEvent::ToolCall { id, name, args } => {
                            if let Some(cb) = on_tool_call_sendable.0 {
                                if let (Ok(id_c), Ok(name_c), Ok(args_c)) = (
                                    CString::new(id.as_str()),
                                    CString::new(name.as_str()),
                                    CString::new(args.as_str()),
                                ) {
                                    unsafe {
                                        cb(udata, id_c.as_ptr(), name_c.as_ptr(), args_c.as_ptr());
                                    }
                                }
                            }
                            if unified_reg.is_some() {
                                if name == "ask_user_question"
                                    || name == "request_action_confirmation"
                                {
                                    let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                                    let payload = serde_json::json!({
                                        "request_id": id,
                                        "kind": if name == "ask_user_question" { "question" } else { "confirmation" },
                                        "args": args,
                                    }).to_string();
                                    dispatch_unified_event(
                                        &unified_reg,
                                        &turn_callback_lease,
                                        &session_callback_lease,
                                        &run_id,
                                        seq,
                                        MahoAgentEventKindV2::InteractionRequest,
                                        &payload,
                                    );
                                }
                                let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                                let payload = serde_json::json!({
                                    "id": id,
                                    "name": name,
                                    "args": args,
                                })
                                .to_string();
                                dispatch_unified_event(
                                    &unified_reg,
                                    &turn_callback_lease,
                                    &session_callback_lease,
                                    &run_id,
                                    seq,
                                    MahoAgentEventKindV2::ToolCall,
                                    &payload,
                                );
                            }
                        }
                        maho_agent::AgentStreamEvent::ToolResult {
                            id,
                            name,
                            result,
                            succeeded,
                        } => {
                            if let Some(cb) = on_tool_result_sendable.0 {
                                if let (Ok(id_c), Ok(name_c), Ok(result_c)) = (
                                    CString::new(id.as_str()),
                                    CString::new(name.as_str()),
                                    CString::new(result.as_str()),
                                ) {
                                    unsafe {
                                        cb(
                                            udata,
                                            id_c.as_ptr(),
                                            name_c.as_ptr(),
                                            result_c.as_ptr(),
                                            succeeded,
                                        );
                                    }
                                }
                            }
                            if unified_reg.is_some() {
                                let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                                let payload = serde_json::json!({
                                    "id": id,
                                    "name": name,
                                    "result": result,
                                    "succeeded": succeeded,
                                })
                                .to_string();
                                dispatch_unified_event(
                                    &unified_reg,
                                    &turn_callback_lease,
                                    &session_callback_lease,
                                    &run_id,
                                    seq,
                                    MahoAgentEventKindV2::ToolResult,
                                    &payload,
                                );
                            }
                        }
                        maho_agent::AgentStreamEvent::ArtifactCreated { artifact } => {
                            if let Some(cb) = artifact_cb_sendable.0 {
                                let audata = artifact_udata.0 as *mut std::ffi::c_void;
                                if let Ok(json) = serde_json::to_string(&artifact) {
                                    if let Ok(json_c) = CString::new(json) {
                                        unsafe {
                                            cb(audata, json_c.as_ptr());
                                        }
                                    }
                                }
                            }
                            if unified_reg.is_some() {
                                let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                                let payload = serde_json::to_string(&artifact).unwrap_or_default();
                                dispatch_unified_event(
                                    &unified_reg,
                                    &turn_callback_lease,
                                    &session_callback_lease,
                                    &run_id,
                                    seq,
                                    MahoAgentEventKindV2::ArtifactCreated,
                                    &payload,
                                );
                            }
                        }
                        maho_agent::AgentStreamEvent::Status {
                            elapsed_secs,
                            message,
                        } => {
                            if unified_reg.is_some() {
                                let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                                let payload = serde_json::json!({
                                    "elapsed_secs": elapsed_secs,
                                    "message": message,
                                })
                                .to_string();
                                dispatch_unified_event(
                                    &unified_reg,
                                    &turn_callback_lease,
                                    &session_callback_lease,
                                    &run_id,
                                    seq,
                                    MahoAgentEventKindV2::Status,
                                    &payload,
                                );
                            }
                        }
                        maho_agent::AgentStreamEvent::ProofOrReason { .. } => {}
                        maho_agent::AgentStreamEvent::Token(_) => {}
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

            if let Some(callback_lease) = turn_callback_lease.as_ref() {
                callback_lease.arm();
            }

            let active_guard = ActiveConversationGuard::new(
                Arc::clone(&session.active_conversation_registry),
                session_id.clone(),
            );

            let fut = async move {
                let _active_guard = active_guard;
                let user_chat_msg = maho_types::chat::ChatMessage::user(
                    maho_types::chat::ChatContent::text(msg_str),
                );
                let result = runtime
                    .run_turn(&session_id, user_chat_msg, on_token_cb, on_event_cb)
                    .await;
                let run_id = runtime
                    .latest_run_id(&session_id)
                    .map(|r| r.0)
                    .unwrap_or_else(|| default_run_id.clone());

                invoke_with_turn_callback_lease(turn_callback_lease.clone(), || match result {
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
                        if unified_reg.is_some() {
                            let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                            let payload = serde_json::json!({
                                "full_text": text,
                                "tool_calls": "[]",
                            })
                            .to_string();
                            dispatch_unified_event(
                                &unified_reg,
                                &turn_callback_lease,
                                &session_callback_lease,
                                &run_id,
                                seq,
                                MahoAgentEventKindV2::Completed,
                                &payload,
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
                        if unified_reg.is_some() {
                            let seq = event_seq_counter.fetch_add(1, Ordering::SeqCst);
                            let payload = serde_json::json!({
                                "error": error_text,
                            })
                            .to_string();
                            dispatch_unified_event(
                                &unified_reg,
                                &turn_callback_lease,
                                &session_callback_lease,
                                &run_id,
                                seq,
                                MahoAgentEventKindV2::Error,
                                &payload,
                            );
                        }
                    }
                });
                drop(on_complete);
                drop(on_error);
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

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
pub unsafe extern "C" fn maho_agent_cancel(session: *mut MahoAgentSession) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let session = &*session;
            let accepted_leased_turn = session.accepted_leased_turns.signal_current();
            let runtime = Arc::clone(&session.runtime);
            let session_id = session.session_id.clone();

            let isolation = Arc::clone(&session.isolation);
            let res = isolation.block_on(async move { runtime.cancel(&session_id).await });
            accepted_leased_turn || res.is_ok()
        },
        false
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `policy` must be a null-terminated C string (e.g. "prompt", "allow", "deny").
pub unsafe extern "C" fn maho_agent_set_approval_policy(
    session: *mut MahoAgentSession,
    policy: *const c_char,
) {
    ffi_safe!(
        {
            if session.is_null() || policy.is_null() {
                return;
            }
            let session = &*session;
            if let Ok(p) = CStr::from_ptr(policy).to_str() {
                session.runtime.set_approval_policy(p.to_string());
            }
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `permission_tier` must be null or a null-terminated UTF-8 C string
/// ("read_only", "guard", "full_access"); unknown values fail closed to
/// "guard". Plumbing only: no enforcement behavior changes in this row.
pub unsafe extern "C" fn maho_agent_set_runtime_config(
    session: *mut MahoAgentSession,
    permission_tier: *const c_char,
    final_confirm: bool,
    proactive_mode: bool,
) {
    ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            let session = &*session;
            let raw_tier = if permission_tier.is_null() {
                ""
            } else {
                CStr::from_ptr(permission_tier).to_str().unwrap_or_default()
            };
            let persisted = {
                let mut config = session
                    .runtime_config
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                config.permission_tier = AgentRuntimeConfig::normalize_tier(raw_tier);
                config.final_confirm = final_confirm;
                config.proactive_mode = proactive_mode;
                config.clone()
            };
            // Wave 1A write-through: persist the normalized triple keyed by
            // the FFI session id so a later session open restores it.
            // SAFETY: `session.core` points to the live `MahoCore` that owns
            // the session (see the create-path SAFETY notes).
            let core_ref = unsafe { session.core.as_ref() };
            let db_path = core_ref.sqlite_db_path();
            persist_runtime_config_to_store(db_path, &session.session_id, &persisted);
            // Wave 1D panel-side tier binding (D9 live rebind): mirror the
            // CLI's kernel-gate wiring (maho-cli main.rs) so a mid-session
            // tier change takes effect on the next kernel tool call without
            // reopening the session. Roots come from the shared provisioning
            // source (plan R-N2) — never an ad-hoc list. Kept in lockstep
            // with the lib.rs `#[no_mangle]` copy (single semantics, two
            // symbols — see the F3-R4 duplication-hazard note).
            session
                .runtime
                .set_runtime_tier(Some(persisted.permission_tier.clone()));
            session.runtime.set_fs_whitelist_roots(
                maho_agent::permission::default_fs_whitelist_roots(&session.workspace_root),
            );
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
pub unsafe extern "C" fn maho_agent_set_mail_authorization_state(
    session: *mut MahoAgentSession,
    feature_enabled: bool,
    helper_ready: bool,
    helper_starting: bool,
    read_allowed: bool,
) {
    ffi_safe!(
        {
            if session.is_null() {
                return;
            }
            let session = &*session;
            session
                .runtime
                .set_mail_authorization_state(maho_agent::MailAuthorizationState {
                    feature_enabled,
                    helper_ready,
                    helper_starting,
                    read_allowed,
                });
        },
        ()
    )
}

/// # Safety
/// `session` must be a valid pointer returned by a `maho_agent_create_session*`

/// # Safety
/// `session` must be a valid pointer to a `MahoAgentSession`.
/// `provider` may be null to clear the explicit provider selection; otherwise it
/// must be a null-terminated UTF-8 string naming an allowlisted provider.
pub unsafe extern "C" fn maho_agent_session_set_preferred_provider(
    session: *mut MahoAgentSession,
    provider: *const c_char,
) -> bool {
    ffi_safe!(
        {
            if session.is_null() {
                return false;
            }
            let provider = if provider.is_null() {
                None
            } else {
                match CStr::from_ptr(provider).to_str() {
                    Ok(s) => Some(s.to_owned()),
                    Err(_) => return false,
                }
            };
            let session = &*session;
            session.runtime.set_preferred_provider(provider);
            true
        },
        false
    )
}

#[cfg(test)]
mod runtime_config_tests {
    use super::*;

    #[test]
    fn test_runtime_config_default_triple_is_guard_true_false() {
        // The plumbing default must mirror today's effective behavior:
        // permission_tier="guard", final_confirm=true, proactive_mode=false.
        let cfg = AgentRuntimeConfig::default();
        assert_eq!(cfg.permission_tier, "guard");
        assert!(cfg.final_confirm);
        assert!(!cfg.proactive_mode);

        // Unknown / empty tiers fail safe back to the guard default.
        assert_eq!(AgentRuntimeConfig::normalize_tier("bogus"), "guard");
        assert_eq!(AgentRuntimeConfig::normalize_tier(""), "guard");
        assert_eq!(AgentRuntimeConfig::normalize_tier("read_only"), "read_only");
        assert_eq!(AgentRuntimeConfig::normalize_tier("guard"), "guard");
        assert_eq!(
            AgentRuntimeConfig::normalize_tier("full_access"),
            "full_access"
        );
    }

    #[test]
    fn test_maho_agent_set_runtime_config_roundtrip() {
        let mut session = MahoAgentSession::create_for_test("runtime-config-session");
        let session_ptr: *mut MahoAgentSession = session.as_mut();

        // Defaults are present before any setter call.
        {
            let cfg = session
                .runtime_config
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            assert_eq!(cfg.permission_tier, "guard");
            assert!(cfg.final_confirm);
            assert!(!cfg.proactive_mode);
        }

        // set -> get roundtrip through the FFI symbol.
        let tier = CString::new("read_only").unwrap();
        unsafe {
            maho_agent_set_runtime_config(session_ptr, tier.as_ptr(), false, true);
        }

        let cfg = session
            .runtime_config
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        assert_eq!(cfg.permission_tier, "read_only");
        assert!(!cfg.final_confirm);
        assert!(cfg.proactive_mode);

        // Wave 1D panel-side tier binding: the setter must rebind the
        // session backend's kernel dispatch gate (D9 live rebind) and
        // provision the R-N2 single-source whitelist roots, mirroring the
        // CLI wiring (maho-cli main.rs) so cross-surface verdicts match
        // (SC8).
        assert_eq!(
            session.runtime.runtime_tier(),
            Some("read_only".to_string())
        );
        assert_eq!(
            session.runtime.fs_whitelist_roots(),
            maho_agent::permission::default_fs_whitelist_roots(&session.workspace_root)
        );
    }

    #[test]
    fn test_maho_agent_set_runtime_config_unknown_tier_falls_back_to_guard() {
        let mut session = MahoAgentSession::create_for_test("runtime-config-unknown-tier");
        let session_ptr: *mut MahoAgentSession = session.as_mut();

        let tier = CString::new("root").unwrap();
        unsafe {
            maho_agent_set_runtime_config(session_ptr, tier.as_ptr(), true, false);
        }

        let cfg = session
            .runtime_config
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        assert_eq!(cfg.permission_tier, "guard");

        // The binding mirrors the normalized (fail-closed) tier, not the raw
        // input.
        assert_eq!(session.runtime.runtime_tier(), Some("guard".to_string()));
    }

    /// Wave 1D panel-side binding: a session with NO set_runtime_config call
    /// keeps the kernel gate inert (back-compat — today's unconfigured
    /// posture is unchanged), and the session-open binding after a stored
    /// row restores the persisted tier (SC8 seeded-row semantics).
    #[test]
    fn test_unconfigured_session_keeps_kernel_gate_inert() {
        let session = MahoAgentSession::create_for_test("runtime-config-unbound");

        // No setter call, no stored row → the FFI session constructor binds
        // the fail-closed default "guard" from the loaded default config.
        assert_eq!(session.runtime.runtime_tier(), Some("guard".to_string()));
        assert_eq!(
            session.runtime.fs_whitelist_roots(),
            maho_agent::permission::default_fs_whitelist_roots(&session.workspace_root)
        );
    }

    #[test]
    fn test_append_proactive_block_matches_cli_composer_byte_for_byte() {
        use maho_agent::system_prompt::{
            compose_system_prompt_proactive, PROACTIVITY_INSTRUCTION_SENTINEL,
            PROACTIVITY_INSTRUCTION_TEXT,
        };

        // WITH the flag: "\n\n" + the fixed block — the appended segment is
        // byte-identical to the CLI path (maho-cli main.rs
        // agent_task_system_prompt) applied to the same base content (the CLI
        // composer additionally prepends the baseline contract, so the panel
        // prompt must be its tail).
        let mut panel = "skill block".to_string();
        append_proactive_block(&mut panel, true);
        assert!(compose_system_prompt_proactive("skill block", &[], None, true).ends_with(&panel));
        assert!(panel.ends_with(PROACTIVITY_INSTRUCTION_TEXT));
        assert!(panel.contains(PROACTIVITY_INSTRUCTION_SENTINEL));

        // WITHOUT the flag: byte-identical to the pre-D8 composition.
        let mut plain = "skill block".to_string();
        append_proactive_block(&mut plain, false);
        assert_eq!(plain, "skill block");
        assert!(compose_system_prompt_proactive("skill block", &[], None, false).ends_with(&plain));
        assert!(!plain.contains(PROACTIVITY_INSTRUCTION_SENTINEL));
    }

    /// D8 (Wave 3B) deliverable test: the panel prompt carries the proactive
    /// block when the session runtime-config flag is on, and stays free of it
    /// when off. Runs through the same composition path
    /// `apply_agent_skill_system_prompt` uses, with the flag sourced from the
    /// session's runtime_config (set via the FFI setter, as the panel does).
    #[test]
    fn test_panel_prompt_proactive_block_gated_on_runtime_config() {
        use maho_agent::system_prompt::{
            PROACTIVITY_INSTRUCTION_SENTINEL, PROACTIVITY_INSTRUCTION_TEXT,
        };

        // OFF (plumbing default): no proactive block anywhere.
        let session = MahoAgentSession::create_for_test("proactive-prompt-off");
        let flag = session
            .runtime_config
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .proactive_mode;
        assert!(!flag);
        // SAFETY: session-owned core pointer, same contract as the apply path.
        let core = unsafe { session.core.as_ref() };
        let prompt_off = compose_panel_system_prompt(core, &session, flag);
        assert!(!prompt_off.contains(PROACTIVITY_INSTRUCTION_SENTINEL));

        // ON: flag set through the FFI setter (the panel's runtime-config
        // card path), block present, appended after the composed content.
        let mut session_on = MahoAgentSession::create_for_test("proactive-prompt-on");
        let session_ptr: *mut MahoAgentSession = session_on.as_mut();
        let tier = CString::new("guard").unwrap();
        unsafe {
            maho_agent_set_runtime_config(session_ptr, tier.as_ptr(), true, true);
        }
        let flag_on = session_on
            .runtime_config
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .proactive_mode;
        assert!(flag_on);
        // SAFETY: session-owned core pointer, same contract as the apply path.
        let core_on = unsafe { session_on.core.as_ref() };
        let prompt_on = compose_panel_system_prompt(core_on, &session_on, flag_on);
        assert!(prompt_on.contains(PROACTIVITY_INSTRUCTION_SENTINEL));
        assert!(prompt_on.ends_with(PROACTIVITY_INSTRUCTION_TEXT));
        assert!(!prompt_off.is_empty());
        // Same base composition, only the block differs.
        assert_eq!(
            prompt_on
                .trim_end_matches(['\n'])
                .strip_suffix(PROACTIVITY_INSTRUCTION_TEXT)
                .map(str::trim_end)
                .unwrap_or_default(),
            prompt_off.trim_end(),
        );
    }

    #[test]
    fn test_compose_panel_system_prompt_preserves_base_prompt_when_skill_context_is_none() {
        let session = MahoAgentSession::create_for_test("prompt-fallback-test");
        // SAFETY: test session owns a valid MahoCore.
        let core = unsafe { session.core.as_ref() };
        let prompt = compose_panel_system_prompt(core, &session, false);
        assert!(
            !prompt.is_empty(),
            "System prompt must never be empty when skills are absent"
        );
        assert_eq!(prompt, maho_agent::system_prompt::MAHO_BROWSER_SYSTEM_PROMPT);
    }

    #[test]
    fn test_maho_agent_set_runtime_config_null_session_is_safe() {
        let tier = CString::new("guard").unwrap();
        unsafe {
            maho_agent_set_runtime_config(std::ptr::null_mut(), tier.as_ptr(), true, false);
        }
    }

    /// Wave 1A roundtrip: set → persist → reopen → read matches.
    #[test]
    fn test_runtime_config_set_persists_and_reopen_restores() {
        const KEY: &str = "test_key";
        // The key is process-global; hold the test-key lock so parallel
        // key-flipping tests cannot corrupt this test's database opens.
        let _key_lock = crate::common::sqlcipher_test_key_lock();
        maho_storage::sqlite::set_sqlcipher_key(KEY).unwrap();
        let dir = tempfile::tempdir().unwrap();
        let db_path = dir.path().join("reopen.sqlite");
        let db_str = db_path.to_str().unwrap();

        // 1) set: the FFI setter updates the live Mutex AND writes through to
        //    the store row keyed by the FFI session id.
        let mut session = MahoAgentSession::create_for_test_with_db("reopen-sess", Some(db_str));
        {
            let session_ptr: *mut MahoAgentSession = session.as_mut();
            let tier = CString::new("full_access").unwrap();
            unsafe {
                maho_agent_set_runtime_config(session_ptr, tier.as_ptr(), false, true);
            }
            let cfg = session
                .runtime_config
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            assert_eq!(cfg.permission_tier, "full_access");
            assert!(!cfg.final_confirm);
            assert!(cfg.proactive_mode);
        }

        // 2) persist: the row is visible to an independent store handle
        //    (explicit key — no reliance on the process-wide injection here).
        {
            let storage = maho_storage::sqlite::SqliteStorage::open_with_key(db_str, KEY).unwrap();
            let row = storage
                .load_agent_runtime_config("reopen-sess")
                .unwrap()
                .expect("runtime-config row persisted by the FFI setter");
            assert_eq!(row.permission_tier, "full_access");
            assert!(!row.final_confirm);
            assert!(row.proactive_mode);
        }

        // 3) reopen: a fresh session over the same db loads the persisted row
        //    into the runtime_config Mutex.
        let reopened = MahoAgentSession::create_for_test_with_db("reopen-sess", Some(db_str));
        {
            let cfg = reopened
                .runtime_config
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            assert_eq!(cfg.permission_tier, "full_access");
            assert!(!cfg.final_confirm);
            assert!(cfg.proactive_mode);
        }

        // 4) a different session id on the same db still gets the defaults.
        let other = MahoAgentSession::create_for_test_with_db("other-sess", Some(db_str));
        {
            let cfg = other
                .runtime_config
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            assert_eq!(cfg.permission_tier, "guard");
            assert!(cfg.final_confirm);
            assert!(!cfg.proactive_mode);
        }

        // 5) a hand-edited store row fails closed to "guard" on load.
        {
            let storage = maho_storage::sqlite::SqliteStorage::open_with_key(db_str, KEY).unwrap();
            storage
                .save_agent_runtime_config("tampered-sess", "root", true, false)
                .unwrap();
        }
        let tampered = MahoAgentSession::create_for_test_with_db("tampered-sess", Some(db_str));
        {
            let cfg = tampered
                .runtime_config
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            assert_eq!(cfg.permission_tier, "guard");
        }
    }
}
