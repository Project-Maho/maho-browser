use crate::ToolDefinition;

pub fn get_platform_tools() -> Vec<ToolDefinition> {
    #[cfg(any(target_os = "ios", target_os = "android"))]
    {
        vec![
            ToolDefinition {
                name: "camera".to_string(),
                description: "Access mobile camera to take a picture".to_string(),
                input_schema: serde_json::json!({
                    "type": "object",
                    "properties": {}
                }),
            },
            ToolDefinition {
                name: "photo_library".to_string(),
                description: "Select a photo from mobile library".to_string(),
                input_schema: serde_json::json!({
                    "type": "object",
                    "properties": {}
                }),
            },
            ToolDefinition {
                name: "clipboard".to_string(),
                description: "Read or write to the mobile system clipboard".to_string(),
                input_schema: serde_json::json!({
                    "type": "object",
                    "properties": {
                        "action": { "type": "string", "enum": ["read", "write"] },
                        "text": { "type": "string" }
                    },
                    "required": ["action"]
                }),
            },
        ]
    }
    #[cfg(not(any(target_os = "ios", target_os = "android")))]
    {
        vec![
            ToolDefinition {
                name: "fs_read".to_string(),
                description: "Read content of a file on desktop filesystem".to_string(),
                input_schema: serde_json::json!({
                    "type": "object",
                    "properties": {
                        "path": { "type": "string" }
                    },
                    "required": ["path"]
                }),
            },
            ToolDefinition {
                name: "shell_exec".to_string(),
                description: "Execute a command in desktop shell".to_string(),
                input_schema: serde_json::json!({
                    "type": "object",
                    "properties": {
                        "command": { "type": "string" }
                    },
                    "required": ["command"]
                }),
            },
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_platform_tools_divergence() {
        let tools = get_platform_tools();
        
        #[cfg(any(target_os = "ios", target_os = "android"))]
        {
            assert_eq!(tools.len(), 3);
            assert!(tools.iter().any(|t| t.name == "camera"));
            assert!(tools.iter().any(|t| t.name == "photo_library"));
            assert!(tools.iter().any(|t| t.name == "clipboard"));
            assert!(!tools.iter().any(|t| t.name == "fs_read"));
        }

        #[cfg(not(any(target_os = "ios", target_os = "android")))]
        {
            assert_eq!(tools.len(), 2);
            assert!(tools.iter().any(|t| t.name == "fs_read"));
            assert!(tools.iter().any(|t| t.name == "shell_exec"));
            assert!(!tools.iter().any(|t| t.name == "camera"));
        }
    }
}
