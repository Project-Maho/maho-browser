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
