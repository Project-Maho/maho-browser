use std::future::Future;
use std::pin::Pin;

use crate::approval::ToolSensitivity;

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct PermissionRequest {
    pub tool_name: String,
    pub arguments: String,
    pub sensitivity: ToolSensitivity,
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum PermissionDecision {
    Allow,
    Deny,
}

impl PermissionDecision {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Allow => "allow",
            Self::Deny => "deny",
        }
    }
}

impl PermissionRequest {
    pub fn new(tool_name: impl Into<String>, raw_args: &str, sensitivity: ToolSensitivity) -> Self {
        let tool_name = tool_name.into();
        Self {
            arguments: sanitize_tool_arguments_for_tool(&tool_name, raw_args),
            tool_name,
            sensitivity,
        }
    }
}

// Sanitizes raw tool-call arguments at the agent trust boundary. Valid JSON is
// recursively redacted by key; anything that does not parse as JSON fails
// closed to a fixed marker so a malformed blob can never smuggle a secret
// through verbatim. This is the single chokepoint for permission payloads and
// stream tool-call args.
pub fn sanitize_tool_arguments(raw: &str) -> String {
    sanitize_json_payload(raw, "\"[REDACTED_INVALID_ARGUMENTS]\"")
}

pub fn sanitize_tool_arguments_for_tool(tool_name: &str, raw: &str) -> String {
    let mut value = match serde_json::from_str::<serde_json::Value>(raw) {
        Ok(value) => value,
        Err(_) => return "\"[REDACTED_INVALID_ARGUMENTS]\"".to_string(),
    };
    redact_value(&mut value);
    if tool_name == "browser_type" {
        if let Some(text) = value.get_mut("text") {
            *text = serde_json::json!("[REDACTED]");
        }
    }
    value.to_string()
}

// Sanitizes a tool result before it is emitted on the stream / persisted to
// history. Same boundary as tool-call args: JSON is recursively key-redacted
// (nested objects and arrays), and non-JSON / malformed output fails closed so
// a secret can never ride out through a broken result blob.
pub fn sanitize_tool_result(raw: &str) -> String {
    sanitize_json_payload(raw, "\"[REDACTED_INVALID_RESULT]\"")
}

fn sanitize_json_payload(raw: &str, invalid_marker: &str) -> String {
    match serde_json::from_str::<serde_json::Value>(raw) {
        Ok(mut val) => {
            redact_value(&mut val);
            val.to_string()
        }
        Err(_) => invalid_marker.to_string(),
    }
}

fn key_is_sensitive(key_lower: &str) -> bool {
    const SENSITIVE_FRAGMENTS: [&str; 9] = [
        "password",
        "passcode",
        "secret",
        "totp",
        "recovery",
        "otp",
        "one-time",
        "onetime",
        "credential",
    ];
    SENSITIVE_FRAGMENTS
        .iter()
        .any(|fragment| key_lower.contains(fragment))
}

fn redact_value(val: &mut serde_json::Value) {
    match val {
        serde_json::Value::Object(map) => {
            for (k, v) in map.iter_mut() {
                if key_is_sensitive(&k.to_lowercase()) {
                    *v = serde_json::json!("[REDACTED]");
                } else {
                    redact_value(v);
                }
            }
        }
        serde_json::Value::Array(arr) => {
            for item in arr.iter_mut() {
                redact_value(item);
            }
        }
        _ => {}
    }
}

pub type PermissionCallback = Box<
    dyn Fn(PermissionRequest) -> Pin<Box<dyn Future<Output = PermissionDecision> + Send>>
        + Send
        + Sync
        + 'static,
>;

#[cfg(test)]
mod tests {
    use super::*;

    const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

    #[test]
    fn test_permission_request_redacts_credentials() {
        let req = PermissionRequest::new(
            "browser_type",
            r#"{"ref": 1, "password": "super_secret_123"}"#,
            ToolSensitivity::Sensitive,
        );
        assert!(!req.arguments.contains("super_secret_123"));
        assert!(req.arguments.contains("[REDACTED]"));
    }

    #[test]
    fn browser_type_text_is_redacted() {
        let req = PermissionRequest::new(
            "browser_type",
            r#"{"ref": 2, "text": "hello world"}"#,
            ToolSensitivity::Sensitive,
        );
        assert!(!req.arguments.contains("hello world"));
        assert!(req.arguments.contains("[REDACTED]"));
    }

    #[test]
    fn credential_class_keys_are_redacted() {
        let raw = format!(
            r#"{{"password":"{s}","current-password":"{s}","one-time-code":"{s}","otp":"{s}","recovery_code":"{s}","totp":"{s}"}}"#,
            s = SENTINEL
        );
        let req = PermissionRequest::new("browser_type", &raw, ToolSensitivity::Sensitive);
        assert!(
            !req.arguments.contains(SENTINEL),
            "sentinel leaked through credential-key redaction: {}",
            req.arguments
        );
    }

    #[test]
    fn nested_credential_values_are_redacted() {
        let raw = format!(
            r#"{{"outer":{{"list":[{{"password":"{s}"}}]}}}}"#,
            s = SENTINEL
        );
        let req = PermissionRequest::new("browser_type", &raw, ToolSensitivity::Sensitive);
        assert!(!req.arguments.contains(SENTINEL));
    }

    #[test]
    fn malformed_non_json_arguments_fail_closed() {
        let raw = format!("not-json password={SENTINEL}");
        let req = PermissionRequest::new("browser_type", &raw, ToolSensitivity::Sensitive);
        assert!(
            !req.arguments.contains(SENTINEL),
            "sentinel leaked through non-JSON fail-open path: {}",
            req.arguments
        );
    }

    #[test]
    fn sanitize_helper_fails_closed_on_invalid_json() {
        let out = sanitize_tool_arguments(&format!("{{broken json {SENTINEL}"));
        assert!(!out.contains(SENTINEL));
        assert!(out.contains("[REDACTED_INVALID_ARGUMENTS]"));
    }

    #[test]
    fn tool_result_redacts_nested_credential_values() {
        let raw = format!(
            r#"{{"items":[{{"label":"pw","password":"{s}"}}],"totp":"{s}"}}"#,
            s = SENTINEL
        );
        let out = sanitize_tool_result(&raw);
        assert!(
            !out.contains(SENTINEL),
            "sentinel leaked through tool-result redaction: {out}"
        );
        assert!(out.contains("[REDACTED]"));
    }

    #[test]
    fn tool_result_fails_closed_on_malformed() {
        let out = sanitize_tool_result(&format!("Tool call failed: password={SENTINEL}"));
        assert!(
            !out.contains(SENTINEL),
            "sentinel leaked through malformed tool-result: {out}"
        );
        assert!(out.contains("[REDACTED_INVALID_RESULT]"));
    }

    #[test]
    fn ordinary_json_result_passes_through() {
        let out = sanitize_tool_result(r#"{"clicked":true,"ref":3}"#);
        assert!(out.contains("clicked"));
        assert!(out.contains("true"));
        assert!(!out.contains("[REDACTED"));
    }
}
