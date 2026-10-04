use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SharedCollection {
    pub id: String,
    pub space_id: String,
    pub name: String,
    pub permission: String,
    pub member_count: u32,
    pub share_link: Option<String>,
    pub created_at: String,
}
