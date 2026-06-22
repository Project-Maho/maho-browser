use std::future::Future;
use std::pin::Pin;

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct PermissionRequest {
    pub tool_name: String,
    pub arguments: String,
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub enum PermissionDecision {
    Allow,
    Deny,
}

pub type PermissionCallback = Box<
    dyn Fn(PermissionRequest) -> Pin<Box<dyn Future<Output = PermissionDecision> + Send>>
        + Send
        + Sync
        + 'static,
>;
