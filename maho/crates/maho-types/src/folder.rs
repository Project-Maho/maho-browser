use serde::{Deserialize, Serialize};

use crate::identifiers::{FolderId, TabId};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Folder {
    pub id: FolderId,
    pub name: String,
    pub tab_ids: Vec<TabId>,
    pub is_expanded: bool,
    #[serde(default)]
    pub is_pinned: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub parent_folder_id: Option<FolderId>,
}
