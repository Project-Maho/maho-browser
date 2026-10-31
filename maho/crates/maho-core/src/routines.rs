//! Maho Routines — recipe automation for Max-tier users.
//!
//! This module provides two kinds of browser-automation recipes, both run via
//! the SwiftideBackend agent and gated to Max tier:
//!   - Four hardcoded built-in recipes (see `ROUTINES`), and
//!   - User-configurable recipes persisted in `maho-storage`
//!     (`custom_routines` table, see `maho_storage::CustomRoutine`).
//!
//! Custom recipes are triggered by either a cron `schedule` OR a
//! `RoutineEvent` (e.g. `OnStartup`, `OnManyTabs`). Cron routines fire from the
//! scheduler tick; event routines fire via `fire_event_routines`.
//!
//! ## R-9 Ownership Pattern (SwiftideBackend mutable singleton)
//!
//! This module borrows SwiftideBackend immutably (or via `Arc<Mutex>`) for
//! the duration of a single recipe run. No long-lived mutable handle is
//! held. The caller is responsible for providing a reference to the agent
//! backend that outlives the routine execution. After `run_routine` returns,
//! no reference to the backend is retained by this module.

use std::collections::HashMap;
use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;

use serde::Serialize;
use tokio::sync::Mutex;

use maho_storage::sqlite::SqliteStorage;
use maho_storage::CustomRoutine;

use crate::routine_runs::{RoutineRunRegistry, RoutineRunStatus};

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/// Subscription tier for gating routine execution.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UserTier {
    Free,
    Pro,
    Max,
}

/// A hardcoded routine recipe.
#[derive(Debug, Clone, Serialize)]
pub struct Routine {
    pub id: &'static str,
    pub name: &'static str,
    pub cron: &'static str,
    pub description: &'static str,
}

/// Result of executing a routine.
#[derive(Debug, Clone, Serialize)]
pub struct RoutineResult {
    pub id: String,
    pub ran_at: i64,
    pub content: String,
    pub success: bool,
}

/// Errors that can occur when running a routine.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RoutineError {
    /// User is not on Max tier.
    TierLockedToMax,
    /// No routine found with the given ID.
    NotFound(String),
    /// The agent backend returned an error.
    ExecutionFailed(String),
}

impl std::fmt::Display for RoutineError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::TierLockedToMax => write!(f, "Routines are available on Max tier only"),
            Self::NotFound(id) => write!(f, "No routine found with id: {id}"),
            Self::ExecutionFailed(msg) => write!(f, "Routine execution failed: {msg}"),
        }
    }
}

impl std::error::Error for RoutineError {}

// ---------------------------------------------------------------------------
// Event triggers (P2.3)
// ---------------------------------------------------------------------------

/// A non-cron event that can trigger a user-defined routine.
///
/// ## Emitter status per variant
///
/// - `OnStartup`, `OnManyTabs`: browser-side emitters exist (startup hook and
///   the tab-count path via `maho_routines_fire_event`).
/// - `OnNotification`, `OnInboxHeartbeat`: parsing/matching/firing are in
///   place and reachable via `maho_routines_fire_event`, but no browser
///   notification adapter or mail heartbeat emits them yet. A routine
///   configured with one of these is stored and fires only once an emitter is
///   added. Deferred wiring — not launch-claimed until an emitter lands.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum RoutineEvent {
    /// Fired once when the browser finishes starting up.
    OnStartup,
    /// Fired when the open-tab count reaches/exceeds a threshold.
    OnManyTabs { threshold: u32 },
    /// Notification/push equivalent. `channel` scopes the routine to a
    /// notification source; an empty configured channel matches any channel.
    /// (Deferred wiring: no browser notification emitter yet.)
    OnNotification { channel: String },
    /// Inbox/mail heartbeat equivalent. `min_unread` is the minimum number of
    /// new/unread items that must be observed for a configured routine to fire.
    /// (Deferred wiring: no mail heartbeat emitter yet.)
    OnInboxHeartbeat { min_unread: u32 },
}

impl RoutineEvent {
    /// Parse the compact form stored in `custom_routines.event_trigger`.
    /// Forms: `on_startup`, `on_many_tabs:<n>`, `on_notification:<channel>`,
    /// `on_inbox_heartbeat:<n>`.
    pub fn parse(s: &str) -> Option<Self> {
        let s = s.trim();
        if s == "on_startup" {
            return Some(Self::OnStartup);
        }
        if let Some(rest) = s.strip_prefix("on_many_tabs:") {
            return rest
                .trim()
                .parse::<u32>()
                .ok()
                .map(|threshold| Self::OnManyTabs { threshold });
        }
        if let Some(rest) = s.strip_prefix("on_notification:") {
            return Some(Self::OnNotification {
                channel: rest.trim().to_string(),
            });
        }
        if let Some(rest) = s.strip_prefix("on_inbox_heartbeat:") {
            return rest
                .trim()
                .parse::<u32>()
                .ok()
                .map(|min_unread| Self::OnInboxHeartbeat { min_unread });
        }
        None
    }

    /// Serialize to the compact form stored in the database.
    pub fn to_trigger_string(&self) -> String {
        match self {
            Self::OnStartup => "on_startup".to_string(),
            Self::OnManyTabs { threshold } => format!("on_many_tabs:{threshold}"),
            Self::OnNotification { channel } => format!("on_notification:{channel}"),
            Self::OnInboxHeartbeat { min_unread } => format!("on_inbox_heartbeat:{min_unread}"),
        }
    }

    /// Whether a routine configured with `self` should fire for `fired`.
    /// For threshold events (`OnManyTabs`, `OnInboxHeartbeat`), the fired
    /// event's actual count must meet the configured threshold. For
    /// `OnNotification`, the channels must match (an empty configured channel is
    /// a wildcard matching any channel).
    pub fn matches(&self, fired: &RoutineEvent) -> bool {
        match (self, fired) {
            (Self::OnStartup, Self::OnStartup) => true,
            (Self::OnManyTabs { threshold }, Self::OnManyTabs { threshold: actual }) => {
                actual >= threshold
            }
            (Self::OnNotification { channel }, Self::OnNotification { channel: actual }) => {
                channel.is_empty() || channel == actual
            }
            (
                Self::OnInboxHeartbeat { min_unread },
                Self::OnInboxHeartbeat { min_unread: actual },
            ) => actual >= min_unread,
            _ => false,
        }
    }
}

/// How a routine run was triggered. Persisted with each result in the routine
/// inbox so the UI/CLI can distinguish scheduled runs from manual/event runs.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RoutineRunSource {
    Scheduled,
    Manual,
    Event,
}

impl RoutineRunSource {
    /// The stable string persisted in `routine_results.source`.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Scheduled => "scheduled",
            Self::Manual => "manual",
            Self::Event => "event",
        }
    }
}

/// How a routine is triggered: on a cron schedule, or by an event.
pub enum RoutineTrigger {
    Cron(CronSchedule),
    Event(RoutineEvent),
}

impl RoutineTrigger {
    /// Resolve a stored routine's trigger from its `schedule` / `event_trigger`
    /// columns. Cron takes precedence when both are present.
    pub fn from_stored(schedule: Option<&str>, trigger: Option<&str>) -> Option<Self> {
        if let Some(cron) = schedule {
            if let Some(parsed) = CronSchedule::parse(cron) {
                return Some(Self::Cron(parsed));
            }
        }
        if let Some(event) = trigger {
            if let Some(parsed) = RoutineEvent::parse(event) {
                return Some(Self::Event(parsed));
            }
        }
        None
    }
}

// ---------------------------------------------------------------------------
// Merged listing view (built-in + user-defined)
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RoutineSource {
    Builtin,
    Custom,
}

/// A unified, owned view over both built-in and user-defined routines, for
/// listing surfaces (FFI/WebUI). Exactly one of `cron`/`trigger` is populated.
#[derive(Debug, Clone, Serialize)]
pub struct RoutineView {
    pub id: String,
    pub name: String,
    pub cron: Option<String>,
    pub trigger: Option<String>,
    pub description: String,
    pub enabled: bool,
    pub source: RoutineSource,
}

// ---------------------------------------------------------------------------
// Hardcoded Recipes
// ---------------------------------------------------------------------------

static ROUTINES: &[Routine] = &[
    Routine {
        id: "morning_briefing",
        name: "Morning Briefing",
        cron: "0 8 * * *",
        description:
            "Summarize top 5 most-read tabs from yesterday + headlines from bookmarked news sites.",
    },
    Routine {
        id: "close_old_tabs",
        name: "Close Old Tabs",
        cron: "0 22 * * 0",
        description: "Suggest closing tabs unopened for 14+ days; user confirms before close.",
    },
    Routine {
        id: "summarize_reading_list",
        name: "Summarize Reading List",
        cron: "0 9 * * 6",
        description: "Generate 1-paragraph summary for each unread bookmark added this week.",
    },
    Routine {
        id: "weekly_tab_tidy",
        name: "Weekly Tab Tidy",
        cron: "0 18 * * 5",
        description: "Apply Tab Tidy across all open tabs (uses request_tidy_tabs in llm_manager).",
    },
];

/// Returns the full list of hardcoded routines.
pub fn list_routines() -> &'static [Routine] {
    ROUTINES
}

/// Execute a routine by ID.
///
/// # Tier Gate
/// Returns `RoutineError::TierLockedToMax` if `user_tier` is not `UserTier::Max`.
///
/// # Agent Interaction
/// The `agent_prompt_runner` closure is called with the constructed prompt string.
/// In production this invokes `SwiftideBackend::run_prompt`; in tests it can be
/// replaced with a mock.
pub async fn run_routine<F, Fut>(
    id: &str,
    user_tier: UserTier,
    agent_prompt_runner: F,
) -> Result<RoutineResult, RoutineError>
where
    F: FnOnce(String) -> Fut,
    Fut: std::future::Future<Output = Result<String, String>>,
{
    // Tier gate
    if user_tier != UserTier::Max {
        return Err(RoutineError::TierLockedToMax);
    }

    // Find routine
    let routine = ROUTINES
        .iter()
        .find(|r| r.id == id)
        .ok_or_else(|| RoutineError::NotFound(id.to_string()))?;

    // Build prompt
    let prompt = build_prompt(routine);

    // Execute via agent
    let content = agent_prompt_runner(prompt)
        .await
        .map_err(RoutineError::ExecutionFailed)?;

    let now = chrono::Utc::now().timestamp();

    Ok(RoutineResult {
        id: routine.id.to_string(),
        ran_at: now,
        content,
        success: true,
    })
}

// ---------------------------------------------------------------------------
// User-defined routines (P2.3): resolution, execution, event dispatch
// ---------------------------------------------------------------------------

/// Merged listing of built-in and user-defined routines.
///
/// Built-ins are always enabled and cron-triggered; custom routines carry their
/// own `enabled` flag and either a `cron`/`schedule` or an event `trigger`.
pub fn list_all_routines(storage: &SqliteStorage) -> Result<Vec<RoutineView>, RoutineError> {
    let mut views: Vec<RoutineView> = ROUTINES
        .iter()
        .map(|r| RoutineView {
            id: r.id.to_string(),
            name: r.name.to_string(),
            cron: Some(r.cron.to_string()),
            trigger: None,
            description: r.description.to_string(),
            enabled: true,
            source: RoutineSource::Builtin,
        })
        .collect();

    let custom = storage
        .list_custom_routines()
        .map_err(|e| RoutineError::ExecutionFailed(e.to_string()))?;
    for c in custom {
        views.push(RoutineView {
            id: c.id,
            name: c.name,
            cron: c.schedule,
            trigger: c.trigger,
            description: c.prompt.chars().take(140).collect(),
            enabled: c.enabled,
            source: RoutineSource::Custom,
        });
    }
    Ok(views)
}

fn build_custom_prompt(routine: &CustomRoutine) -> String {
    format!(
        "You are a browser assistant running the user-defined '{}' routine.\n\
         Instructions provided by the user:\n{}\n\
         Follow the instructions and produce a clear, readable result.",
        routine.name, routine.prompt
    )
}

/// Execute a routine by id, resolving built-ins first and then user-defined
/// recipes from storage. The Max-tier gate applies to both.
pub async fn run_routine_resolved<F, Fut>(
    id: &str,
    user_tier: UserTier,
    storage: &SqliteStorage,
    agent_prompt_runner: F,
) -> Result<RoutineResult, RoutineError>
where
    F: FnOnce(String) -> Fut,
    Fut: std::future::Future<Output = Result<String, String>>,
{
    if user_tier != UserTier::Max {
        return Err(RoutineError::TierLockedToMax);
    }

    if let Some(routine) = ROUTINES.iter().find(|r| r.id == id) {
        let prompt = build_prompt(routine);
        let content = agent_prompt_runner(prompt)
            .await
            .map_err(RoutineError::ExecutionFailed)?;
        return Ok(RoutineResult {
            id: routine.id.to_string(),
            ran_at: chrono::Utc::now().timestamp(),
            content,
            success: true,
        });
    }

    let custom = storage
        .get_custom_routine(id)
        .map_err(|e| RoutineError::ExecutionFailed(e.to_string()))?;
    match custom {
        Some(c) => {
            let prompt = build_custom_prompt(&c);
            let content = agent_prompt_runner(prompt)
                .await
                .map_err(RoutineError::ExecutionFailed)?;
            Ok(RoutineResult {
                id: c.id,
                ran_at: chrono::Utc::now().timestamp(),
                content,
                success: true,
            })
        }
        None => Err(RoutineError::NotFound(id.to_string())),
    }
}

/// Best-effort persistence of a routine result into the routine inbox. A
/// storage failure is logged and swallowed: it must never abort or fail the
/// routine run itself.
fn persist_result(storage: &SqliteStorage, result: &RoutineResult, source: RoutineRunSource) {
    if let Err(e) = storage.record_routine_result(
        &result.id,
        result.ran_at,
        result.success,
        &result.content,
        source.as_str(),
    ) {
        eprintln!("[routines] failed to persist result for {}: {e}", result.id);
    }
}

/// Run a routine by id (built-in or custom) and record the outcome in the
/// durable routine inbox, then return it. Both successful and failed runs are
/// persisted: a failure is stored as a non-`success` record whose `content` is
/// the error text. Tier-locked and not-found errors are returned without a
/// record (there was no run to log).
pub async fn run_routine_and_record<F, Fut>(
    id: &str,
    user_tier: UserTier,
    storage: &SqliteStorage,
    source: RoutineRunSource,
    agent_prompt_runner: F,
) -> Result<RoutineResult, RoutineError>
where
    F: FnOnce(String) -> Fut,
    Fut: std::future::Future<Output = Result<String, String>>,
{
    run_routine_and_record_inner(id, user_tier, storage, source, agent_prompt_runner, None).await
}

/// Execute through the same resolved-and-recorded path while publishing the
/// process-lifetime run status that was already queued by the caller.
pub async fn run_routine_and_record_tracked<F, Fut>(
    id: &str,
    user_tier: UserTier,
    storage: &SqliteStorage,
    source: RoutineRunSource,
    agent_prompt_runner: F,
    registry: &RoutineRunRegistry,
    queued: RoutineRunStatus,
) -> Result<RoutineResult, RoutineError>
where
    F: FnOnce(String) -> Fut,
    Fut: std::future::Future<Output = Result<String, String>>,
{
    run_routine_and_record_inner(
        id,
        user_tier,
        storage,
        source,
        agent_prompt_runner,
        Some((registry, queued)),
    )
    .await
}

async fn run_routine_and_record_inner<F, Fut>(
    id: &str,
    user_tier: UserTier,
    storage: &SqliteStorage,
    source: RoutineRunSource,
    agent_prompt_runner: F,
    tracked: Option<(&RoutineRunRegistry, RoutineRunStatus)>,
) -> Result<RoutineResult, RoutineError>
where
    F: FnOnce(String) -> Fut,
    Fut: std::future::Future<Output = Result<String, String>>,
{
    if let Some((registry, queued)) = tracked.as_ref() {
        if let Err(error) = registry.mark_running(&queued.run_id) {
            eprintln!("[routines] failed to mark run running: {error}");
        }
    }

    let result = run_routine_resolved(id, user_tier, storage, agent_prompt_runner).await;
    match &result {
        Ok(res) => {
            persist_result(storage, res, source);
            if let Some((registry, queued)) = tracked.as_ref() {
                if let Err(error) = registry.mark_succeeded(&queued.run_id, res.content.clone()) {
                    eprintln!("[routines] failed to mark run succeeded: {error}");
                }
            }
        }
        Err(error) => {
            if let RoutineError::ExecutionFailed(msg) = error {
                let failed = RoutineResult {
                    id: id.to_string(),
                    ran_at: chrono::Utc::now().timestamp(),
                    content: msg.clone(),
                    success: false,
                };
                persist_result(storage, &failed, source);
            }
            if let Some((registry, queued)) = tracked.as_ref() {
                if let Err(transition_error) =
                    registry.mark_failed(&queued.run_id, error.to_string())
                {
                    eprintln!("[routines] failed to mark run failed: {transition_error}");
                }
            }
        }
    }
    result
}

/// Fire every enabled, event-triggered user routine whose configured trigger
/// matches `event`. Non-Max tiers are a silent no-op (empty result), matching
/// the scheduler's tier behavior. A per-routine agent failure is recorded as a
/// non-`success` `RoutineResult` rather than aborting the whole batch. Every
/// result (success or failure) is persisted to the routine inbox with
/// `source = event`.
pub async fn fire_event_routines<F, Fut>(
    event: RoutineEvent,
    user_tier: UserTier,
    storage: &SqliteStorage,
    agent_prompt_runner: F,
) -> Result<Vec<RoutineResult>, RoutineError>
where
    F: Fn(String) -> Fut,
    Fut: std::future::Future<Output = Result<String, String>>,
{
    fire_event_routines_inner(
        event,
        user_tier,
        storage,
        |prompt, _run_id| agent_prompt_runner(prompt),
        None,
    )
    .await
}

pub async fn fire_event_routines_tracked<F, Fut>(
    event: RoutineEvent,
    user_tier: UserTier,
    storage: &SqliteStorage,
    agent_prompt_runner: F,
    registry: &RoutineRunRegistry,
) -> Result<Vec<RoutineResult>, RoutineError>
where
    F: Fn(String, String) -> Fut,
    Fut: std::future::Future<Output = Result<String, String>>,
{
    fire_event_routines_inner(
        event,
        user_tier,
        storage,
        agent_prompt_runner,
        Some(registry),
    )
    .await
}

async fn fire_event_routines_inner<F, Fut>(
    event: RoutineEvent,
    user_tier: UserTier,
    storage: &SqliteStorage,
    agent_prompt_runner: F,
    registry: Option<&RoutineRunRegistry>,
) -> Result<Vec<RoutineResult>, RoutineError>
where
    F: Fn(String, String) -> Fut,
    Fut: std::future::Future<Output = Result<String, String>>,
{
    if user_tier != UserTier::Max {
        return Ok(Vec::new());
    }

    let custom = storage
        .list_custom_routines()
        .map_err(|e| RoutineError::ExecutionFailed(e.to_string()))?;

    let mut results = Vec::new();
    for c in custom {
        if !c.enabled {
            continue;
        }
        let configured = match c.trigger.as_deref().and_then(RoutineEvent::parse) {
            Some(ev) => ev,
            None => continue,
        };
        if !configured.matches(&event) {
            continue;
        }

        let queued = registry.map(|runs| runs.begin(&c.id, RoutineRunSource::Event));
        if let (Some(runs), Some(status)) = (registry, queued.as_ref()) {
            if let Err(error) = runs.mark_running(&status.run_id) {
                eprintln!("[routines] failed to mark event run running: {error}");
            }
        }
        let prompt = build_custom_prompt(&c);
        let ran_at = chrono::Utc::now().timestamp();
        let run_id = queued
            .as_ref()
            .map(|status| status.run_id.clone())
            .unwrap_or_default();
        let result = match agent_prompt_runner(prompt, run_id).await {
            Ok(content) => RoutineResult {
                id: c.id,
                ran_at,
                content,
                success: true,
            },
            Err(e) => RoutineResult {
                id: c.id,
                ran_at,
                content: e,
                success: false,
            },
        };
        persist_result(storage, &result, RoutineRunSource::Event);
        if let (Some(runs), Some(status)) = (registry, queued.as_ref()) {
            let transition = if result.success {
                runs.mark_succeeded(&status.run_id, result.content.clone())
            } else {
                runs.mark_failed(&status.run_id, result.content.clone())
            };
            if let Err(error) = transition {
                eprintln!("[routines] failed to finish event run: {error}");
            }
        }
        results.push(result);
    }
    Ok(results)
}

// ---------------------------------------------------------------------------
// Internal: prompt construction per recipe
// ---------------------------------------------------------------------------

fn build_prompt(routine: &Routine) -> String {
    match routine.id {
        "morning_briefing" => {
            format!(
                "You are a browser assistant running the '{}' routine.\n\
                 Task: {}\n\
                 Instructions:\n\
                 1. Look at the user's tab history from yesterday.\n\
                 2. Identify the top 5 most-visited/most-read tabs.\n\
                 3. Check bookmarked news sites for today's headlines.\n\
                 4. Produce a concise morning briefing summarizing key content.\n\
                 Format the output as a readable summary with bullet points.",
                routine.name, routine.description
            )
        }
        "close_old_tabs" => {
            format!(
                "You are a browser assistant running the '{}' routine.\n\
                 Task: {}\n\
                 Instructions:\n\
                 1. Enumerate all open tabs across all spaces.\n\
                 2. Identify tabs that have not been accessed in 14+ days.\n\
                 3. For each stale tab, provide: title, URL, days since last access.\n\
                 4. Present the list and ask the user to confirm which to close.\n\
                 Format as a numbered list with [CLOSE] / [KEEP] suggestions.",
                routine.name, routine.description
            )
        }
        "summarize_reading_list" => {
            format!(
                "You are a browser assistant running the '{}' routine.\n\
                 Task: {}\n\
                 Instructions:\n\
                 1. Look at the user's reading list / bookmarks added in the past 7 days.\n\
                 2. For each unread item, fetch its content.\n\
                 3. Generate a 1-paragraph summary of each item.\n\
                 4. Present all summaries in a readable digest format.\n\
                 Include the original URL with each summary.",
                routine.name, routine.description
            )
        }
        "weekly_tab_tidy" => {
            format!(
                "You are a browser assistant running the '{}' routine.\n\
                 Task: {}\n\
                 Instructions:\n\
                 1. Enumerate all open tabs across all spaces.\n\
                 2. Analyze tab topics and suggest groupings.\n\
                 3. Propose a tidy action: group related tabs, close duplicates, \
                    archive low-priority tabs.\n\
                 4. Present the proposed changes for user confirmation.\n\
                 Use the request_tidy_tabs tool if available.",
                routine.name, routine.description
            )
        }
        _ => {
            format!(
                "You are a browser assistant. Execute routine '{}': {}",
                routine.name, routine.description
            )
        }
    }
}

// ---------------------------------------------------------------------------
// Inline cron parser — supports `*` and integer literals for the 4 recipes.
// Fields: minute hour day-of-month month day-of-week
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq)]
pub struct CronField {
    pub value: Option<u32>, // None = wildcard (*)
}

#[derive(Debug, Clone)]
pub struct CronSchedule {
    pub minute: CronField,
    pub hour: CronField,
    pub day_of_month: CronField,
    pub month: CronField,
    pub day_of_week: CronField,
}

impl CronSchedule {
    pub fn parse(cron_str: &str) -> Option<Self> {
        let parts: Vec<&str> = cron_str.split_whitespace().collect();
        if parts.len() != 5 {
            return None;
        }
        Some(Self {
            minute: Self::parse_field(parts[0])?,
            hour: Self::parse_field(parts[1])?,
            day_of_month: Self::parse_field(parts[2])?,
            month: Self::parse_field(parts[3])?,
            day_of_week: Self::parse_field(parts[4])?,
        })
    }

    fn parse_field(s: &str) -> Option<CronField> {
        if s == "*" {
            Some(CronField { value: None })
        } else {
            s.parse::<u32>().ok().map(|v| CronField { value: Some(v) })
        }
    }

    pub fn matches_timestamp(&self, ts: i64) -> bool {
        let dt = match chrono::DateTime::from_timestamp(ts, 0) {
            Some(d) => d,
            None => return false,
        };
        let minute_ok = self.minute.value.map_or(true, |v| {
            dt.format("%M").to_string().parse::<u32>().unwrap_or(99) == v
        });
        let hour_ok = self.hour.value.map_or(true, |v| {
            dt.format("%H").to_string().parse::<u32>().unwrap_or(99) == v
        });
        let dom_ok = self.day_of_month.value.map_or(true, |v| {
            dt.format("%d").to_string().parse::<u32>().unwrap_or(99) == v
        });
        let month_ok = self.month.value.map_or(true, |v| {
            dt.format("%m").to_string().parse::<u32>().unwrap_or(99) == v
        });
        let dow_ok = self.day_of_week.value.map_or(true, |v| {
            // chrono: Monday=1..Sunday=7; cron: Sunday=0, Monday=1..Saturday=6
            let chrono_dow = dt.format("%u").to_string().parse::<u32>().unwrap_or(99);
            let cron_dow = if chrono_dow == 7 { 0 } else { chrono_dow };
            cron_dow == v
        });
        minute_ok && hour_ok && dom_ok && month_ok && dow_ok
    }

    pub fn next_fire_after(&self, after_ts: i64) -> i64 {
        // Walk forward minute-by-minute from (after_ts + 60), capped at 8 days ahead
        let start = after_ts - (after_ts % 60) + 60;
        let cap = start + 8 * 24 * 3600;
        let mut t = start;
        while t < cap {
            if self.matches_timestamp(t) {
                return t;
            }
            t += 60;
        }
        cap
    }
}

// ---------------------------------------------------------------------------
// RoutineScheduler
// ---------------------------------------------------------------------------

pub type AgentRunnerFn = Arc<
    dyn Fn(String) -> Pin<Box<dyn Future<Output = Result<String, String>> + Send>> + Send + Sync,
>;

pub type TierProviderFn = Arc<dyn Fn() -> UserTier + Send + Sync>;

pub type StorageGetFn = Arc<dyn Fn(&str) -> Option<String> + Send + Sync>;
pub type StorageSetFn = Arc<dyn Fn(&str, &str) + Send + Sync>;

/// Supplies the current set of user-defined routines each tick (typically a
/// closure over `SqliteStorage::list_custom_routines`).
pub type CustomRoutinesProviderFn = Arc<dyn Fn() -> Vec<CustomRoutine> + Send + Sync>;

pub struct RoutineScheduler {
    agent_runner: AgentRunnerFn,
    tier_provider: TierProviderFn,
    storage_get: StorageGetFn,
    storage_set: StorageSetFn,
    custom_provider: CustomRoutinesProviderFn,
    last_runs: Mutex<HashMap<&'static str, i64>>,
    custom_last_runs: Mutex<HashMap<String, i64>>,
}

impl RoutineScheduler {
    pub fn new(
        agent_runner: AgentRunnerFn,
        tier_provider: TierProviderFn,
        storage_get: StorageGetFn,
        storage_set: StorageSetFn,
    ) -> Self {
        Self::new_with_custom(
            agent_runner,
            tier_provider,
            storage_get,
            storage_set,
            Arc::new(Vec::new),
        )
    }

    /// Like `new`, but also schedules cron-triggered user-defined routines
    /// supplied by `custom_provider`. Event-triggered custom routines are not
    /// fired here — use `fire_event_routines`.
    pub fn new_with_custom(
        agent_runner: AgentRunnerFn,
        tier_provider: TierProviderFn,
        storage_get: StorageGetFn,
        storage_set: StorageSetFn,
        custom_provider: CustomRoutinesProviderFn,
    ) -> Self {
        let mut initial_runs = HashMap::new();
        for routine in ROUTINES {
            let key = format!("routines:last_run:{}", routine.id);
            if let Some(val) = storage_get(&key) {
                if let Ok(ts) = val.parse::<i64>() {
                    initial_runs.insert(routine.id, ts);
                }
            }
        }
        Self {
            agent_runner,
            tier_provider,
            storage_get,
            storage_set,
            custom_provider,
            last_runs: Mutex::new(initial_runs),
            custom_last_runs: Mutex::new(HashMap::new()),
        }
    }

    pub async fn run_loop(&self) -> ! {
        let mut interval = tokio::time::interval(std::time::Duration::from_secs(60));
        loop {
            interval.tick().await;
            let now = chrono::Utc::now().timestamp();
            self.tick(now).await;
        }
    }

    pub async fn tick(&self, now: i64) {
        let tier = (self.tier_provider)();
        if tier != UserTier::Max {
            return;
        }

        {
            let mut last_runs = self.last_runs.lock().await;
            for routine in ROUTINES {
                let schedule = match CronSchedule::parse(routine.cron) {
                    Some(s) => s,
                    None => {
                        eprintln!(
                            "[RoutineScheduler] failed to parse cron for {}: {}",
                            routine.id, routine.cron
                        );
                        continue;
                    }
                };

                let last_run = last_runs.get(routine.id).copied().unwrap_or(0);
                let next_fire = schedule.next_fire_after(last_run);

                if now >= next_fire {
                    let prompt = build_prompt(routine);
                    let runner = self.agent_runner.clone();
                    let result = runner(prompt).await;

                    last_runs.insert(routine.id, now);
                    let key = format!("routines:last_run:{}", routine.id);
                    (self.storage_set)(&key, &now.to_string());

                    if let Err(e) = result {
                        eprintln!("[RoutineScheduler] routine {} failed: {}", routine.id, e);
                    }
                }
            }
        }

        self.tick_custom_cron(now).await;
    }

    async fn tick_custom_cron(&self, now: i64) {
        let customs = (self.custom_provider)();
        if customs.is_empty() {
            return;
        }

        let mut custom_last_runs = self.custom_last_runs.lock().await;
        for c in customs {
            if !c.enabled {
                continue;
            }
            let cron = match c.schedule.as_deref() {
                Some(s) => s,
                None => continue,
            };
            let schedule = match CronSchedule::parse(cron) {
                Some(s) => s,
                None => {
                    eprintln!(
                        "[RoutineScheduler] failed to parse cron for custom {}: {}",
                        c.id, cron
                    );
                    continue;
                }
            };

            let key = format!("routines:last_run:{}", c.id);
            let last_run = custom_last_runs
                .get(&c.id)
                .copied()
                .or_else(|| (self.storage_get)(&key).and_then(|v| v.parse::<i64>().ok()))
                .unwrap_or(0);
            let next_fire = schedule.next_fire_after(last_run);

            if now >= next_fire {
                let prompt = build_custom_prompt(&c);
                let runner = self.agent_runner.clone();
                let result = runner(prompt).await;

                custom_last_runs.insert(c.id.clone(), now);
                (self.storage_set)(&key, &now.to_string());

                if let Err(e) = result {
                    eprintln!("[RoutineScheduler] custom routine {} failed: {}", c.id, e);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn test_init() {
        crate::install_test_sqlcipher_key();
    }

    #[test]
    fn test_list_returns_4_routines() {
        let routines = list_routines();
        assert_eq!(routines.len(), 4);
    }

    #[test]
    fn test_routine_ids_are_unique() {
        let routines = list_routines();
        let mut ids: Vec<&str> = routines.iter().map(|r| r.id).collect();
        ids.sort();
        ids.dedup();
        assert_eq!(ids.len(), 4);
    }

    #[tokio::test]
    async fn test_tier_gate_rejects_free() {
        let result = run_routine("morning_briefing", UserTier::Free, |_| async {
            Ok("should not reach".to_string())
        })
        .await;
        assert_eq!(result.unwrap_err(), RoutineError::TierLockedToMax);
    }

    #[tokio::test]
    async fn test_tier_gate_rejects_pro() {
        let result = run_routine("morning_briefing", UserTier::Pro, |_| async {
            Ok("should not reach".to_string())
        })
        .await;
        assert_eq!(result.unwrap_err(), RoutineError::TierLockedToMax);
    }

    #[tokio::test]
    async fn test_unknown_id_returns_not_found() {
        let result = run_routine("nonexistent_routine", UserTier::Max, |_| async {
            Ok("should not reach".to_string())
        })
        .await;
        assert_eq!(
            result.unwrap_err(),
            RoutineError::NotFound("nonexistent_routine".to_string())
        );
    }

    #[tokio::test]
    async fn test_max_tier_runs_successfully() {
        let result = run_routine("morning_briefing", UserTier::Max, |prompt| async move {
            assert!(prompt.contains("Morning Briefing"));
            Ok("Briefing content here".to_string())
        })
        .await;
        let res = result.unwrap();
        assert_eq!(res.id, "morning_briefing");
        assert!(res.success);
        assert_eq!(res.content, "Briefing content here");
        assert!(res.ran_at > 0);
    }

    #[tokio::test]
    async fn test_agent_error_propagates() {
        let result = run_routine("close_old_tabs", UserTier::Max, |_| async {
            Err("agent crashed".to_string())
        })
        .await;
        assert_eq!(
            result.unwrap_err(),
            RoutineError::ExecutionFailed("agent crashed".to_string())
        );
    }

    #[tokio::test]
    async fn test_morning_briefing_uses_bookmark_data() {
        use std::sync::{Arc, Mutex};

        let captured_prompt = Arc::new(Mutex::new(String::new()));
        let captured_clone = Arc::clone(&captured_prompt);

        let result = run_routine("morning_briefing", UserTier::Max, move |prompt| {
            let cap = Arc::clone(&captured_clone);
            async move {
                *cap.lock().unwrap() = prompt;
                Ok("• Top tabs: Rust Docs, GitHub\n• Headlines: AI funding surge".to_string())
            }
        })
        .await;

        let res = result.unwrap();
        assert!(res.success);
        assert_eq!(res.id, "morning_briefing");
        assert!(res.content.contains("Top tabs"));
        assert!(res.content.contains("Headlines"));

        let prompt = captured_prompt.lock().unwrap().clone();
        assert!(prompt.contains("Morning Briefing"));
        assert!(prompt.contains("tab history from yesterday"));
        assert!(prompt.contains("bookmarked news sites"));
    }

    #[tokio::test]
    async fn test_close_old_tabs_filters_correctly() {
        use std::sync::{Arc, Mutex};

        let captured_prompt = Arc::new(Mutex::new(String::new()));
        let captured_clone = Arc::clone(&captured_prompt);

        let result = run_routine("close_old_tabs", UserTier::Max, move |prompt| {
            let cap = Arc::clone(&captured_clone);
            async move {
                *cap.lock().unwrap() = prompt;
                Ok("12 tabs identified as stale (>14 days):\n1. [CLOSE] Old Tab".to_string())
            }
        })
        .await;

        let res = result.unwrap();
        assert!(res.success);
        assert_eq!(res.id, "close_old_tabs");

        let prompt = captured_prompt.lock().unwrap().clone();
        assert!(prompt.contains("Close Old Tabs"));
        assert!(prompt.contains("14+ days"));
        assert!(prompt.contains("Enumerate all open tabs"));
        assert!(prompt.contains("[CLOSE] / [KEEP]"));
    }

    #[tokio::test]
    async fn test_weekly_tab_tidy_delegates_to_request_tidy_tabs() {
        use std::sync::{Arc, Mutex};

        let captured_prompt = Arc::new(Mutex::new(String::new()));
        let captured_clone = Arc::clone(&captured_prompt);

        let result = run_routine("weekly_tab_tidy", UserTier::Max, move |prompt| {
            let cap = Arc::clone(&captured_clone);
            async move {
                *cap.lock().unwrap() = prompt;
                Ok(r#"{"folders": [{"name": "Dev", "tab_ids": ["t1","t2"]}]}"#.to_string())
            }
        })
        .await;

        let res = result.unwrap();
        assert!(res.success);
        assert_eq!(res.id, "weekly_tab_tidy");

        let prompt = captured_prompt.lock().unwrap().clone();
        assert!(prompt.contains("Weekly Tab Tidy"));
        assert!(prompt.contains("request_tidy_tabs"));
        assert!(prompt.contains("Enumerate all open tabs"));
        assert!(prompt.contains("group related tabs"));
    }

    #[test]
    fn test_cron_parse_valid() {
        let s = CronSchedule::parse("0 8 * * *").unwrap();
        assert_eq!(s.minute.value, Some(0));
        assert_eq!(s.hour.value, Some(8));
        assert_eq!(s.day_of_month.value, None);
        assert_eq!(s.month.value, None);
        assert_eq!(s.day_of_week.value, None);
    }

    #[test]
    fn test_cron_parse_all_fields() {
        let s = CronSchedule::parse("0 22 * * 0").unwrap();
        assert_eq!(s.minute.value, Some(0));
        assert_eq!(s.hour.value, Some(22));
        assert_eq!(s.day_of_week.value, Some(0));
    }

    #[test]
    fn test_cron_parse_invalid() {
        assert!(CronSchedule::parse("bad").is_none());
        assert!(CronSchedule::parse("0 8 *").is_none());
    }

    #[test]
    fn test_cron_matches_timestamp() {
        // 2026-01-05 08:00:00 UTC is a Monday (dow=1 in cron)
        let ts = chrono::NaiveDate::from_ymd_opt(2026, 1, 5)
            .unwrap()
            .and_hms_opt(8, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp();
        let schedule = CronSchedule::parse("0 8 * * *").unwrap();
        assert!(schedule.matches_timestamp(ts));

        let non_match = CronSchedule::parse("0 9 * * *").unwrap();
        assert!(!non_match.matches_timestamp(ts));
    }

    #[test]
    fn test_cron_next_fire_after() {
        // Start at 2026-01-05 07:30:00 UTC, next "0 8 * * *" should be 08:00 same day
        let start = chrono::NaiveDate::from_ymd_opt(2026, 1, 5)
            .unwrap()
            .and_hms_opt(7, 30, 0)
            .unwrap()
            .and_utc()
            .timestamp();
        let expected = chrono::NaiveDate::from_ymd_opt(2026, 1, 5)
            .unwrap()
            .and_hms_opt(8, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp();
        let schedule = CronSchedule::parse("0 8 * * *").unwrap();
        assert_eq!(schedule.next_fire_after(start), expected);
    }

    #[tokio::test]
    async fn test_scheduler_runs_due_recipe_and_skips_undue() {
        let prompts: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
        let prompts_clone = prompts.clone();

        let agent: AgentRunnerFn = Arc::new(move |prompt: String| {
            let pc = prompts_clone.clone();
            Box::pin(async move {
                pc.lock().await.push(prompt);
                Ok("done".to_string())
            })
        });

        let tier_provider: TierProviderFn = Arc::new(|| UserTier::Max);
        let storage: Arc<std::sync::Mutex<HashMap<String, String>>> =
            Arc::new(std::sync::Mutex::new(HashMap::new()));

        let sg = storage.clone();
        let storage_get: StorageGetFn =
            Arc::new(move |key: &str| sg.lock().unwrap().get(key).cloned());
        let ss = storage.clone();
        let storage_set: StorageSetFn = Arc::new(move |key: &str, val: &str| {
            ss.lock().unwrap().insert(key.to_string(), val.to_string());
        });

        let scheduler = RoutineScheduler::new(agent, tier_provider, storage_get, storage_set);

        // "0 8 * * *" — pick a `now` that is 08:00 on some day
        let now = chrono::NaiveDate::from_ymd_opt(2026, 6, 15)
            .unwrap()
            .and_hms_opt(8, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp();

        // Set all routines' last_run to `now` (so nothing is due)
        {
            let mut lr = scheduler.last_runs.lock().await;
            for r in ROUTINES {
                lr.insert(r.id, now);
            }
            // Then set morning_briefing to far past so it IS due
            lr.insert("morning_briefing", now - 2 * 86400);
        }

        scheduler.tick(now).await;

        let captured = prompts.lock().await;
        assert_eq!(captured.len(), 1);
        assert!(captured[0].contains("Morning Briefing"));
    }

    #[tokio::test]
    async fn test_scheduler_skips_when_tier_not_max() {
        let prompts: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
        let prompts_clone = prompts.clone();

        let agent: AgentRunnerFn = Arc::new(move |prompt: String| {
            let pc = prompts_clone.clone();
            Box::pin(async move {
                pc.lock().await.push(prompt);
                Ok("done".to_string())
            })
        });

        let tier_provider: TierProviderFn = Arc::new(|| UserTier::Pro);
        let storage: Arc<std::sync::Mutex<HashMap<String, String>>> =
            Arc::new(std::sync::Mutex::new(HashMap::new()));

        let sg = storage.clone();
        let storage_get: StorageGetFn =
            Arc::new(move |key: &str| sg.lock().unwrap().get(key).cloned());
        let ss = storage.clone();
        let storage_set: StorageSetFn = Arc::new(move |key: &str, val: &str| {
            ss.lock().unwrap().insert(key.to_string(), val.to_string());
        });

        let scheduler = RoutineScheduler::new(agent, tier_provider, storage_get, storage_set);

        // Set all last_runs to 0 so everything would be due IF tier were Max
        {
            let mut lr = scheduler.last_runs.lock().await;
            for r in ROUTINES {
                lr.insert(r.id, 0);
            }
        }

        let now = chrono::NaiveDate::from_ymd_opt(2026, 6, 15)
            .unwrap()
            .and_hms_opt(8, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp();

        scheduler.tick(now).await;

        let captured = prompts.lock().await;
        assert_eq!(captured.len(), 0);
    }

    // === User-defined routines (P2.3) ===

    fn make_custom(id: &str, schedule: Option<&str>, trigger: Option<&str>) -> CustomRoutine {
        CustomRoutine {
            id: id.to_string(),
            name: format!("Custom {id}"),
            prompt: "Do the user thing.".to_string(),
            schedule: schedule.map(|s| s.to_string()),
            trigger: trigger.map(|t| t.to_string()),
            enabled: true,
            created_at: "2026-07-13T00:00:00Z".to_string(),
        }
    }

    #[test]
    fn test_routine_event_parse_roundtrip() {
        let ev = RoutineEvent::OnManyTabs { threshold: 20 };
        assert_eq!(ev.to_trigger_string(), "on_many_tabs:20");
        assert_eq!(RoutineEvent::parse("on_many_tabs:20"), Some(ev));
        assert_eq!(
            RoutineEvent::parse("on_startup"),
            Some(RoutineEvent::OnStartup)
        );
        assert_eq!(RoutineEvent::OnStartup.to_trigger_string(), "on_startup");
        assert_eq!(RoutineEvent::parse("bogus"), None);
        assert_eq!(RoutineEvent::parse("on_many_tabs:notanumber"), None);
    }

    #[test]
    fn test_routine_event_matches_threshold() {
        let configured = RoutineEvent::OnManyTabs { threshold: 20 };
        assert!(configured.matches(&RoutineEvent::OnManyTabs { threshold: 25 }));
        assert!(configured.matches(&RoutineEvent::OnManyTabs { threshold: 20 }));
        assert!(!configured.matches(&RoutineEvent::OnManyTabs { threshold: 10 }));
        assert!(!configured.matches(&RoutineEvent::OnStartup));
    }

    #[test]
    fn test_notification_and_heartbeat_parse_roundtrip() {
        let notif = RoutineEvent::OnNotification {
            channel: "mail".to_string(),
        };
        assert_eq!(notif.to_trigger_string(), "on_notification:mail");
        assert_eq!(RoutineEvent::parse("on_notification:mail"), Some(notif));

        let hb = RoutineEvent::OnInboxHeartbeat { min_unread: 3 };
        assert_eq!(hb.to_trigger_string(), "on_inbox_heartbeat:3");
        assert_eq!(RoutineEvent::parse("on_inbox_heartbeat:3"), Some(hb));

        assert_eq!(
            RoutineEvent::parse("on_notification:"),
            Some(RoutineEvent::OnNotification {
                channel: String::new()
            })
        );
        assert_eq!(RoutineEvent::parse("on_inbox_heartbeat:x"), None);
    }

    #[test]
    fn test_notification_matches_channel_and_wildcard() {
        let scoped = RoutineEvent::OnNotification {
            channel: "mail".to_string(),
        };
        assert!(scoped.matches(&RoutineEvent::OnNotification {
            channel: "mail".to_string()
        }));
        assert!(!scoped.matches(&RoutineEvent::OnNotification {
            channel: "calendar".to_string()
        }));

        let wildcard = RoutineEvent::OnNotification {
            channel: String::new(),
        };
        assert!(wildcard.matches(&RoutineEvent::OnNotification {
            channel: "anything".to_string()
        }));
    }

    #[test]
    fn test_heartbeat_matches_threshold() {
        let configured = RoutineEvent::OnInboxHeartbeat { min_unread: 5 };
        assert!(configured.matches(&RoutineEvent::OnInboxHeartbeat { min_unread: 5 }));
        assert!(configured.matches(&RoutineEvent::OnInboxHeartbeat { min_unread: 12 }));
        assert!(!configured.matches(&RoutineEvent::OnInboxHeartbeat { min_unread: 1 }));
        assert!(!configured.matches(&RoutineEvent::OnStartup));
    }

    #[tokio::test]
    async fn test_run_and_record_persists_success() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        let res = run_routine_and_record(
            "morning_briefing",
            UserTier::Max,
            &storage,
            RoutineRunSource::Manual,
            |_| async { Ok("briefing body".to_string()) },
        )
        .await
        .unwrap();
        assert!(res.success);

        let latest = storage
            .latest_routine_result("morning_briefing")
            .unwrap()
            .expect("run must be recorded");
        assert!(latest.success);
        assert_eq!(latest.content, "briefing body");
        assert_eq!(latest.source, "manual");
    }

    #[tokio::test]
    async fn test_run_and_record_persists_failure_with_error() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        let err = run_routine_and_record(
            "close_old_tabs",
            UserTier::Max,
            &storage,
            RoutineRunSource::Scheduled,
            |_| async { Err("agent exploded".to_string()) },
        )
        .await
        .unwrap_err();
        assert_eq!(
            err,
            RoutineError::ExecutionFailed("agent exploded".to_string())
        );

        let latest = storage
            .latest_routine_result("close_old_tabs")
            .unwrap()
            .expect("a failed run must still be recorded");
        assert!(!latest.success, "failed run persists success = false");
        assert_eq!(latest.content, "agent exploded");
        assert_eq!(latest.source, "scheduled");
    }

    #[tokio::test]
    async fn test_run_and_record_tier_locked_writes_nothing() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        let err = run_routine_and_record(
            "morning_briefing",
            UserTier::Pro,
            &storage,
            RoutineRunSource::Manual,
            |_| async { Ok("nope".to_string()) },
        )
        .await
        .unwrap_err();
        assert_eq!(err, RoutineError::TierLockedToMax);
        assert_eq!(
            storage.list_routine_results(10).unwrap().len(),
            0,
            "a tier-locked (never-ran) routine must not be recorded"
        );
    }

    #[tokio::test]
    async fn test_fire_event_persists_each_result() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage
            .create_custom_routine(&make_custom("evt", None, Some("on_inbox_heartbeat:2")))
            .unwrap();

        let fired = fire_event_routines(
            RoutineEvent::OnInboxHeartbeat { min_unread: 5 },
            UserTier::Max,
            &storage,
            |_| async { Ok("digest".to_string()) },
        )
        .await
        .unwrap();
        assert_eq!(fired.len(), 1);

        let recorded = storage.list_routine_results_for("evt", 10).unwrap();
        assert_eq!(recorded.len(), 1);
        assert!(recorded[0].success);
        assert_eq!(recorded[0].content, "digest");
        assert_eq!(recorded[0].source, "event");
    }

    #[test]
    fn test_custom_cron_parses_for_user_schedule() {
        let c = make_custom("c1", Some("30 7 * * 1"), None);
        let parsed = CronSchedule::parse(c.schedule.as_deref().unwrap()).unwrap();
        assert_eq!(parsed.minute.value, Some(30));
        assert_eq!(parsed.hour.value, Some(7));
        assert_eq!(parsed.day_of_week.value, Some(1));
    }

    #[test]
    fn test_list_all_routines_merges_builtin_and_custom() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage
            .create_custom_routine(&make_custom("c1", Some("0 9 * * *"), None))
            .unwrap();
        let all = list_all_routines(&storage).unwrap();
        assert_eq!(all.len(), 5);
        assert_eq!(
            all.iter()
                .filter(|v| matches!(v.source, RoutineSource::Builtin))
                .count(),
            4
        );
        let custom = all.iter().find(|v| v.id == "c1").unwrap();
        assert!(matches!(custom.source, RoutineSource::Custom));
        assert_eq!(custom.cron.as_deref(), Some("0 9 * * *"));
    }

    #[tokio::test]
    async fn test_custom_routine_create_list_run() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage
            .create_custom_routine(&make_custom("c1", Some("0 9 * * *"), None))
            .unwrap();

        let listed = storage.list_custom_routines().unwrap();
        assert_eq!(listed.len(), 1);
        assert_eq!(listed[0].id, "c1");

        let captured = Arc::new(std::sync::Mutex::new(String::new()));
        let cap = captured.clone();
        let result = run_routine_resolved("c1", UserTier::Max, &storage, move |prompt| {
            let cap = cap.clone();
            async move {
                *cap.lock().unwrap() = prompt;
                Ok("custom result".to_string())
            }
        })
        .await
        .unwrap();

        assert_eq!(result.id, "c1");
        assert!(result.success);
        assert_eq!(result.content, "custom result");
        let prompt = captured.lock().unwrap().clone();
        assert!(prompt.contains("Custom c1"));
        assert!(prompt.contains("Do the user thing."));
    }

    #[tokio::test]
    async fn test_run_routine_resolved_builtin_still_works() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        let result = run_routine_resolved(
            "morning_briefing",
            UserTier::Max,
            &storage,
            |prompt| async move {
                assert!(prompt.contains("Morning Briefing"));
                Ok("briefing".to_string())
            },
        )
        .await
        .unwrap();
        assert_eq!(result.id, "morning_briefing");
    }

    #[tokio::test]
    async fn test_custom_routine_tier_gate() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage
            .create_custom_routine(&make_custom("c1", Some("0 9 * * *"), None))
            .unwrap();
        let result = run_routine_resolved("c1", UserTier::Pro, &storage, |_| async {
            Ok("should not reach".to_string())
        })
        .await;
        assert_eq!(result.unwrap_err(), RoutineError::TierLockedToMax);
    }

    #[tokio::test]
    async fn test_custom_routine_unknown_id_not_found() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        let result = run_routine_resolved("ghost", UserTier::Max, &storage, |_| async {
            Ok("nope".to_string())
        })
        .await;
        assert_eq!(
            result.unwrap_err(),
            RoutineError::NotFound("ghost".to_string())
        );
    }

    #[tokio::test]
    async fn test_fire_event_routines_matches_and_gates() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage
            .create_custom_routine(&make_custom("evt", None, Some("on_many_tabs:20")))
            .unwrap();
        // A disabled event routine must never fire.
        let mut disabled = make_custom("evt_off", None, Some("on_many_tabs:5"));
        disabled.enabled = false;
        storage.create_custom_routine(&disabled).unwrap();
        // A cron routine must not fire on events.
        storage
            .create_custom_routine(&make_custom("cron", Some("0 9 * * *"), None))
            .unwrap();

        let count = Arc::new(std::sync::Mutex::new(0u32));
        let count2 = count.clone();
        let runner = move |_prompt: String| {
            let count3 = count2.clone();
            async move {
                *count3.lock().unwrap() += 1;
                Ok::<String, String>("done".to_string())
            }
        };

        // Below threshold: nothing fires.
        let below = fire_event_routines(
            RoutineEvent::OnManyTabs { threshold: 10 },
            UserTier::Max,
            &storage,
            &runner,
        )
        .await
        .unwrap();
        assert_eq!(below.len(), 0);

        // At/above threshold: only the enabled matching routine fires.
        let hit = fire_event_routines(
            RoutineEvent::OnManyTabs { threshold: 25 },
            UserTier::Max,
            &storage,
            &runner,
        )
        .await
        .unwrap();
        assert_eq!(hit.len(), 1);
        assert_eq!(hit[0].id, "evt");
        assert!(hit[0].success);
        assert_eq!(*count.lock().unwrap(), 1);
    }

    #[tokio::test]
    async fn test_fire_event_routines_not_max_is_noop() {
        test_init();
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage
            .create_custom_routine(&make_custom("evt", None, Some("on_startup")))
            .unwrap();
        let results = fire_event_routines(
            RoutineEvent::OnStartup,
            UserTier::Pro,
            &storage,
            |_| async { Ok("x".to_string()) },
        )
        .await
        .unwrap();
        assert!(results.is_empty());
    }

    #[tokio::test]
    async fn test_scheduler_runs_due_custom_cron() {
        let prompts: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
        let prompts_clone = prompts.clone();
        let agent: AgentRunnerFn = Arc::new(move |prompt: String| {
            let pc = prompts_clone.clone();
            Box::pin(async move {
                pc.lock().await.push(prompt);
                Ok("done".to_string())
            })
        });
        let tier_provider: TierProviderFn = Arc::new(|| UserTier::Max);
        let storage: Arc<std::sync::Mutex<HashMap<String, String>>> =
            Arc::new(std::sync::Mutex::new(HashMap::new()));
        let sg = storage.clone();
        let storage_get: StorageGetFn =
            Arc::new(move |key: &str| sg.lock().unwrap().get(key).cloned());
        let ss = storage.clone();
        let storage_set: StorageSetFn = Arc::new(move |key: &str, val: &str| {
            ss.lock().unwrap().insert(key.to_string(), val.to_string());
        });

        // Custom cron routine due every day at 08:00; last_run defaults to 0 → due.
        let custom_provider: CustomRoutinesProviderFn =
            Arc::new(|| vec![make_custom("c_daily", Some("0 8 * * *"), None)]);

        let scheduler = RoutineScheduler::new_with_custom(
            agent,
            tier_provider,
            storage_get,
            storage_set,
            custom_provider,
        );

        // Pin built-ins to `now` so only the custom routine is due.
        let now = chrono::NaiveDate::from_ymd_opt(2026, 6, 15)
            .unwrap()
            .and_hms_opt(8, 0, 0)
            .unwrap()
            .and_utc()
            .timestamp();
        {
            let mut lr = scheduler.last_runs.lock().await;
            for r in ROUTINES {
                lr.insert(r.id, now);
            }
        }

        scheduler.tick(now).await;

        let captured = prompts.lock().await;
        assert_eq!(captured.len(), 1);
        assert!(captured[0].contains("Custom c_daily"));
        // Persisted last-run for the custom routine.
        assert!(storage
            .lock()
            .unwrap()
            .contains_key("routines:last_run:c_daily"));
    }
}
