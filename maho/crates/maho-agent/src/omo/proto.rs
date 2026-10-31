use serde::{Deserialize, Serialize};

#[derive(Debug, thiserror::Error)]
pub enum ProtoError {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("JSON serialization/deserialization error: {0}")]
    Json(#[from] serde_json::Error),
    #[error("UTF-8 decoding error: {0}")]
    Utf8(#[from] std::str::Utf8Error),
    #[error("Unknown error code: {0}")]
    UnknownErrorCode(String),
    #[error("Protocol error: {0}")]
    Protocol(String),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RpcErrorCode {
    UnknownSession,
    SessionClosing,
    SessionPathInUse,
    MissingSessionId,
    MultiSessionDisabled,
    InvalidPath,
    OpenFailed,
    MediaNotFound,
}

impl RpcErrorCode {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::UnknownSession => "unknown_session",
            Self::SessionClosing => "session_closing",
            Self::SessionPathInUse => "session_path_in_use",
            Self::MissingSessionId => "missing_session_id",
            Self::MultiSessionDisabled => "multi_session_disabled",
            Self::InvalidPath => "invalid_path",
            Self::OpenFailed => "open_failed",
            Self::MediaNotFound => "media_not_found",
        }
    }

    pub fn parse_from_str(s: &str) -> Option<Self> {
        match s {
            "unknown_session" => Some(Self::UnknownSession),
            "session_closing" => Some(Self::SessionClosing),
            "session_path_in_use" => Some(Self::SessionPathInUse),
            "missing_session_id" => Some(Self::MissingSessionId),
            "multi_session_disabled" => Some(Self::MultiSessionDisabled),
            "invalid_path" => Some(Self::InvalidPath),
            "open_failed" => Some(Self::OpenFailed),
            "media_not_found" => Some(Self::MediaNotFound),
            _ if s.starts_with("open_failed:") => Some(Self::OpenFailed),
            _ => None,
        }
    }
}

impl std::str::FromStr for RpcErrorCode {
    type Err = ProtoError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Self::parse_from_str(s).ok_or_else(|| ProtoError::UnknownErrorCode(s.to_string()))
    }
}

impl std::fmt::Display for RpcErrorCode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ThinkingLevel {
    Off,
    Minimal,
    Low,
    Medium,
    High,
    Xhigh,
    Max,
}

impl std::str::FromStr for ThinkingLevel {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_ascii_lowercase().as_str() {
            "off" => Ok(Self::Off),
            "minimal" => Ok(Self::Minimal),
            "low" => Ok(Self::Low),
            "medium" => Ok(Self::Medium),
            "high" => Ok(Self::High),
            "xhigh" => Ok(Self::Xhigh),
            "max" => Ok(Self::Max),
            _ => Err(()),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum StreamingBehavior {
    #[serde(rename = "steer")]
    Steer,
    #[serde(rename = "followUp")]
    FollowUp,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum SessionTitlePrompt {
    Disabled(bool),
    Custom(String),
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ImageContent {
    #[serde(rename = "type")]
    pub content_type: String,
    pub data: String,
    #[serde(rename = "mimeType")]
    pub mime_type: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum RpcCommand {
    OpenSession {
        #[serde(skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        #[serde(rename = "sessionPath", skip_serializing_if = "Option::is_none")]
        session_path: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        cwd: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        provider: Option<String>,
        #[serde(rename = "modelId", skip_serializing_if = "Option::is_none")]
        model_id: Option<String>,
        #[serde(rename = "thinkingLevel", skip_serializing_if = "Option::is_none")]
        thinking_level: Option<ThinkingLevel>,
        #[serde(rename = "permissionPreset", skip_serializing_if = "Option::is_none")]
        permission_preset: Option<String>,
    },
    Prompt {
        #[serde(skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        message: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        images: Option<Vec<ImageContent>>,
        #[serde(rename = "streamingBehavior", skip_serializing_if = "Option::is_none")]
        streaming_behavior: Option<StreamingBehavior>,
        #[serde(rename = "thinkingLevel", skip_serializing_if = "Option::is_none")]
        thinking_level: Option<ThinkingLevel>,
        #[serde(rename = "sessionTitlePrompt", skip_serializing_if = "Option::is_none")]
        session_title_prompt: Option<SessionTitlePrompt>,
        #[serde(
            rename = "expandPromptTemplates",
            skip_serializing_if = "Option::is_none"
        )]
        expand_prompt_templates: Option<bool>,
    },
    Abort {
        #[serde(skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
    },
    GetAuthProviders {
        #[serde(skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
    },
    LoginStart {
        #[serde(skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        provider: String,
    },
    LoginCancel {
        #[serde(skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        provider: String,
    },
    LoginApiKey {
        #[serde(skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        provider: String,
        key: String,
    },
}

impl RpcCommand {
    pub fn open_session(session_path: Option<String>, cwd: Option<String>) -> Self {
        Self::OpenSession {
            id: None,
            session_path,
            cwd,
            provider: None,
            model_id: None,
            thinking_level: None,
            permission_preset: None,
        }
    }

    pub fn prompt(session_id: impl Into<String>, message: impl Into<String>) -> Self {
        Self::Prompt {
            id: None,
            session_id: Some(session_id.into()),
            message: message.into(),
            images: None,
            streaming_behavior: None,
            thinking_level: None,
            session_title_prompt: None,
            expand_prompt_templates: None,
        }
    }

    pub fn abort(session_id: impl Into<String>) -> Self {
        Self::Abort {
            id: None,
            session_id: Some(session_id.into()),
        }
    }

    pub fn get_auth_providers(session_id: impl Into<String>) -> Self {
        Self::GetAuthProviders {
            id: None,
            session_id: Some(session_id.into()),
        }
    }

    pub fn login_start(session_id: impl Into<String>, provider: impl Into<String>) -> Self {
        Self::LoginStart {
            id: None,
            session_id: Some(session_id.into()),
            provider: provider.into(),
        }
    }

    pub fn login_cancel(session_id: impl Into<String>, provider: impl Into<String>) -> Self {
        Self::LoginCancel {
            id: None,
            session_id: Some(session_id.into()),
            provider: provider.into(),
        }
    }

    pub fn login_api_key(
        session_id: impl Into<String>,
        provider: impl Into<String>,
        key: impl Into<String>,
    ) -> Self {
        Self::LoginApiKey {
            id: None,
            session_id: Some(session_id.into()),
            provider: provider.into(),
            key: key.into(),
        }
    }

    pub fn with_id(mut self, id: impl Into<String>) -> Self {
        match &mut self {
            Self::OpenSession { id: target, .. }
            | Self::Prompt { id: target, .. }
            | Self::Abort { id: target, .. }
            | Self::GetAuthProviders { id: target, .. }
            | Self::LoginStart { id: target, .. }
            | Self::LoginCancel { id: target, .. }
            | Self::LoginApiKey { id: target, .. } => *target = Some(id.into()),
        }
        self
    }

    pub fn with_session_id(mut self, session_id: impl Into<String>) -> Self {
        let sid = Some(session_id.into());
        match &mut self {
            Self::OpenSession { .. } => {}
            Self::Prompt { session_id, .. }
            | Self::Abort { session_id, .. }
            | Self::GetAuthProviders { session_id, .. }
            | Self::LoginStart { session_id, .. }
            | Self::LoginCancel { session_id, .. }
            | Self::LoginApiKey { session_id, .. } => *session_id = sid,
        }
        self
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum AssistantMessageEvent {
    Start,
    TextStart {
        #[serde(rename = "contentIndex")]
        content_index: usize,
    },
    TextDelta {
        #[serde(rename = "contentIndex")]
        content_index: usize,
        delta: String,
    },
    TextEnd {
        #[serde(rename = "contentIndex")]
        content_index: usize,
        content: String,
    },
    ThinkingStart {
        #[serde(rename = "contentIndex")]
        content_index: usize,
    },
    ThinkingDelta {
        #[serde(rename = "contentIndex")]
        content_index: usize,
        delta: String,
    },
    ThinkingEnd {
        #[serde(rename = "contentIndex")]
        content_index: usize,
        content: String,
    },
    ToolcallStart {
        #[serde(rename = "contentIndex")]
        content_index: usize,
        #[serde(skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        #[serde(rename = "toolName", skip_serializing_if = "Option::is_none")]
        tool_name: Option<String>,
    },
    ToolcallDelta {
        #[serde(rename = "contentIndex")]
        content_index: usize,
        delta: String,
    },
    ToolcallEnd {
        #[serde(rename = "contentIndex")]
        content_index: usize,
        #[serde(rename = "toolCall", skip_serializing_if = "Option::is_none")]
        tool_call: Option<serde_json::Value>,
    },
    Done {
        #[serde(skip_serializing_if = "Option::is_none")]
        reason: Option<String>,
    },
    Error {
        /// pi-ai `StopReason`: the literal `"error"` or `"aborted"`, never the cause.
        #[serde(skip_serializing_if = "Option::is_none")]
        reason: Option<String>,
        /// The failed assistant message; its `errorMessage` carries the provider's text.
        #[serde(skip_serializing_if = "Option::is_none")]
        error: Option<serde_json::Value>,
    },
    #[serde(other)]
    Other,
}

impl AssistantMessageEvent {
    pub fn text_delta(&self) -> Option<&str> {
        match self {
            Self::TextDelta { delta, .. } => Some(delta.as_str()),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct Usage {
    #[serde(default)]
    pub input: u64,
    #[serde(default)]
    pub output: u64,
    #[serde(rename = "cacheRead", default)]
    pub cache_read: u64,
    #[serde(rename = "cacheWrite", default)]
    pub cache_write: u64,
    #[serde(rename = "totalTokens", default)]
    pub total_tokens: u64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum RpcEvent {
    SessionOpened {
        #[serde(rename = "sessionId")]
        session_id: String,
    },
    /// omo ended the session handle (explicit close or idle eviction, `reason:
    /// "idle_evicted"`). The handle is dead; the session reopens by path.
    SessionClosed {
        #[serde(rename = "sessionId")]
        session_id: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        reason: Option<String>,
    },
    /// omo's idle sweep parked a retained session back on disk. The handle is dead
    /// exactly as with `session_closed`; the file reopens with `open_session`.
    SessionParked {
        #[serde(rename = "sessionId")]
        session_id: String,
        #[serde(rename = "sessionPath", skip_serializing_if = "Option::is_none")]
        session_path: Option<String>,
    },
    SessionAbort {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
    },
    SessionReplaced {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(rename = "durableSessionId")]
        durable_session_id: String,
        #[serde(rename = "sessionFile", skip_serializing_if = "Option::is_none")]
        session_file: Option<String>,
        cwd: String,
        #[serde(rename = "sessionName", skip_serializing_if = "Option::is_none")]
        session_name: Option<String>,
    },
    AgentStart {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
    },
    AgentEnd {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        messages: Option<Vec<serde_json::Value>>,
    },
    AgentSettled {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
    },
    AgentIdle {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
    },
    TurnStart {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
    },
    TurnEnd {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        message: Option<serde_json::Value>,
        #[serde(rename = "toolResults", skip_serializing_if = "Option::is_none")]
        tool_results: Option<Vec<serde_json::Value>>,
    },
    MessageUpdate {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(rename = "assistantMessageEvent")]
        assistant_message_event: AssistantMessageEvent,
        #[serde(skip_serializing_if = "Option::is_none")]
        usage: Option<Usage>,
    },
    MessageStart {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        message: Option<serde_json::Value>,
    },
    MessageEnd {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        message: Option<serde_json::Value>,
    },
    Token {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        token: String,
    },
    Assistant {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        delta: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        text: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        message: Option<serde_json::Value>,
    },
    Error {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        error: String,
        #[serde(rename = "errorCode", skip_serializing_if = "Option::is_none")]
        error_code: Option<RpcErrorCode>,
    },
    ContinuationError {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(rename = "errorMessage")]
        error_message: String,
    },
    ToolExecutionStart {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(rename = "toolCallId")]
        tool_call_id: String,
        #[serde(rename = "toolName")]
        tool_name: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        args: Option<serde_json::Value>,
    },
    ToolExecutionEnd {
        #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        #[serde(rename = "toolCallId")]
        tool_call_id: String,
        #[serde(rename = "toolName")]
        tool_name: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        result: Option<serde_json::Value>,
        #[serde(rename = "isError", default)]
        is_error: bool,
    },
    #[serde(other)]
    Other,
}

impl RpcEvent {
    pub fn session_id(&self) -> Option<&str> {
        match self {
            Self::SessionOpened { session_id }
            | Self::SessionClosed { session_id, .. }
            | Self::SessionParked { session_id, .. } => Some(session_id.as_str()),
            Self::SessionAbort { session_id }
            | Self::SessionReplaced { session_id, .. }
            | Self::AgentStart { session_id }
            | Self::AgentEnd { session_id, .. }
            | Self::AgentSettled { session_id }
            | Self::AgentIdle { session_id }
            | Self::TurnStart { session_id }
            | Self::TurnEnd { session_id, .. }
            | Self::MessageUpdate { session_id, .. }
            | Self::MessageStart { session_id, .. }
            | Self::MessageEnd { session_id, .. }
            | Self::Token { session_id, .. }
            | Self::Assistant { session_id, .. }
            | Self::Error { session_id, .. }
            | Self::ContinuationError { session_id, .. }
            | Self::ToolExecutionStart { session_id, .. }
            | Self::ToolExecutionEnd { session_id, .. } => session_id.as_deref(),
            Self::Other => None,
        }
    }

    pub fn token(&self) -> Option<&str> {
        match self {
            Self::MessageUpdate {
                assistant_message_event,
                ..
            } => assistant_message_event.text_delta(),
            Self::Token { token, .. } => Some(token.as_str()),
            Self::Assistant { delta: Some(d), .. } => Some(d.as_str()),
            Self::Assistant { text: Some(t), .. } => Some(t.as_str()),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ResponseTypeMarker;

impl Serialize for ResponseTypeMarker {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        serializer.serialize_str("response")
    }
}

impl<'de> Deserialize<'de> for ResponseTypeMarker {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        if s == "response" {
            Ok(Self)
        } else {
            Err(serde::de::Error::invalid_value(
                serde::de::Unexpected::Str(&s),
                &"\"response\"",
            ))
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RpcResponse {
    #[serde(rename = "type")]
    pub response_type: ResponseTypeMarker,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    #[serde(rename = "sessionId", skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
    pub command: String,
    pub success: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<serde_json::Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    #[serde(rename = "errorCode", skip_serializing_if = "Option::is_none")]
    pub error_code: Option<RpcErrorCode>,
    #[serde(rename = "errorData", skip_serializing_if = "Option::is_none")]
    pub error_data: Option<serde_json::Value>,
}

impl RpcResponse {
    pub fn parsed_error_code(&self) -> Option<RpcErrorCode> {
        if let Some(code) = self.error_code {
            return Some(code);
        }
        if let Some(ref err) = self.error {
            return RpcErrorCode::parse_from_str(err);
        }
        None
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RpcAuthProvider {
    pub id: String,
    pub name: String,
    #[serde(rename = "authType")]
    pub auth_type: String,
    pub status: RpcAuthStatus,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RpcAuthStatus {
    pub configured: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub label: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum RpcInboundMessage {
    Response(RpcResponse),
    Event(RpcEvent),
}

#[derive(Debug, Default, Clone)]
pub struct FramingCodec {
    buffer: Vec<u8>,
}

pub type JsonLinesCodec = FramingCodec;

impl FramingCodec {
    pub fn new() -> Self {
        Self { buffer: Vec::new() }
    }

    pub fn decode(&mut self, chunk: &[u8]) -> Result<Vec<String>, ProtoError> {
        self.buffer.extend_from_slice(chunk);
        let mut messages = Vec::new();

        while let Some(pos) = self.buffer.iter().position(|&b| b == b'\n') {
            let mut line = self.buffer.drain(..=pos).collect::<Vec<u8>>();
            line.pop();
            if line.last() == Some(&b'\r') {
                line.pop();
            }
            let trimmed = match std::str::from_utf8(&line) {
                Ok(s) => s.trim(),
                Err(e) => return Err(ProtoError::from(e)),
            };
            if !trimmed.is_empty() {
                messages.push(trimmed.to_string());
            }
        }

        Ok(messages)
    }

    pub fn decode_messages<T: for<'de> Deserialize<'de>>(
        &mut self,
        chunk: &[u8],
    ) -> Result<Vec<T>, ProtoError> {
        let lines = self.decode(chunk)?;
        let mut results = Vec::with_capacity(lines.len());
        for line in lines {
            let msg = serde_json::from_str::<T>(&line)?;
            results.push(msg);
        }
        Ok(results)
    }

    pub fn pending(&self) -> &[u8] {
        &self.buffer
    }

    pub fn pending_str(&self) -> Result<&str, ProtoError> {
        std::str::from_utf8(&self.buffer).map_err(ProtoError::from)
    }

    pub fn encode<T: Serialize>(message: &T) -> Result<Vec<u8>, ProtoError> {
        let mut bytes = serde_json::to_vec(message)?;
        bytes.push(b'\n');
        Ok(bytes)
    }

    pub fn encode_string<T: Serialize>(message: &T) -> Result<String, ProtoError> {
        let mut s = serde_json::to_string(message)?;
        s.push('\n');
        Ok(s)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_prompt_command_roundtrip() {
        let prompt = RpcCommand::Prompt {
            id: Some("req-123".to_string()),
            session_id: Some("rpc-session-1".to_string()),
            message: "Hello omo".to_string(),
            images: None,
            streaming_behavior: Some(StreamingBehavior::FollowUp),
            thinking_level: Some(ThinkingLevel::High),
            session_title_prompt: Some(SessionTitlePrompt::Custom("My Title".to_string())),
            expand_prompt_templates: Some(true),
        };
        let json = serde_json::to_string(&prompt).unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed["type"], "prompt");
        assert_eq!(parsed["id"], "req-123");
        assert_eq!(parsed["sessionId"], "rpc-session-1");
        assert_eq!(parsed["message"], "Hello omo");
        assert_eq!(parsed["streamingBehavior"], "followUp");
        assert_eq!(parsed["thinkingLevel"], "high");
        assert_eq!(parsed["sessionTitlePrompt"], "My Title");
        assert_eq!(parsed["expandPromptTemplates"], true);

        let roundtripped: RpcCommand = serde_json::from_str(&json).unwrap();
        assert_eq!(roundtripped, prompt);

        // Minimal prompt constructor roundtrip
        let minimal = RpcCommand::prompt("session-42", "Simple message");
        let min_json = serde_json::to_string(&minimal).unwrap();
        let min_parsed: serde_json::Value = serde_json::from_str(&min_json).unwrap();
        assert_eq!(min_parsed["type"], "prompt");
        assert_eq!(min_parsed["sessionId"], "session-42");
        assert_eq!(min_parsed["message"], "Simple message");
        assert!(min_parsed.get("id").is_none());
        assert!(min_parsed.get("streamingBehavior").is_none());

        let min_roundtripped: RpcCommand = serde_json::from_str(&min_json).unwrap();
        assert_eq!(min_roundtripped, minimal);
    }

    #[test]
    fn test_deserialize_assistant_token_event() {
        let json = r#"{
            "type": "message_update",
            "sessionId": "session-xyz",
            "usage": {
                "input": 150,
                "output": 12,
                "cacheRead": 0,
                "cacheWrite": 0,
                "totalTokens": 162
            },
            "assistantMessageEvent": {
                "type": "text_delta",
                "contentIndex": 0,
                "delta": "Hello from assistant!"
            }
        }"#;
        let event: RpcEvent = serde_json::from_str(json).unwrap();
        assert_eq!(event.session_id(), Some("session-xyz"));
        assert_eq!(event.token(), Some("Hello from assistant!"));

        match &event {
            RpcEvent::MessageUpdate {
                session_id,
                assistant_message_event,
                usage,
            } => {
                assert_eq!(session_id.as_deref(), Some("session-xyz"));
                assert_eq!(
                    assistant_message_event,
                    &AssistantMessageEvent::TextDelta {
                        content_index: 0,
                        delta: "Hello from assistant!".to_string(),
                    }
                );
                assert_eq!(usage.as_ref().map(|u| u.total_tokens), Some(162));
            }
            _ => panic!("Expected MessageUpdate event"),
        }

        // Direct token event variant
        let token_json = r#"{"type":"token","sessionId":"s1","token":"world"}"#;
        let token_ev: RpcEvent = serde_json::from_str(token_json).unwrap();
        assert_eq!(token_ev.token(), Some("world"));

        // Direct assistant event variant
        let assistant_json = r#"{"type":"assistant","sessionId":"s1","delta":" streamed"}"#;
        let assistant_ev: RpcEvent = serde_json::from_str(assistant_json).unwrap();
        assert_eq!(assistant_ev.token(), Some(" streamed"));
    }

    #[test]
    fn test_deserialize_rpc_error_codes() {
        let expected_codes = [
            ("unknown_session", RpcErrorCode::UnknownSession),
            ("session_closing", RpcErrorCode::SessionClosing),
            ("session_path_in_use", RpcErrorCode::SessionPathInUse),
            ("missing_session_id", RpcErrorCode::MissingSessionId),
            ("multi_session_disabled", RpcErrorCode::MultiSessionDisabled),
            ("invalid_path", RpcErrorCode::InvalidPath),
            ("open_failed", RpcErrorCode::OpenFailed),
            ("media_not_found", RpcErrorCode::MediaNotFound),
        ];

        for (code_str, expected) in expected_codes {
            // Direct enum deserialization from JSON string
            let deserialized: RpcErrorCode =
                serde_json::from_str(&format!("\"{code_str}\"")).unwrap();
            assert_eq!(
                deserialized, expected,
                "Failed deserializing code: {code_str}"
            );
            assert_eq!(deserialized.as_str(), code_str);

            // Deserializing inside an error response with errorCode field
            let resp_json = format!(
                r#"{{"type":"response","command":"test","success":false,"error":"Failure","errorCode":"{code_str}"}}"#
            );
            let resp: RpcResponse = serde_json::from_str(&resp_json).unwrap();
            assert!(!resp.success);
            assert_eq!(resp.error_code, Some(expected));
            assert_eq!(resp.parsed_error_code(), Some(expected));

            // Deserializing inside an error response where error string itself holds the code
            let resp_str_json = format!(
                r#"{{"type":"response","command":"test","success":false,"error":"{code_str}"}}"#
            );
            let resp2: RpcResponse = serde_json::from_str(&resp_str_json).unwrap();
            assert_eq!(resp2.parsed_error_code(), Some(expected));
        }

        // open_failed with detail string
        let resp_detail_json = r#"{"type":"response","command":"open_session","success":false,"error":"open_failed: directory missing"}"#;
        let resp3: RpcResponse = serde_json::from_str(resp_detail_json).unwrap();
        assert_eq!(resp3.parsed_error_code(), Some(RpcErrorCode::OpenFailed));
    }

    #[test]
    fn test_framing_codec_split_and_retain_fragment() {
        let mut codec = FramingCodec::new();
        let buffer = b"{\"type\":\"prompt\",\"message\":\"msg1\"}\n{\"type\":\"prompt\",\"message\":\"msg2\"}\n{\"type\":\"prompt\",\"message\":\"msg3_part";
        let messages = codec.decode(buffer).unwrap();
        assert_eq!(messages.len(), 2, "Expected exactly two messages");
        assert_eq!(messages[0], "{\"type\":\"prompt\",\"message\":\"msg1\"}");
        assert_eq!(messages[1], "{\"type\":\"prompt\",\"message\":\"msg2\"}");
        assert_eq!(
            codec.pending(),
            b"{\"type\":\"prompt\",\"message\":\"msg3_part"
        );

        // Complete the third message with CRLF termination
        let next_chunk = b"ial_completed\"}\r\n";
        let completed = codec.decode(next_chunk).unwrap();
        assert_eq!(completed.len(), 1);
        assert_eq!(
            completed[0],
            "{\"type\":\"prompt\",\"message\":\"msg3_partial_completed\"}"
        );
        assert!(codec.pending().is_empty());

        // Typed message decoding
        let typed_buffer = b"{\"type\":\"abort\"}\n";
        let typed_messages: Vec<RpcCommand> = codec.decode_messages(typed_buffer).unwrap();
        assert_eq!(typed_messages.len(), 1);
        assert!(matches!(typed_messages[0], RpcCommand::Abort { .. }));
    }

    #[test]
    fn test_deserialize_tool_execution_events() {
        let start_json = r#"{
            "type": "tool_execution_start",
            "sessionId": "session-tool-1",
            "toolCallId": "call-1",
            "toolName": "browser_tab_list",
            "args": {}
        }"#;
        let event: RpcEvent = serde_json::from_str(start_json).unwrap();
        assert_eq!(event.session_id(), Some("session-tool-1"));
        match event {
            RpcEvent::ToolExecutionStart {
                tool_call_id,
                tool_name,
                ..
            } => {
                assert_eq!(tool_call_id, "call-1");
                assert_eq!(tool_name, "browser_tab_list");
            }
            _ => panic!("expected ToolExecutionStart"),
        }

        let end_json = r#"{
            "type": "tool_execution_end",
            "sessionId": "session-tool-1",
            "toolCallId": "call-1",
            "toolName": "browser_tab_list",
            "result": { "tabs": [{ "id": 1, "url": "https://example.com" }] },
            "isError": false
        }"#;
        let event2: RpcEvent = serde_json::from_str(end_json).unwrap();
        assert_eq!(event2.session_id(), Some("session-tool-1"));
        match event2 {
            RpcEvent::ToolExecutionEnd {
                tool_call_id,
                tool_name,
                is_error,
                ..
            } => {
                assert_eq!(tool_call_id, "call-1");
                assert_eq!(tool_name, "browser_tab_list");
                assert!(!is_error);
            }
            _ => panic!("expected ToolExecutionEnd"),
        }
    }
}
