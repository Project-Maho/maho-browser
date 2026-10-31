//! Agent-created automations: a prompt re-run on a fixed interval.
//!
//! The agent's tool server writes them; the browser's routines tick fires
//! the due ones. Both sides share one JSON file in the agent directory, so
//! the store is written atomically (temp file + rename).

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

pub const MIN_INTERVAL_SECS: i64 = 60;
pub const MAX_INTERVAL_SECS: i64 = 7 * 24 * 60 * 60;
pub const MAX_AUTOMATIONS_PER_TASK: usize = 10;
pub const STORE_FILE_NAME: &str = "automations.json";

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Automation {
    pub id: String,
    pub task_id: String,
    pub name: String,
    pub prompt: String,
    pub interval_secs: i64,
    pub created_at: i64,
    /// Epoch seconds of the last fire; `None` until it first runs.
    #[serde(default)]
    pub last_run: Option<i64>,
}

impl Automation {
    pub fn next_fire(&self) -> i64 {
        self.last_run.unwrap_or(self.created_at) + self.interval_secs
    }
}

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum AutomationError {
    #[error("invalid automation: {0}")]
    Invalid(String),
    #[error("task {0} already has {MAX_AUTOMATIONS_PER_TASK} automations")]
    TaskLimit(String),
    #[error("automation {0} not found")]
    NotFound(String),
    #[error("automation store: {0}")]
    Store(String),
}

pub struct NewAutomation<'a> {
    pub task_id: &'a str,
    pub name: &'a str,
    pub prompt: &'a str,
    pub interval_secs: i64,
}

pub struct AutomationStore {
    path: PathBuf,
}

impl AutomationStore {
    pub fn new(path: impl Into<PathBuf>) -> Self {
        Self { path: path.into() }
    }

    pub fn in_agent_dir(agent_dir: &Path) -> Self {
        Self::new(agent_dir.join(STORE_FILE_NAME))
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn list(&self) -> Result<Vec<Automation>, AutomationError> {
        match std::fs::read(&self.path) {
            Ok(bytes) => serde_json::from_slice(&bytes)
                .map_err(|error| AutomationError::Store(format!("{}: {error}", self.path.display()))),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(Vec::new()),
            Err(error) => Err(AutomationError::Store(error.to_string())),
        }
    }

    fn save(&self, automations: &[Automation]) -> Result<(), AutomationError> {
        let store_error = |error: std::io::Error| AutomationError::Store(error.to_string());
        if let Some(parent) = self.path.parent() {
            std::fs::create_dir_all(parent).map_err(store_error)?;
        }
        let bytes = serde_json::to_vec_pretty(automations)
            .map_err(|error| AutomationError::Store(error.to_string()))?;
        let temp = self.path.with_extension(format!("json.tmp-{}", std::process::id()));
        std::fs::write(&temp, bytes).map_err(store_error)?;
        std::fs::rename(&temp, &self.path).map_err(store_error)
    }

    pub fn create(&self, new: NewAutomation<'_>, now: i64) -> Result<Automation, AutomationError> {
        if new.task_id.trim().is_empty() {
            return Err(AutomationError::Invalid("task_id is required".into()));
        }
        if new.name.trim().is_empty() || new.prompt.trim().is_empty() {
            return Err(AutomationError::Invalid("name and prompt are required".into()));
        }
        if !(MIN_INTERVAL_SECS..=MAX_INTERVAL_SECS).contains(&new.interval_secs) {
            return Err(AutomationError::Invalid(format!(
                "interval must be between {MIN_INTERVAL_SECS}s (1 minute) and {MAX_INTERVAL_SECS}s (7 days)"
            )));
        }
        let mut automations = self.list()?;
        if automations.iter().filter(|a| a.task_id == new.task_id).count() >= MAX_AUTOMATIONS_PER_TASK {
            return Err(AutomationError::TaskLimit(new.task_id.to_string()));
        }
        let automation = Automation {
            id: uuid::Uuid::new_v4().to_string(),
            task_id: new.task_id.to_string(),
            name: new.name.trim().to_string(),
            prompt: new.prompt.to_string(),
            interval_secs: new.interval_secs,
            created_at: now,
            last_run: None,
        };
        automations.push(automation.clone());
        self.save(&automations)?;
        Ok(automation)
    }

    pub fn delete(&self, id: &str) -> Result<(), AutomationError> {
        let mut automations = self.list()?;
        let before = automations.len();
        automations.retain(|a| a.id != id);
        if automations.len() == before {
            return Err(AutomationError::NotFound(id.to_string()));
        }
        self.save(&automations)
    }

    /// Returns the automations due at `now` and records them as fired, so a
    /// second tick in the same interval never fires them again.
    pub fn take_due(&self, now: i64) -> Result<Vec<Automation>, AutomationError> {
        let mut automations = self.list()?;
        let mut due = Vec::new();
        for automation in &mut automations {
            if now >= automation.next_fire() {
                automation.last_run = Some(now);
                due.push(automation.clone());
            }
        }
        if !due.is_empty() {
            self.save(&automations)?;
        }
        Ok(due)
    }
}

pub fn build_automation_prompt(automation: &Automation) -> String {
    format!(
        "You are a browser assistant running the automation '{}' (task {}), \
         scheduled every {} minutes.\nInstructions:\n{}",
        automation.name,
        automation.task_id,
        automation.interval_secs / 60,
        automation.prompt
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn store() -> (tempfile::TempDir, AutomationStore) {
        let dir = tempfile::tempdir().unwrap();
        let store = AutomationStore::in_agent_dir(dir.path());
        (dir, store)
    }

    fn new<'a>(task: &'a str, interval_secs: i64) -> NewAutomation<'a> {
        NewAutomation { task_id: task, name: "Check inbox", prompt: "Summarize new mail", interval_secs }
    }

    #[test]
    fn due_automations_fire_once_per_interval_with_a_fake_clock() {
        let (_dir, store) = store();
        let t0 = 1_800_000_000;
        let hourly = store.create(new("task-1", 3600), t0).unwrap();
        let minutely = store.create(new("task-1", 60), t0).unwrap();

        assert!(store.take_due(t0 + 59).unwrap().is_empty());
        let due = store.take_due(t0 + 60).unwrap();
        assert_eq!(due.iter().map(|a| a.id.as_str()).collect::<Vec<_>>(), [minutely.id.as_str()]);
        assert!(store.take_due(t0 + 61).unwrap().is_empty(), "same interval never refires");

        let due = store.take_due(t0 + 3600).unwrap();
        assert_eq!(due.len(), 2);
        assert!(due.iter().any(|a| a.id == hourly.id));
        let persisted = AutomationStore::new(store.path()).list().unwrap();
        assert!(persisted.iter().all(|a| a.last_run == Some(t0 + 3600)));
    }

    #[test]
    fn bounds_and_limits_are_enforced() {
        let (_dir, store) = store();
        for interval in [0, 59, MAX_INTERVAL_SECS + 1, -60] {
            assert!(matches!(store.create(new("t", interval), 0), Err(AutomationError::Invalid(_))));
        }
        assert!(store.create(new("t", MIN_INTERVAL_SECS), 0).is_ok());
        assert!(store.create(new("t", MAX_INTERVAL_SECS), 0).is_ok());
        assert!(matches!(store.create(new("", 60), 0), Err(AutomationError::Invalid(_))));
        let blank = NewAutomation { task_id: "t", name: " ", prompt: "x", interval_secs: 60 };
        assert!(matches!(store.create(blank, 0), Err(AutomationError::Invalid(_))));

        for _ in 0..8 {
            store.create(new("t", 60), 0).unwrap();
        }
        assert!(matches!(store.create(new("t", 60), 0), Err(AutomationError::TaskLimit(_))));
        assert!(store.create(new("other", 60), 0).is_ok(), "the cap is per task");

        let first = store.list().unwrap()[0].id.clone();
        store.delete(&first).unwrap();
        assert!(matches!(store.delete(&first), Err(AutomationError::NotFound(_))));
        assert!(store.create(new("t", 60), 0).is_ok());
    }

    #[test]
    fn corrupt_store_is_an_error_not_an_empty_list() {
        let (_dir, store) = store();
        std::fs::write(store.path(), b"{not json").unwrap();
        assert!(matches!(store.list(), Err(AutomationError::Store(_))));
        assert!(store.create(new("t", 60), 0).is_err());
    }
}
