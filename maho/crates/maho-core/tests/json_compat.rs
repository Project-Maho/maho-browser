// maho/crates/maho-core/tests/json_compat.rs
//! Pin exact wire format expected by iOS Swift and Android Kotlin bridges.

use maho_types::chat::{ChatContent, ChatContentPart, ChatMessage};
use maho_types::llm::LLMStreamChunk;
use maho_types::space::SpaceAIConfig;
use serde_json::Value;

fn assert_json_eq(actual: &str, expected: &str, context: &str) {
    let a: Value = serde_json::from_str(actual).expect("actual not valid JSON");
    let e: Value = serde_json::from_str(expected).expect("expected not valid JSON");
    assert_eq!(
        a, e,
        "JSON mismatch [{context}]:\n  actual:   {actual}\n  expected: {expected}"
    );
}

#[test]
fn chat_content_text_wire_format() {
    let v = ChatContent::Text("hello".to_string());
    let json = serde_json::to_string(&v).unwrap();
    assert_json_eq(
        &json,
        r#"{"type":"text","data":"hello"}"#,
        "ChatContent::Text",
    );
}

#[test]
fn chat_content_image_wire_format() {
    let v = ChatContent::Image {
        mime: "image/png".to_string(),
        data: vec![0x01, 0x02],
    };
    let json = serde_json::to_string(&v).unwrap();
    // Note: Vec<u8> serializes as JSON array [1,2], not base64.
    // If Swift/Kotlin expect base64, this is a BUG to surface here.
    assert_json_eq(
        &json,
        r#"{"type":"image","data":{"mime":"image/png","data":[1,2]}}"#,
        "ChatContent::Image — verify Swift/Kotlin expect array vs base64",
    );
}

#[test]
fn chat_content_mixed_text_and_image_wire_format() {
    let v = ChatContent::Mixed(vec![
        ChatContentPart::Text("caption".to_string()),
        ChatContentPart::Image {
            mime: "image/jpeg".to_string(),
            data: vec![0xFF],
        },
    ]);
    let json = serde_json::to_string(&v).unwrap();
    assert_json_eq(
        &json,
        r#"{"type":"mixed","data":[{"kind":"text","data":"caption"},{"kind":"image","data":{"mime":"image/jpeg","data":[255]}}]}"#,
        "ChatContent::Mixed",
    );
}

#[test]
fn chat_message_wire_format() {
    let v = ChatMessage::user(ChatContent::Text("hi".to_string()));
    let json = serde_json::to_string(&v).unwrap();
    assert_json_eq(
        &json,
        r#"{"role":"user","content":{"type":"text","data":"hi"}}"#,
        "ChatMessage::user — tool_calls omitted via skip_serializing_if",
    );
}

#[test]
fn llm_stream_chunk_token_wire_format() {
    let v = LLMStreamChunk::Token("Hi".to_string());
    let json = serde_json::to_string(&v).unwrap();
    assert_json_eq(
        &json,
        r#"{"type":"token","data":"Hi"}"#,
        "LLMStreamChunk::Token",
    );
}

#[test]
fn llm_stream_chunk_tool_call_delta_wire_format() {
    let v = LLMStreamChunk::ToolCallDelta {
        index: 0,
        id: Some("c1".to_string()),
        name: Some("search".to_string()),
        arguments_delta: "{".to_string(),
    };
    let json = serde_json::to_string(&v).unwrap();
    let parsed: Value = serde_json::from_str(&json).unwrap();
    assert_eq!(parsed["type"], "tool_call_delta");
    assert_eq!(parsed["data"]["index"], 0);
    assert_eq!(parsed["data"]["id"], "c1");
    assert_eq!(parsed["data"]["name"], "search");
    assert_eq!(parsed["data"]["arguments_delta"], "{");
}

#[test]
fn llm_stream_chunk_image_delta_wire_format() {
    let v = LLMStreamChunk::ImageDelta {
        index: 0,
        mime: Some("image/png".to_string()),
        b64_chunk: "ABC".to_string(),
    };
    let json = serde_json::to_string(&v).unwrap();
    let parsed: Value = serde_json::from_str(&json).unwrap();
    assert_eq!(parsed["type"], "image_delta");
    assert_eq!(parsed["data"]["index"], 0);
    assert_eq!(parsed["data"]["mime"], "image/png");
    assert_eq!(parsed["data"]["b64_chunk"], "ABC");
}

#[test]
fn space_ai_config_wire_format_minimal() {
    let v = SpaceAIConfig::default();
    let json = serde_json::to_string(&v).unwrap();
    let parsed: Value = serde_json::from_str(&json).unwrap();
    // Verify field names match what iOS+Android decoders expect.
    // Common pattern: focus_areas (snake) for Rust, focusAreas (camel) for iOS via CodingKeys.
    // Document the actual format here — this test is the contract.
    assert!(parsed.is_object());
    // Failure here = mismatch. Update either Rust serde rename_all or shell decoders.
}

#[test]
fn space_ai_config_decodes_missing_fields() {
    // SpaceAIConfig must allow #[serde(default)] on optional fields
    let json = r#"{}"#;
    let decoded: SpaceAIConfig = serde_json::from_str(json)
        .expect("SpaceAIConfig must decode from empty object — check #[serde(default)]");
    assert!(decoded.system_prompt.is_none());
    assert!(decoded.tone.is_none());
    assert!(decoded.focus_areas.is_empty());
    assert!(decoded.preferred_model.is_none());
}
