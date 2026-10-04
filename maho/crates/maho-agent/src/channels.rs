// Copyright 2026 Maho Browser. All rights reserved.

//! Multi-platform channel gateway contracts (Slack, Discord, Telegram).
//!
//! Provides channel event ingestion, cursor-based deduplication, and
//! outbound message dispatch through the direct API broker.

use serde::{Deserialize, Serialize};
use std::cmp::Ordering;
use std::collections::{HashMap, HashSet};

use crate::direct_api::{
    DirectApiExecutionContext, DirectApiOperation, DirectApiOutcome, DirectApiServiceKind,
};

/// Unique identifier for an external communication channel.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct ChannelId(pub String);

impl ChannelId {
    pub fn new(id: impl Into<String>) -> Self {
        Self(id.into())
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Display for ChannelId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// External messaging platform provider.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelProvider {
    Slack,
    Discord,
    Telegram,
    Custom(String),
}

/// Cursor tracking consumed messages for deduplication across restarts.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ChannelCursor {
    pub channel_id: ChannelId,
    pub cursor_position: String,
    pub last_event_timestamp: u64,
}

/// Category of event received from an external channel.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelEventKind {
    MessageReceived,
    ReactionAdded,
    ThreadReply,
    ChannelJoined,
}

/// Inbound event received from a channel.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ChannelEvent {
    pub id: String,
    pub channel_id: ChannelId,
    pub provider: ChannelProvider,
    pub kind: ChannelEventKind,
    pub sender_id: String,
    pub sender_name: Option<String>,
    pub text: String,
    pub cursor: String,
    pub timestamp: u64,
}

/// Outbound message payload to be sent to a channel.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct OutboundChannelMessage {
    pub channel_id: ChannelId,
    pub provider: ChannelProvider,
    pub text: String,
    pub reply_to_event_id: Option<String>,
}

/// Channel credential token with zeroization on drop and strict redaction in Debug and Serialize.
#[derive(Clone)]
pub struct ChannelToken(secrecy::SecretString);

impl ChannelToken {
    pub fn new(token: impl Into<String>) -> Self {
        Self(secrecy::SecretString::new(token.into().into_boxed_str()))
    }

    pub fn expose_secret(&self) -> &str {
        use secrecy::ExposeSecret;
        self.0.expose_secret()
    }
}

impl PartialEq for ChannelToken {
    fn eq(&self, other: &Self) -> bool {
        self.expose_secret() == other.expose_secret()
    }
}

impl Eq for ChannelToken {}

impl std::fmt::Debug for ChannelToken {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[REDACTED]")
    }
}

impl Serialize for ChannelToken {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        serializer.serialize_str("[REDACTED]")
    }
}

impl<'de> Deserialize<'de> for ChannelToken {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        Ok(Self::new(s))
    }
}

/// Configuration for a configured channel gateway connection.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ChannelConfig {
    pub id: ChannelId,
    pub provider: ChannelProvider,
    pub name: String,
    pub enabled: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub token: Option<ChannelToken>,
    pub account_id: Option<String>,
    pub opaque_auth_handle: Option<String>,
}

/// Health status category for a channel connection.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelHealthStatus {
    Healthy,
    Stale,
    Degraded,
    Disconnected,
    Unconfigured,
}

/// Comprehensive health report for a channel.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ChannelHealth {
    pub status: ChannelHealthStatus,
    pub last_poll_timestamp: Option<u64>,
    pub details: Option<String>,
}

/// Trigger payload dispatched to the agent runtime when an inbound event is processed.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AgentSessionTrigger {
    pub channel_id: ChannelId,
    pub provider: ChannelProvider,
    pub sender_id: String,
    pub sender_name: Option<String>,
    pub message_text: String,
    pub event_id: String,
    pub cursor: String,
    pub timestamp: u64,
}

/// Typed outcome resulting from attempting to process an inbound channel event.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum InboundProcessOutcome {
    Triggered(AgentSessionTrigger),
    DuplicateCursor { cursor: String },
    MalformedEvent { reason: String },
    ChannelNotFound { channel_id: ChannelId },
    ChannelDisabled { channel_id: ChannelId },
}

/// Outbound command wrapping a direct API operation with confirmation requirement.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DirectApiOp {
    pub operation: DirectApiOperation,
    pub context: DirectApiExecutionContext,
    pub requires_confirmation: bool,
}

pub type OutboundDirectApiOp = DirectApiOp;

/// Typed receipt returned upon successful outbound send.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct OutboundSendReceipt {
    pub channel_id: ChannelId,
    pub message_id: Option<String>,
    pub timestamp: u64,
}

/// Errors originating in channel gateway operations.
#[derive(Debug, thiserror::Error)]
pub enum ChannelError {
    #[error("Channel not found: {0}")]
    ChannelNotFound(ChannelId),
    #[error("Channel disabled: {0}")]
    ChannelDisabled(ChannelId),
    #[error("Unsupported provider for direct API: {0:?}")]
    UnsupportedProvider(ChannelProvider),
    #[error("Direct API unavailable: {reason} (can fallback: {can_fallback_to_tabs})")]
    Unavailable {
        reason: String,
        can_fallback_to_tabs: bool,
    },
    #[error("Direct API hard failure [{code}]: {message} (policy denial: {is_policy_denial})")]
    HardFailure {
        code: String,
        message: String,
        is_policy_denial: bool,
    },
    #[error("Malformed event: {0}")]
    MalformedEvent(String),
}

/// Per-channel runtime state maintained in the registry.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ChannelEntry {
    pub config: ChannelConfig,
    pub last_cursor: Option<String>,
    pub last_poll_timestamp: Option<u64>,
    #[serde(default, skip_serializing)]
    pub seen_cursors: HashSet<String>,
}

/// Registry managing active channel registrations and their cursor deduplication state.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ChannelRegistry {
    pub channels: HashMap<ChannelId, ChannelEntry>,
}

/// Helper comparing cursor positions numerically or with alpha prefixes (e.g. C5 vs C6).
fn compare_cursor_positions(a: &str, b: &str) -> Ordering {
    if a == b {
        return Ordering::Equal;
    }

    if let (Ok(num_a), Ok(num_b)) = (a.parse::<u64>(), b.parse::<u64>()) {
        return num_a.cmp(&num_b);
    }

    let strip_prefix = |s: &str| -> Option<(char, u64)> {
        let mut chars = s.chars();
        let first = chars.next()?;
        if first.is_alphabetic() {
            let rest: String = chars.collect();
            let num: u64 = rest.parse().ok()?;
            Some((first, num))
        } else {
            None
        }
    };

    if let (Some((prefix_a, num_a)), Some((prefix_b, num_b))) = (strip_prefix(a), strip_prefix(b)) {
        if prefix_a == prefix_b {
            return num_a.cmp(&num_b);
        }
    }

    a.cmp(b)
}

/// Constructs a typed DirectApiOp for outbound message sending.
///
/// Ensures credentials are not included in payload parameters and that
/// requires_confirmation is strictly set to true.
pub fn send_via_direct_api(
    channel: &ChannelConfig,
    message: OutboundChannelMessage,
) -> Result<DirectApiOp, ChannelError> {
    if !channel.enabled {
        return Err(ChannelError::ChannelDisabled(channel.id.clone()));
    }
    if channel.id != message.channel_id {
        return Err(ChannelError::ChannelNotFound(message.channel_id));
    }

    let service = match message.provider {
        ChannelProvider::Slack => DirectApiServiceKind::Slack,
        ChannelProvider::Discord => DirectApiServiceKind::Discord,
        ChannelProvider::Telegram => DirectApiServiceKind::Telegram,
        ChannelProvider::Custom(_) => {
            return Err(ChannelError::UnsupportedProvider(message.provider));
        }
    };

    let operation_name = "channels.send_message".to_string();

    let parameters = serde_json::json!({
        "channel_id": message.channel_id.as_str(),
        "text": message.text,
        "reply_to_event_id": message.reply_to_event_id,
    });

    let execution_context = DirectApiExecutionContext {
        session_id: format!("channel-session-{}", message.channel_id.as_str()),
        account_id: channel.account_id.clone(),
        opaque_auth_handle: channel.opaque_auth_handle.clone(),
    };

    Ok(DirectApiOp {
        operation: DirectApiOperation {
            service,
            operation_name,
            parameters,
            read_only: false,
            required_scopes: Vec::new(),
        },
        context: execution_context,
        requires_confirmation: true,
    })
}

/// Maps DirectApiOutcome results into typed OutboundSendReceipt or ChannelError.
pub fn handle_direct_api_outcome(
    outcome: &DirectApiOutcome,
) -> Result<OutboundSendReceipt, ChannelError> {
    match outcome {
        DirectApiOutcome::Supported(val) => {
            let channel_id_str = val
                .get("channel_id")
                .and_then(|v| v.as_str())
                .unwrap_or("unknown");
            let message_id = val
                .get("message_id")
                .and_then(|v| v.as_str())
                .map(|s| s.to_string());
            let timestamp = val.get("timestamp").and_then(|v| v.as_u64()).unwrap_or(0);
            Ok(OutboundSendReceipt {
                channel_id: ChannelId::new(channel_id_str),
                message_id,
                timestamp,
            })
        }
        DirectApiOutcome::TypedUnavailable {
            reason,
            can_fallback_to_tabs,
        } => Err(ChannelError::Unavailable {
            reason: reason.clone(),
            can_fallback_to_tabs: *can_fallback_to_tabs,
        }),
        DirectApiOutcome::HardFailure {
            code,
            message,
            is_policy_denial,
        } => Err(ChannelError::HardFailure {
            code: code.clone(),
            message: message.clone(),
            is_policy_denial: *is_policy_denial,
        }),
    }
}

impl ChannelRegistry {
    pub fn new() -> Self {
        Self {
            channels: HashMap::new(),
        }
    }

    /// Registers or updates a channel configuration.
    pub fn register_channel(&mut self, config: ChannelConfig) {
        let channel_id = config.id.clone();
        if let Some(entry) = self.channels.get_mut(&channel_id) {
            entry.config = config;
        } else {
            self.channels.insert(
                channel_id,
                ChannelEntry {
                    config,
                    last_cursor: None,
                    last_poll_timestamp: None,
                    seen_cursors: HashSet::new(),
                },
            );
        }
    }

    /// Unregisters a channel by ID.
    pub fn unregister_channel(&mut self, channel_id: &ChannelId) -> Option<ChannelConfig> {
        self.channels.remove(channel_id).map(|e| e.config)
    }

    /// Retrieves a channel configuration by ID.
    pub fn get_channel(&self, channel_id: &ChannelId) -> Option<&ChannelConfig> {
        self.channels.get(channel_id).map(|e| &e.config)
    }

    /// Retrieves the last recorded cursor for a channel.
    pub fn last_cursor(&self, channel_id: &ChannelId) -> Option<&str> {
        self.channels
            .get(channel_id)
            .and_then(|e| e.last_cursor.as_deref())
    }

    /// Records an active poll timestamp for a channel.
    pub fn record_poll(&mut self, channel_id: &ChannelId, timestamp: u64) {
        if let Some(entry) = self.channels.get_mut(channel_id) {
            entry.last_poll_timestamp = Some(timestamp);
        }
    }

    /// Ingests and processes an inbound channel event, enforcing deduplication and validation.
    pub fn process_inbound_event(&mut self, event: ChannelEvent) -> InboundProcessOutcome {
        // Validate malformed event fields
        if event.id.trim().is_empty() {
            return InboundProcessOutcome::MalformedEvent {
                reason: "Event id is empty".to_string(),
            };
        }
        if event.channel_id.as_str().trim().is_empty() {
            return InboundProcessOutcome::MalformedEvent {
                reason: "Channel id is empty".to_string(),
            };
        }
        if event.sender_id.trim().is_empty() {
            return InboundProcessOutcome::MalformedEvent {
                reason: "Sender id is empty".to_string(),
            };
        }
        if event.cursor.trim().is_empty() {
            return InboundProcessOutcome::MalformedEvent {
                reason: "Cursor position is empty".to_string(),
            };
        }
        if event.timestamp == 0 {
            return InboundProcessOutcome::MalformedEvent {
                reason: "Event timestamp is zero or invalid".to_string(),
            };
        }

        let entry = match self.channels.get_mut(&event.channel_id) {
            Some(entry) => entry,
            None => {
                return InboundProcessOutcome::ChannelNotFound {
                    channel_id: event.channel_id,
                };
            }
        };

        if !entry.config.enabled {
            return InboundProcessOutcome::ChannelDisabled {
                channel_id: event.channel_id,
            };
        }

        // Deduplication check: seen set or <= last_cursor
        if entry.seen_cursors.contains(&event.cursor) {
            return InboundProcessOutcome::DuplicateCursor {
                cursor: event.cursor,
            };
        }

        if let Some(ref last) = entry.last_cursor {
            if compare_cursor_positions(&event.cursor, last) != Ordering::Greater {
                return InboundProcessOutcome::DuplicateCursor {
                    cursor: event.cursor,
                };
            }
        }

        // Update cursor tracking
        entry.seen_cursors.insert(event.cursor.clone());
        entry.last_cursor = Some(event.cursor.clone());
        entry.last_poll_timestamp = Some(event.timestamp);

        InboundProcessOutcome::Triggered(AgentSessionTrigger {
            channel_id: event.channel_id,
            provider: event.provider,
            sender_id: event.sender_id,
            sender_name: event.sender_name,
            message_text: event.text,
            event_id: event.id,
            cursor: event.cursor,
            timestamp: event.timestamp,
        })
    }

    /// Evaluates channel health given current injected timestamp and staleness threshold.
    pub fn health(
        &self,
        channel_id: &ChannelId,
        now: u64,
        stale_threshold_secs: u64,
    ) -> ChannelHealth {
        let entry = match self.channels.get(channel_id) {
            Some(entry) => entry,
            None => {
                return ChannelHealth {
                    status: ChannelHealthStatus::Unconfigured,
                    last_poll_timestamp: None,
                    details: Some("channel not registered".to_string()),
                };
            }
        };

        if !entry.config.enabled {
            return ChannelHealth {
                status: ChannelHealthStatus::Unconfigured,
                last_poll_timestamp: entry.last_poll_timestamp,
                details: Some("channel disabled".to_string()),
            };
        }

        if entry.config.token.is_none() && entry.config.opaque_auth_handle.is_none() {
            return ChannelHealth {
                status: ChannelHealthStatus::Unconfigured,
                last_poll_timestamp: entry.last_poll_timestamp,
                details: Some("no credentials or auth handle configured".to_string()),
            };
        }

        match entry.last_poll_timestamp {
            None => ChannelHealth {
                status: ChannelHealthStatus::Disconnected,
                last_poll_timestamp: None,
                details: Some("channel has not polled yet".to_string()),
            },
            Some(last_poll) => {
                if now.saturating_sub(last_poll) > stale_threshold_secs {
                    ChannelHealth {
                        status: ChannelHealthStatus::Stale,
                        last_poll_timestamp: Some(last_poll),
                        details: Some(format!(
                            "poll interval exceeded: {}s since last poll (threshold: {}s)",
                            now.saturating_sub(last_poll),
                            stale_threshold_secs
                        )),
                    }
                } else {
                    ChannelHealth {
                        status: ChannelHealthStatus::Healthy,
                        last_poll_timestamp: Some(last_poll),
                        details: None,
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_failing_first_outbound_send_via_direct_api() {
        let channel = ChannelConfig {
            id: ChannelId::new("slack-general"),
            provider: ChannelProvider::Slack,
            name: "General Slack".to_string(),
            enabled: true,
            token: Some(ChannelToken::new("xoxb-secret-token-12345")),
            account_id: Some("acc-123".to_string()),
            opaque_auth_handle: Some("auth-handle-xyz".to_string()),
        };
        let msg = OutboundChannelMessage {
            channel_id: ChannelId::new("slack-general"),
            provider: ChannelProvider::Slack,
            text: "Hello team".to_string(),
            reply_to_event_id: Some("evt-001".to_string()),
        };

        let op = send_via_direct_api(&channel, msg).expect("op created");
        assert_eq!(op.operation.service, DirectApiServiceKind::Slack);
        assert_eq!(op.operation.operation_name, "channels.send_message");
        assert!(!op.operation.read_only);
        assert!(
            op.requires_confirmation,
            "Outbound message must require confirmation"
        );
        assert_eq!(
            op.context.opaque_auth_handle.as_deref(),
            Some("auth-handle-xyz")
        );
        assert_eq!(op.context.account_id.as_deref(), Some("acc-123"));
    }

    #[test]
    fn test_outbound_direct_api_outcome_handling() {
        // Supported outcome
        let supported = DirectApiOutcome::Supported(serde_json::json!({
            "channel_id": "slack-general",
            "message_id": "msg-99",
            "timestamp": 1700000010
        }));
        let receipt = handle_direct_api_outcome(&supported).expect("receipt");
        assert_eq!(receipt.channel_id.as_str(), "slack-general");
        assert_eq!(receipt.message_id.as_deref(), Some("msg-99"));
        assert_eq!(receipt.timestamp, 1700000010);

        // TypedUnavailable outcome
        let unavailable = DirectApiOutcome::TypedUnavailable {
            reason: "API rate limited".to_string(),
            can_fallback_to_tabs: true,
        };
        match handle_direct_api_outcome(&unavailable) {
            Err(ChannelError::Unavailable {
                reason,
                can_fallback_to_tabs,
            }) => {
                assert_eq!(reason, "API rate limited");
                assert!(can_fallback_to_tabs);
            }
            other => panic!("Expected Unavailable, got {:?}", other),
        }

        // HardFailure outcome
        let hard_failure = DirectApiOutcome::HardFailure {
            code: "UNAUTHORIZED".to_string(),
            message: "Revoked token".to_string(),
            is_policy_denial: true,
        };
        match handle_direct_api_outcome(&hard_failure) {
            Err(ChannelError::HardFailure {
                code,
                message,
                is_policy_denial,
            }) => {
                assert_eq!(code, "UNAUTHORIZED");
                assert_eq!(message, "Revoked token");
                assert!(is_policy_denial);
            }
            other => panic!("Expected HardFailure, got {:?}", other),
        }
    }

    #[test]
    fn test_failing_first_inbound_cursor_dedupe() {
        let mut registry = ChannelRegistry::new();
        let channel = ChannelConfig {
            id: ChannelId::new("discord-alerts"),
            provider: ChannelProvider::Discord,
            name: "Discord Alerts".to_string(),
            enabled: true,
            token: None,
            account_id: None,
            opaque_auth_handle: Some("auth-discord".to_string()),
        };
        registry.register_channel(channel);

        let event1 = ChannelEvent {
            id: "evt-1".to_string(),
            channel_id: ChannelId::new("discord-alerts"),
            provider: ChannelProvider::Discord,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "user-42".to_string(),
            sender_name: Some("Alice".to_string()),
            text: "Deploy started".to_string(),
            cursor: "C1".to_string(),
            timestamp: 1700000001,
        };

        let outcome1 = registry.process_inbound_event(event1.clone());
        match outcome1 {
            InboundProcessOutcome::Triggered(trigger) => {
                assert_eq!(trigger.cursor, "C1");
                assert_eq!(trigger.message_text, "Deploy started");
            }
            other => panic!("Expected Triggered, got {:?}", other),
        }

        // Duplicate same cursor C1
        let outcome2 = registry.process_inbound_event(event1);
        match outcome2 {
            InboundProcessOutcome::DuplicateCursor { cursor } => {
                assert_eq!(cursor, "C1");
            }
            other => panic!("Expected DuplicateCursor, got {:?}", other),
        }
    }

    #[test]
    fn test_failing_first_restart_simulation_last_cursor_persistence() {
        let mut registry = ChannelRegistry::new();
        let channel = ChannelConfig {
            id: ChannelId::new("tg-feed"),
            provider: ChannelProvider::Telegram,
            name: "Telegram Feed".to_string(),
            enabled: true,
            token: Some(ChannelToken::new("tg-bot-secret-999")),
            account_id: None,
            opaque_auth_handle: None,
        };
        registry.register_channel(channel);

        // Process up to C5
        for i in 1..=5 {
            let evt = ChannelEvent {
                id: format!("tg-evt-{i}"),
                channel_id: ChannelId::new("tg-feed"),
                provider: ChannelProvider::Telegram,
                kind: ChannelEventKind::MessageReceived,
                sender_id: "user-tg".to_string(),
                sender_name: None,
                text: format!("Msg {i}"),
                cursor: format!("C{i}"),
                timestamp: 1700000000 + i as u64,
            };
            registry.process_inbound_event(evt);
        }

        // Persist to JSON
        let serialized = serde_json::to_string(&registry).expect("serialize registry");

        // Reload into a new registry
        let mut reloaded: ChannelRegistry =
            serde_json::from_str(&serialized).expect("deserialize registry");

        // C5 must NOT be reprocessed
        let event_c5 = ChannelEvent {
            id: "tg-evt-5".to_string(),
            channel_id: ChannelId::new("tg-feed"),
            provider: ChannelProvider::Telegram,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "user-tg".to_string(),
            sender_name: None,
            text: "Msg 5".to_string(),
            cursor: "C5".to_string(),
            timestamp: 1700000005,
        };
        let outcome_c5 = reloaded.process_inbound_event(event_c5);
        assert!(
            matches!(outcome_c5, InboundProcessOutcome::DuplicateCursor { cursor } if cursor == "C5"),
            "C5 must be treated as duplicate after reload"
        );

        // C4 must also NOT be reprocessed (older cursor)
        let event_c4 = ChannelEvent {
            id: "tg-evt-4".to_string(),
            channel_id: ChannelId::new("tg-feed"),
            provider: ChannelProvider::Telegram,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "user-tg".to_string(),
            sender_name: None,
            text: "Msg 4".to_string(),
            cursor: "C4".to_string(),
            timestamp: 1700000004,
        };
        let outcome_c4 = reloaded.process_inbound_event(event_c4);
        assert!(
            matches!(outcome_c4, InboundProcessOutcome::DuplicateCursor { cursor } if cursor == "C4"),
            "C4 must be treated as duplicate after reload"
        );

        // C6 must be processed
        let event_c6 = ChannelEvent {
            id: "tg-evt-6".to_string(),
            channel_id: ChannelId::new("tg-feed"),
            provider: ChannelProvider::Telegram,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "user-tg".to_string(),
            sender_name: None,
            text: "Msg 6".to_string(),
            cursor: "C6".to_string(),
            timestamp: 1700000006,
        };
        let outcome_c6 = reloaded.process_inbound_event(event_c6);
        match outcome_c6 {
            InboundProcessOutcome::Triggered(trigger) => {
                assert_eq!(trigger.cursor, "C6");
                assert_eq!(trigger.message_text, "Msg 6");
            }
            other => panic!("Expected Triggered for C6, got {:?}", other),
        }
    }

    #[test]
    fn test_failing_first_token_redaction_probe() {
        let secret_token = "super-secret-slack-token-xyz987";
        let config = ChannelConfig {
            id: ChannelId::new("slack-secret"),
            provider: ChannelProvider::Slack,
            name: "Secret Slack".to_string(),
            enabled: true,
            token: Some(ChannelToken::new(secret_token)),
            account_id: Some("acc-sec".to_string()),
            opaque_auth_handle: Some("auth-sec".to_string()),
        };

        // Assert Debug redaction
        let debug_str = format!("{:?}", config);
        assert!(
            !debug_str.contains(secret_token),
            "Debug output leaked token!"
        );
        assert!(debug_str.contains("[REDACTED]"));

        // Assert Serialize redaction
        let json_str = serde_json::to_string(&config).expect("serialize config");
        assert!(
            !json_str.contains(secret_token),
            "JSON serialized config leaked token!"
        );

        // Assert DirectApiOp debug & serialization
        let msg = OutboundChannelMessage {
            channel_id: ChannelId::new("slack-secret"),
            provider: ChannelProvider::Slack,
            text: "secret message".to_string(),
            reply_to_event_id: None,
        };
        let op = send_via_direct_api(&config, msg).expect("op");
        let op_debug = format!("{:?}", op);
        let op_json = serde_json::to_string(&op).expect("op json");
        assert!(
            !op_debug.contains(secret_token),
            "DirectApiOp Debug leaked token!"
        );
        assert!(
            !op_json.contains(secret_token),
            "DirectApiOp JSON leaked token!"
        );
    }

    #[test]
    fn test_adversarial_malformed_input_handling() {
        let mut registry = ChannelRegistry::new();
        let channel = ChannelConfig {
            id: ChannelId::new("slack-malformed"),
            provider: ChannelProvider::Slack,
            name: "Malformed Test".to_string(),
            enabled: true,
            token: Some(ChannelToken::new("tok-123")),
            account_id: None,
            opaque_auth_handle: None,
        };
        registry.register_channel(channel);

        // Empty event ID
        let bad_event1 = ChannelEvent {
            id: "   ".to_string(),
            channel_id: ChannelId::new("slack-malformed"),
            provider: ChannelProvider::Slack,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "user-1".to_string(),
            sender_name: None,
            text: "Hi".to_string(),
            cursor: "C1".to_string(),
            timestamp: 1700000000,
        };
        assert!(matches!(
            registry.process_inbound_event(bad_event1),
            InboundProcessOutcome::MalformedEvent { .. }
        ));

        // Empty sender ID
        let bad_event2 = ChannelEvent {
            id: "evt-2".to_string(),
            channel_id: ChannelId::new("slack-malformed"),
            provider: ChannelProvider::Slack,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "".to_string(),
            sender_name: None,
            text: "Hi".to_string(),
            cursor: "C1".to_string(),
            timestamp: 1700000000,
        };
        assert!(matches!(
            registry.process_inbound_event(bad_event2),
            InboundProcessOutcome::MalformedEvent { .. }
        ));

        // Empty cursor
        let bad_event3 = ChannelEvent {
            id: "evt-3".to_string(),
            channel_id: ChannelId::new("slack-malformed"),
            provider: ChannelProvider::Slack,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "user-1".to_string(),
            sender_name: None,
            text: "Hi".to_string(),
            cursor: "".to_string(),
            timestamp: 1700000000,
        };
        assert!(matches!(
            registry.process_inbound_event(bad_event3),
            InboundProcessOutcome::MalformedEvent { .. }
        ));

        // Zero timestamp
        let bad_event4 = ChannelEvent {
            id: "evt-4".to_string(),
            channel_id: ChannelId::new("slack-malformed"),
            provider: ChannelProvider::Slack,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "user-1".to_string(),
            sender_name: None,
            text: "Hi".to_string(),
            cursor: "C1".to_string(),
            timestamp: 0,
        };
        assert!(matches!(
            registry.process_inbound_event(bad_event4),
            InboundProcessOutcome::MalformedEvent { .. }
        ));

        // Cursor should still be uninitialized since all bad events were rejected
        assert_eq!(
            registry.last_cursor(&ChannelId::new("slack-malformed")),
            None
        );

        // Unknown channel
        let unknown_event = ChannelEvent {
            id: "evt-5".to_string(),
            channel_id: ChannelId::new("non-existent"),
            provider: ChannelProvider::Slack,
            kind: ChannelEventKind::MessageReceived,
            sender_id: "user-1".to_string(),
            sender_name: None,
            text: "Hi".to_string(),
            cursor: "C1".to_string(),
            timestamp: 1700000000,
        };
        assert!(matches!(
            registry.process_inbound_event(unknown_event),
            InboundProcessOutcome::ChannelNotFound { .. }
        ));
    }

    #[test]
    fn test_channel_health_evaluation_with_injected_clock() {
        let mut registry = ChannelRegistry::new();
        let channel_id = ChannelId::new("slack-health");
        let channel = ChannelConfig {
            id: channel_id.clone(),
            provider: ChannelProvider::Slack,
            name: "Health Test".to_string(),
            enabled: true,
            token: Some(ChannelToken::new("tok-health")),
            account_id: None,
            opaque_auth_handle: None,
        };
        registry.register_channel(channel);

        // Before any poll -> Disconnected
        let health1 = registry.health(&channel_id, 1000, 30);
        assert_eq!(health1.status, ChannelHealthStatus::Disconnected);

        // Record poll at t=1000
        registry.record_poll(&channel_id, 1000);

        // At t=1020 (diff 20s <= 30s threshold) -> Healthy
        let health2 = registry.health(&channel_id, 1020, 30);
        assert_eq!(health2.status, ChannelHealthStatus::Healthy);

        // At t=1035 (diff 35s > 30s threshold) -> Stale
        let health3 = registry.health(&channel_id, 1035, 30);
        assert_eq!(health3.status, ChannelHealthStatus::Stale);

        // Disable channel -> Unconfigured
        let mut disabled_config = registry.get_channel(&channel_id).unwrap().clone();
        disabled_config.enabled = false;
        registry.register_channel(disabled_config);
        let health4 = registry.health(&channel_id, 1035, 30);
        assert_eq!(health4.status, ChannelHealthStatus::Unconfigured);
    }
}
