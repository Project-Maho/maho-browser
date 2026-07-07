use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize)]
pub struct PushRequest {
    pub blobs: Vec<String>,
}

#[derive(Debug, Serialize)]
pub struct PushAck {
    pub seq: i64,
}

#[derive(Debug, Serialize)]
pub struct PushResponse {
    pub acks: Vec<PushAck>,
}

#[derive(Debug, Deserialize)]
pub struct PullQuery {
    #[serde(default)]
    pub since_seq: i64,
    #[serde(default = "default_pull_limit")]
    pub limit: usize,
}

#[derive(Debug, Serialize)]
pub struct PulledMessage {
    pub seq: i64,
    pub blob: String,
}

#[derive(Debug, Serialize)]
pub struct PullResponse {
    pub messages: Vec<PulledMessage>,
    pub has_more: bool,
}

const DEFAULT_PULL_LIMIT: usize = 50;

pub const MAX_PULL_LIMIT: usize = 100;

fn default_pull_limit() -> usize {
    DEFAULT_PULL_LIMIT
}

#[derive(Debug, Deserialize)]
pub struct UploadSnapshotRequest {
    pub hlc_ts_ceiling: i64,
    pub creator_device_id: i64,
    pub blob: String, // Base64 encoded blob
}

#[derive(Debug, Serialize)]
pub struct DownloadSnapshotResponse {
    pub hlc_ts_ceiling: i64,
    pub created_at: i64,
    pub creator_device_id: i64,
    pub blob: String, // Base64 encoded blob
}
