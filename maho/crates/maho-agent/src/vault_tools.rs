use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct VaultListCredentialsParams {
    pub origin: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct VaultRequestCredentialUseParams {
    pub handle: String,
    pub origin: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct VaultFillCredentialParams {
    pub grant_handle: String,
    pub target_ref: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct VaultFillTotpParams {
    pub grant_handle: String,
    pub target_ref: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct VaultCredentialHandle {
    pub handle_id: String,
    pub origin: String,
    pub created_at_ms: i64,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VaultCredentialLease {
    pub handle: VaultCredentialHandle,
    pub item_id: String,
}

impl VaultCredentialHandle {
    pub fn new_opaque(_item_id: impl Into<String>, origin: impl Into<String>) -> Self {
        use std::time::{SystemTime, UNIX_EPOCH};
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as i64;
        let handle_id = format!("vh_{}_{}", now, uuid::Uuid::new_v4().simple());
        Self {
            handle_id,
            origin: origin.into(),
            created_at_ms: now,
        }
    }
}

pub fn register_vault_agent_tools() -> Vec<&'static str> {
    if cfg!(feature = "vault-agent-tools") {
        vec![
            "vault_list_credentials_for_active_page",
            "vault_request_credential_use",
            "vault_fill_credential",
            "vault_fill_totp",
        ]
    } else {
        Vec::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vault_agent_tools_gated_off_by_default() {
        let tools = register_vault_agent_tools();
        if cfg!(feature = "vault-agent-tools") {
            assert_eq!(tools.len(), 4);
            assert!(tools.contains(&"vault_list_credentials_for_active_page"));
            assert!(tools.contains(&"vault_fill_credential"));
        } else {
            assert!(
                tools.is_empty(),
                "ADR 0015: agent password-vault tools must not be advertised unless \
                 the `vault-agent-tools` feature is explicitly enabled"
            );
        }
    }

    #[test]
    fn test_opaque_handle_generation_contains_no_secrets() {
        let handle = VaultCredentialHandle::new_opaque("item_123", "https://example.com");
        assert!(handle.handle_id.starts_with("vh_"));
        assert_eq!(handle.origin, "https://example.com");
        assert!(!handle.handle_id.contains("super_secret"));
        let serialized = serde_json::to_string(&handle).unwrap();
        assert!(!serialized.contains("item_123"));
        assert!(!serialized.contains("item_id"));
    }

    #[test]
    fn request_credential_use_is_handle_based() {
        let request = VaultRequestCredentialUseParams {
            handle: "vh_test".to_string(),
            origin: "https://example.com".to_string(),
        };
        let serialized = serde_json::to_string(&request).unwrap();
        assert!(serialized.contains("vh_test"));
        assert!(serialized.contains("handle"));
        assert!(!serialized.contains("item_id"));
    }

    #[test]
    fn test_lease_manager_origin_mismatch_fails_closed() {
        let mut manager = VaultCredentialLeaseManager::new(300);
        let handle = manager.grant_lease("item_abc", "https://trusted.com");

        // Matching origin succeeds
        assert!(manager
            .validate_and_consume(&handle.handle_id, "https://trusted.com")
            .is_ok());

        // Re-using consumed handle fails
        assert_eq!(
            manager.validate_and_consume(&handle.handle_id, "https://trusted.com"),
            Err(LeaseValidationError::ExpiredOrNotFound)
        );

        // Mismatched origin fails closed
        let handle2 = manager.grant_lease("item_xyz", "https://trusted.com");
        assert_eq!(
            manager.validate_and_consume(&handle2.handle_id, "https://attacker.com"),
            Err(LeaseValidationError::OriginMismatch)
        );
    }
}

#[derive(Debug, PartialEq, Eq)]
pub enum LeaseValidationError {
    ExpiredOrNotFound,
    OriginMismatch,
}

pub struct VaultCredentialLeaseManager {
    ttl_seconds: i64,
    leases: std::collections::HashMap<String, VaultCredentialLease>,
}

impl VaultCredentialLeaseManager {
    pub fn new(ttl_seconds: i64) -> Self {
        Self {
            ttl_seconds,
            leases: std::collections::HashMap::new(),
        }
    }

    pub fn sweep_expired_leases(&mut self) {
        use std::time::{SystemTime, UNIX_EPOCH};
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs() as i64;
        self.leases
            .retain(|_, lease| (now - (lease.handle.created_at_ms / 1000)) <= self.ttl_seconds);
    }

    pub fn grant_lease(&mut self, item_id: &str, origin: &str) -> VaultCredentialHandle {
        self.sweep_expired_leases();
        let handle = VaultCredentialHandle::new_opaque(item_id, origin);
        self.leases.insert(
            handle.handle_id.clone(),
            VaultCredentialLease {
                handle: handle.clone(),
                item_id: item_id.to_string(),
            },
        );
        handle
    }

    pub fn validate_and_consume(
        &mut self,
        handle_id: &str,
        target_origin: &str,
    ) -> Result<VaultCredentialLease, LeaseValidationError> {
        self.sweep_expired_leases();
        use std::time::{SystemTime, UNIX_EPOCH};
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs() as i64;

        let handle = self
            .leases
            .remove(handle_id)
            .ok_or(LeaseValidationError::ExpiredOrNotFound)?;

        if target_origin != handle.handle.origin {
            return Err(LeaseValidationError::OriginMismatch);
        }

        if (now - (handle.handle.created_at_ms / 1000)) > self.ttl_seconds {
            return Err(LeaseValidationError::ExpiredOrNotFound);
        }

        Ok(handle)
    }
}
