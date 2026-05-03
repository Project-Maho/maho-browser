use serde::{Deserialize, Serialize};

use crate::common::{DateTime, Url};
use crate::identifiers::{NoteId, TabId};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Note {
    pub id: NoteId,
    pub linked_tab_id: Option<TabId>,
    pub linked_url: Option<Url>,
    pub content: String,
    pub created_at: DateTime,
    pub updated_at: DateTime,
}
