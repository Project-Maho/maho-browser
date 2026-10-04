// Copyright 2026 Maho Browser. All rights reserved.

//! Proactive context gathering contracts with origin provenance.
//!
//! Provides bounded, read-only preflight gathering across memory, history,
//! search, and active tabs with typed provenance attribution.

use serde::{Deserialize, Serialize};

/// Source origin for a gathered context block.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ContextBlockSource {
    Memory,
    BrowserHistory,
    WebSearch,
    DirectApi,
    ActiveTab,
}

/// Provenance metadata tracking the origin and freshness of a context block.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ContextBlockProvenance {
    pub source: ContextBlockSource,
    pub origin_url: Option<String>,
    pub memory_key: Option<String>,
    pub gathered_at: u64,
    pub confidence_score: f32,
}

/// A bounded unit of gathered contextual information.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ContextBlock {
    pub id: String,
    pub source: ContextBlockSource,
    pub title: String,
    pub content: String,
    pub estimated_tokens: usize,
    pub provenance: ContextBlockProvenance,
}

/// Request parameters for preflight context gathering.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ContextGatherRequest {
    pub query: String,
    pub max_total_tokens: usize,
    pub allowed_sources: Vec<ContextBlockSource>,
}

/// Aggregated response containing gathered context blocks.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ContextGatherResponse {
    pub blocks: Vec<ContextBlock>,
    pub total_estimated_tokens: usize,
    pub partial_results: bool,
}

/// Redacted metadata for an authenticated account; strictly contains service and label with NO secret credentials.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AccountContextEntry {
    pub service: String,
    pub handle_label: String,
    pub scopes: Vec<String>,
}

impl AccountContextEntry {
    pub fn new(
        service: impl Into<String>,
        handle_label: impl Into<String>,
        scopes: Vec<String>,
    ) -> Self {
        Self {
            service: service.into(),
            handle_label: handle_label.into(),
            scopes,
        }
    }
}

/// DTO representing the accounts context slot provided to the agent/model.
/// Contains read-only metadata and handle labels only, guaranteeing secret redaction.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AccountContextSlot {
    pub available: bool,
    pub accounts: Vec<AccountContextEntry>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub unavailable_reason: Option<String>,
}

impl AccountContextSlot {
    pub fn available(accounts: Vec<AccountContextEntry>) -> Self {
        Self {
            available: true,
            accounts,
            unavailable_reason: None,
        }
    }

    pub fn unavailable(reason: impl Into<String>) -> Self {
        Self {
            available: false,
            accounts: Vec::new(),
            unavailable_reason: Some(reason.into()),
        }
    }

    /// Enumerates accounts from direct API broker grants in a read-only, sanitized manner.
    pub fn from_broker_grants<I>(grants: I) -> Self
    where
        I: IntoIterator<Item = (String, String, Vec<String>)>,
    {
        let mut accounts: Vec<AccountContextEntry> = grants
            .into_iter()
            .map(|(service, handle_label, scopes)| AccountContextEntry {
                service,
                handle_label,
                scopes,
            })
            .collect();
        accounts.sort_by(|a, b| (&a.service, &a.handle_label).cmp(&(&b.service, &b.handle_label)));
        Self::available(accounts)
    }

    /// Populates AccountContextSlot from a DirectApiBroker's ScopeRegistry.
    pub fn gather_from_scope_registry(
        registry: &dyn crate::direct_api::ScopeRegistry,
        services: &[crate::direct_api::DirectApiServiceKind],
        handles: &[&str],
    ) -> Self {
        let mut accounts = Vec::new();
        for &service in services {
            for &handle in handles {
                let scopes = registry.get_granted_scopes(handle, service);
                if !scopes.is_empty() {
                    let mut sorted_scopes: Vec<String> = scopes.into_iter().collect();
                    sorted_scopes.sort();
                    accounts.push(AccountContextEntry {
                        service: service.to_string(),
                        handle_label: handle.to_string(),
                        scopes: sorted_scopes,
                    });
                }
            }
        }

        if accounts.is_empty() {
            Self::unavailable("No authenticated broker accounts found")
        } else {
            accounts
                .sort_by(|a, b| (&a.service, &a.handle_label).cmp(&(&b.service, &b.handle_label)));
            Self::available(accounts)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::direct_api::{
        CredentialVault, DirectApiServiceKind, InMemoryCredentialVault, InMemoryScopeRegistry,
        ScopeRegistry, VaultCredential,
    };

    #[test]
    fn test_account_context_slot_with_fake_broker_accounts() {
        let registry = InMemoryScopeRegistry::new();
        registry.grant_scopes(
            "work-email@example.com",
            DirectApiServiceKind::Gmail,
            vec!["gmail.readonly".to_string(), "gmail.send".to_string()],
        );
        registry.grant_scopes(
            "my-workspace",
            DirectApiServiceKind::Slack,
            vec!["channels.read".to_string(), "chat.write".to_string()],
        );

        let slot = AccountContextSlot::gather_from_scope_registry(
            &registry,
            &[DirectApiServiceKind::Gmail, DirectApiServiceKind::Slack],
            &["work-email@example.com", "my-workspace"],
        );

        assert!(slot.available);
        assert_eq!(slot.accounts.len(), 2);
        assert_eq!(slot.unavailable_reason, None);

        let gmail = slot.accounts.iter().find(|a| a.service == "gmail").unwrap();
        assert_eq!(gmail.handle_label, "work-email@example.com");
        assert_eq!(gmail.scopes, vec!["gmail.readonly", "gmail.send"]);

        let slack = slot.accounts.iter().find(|a| a.service == "slack").unwrap();
        assert_eq!(slack.handle_label, "my-workspace");
        assert_eq!(slack.scopes, vec!["channels.read", "chat.write"]);
    }

    #[test]
    fn test_account_context_slot_unavailable_honest_reason() {
        let slot =
            AccountContextSlot::unavailable("Direct API integration disabled for security policy");
        assert!(!slot.available);
        assert!(slot.accounts.is_empty());
        assert_eq!(
            slot.unavailable_reason.as_deref(),
            Some("Direct API integration disabled for security policy")
        );

        let json = serde_json::to_string(&slot).unwrap();
        assert!(json.contains("\"available\":false"));
        assert!(json.contains("Direct API integration disabled"));
    }

    #[test]
    fn test_account_context_slot_redaction_probe() {
        let vault = InMemoryCredentialVault::new();
        vault.store_credential(
            "secret-handle-42".to_string(),
            VaultCredential::bearer("super_secret_oauth_access_token_12345"),
        );

        let registry = InMemoryScopeRegistry::new();
        registry.grant_scopes(
            "secret-handle-42",
            DirectApiServiceKind::Telegram,
            vec!["bot.messages".to_string()],
        );

        let slot = AccountContextSlot::gather_from_scope_registry(
            &registry,
            &[DirectApiServiceKind::Telegram],
            &["secret-handle-42"],
        );

        let serialized = serde_json::to_string(&slot).unwrap();
        let val: serde_json::Value = serde_json::from_str(&serialized).unwrap();

        // Must serialize service, handle_label, scopes
        assert!(serialized.contains("secret-handle-42"));
        assert!(serialized.contains("telegram"));
        assert!(serialized.contains("bot.messages"));

        // Redaction probe: assert no raw credentials, tokens, bearer or auth secrets are serialized
        assert!(!serialized.contains("super_secret_oauth_access_token_12345"));
        assert!(!serialized.contains("bearer"));
        assert!(!serialized.contains("token"));
        assert!(!serialized.contains("credential"));
        assert!(!serialized.contains("secret_key"));
        assert!(!serialized.contains("password"));

        // Value structure assertions
        assert_eq!(val["available"], true);
        assert_eq!(val["accounts"][0]["handle_label"], "secret-handle-42");
        assert_eq!(val["accounts"][0]["service"], "telegram");
    }
}
