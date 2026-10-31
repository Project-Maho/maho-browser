//! Process-lifetime routine run state and revisioned status notifications.
//!
//! This registry is intentionally in-memory: terminal routine output remains
//! available for the lifetime of the browser process, while an unclean browser
//! restart loses non-terminal runs. Durable result history remains owned by the
//! existing `routine_results` store.

use std::collections::HashMap;
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex, Weak};
use std::task::{Context, Poll};

use serde::Serialize;
use tokio::sync::oneshot;
use uuid::Uuid;

use crate::routines::RoutineRunSource;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RoutineRunState {
    Queued,
    Running,
    AwaitingApproval,
    Succeeded,
    Failed,
}

impl RoutineRunState {
    pub fn is_terminal(self) -> bool {
        matches!(self, Self::Succeeded | Self::Failed)
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RoutineRunApproval {
    pub approval_id: String,
    pub tool_name: String,
    pub sensitivity: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RoutineRunStatus {
    pub run_id: String,
    pub routine_id: String,
    pub source: RoutineRunSource,
    pub state: RoutineRunState,
    pub revision: u64,
    pub started_at: i64,
    pub updated_at: i64,
    pub finished_at: Option<i64>,
    pub result: Option<String>,
    pub error: Option<String>,
    pub approval: Option<RoutineRunApproval>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RoutineRunTransitionError {
    RunNotFound(String),
    InvalidTransition {
        run_id: String,
        from: RoutineRunState,
        to: RoutineRunState,
    },
}

impl std::fmt::Display for RoutineRunTransitionError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::RunNotFound(run_id) => write!(f, "routine run not found: {run_id}"),
            Self::InvalidTransition { run_id, from, to } => {
                write!(
                    f,
                    "invalid routine run transition for {run_id}: {from:?} -> {to:?}"
                )
            }
        }
    }
}

impl std::error::Error for RoutineRunTransitionError {}

type StatusObserver = Arc<dyn Fn(&RoutineRunStatus) + Send + Sync>;

const MAX_ROUTINE_RUNS: usize = 50;

#[derive(Default)]
struct RegistryState {
    runs: HashMap<String, RoutineRunStatus>,
    pending_approvals: HashMap<(String, String), oneshot::Sender<bool>>,
    observers: HashMap<u64, StatusObserver>,
    next_observer_token: u64,
}

pub struct RoutineApprovalWaiter {
    registry: Weak<RoutineRunRegistry>,
    run_id: String,
    approval_id: String,
    receiver: oneshot::Receiver<bool>,
    completed: bool,
}

impl Future for RoutineApprovalWaiter {
    type Output = bool;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match Pin::new(&mut self.receiver).poll(cx) {
            Poll::Ready(Ok(approved)) => {
                self.completed = true;
                Poll::Ready(approved)
            }
            Poll::Ready(Err(_)) => {
                self.completed = true;
                if let Some(registry) = self.registry.upgrade() {
                    registry.cancel_approval(&self.run_id, &self.approval_id, "Approval cancelled");
                }
                Poll::Ready(false)
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

impl Drop for RoutineApprovalWaiter {
    fn drop(&mut self) {
        if !self.completed {
            if let Some(registry) = self.registry.upgrade() {
                registry.cancel_approval(&self.run_id, &self.approval_id, "Approval timed out");
            }
        }
    }
}

#[derive(Default)]
pub struct RoutineRunRegistry {
    state: Mutex<RegistryState>,
}

impl RoutineRunRegistry {
    pub fn begin(
        &self,
        routine_id: impl Into<String>,
        source: RoutineRunSource,
    ) -> RoutineRunStatus {
        self.begin_at(routine_id, source, chrono::Utc::now().timestamp())
    }

    pub fn begin_at(
        &self,
        routine_id: impl Into<String>,
        source: RoutineRunSource,
        now: i64,
    ) -> RoutineRunStatus {
        let status = RoutineRunStatus {
            run_id: Uuid::new_v4().to_string(),
            routine_id: routine_id.into(),
            source,
            state: RoutineRunState::Queued,
            revision: 1,
            started_at: now,
            updated_at: now,
            finished_at: None,
            result: None,
            error: None,
            approval: None,
        };
        let observers = {
            let mut state = self
                .state
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            state.runs.insert(status.run_id.clone(), status.clone());
            if state.runs.len() > MAX_ROUTINE_RUNS {
                let mut terminal_entries: Vec<(String, i64)> = state
                    .runs
                    .iter()
                    .filter(|(_, v)| v.state.is_terminal())
                    .map(|(k, v)| (k.clone(), v.started_at))
                    .collect();
                terminal_entries.sort_by_key(|(_, t)| *t);
                let remove_count =
                    (state.runs.len() - MAX_ROUTINE_RUNS).min(terminal_entries.len());
                for (key, _) in terminal_entries.into_iter().take(remove_count) {
                    state.runs.remove(&key);
                }
            }
            state.observers.values().cloned().collect::<Vec<_>>()
        };
        Self::notify(observers, &status);
        status
    }

    pub fn mark_running(
        &self,
        run_id: &str,
    ) -> Result<RoutineRunStatus, RoutineRunTransitionError> {
        self.transition(run_id, RoutineRunState::Running, None, None)
    }

    pub fn request_approval(
        self: &Arc<Self>,
        run_id: &str,
        tool_name: String,
        sensitivity: String,
    ) -> Result<RoutineApprovalWaiter, RoutineRunTransitionError> {
        let approval_id = Uuid::new_v4().to_string();
        let (sender, receiver) = oneshot::channel();
        let (status, observers) = {
            let mut state = self
                .state
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let current = state
                .runs
                .get_mut(run_id)
                .ok_or_else(|| RoutineRunTransitionError::RunNotFound(run_id.to_string()))?;
            if current.state != RoutineRunState::Running {
                return Err(RoutineRunTransitionError::InvalidTransition {
                    run_id: run_id.to_string(),
                    from: current.state,
                    to: RoutineRunState::AwaitingApproval,
                });
            }
            current.state = RoutineRunState::AwaitingApproval;
            current.revision += 1;
            current.updated_at = chrono::Utc::now().timestamp();
            current.approval = Some(RoutineRunApproval {
                approval_id: approval_id.clone(),
                tool_name,
                sensitivity,
            });
            let status = current.clone();
            state
                .pending_approvals
                .insert((run_id.to_string(), approval_id.clone()), sender);
            let observers = state.observers.values().cloned().collect::<Vec<_>>();
            (status, observers)
        };
        Self::notify(observers, &status);
        Ok(RoutineApprovalWaiter {
            registry: Arc::downgrade(self),
            run_id: run_id.to_string(),
            approval_id,
            receiver,
            completed: false,
        })
    }

    pub fn respond_to_approval(&self, run_id: &str, approval_id: &str, approved: bool) -> bool {
        let (sender, status, observers) = {
            let mut state = self
                .state
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let key = (run_id.to_string(), approval_id.to_string());
            let Some(sender) = state.pending_approvals.remove(&key) else {
                return false;
            };
            let Some(current) = state.runs.get_mut(run_id) else {
                state.pending_approvals.insert(key, sender);
                return false;
            };
            let matches_current = current.state == RoutineRunState::AwaitingApproval
                && current
                    .approval
                    .as_ref()
                    .is_some_and(|approval| approval.approval_id == approval_id);
            if !matches_current {
                state.pending_approvals.insert(key, sender);
                return false;
            }
            current.state = if approved {
                RoutineRunState::Running
            } else {
                RoutineRunState::Failed
            };
            current.revision += 1;
            let now = chrono::Utc::now().timestamp();
            current.updated_at = now;
            current.finished_at = (!approved).then_some(now);
            current.error = (!approved).then(|| "Routine approval denied".to_string());
            current.approval = None;
            let status = current.clone();
            let observers = state.observers.values().cloned().collect::<Vec<_>>();
            (sender, status, observers)
        };
        Self::notify(observers, &status);
        let _ = sender.send(approved);
        true
    }

    fn cancel_approval(&self, run_id: &str, approval_id: &str, error: &str) -> bool {
        let (status, observers) = {
            let mut state = self
                .state
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let key = (run_id.to_string(), approval_id.to_string());
            if state.pending_approvals.remove(&key).is_none() {
                return false;
            }
            let Some(current) = state.runs.get_mut(run_id) else {
                return false;
            };
            if current.state != RoutineRunState::AwaitingApproval
                || !current
                    .approval
                    .as_ref()
                    .is_some_and(|approval| approval.approval_id == approval_id)
            {
                return false;
            }
            current.state = RoutineRunState::Failed;
            current.revision += 1;
            let now = chrono::Utc::now().timestamp();
            current.updated_at = now;
            current.finished_at = Some(now);
            current.error = Some(error.to_string());
            current.approval = None;
            let status = current.clone();
            let observers = state.observers.values().cloned().collect::<Vec<_>>();
            (status, observers)
        };
        Self::notify(observers, &status);
        true
    }

    pub fn mark_succeeded(
        &self,
        run_id: &str,
        result: String,
    ) -> Result<RoutineRunStatus, RoutineRunTransitionError> {
        self.transition(run_id, RoutineRunState::Succeeded, Some(result), None)
    }

    pub fn mark_failed(
        &self,
        run_id: &str,
        error: String,
    ) -> Result<RoutineRunStatus, RoutineRunTransitionError> {
        self.transition(run_id, RoutineRunState::Failed, None, Some(error))
    }

    /// Terminalize a run that was already published as queued but failed
    /// during worker/runtime setup, before the normal execution path could
    /// mark it running. Observers still see the contracted queued -> running ->
    /// failed lifecycle and strictly increasing revisions.
    pub fn mark_setup_failed(
        &self,
        run_id: &str,
        error: String,
    ) -> Result<RoutineRunStatus, RoutineRunTransitionError> {
        self.mark_running(run_id)?;
        self.mark_failed(run_id, error)
    }

    fn transition(
        &self,
        run_id: &str,
        next: RoutineRunState,
        result: Option<String>,
        error: Option<String>,
    ) -> Result<RoutineRunStatus, RoutineRunTransitionError> {
        let now = chrono::Utc::now().timestamp();
        let (status, observers) = {
            let mut state = self
                .state
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let current = state
                .runs
                .get_mut(run_id)
                .ok_or_else(|| RoutineRunTransitionError::RunNotFound(run_id.to_string()))?;
            let valid = matches!(
                (current.state, next),
                (RoutineRunState::Queued, RoutineRunState::Running)
                    | (RoutineRunState::Running, RoutineRunState::Succeeded)
                    | (RoutineRunState::Running, RoutineRunState::Failed)
            );
            if !valid {
                return Err(RoutineRunTransitionError::InvalidTransition {
                    run_id: run_id.to_string(),
                    from: current.state,
                    to: next,
                });
            }
            current.state = next;
            current.revision += 1;
            current.updated_at = now;
            current.finished_at = next.is_terminal().then_some(now);
            current.result = result;
            current.error = error;
            current.approval = None;
            let status = current.clone();
            let observers = state.observers.values().cloned().collect::<Vec<_>>();
            (status, observers)
        };
        Self::notify(observers, &status);
        Ok(status)
    }

    pub fn snapshot(&self) -> Vec<RoutineRunStatus> {
        let state = self
            .state
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let mut runs = state.runs.values().cloned().collect::<Vec<_>>();
        runs.sort_by(|left, right| {
            right
                .updated_at
                .cmp(&left.updated_at)
                .then_with(|| right.run_id.cmp(&left.run_id))
        });
        runs
    }

    pub fn add_observer(&self, observer: StatusObserver) -> u64 {
        let mut state = self
            .state
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        state.next_observer_token = state.next_observer_token.saturating_add(1).max(1);
        let token = state.next_observer_token;
        state.observers.insert(token, observer);
        token
    }

    pub fn remove_observer(&self, token: u64) {
        let mut state = self
            .state
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        state.observers.remove(&token);
    }

    fn notify(observers: Vec<StatusObserver>, status: &RoutineRunStatus) {
        for observer in observers {
            observer(status);
        }
    }
}

#[cfg(test)]
mod tests {
    use std::collections::HashSet;
    use std::sync::{Arc, Barrier, Mutex};

    use super::*;

    #[test]
    fn lifecycle_revisions_are_strictly_monotonic() {
        let registry = RoutineRunRegistry::default();
        let events = Arc::new(Mutex::new(Vec::new()));
        let captured = Arc::clone(&events);
        registry.add_observer(Arc::new(move |status| {
            captured
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .push((status.state, status.revision));
        }));

        let queued = registry.begin_at("r", RoutineRunSource::Manual, 10);
        let running = registry
            .mark_running(&queued.run_id)
            .expect("queued -> running");
        let succeeded = registry
            .mark_succeeded(&queued.run_id, "done".to_string())
            .expect("running -> succeeded");

        assert_eq!(queued.revision, 1);
        assert_eq!(running.revision, 2);
        assert_eq!(succeeded.revision, 3);
        assert_eq!(succeeded.result.as_deref(), Some("done"));
        assert!(succeeded.finished_at.is_some());
        assert_eq!(
            *events
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner),
            vec![
                (RoutineRunState::Queued, 1),
                (RoutineRunState::Running, 2),
                (RoutineRunState::Succeeded, 3),
            ]
        );
    }

    #[test]
    fn invalid_and_duplicate_terminal_transitions_are_rejected_without_revision_change() {
        let registry = RoutineRunRegistry::default();
        let queued = registry.begin("r", RoutineRunSource::Scheduled);
        let direct_terminal = registry.mark_failed(&queued.run_id, "bad".to_string());
        assert!(matches!(
            direct_terminal,
            Err(RoutineRunTransitionError::InvalidTransition { .. })
        ));

        registry.mark_running(&queued.run_id).expect("running");
        let terminal = registry
            .mark_failed(&queued.run_id, "first".to_string())
            .expect("failed");
        let duplicate = registry.mark_succeeded(&queued.run_id, "late".to_string());
        assert!(matches!(
            duplicate,
            Err(RoutineRunTransitionError::InvalidTransition { .. })
        ));
        let stored = registry
            .snapshot()
            .into_iter()
            .find(|status| status.run_id == queued.run_id)
            .expect("stored run");
        assert_eq!(stored.revision, terminal.revision);
        assert_eq!(stored.state, RoutineRunState::Failed);
        assert_eq!(stored.error.as_deref(), Some("first"));
    }

    #[test]
    fn queued_setup_failure_terminalizes_through_running() {
        let registry = RoutineRunRegistry::default();
        let events = Arc::new(Mutex::new(Vec::new()));
        let captured = Arc::clone(&events);
        registry.add_observer(Arc::new(move |status| {
            captured
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .push((status.state, status.revision));
        }));

        let queued = registry.begin_at("r", RoutineRunSource::Scheduled, 10);
        let failed = registry
            .mark_setup_failed(&queued.run_id, "storage unavailable".to_string())
            .expect("setup failure terminalizes");

        assert_eq!(failed.state, RoutineRunState::Failed);
        assert_eq!(failed.revision, 3);
        assert_eq!(failed.error.as_deref(), Some("storage unavailable"));
        assert!(failed.finished_at.is_some());
        assert_eq!(
            *events
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner),
            vec![
                (RoutineRunState::Queued, 1),
                (RoutineRunState::Running, 2),
                (RoutineRunState::Failed, 3),
            ]
        );
    }

    #[tokio::test]
    async fn sensitive_approval_allow_resumes_with_monotonic_revisions() {
        let registry = Arc::new(RoutineRunRegistry::default());
        let queued = registry.begin("r", RoutineRunSource::Manual);
        registry.mark_running(&queued.run_id).expect("running");
        let waiter = registry
            .request_approval(
                &queued.run_id,
                "browser_type".to_string(),
                "sensitive".to_string(),
            )
            .expect("awaiting approval");
        let awaiting = registry.snapshot().pop().expect("status");
        let approval = awaiting.approval.expect("sanitized approval");
        assert_eq!(awaiting.state, RoutineRunState::AwaitingApproval);
        assert_eq!(awaiting.revision, 3);
        assert_eq!(approval.tool_name, "browser_type");
        assert_eq!(approval.sensitivity, "sensitive");
        assert!(registry.respond_to_approval(&queued.run_id, &approval.approval_id, true));
        assert!(waiter.await);
        let running = registry.snapshot().pop().expect("status");
        assert_eq!(running.state, RoutineRunState::Running);
        assert_eq!(running.revision, 4);
        assert!(running.approval.is_none());
    }

    #[tokio::test]
    async fn denial_terminalizes_and_stale_ids_do_not_change_state() {
        let registry = Arc::new(RoutineRunRegistry::default());
        let queued = registry.begin("r", RoutineRunSource::Manual);
        registry.mark_running(&queued.run_id).expect("running");
        let waiter = registry
            .request_approval(
                &queued.run_id,
                "fs_write".to_string(),
                "sensitive".to_string(),
            )
            .expect("awaiting approval");
        let approval_id = registry.snapshot()[0]
            .approval
            .as_ref()
            .expect("approval")
            .approval_id
            .clone();
        assert!(!registry.respond_to_approval(&queued.run_id, "wrong", true));
        assert_eq!(registry.snapshot()[0].revision, 3);
        assert!(registry.respond_to_approval(&queued.run_id, &approval_id, false));
        assert!(!waiter.await);
        let failed = &registry.snapshot()[0];
        assert_eq!(failed.state, RoutineRunState::Failed);
        assert_eq!(failed.revision, 4);
        assert!(failed.finished_at.is_some());
        assert!(!registry.respond_to_approval(&queued.run_id, &approval_id, true));
        assert_eq!(registry.snapshot()[0].revision, 4);
    }

    #[tokio::test]
    async fn dropped_waiter_times_out_to_failed_and_exposes_no_raw_arguments() {
        const RAW_SENTINEL: &str = "RAW-ROUTINE-SECRET-9F4C";
        let registry = Arc::new(RoutineRunRegistry::default());
        let queued = registry.begin("r", RoutineRunSource::Manual);
        registry.mark_running(&queued.run_id).expect("running");
        let waiter = registry
            .request_approval(
                &queued.run_id,
                "browser_type".to_string(),
                "sensitive".to_string(),
            )
            .expect("awaiting approval");
        let json = serde_json::to_string(&registry.snapshot()[0]).expect("status json");
        assert!(!json.contains(RAW_SENTINEL));
        assert!(!json.contains("arguments"));
        drop(waiter);
        let failed = &registry.snapshot()[0];
        assert_eq!(failed.state, RoutineRunState::Failed);
        assert_eq!(failed.revision, 4);
        assert_eq!(failed.error.as_deref(), Some("Approval timed out"));
    }

    #[test]
    fn non_sensitive_path_remains_running_without_pending_approval() {
        let registry = RoutineRunRegistry::default();
        let queued = registry.begin("r", RoutineRunSource::Manual);
        let running = registry.mark_running(&queued.run_id).expect("running");
        assert_eq!(running.state, RoutineRunState::Running);
        assert_eq!(running.revision, 2);
        assert!(running.approval.is_none());
    }

    #[test]
    fn concurrent_runs_of_one_routine_have_distinct_ids_and_independent_revisions() {
        const RUNS: usize = 16;
        let registry = Arc::new(RoutineRunRegistry::default());
        let barrier = Arc::new(Barrier::new(RUNS));
        let mut threads = Vec::new();
        for index in 0..RUNS {
            let registry = Arc::clone(&registry);
            let barrier = Arc::clone(&barrier);
            threads.push(std::thread::spawn(move || {
                barrier.wait();
                let queued = registry.begin("same-routine", RoutineRunSource::Event);
                registry.mark_running(&queued.run_id).expect("running");
                if index % 2 == 0 {
                    registry
                        .mark_succeeded(&queued.run_id, index.to_string())
                        .expect("succeeded")
                } else {
                    registry
                        .mark_failed(&queued.run_id, index.to_string())
                        .expect("failed")
                }
            }));
        }
        let statuses = threads
            .into_iter()
            .map(|thread| thread.join().expect("thread completed"))
            .collect::<Vec<_>>();
        let ids = statuses
            .iter()
            .map(|status| status.run_id.as_str())
            .collect::<HashSet<_>>();
        assert_eq!(ids.len(), RUNS);
        assert!(statuses.iter().all(|status| status.revision == 3));
        assert_eq!(registry.snapshot().len(), RUNS);
    }

    #[test]
    fn registry_caps_at_max_routine_runs() {
        let registry = RoutineRunRegistry::default();
        for i in 0..MAX_ROUTINE_RUNS {
            let run = registry.begin_at(format!("routine-{i}"), RoutineRunSource::Manual, i as i64);
            registry.mark_running(&run.run_id).unwrap();
            registry
                .mark_succeeded(&run.run_id, "done".to_string())
                .unwrap();
        }
        for i in MAX_ROUTINE_RUNS..MAX_ROUTINE_RUNS + 10 {
            let run = registry.begin_at(format!("routine-{i}"), RoutineRunSource::Manual, i as i64);
            registry.mark_running(&run.run_id).unwrap();
            registry
                .mark_succeeded(&run.run_id, "done".to_string())
                .unwrap();
        }
        let runs = registry.snapshot();
        assert_eq!(runs.len(), MAX_ROUTINE_RUNS);
        let min_started_at = runs.iter().map(|r| r.started_at).min().unwrap();
        assert_eq!(min_started_at, 10);
    }
}
