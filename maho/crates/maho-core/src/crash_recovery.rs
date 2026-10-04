use serde::Serialize;
use std::collections::HashMap;

use maho_types::common::DateTime;
use maho_types::tab::TabRole;
use maho_types::traits::shell_renderer::{SpaceViewModel, TabViewModel};

#[derive(Serialize)]
pub struct SessionState {
    pub spaces: Vec<SpaceSnapshot>,
    pub active_space_id: String,
    pub timestamp: DateTime,
}

#[derive(Serialize)]
pub struct SpaceSnapshot {
    pub id: String,
    pub name: String,
    pub tabs: Vec<CrashTabSnapshot>,
}

#[derive(Serialize)]
pub struct CrashTabSnapshot {
    pub id: String,
    pub url: String,
    pub title: String,
    pub is_pinned: bool,
    pub role: TabRole,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub form_data: Option<String>,
}

pub struct CrashRecoveryManager {
    last_session: Option<SessionState>,
    auto_save_interval_secs: u64,
    dirty: bool,
    form_data: HashMap<String, String>,
}

impl Default for CrashRecoveryManager {
    fn default() -> Self {
        Self::new()
    }
}

impl CrashRecoveryManager {
    pub fn new() -> Self {
        Self {
            last_session: None,
            auto_save_interval_secs: 60,
            dirty: false,
            form_data: HashMap::new(),
        }
    }

    pub fn capture_state(
        spaces: &[SpaceViewModel],
        active_space_id: &str,
        tabs: &[TabViewModel],
        form_data_map: &HashMap<String, String>,
    ) -> SessionState {
        let space_snapshots: Vec<SpaceSnapshot> = spaces
            .iter()
            .map(|space| {
                let space_tabs: Vec<CrashTabSnapshot> = tabs
                    .iter()
                    .filter(|tab| tab.space_id.0 == space.id.0 && !tab.is_private)
                    .map(|tab| CrashTabSnapshot {
                        id: tab.id.0.clone(),
                        url: tab.url.clone(),
                        title: tab.title.clone(),
                        is_pinned: tab.role.is_pinned(),
                        role: tab.role.clone(),
                        form_data: form_data_map.get(&tab.id.0).cloned(),
                    })
                    .collect();

                SpaceSnapshot {
                    id: space.id.0.clone(),
                    name: space.name.clone(),
                    tabs: space_tabs,
                }
            })
            .collect();

        SessionState {
            spaces: space_snapshots,
            active_space_id: active_space_id.to_string(),
            timestamp: DateTime::now(),
        }
    }

    pub fn save_session(&mut self, state: SessionState) {
        self.last_session = Some(state);
        self.dirty = false;
    }

    pub fn get_last_session(&self) -> Option<&SessionState> {
        self.last_session.as_ref()
    }

    pub fn clear_session(&mut self) {
        self.last_session = None;
        self.dirty = false;
    }

    pub fn has_pending_restore(&self) -> bool {
        self.last_session.is_some()
    }

    pub fn mark_dirty(&mut self) {
        self.dirty = true;
    }

    pub fn needs_save(&self) -> bool {
        self.dirty
    }

    pub fn auto_save_interval_secs(&self) -> u64 {
        self.auto_save_interval_secs
    }

    pub fn save_form_data(&mut self, tab_id: &str, form_json: String) {
        self.form_data.insert(tab_id.to_string(), form_json);
        self.dirty = true;
    }

    pub fn get_form_data(&self, tab_id: &str) -> Option<&str> {
        self.form_data.get(tab_id).map(|s| s.as_str())
    }

    pub fn set_save_interval(&mut self, seconds: u64) {
        self.auto_save_interval_secs = seconds;
    }

    pub fn clear_form_data(&mut self, tab_id: &str) {
        self.form_data.remove(tab_id);
    }
}
