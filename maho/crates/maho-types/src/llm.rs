use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ModelInfo {
    pub id: String,
    pub name: String,
    pub context_length: Option<usize>,
    pub supports_vision: bool,
    pub supports_tools: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type", content = "data", rename_all = "snake_case")]
pub enum LLMStreamChunk {
    Token(String),
    ToolCallDelta {
        index: usize,
        id: Option<String>,
        name: Option<String>,
        arguments_delta: String,
    },
    ImageDelta {
        index: usize,
        mime: Option<String>,
        b64_chunk: String,
    },
    Error(String),
}

#[cfg(test)]
mod llm_stream_chunk_tests {
    use super::*;
    use serde_json;

    #[test]
    fn token_chunk_roundtrip() {
        let original = LLMStreamChunk::Token("Hello".to_string());
        let json = serde_json::to_string(&original).unwrap();
        let decoded: LLMStreamChunk = serde_json::from_str(&json).unwrap();
        match decoded {
            LLMStreamChunk::Token(s) => assert_eq!(s, "Hello"),
            _ => panic!("expected Token"),
        }
        assert!(json.contains("\"type\":\"token\""));
    }

    #[test]
    fn tool_call_delta_roundtrip() {
        let original = LLMStreamChunk::ToolCallDelta {
            index: 0,
            id: Some("call_abc".to_string()),
            name: Some("search".to_string()),
            arguments_delta: r#"{"q":"#.to_string(),
        };
        let json = serde_json::to_string(&original).unwrap();
        let decoded: LLMStreamChunk = serde_json::from_str(&json).unwrap();
        match decoded {
            LLMStreamChunk::ToolCallDelta {
                index,
                id,
                name,
                arguments_delta,
            } => {
                assert_eq!(index, 0);
                assert_eq!(id.as_deref(), Some("call_abc"));
                assert_eq!(name.as_deref(), Some("search"));
                assert_eq!(arguments_delta, r#"{"q":"#);
            }
            _ => panic!("expected ToolCallDelta"),
        }
    }

    #[test]
    fn image_delta_roundtrip() {
        let original = LLMStreamChunk::ImageDelta {
            index: 1,
            mime: Some("image/png".to_string()),
            b64_chunk: "iVBORw0K".to_string(),
        };
        let json = serde_json::to_string(&original).unwrap();
        let decoded: LLMStreamChunk = serde_json::from_str(&json).unwrap();
        match decoded {
            LLMStreamChunk::ImageDelta {
                index,
                mime,
                b64_chunk,
            } => {
                assert_eq!(index, 1);
                assert_eq!(mime.as_deref(), Some("image/png"));
                assert_eq!(b64_chunk, "iVBORw0K");
            }
            _ => panic!("expected ImageDelta"),
        }
    }

    #[test]
    fn error_chunk_roundtrip() {
        let original = LLMStreamChunk::Error("rate limited".to_string());
        let json = serde_json::to_string(&original).unwrap();
        let decoded: LLMStreamChunk = serde_json::from_str(&json).unwrap();
        match decoded {
            LLMStreamChunk::Error(s) => assert_eq!(s, "rate limited"),
            _ => panic!("expected Error"),
        }
    }

    #[test]
    fn unknown_variant_rejected() {
        let json = r#"{"type":"unknown","data":"x"}"#;
        let result: Result<LLMStreamChunk, _> = serde_json::from_str(json);
        assert!(result.is_err());
    }
}
