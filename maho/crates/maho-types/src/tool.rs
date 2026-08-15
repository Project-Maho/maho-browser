use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolDescriptor {
    pub name: String, // namespaced: "mcp:server/tool", "sh:name", bare for builtin
    pub description: String,
    #[serde(rename = "inputSchema")]
    pub parameters_schema: serde_json::Value, // JSON Schema
    pub provenance: ToolProvenance,
    pub sensitive: bool, // true = always prompt per-invocation
    pub permission: ToolPermission,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ToolProvenance {
    BuiltinBrowser,
    AgentLocal,       // CLI tools, internal adapters
    External(String), // MCP server name
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ToolPermission {
    AutoApprove,
    SessionApprove,
    AlwaysAsk,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct BrowserToolPolicy {
    pub sensitive: bool,
    pub permission: ToolPermission,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct BrowserToolDescriptor {
    pub capability_id: String,
    pub name: String,
    pub description: String,
    #[serde(rename = "inputSchema")]
    pub input_schema: serde_json::Value,
    pub schema_version: u32,
    pub policy: BrowserToolPolicy,
}

pub const BROWSER_RECEIPT_SCHEMA_VERSION: u32 = 1;
pub const BROWSER_RECEIPT_RESULT_VERSION: u32 = 1;
pub const BROWSER_RECEIPT_REDACTED: &str = "[REDACTED]";

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct BrowserToolExecutionReceipt {
    pub capability_id: String,
    pub execution_id: String,
    pub metadata: serde_json::Value,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum BrowserReceiptOutcomeStatus {
    Succeeded,
    Failed,
    Denied,
    Disconnected,
    Unknown,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct BrowserReceiptProjection {
    pub schema_version: u32,
    pub result_version: u32,
    pub receipt_id: String,
    pub capability_id: String,
    pub controller: BrowserReceiptController,
    pub target: BrowserReceiptTarget,
    pub category: String,
    pub sensitivity: String,
    pub approval: String,
    pub outcome: BrowserReceiptOutcome,
    pub timestamps: BrowserReceiptTimestamps,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct BrowserReceiptController {
    pub id: Option<String>,
    pub name: String,
    pub r#type: String,
    pub plane: String,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct BrowserReceiptTarget {
    pub tab_id: Option<i64>,
    pub origin: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct BrowserReceiptOutcome {
    pub status: BrowserReceiptOutcomeStatus,
    pub code: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct BrowserReceiptTimestamps {
    pub started_at: Option<f64>,
    pub completed_at: Option<f64>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct BrowserToolExecution {
    pub output_json: String,
    pub receipt: BrowserToolExecutionReceipt,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct BrowserToolBridgeError {
    pub code: String,
    pub message: String,
    pub retryable: bool,
}

impl BrowserToolBridgeError {
    pub fn new(code: impl Into<String>, message: impl Into<String>, retryable: bool) -> Self {
        Self {
            code: code.into(),
            message: message.into(),
            retryable,
        }
    }
}

impl std::fmt::Display for BrowserToolBridgeError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for BrowserToolBridgeError {}

impl BrowserToolExecutionReceipt {
    pub fn redacted(&self) -> Self {
        let mut receipt = self.clone();
        redact_sensitive_metadata(&mut receipt.metadata);
        receipt
    }

    pub fn project(&self) -> BrowserReceiptProjection {
        let receipt = self.redacted();
        let metadata = receipt.metadata.as_object();
        let controller = object_field(metadata, &["controller"]);
        let target = object_field(metadata, &["target"]);
        let outcome = object_field(metadata, &["outcome", "result", "failure"]);
        let timestamps = object_field(metadata, &["timestamps"]);
        BrowserReceiptProjection {
            schema_version: BROWSER_RECEIPT_SCHEMA_VERSION,
            result_version: BROWSER_RECEIPT_RESULT_VERSION,
            receipt_id: nonempty(&receipt.execution_id)
                .unwrap_or("unknown")
                .to_string(),
            capability_id: nonempty(&receipt.capability_id)
                .unwrap_or("unknown")
                .to_string(),
            controller: BrowserReceiptController {
                id: string_field(controller, &["id", "sessionId", "session_id"])
                    .map(ToOwned::to_owned),
                name: string_field(controller, &["name", "displayName", "display_name"])
                    .unwrap_or("Unknown controller")
                    .to_string(),
                r#type: string_field(controller, &["type", "kind"])
                    .unwrap_or("unknown")
                    .to_string(),
                plane: string_field(controller, &["plane", "controlPlane", "control_plane"])
                    .unwrap_or("unknown")
                    .to_string(),
            },
            target: BrowserReceiptTarget {
                tab_id: integer_field(target, &["tabId", "tab_id"]),
                origin: string_field(target, &["origin"]).map(ToOwned::to_owned),
            },
            category: string_field(metadata, &["category"])
                .unwrap_or("unknown")
                .to_string(),
            sensitivity: string_field(metadata, &["sensitivity"])
                .unwrap_or("unknown")
                .to_string(),
            approval: string_field(
                metadata,
                &["approval", "approvalOutcome", "approval_outcome"],
            )
            .unwrap_or("not_requested")
            .to_string(),
            outcome: BrowserReceiptOutcome {
                status: receipt_outcome_status(
                    string_field(outcome, &["status", "state"])
                        .or_else(|| string_field(metadata, &["state", "status"])),
                ),
                code: string_field(outcome, &["code"])
                    .or_else(|| string_field(metadata, &["code"]))
                    .map(ToOwned::to_owned),
            },
            timestamps: BrowserReceiptTimestamps {
                started_at: number_field(timestamps, &["startedAt", "started_at"]).or_else(|| {
                    number_field(
                        metadata,
                        &["startedAt", "started_at", "createdAt", "created_at"],
                    )
                }),
                completed_at: number_field(timestamps, &["completedAt", "completed_at"]).or_else(
                    || {
                        number_field(
                            metadata,
                            &["completedAt", "completed_at", "updatedAt", "updated_at"],
                        )
                    },
                ),
            },
        }
    }
}

fn receipt_outcome_status(status: Option<&str>) -> BrowserReceiptOutcomeStatus {
    match status
        .unwrap_or_default()
        .to_ascii_lowercase()
        .replace('-', "_")
        .as_str()
    {
        "success" | "succeeded" | "completed" | "done" => BrowserReceiptOutcomeStatus::Succeeded,
        "failure" | "failed" | "error" => BrowserReceiptOutcomeStatus::Failed,
        "denied" | "rejected" | "approval_denied" => BrowserReceiptOutcomeStatus::Denied,
        "disconnected" | "cancelled" | "canceled" | "timeout" => {
            BrowserReceiptOutcomeStatus::Disconnected
        }
        _ => BrowserReceiptOutcomeStatus::Unknown,
    }
}

fn object_field<'a>(
    object: Option<&'a serde_json::Map<String, serde_json::Value>>,
    keys: &[&str],
) -> Option<&'a serde_json::Map<String, serde_json::Value>> {
    keys.iter().find_map(|key| object?.get(*key)?.as_object())
}

fn string_field<'a>(
    object: Option<&'a serde_json::Map<String, serde_json::Value>>,
    keys: &[&str],
) -> Option<&'a str> {
    keys.iter()
        .find_map(|key| object?.get(*key)?.as_str())
        .and_then(nonempty)
}

fn integer_field(
    object: Option<&serde_json::Map<String, serde_json::Value>>,
    keys: &[&str],
) -> Option<i64> {
    keys.iter().find_map(|key| object?.get(*key)?.as_i64())
}

fn number_field(
    object: Option<&serde_json::Map<String, serde_json::Value>>,
    keys: &[&str],
) -> Option<f64> {
    keys.iter().find_map(|key| object?.get(*key)?.as_f64())
}

fn nonempty(value: &str) -> Option<&str> {
    let trimmed = value.trim();
    (!trimmed.is_empty()).then_some(trimmed)
}

fn redact_sensitive_metadata(value: &mut serde_json::Value) {
    match value {
        serde_json::Value::Object(fields) => {
            for (key, field) in fields {
                let key = key.to_ascii_lowercase();
                if [
                    "authorization",
                    "cookie",
                    "password",
                    "passcode",
                    "secret",
                    "credential",
                    "token",
                    "otp",
                    "api_key",
                    "apikey",
                    "private_key",
                    "arguments",
                    "selector",
                    "page_text",
                    "page_payload",
                    "content",
                    "html",
                    "dom",
                    "snapshot",
                    "screenshot",
                ]
                .iter()
                .any(|fragment| key.contains(fragment))
                {
                    *field = serde_json::Value::String(BROWSER_RECEIPT_REDACTED.to_string());
                } else {
                    redact_sensitive_metadata(field);
                }
            }
        }
        serde_json::Value::Array(items) => {
            for item in items {
                redact_sensitive_metadata(item);
            }
        }
        _ => {}
    }
}

impl ToolDescriptor {
    pub fn to_openai_function(&self) -> serde_json::Value {
        let mut function = serde_json::Map::new();
        function.insert(
            "name".to_string(),
            serde_json::Value::String(self.name.clone()),
        );
        function.insert(
            "description".to_string(),
            serde_json::Value::String(self.description.clone()),
        );
        function.insert("parameters".to_string(), self.parameters_schema.clone());

        let mut root = serde_json::Map::new();
        root.insert(
            "type".to_string(),
            serde_json::Value::String("function".to_string()),
        );
        root.insert("function".to_string(), serde_json::Value::Object(function));
        serde_json::Value::Object(root)
    }
}
