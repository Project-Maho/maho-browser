// Copyright 2026 Maho Browser. All rights reserved.

//! Zero-tab direct API broker typed contracts and implementation.
//!
//! Provides typed execution outcomes for authenticated web service APIs
//! (Gmail, Google Workspace, Calendar, Sheets, Slack, Discord, Telegram, etc.)
//! without requiring tab automation.
//!
//! Safety and privacy invariants:
//! - Credentials and tokens remain strictly opaque in browser vault storage
//! - Broker injects Authorization headers exclusively at the transport layer
//! - Model-facing operation descriptors expose only service provider, operation, and required scopes
//! - Write/mutating operations fail-closed with NeedsConfirmation until single-use approval token is provided
//! - Scope enforcement validates grants before any network dispatch
//! - All outcomes are strictly typed (never raw HTTP HTML/dumps into model context)
//! - Uncertain mutations or cancelled requests delegate to retry policy (never auto-retried)

use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::sync::{Arc, Mutex};

/// Outcome of invoking a direct service API operation (legacy and high-level contract).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "outcome", rename_all = "snake_case")]
pub enum DirectApiOutcome<T = serde_json::Value> {
    /// Operation succeeded via direct API.
    Supported(T),
    /// Direct API is unavailable for this operation/account; tab fallback allowed.
    TypedUnavailable {
        reason: String,
        can_fallback_to_tabs: bool,
    },
    /// Hard failure or security denial; tab fallback is NOT permitted.
    HardFailure {
        code: String,
        message: String,
        is_policy_denial: bool,
    },
}

/// Service provider kind supported by direct API execution.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DirectApiServiceKind {
    Gmail,
    GoogleDrive,
    GoogleCalendar,
    GoogleSheets,
    Slack,
    Discord,
    Telegram,
}

impl std::fmt::Display for DirectApiServiceKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Gmail => write!(f, "gmail"),
            Self::GoogleDrive => write!(f, "google_drive"),
            Self::GoogleCalendar => write!(f, "google_calendar"),
            Self::GoogleSheets => write!(f, "google_sheets"),
            Self::Slack => write!(f, "slack"),
            Self::Discord => write!(f, "discord"),
            Self::Telegram => write!(f, "telegram"),
        }
    }
}

/// Structured request descriptor for a direct API operation.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DirectApiOperation {
    pub service: DirectApiServiceKind,
    pub operation_name: String,
    pub parameters: serde_json::Value,
    pub read_only: bool,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub required_scopes: Vec<String>,
}

impl DirectApiOperation {
    pub fn new(
        service: DirectApiServiceKind,
        operation_name: impl Into<String>,
        parameters: serde_json::Value,
        read_only: bool,
    ) -> Self {
        Self {
            service,
            operation_name: operation_name.into(),
            parameters,
            read_only,
            required_scopes: Vec::new(),
        }
    }

    pub fn with_scopes(mut self, scopes: Vec<String>) -> Self {
        self.required_scopes = scopes;
        self
    }
}

/// Execution context carrying non-credential session identifiers.
///
/// Invariant: NEVER contains raw cookies, tokens, or headers.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DirectApiExecutionContext {
    pub session_id: String,
    pub account_id: Option<String>,
    /// Opaque broker handle; NEVER contains raw cookies, tokens, or headers.
    pub opaque_auth_handle: Option<String>,
}

impl DirectApiExecutionContext {
    pub fn new(session_id: impl Into<String>) -> Self {
        Self {
            session_id: session_id.into(),
            account_id: None,
            opaque_auth_handle: None,
        }
    }

    pub fn with_auth_handle(mut self, handle: impl Into<String>) -> Self {
        self.opaque_auth_handle = Some(handle.into());
        self
    }

    pub fn with_account_id(mut self, account_id: impl Into<String>) -> Self {
        self.account_id = Some(account_id.into());
        self
    }
}

/// Model-facing direct API request structure.
///
/// Contains provider, operation, scopes, and parameters ONLY.
/// Guaranteed to contain no secret or credential fields.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ModelFacingDirectApiRequest {
    pub service: DirectApiServiceKind,
    pub operation_name: String,
    pub scopes: Vec<String>,
    pub parameters: serde_json::Value,
    pub read_only: bool,
}

impl From<&DirectApiOperation> for ModelFacingDirectApiRequest {
    fn from(op: &DirectApiOperation) -> Self {
        Self {
            service: op.service,
            operation_name: op.operation_name.clone(),
            scopes: op.required_scopes.clone(),
            parameters: op.parameters.clone(),
            read_only: op.read_only,
        }
    }
}

/// Error codes returned in typed direct API failures.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DirectApiErrorCode {
    ScopeDenied,
    MissingCredentials,
    InvalidAuthHandle,
    ConfirmationRequired,
    InvalidApprovalToken,
    ApprovalTokenAlreadyUsed,
    RateLimited,
    NetworkError,
    ServiceUnavailable,
    ResourceNotFound,
    BadRequest,
    OperationCancelled,
    InternalError,
}

impl std::fmt::Display for DirectApiErrorCode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::ScopeDenied => write!(f, "scope_denied"),
            Self::MissingCredentials => write!(f, "missing_credentials"),
            Self::InvalidAuthHandle => write!(f, "invalid_auth_handle"),
            Self::ConfirmationRequired => write!(f, "confirmation_required"),
            Self::InvalidApprovalToken => write!(f, "invalid_approval_token"),
            Self::ApprovalTokenAlreadyUsed => write!(f, "approval_token_already_used"),
            Self::RateLimited => write!(f, "rate_limited"),
            Self::NetworkError => write!(f, "network_error"),
            Self::ServiceUnavailable => write!(f, "service_unavailable"),
            Self::ResourceNotFound => write!(f, "resource_not_found"),
            Self::BadRequest => write!(f, "bad_request"),
            Self::OperationCancelled => write!(f, "operation_cancelled"),
            Self::InternalError => write!(f, "internal_error"),
        }
    }
}

/// Typed execution outcome from the Direct API broker.
///
/// Invariant: Raw HTML/HTTP dumps are strictly sanitized into structured fields.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "status", rename_all = "snake_case")]
pub enum DirectApiExecutionOutcome {
    /// Operation succeeded with typed response payload.
    Ok {
        service: DirectApiServiceKind,
        operation_name: String,
        payload: serde_json::Value,
    },
    /// Operation requires user confirmation before execution (fail-closed).
    NeedsConfirmation {
        action_id: String,
        description: String,
        required_scopes: Vec<String>,
    },
    /// Scope denied or permission rejected before network dispatch.
    Forbidden {
        reason: String,
        missing_scopes: Vec<String>,
    },
    /// Typed structured error.
    TypedError {
        code: DirectApiErrorCode,
        message: String,
        retryable: bool,
    },
    /// Operation was explicitly cancelled mid-execution.
    Cancelled { reason: String },
}

impl DirectApiExecutionOutcome {
    /// Converts to the high-level `DirectApiOutcome` enum for fallback orchestration.
    pub fn to_direct_api_outcome(&self) -> DirectApiOutcome {
        match self {
            Self::Ok { payload, .. } => DirectApiOutcome::Supported(payload.clone()),
            Self::NeedsConfirmation { description, .. } => DirectApiOutcome::HardFailure {
                code: "CONFIRMATION_REQUIRED".to_string(),
                message: description.clone(),
                is_policy_denial: true,
            },
            Self::Forbidden {
                reason,
                missing_scopes,
            } => DirectApiOutcome::HardFailure {
                code: "FORBIDDEN".to_string(),
                message: format!("{reason} (missing: {missing_scopes:?})"),
                is_policy_denial: true,
            },
            Self::TypedError {
                code,
                message,
                retryable,
            } => match code {
                DirectApiErrorCode::RateLimited | DirectApiErrorCode::ServiceUnavailable => {
                    DirectApiOutcome::TypedUnavailable {
                        reason: message.clone(),
                        can_fallback_to_tabs: true,
                    }
                }
                DirectApiErrorCode::ScopeDenied
                | DirectApiErrorCode::MissingCredentials
                | DirectApiErrorCode::InvalidAuthHandle
                | DirectApiErrorCode::ConfirmationRequired
                | DirectApiErrorCode::InvalidApprovalToken
                | DirectApiErrorCode::ApprovalTokenAlreadyUsed => DirectApiOutcome::HardFailure {
                    code: code.to_string().to_uppercase(),
                    message: message.clone(),
                    is_policy_denial: true,
                },
                _ => DirectApiOutcome::HardFailure {
                    code: code.to_string().to_uppercase(),
                    message: message.clone(),
                    is_policy_denial: !retryable,
                },
            },
            Self::Cancelled { reason } => DirectApiOutcome::HardFailure {
                code: "CANCELLED".to_string(),
                message: reason.clone(),
                is_policy_denial: false,
            },
        }
    }
}

/// Request to execute an operation via the direct API broker.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DirectApiExecutionRequest {
    pub operation: DirectApiOperation,
    pub context: DirectApiExecutionContext,
    pub approval_token: Option<String>,
    pub requires_confirmation: bool,
}

impl DirectApiExecutionRequest {
    pub fn new(operation: DirectApiOperation, context: DirectApiExecutionContext) -> Self {
        let requires_confirmation = !operation.read_only;
        Self {
            operation,
            context,
            approval_token: None,
            requires_confirmation,
        }
    }

    pub fn with_approval_token(mut self, token: impl Into<String>) -> Self {
        self.approval_token = Some(token.into());
        self
    }

    pub fn with_requires_confirmation(mut self, required: bool) -> Self {
        self.requires_confirmation = required;
        self
    }
}

/// Opaque authorization handle referencing secure vault storage.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct OpaqueAuthHandle(pub String);

impl OpaqueAuthHandle {
    pub fn new(handle: impl Into<String>) -> Self {
        Self(handle.into())
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// Vault credential containing zeroizable secret string, never exposed to model context.
#[derive(Clone)]
pub struct VaultCredential {
    pub token_type: String,
    secret: secrecy::SecretString,
}

impl VaultCredential {
    pub fn bearer(token: impl Into<String>) -> Self {
        Self {
            token_type: "Bearer".to_string(),
            secret: secrecy::SecretString::new(token.into().into_boxed_str()),
        }
    }

    pub fn new(token_type: impl Into<String>, secret: impl Into<String>) -> Self {
        Self {
            token_type: token_type.into(),
            secret: secrecy::SecretString::new(secret.into().into_boxed_str()),
        }
    }

    pub fn format_auth_header(&self) -> String {
        use secrecy::ExposeSecret;
        format!("{} {}", self.token_type, self.secret.expose_secret())
    }
}

impl std::fmt::Debug for VaultCredential {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "VaultCredential({} [REDACTED])", self.token_type)
    }
}

/// Trait for vault-style credential storage.
pub trait CredentialVault: Send + Sync {
    fn get_credential(&self, handle: &str) -> Option<VaultCredential>;
    fn store_credential(&self, handle: String, cred: VaultCredential);
}

/// In-memory implementation of CredentialVault for testing.
#[derive(Default)]
pub struct InMemoryCredentialVault {
    credentials: Mutex<HashMap<String, VaultCredential>>,
}

impl InMemoryCredentialVault {
    pub fn new() -> Self {
        Self {
            credentials: Mutex::new(HashMap::new()),
        }
    }
}

impl CredentialVault for InMemoryCredentialVault {
    fn get_credential(&self, handle: &str) -> Option<VaultCredential> {
        let guard = self
            .credentials
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        guard.get(handle).cloned()
    }

    fn store_credential(&self, handle: String, cred: VaultCredential) {
        let mut guard = self
            .credentials
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        guard.insert(handle, cred);
    }
}

/// Single-use approval token.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct ApprovalToken(pub String);

/// Store managing single-use confirmation approval tokens.
pub trait ApprovalTokenStore: Send + Sync {
    fn issue_token(&self, action_id: &str) -> ApprovalToken;
    fn consume_token(&self, action_id: &str, token: &str) -> Result<(), DirectApiErrorCode>;
}

/// In-memory implementation of ApprovalTokenStore.
#[derive(Default)]
pub struct InMemoryApprovalTokenStore {
    valid_tokens: Mutex<HashMap<String, HashSet<String>>>,
    consumed_tokens: Mutex<HashSet<String>>,
}

impl InMemoryApprovalTokenStore {
    pub fn new() -> Self {
        Self {
            valid_tokens: Mutex::new(HashMap::new()),
            consumed_tokens: Mutex::new(HashSet::new()),
        }
    }
}

impl ApprovalTokenStore for InMemoryApprovalTokenStore {
    fn issue_token(&self, action_id: &str) -> ApprovalToken {
        let token_value = format!("appr_{}", uuid::Uuid::new_v4());
        let mut valid = self
            .valid_tokens
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        valid
            .entry(action_id.to_string())
            .or_default()
            .insert(token_value.clone());
        ApprovalToken(token_value)
    }

    fn consume_token(&self, action_id: &str, token: &str) -> Result<(), DirectApiErrorCode> {
        let mut consumed = self
            .consumed_tokens
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if consumed.contains(token) {
            return Err(DirectApiErrorCode::ApprovalTokenAlreadyUsed);
        }

        let mut valid = self
            .valid_tokens
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if let Some(tokens_for_action) = valid.get_mut(action_id) {
            if tokens_for_action.remove(token) {
                consumed.insert(token.to_string());
                return Ok(());
            }
        }

        Err(DirectApiErrorCode::InvalidApprovalToken)
    }
}

/// Registry tracking granted scopes per account/handle and service.
pub trait ScopeRegistry: Send + Sync {
    fn get_granted_scopes(
        &self,
        handle_or_account: &str,
        service: DirectApiServiceKind,
    ) -> HashSet<String>;
    fn grant_scopes(
        &self,
        handle_or_account: &str,
        service: DirectApiServiceKind,
        scopes: Vec<String>,
    );
    fn check_scopes(
        &self,
        handle_or_account: &str,
        service: DirectApiServiceKind,
        required: &[String],
    ) -> Result<(), Vec<String>> {
        let granted = self.get_granted_scopes(handle_or_account, service);
        let missing: Vec<String> = required
            .iter()
            .filter(|r| !granted.contains(r.as_str()))
            .cloned()
            .collect();
        if missing.is_empty() {
            Ok(())
        } else {
            Err(missing)
        }
    }
}

/// In-memory implementation of ScopeRegistry.
#[derive(Default)]
pub struct InMemoryScopeRegistry {
    grants: Mutex<HashMap<(String, DirectApiServiceKind), HashSet<String>>>,
}

impl InMemoryScopeRegistry {
    pub fn new() -> Self {
        Self {
            grants: Mutex::new(HashMap::new()),
        }
    }
}

impl ScopeRegistry for InMemoryScopeRegistry {
    fn get_granted_scopes(
        &self,
        handle_or_account: &str,
        service: DirectApiServiceKind,
    ) -> HashSet<String> {
        let guard = self
            .grants
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        guard
            .get(&(handle_or_account.to_string(), service))
            .cloned()
            .unwrap_or_default()
    }

    fn grant_scopes(
        &self,
        handle_or_account: &str,
        service: DirectApiServiceKind,
        scopes: Vec<String>,
    ) {
        let mut guard = self
            .grants
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let entry = guard
            .entry((handle_or_account.to_string(), service))
            .or_default();
        for s in scopes {
            entry.insert(s);
        }
    }
}

/// Internal request dispatched to the transport layer.
///
/// Invariant: Contains injected Authorization headers. Never leaked to model context.
#[derive(Debug, Clone, PartialEq)]
pub struct TransportRequest {
    pub service: DirectApiServiceKind,
    pub operation_name: String,
    pub method: String,
    pub headers: HashMap<String, String>,
    pub body: Option<serde_json::Value>,
}

/// Response returned from the transport layer.
#[derive(Debug, Clone, PartialEq)]
pub struct TransportResponse {
    pub status_code: u16,
    pub body: serde_json::Value,
    pub headers: HashMap<String, String>,
}

/// Errors originating in the transport layer.
#[derive(Debug, Clone, thiserror::Error)]
pub enum TransportError {
    #[error("Network connection failed: {0}")]
    NetworkError(String),
    #[error("Request timed out")]
    Timeout,
    #[error("Operation cancelled mid-flight")]
    Cancelled,
}

/// Direct API transport trait.
#[async_trait]
pub trait DirectApiTransport: Send + Sync {
    async fn send(&self, request: TransportRequest) -> Result<TransportResponse, TransportError>;
}

/// Canned mock transport for tests with header and invocation inspection.
pub struct InMemoryDirectApiTransport {
    canned_responses:
        Mutex<HashMap<(DirectApiServiceKind, String), Result<TransportResponse, TransportError>>>,
    sent_requests: Mutex<Vec<TransportRequest>>,
}

impl InMemoryDirectApiTransport {
    pub fn new() -> Self {
        Self {
            canned_responses: Mutex::new(HashMap::new()),
            sent_requests: Mutex::new(Vec::new()),
        }
    }

    pub fn set_response(
        &self,
        service: DirectApiServiceKind,
        operation_name: impl Into<String>,
        response: Result<TransportResponse, TransportError>,
    ) {
        let mut guard = self
            .canned_responses
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        guard.insert((service, operation_name.into()), response);
    }

    pub fn get_sent_requests(&self) -> Vec<TransportRequest> {
        let guard = self
            .sent_requests
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        guard.clone()
    }

    pub fn sent_count(&self) -> usize {
        let guard = self
            .sent_requests
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        guard.len()
    }
}

#[async_trait]
impl DirectApiTransport for InMemoryDirectApiTransport {
    async fn send(&self, request: TransportRequest) -> Result<TransportResponse, TransportError> {
        {
            let mut sent = self
                .sent_requests
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            sent.push(request.clone());
        }

        let guard = self
            .canned_responses
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if let Some(res) = guard.get(&(request.service, request.operation_name.clone())) {
            res.clone()
        } else {
            Ok(TransportResponse {
                status_code: 200,
                body: serde_json::json!({
                    "status": "default_ok",
                    "service": request.service.to_string(),
                    "op": request.operation_name
                }),
                headers: HashMap::new(),
            })
        }
    }
}

/// Direct API Broker trait.
#[async_trait]
pub trait DirectApiBroker: Send + Sync {
    async fn execute(&self, request: DirectApiExecutionRequest) -> DirectApiExecutionOutcome;
}

/// In-memory direct API broker implementation.
pub struct InMemoryDirectApiBroker {
    pub vault: Arc<dyn CredentialVault>,
    pub approval_store: Arc<dyn ApprovalTokenStore>,
    pub scope_registry: Arc<dyn ScopeRegistry>,
    pub transport: Arc<dyn DirectApiTransport>,
}

impl InMemoryDirectApiBroker {
    pub fn new(
        vault: Arc<dyn CredentialVault>,
        approval_store: Arc<dyn ApprovalTokenStore>,
        scope_registry: Arc<dyn ScopeRegistry>,
        transport: Arc<dyn DirectApiTransport>,
    ) -> Self {
        Self {
            vault,
            approval_store,
            scope_registry,
            transport,
        }
    }

    pub fn default_test() -> Self {
        Self {
            vault: Arc::new(InMemoryCredentialVault::new()),
            approval_store: Arc::new(InMemoryApprovalTokenStore::new()),
            scope_registry: Arc::new(InMemoryScopeRegistry::new()),
            transport: Arc::new(InMemoryDirectApiTransport::new()),
        }
    }
}

#[async_trait]
impl DirectApiBroker for InMemoryDirectApiBroker {
    async fn execute(&self, request: DirectApiExecutionRequest) -> DirectApiExecutionOutcome {
        // Step 1: Malformed input validation
        if request.operation.operation_name.trim().is_empty() {
            return DirectApiExecutionOutcome::TypedError {
                code: DirectApiErrorCode::BadRequest,
                message: "Operation name cannot be empty".to_string(),
                retryable: false,
            };
        }

        let action_id = format!(
            "{}:{}",
            request.operation.service, request.operation.operation_name
        );

        // Step 2: Confirmation check (Fail-closed)
        if request.requires_confirmation {
            match &request.approval_token {
                None => {
                    let _ = self.approval_store.issue_token(&action_id);
                    return DirectApiExecutionOutcome::NeedsConfirmation {
                        action_id,
                        description: format!(
                            "Action {} on service {} requires explicit user confirmation",
                            request.operation.operation_name, request.operation.service
                        ),
                        required_scopes: request.operation.required_scopes.clone(),
                    };
                }
                Some(token) => {
                    if let Err(err_code) = self.approval_store.consume_token(&action_id, token) {
                        return DirectApiExecutionOutcome::TypedError {
                            code: err_code,
                            message: format!("Confirmation verification failed: {err_code}"),
                            retryable: false,
                        };
                    }
                }
            }
        }

        // Step 3: Auth handle resolution and Scope enforcement
        let auth_target = match request
            .context
            .opaque_auth_handle
            .as_deref()
            .or(request.context.account_id.as_deref())
        {
            Some(target) if !target.trim().is_empty() => target,
            _ => {
                return DirectApiExecutionOutcome::TypedError {
                    code: DirectApiErrorCode::MissingCredentials,
                    message: "No authentication handle or account ID provided in execution context"
                        .to_string(),
                    retryable: false,
                };
            }
        };

        if !request.operation.required_scopes.is_empty() {
            if let Err(missing) = self.scope_registry.check_scopes(
                auth_target,
                request.operation.service,
                &request.operation.required_scopes,
            ) {
                return DirectApiExecutionOutcome::Forbidden {
                    reason: format!(
                        "Scope enforcement failed for service {}",
                        request.operation.service
                    ),
                    missing_scopes: missing,
                };
            }
        }

        // Step 4: Vault credential resolution & header injection
        let handle = match request.context.opaque_auth_handle.as_deref() {
            Some(h) => h,
            None => auth_target,
        };

        let cred = match self.vault.get_credential(handle) {
            Some(c) => c,
            None => {
                return DirectApiExecutionOutcome::TypedError {
                    code: DirectApiErrorCode::InvalidAuthHandle,
                    message: format!("Credential not found for handle '{handle}'"),
                    retryable: false,
                };
            }
        };

        let mut headers = HashMap::new();
        headers.insert("Authorization".to_string(), cred.format_auth_header());
        headers.insert("Content-Type".to_string(), "application/json".to_string());

        let method = if request.operation.read_only {
            "GET".to_string()
        } else {
            "POST".to_string()
        };

        let transport_req = TransportRequest {
            service: request.operation.service,
            operation_name: request.operation.operation_name.clone(),
            method,
            headers,
            body: Some(request.operation.parameters.clone()),
        };

        // Step 5: Transport execution and outcome mapping
        match self.transport.send(transport_req).await {
            Ok(resp) => match resp.status_code {
                200..=299 => DirectApiExecutionOutcome::Ok {
                    service: request.operation.service,
                    operation_name: request.operation.operation_name,
                    payload: resp.body,
                },
                401 | 403 => DirectApiExecutionOutcome::Forbidden {
                    reason: format!(
                        "Upstream service rejected authorization ({})",
                        resp.status_code
                    ),
                    missing_scopes: Vec::new(),
                },
                429 => DirectApiExecutionOutcome::TypedError {
                    code: DirectApiErrorCode::RateLimited,
                    message: "Rate limit exceeded".to_string(),
                    retryable: true,
                },
                400 => DirectApiExecutionOutcome::TypedError {
                    code: DirectApiErrorCode::BadRequest,
                    message: sanitize_error_payload(&resp.body),
                    retryable: false,
                },
                404 => DirectApiExecutionOutcome::TypedError {
                    code: DirectApiErrorCode::ResourceNotFound,
                    message: sanitize_error_payload(&resp.body),
                    retryable: false,
                },
                500..=599 => DirectApiExecutionOutcome::TypedError {
                    code: DirectApiErrorCode::ServiceUnavailable,
                    message: format!("Service unavailable: {}", resp.status_code),
                    retryable: true,
                },
                other => DirectApiExecutionOutcome::TypedError {
                    code: DirectApiErrorCode::InternalError,
                    message: format!("Unexpected response status: {other}"),
                    retryable: false,
                },
            },
            Err(TransportError::Cancelled) => DirectApiExecutionOutcome::Cancelled {
                reason: "Direct API operation was cancelled mid-flight".to_string(),
            },
            Err(TransportError::Timeout) => DirectApiExecutionOutcome::TypedError {
                code: DirectApiErrorCode::NetworkError,
                message: "Direct API transport request timed out".to_string(),
                retryable: true,
            },
            Err(TransportError::NetworkError(msg)) => DirectApiExecutionOutcome::TypedError {
                code: DirectApiErrorCode::NetworkError,
                message: format!("Network error: {msg}"),
                retryable: true,
            },
        }
    }
}

/// Sanitizes backend error responses to prevent raw HTML dumps from entering model context.
fn sanitize_error_payload(body: &serde_json::Value) -> String {
    if let Some(msg) = body.get("message").and_then(|m| m.as_str()) {
        return msg.to_string();
    }
    if let Some(err) = body.get("error").and_then(|e| e.as_str()) {
        return err.to_string();
    }
    let raw = body.to_string();
    if raw.contains("<html") || raw.contains("<HTML") {
        "HTML error response from upstream service".to_string()
    } else {
        raw
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::retry_policy::{ErrorClass, IdempotencyClass, RetryPolicy};

    // (a) Credential resolution NEVER returns raw token to model context;
    // broker injects Authorization header at transport layer;
    // model-facing op/request struct contains provider + scopes only (assert no credential field serializes).
    #[tokio::test]
    async fn test_credential_resolution_never_returns_raw_token_to_model_context_header_injection_probe(
    ) {
        let vault = Arc::new(InMemoryCredentialVault::new());
        let approval_store = Arc::new(InMemoryApprovalTokenStore::new());
        let scope_registry = Arc::new(InMemoryScopeRegistry::new());
        let transport = Arc::new(InMemoryDirectApiTransport::new());

        let raw_secret = "secret-token-super-private-987654321";
        vault.store_credential(
            "gmail-handle-1".to_string(),
            VaultCredential::bearer(raw_secret),
        );

        scope_registry.grant_scopes(
            "gmail-handle-1",
            DirectApiServiceKind::Gmail,
            vec!["gmail.readonly".to_string()],
        );

        transport.set_response(
            DirectApiServiceKind::Gmail,
            "messages.list",
            Ok(TransportResponse {
                status_code: 200,
                body: serde_json::json!({ "messages": ["msg_1", "msg_2"] }),
                headers: HashMap::new(),
            }),
        );

        let broker =
            InMemoryDirectApiBroker::new(vault, approval_store, scope_registry, transport.clone());

        let op = DirectApiOperation::new(
            DirectApiServiceKind::Gmail,
            "messages.list",
            serde_json::json!({ "q": "label:unread" }),
            true,
        )
        .with_scopes(vec!["gmail.readonly".to_string()]);

        // Model-facing representation assertion
        let model_req = ModelFacingDirectApiRequest::from(&op);
        let model_json = serde_json::to_string(&model_req).expect("serialize model req");
        assert!(!model_json.contains(raw_secret));
        assert!(model_json.contains("gmail"));
        assert!(model_json.contains("messages.list"));
        assert!(model_json.contains("gmail.readonly"));

        let exec_req = DirectApiExecutionRequest::new(
            op,
            DirectApiExecutionContext::new("session-1").with_auth_handle("gmail-handle-1"),
        );

        // Serialization probe on execution request
        let exec_json = serde_json::to_string(&exec_req).expect("serialize exec req");
        assert!(!exec_json.contains(raw_secret));

        // Execute via broker
        let outcome = broker.execute(exec_req).await;

        // Model-facing outcome contains payload, never raw secret
        match &outcome {
            DirectApiExecutionOutcome::Ok {
                service,
                operation_name,
                payload,
            } => {
                assert_eq!(*service, DirectApiServiceKind::Gmail);
                assert_eq!(operation_name, "messages.list");
                let payload_str = serde_json::to_string(payload).unwrap();
                assert!(!payload_str.contains(raw_secret));
                assert_eq!(payload["messages"][0], "msg_1");
            }
            other => panic!("Expected Ok outcome, got {other:?}"),
        }

        // Transport header injection assertion: Transport received injected Authorization header
        let sent = transport.get_sent_requests();
        assert_eq!(sent.len(), 1);
        let auth_hdr = sent[0]
            .headers
            .get("Authorization")
            .expect("Auth header injected");
        assert_eq!(auth_hdr, &format!("Bearer {raw_secret}"));
    }

    // (b) execute(op) with confirmation-required op returns NeedsConfirmation (fail-closed)
    // until an explicit approval token is supplied; approval token single-use (second use rejected).
    #[tokio::test]
    async fn test_execute_confirmation_required_fail_closed_and_single_use_token() {
        let vault = Arc::new(InMemoryCredentialVault::new());
        let approval_store = Arc::new(InMemoryApprovalTokenStore::new());
        let scope_registry = Arc::new(InMemoryScopeRegistry::new());
        let transport = Arc::new(InMemoryDirectApiTransport::new());

        vault.store_credential(
            "slack-handle".to_string(),
            VaultCredential::bearer("slack-tok"),
        );

        let broker = InMemoryDirectApiBroker::new(
            vault,
            approval_store.clone(),
            scope_registry,
            transport.clone(),
        );

        let op = DirectApiOperation::new(
            DirectApiServiceKind::Slack,
            "chat.postMessage",
            serde_json::json!({ "channel": "C123", "text": "Hello world" }),
            false, // Mutating write op -> requires confirmation
        );

        let req = DirectApiExecutionRequest::new(
            op.clone(),
            DirectApiExecutionContext::new("session-1").with_auth_handle("slack-handle"),
        );

        // 1. Initial invocation without approval token -> NeedsConfirmation (Fail-closed)
        let outcome1 = broker.execute(req).await;
        let action_id = match outcome1 {
            DirectApiExecutionOutcome::NeedsConfirmation { action_id, .. } => {
                assert_eq!(action_id, "slack:chat.postMessage");
                action_id
            }
            other => panic!("Expected NeedsConfirmation, got {other:?}"),
        };
        // Transport was NOT called
        assert_eq!(transport.sent_count(), 0);

        // 2. Issue approval token for action_id
        let token = approval_store.issue_token(&action_id);

        // 3. First execution with approval token -> Succeeds
        let req_approved = DirectApiExecutionRequest::new(
            op.clone(),
            DirectApiExecutionContext::new("session-1").with_auth_handle("slack-handle"),
        )
        .with_approval_token(&token.0);

        let outcome2 = broker.execute(req_approved.clone()).await;
        assert!(matches!(outcome2, DirectApiExecutionOutcome::Ok { .. }));
        assert_eq!(transport.sent_count(), 1);

        // 4. Second execution with the SAME approval token -> REJECTED (single-use invariant)
        let outcome3 = broker.execute(req_approved).await;
        match outcome3 {
            DirectApiExecutionOutcome::TypedError {
                code, retryable, ..
            } => {
                assert_eq!(code, DirectApiErrorCode::ApprovalTokenAlreadyUsed);
                assert!(!retryable);
            }
            other => panic!("Expected ApprovalTokenAlreadyUsed, got {other:?}"),
        }
        // Transport count still 1 (no second network dispatch)
        assert_eq!(transport.sent_count(), 1);
    }

    // (c) typed outcomes per service kind (Gmail/Calendar/Sheets/Slack/Discord/Telegram):
    // Ok(payload), TypedError(code, retryable), Forbidden — never raw HTTP dumps.
    #[tokio::test]
    async fn test_typed_outcomes_per_service_kind_never_raw_http_dumps() {
        let vault = Arc::new(InMemoryCredentialVault::new());
        let approval_store = Arc::new(InMemoryApprovalTokenStore::new());
        let scope_registry = Arc::new(InMemoryScopeRegistry::new());
        let transport = Arc::new(InMemoryDirectApiTransport::new());

        let services = vec![
            (DirectApiServiceKind::Gmail, "users.messages.get"),
            (DirectApiServiceKind::GoogleCalendar, "events.list"),
            (
                DirectApiServiceKind::GoogleSheets,
                "spreadsheets.values.get",
            ),
            (DirectApiServiceKind::GoogleDrive, "files.list"),
            (DirectApiServiceKind::Slack, "conversations.history"),
            (DirectApiServiceKind::Discord, "channels.messages.list"),
            (DirectApiServiceKind::Telegram, "getUpdates"),
        ];

        for (service, op_name) in &services {
            let handle = format!("{service}-handle");
            vault.store_credential(handle.clone(), VaultCredential::bearer("test-tok"));
            transport.set_response(
                *service,
                *op_name,
                Ok(TransportResponse {
                    status_code: 200,
                    body: serde_json::json!({ "service": service.to_string(), "status": "ok" }),
                    headers: HashMap::new(),
                }),
            );

            let broker = InMemoryDirectApiBroker::new(
                vault.clone(),
                approval_store.clone(),
                scope_registry.clone(),
                transport.clone(),
            );

            let op = DirectApiOperation::new(*service, *op_name, serde_json::json!({}), true);
            let req = DirectApiExecutionRequest::new(
                op,
                DirectApiExecutionContext::new("session").with_auth_handle(handle),
            );

            let res = broker.execute(req).await;
            match res {
                DirectApiExecutionOutcome::Ok {
                    service: s,
                    operation_name: o,
                    payload,
                } => {
                    assert_eq!(s, *service);
                    assert_eq!(o, *op_name);
                    assert_eq!(payload["status"], "ok");
                }
                other => panic!("Expected Ok for {service:?}, got {other:?}"),
            }
        }

        // Test error types and HTML dump protection
        let broker = InMemoryDirectApiBroker::new(
            vault.clone(),
            approval_store.clone(),
            scope_registry.clone(),
            transport.clone(),
        );

        // 403 Forbidden
        transport.set_response(
            DirectApiServiceKind::Gmail,
            "forbidden_op",
            Ok(TransportResponse {
                status_code: 403,
                body: serde_json::json!({ "error": "Unauthorized" }),
                headers: HashMap::new(),
            }),
        );
        let req_forbidden = DirectApiExecutionRequest::new(
            DirectApiOperation::new(
                DirectApiServiceKind::Gmail,
                "forbidden_op",
                serde_json::json!({}),
                true,
            ),
            DirectApiExecutionContext::new("s").with_auth_handle("gmail-handle"),
        );
        let out_forbidden = broker.execute(req_forbidden).await;
        assert!(matches!(
            out_forbidden,
            DirectApiExecutionOutcome::Forbidden { .. }
        ));

        // 429 RateLimited
        transport.set_response(
            DirectApiServiceKind::GoogleCalendar,
            "rate_limited_op",
            Ok(TransportResponse {
                status_code: 429,
                body: serde_json::json!({ "error": "Too Many Requests" }),
                headers: HashMap::new(),
            }),
        );
        let req_429 = DirectApiExecutionRequest::new(
            DirectApiOperation::new(
                DirectApiServiceKind::GoogleCalendar,
                "rate_limited_op",
                serde_json::json!({}),
                true,
            ),
            DirectApiExecutionContext::new("s").with_auth_handle("google_calendar-handle"),
        );
        let out_429 = broker.execute(req_429).await;
        match out_429 {
            DirectApiExecutionOutcome::TypedError {
                code, retryable, ..
            } => {
                assert_eq!(code, DirectApiErrorCode::RateLimited);
                assert!(retryable);
            }
            other => panic!("Expected RateLimited, got {other:?}"),
        }

        // Raw HTML dump sanitization
        transport.set_response(
            DirectApiServiceKind::GoogleSheets,
            "bad_gateway",
            Ok(TransportResponse {
                status_code: 400,
                body: serde_json::json!(
                    "<!DOCTYPE html><html><body><h1>502 Bad Gateway</h1></body></html>"
                ),
                headers: HashMap::new(),
            }),
        );
        let req_html = DirectApiExecutionRequest::new(
            DirectApiOperation::new(
                DirectApiServiceKind::GoogleSheets,
                "bad_gateway",
                serde_json::json!({}),
                true,
            ),
            DirectApiExecutionContext::new("s").with_auth_handle("google_sheets-handle"),
        );
        let out_html = broker.execute(req_html).await;
        match out_html {
            DirectApiExecutionOutcome::TypedError { code, message, .. } => {
                assert_eq!(code, DirectApiErrorCode::BadRequest);
                assert!(!message.contains("<!DOCTYPE"));
                assert_eq!(message, "HTML error response from upstream service");
            }
            other => panic!("Expected sanitized TypedError, got {other:?}"),
        }
    }

    // (d) scope enforcement: op requesting scope not granted -> Forbidden / ScopeDenied without network
    #[tokio::test]
    async fn test_scope_enforcement_op_requesting_scope_not_granted_fails_closed_without_network() {
        let vault = Arc::new(InMemoryCredentialVault::new());
        let approval_store = Arc::new(InMemoryApprovalTokenStore::new());
        let scope_registry = Arc::new(InMemoryScopeRegistry::new());
        let transport = Arc::new(InMemoryDirectApiTransport::new());

        vault.store_credential("acc-1".to_string(), VaultCredential::bearer("tok-acc-1"));
        scope_registry.grant_scopes(
            "acc-1",
            DirectApiServiceKind::Gmail,
            vec!["gmail.readonly".to_string()],
        );

        let broker =
            InMemoryDirectApiBroker::new(vault, approval_store, scope_registry, transport.clone());

        let op_unauthorized_scope = DirectApiOperation::new(
            DirectApiServiceKind::Gmail,
            "messages.send",
            serde_json::json!({ "to": "user@example.com" }),
            true, // test scope check
        )
        .with_scopes(vec!["gmail.send".to_string(), "gmail.readonly".to_string()]);

        let req = DirectApiExecutionRequest::new(
            op_unauthorized_scope,
            DirectApiExecutionContext::new("s").with_auth_handle("acc-1"),
        );

        let outcome = broker.execute(req).await;
        match outcome {
            DirectApiExecutionOutcome::Forbidden {
                missing_scopes,
                reason,
            } => {
                assert_eq!(missing_scopes, vec!["gmail.send".to_string()]);
                assert!(reason.contains("Scope enforcement failed"));
            }
            other => panic!("Expected Forbidden, got {other:?}"),
        }

        // Invariant: ZERO network/transport requests dispatched
        assert_eq!(transport.sent_count(), 0);
    }

    // (e) cancel mid-op -> Cancelled, no auto-retry (delegates to retry_policy's UncertainMutation rule)
    #[tokio::test]
    async fn test_cancel_mid_op_no_auto_retry_delegates_to_retry_policy() {
        let vault = Arc::new(InMemoryCredentialVault::new());
        let approval_store = Arc::new(InMemoryApprovalTokenStore::new());
        let scope_registry = Arc::new(InMemoryScopeRegistry::new());
        let transport = Arc::new(InMemoryDirectApiTransport::new());

        vault.store_credential("tg-handle".to_string(), VaultCredential::bearer("tg-tok"));
        transport.set_response(
            DirectApiServiceKind::Telegram,
            "sendMessage",
            Err(TransportError::Cancelled),
        );

        let broker =
            InMemoryDirectApiBroker::new(vault, approval_store.clone(), scope_registry, transport);

        let action_id = "telegram:sendMessage";
        let token = approval_store.issue_token(action_id);

        let op = DirectApiOperation::new(
            DirectApiServiceKind::Telegram,
            "sendMessage",
            serde_json::json!({ "chat_id": 123, "text": "Hi" }),
            false,
        );

        let req = DirectApiExecutionRequest::new(
            op,
            DirectApiExecutionContext::new("s").with_auth_handle("tg-handle"),
        )
        .with_approval_token(&token.0);

        let outcome = broker.execute(req).await;
        match outcome {
            DirectApiExecutionOutcome::Cancelled { reason } => {
                assert!(reason.contains("cancelled"));
            }
            other => panic!("Expected Cancelled, got {other:?}"),
        }

        // Delegate to retry_policy: uncertain mutation rule guarantees NO auto-retry
        let policy = RetryPolicy::default();
        let retry_eval = policy.evaluate(
            ErrorClass::UncertainMutation,
            IdempotencyClass::NonIdempotentMutation,
            1,
            0.5,
        );
        assert!(
            matches!(
                retry_eval,
                Err(crate::retry_policy::DoNotRetryReason::NeedsResolution(_))
            ),
            "Cancelled mutation must never auto-retry under retry policy"
        );
    }

    // (f) credentials in vault-style storage are referenced by handle, never inlined (struct assertion)
    #[test]
    fn test_credentials_in_vault_referenced_by_handle_never_inlined() {
        let secret_token = "ultra-secret-vault-token-xyz-123456";
        let vault = InMemoryCredentialVault::new();
        vault.store_credential(
            "opaque-handle-42".to_string(),
            VaultCredential::bearer(secret_token),
        );

        // Execution context carries handle ONLY
        let ctx = DirectApiExecutionContext::new("session-uuid-999")
            .with_auth_handle("opaque-handle-42")
            .with_account_id("acc-user-1");

        let ctx_json = serde_json::to_string(&ctx).expect("serialize context");
        assert!(!ctx_json.contains(secret_token));
        assert!(ctx_json.contains("opaque-handle-42"));

        let ctx_debug = format!("{ctx:?}");
        assert!(!ctx_debug.contains(secret_token));

        // Vault credential debug output redacts secret
        let stored_cred = vault.get_credential("opaque-handle-42").unwrap();
        let cred_debug = format!("{stored_cred:?}");
        assert!(!cred_debug.contains(secret_token));
        assert!(cred_debug.contains("[REDACTED]"));
    }

    // Adversarial: malformed input handling
    #[tokio::test]
    async fn test_adversarial_malformed_input_handling() {
        let broker = InMemoryDirectApiBroker::default_test();

        // 1. Empty operation name
        let req_empty_op = DirectApiExecutionRequest::new(
            DirectApiOperation::new(
                DirectApiServiceKind::Gmail,
                "   ",
                serde_json::json!({}),
                true,
            ),
            DirectApiExecutionContext::new("s").with_auth_handle("h1"),
        );
        let out1 = broker.execute(req_empty_op).await;
        assert!(matches!(
            out1,
            DirectApiExecutionOutcome::TypedError {
                code: DirectApiErrorCode::BadRequest,
                ..
            }
        ));

        // 2. Missing credentials / no handle
        let req_no_handle = DirectApiExecutionRequest::new(
            DirectApiOperation::new(
                DirectApiServiceKind::Gmail,
                "list",
                serde_json::json!({}),
                true,
            ),
            DirectApiExecutionContext::new("s"),
        );
        let out2 = broker.execute(req_no_handle).await;
        assert!(matches!(
            out2,
            DirectApiExecutionOutcome::TypedError {
                code: DirectApiErrorCode::MissingCredentials,
                ..
            }
        ));

        // 3. Invalid auth handle (not found in vault)
        let req_invalid_handle = DirectApiExecutionRequest::new(
            DirectApiOperation::new(
                DirectApiServiceKind::Gmail,
                "list",
                serde_json::json!({}),
                true,
            ),
            DirectApiExecutionContext::new("s").with_auth_handle("non-existent-handle"),
        );
        let out3 = broker.execute(req_invalid_handle).await;
        assert!(matches!(
            out3,
            DirectApiExecutionOutcome::TypedError {
                code: DirectApiErrorCode::InvalidAuthHandle,
                ..
            }
        ));
    }

    #[tokio::test]
    async fn test_in_memory_stores_poison_resilience() {
        // Test InMemoryCredentialVault poison resilience
        let vault = Arc::new(InMemoryCredentialVault::new());
        vault.store_credential("h1".to_string(), VaultCredential::bearer("tok1"));
        let v_clone = vault.clone();
        let _ = std::panic::catch_unwind(move || {
            let _guard = v_clone.credentials.lock().unwrap();
            panic!("poisoning credentials lock");
        });
        // Subsequent access should not panic
        let cred = vault.get_credential("h1");
        assert!(cred.is_some());
        vault.store_credential("h2".to_string(), VaultCredential::bearer("tok2"));
        assert_eq!(
            vault.get_credential("h2").unwrap().format_auth_header(),
            "Bearer tok2"
        );

        // Test InMemoryApprovalTokenStore poison resilience
        let approval_store = Arc::new(InMemoryApprovalTokenStore::new());
        let token = approval_store.issue_token("act1");
        let a_clone = approval_store.clone();
        let _ = std::panic::catch_unwind(move || {
            let _guard = a_clone.valid_tokens.lock().unwrap();
            panic!("poisoning valid_tokens lock");
        });
        let consume_res = approval_store.consume_token("act1", &token.0);
        assert!(consume_res.is_ok());

        // Test InMemoryScopeRegistry poison resilience
        let registry = Arc::new(InMemoryScopeRegistry::new());
        registry.grant_scopes(
            "h1",
            DirectApiServiceKind::Gmail,
            vec!["scope1".to_string()],
        );
        let r_clone = registry.clone();
        let _ = std::panic::catch_unwind(move || {
            let _guard = r_clone.grants.lock().unwrap();
            panic!("poisoning grants lock");
        });
        let scopes = registry.get_granted_scopes("h1", DirectApiServiceKind::Gmail);
        assert!(scopes.contains("scope1"));

        // Test InMemoryDirectApiTransport poison resilience
        let transport = Arc::new(InMemoryDirectApiTransport::new());
        transport.set_response(
            DirectApiServiceKind::Gmail,
            "op1",
            Ok(TransportResponse {
                status_code: 200,
                body: serde_json::json!({"res": "ok"}),
                headers: HashMap::new(),
            }),
        );
        let t_clone = transport.clone();
        let _ = std::panic::catch_unwind(move || {
            let _guard = t_clone.sent_requests.lock().unwrap();
            panic!("poisoning sent_requests lock");
        });
        let res = transport
            .send(TransportRequest {
                service: DirectApiServiceKind::Gmail,
                operation_name: "op1".to_string(),
                method: "POST".to_string(),
                headers: HashMap::new(),
                body: None,
            })
            .await;
        assert!(res.is_ok());
        assert_eq!(transport.sent_count(), 1);
    }

    #[tokio::test]
    async fn test_malformed_transport_responses_return_typed_errors() {
        let vault = Arc::new(InMemoryCredentialVault::new());
        let approval_store = Arc::new(InMemoryApprovalTokenStore::new());
        let scope_registry = Arc::new(InMemoryScopeRegistry::new());
        let transport = Arc::new(InMemoryDirectApiTransport::new());

        vault.store_credential("h1".to_string(), VaultCredential::bearer("tok"));
        scope_registry.grant_scopes("h1", DirectApiServiceKind::Gmail, vec!["read".to_string()]);

        let broker =
            InMemoryDirectApiBroker::new(vault, approval_store, scope_registry, transport.clone());

        // 400 Bad Request with malformed error payload
        transport.set_response(
            DirectApiServiceKind::Gmail,
            "op_bad",
            Ok(TransportResponse {
                status_code: 400,
                body: serde_json::json!({ "unrecognized": 12345 }),
                headers: HashMap::new(),
            }),
        );

        let req = DirectApiExecutionRequest::new(
            DirectApiOperation::new(
                DirectApiServiceKind::Gmail,
                "op_bad",
                serde_json::json!({}),
                true,
            )
            .with_scopes(vec!["read".to_string()]),
            DirectApiExecutionContext::new("s").with_auth_handle("h1"),
        );

        let outcome = broker.execute(req).await;
        match outcome {
            DirectApiExecutionOutcome::TypedError {
                code,
                message,
                retryable,
            } => {
                assert_eq!(code, DirectApiErrorCode::BadRequest);
                assert!(!retryable);
                assert!(!message.is_empty());
            }
            other => panic!("Expected TypedError BadRequest, got {other:?}"),
        }

        // 500 Service Unavailable
        transport.set_response(
            DirectApiServiceKind::Gmail,
            "op_err",
            Ok(TransportResponse {
                status_code: 500,
                body: serde_json::json!("raw string error"),
                headers: HashMap::new(),
            }),
        );

        let req2 = DirectApiExecutionRequest::new(
            DirectApiOperation::new(
                DirectApiServiceKind::Gmail,
                "op_err",
                serde_json::json!({}),
                true,
            )
            .with_scopes(vec!["read".to_string()]),
            DirectApiExecutionContext::new("s").with_auth_handle("h1"),
        );

        let outcome2 = broker.execute(req2).await;
        match outcome2 {
            DirectApiExecutionOutcome::TypedError {
                code,
                message,
                retryable,
            } => {
                assert_eq!(code, DirectApiErrorCode::ServiceUnavailable);
                assert!(retryable);
                assert!(message.contains("500"));
            }
            other => panic!("Expected TypedError ServiceUnavailable, got {other:?}"),
        }
    }
}
