// Copyright 2026 Maho Browser. All rights reserved.

//! Event wait and session wake contracts.
//!
//! Allows agent runs to suspend execution while awaiting external notifications
//! or signals, and wake up to resume the same run identity upon arrival.

use crate::run_journal::AgentRunId;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use thiserror::Error;

/// Unique handle identifying a pending wait registration.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct WaitHandle(pub String);

impl WaitHandle {
    pub fn new(id: impl Into<String>) -> Self {
        Self(id.into())
    }

    pub fn from_u64(seq: u64) -> Self {
        Self(format!("wait-handle-{seq}"))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Display for WaitHandle {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// Token proving authorization to resume a specific agent run following a wake event.
///
/// Provides a type-level link to the agent journal (#11) preserving run identity.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct ResumeToken {
    pub run_id: AgentRunId,
    pub handle: WaitHandle,
}

impl ResumeToken {
    pub fn new(run_id: impl Into<AgentRunId>, handle: WaitHandle) -> Self {
        Self {
            run_id: run_id.into(),
            handle,
        }
    }

    pub fn run_id(&self) -> &AgentRunId {
        &self.run_id
    }

    pub fn handle(&self) -> &WaitHandle {
        &self.handle
    }
}

/// Event kind classification for structured wait filters.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum EventKind {
    Notification,
    InboxMessage,
    Webhook,
    RoutineTrigger,
    SystemSignal,
    Custom(String),
}

impl std::fmt::Display for EventKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Notification => write!(f, "notification"),
            Self::InboxMessage => write!(f, "inbox_message"),
            Self::Webhook => write!(f, "webhook"),
            Self::RoutineTrigger => write!(f, "routine_trigger"),
            Self::SystemSignal => write!(f, "system_signal"),
            Self::Custom(name) => write!(f, "custom({name})"),
        }
    }
}

/// Filter matching incoming events for a wait handle.
///
/// Uses simple enum and optional string matching (no regex).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct EventFilter {
    pub kind: EventKind,
    pub sender: Option<String>,
    pub source: Option<String>,
    pub topic: Option<String>,
}

impl EventFilter {
    pub fn new(kind: EventKind) -> Self {
        Self {
            kind,
            sender: None,
            source: None,
            topic: None,
        }
    }

    pub fn notification(sender: Option<impl Into<String>>) -> Self {
        Self {
            kind: EventKind::Notification,
            sender: sender.map(Into::into),
            source: None,
            topic: None,
        }
    }

    pub fn inbox_message(sender: Option<impl Into<String>>) -> Self {
        Self {
            kind: EventKind::InboxMessage,
            sender: sender.map(Into::into),
            source: None,
            topic: None,
        }
    }

    pub fn webhook(source: Option<impl Into<String>>, topic: Option<impl Into<String>>) -> Self {
        Self {
            kind: EventKind::Webhook,
            sender: None,
            source: source.map(Into::into),
            topic: topic.map(Into::into),
        }
    }

    pub fn with_sender(mut self, sender: impl Into<String>) -> Self {
        self.sender = Some(sender.into());
        self
    }

    pub fn with_source(mut self, source: impl Into<String>) -> Self {
        self.source = Some(source.into());
        self
    }

    pub fn with_topic(mut self, topic: impl Into<String>) -> Self {
        self.topic = Some(topic.into());
        self
    }

    /// Match an incoming wake event against this filter using simple structured equality.
    pub fn matches(&self, event: &WakeEvent) -> bool {
        if self.kind != event.kind {
            return false;
        }
        if let Some(ref expected_sender) = self.sender {
            if event.sender.as_ref() != Some(expected_sender) {
                return false;
            }
        }
        if let Some(ref expected_source) = self.source {
            if event.source.as_ref() != Some(expected_source) {
                return false;
            }
        }
        if let Some(ref expected_topic) = self.topic {
            if event.topic.as_ref() != Some(expected_topic) {
                return false;
            }
        }
        true
    }
}

/// Backwards-compatible legacy wait filter.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct WaitFilter {
    pub event_source: String,
    pub event_type: String,
    pub match_criteria: serde_json::Value,
}

/// Event that woke or can wake a suspended agent run.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct WakeEvent {
    pub run_id: Option<AgentRunId>,
    pub kind: EventKind,
    pub sender: Option<String>,
    pub source: Option<String>,
    pub topic: Option<String>,
    pub payload: serde_json::Value,
    pub timestamp: u64,
}

impl WakeEvent {
    pub fn new(kind: EventKind, timestamp: u64) -> Self {
        Self {
            run_id: None,
            kind,
            sender: None,
            source: None,
            topic: None,
            payload: serde_json::Value::Null,
            timestamp,
        }
    }

    pub fn for_run(run_id: impl Into<AgentRunId>, kind: EventKind, timestamp: u64) -> Self {
        Self {
            run_id: Some(run_id.into()),
            kind,
            sender: None,
            source: None,
            topic: None,
            payload: serde_json::Value::Null,
            timestamp,
        }
    }

    pub fn with_sender(mut self, sender: impl Into<String>) -> Self {
        self.sender = Some(sender.into());
        self
    }

    pub fn with_source(mut self, source: impl Into<String>) -> Self {
        self.source = Some(source.into());
        self
    }

    pub fn with_topic(mut self, topic: impl Into<String>) -> Self {
        self.topic = Some(topic.into());
        self
    }

    pub fn with_payload(mut self, payload: serde_json::Value) -> Self {
        self.payload = payload;
        self
    }
}

/// Current status and resolution of an event wait.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "status", rename_all = "snake_case")]
pub enum WaitStatus {
    Suspended {
        handle: WaitHandle,
        timeout_at: u64,
    },
    Woken {
        token: ResumeToken,
        event: WakeEvent,
    },
    TimedOut {
        handle: WaitHandle,
        deadline: u64,
    },
    Cancelled {
        handle: WaitHandle,
    },
}

impl WaitStatus {
    pub fn is_suspended(&self) -> bool {
        matches!(self, Self::Suspended { .. })
    }

    pub fn is_woken(&self) -> bool {
        matches!(self, Self::Woken { .. })
    }

    pub fn is_timed_out(&self) -> bool {
        matches!(self, Self::TimedOut { .. })
    }

    pub fn is_cancelled(&self) -> bool {
        matches!(self, Self::Cancelled { .. })
    }

    pub fn resume_token(&self) -> Option<&ResumeToken> {
        match self {
            Self::Woken { token, .. } => Some(token),
            _ => None,
        }
    }
}

/// Result of polling or checking an event wait.
pub type WaitResult = WaitStatus;

/// Notification permission gate state.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct NotificationActivation {
    pub granted: bool,
}

impl NotificationActivation {
    pub fn granted() -> Self {
        Self { granted: true }
    }

    pub fn denied() -> Self {
        Self { granted: false }
    }

    pub fn ensure_granted(&self) -> Result<(), EventWaitError> {
        if self.granted {
            Ok(())
        } else {
            Err(EventWaitError::NotificationPermissionDenied)
        }
    }
}

/// Typed errors arising from event wait operations.
#[derive(Debug, Clone, PartialEq, Eq, Error, Serialize, Deserialize)]
pub enum EventWaitError {
    #[error("Notification activation not granted")]
    NotificationPermissionDenied,
    #[error("Agent run {0} is already waiting for an event")]
    AlreadyWaiting(AgentRunId),
    #[error("Wait handle {0} not found")]
    NotFound(WaitHandle),
    #[error("Run {0} not found in wait registry")]
    RunNotFound(AgentRunId),
    #[error("Wait deadline {deadline} must be after current time {current_time}")]
    InvalidDeadline { deadline: u64, current_time: u64 },
    #[error("Wait entry {0} is already in terminal state")]
    AlreadyTerminal(WaitHandle),
}

/// Abstract clock source for deterministic temporal assertions without sleeps.
pub trait Clock: Send + Sync {
    fn now_millis(&self) -> u64;
}

/// System clock using `SystemTime`.
#[derive(Debug, Clone, Default)]
pub struct SystemClock;

impl Clock for SystemClock {
    fn now_millis(&self) -> u64 {
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as u64
    }
}

/// Controllable mock clock for deterministic testing.
#[derive(Debug, Clone)]
pub struct MockClock {
    now: Arc<std::sync::atomic::AtomicU64>,
}

impl MockClock {
    pub fn new(initial_millis: u64) -> Self {
        Self {
            now: Arc::new(std::sync::atomic::AtomicU64::new(initial_millis)),
        }
    }

    pub fn advance(&self, millis: u64) {
        self.now
            .fetch_add(millis, std::sync::atomic::Ordering::SeqCst);
    }

    pub fn set(&self, millis: u64) {
        self.now.store(millis, std::sync::atomic::Ordering::SeqCst);
    }
}

impl Clock for MockClock {
    fn now_millis(&self) -> u64 {
        self.now.load(std::sync::atomic::Ordering::SeqCst)
    }
}

/// Internal entry stored in `WaitRegistry`.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct WaitEntry {
    pub handle: WaitHandle,
    pub run_id: AgentRunId,
    pub filter: EventFilter,
    pub deadline: u64,
    pub created_at: u64,
    pub status: WaitStatus,
}

/// Registry managing active event waits, wake dispatching, cancellation, and expirations.
pub struct WaitRegistry {
    clock: Arc<dyn Clock>,
    next_seq: u64,
    entries: HashMap<AgentRunId, WaitEntry>,
    handle_to_run: HashMap<WaitHandle, AgentRunId>,
}

impl Default for WaitRegistry {
    fn default() -> Self {
        Self::new()
    }
}

impl WaitRegistry {
    pub fn new() -> Self {
        Self {
            clock: Arc::new(SystemClock),
            next_seq: 1,
            entries: HashMap::new(),
            handle_to_run: HashMap::new(),
        }
    }

    pub fn with_clock(clock: Arc<dyn Clock>) -> Self {
        Self {
            clock,
            next_seq: 1,
            entries: HashMap::new(),
            handle_to_run: HashMap::new(),
        }
    }

    /// Register a new event wait for a run.
    pub fn register(
        &mut self,
        run_id: impl Into<AgentRunId>,
        filter: EventFilter,
        deadline: u64,
    ) -> Result<WaitHandle, EventWaitError> {
        let run_id = run_id.into();
        let now = self.clock.now_millis();

        if deadline <= now {
            return Err(EventWaitError::InvalidDeadline {
                deadline,
                current_time: now,
            });
        }

        if let Some(existing) = self.entries.get(&run_id) {
            if existing.status.is_suspended() {
                return Err(EventWaitError::AlreadyWaiting(run_id));
            }
        }

        let handle = WaitHandle::from_u64(self.next_seq);
        self.next_seq += 1;

        let entry = WaitEntry {
            handle: handle.clone(),
            run_id: run_id.clone(),
            filter,
            deadline,
            created_at: now,
            status: WaitStatus::Suspended {
                handle: handle.clone(),
                timeout_at: deadline,
            },
        };

        self.handle_to_run.insert(handle.clone(), run_id.clone());
        self.entries.insert(run_id, entry);

        Ok(handle)
    }

    /// Suspend execution of a run while awaiting an event (alias for register).
    pub fn suspend(
        &mut self,
        run_id: impl Into<AgentRunId>,
        filter: EventFilter,
        deadline: u64,
    ) -> Result<WaitHandle, EventWaitError> {
        self.register(run_id, filter, deadline)
    }

    /// Suspend execution gated by notification permission activation.
    pub fn suspend_with_activation(
        &mut self,
        run_id: impl Into<AgentRunId>,
        filter: EventFilter,
        deadline: u64,
        activation: NotificationActivation,
    ) -> Result<WaitHandle, EventWaitError> {
        activation.ensure_granted()?;
        self.register(run_id, filter, deadline)
    }

    /// Wake suspended runs matching the incoming wake event.
    ///
    /// Returns the list of `ResumeToken`s for runs that were successfully woken.
    pub fn wake(&mut self, event: &WakeEvent) -> Vec<ResumeToken> {
        let now = self.clock.now_millis();
        let mut woken = Vec::new();

        for entry in self.entries.values_mut() {
            // First check if already expired by clock
            if let WaitStatus::Suspended { .. } = entry.status {
                if now >= entry.deadline {
                    entry.status = WaitStatus::TimedOut {
                        handle: entry.handle.clone(),
                        deadline: entry.deadline,
                    };
                    continue;
                }
            }

            if !entry.status.is_suspended() {
                continue;
            }

            // Check run_id target if specified in event
            if let Some(ref target_run_id) = event.run_id {
                if target_run_id != &entry.run_id {
                    continue;
                }
            }

            // Match filter
            if entry.filter.matches(event) {
                let token = ResumeToken::new(entry.run_id.clone(), entry.handle.clone());
                entry.status = WaitStatus::Woken {
                    token: token.clone(),
                    event: event.clone(),
                };
                woken.push(token);
            }
        }

        woken
    }

    /// Cancel a pending wait by run ID.
    pub fn cancel(&mut self, run_id: &AgentRunId) -> Result<WaitHandle, EventWaitError> {
        let entry = self
            .entries
            .get_mut(run_id)
            .ok_or_else(|| EventWaitError::RunNotFound(run_id.clone()))?;

        if !entry.status.is_suspended() {
            return Err(EventWaitError::AlreadyTerminal(entry.handle.clone()));
        }

        let handle = entry.handle.clone();
        entry.status = WaitStatus::Cancelled {
            handle: handle.clone(),
        };
        Ok(handle)
    }

    /// Cancel a pending wait by wait handle.
    pub fn cancel_handle(&mut self, handle: &WaitHandle) -> Result<WaitHandle, EventWaitError> {
        let run_id = self
            .handle_to_run
            .get(handle)
            .cloned()
            .ok_or_else(|| EventWaitError::NotFound(handle.clone()))?;
        self.cancel(&run_id)
    }

    /// Poll current status for a run, checking for deadline expirations against the clock.
    pub fn poll(&mut self, run_id: &AgentRunId) -> Result<WaitStatus, EventWaitError> {
        let now = self.clock.now_millis();
        let entry = self
            .entries
            .get_mut(run_id)
            .ok_or_else(|| EventWaitError::RunNotFound(run_id.clone()))?;

        if let WaitStatus::Suspended { .. } = entry.status {
            if now >= entry.deadline {
                entry.status = WaitStatus::TimedOut {
                    handle: entry.handle.clone(),
                    deadline: entry.deadline,
                };
            }
        }

        Ok(entry.status.clone())
    }

    /// Poll current status by wait handle.
    pub fn poll_handle(&mut self, handle: &WaitHandle) -> Result<WaitStatus, EventWaitError> {
        let run_id = self
            .handle_to_run
            .get(handle)
            .cloned()
            .ok_or_else(|| EventWaitError::NotFound(handle.clone()))?;
        self.poll(&run_id)
    }

    /// Number of currently suspended (active) wait entries.
    pub fn active_wait_count(&self) -> usize {
        let now = self.clock.now_millis();
        self.entries
            .values()
            .filter(|e| e.status.is_suspended() && e.deadline > now)
            .count()
    }

    /// Total number of tracked entries.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Retrieve isolated debug representation of a specific run's wait context.
    ///
    /// Privacy guarantee: Content from other runs is NEVER leaked into this output.
    pub fn run_debug_context(&self, run_id: &AgentRunId) -> Result<String, EventWaitError> {
        let entry = self
            .entries
            .get(run_id)
            .ok_or_else(|| EventWaitError::RunNotFound(run_id.clone()))?;

        Ok(format!(
            "RunWaitContext {{ run_id: \"{}\", handle: \"{}\", filter_kind: \"{}\", status: {:?} }}",
            entry.run_id, entry.handle, entry.filter.kind, entry.status
        ))
    }
}

impl std::fmt::Debug for WaitRegistry {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("WaitRegistry")
            .field("active_count", &self.active_wait_count())
            .field("total_runs", &self.entries.len())
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_suspend_wake_matching_event_returns_same_run_id_resume_token() {
        let clock = Arc::new(MockClock::new(1000));
        let mut registry = WaitRegistry::with_clock(clock);

        let run_id = AgentRunId::new("run-1001");
        let filter = EventFilter::notification(Some("alice@example.com"));

        let handle = registry
            .suspend(run_id.clone(), filter.clone(), 5000)
            .expect("suspend should succeed");

        assert_eq!(registry.active_wait_count(), 1);

        // Matching wake event
        let wake_ev = WakeEvent::for_run(run_id.clone(), EventKind::Notification, 2000)
            .with_sender("alice@example.com")
            .with_payload(serde_json::json!({"title": "Hello"}));

        let tokens = registry.wake(&wake_ev);
        assert_eq!(tokens.len(), 1);
        assert_eq!(tokens[0].run_id(), &run_id);
        assert_eq!(tokens[0].handle(), &handle);

        let status = registry.poll(&run_id).expect("poll should succeed");
        match status {
            WaitStatus::Woken { token, event } => {
                assert_eq!(token.run_id(), &run_id);
                assert_eq!(token.handle(), &handle);
                assert_eq!(event.payload["title"], "Hello");
            }
            other => panic!("Expected Woken status, got {:?}", other),
        }
    }

    #[test]
    fn test_non_matching_event_keeps_run_waiting() {
        let clock = Arc::new(MockClock::new(1000));
        let mut registry = WaitRegistry::with_clock(clock);

        let run_id = AgentRunId::new("run-1002");
        let filter = EventFilter::notification(Some("alice@example.com"));

        let handle = registry
            .suspend(run_id.clone(), filter, 5000)
            .expect("suspend should succeed");

        // Non-matching sender
        let non_matching_ev = WakeEvent::for_run(run_id.clone(), EventKind::Notification, 2000)
            .with_sender("mallory@example.com");

        let tokens = registry.wake(&non_matching_ev);
        assert!(tokens.is_empty());

        let status = registry.poll(&run_id).expect("poll should succeed");
        assert!(status.is_suspended());

        // Non-matching kind
        let wrong_kind_ev = WakeEvent::for_run(run_id.clone(), EventKind::InboxMessage, 2000)
            .with_sender("alice@example.com");

        let tokens2 = registry.wake(&wrong_kind_ev);
        assert!(tokens2.is_empty());

        let status2 = registry.poll(&run_id).expect("poll should succeed");
        assert_eq!(
            status2,
            WaitStatus::Suspended {
                handle,
                timeout_at: 5000
            }
        );
    }

    #[test]
    fn test_bounded_timeout_with_injected_clock_fails_closed_no_wake() {
        let clock = Arc::new(MockClock::new(1000));
        let mut registry = WaitRegistry::with_clock(clock.clone());

        let run_id = AgentRunId::new("run-1003");
        let filter = EventFilter::notification(Some("alice@example.com"));

        let handle = registry
            .suspend(run_id.clone(), filter, 5000)
            .expect("suspend should succeed");

        // Advance clock past deadline
        clock.advance(4001); // now = 5001 > 5000

        // Attempt wake after expiration -> must fail-closed (no wake)
        let wake_ev = WakeEvent::for_run(run_id.clone(), EventKind::Notification, 5001)
            .with_sender("alice@example.com");
        let tokens = registry.wake(&wake_ev);
        assert!(
            tokens.is_empty(),
            "Expired wait must not produce resume tokens"
        );

        let status = registry.poll(&run_id).expect("poll should succeed");
        assert_eq!(
            status,
            WaitStatus::TimedOut {
                handle,
                deadline: 5000
            }
        );
    }

    #[test]
    fn test_cancel_while_waiting_transitions_to_cancelled_and_prevents_later_wake() {
        let clock = Arc::new(MockClock::new(1000));
        let mut registry = WaitRegistry::with_clock(clock);

        let run_id = AgentRunId::new("run-1004");
        let filter = EventFilter::notification(Some("alice@example.com"));

        let handle = registry
            .suspend(run_id.clone(), filter, 5000)
            .expect("suspend should succeed");

        // Cancel the wait
        let cancelled_handle = registry.cancel(&run_id).expect("cancel should succeed");
        assert_eq!(cancelled_handle, handle);

        let status = registry.poll(&run_id).expect("poll should succeed");
        assert_eq!(status, WaitStatus::Cancelled { handle });

        // Late wake event should NOT wake cancelled run
        let late_ev = WakeEvent::for_run(run_id.clone(), EventKind::Notification, 2000)
            .with_sender("alice@example.com");
        let tokens = registry.wake(&late_ev);
        assert!(tokens.is_empty(), "Cancelled wait must not be woken");

        let status_after = registry.poll(&run_id).expect("poll should succeed");
        assert!(status_after.is_cancelled());
    }

    #[test]
    fn test_notification_permission_gate_denied_returns_typed_error_before_wait() {
        let clock = Arc::new(MockClock::new(1000));
        let mut registry = WaitRegistry::with_clock(clock);

        let run_id = AgentRunId::new("run-1005");
        let filter = EventFilter::notification(Some("alice@example.com"));
        let denied_activation = NotificationActivation::denied();

        let result = registry.suspend_with_activation(
            run_id.clone(),
            filter.clone(),
            5000,
            denied_activation,
        );

        assert_eq!(result, Err(EventWaitError::NotificationPermissionDenied));
        assert_eq!(registry.len(), 0);
        assert!(registry.poll(&run_id).is_err());

        // Granted activation proceeds normally
        let granted_activation = NotificationActivation::granted();
        let ok_result =
            registry.suspend_with_activation(run_id.clone(), filter, 5000, granted_activation);
        assert!(ok_result.is_ok());
        assert_eq!(registry.active_wait_count(), 1);
    }

    #[test]
    fn test_privacy_assertion_filter_content_never_copied_to_unrelated_run_debug() {
        let clock = Arc::new(MockClock::new(1000));
        let mut registry = WaitRegistry::with_clock(clock);

        let run_a = AgentRunId::new("run-alice-private-task");
        let secret_sender = "secret-classified-channel-sender-777";
        let filter_a = EventFilter::new(EventKind::Custom("confidential_op".to_string()))
            .with_sender(secret_sender);

        let run_b = AgentRunId::new("run-bob-public-task");
        let public_sender = "bob-public-sender";
        let filter_b = EventFilter::notification(Some(public_sender));

        registry.suspend(run_a.clone(), filter_a, 5000).unwrap();
        registry.suspend(run_b.clone(), filter_b, 5000).unwrap();

        // Query debug output for run B
        let debug_b = registry.run_debug_context(&run_b).unwrap();
        assert!(
            !debug_b.contains(secret_sender),
            "Run B context must not leak Run A's private sender"
        );
        assert!(
            !debug_b.contains("confidential_op"),
            "Run B context must not leak Run A's filter op"
        );
        assert!(
            !debug_b.contains(&run_a.to_string()),
            "Run B context must not leak Run A's ID"
        );

        // Registry top-level debug output should not dump sensitive filter contents
        let registry_debug = format!("{:?}", registry);
        assert!(!registry_debug.contains(secret_sender));
        assert!(!registry_debug.contains("confidential_op"));
    }

    #[test]
    fn test_cancel_resume_adversarial_probes() {
        let clock = Arc::new(MockClock::new(1000));
        let mut registry = WaitRegistry::with_clock(clock.clone());

        let run_id = AgentRunId::new("run-adv-1");
        let filter = EventFilter::notification(Some("sender-adv"));

        let _handle = registry.suspend(run_id.clone(), filter, 4000).unwrap();

        // Double cancel returns AlreadyTerminal error
        registry.cancel(&run_id).unwrap();
        let double_cancel = registry.cancel(&run_id);
        assert!(matches!(
            double_cancel,
            Err(EventWaitError::AlreadyTerminal(_))
        ));

        // Waking a cancelled handle by general event returns empty
        let ev = WakeEvent::new(EventKind::Notification, 2000).with_sender("sender-adv");
        let woken = registry.wake(&ev);
        assert!(woken.is_empty());

        // Cancel via handle also works
        let run_id_2 = AgentRunId::new("run-adv-2");
        let filter_2 = EventFilter::notification(Some("sender-adv-2"));
        let handle_2 = registry.suspend(run_id_2.clone(), filter_2, 6000).unwrap();
        let res = registry.cancel_handle(&handle_2);
        assert_eq!(res.unwrap(), handle_2);
    }

    #[test]
    fn test_concurrent_waits_and_filter_matching_by_topic_source() {
        let clock = Arc::new(MockClock::new(1000));
        let mut registry = WaitRegistry::with_clock(clock);

        let run_1 = AgentRunId::new("run-webhook-1");
        let run_2 = AgentRunId::new("run-webhook-2");

        let filter_1 = EventFilter::webhook(Some("github"), Some("push"));
        let filter_2 = EventFilter::webhook(Some("slack"), Some("message"));

        let handle_1 = registry.register(run_1.clone(), filter_1, 5000).unwrap();
        let handle_2 = registry.register(run_2.clone(), filter_2, 5000).unwrap();

        assert_eq!(registry.active_wait_count(), 2);

        // Wake matching only webhook 1
        let ev = WakeEvent::new(EventKind::Webhook, 2000)
            .with_source("github")
            .with_topic("push")
            .with_payload(serde_json::json!({"ref": "refs/heads/main"}));

        let tokens = registry.wake(&ev);
        assert_eq!(tokens.len(), 1);
        assert_eq!(tokens[0].run_id(), &run_1);
        assert_eq!(tokens[0].handle(), &handle_1);

        // run_1 is woken
        assert!(registry.poll(&run_1).unwrap().is_woken());
        // run_2 is still waiting
        assert!(registry.poll(&run_2).unwrap().is_suspended());
        assert_eq!(registry.active_wait_count(), 1);

        // Wake webhook 2
        let ev_2 = WakeEvent::new(EventKind::Webhook, 2500)
            .with_source("slack")
            .with_topic("message");
        let tokens_2 = registry.wake(&ev_2);
        assert_eq!(tokens_2.len(), 1);
        assert_eq!(tokens_2[0].run_id(), &run_2);
        assert_eq!(tokens_2[0].handle(), &handle_2);
        assert!(registry.poll(&run_2).unwrap().is_woken());
        assert_eq!(registry.active_wait_count(), 0);
    }

    #[test]
    fn test_invalid_deadline_and_already_waiting_errors() {
        let clock = Arc::new(MockClock::new(2000));
        let mut registry = WaitRegistry::with_clock(clock.clone());

        let run_id = AgentRunId::new("run-err-1");
        let filter = EventFilter::new(EventKind::RoutineTrigger);

        // Deadline in the past or now
        let past_err = registry.register(run_id.clone(), filter.clone(), 1999);
        assert!(matches!(
            past_err,
            Err(EventWaitError::InvalidDeadline {
                deadline: 1999,
                current_time: 2000
            })
        ));

        let eq_err = registry.register(run_id.clone(), filter.clone(), 2000);
        assert!(matches!(
            eq_err,
            Err(EventWaitError::InvalidDeadline {
                deadline: 2000,
                current_time: 2000
            })
        ));

        // Valid registration
        let handle = registry
            .register(run_id.clone(), filter.clone(), 3000)
            .unwrap();

        // Duplicate registration while active returns AlreadyWaiting
        let dup_err = registry.register(run_id.clone(), filter.clone(), 4000);
        assert!(matches!(dup_err, Err(EventWaitError::AlreadyWaiting(_))));

        // Unknown run / handle error checks
        assert!(matches!(
            registry.poll(&AgentRunId::new("unknown")),
            Err(EventWaitError::RunNotFound(_))
        ));
        assert!(matches!(
            registry.poll_handle(&WaitHandle::new("nonexistent")),
            Err(EventWaitError::NotFound(_))
        ));
        assert!(matches!(
            registry.cancel(&AgentRunId::new("unknown")),
            Err(EventWaitError::RunNotFound(_))
        ));
        assert!(matches!(
            registry.cancel_handle(&WaitHandle::new("nonexistent")),
            Err(EventWaitError::NotFound(_))
        ));

        // After expiring, re-registering the same run succeeds
        clock.advance(1500); // now = 3500 > 3000
        assert!(registry.poll_handle(&handle).unwrap().is_timed_out());

        let new_handle = registry.register(run_id.clone(), filter, 6000);
        assert!(new_handle.is_ok());
    }
}
