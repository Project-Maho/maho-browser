use serde::{Deserialize, Serialize};

/// Subscription tier for gating feature access.
#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum UserTier {
    Free,
    Pro,
    Max,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum AuthState {
    Unauthenticated,
    Authenticating,
    Authenticated,
    TokenRefreshing,
    Error { message: String },
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AccountInfo {
    pub user_id: String,
    pub email: String,
    pub display_name: Option<String>,
    pub avatar_url: Option<String>,
    pub device_id: Option<String>,
    pub sync_enabled: bool,
    pub auth_state: AuthState,
    /// Subscription tier. Defaults to Free if not set by the relay.
    #[serde(default = "default_tier")]
    pub tier: UserTier,
}

fn default_tier() -> UserTier {
    UserTier::Free
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum SyncState {
    Idle,
    Syncing { progress: f64 },
    Synced { last_sync_at: String },
    Error { message: String },
    Offline,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn account_info_serializes_with_auth_state() {
        let account = AccountInfo {
            user_id: "user_123".to_string(),
            email: "user@example.com".to_string(),
            display_name: Some("Maho User".to_string()),
            avatar_url: Some("https://example.com/avatar.png".to_string()),
            device_id: Some("device_abc".to_string()),
            sync_enabled: true,
            auth_state: AuthState::Authenticated,
            tier: UserTier::Max,
        };

        let json = serde_json::to_value(&account).expect("account should serialize");
        assert_eq!(json["userId"], "user_123");
        assert_eq!(json["authState"]["kind"], "authenticated");
    }

    #[test]
    fn auth_state_round_trips() {
        let state = AuthState::Error {
            message: "token expired".to_string(),
        };
        let json = serde_json::to_string(&state).expect("state should serialize");
        let decoded: AuthState = serde_json::from_str(&json).expect("state should deserialize");
        assert!(json.contains("error"));
        assert_eq!(decoded, state);
    }
}
