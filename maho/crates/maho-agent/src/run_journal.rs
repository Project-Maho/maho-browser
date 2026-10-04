// Copyright 2026 Maho Browser. All rights reserved.

//! Durable run journal and replay sequence contracts.
//!
//! Provides monotonic event sequencing, durable run checkpoints, and
//! restart classification for execution continuity across reconnects.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;

/// Unique identifier for an agent execution run.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct AgentRunId(pub String);

impl AgentRunId {
    pub fn new(id: impl Into<String>) -> Self {
        Self(id.into())
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Display for AgentRunId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// Monotonically increasing sequence number for external runtime events in a run.
pub type EventSeq = u64;

/// Classification of a run state upon process restart or connection recovery.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RestartClassification {
    /// Safe to automatically resume (e.g. read-only preflight, waiting on external timer).
    Recoverable,
    /// Must not auto-retry because an uncertain external mutation occurred or approval is pending.
    NeedsUserResume,
}

/// Lifecycle checkpoint kind recorded during an agent run.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RunCheckpointKind {
    Started,
    StepProgress,
    WaitingEvent,
    WaitingApproval,
    WaitingInteraction,
    Compacting,
    Completed,
    Failed,
    Cancelled,
}

impl RunCheckpointKind {
    pub fn is_terminal(&self) -> bool {
        matches!(self, Self::Completed | Self::Failed | Self::Cancelled)
    }
}

/// Durable checkpoint capturing the state of an agent run at a specific event sequence.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RunCheckpoint {
    pub run_id: AgentRunId,
    pub session_id: String,
    pub last_event_seq: EventSeq,
    pub kind: RunCheckpointKind,
    pub classification: RestartClassification,
    pub state_payload: Option<serde_json::Value>,
    pub timestamp: u64,
}

/// Journal entry recorded for durable replay.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RunJournalEntry {
    pub run_id: AgentRunId,
    pub event_seq: EventSeq,
    pub event_name: String,
    pub payload_json: String,
    pub timestamp: u64,
}

/// A resume step evaluated during restart analysis.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ResumeStep {
    pub event_seq: EventSeq,
    pub event_name: String,
    pub classification: RestartClassification,
    pub user_decision_required: bool,
    pub reason: String,
}

/// Complete resume plan for a run.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ResumePlan {
    pub run_id: AgentRunId,
    pub is_terminal: bool,
    pub can_auto_resume: bool,
    pub recoverable_steps: Vec<ResumeStep>,
    pub uncertain_steps: Vec<ResumeStep>,
}

impl ResumePlan {
    pub fn requires_user_decision(&self) -> bool {
        !self.uncertain_steps.is_empty()
    }
}

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum RunJournalError {
    #[error("run not found: {0}")]
    RunNotFound(AgentRunId),
    #[error("run {0} is already in a terminal state")]
    RunAlreadyTerminal(AgentRunId),
    #[error("duplicate terminal marker for run {0}")]
    DuplicateTerminalMarker(AgentRunId),
    #[error("invalid sequence number: expected {expected}, got {actual}")]
    InvalidSequence {
        expected: EventSeq,
        actual: EventSeq,
    },
    #[error("serialization error: {0}")]
    SerializationError(String),
    #[error("storage error: {0}")]
    StorageError(String),
}

/// Abstract storage trait for durable journal persistence.
pub trait JournalStore: Send + Sync + std::fmt::Debug {
    fn save(&self, journal: &RunJournal) -> Result<(), RunJournalError>;
    fn load(&self) -> Result<RunJournal, RunJournalError>;
}

/// Filesystem JSON storage implementation for durable run journals.
#[derive(Debug, Clone)]
pub struct FsJsonStore {
    path: std::path::PathBuf,
}

impl FsJsonStore {
    pub fn new(path: impl Into<std::path::PathBuf>) -> Self {
        Self { path: path.into() }
    }

    pub fn path(&self) -> &std::path::Path {
        &self.path
    }
}

impl JournalStore for FsJsonStore {
    fn save(&self, journal: &RunJournal) -> Result<(), RunJournalError> {
        if let Some(parent) = self.path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let json = journal.to_json()?;
        std::fs::write(&self.path, json)
            .map_err(|e| RunJournalError::StorageError(e.to_string()))?;
        Ok(())
    }

    fn load(&self) -> Result<RunJournal, RunJournalError> {
        let content = std::fs::read_to_string(&self.path)
            .map_err(|e| RunJournalError::StorageError(e.to_string()))?;
        RunJournal::from_json(&content)
    }
}

/// Internal state for a single agent execution run.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RunRecord {
    pub run_id: AgentRunId,
    pub session_id: String,
    pub next_seq: EventSeq,
    pub entries: Vec<RunJournalEntry>,
    pub checkpoints: Vec<RunCheckpoint>,
    pub terminal_kind: Option<RunCheckpointKind>,
}

/// Classifies a journal entry into Recoverable (read/wait ops) vs NeedsUserResume (uncertain mutation).
pub fn classify_entry(entry: &RunJournalEntry) -> RestartClassification {
    let name = entry.event_name.to_ascii_lowercase();

    // 1. Inspect explicit effect flags in payload JSON if parseable
    if let Ok(val) = serde_json::from_str::<serde_json::Value>(&entry.payload_json) {
        if let Some(effect) = val.get("effect").and_then(|v| v.as_str()) {
            match effect {
                "read_only" | "wait" | "query" | "status" => {
                    return RestartClassification::Recoverable
                }
                "mutation" | "external_mutation" | "write" | "action" | "approval" => {
                    return RestartClassification::NeedsUserResume
                }
                _ => {}
            }
        }
        if val.get("read_only").and_then(|v| v.as_bool()) == Some(true) {
            return RestartClassification::Recoverable;
        }
        if val.get("side_effect").and_then(|v| v.as_bool()) == Some(true)
            || val.get("uncertain").and_then(|v| v.as_bool()) == Some(true)
            || val.get("mutation").and_then(|v| v.as_bool()) == Some(true)
            || val.get("decision_required").and_then(|v| v.as_bool()) == Some(true)
        {
            return RestartClassification::NeedsUserResume;
        }
    }

    // 2. Classify known read-only and wait primitives
    if name == "status"
        || name == "progress"
        || name == "started"
        || name == "step_started"
        || name == "step_progress"
        || name == "waiting_event"
        || name == "wait_for_event"
        || name == "compacting"
        || name == "read"
        || name == "browser_read"
        || name == "history_search"
        || name == "browser_history_search"
        || name == "web_search"
        || name == "image_search"
        || name == "page_context"
        || name == "get_element"
        || name == "read_storage"
        || name == "query"
        || name == "memory_query"
        || name == "get_session"
        || name == "list_sessions"
        || name == "inspect"
    {
        return RestartClassification::Recoverable;
    }

    // 3. Any external mutation, click, send, write, approval request, or unknown action
    // strictly defaults to NeedsUserResume (fail-closed guardrail).
    RestartClassification::NeedsUserResume
}

/// Compaction metadata extracted from journal compaction events.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LastCompactionInfo {
    pub triggered_at: u64,
    pub dropped_turns: Vec<usize>,
    pub kept_markers: Vec<String>,
    pub session_id: String,
}

/// Durable run journal tracking active and past agent runs.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct RunJournal {
    runs: HashMap<AgentRunId, RunRecord>,
    #[serde(skip)]
    store: Option<Arc<dyn JournalStore>>,
    #[serde(skip)]
    is_persist_on_append: bool,
}

impl RunJournal {
    pub fn new() -> Self {
        Self {
            runs: HashMap::new(),
            store: None,
            is_persist_on_append: false,
        }
    }

    pub fn with_store(mut self, store: Arc<dyn JournalStore>) -> Self {
        self.store = Some(store);
        self
    }

    /// Constructs a new `RunJournal` backed by an `FsJsonStore` at the specified file path,
    /// with write-through persistence enabled on append.
    pub fn with_fs_store(path: impl Into<std::path::PathBuf>) -> Self {
        let store = Arc::new(FsJsonStore::new(path));
        let mut journal = Self::new().with_store(store);
        journal.persist_on_append();
        journal
    }

    /// Constructs or loads a `RunJournal` from an `FsJsonStore` path, with write-through persistence enabled.
    pub fn from_fs_path_or_new(path: impl Into<std::path::PathBuf>) -> Self {
        let path = path.into();
        let store: Arc<dyn JournalStore> = Arc::new(FsJsonStore::new(path));
        match Self::load_from_store(Arc::clone(&store)) {
            Ok(mut j) => {
                j.set_persist_on_append(true);
                j
            }
            Err(_) => {
                let mut j = Self::new().with_store(store);
                j.set_persist_on_append(true);
                j
            }
        }
    }

    /// Constructs or loads a `RunJournal` from an arbitrary `JournalStore`, with write-through persistence enabled.
    pub fn from_store_or_new(store: Arc<dyn JournalStore>) -> Self {
        match Self::load_from_store(Arc::clone(&store)) {
            Ok(mut j) => {
                j.set_persist_on_append(true);
                j
            }
            Err(_) => {
                let mut j = Self::new().with_store(store);
                j.set_persist_on_append(true);
                j
            }
        }
    }

    /// Creates a `RunJournal` for a specific session located in a base directory.
    pub fn for_session(base_dir: impl AsRef<std::path::Path>, session_id: &str) -> Self {
        let path = base_dir.as_ref().join(format!("{session_id}.journal.json"));
        Self::with_fs_store(path)
    }

    /// Checks if a backing store is currently attached.
    pub fn has_store(&self) -> bool {
        self.store.is_some()
    }

    /// Returns whether persist-on-append is currently enabled.
    pub fn is_persist_on_append(&self) -> bool {
        self.is_persist_on_append
    }

    /// Attaches a filesystem store at `path` and enables persist-on-append.
    pub fn attach_fs_store(&mut self, path: impl Into<std::path::PathBuf>) {
        self.store = Some(Arc::new(FsJsonStore::new(path)));
        self.is_persist_on_append = true;
    }

    pub fn set_store(&mut self, store: Arc<dyn JournalStore>) {
        self.store = Some(store);
    }

    pub fn persist_on_append(&mut self) -> &mut Self {
        self.is_persist_on_append = true;
        self
    }

    pub fn with_persist_on_append(mut self, enabled: bool) -> Self {
        self.is_persist_on_append = enabled;
        self
    }

    pub fn set_persist_on_append(&mut self, enabled: bool) {
        self.is_persist_on_append = enabled;
    }

    pub fn flush(&self) -> Result<(), RunJournalError> {
        if let Some(ref store) = self.store {
            store.save(self)?;
        }
        Ok(())
    }

    pub fn load_from_store(store: Arc<dyn JournalStore>) -> Result<Self, RunJournalError> {
        let mut journal = store.load()?;
        journal.store = Some(store);
        Ok(journal)
    }

    /// Initializes a new agent execution run starting sequence at 1.
    pub fn start_run(
        &mut self,
        run_id: AgentRunId,
        session_id: impl Into<String>,
    ) -> Result<(), RunJournalError> {
        let record = RunRecord {
            run_id: run_id.clone(),
            session_id: session_id.into(),
            next_seq: 1,
            entries: Vec::new(),
            checkpoints: Vec::new(),
            terminal_kind: None,
        };
        self.runs.insert(run_id, record);
        if self.is_persist_on_append {
            self.flush()?;
        }
        Ok(())
    }

    /// Appends a new sequenced runtime event to the run journal.
    /// Guarantees monotonic sequence numbering starting at 1 and rejects writes to terminal runs.
    pub fn append_event(
        &mut self,
        run_id: &AgentRunId,
        event_name: impl Into<String>,
        payload_json: impl Into<String>,
    ) -> Result<RunJournalEntry, RunJournalError> {
        let run = self
            .runs
            .get_mut(run_id)
            .ok_or_else(|| RunJournalError::RunNotFound(run_id.clone()))?;

        if run.terminal_kind.is_some() {
            return Err(RunJournalError::RunAlreadyTerminal(run_id.clone()));
        }

        let seq = run.next_seq;
        run.next_seq += 1;

        let entry = RunJournalEntry {
            run_id: run_id.clone(),
            event_seq: seq,
            event_name: event_name.into(),
            payload_json: payload_json.into(),
            timestamp: 0,
        };

        run.entries.push(entry.clone());
        if self.is_persist_on_append {
            self.flush()?;
        }
        Ok(entry)
    }

    /// Records an arbitrary checkpoint. If the checkpoint is terminal, enforces exactly-once recording.
    pub fn record_checkpoint(&mut self, checkpoint: RunCheckpoint) -> Result<(), RunJournalError> {
        let run = self
            .runs
            .get_mut(&checkpoint.run_id)
            .ok_or_else(|| RunJournalError::RunNotFound(checkpoint.run_id.clone()))?;

        if checkpoint.kind.is_terminal() {
            if run.terminal_kind.is_some() {
                return Err(RunJournalError::DuplicateTerminalMarker(
                    checkpoint.run_id.clone(),
                ));
            }
            run.terminal_kind = Some(checkpoint.kind);
        } else if run.terminal_kind.is_some() {
            return Err(RunJournalError::RunAlreadyTerminal(
                checkpoint.run_id.clone(),
            ));
        }

        run.checkpoints.push(checkpoint);
        if self.is_persist_on_append {
            self.flush()?;
        }
        Ok(())
    }

    /// Records a terminal marker (Completed, Failed, Cancelled) exactly once. Duplicate calls are rejected.
    pub fn record_terminal(
        &mut self,
        run_id: &AgentRunId,
        kind: RunCheckpointKind,
        payload: Option<serde_json::Value>,
    ) -> Result<RunCheckpoint, RunJournalError> {
        if !kind.is_terminal() {
            return Err(RunJournalError::InvalidSequence {
                expected: 0,
                actual: 0,
            });
        }

        let run = self
            .runs
            .get_mut(run_id)
            .ok_or_else(|| RunJournalError::RunNotFound(run_id.clone()))?;

        if run.terminal_kind.is_some() {
            return Err(RunJournalError::DuplicateTerminalMarker(run_id.clone()));
        }

        run.terminal_kind = Some(kind);
        let last_seq = run.entries.last().map(|e| e.event_seq).unwrap_or(0);

        let checkpoint = RunCheckpoint {
            run_id: run_id.clone(),
            session_id: run.session_id.clone(),
            last_event_seq: last_seq,
            kind,
            classification: RestartClassification::NeedsUserResume,
            state_payload: payload,
            timestamp: 0,
        };

        run.checkpoints.push(checkpoint.clone());
        if self.is_persist_on_append {
            self.flush()?;
        }
        Ok(checkpoint)
    }

    /// Classifies an individual entry for restart safety.
    pub fn classify(&self, entry: &RunJournalEntry) -> RestartClassification {
        classify_entry(entry)
    }

    /// Generates a restart resume plan.
    /// Evaluates all past steps: recoverable ones (wait / read-only) are marked safe,
    /// while uncertain external mutations are flagged for user decision and NEVER auto-retried.
    pub fn resume_plan(&self, run_id: &AgentRunId) -> Result<ResumePlan, RunJournalError> {
        let run = self
            .runs
            .get(run_id)
            .ok_or_else(|| RunJournalError::RunNotFound(run_id.clone()))?;

        let is_terminal = run.terminal_kind.is_some();
        let mut recoverable_steps = Vec::new();
        let mut uncertain_steps = Vec::new();

        for entry in &run.entries {
            let classification = self.classify(entry);
            match classification {
                RestartClassification::Recoverable => {
                    recoverable_steps.push(ResumeStep {
                        event_seq: entry.event_seq,
                        event_name: entry.event_name.clone(),
                        classification,
                        user_decision_required: false,
                        reason: "Operation is read-only or safe wait state".to_string(),
                    });
                }
                RestartClassification::NeedsUserResume => {
                    uncertain_steps.push(ResumeStep {
                        event_seq: entry.event_seq,
                        event_name: entry.event_name.clone(),
                        classification,
                        user_decision_required: true,
                        reason: "External side-effect or uncertain mutation occurred".to_string(),
                    });
                }
            }
        }

        // Check for pending approval or uncertain checkpoint states
        for cp in &run.checkpoints {
            if cp.kind == RunCheckpointKind::WaitingApproval
                || cp.classification == RestartClassification::NeedsUserResume
            {
                uncertain_steps.push(ResumeStep {
                    event_seq: cp.last_event_seq,
                    event_name: format!("checkpoint_{:?}", cp.kind).to_ascii_lowercase(),
                    classification: RestartClassification::NeedsUserResume,
                    user_decision_required: true,
                    reason: format!("Run is in pending checkpoint state: {:?}", cp.kind),
                });
            }
        }

        // Terminal runs or runs containing uncertain external mutations CANNOT be auto-resumed
        let can_auto_resume = !is_terminal && uncertain_steps.is_empty();

        Ok(ResumePlan {
            run_id: run_id.clone(),
            is_terminal,
            can_auto_resume,
            recoverable_steps,
            uncertain_steps,
        })
    }

    pub fn get_entries(&self, run_id: &AgentRunId) -> Result<&[RunJournalEntry], RunJournalError> {
        self.runs
            .get(run_id)
            .map(|r| r.entries.as_slice())
            .ok_or_else(|| RunJournalError::RunNotFound(run_id.clone()))
    }

    pub fn runs(&self) -> &HashMap<AgentRunId, RunRecord> {
        &self.runs
    }

    /// Returns the last compaction event metadata for the given run or session, if any.
    pub fn last_compaction_info(&self, run_id: &AgentRunId) -> Option<LastCompactionInfo> {
        let run = self.runs.get(run_id)?;
        self.last_compaction_info_for_record(run)
    }

    /// Returns the last compaction event metadata for any run in the given session.
    pub fn last_compaction_info_for_session(&self, session_id: &str) -> Option<LastCompactionInfo> {
        for run in self.runs.values() {
            if run.session_id == session_id {
                if let Some(info) = self.last_compaction_info_for_record(run) {
                    return Some(info);
                }
            }
        }
        None
    }

    fn last_compaction_info_for_record(&self, run: &RunRecord) -> Option<LastCompactionInfo> {
        for entry in run.entries.iter().rev() {
            if entry.event_name == "compacting" || entry.event_name == "context_compaction" {
                if let Ok(val) = serde_json::from_str::<serde_json::Value>(&entry.payload_json) {
                    let triggered_at = val
                        .get("triggered_at")
                        .and_then(|v| v.as_u64())
                        .or_else(|| val.get("compacted_at").and_then(|v| v.as_u64()))
                        .unwrap_or(entry.timestamp);

                    let dropped_turns = val
                        .get("dropped_turns")
                        .or_else(|| val.get("dropped_indices"))
                        .and_then(|v| serde_json::from_value::<Vec<usize>>(v.clone()).ok())
                        .unwrap_or_default();

                    let kept_markers = val
                        .get("kept_markers")
                        .or_else(|| val.get("preserved_keys"))
                        .and_then(|v| serde_json::from_value::<Vec<String>>(v.clone()).ok())
                        .unwrap_or_default();

                    let session_id = val
                        .get("session_id")
                        .and_then(|v| v.as_str())
                        .unwrap_or(&run.session_id)
                        .to_string();

                    return Some(LastCompactionInfo {
                        triggered_at,
                        dropped_turns,
                        kept_markers,
                        session_id,
                    });
                }
            }
        }
        None
    }

    pub fn get_record(&self, run_id: &AgentRunId) -> Option<&RunRecord> {
        self.runs.get(run_id)
    }

    pub fn list_runs(&self) -> Vec<&RunRecord> {
        self.runs.values().collect()
    }

    pub fn get_checkpoints(
        &self,
        run_id: &AgentRunId,
    ) -> Result<&[RunCheckpoint], RunJournalError> {
        self.runs
            .get(run_id)
            .map(|r| r.checkpoints.as_slice())
            .ok_or_else(|| RunJournalError::RunNotFound(run_id.clone()))
    }

    pub fn is_terminal(&self, run_id: &AgentRunId) -> Result<bool, RunJournalError> {
        self.runs
            .get(run_id)
            .map(|r| r.terminal_kind.is_some())
            .ok_or_else(|| RunJournalError::RunNotFound(run_id.clone()))
    }

    pub fn to_json(&self) -> Result<String, RunJournalError> {
        serde_json::to_string(self).map_err(|e| RunJournalError::SerializationError(e.to_string()))
    }

    pub fn from_json(json: &str) -> Result<Self, RunJournalError> {
        serde_json::from_str(json).map_err(|e| RunJournalError::SerializationError(e.to_string()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new_run_monotonic_sequence_and_terminal_rejection() {
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-101");
        journal.start_run(run_id.clone(), "session-abc").unwrap();

        // 1. Seq starts at 1 and increments monotonically
        let e1 = journal
            .append_event(&run_id, "status", r#"{"state":"started"}"#)
            .unwrap();
        assert_eq!(e1.event_seq, 1);
        assert_eq!(e1.run_id, run_id);

        let e2 = journal
            .append_event(&run_id, "read", r#"{"url":"https://example.com"}"#)
            .unwrap();
        assert_eq!(e2.event_seq, 2);

        let e3 = journal
            .append_event(&run_id, "progress", r#"{"percent":50}"#)
            .unwrap();
        assert_eq!(e3.event_seq, 3);

        // 2. Record terminal marker (Completed)
        let term = journal
            .record_terminal(&run_id, RunCheckpointKind::Completed, None)
            .unwrap();
        assert_eq!(term.kind, RunCheckpointKind::Completed);
        assert_eq!(term.last_event_seq, 3);

        // 3. Duplicate terminal marker rejected
        let dup_err = journal.record_terminal(&run_id, RunCheckpointKind::Failed, None);
        assert_eq!(
            dup_err.unwrap_err(),
            RunJournalError::DuplicateTerminalMarker(run_id.clone())
        );

        // 4. Appending after terminal rejected
        let post_err = journal.append_event(&run_id, "status", "{}");
        assert_eq!(
            post_err.unwrap_err(),
            RunJournalError::RunAlreadyTerminal(run_id.clone())
        );
    }

    #[test]
    fn test_checkpoint_classification_and_resume_plan() {
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-mutation-test");
        journal.start_run(run_id.clone(), "session-xyz").unwrap();

        // Event 1: Read-only op -> Recoverable
        let e1 = journal
            .append_event(&run_id, "history_search", r#"{"query":"docs"}"#)
            .unwrap();
        assert_eq!(journal.classify(&e1), RestartClassification::Recoverable);

        // Event 2: Wait op -> Recoverable
        let e2 = journal
            .append_event(&run_id, "wait_for_event", r#"{"deadline":1000}"#)
            .unwrap();
        assert_eq!(journal.classify(&e2), RestartClassification::Recoverable);

        // Event 3: External mutation -> NeedsUserResume (uncertain external state)
        let e3 = journal
            .append_event(&run_id, "fs_write", r#"{"path":"/tmp/out.txt"}"#)
            .unwrap();
        assert_eq!(
            journal.classify(&e3),
            RestartClassification::NeedsUserResume
        );

        // Event 4: Mail send -> NeedsUserResume
        let e4 = journal
            .append_event(&run_id, "mail_send", r#"{"to":"alice@example.com"}"#)
            .unwrap();
        assert_eq!(
            journal.classify(&e4),
            RestartClassification::NeedsUserResume
        );

        // Resume plan inspects all steps and strictly forbids auto-retry if uncertain mutation exists
        let plan = journal.resume_plan(&run_id).unwrap();
        assert!(!plan.is_terminal);
        assert!(
            !plan.can_auto_resume,
            "Journal must NEVER auto-retry uncertain mutations"
        );
        assert_eq!(plan.recoverable_steps.len(), 2);
        assert_eq!(plan.uncertain_steps.len(), 2);
        assert!(plan
            .uncertain_steps
            .iter()
            .all(|s| s.user_decision_required));
    }

    #[test]
    fn test_restart_simulation_json_persistence_and_no_auto_execution() {
        let mut journal = RunJournal::new();

        // Run 1: Clean completed run
        let completed_run = AgentRunId::new("run-completed");
        journal.start_run(completed_run.clone(), "sess-1").unwrap();
        journal
            .append_event(&completed_run, "status", r#"{"msg":"start"}"#)
            .unwrap();
        journal
            .append_event(&completed_run, "read", r#"{"target":"page"}"#)
            .unwrap();
        journal
            .record_terminal(&completed_run, RunCheckpointKind::Completed, None)
            .unwrap();

        // Run 2: In-flight uncertain mutation interrupted by crash/restart
        let uncertain_run = AgentRunId::new("run-uncertain");
        journal.start_run(uncertain_run.clone(), "sess-2").unwrap();
        journal
            .append_event(&uncertain_run, "page_context", r#"{"title":"Form"}"#)
            .unwrap();
        journal
            .append_event(
                &uncertain_run,
                "click_element",
                "{\"selector\":\"#submit-payment\"}",
            )
            .unwrap();

        // Persist journal to JSON (simulating disk storage across restart)
        let json_dump = journal.to_json().expect("Serialization must succeed");

        // Simulate new process / browser restart: load journal from JSON
        let reloaded = RunJournal::from_json(&json_dump).expect("Deserialization must succeed");

        // Assert completed run history is completely intact and marked terminal
        let completed_plan = reloaded.resume_plan(&completed_run).unwrap();
        assert!(completed_plan.is_terminal);
        assert!(!completed_plan.can_auto_resume);

        // Assert uncertain run is NOT auto-executed and flags uncertain step
        let uncertain_plan = reloaded.resume_plan(&uncertain_run).unwrap();
        assert!(!uncertain_plan.is_terminal);
        assert!(
            !uncertain_plan.can_auto_resume,
            "Uncertain interrupted run MUST NOT auto-execute"
        );
        assert_eq!(uncertain_plan.uncertain_steps.len(), 1);
        assert_eq!(
            uncertain_plan.uncertain_steps[0].event_name,
            "click_element"
        );
        assert!(uncertain_plan.uncertain_steps[0].user_decision_required);
    }

    #[test]
    fn test_pure_read_only_run_can_auto_resume() {
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-pure-read");
        journal.start_run(run_id.clone(), "sess-read").unwrap();

        journal
            .append_event(&run_id, "status", r#"{"state":"started"}"#)
            .unwrap();
        journal
            .append_event(&run_id, "web_search", r#"{"query":"weather"}"#)
            .unwrap();
        journal
            .append_event(&run_id, "page_context", r#"{"url":"https://example.com"}"#)
            .unwrap();
        journal
            .append_event(&run_id, "wait_for_event", r#"{"event":"dom_ready"}"#)
            .unwrap();

        let plan = journal.resume_plan(&run_id).unwrap();
        assert!(!plan.is_terminal);
        assert!(
            plan.can_auto_resume,
            "Pure read-only and wait states are safe to auto-resume"
        );
        assert_eq!(plan.recoverable_steps.len(), 4);
        assert_eq!(plan.uncertain_steps.len(), 0);
        assert!(!plan.requires_user_decision());
    }

    #[test]
    fn test_failed_terminal_marker_and_duplicate_rejection() {
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-fail");
        journal.start_run(run_id.clone(), "sess-fail").unwrap();

        journal.append_event(&run_id, "started", "{}").unwrap();
        let term = journal
            .record_terminal(
                &run_id,
                RunCheckpointKind::Failed,
                Some(serde_json::json!({"error": "network timeout"})),
            )
            .unwrap();
        assert_eq!(term.kind, RunCheckpointKind::Failed);

        let dup = journal.record_terminal(&run_id, RunCheckpointKind::Completed, None);
        assert_eq!(
            dup.unwrap_err(),
            RunJournalError::DuplicateTerminalMarker(run_id.clone())
        );

        assert!(journal.is_terminal(&run_id).unwrap());
    }

    #[test]
    fn test_fail_closed_unknown_event_classification() {
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-unknown");
        journal.start_run(run_id.clone(), "sess-unknown").unwrap();

        // Any unknown operation defaults to NeedsUserResume (fail-closed)
        let unk = journal
            .append_event(&run_id, "custom_arbitrary_plugin_op", "{}")
            .unwrap();
        assert_eq!(
            journal.classify(&unk),
            RestartClassification::NeedsUserResume
        );

        let plan = journal.resume_plan(&run_id).unwrap();
        assert!(!plan.can_auto_resume);
        assert_eq!(plan.uncertain_steps.len(), 1);
    }

    #[test]
    fn test_journal_with_fs_store_drop_and_reload_events_intact() {
        let temp_dir =
            std::env::temp_dir().join(format!("maho_journal_test_{}", uuid::Uuid::new_v4()));
        let store_path = temp_dir.join("journal.json");
        let store = Arc::new(FsJsonStore::new(&store_path));

        let run_id = AgentRunId::new("run-durable-1");
        {
            let mut journal =
                RunJournal::new().with_store(Arc::clone(&store) as Arc<dyn JournalStore>);
            journal.persist_on_append();

            journal
                .start_run(run_id.clone(), "session-durable")
                .unwrap();
            journal
                .append_event(&run_id, "status", r#"{"state":"started"}"#)
                .unwrap();
            journal
                .append_event(&run_id, "web_search", r#"{"query":"rust persistence"}"#)
                .unwrap();
            journal
                .append_event(&run_id, "fs_write", r#"{"path":"/tmp/test.txt"}"#)
                .unwrap();
            // Drop journal here
        }

        // Reload from store
        let reloaded = RunJournal::load_from_store(store).expect("Reload from store must succeed");
        let entries = reloaded
            .get_entries(&run_id)
            .expect("Run entries must exist");
        assert_eq!(entries.len(), 3);
        assert_eq!(entries[0].event_seq, 1);
        assert_eq!(entries[0].event_name, "status");
        assert_eq!(entries[1].event_seq, 2);
        assert_eq!(entries[1].event_name, "web_search");
        assert_eq!(entries[2].event_seq, 3);
        assert_eq!(entries[2].event_name, "fs_write");

        let _ = std::fs::remove_dir_all(temp_dir);
    }

    #[test]
    fn test_approval_pending_restart_classification_needs_user_resume() {
        let temp_dir =
            std::env::temp_dir().join(format!("maho_approval_test_{}", uuid::Uuid::new_v4()));
        let store_path = temp_dir.join("journal.json");
        let store = Arc::new(FsJsonStore::new(&store_path));

        let run_id = AgentRunId::new("run-approval-pending");
        {
            let mut journal =
                RunJournal::new().with_store(Arc::clone(&store) as Arc<dyn JournalStore>);
            journal.persist_on_append();

            journal
                .start_run(run_id.clone(), "session-approval")
                .unwrap();
            journal
                .append_event(&run_id, "status", r#"{"state":"started"}"#)
                .unwrap();
            journal
                .append_event(&run_id, "page_context", r#"{"url":"https://bank.com"}"#)
                .unwrap();

            // Record WaitingApproval checkpoint
            journal
                .record_checkpoint(RunCheckpoint {
                    run_id: run_id.clone(),
                    session_id: "session-approval".to_string(),
                    last_event_seq: 2,
                    kind: RunCheckpointKind::WaitingApproval,
                    classification: RestartClassification::NeedsUserResume,
                    state_payload: Some(serde_json::json!({
                        "action": "transfer_funds",
                        "requires_approval": true
                    })),
                    timestamp: 100,
                })
                .unwrap();
            // Drop journal here
        }

        // Reload from store
        let reloaded = RunJournal::load_from_store(store).expect("Reload must succeed");
        let plan = reloaded
            .resume_plan(&run_id)
            .expect("Resume plan generation must succeed");

        assert!(
            !plan.is_terminal,
            "Run waiting for approval is not terminal"
        );
        assert!(
            !plan.can_auto_resume,
            "Run in WaitingApproval state MUST NEVER auto-resume"
        );
        assert!(plan.requires_user_decision(), "Must require user decision");
        assert!(
            plan.uncertain_steps
                .iter()
                .any(|step| step.classification == RestartClassification::NeedsUserResume),
            "Classification must return NeedsUserResume"
        );

        let _ = std::fs::remove_dir_all(temp_dir);
    }

    #[test]
    fn test_helper_constructors_and_automatic_fs_write_through() {
        let temp_dir =
            std::env::temp_dir().join(format!("maho_helper_test_{}", uuid::Uuid::new_v4()));
        let session_id = "session-helper-test";
        let store_path = temp_dir.join(format!("{session_id}.journal.json"));

        // 1. Test `RunJournal::with_fs_store` and automatic persistence on event append
        let run_id = AgentRunId::new(session_id);
        {
            let mut journal = RunJournal::with_fs_store(&store_path);
            assert!(journal.has_store());
            assert!(journal.is_persist_on_append());

            journal.start_run(run_id.clone(), session_id).unwrap();
            assert!(
                store_path.exists(),
                "File must exist immediately after start_run"
            );

            journal
                .append_event(&run_id, "step_started", r#"{"role":"user"}"#)
                .unwrap();
            journal
                .append_event(&run_id, "web_search", r#"{"query":"rust"}"#)
                .unwrap();
            journal
                .record_checkpoint(RunCheckpoint {
                    run_id: run_id.clone(),
                    session_id: session_id.to_string(),
                    last_event_seq: 2,
                    kind: RunCheckpointKind::StepProgress,
                    classification: RestartClassification::Recoverable,
                    state_payload: None,
                    timestamp: 10,
                })
                .unwrap();
        }

        // 2. Test `RunJournal::from_fs_path_or_new` restores the persisted state
        {
            let mut journal = RunJournal::from_fs_path_or_new(&store_path);
            assert!(journal.has_store());
            assert!(journal.is_persist_on_append());
            let entries = journal.get_entries(&run_id).unwrap();
            assert_eq!(entries.len(), 2);
            assert_eq!(entries[0].event_name, "step_started");
            assert_eq!(entries[1].event_name, "web_search");

            let checkpoints = journal.get_checkpoints(&run_id).unwrap();
            assert_eq!(checkpoints.len(), 1);
            assert_eq!(checkpoints[0].kind, RunCheckpointKind::StepProgress);

            // Append another event and check that disk file updates
            journal
                .append_event(&run_id, "status", r#"{"status":"completed"}"#)
                .unwrap();
        }

        // 3. Test `RunJournal::for_session` helper
        {
            let journal = RunJournal::for_session(&temp_dir, session_id);
            assert!(journal.has_store());
            assert!(journal.is_persist_on_append());
            // Since it points to the same file path, loading from store will recover 3 entries
            let loaded = RunJournal::from_fs_path_or_new(
                temp_dir.join(format!("{session_id}.journal.json")),
            );
            let entries = loaded.get_entries(&run_id).unwrap();
            assert_eq!(entries.len(), 3);
        }

        let _ = std::fs::remove_dir_all(temp_dir);
    }
}
