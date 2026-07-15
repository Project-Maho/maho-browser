use serde::{Deserialize, Serialize};

use crate::common::Orientation;
use crate::identifiers::TabId;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SplitPane {
    pub tab_id: TabId,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SplitViewConfig {
    pub panes: Vec<SplitPane>,
    pub orientation: Orientation,
    pub ratios: Vec<f64>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SplitConfig {
    pub tab_ids: Vec<TabId>,
    pub orientation: Orientation,
}

impl SplitConfig {
    pub fn into_split_view_config(self) -> Option<SplitViewConfig> {
        let pane_count = self.tab_ids.len();
        if pane_count == 0 {
            return None;
        }

        let ratios = vec![1.0 / pane_count as f64; pane_count];
        Some(SplitViewConfig {
            panes: self
                .tab_ids
                .into_iter()
                .map(|tab_id| SplitPane { tab_id })
                .collect(),
            orientation: self.orientation,
            ratios,
        })
    }
}

impl SplitViewConfig {
    pub fn is_empty(&self) -> bool {
        self.panes.is_empty()
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "camelCase")]
pub struct StablePaneIdentity {
    pub url: String,
    pub tab_creation_time_micros: u64,
    pub window_session_uuid: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SplitViewPersistConfig {
    pub panes: Vec<StablePaneIdentity>,
    pub orientation: Orientation,
    pub ratios: Vec<f64>,
}
