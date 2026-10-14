#[cfg(test)]
use std::ffi::CString;
#[cfg(test)]
use std::sync::{Arc, Mutex};

#[cfg(test)]
use maho_agent::AgentRuntime;
use maho_core::maho_core::MahoCore;

#[cfg(test)]
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

#[cfg(test)]
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
            artifact_registration: Mutex::new((None, 0)),
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

/// D8 (Wave 3B, panel proactive composition): appends the fixed
/// `<proactivity_instruction>` block to the panel-composed prompt with the
/// exact semantics of the CLI path (`compose_system_prompt_proactive`,
/// maho-cli main.rs `agent_task_system_prompt`): when `proactive_mode` is on,
/// "\n\n" + the block; when off, `prompt` is left byte-identical — the flag
/// off must not change the pre-D8 panel composition.
#[cfg(test)]
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
#[cfg(test)]
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

pub use crate::{
    maho_agent_cancel, maho_agent_create_session, maho_agent_create_session_leased,
    maho_agent_session_free, maho_agent_session_set_preferred_provider,
    maho_agent_set_approval_policy, maho_agent_set_mail_authorization_state,
    maho_agent_set_runtime_config,
};

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
        // agent_task_system_prompt): the CLI composer's proactive output is its
        // own non-proactive output plus exactly the segment the panel appends.
        let cli_base = compose_system_prompt_proactive("skill block", &[], None, false);
        let mut panel = cli_base.clone();
        append_proactive_block(&mut panel, true);
        assert_eq!(
            compose_system_prompt_proactive("skill block", &[], None, true),
            panel
        );
        assert!(panel.ends_with(PROACTIVITY_INSTRUCTION_TEXT));
        assert!(panel.contains(PROACTIVITY_INSTRUCTION_SENTINEL));

        // WITHOUT the flag: byte-identical to the pre-D8 composition.
        let mut plain = "skill block".to_string();
        append_proactive_block(&mut plain, false);
        assert_eq!(plain, "skill block");
        assert!(!cli_base.contains(PROACTIVITY_INSTRUCTION_SENTINEL));
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
        assert_eq!(
            prompt,
            maho_agent::system_prompt::MAHO_BROWSER_SYSTEM_PROMPT
        );
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
