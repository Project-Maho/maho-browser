use serde::{Deserialize, Serialize};

/// Initialize request params sent by the client.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InitializeParams {
    pub protocol_version: u32,
    pub client_capabilities: ClientCapabilities,
    pub client_info: ClientInfo,
}

/// Client capabilities advertised during initialize.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ClientCapabilities {
    pub fs: FsCapabilities,
    #[serde(default)]
    pub terminal: bool,
}

/// Filesystem capabilities.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FsCapabilities {
    pub read_text_file: bool,
    pub write_text_file: bool,
}

/// Client info sent during initialize.
#[derive(Debug, Clone, Serialize)]
pub struct ClientInfo {
    pub name: &'static str,
    pub title: &'static str,
    pub version: &'static str,
}

/// Initialize response from the agent.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InitializeResult {
    pub protocol_version: u32,
    #[serde(default)]
    pub agent_capabilities: serde_json::Value,
    #[serde(default)]
    pub auth_methods: Vec<AuthMethod>,
}

/// An auth method advertised by the agent.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AuthMethod {
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub name: String,
}

impl InitializeParams {
    /// Build the default Maho client initialize params.
    pub fn maho_default() -> Self {
        Self {
            protocol_version: 1,
            client_capabilities: ClientCapabilities {
                fs: FsCapabilities {
                    read_text_file: true,
                    write_text_file: true,
                },
                terminal: false,
            },
            client_info: ClientInfo {
                name: "Maho",
                title: "Maho",
                version: env!("CARGO_PKG_VERSION"),
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn capability_serialization_snapshot() {
        let params = InitializeParams::maho_default();
        let value = serde_json::to_value(&params).unwrap();

        assert_eq!(value["protocolVersion"], 1);
        assert_eq!(value["clientCapabilities"]["fs"]["readTextFile"], true);
        assert_eq!(value["clientCapabilities"]["fs"]["writeTextFile"], true);
        assert_eq!(value["clientCapabilities"]["terminal"], false);
        assert_eq!(value["clientInfo"]["name"], "Maho");
        assert_eq!(value["clientInfo"]["title"], "Maho");
        assert_eq!(value["clientInfo"]["version"], env!("CARGO_PKG_VERSION"));

        // Ensure no extra fields leak in
        let caps = value["clientCapabilities"].as_object().unwrap();
        assert_eq!(caps.len(), 2, "clientCapabilities should have exactly fs and terminal");
    }
}
