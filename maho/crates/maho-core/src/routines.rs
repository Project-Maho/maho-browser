//! Maho Routines — hardcoded recipe automation for Max-tier users.
//!
//! This module provides a fixed set of browser-automation recipes that run
//! via the SwiftideBackend agent. Recipes are NOT user-configurable; they
//! are a curated set of high-value automations bundled with Max tier.
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
// Hardcoded Recipes
// ---------------------------------------------------------------------------

static ROUTINES: &[Routine] = &[
    Routine {
        id: "morning_briefing",
        name: "Morning Briefing",
        cron: "0 8 * * *",
        description: "Summarize top 5 most-read tabs from yesterday + headlines from bookmarked news sites.",
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
        let minute_ok = self.minute.value.map_or(true, |v| dt.format("%M").to_string().parse::<u32>().unwrap_or(99) == v);
        let hour_ok = self.hour.value.map_or(true, |v| dt.format("%H").to_string().parse::<u32>().unwrap_or(99) == v);
        let dom_ok = self.day_of_month.value.map_or(true, |v| dt.format("%d").to_string().parse::<u32>().unwrap_or(99) == v);
        let month_ok = self.month.value.map_or(true, |v| dt.format("%m").to_string().parse::<u32>().unwrap_or(99) == v);
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

pub struct RoutineScheduler {
    agent_runner: AgentRunnerFn,
    tier_provider: TierProviderFn,
    #[allow(dead_code)]
    storage_get: StorageGetFn,
    storage_set: StorageSetFn,
    last_runs: Mutex<HashMap<&'static str, i64>>,
}

impl RoutineScheduler {
    pub fn new(
        agent_runner: AgentRunnerFn,
        tier_provider: TierProviderFn,
        storage_get: StorageGetFn,
        storage_set: StorageSetFn,
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
            last_runs: Mutex::new(initial_runs),
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

        let mut last_runs = self.last_runs.lock().await;
        for routine in ROUTINES {
            let schedule = match CronSchedule::parse(routine.cron) {
                Some(s) => s,
                None => {
                    eprintln!("[RoutineScheduler] failed to parse cron for {}: {}", routine.id, routine.cron);
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
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

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
        let storage_get: StorageGetFn = Arc::new(move |key: &str| {
            sg.lock().unwrap().get(key).cloned()
        });
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
        let storage_get: StorageGetFn = Arc::new(move |key: &str| {
            sg.lock().unwrap().get(key).cloned()
        });
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
}
