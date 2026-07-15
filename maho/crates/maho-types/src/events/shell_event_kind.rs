use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum ShellEventKind {
    Structural,
    Cosmetic,
}

impl ShellEventKind {
    pub fn classify(event_name: &str) -> Self {
        match event_name {
            "tab_title_updated"
            | "tab_url_updated"
            | "tab_favicon_updated"
            | "tab_loading_state_changed"
            | "update_tab_scroll_position"
            | "update_tab_preview" => Self::Cosmetic,
            _ => Self::Structural, // safe default: structural triggers refresh
        }
    }
}
