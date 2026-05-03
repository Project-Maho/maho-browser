use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SavedPassword {
    pub id: String,
    pub domain: String,
    pub username: String,
    pub created_at: String,
    pub last_used: Option<String>,
    #[serde(default)]
    pub password: Option<String>,
}
