use maho_types::account::{AccountInfo, AuthState, SyncState, UserTier};
use serde::{Deserialize, Serialize};

/// FNV-1a hash over raw bytes. Endian-independent, deterministic across
/// architectures. Used for device_id_hash tiebreaks in LWW sync ordering.
pub fn fnv1a_bytes(bytes: &[u8]) -> u32 {
    let mut hash: u32 = 0x811c9dc5;
    for &b in bytes {
        hash ^= b as u32;
        hash = hash.wrapping_mul(0x01000193);
    }
    hash
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct PersistedAccount {
    pub account: Option<AccountInfo>,
    pub device_id: uuid::Uuid,
}

#[derive(Debug)]
pub struct AccountManager {
    account: Option<AccountInfo>,
    sync_state: SyncState,
    device_id: uuid::Uuid,
}

impl Default for AccountManager {
    fn default() -> Self {
        Self::new()
    }
}

impl AccountManager {
    pub fn new() -> Self {
        Self {
            account: None,
            sync_state: SyncState::Idle,
            device_id: uuid::Uuid::nil(),
        }
    }

    pub fn load_or_create(&mut self, data: Option<&[u8]>) -> (PersistedAccount, bool) {
        let mut dirty = false;
        let mut persisted = if let Some(d) = data {
            if let Ok(mut p) = serde_json::from_slice::<PersistedAccount>(d) {
                if p.device_id.is_nil() {
                    p.device_id = uuid::Uuid::new_v4();
                    dirty = true;
                }
                p
            } else {
                dirty = true;
                PersistedAccount {
                    account: None,
                    device_id: uuid::Uuid::new_v4(),
                }
            }
        } else {
            dirty = true;
            PersistedAccount {
                account: None,
                device_id: uuid::Uuid::new_v4(),
            }
        };

        if let Some(ref mut acc) = persisted.account {
            acc.device_id = Some(persisted.device_id.to_string());
        }

        self.account = persisted.account.clone();
        self.device_id = persisted.device_id;
        (persisted, dirty)
    }

    pub fn device_id(&self) -> uuid::Uuid {
        self.device_id
    }

    pub fn device_id_hash(&self) -> u32 {
        fnv1a_bytes(self.device_id.as_bytes())
    }

    pub fn begin_sign_in(
        &mut self,
        email: String,
        display_name: Option<String>,
        user_id: Option<String>,
        device_id: Option<String>,
    ) -> AccountInfo {
        let actual_device_id = if let Some(d) = device_id {
            if let Ok(u) = uuid::Uuid::parse_str(&d) {
                self.device_id = u;
            }
            d
        } else {
            if self.device_id.is_nil() {
                self.device_id = uuid::Uuid::new_v4();
            }
            self.device_id.to_string()
        };
        let account = AccountInfo {
            user_id: user_id.unwrap_or_else(|| email.clone()),
            email,
            display_name,
            avatar_url: None,
            device_id: Some(actual_device_id),
            sync_enabled: false,
            auth_state: AuthState::Authenticating,
            tier: UserTier::Free,
        };
        self.account = Some(account.clone());
        self.sync_state = SyncState::Idle;
        account
    }

    pub fn sign_in(&mut self, mut account: AccountInfo) -> AccountInfo {
        if account.device_id.is_none() {
            if self.device_id.is_nil() {
                self.device_id = uuid::Uuid::new_v4();
            }
            account.device_id = Some(self.device_id.to_string());
        } else if let Some(ref d) = account.device_id {
            if let Ok(u) = uuid::Uuid::parse_str(d) {
                self.device_id = u;
            }
        }
        self.account = Some(account.clone());
        account
    }

    pub fn sign_out(&mut self) -> Option<AccountInfo> {
        let previous = self.account.take();
        self.sync_state = SyncState::Idle;
        previous
    }

    pub fn set_auth_state(&mut self, auth_state: AuthState) -> Option<AccountInfo> {
        let account = self.account.as_mut()?;
        account.auth_state = auth_state;
        Some(account.clone())
    }

    pub fn mark_auth_error(&mut self, message: String) -> Option<AccountInfo> {
        let account = self.account.as_mut()?;
        account.auth_state = AuthState::Error { message };
        Some(account.clone())
    }

    pub fn refresh_token(&mut self) -> Option<AccountInfo> {
        let account = self.account.as_mut()?;
        account.auth_state = AuthState::TokenRefreshing;
        account.auth_state = AuthState::Authenticated;
        Some(account.clone())
    }

    pub fn toggle_sync(&mut self) -> Option<AccountInfo> {
        let account = self.account.as_mut()?;
        account.sync_enabled = !account.sync_enabled;
        if !account.sync_enabled {
            self.sync_state = SyncState::Idle;
        }
        Some(account.clone())
    }

    pub fn set_sync_state(&mut self, sync_state: SyncState) {
        self.sync_state = sync_state;
    }

    pub fn get_account(&self) -> Option<&AccountInfo> {
        self.account.as_ref()
    }

    pub fn get_account_mut(&mut self) -> Option<&mut AccountInfo> {
        self.account.as_mut()
    }

    pub fn get_auth_state(&self) -> AuthState {
        self.account
            .as_ref()
            .map(|account| account.auth_state.clone())
            .unwrap_or(AuthState::Unauthenticated)
    }

    pub fn get_sync_state(&self) -> &SyncState {
        &self.sync_state
    }

    pub fn get_tier(&self) -> Option<UserTier> {
        self.account.as_ref().map(|a| a.tier)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn begin_sign_in_sets_authenticating_state() {
        let mut manager = AccountManager::new();
        let account = manager.begin_sign_in(
            "user@example.com".to_string(),
            Some("Maho User".to_string()),
            Some("user_123".to_string()),
            Some("device_abc".to_string()),
        );

        assert_eq!(account.auth_state, AuthState::Authenticating);
        assert_eq!(manager.get_auth_state(), AuthState::Authenticating);
        assert_eq!(manager.get_account().unwrap().user_id, "user_123");
    }

    #[test]
    fn auth_error_and_refresh_update_account_state() {
        let mut manager = AccountManager::new();
        manager.begin_sign_in("user@example.com".to_string(), None, None, None);

        let errored = manager
            .mark_auth_error("bad credentials".to_string())
            .expect("account should exist");
        assert_eq!(
            errored.auth_state,
            AuthState::Error {
                message: "bad credentials".to_string()
            }
        );

        let refreshed = manager.refresh_token().expect("account should exist");
        assert_eq!(refreshed.auth_state, AuthState::Authenticated);
    }

    #[test]
    fn sign_out_clears_account_and_sync_state() {
        let mut manager = AccountManager::new();
        manager.begin_sign_in("user@example.com".to_string(), None, None, None);
        manager.set_sync_state(SyncState::Syncing { progress: 0.5 });

        assert!(manager.sign_out().is_some());
        assert!(manager.get_account().is_none());
        assert_eq!(manager.get_auth_state(), AuthState::Unauthenticated);
        assert_eq!(manager.get_sync_state(), &SyncState::Idle);
    }
}
