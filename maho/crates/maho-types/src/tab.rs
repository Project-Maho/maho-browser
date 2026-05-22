use serde::{Deserialize, Serialize};

use crate::common::{DateTime, ImageData, ScrollPosition, TabSnapshot, Url};
use crate::identifiers::{SpaceId, TabId};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum TabLifecycleState {
    Active,
    Frozen,
    Suspended { snapshot: TabSnapshot },
    Archived { metadata_only: bool },
}

impl TabLifecycleState {
    pub fn kind_str(&self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Frozen => "frozen",
            Self::Suspended { .. } => "suspended",
            Self::Archived { .. } => "archived",
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Tab {
    pub id: TabId,
    pub parent_id: Option<TabId>,
    pub space_id: SpaceId,
    pub url: Url,
    pub title: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub custom_title: Option<String>,
    pub favicon: Option<ImageData>,
    pub state: TabLifecycleState,
    pub is_pinned: bool,
    pub is_favorite: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub favorite_order: Option<u32>,
    pub is_muted: bool,
    pub zoom_level: f64,
    pub created_at: DateTime,
    pub last_active_at: DateTime,
    pub scroll_position: ScrollPosition,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub pinned_url: Option<Url>,
    /// Identifies the browser window that owns this tab. `None` means the tab
    /// is shared across all windows (e.g. pinned/favorite tabs). When set, the
    /// tab belongs exclusively to the window with this session ID.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub window_id: Option<i64>,
}
