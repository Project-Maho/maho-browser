use crate::sync_models::SyncEntity;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Snapshot {
    pub schema_version: u8,
    pub hlc_ts_ceiling: u64,
    pub created_at: String,
    pub creator_device_id: u32,
    pub entities: HashMap<String, Vec<SyncEntity>>,
}
