# Phase 1 Execution Plan — Static & Unit Hardening

**Created:** 2026-06-18
**Depends on:** `.omo/plans/maho-mobile-verification-plan.md` (master verification plan)
**Status:** Ready for execution
**Scope:** Detailed, atomic, executable breakdown of V1-1 through V1-12
**Goal:** Eliminate Risk #1 (FFI stubs) + Risk #2 (JSON snake_case/camelCase) before any device testing

---

## 0. Tooling Prerequisites

Verify before starting Phase 1:

```bash
# Rust
rustc --version              # expect 1.78+
cargo --version
cargo clippy --version

# Java (for Android)
java -version                # expect 17+

# Xcode (for iOS)
xcodebuild -version          # expect 16+
xcrun simctl list devices    # iPhone 17 simulator should be available

# Android tools (optional for V1-10, can defer)
./gradlew --version          # in maho/android-shell/
```

**Pass condition:** All commands return without error. If Android tools missing, V1-10 is deferred but does not block other tasks.

---

## 1. V1-1 — cargo clippy maho workspace

| Field | Value |
|-------|-------|
| **WHERE** | `maho/` workspace root |
| **WHY** | Catches new code issues from W2-W8 implementation. Mitigates: unused vars, unwrap() chains, type confusion. Gates: all subsequent V1 tasks. |
| **HOW** | `cd maho && cargo clippy --workspace --all-targets -- -D warnings` |
| **VERIFY** | Exit code 0. No warnings reported. |
| **EFFORT** | 5 min initial run. Up to 60 min if errors require fixing. |

**Failure handling:** If lints found, group by category (unused, unwrap, type) and fix in batches. Do NOT use `#[allow]` to silence; fix the actual issue. If a lint is genuinely impossible to fix without scope creep, document with explicit `#[allow(reason = "...")]` and continue.

---

## 2. V1-2 — cargo clippy relay workspace

| Field | Value |
|-------|-------|
| **WHERE** | `maho/relay/` workspace |
| **WHY** | Independent workspace from maho/. Has separate dep tree. Catches issues in W8 agent infrastructure (executor.rs, protocol.rs, sandbox.rs). |
| **HOW** | `cd maho/relay && cargo clippy --workspace --all-targets -- -D warnings` |
| **VERIFY** | Exit code 0. |
| **EFFORT** | 5 min initial. Up to 60 min if errors. |

**Failure handling:** Same as V1-1. Note: relay has chromiumoxide which is large and pre-existing warnings may be from upstream. If a warning is in vendored code path, allow it.

---

## 3. V1-3 — ChatContent / ChatMessage / ConversationMeta serde tests

| Field | Value |
|-------|-------|
| **WHERE** | `maho/crates/maho-types/src/chat.rs` (existing test module or new at end of file) |
| **WHY** | Mitigates Risk #2 (JSON mismatch). These types cross FFI boundary as JSON. Gates: V1-5, V1-6 JSON compat tests. |
| **HOW** | Add `#[cfg(test)] mod chat_serde_tests { ... }` with round-trip assertions for each variant + edge cases (empty Mixed, large Image bytes). |
| **VERIFY** | `cd maho && cargo test -p maho-types chat_serde_tests` — all assertions pass. |
| **EFFORT** | 30 min |

### Code skeleton (paste-ready)

```rust
#[cfg(test)]
mod chat_serde_tests {
    use super::*;
    use serde_json;

    #[test]
    fn chat_content_text_roundtrip() {
        let original = ChatContent::Text("hello world".to_string());
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ChatContent = serde_json::from_str(&json).unwrap();
        assert_eq!(original, decoded);
        // Verify wire format (snake_case tag/data)
        assert!(json.contains("\"type\":\"text\""));
        assert!(json.contains("\"data\":\"hello world\""));
    }

    #[test]
    fn chat_content_image_roundtrip() {
        let original = ChatContent::Image {
            mime: "image/png".to_string(),
            data: vec![0x89, 0x50, 0x4E, 0x47],
        };
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ChatContent = serde_json::from_str(&json).unwrap();
        assert_eq!(original, decoded);
        assert!(json.contains("\"type\":\"image\""));
    }

    #[test]
    fn chat_content_mixed_roundtrip_empty() {
        let original = ChatContent::Mixed(vec![]);
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ChatContent = serde_json::from_str(&json).unwrap();
        assert_eq!(original, decoded);
    }

    #[test]
    fn chat_content_mixed_roundtrip_text_and_image() {
        let original = ChatContent::Mixed(vec![
            ChatContentPart::Text("describe this:".to_string()),
            ChatContentPart::Image {
                mime: "image/jpeg".to_string(),
                data: vec![0xFF, 0xD8, 0xFF],
            },
        ]);
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ChatContent = serde_json::from_str(&json).unwrap();
        assert_eq!(original, decoded);
    }

    #[test]
    fn chat_message_user_roundtrip() {
        let original = ChatMessage::user(ChatContent::Text("hi".to_string()));
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ChatMessage = serde_json::from_str(&json).unwrap();
        assert_eq!(original.role, decoded.role);
        assert_eq!(original.role, "user");
    }

    #[test]
    fn chat_message_assistant_with_tool_calls() {
        let mut original = ChatMessage::assistant(ChatContent::Text("ok".to_string()));
        original.tool_calls = Some(vec![serde_json::json!({"name": "search", "args": {}})]);
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ChatMessage = serde_json::from_str(&json).unwrap();
        assert_eq!(original.role, decoded.role);
        assert!(decoded.tool_calls.is_some());
    }

    #[test]
    fn chat_message_no_tool_calls_omitted_in_json() {
        let original = ChatMessage::user(ChatContent::Text("hi".to_string()));
        let json = serde_json::to_string(&original).unwrap();
        // tool_calls: None should be skipped in serialization
        assert!(!json.contains("tool_calls"));
    }

    #[test]
    fn conversation_meta_roundtrip_full() {
        let original = ConversationMeta {
            id: "session-123".to_string(),
            title: Some("Test chat".to_string()),
            space_id: Some("space-1".to_string()),
            model: Some("gpt-4o".to_string()),
            created_at: "2026-06-18T10:00:00Z".to_string(),
            last_message_at: "2026-06-18T10:05:00Z".to_string(),
            message_count: 3,
        };
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ConversationMeta = serde_json::from_str(&json).unwrap();
        assert_eq!(original.id, decoded.id);
        assert_eq!(original.message_count, 3);
    }

    #[test]
    fn conversation_meta_roundtrip_minimal() {
        // Test #[serde(default)] on optional fields by omitting them from JSON
        let json = r#"{"id":"x","created_at":"2026-06-18T10:00:00Z","last_message_at":"2026-06-18T10:00:00Z"}"#;
        let decoded: ConversationMeta = serde_json::from_str(json).unwrap();
        assert_eq!(decoded.id, "x");
        assert!(decoded.title.is_none());
        assert!(decoded.space_id.is_none());
        assert!(decoded.model.is_none());
        assert_eq!(decoded.message_count, 0);
    }
}
```

---

## 4. V1-4 — LLMStreamChunk variant tests

| Field | Value |
|-------|-------|
| **WHERE** | `maho/crates/maho-types/src/llm.rs` |
| **WHY** | LLMStreamChunk crosses FFI boundary during streaming. ImageDelta (W3-1b) is new. Mismatch causes silent truncation. |
| **HOW** | Add inline test module with 4 round-trip tests. |
| **VERIFY** | `cargo test -p maho-types llm` — all pass. |
| **EFFORT** | 20 min |

### Code skeleton

```rust
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
            LLMStreamChunk::ToolCallDelta { index, id, name, arguments_delta } => {
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
            LLMStreamChunk::ImageDelta { index, mime, b64_chunk } => {
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
```

---

## 5. V1-5 + V1-6 — JSON format compatibility tests (Rust ↔ Swift ↔ Kotlin)

| Field | Value |
|-------|-------|
| **WHERE** | `maho/crates/maho-core/tests/json_compat.rs` (new file) |
| **WHY** | **Risk #2 (Score 16)**: snake_case/camelCase mismatch silently breaks bridges. iOS uses Swift `Decodable`, Android uses kotlinx-serialization with `@SerialName`. |
| **HOW** | Pin exact JSON wire format expected by each platform; assert Rust serialization matches. |
| **VERIFY** | `cd maho && cargo test --test json_compat` — all assertions pass. |
| **EFFORT** | 60 min |

### JSON format contract (audit results)

| Type | Rust serde output | Swift Decodable expects | Kotlin @Serializable expects | Match? |
|------|-------------------|-------------------------|------------------------------|--------|
| `ChatContent::Text` | `{"type":"text","data":"..."}` | `{"type":"text","data":"..."}` | `{"type":"text","data":"..."}` | ✅ |
| `ChatContent::Image` | `{"type":"image","data":{"mime":"...","data":[...]}}` | TBD | TBD | ⚠️ verify |
| `ChatContent::Mixed` | `{"type":"mixed","data":[{"kind":"text","data":"..."}]}` | TBD | TBD | ⚠️ verify |
| `ChatMessage` | `{"role":"user","content":{"type":"text","data":"..."}}` | matches | matches | ✅ |
| `ConversationMeta` | `{"id":"...","title":"...","space_id":"...","created_at":"...","last_message_at":"...","message_count":3}` | iOS uses `space_id` (snake) per `ConversationSession` | Kotlin uses `@SerialName("space_id")` | ✅ |
| `ConversationSession` (storage) | `{"id":"...","title":null,"space_id":null,"model":null,"message_count":0,"created_at":"...","last_message_at":"..."}` | Swift `ConversationSession` matches | Kotlin matches | ✅ |
| `ConversationTurn` (storage) | `{"id":"...","role":"user","content":"...","url_context":null,"created_at":"..."}` | matches | matches | ✅ |
| `TabCluster` | `{"group_name":"...","tab_ids":["..."]}` | Swift expects `group_name` snake | Kotlin uses `@SerialName("group_name")` | ✅ |
| `SpaceAIConfig` | needs verification — see V1-7 | varies | varies | ⚠️ |
| `BlockResult` | `{"blocked":true,"redirect":null,"rewrittenUrl":null}` | iOS expects `rewrittenUrl` camelCase | Kotlin uses `rewrittenUrl` no @SerialName | ⚠️ mixed |
| `Suggestion` | `{"id":"...","title":"...","description":null,"action":{...},"confidence":0.85,"kind":"new_tab"}` | TBD | TBD | ⚠️ verify |
| `SuggestionAction::OpenUrl` | `{"type":"open_url","data":"https://..."}` | TBD | TBD | ⚠️ verify |

**Identified concerns:**
- `ChatContent` uses `#[serde(tag="type", content="data")]` so Image/Mixed embed nested objects under "data". Swift/Kotlin must mirror this exact structure.
- `BlockResult.rewrittenUrl` is camelCase in Rust struct but the test fixtures need to confirm.
- `SpaceAIConfig` field names mix snake (`focus_areas`) and need verification against Swift/Kotlin model.

### Code skeleton

```rust
// maho/crates/maho-core/tests/json_compat.rs
//! Pin exact wire format expected by iOS Swift and Android Kotlin bridges.

use maho_types::chat::{ChatContent, ChatContentPart, ChatMessage};
use maho_types::llm::LLMStreamChunk;
use maho_types::space::SpaceAIConfig;
use serde_json::Value;

fn assert_json_eq(actual: &str, expected: &str, context: &str) {
    let a: Value = serde_json::from_str(actual).expect("actual not valid JSON");
    let e: Value = serde_json::from_str(expected).expect("expected not valid JSON");
    assert_eq!(a, e, "JSON mismatch [{context}]:\n  actual:   {actual}\n  expected: {expected}");
}

#[test]
fn chat_content_text_wire_format() {
    let v = ChatContent::Text("hello".to_string());
    let json = serde_json::to_string(&v).unwrap();
    assert_json_eq(&json, r#"{"type":"text","data":"hello"}"#, "ChatContent::Text");
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
    assert_json_eq(&json, r#"{"type":"token","data":"Hi"}"#, "LLMStreamChunk::Token");
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
    assert_eq!(parsed["index"], 0);
    assert_eq!(parsed["id"], "c1");
    assert_eq!(parsed["name"], "search");
    assert_eq!(parsed["arguments_delta"], "{");
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
    assert_eq!(parsed["mime"], "image/png");
    assert_eq!(parsed["b64_chunk"], "ABC");
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
    let decoded: SpaceAIConfig = serde_json::from_str(json).expect(
        "SpaceAIConfig must decode from empty object — check #[serde(default)]"
    );
    assert!(decoded.system_prompt.is_none());
    assert!(decoded.tone.is_none());
    assert!(decoded.focus_areas.is_empty());
    assert!(decoded.preferred_model.is_none());
}
```

---

## 6. V1-7 — SpaceAIConfig serde defaults

| Field | Value |
|-------|-------|
| **WHERE** | `maho/crates/maho-types/src/space.rs` (test module) |
| **WHY** | Old persisted Space data lacks `ai_config` field; deserialization must succeed. Missing `#[serde(default)]` on Space struct field causes data loss on app upgrade. |
| **HOW** | Test that Space deserializes from JSON without ai_config field. |
| **VERIFY** | `cargo test -p maho-types space_ai_config_serde` passes. |
| **EFFORT** | 15 min |

### Code skeleton

```rust
#[cfg(test)]
mod space_ai_config_serde_tests {
    use super::*;

    #[test]
    fn space_decodes_without_ai_config() {
        // Backward-compat: pre-W4 persisted Space data has no ai_config field
        let json = r#"{
            "id":"space-1",
            "name":"Default",
            "color":{"variant":"solid","value":"#000000"},
            "tabs":[],
            "icon":null
        }"#;
        // Adjust to actual minimal Space JSON shape
        let result: Result<Space, _> = serde_json::from_str(json);
        assert!(result.is_ok(), "Space must decode without ai_config: {:?}", result.err());
    }

    #[test]
    fn space_ai_config_default_is_none() {
        let space_json_with_ai = r#"{...full Space with ai_config: null}"#;
        // Verify None doesn't crash decode
    }

    #[test]
    fn space_ai_config_partial_decode() {
        let json = r#"{"system_prompt":"You are concise"}"#;
        let cfg: SpaceAIConfig = serde_json::from_str(json).unwrap();
        assert_eq!(cfg.system_prompt.as_deref(), Some("You are concise"));
        assert!(cfg.tone.is_none());
        assert!(cfg.focus_areas.is_empty());
    }
}
```

---

## 7. V1-8 — FFI null-safety smoke tests

| Field | Value |
|-------|-------|
| **WHERE** | `maho/crates/maho-ffi/tests/null_safety.rs` (new file) |
| **WHY** | **Risk #1 (Score 20)**: 14 stub functions added during recovery have NO null pointer guards. Calling with NULL from Swift = segfault. |
| **HOW** | Test each of the 14 functions with NULL pointers + valid-but-corrupt strings. Verify no segfault, returns sensible default. |
| **VERIFY** | `cd maho && cargo test --test null_safety` passes (no segfault). |
| **EFFORT** | 60 min |

### 14 stub function catalog (line numbers from maho-ffi/src/lib.rs)

| # | Function | Line | Returns | Stub Behavior |
|---|----------|------|---------|---------------|
| 1 | `maho_core_create_conversation` | 5133 | `bool` | always true, ignores params |
| 2 | `maho_core_list_conversations` | 5145 | `*mut c_char` | empty `[]` JSON, ignores ptr |
| 3 | `maho_core_get_conversation_messages` | 5155 | `*mut c_char` | empty `[]`, ignores ptr+session_id |
| 4 | `maho_core_save_conversation_message` | 5165 | `bool` | always true |
| 5 | `maho_core_delete_conversation` | 5177 | `bool` | always true |
| 6 | `maho_core_rename_conversation` | 5186 | `bool` | always true |
| 7 | `maho_core_byok_set_key` | 5198 | `bool` | always true |
| 8 | `maho_core_byok_get_key` | 5208 | `*mut c_char` | always null |
| 9 | `maho_core_byok_delete_key` | 5217 | `bool` | always true |
| 10 | `maho_core_byok_get_providers` | 5226 | `*mut c_char` | empty `[]` |
| 11 | `maho_core_byok_validate_key` | 5235 | `bool` | always false |
| 12 | `maho_chat_send_image` | 5265 | `bool` | always false |
| 13 | `maho_chat_send_text_with_image` | 5276 | `bool` | always false |
| 14 | `maho_core_cluster_tabs` | 5290 | `*mut c_char` | empty `[]` |

**Critical asymmetry finding:** JNI side has REAL implementations for conversation + BYOK functions; FFI (iOS) side is all stubs. iOS gets silent no-op behavior while Android works. This is documented as a known issue, not blocked by Phase 1.

### Code skeleton

```rust
// maho/crates/maho-ffi/tests/null_safety.rs
//! Smoke tests proving 14 stub FFI functions don't segfault on NULL input.
//! Runs as `cargo test --test null_safety`.

use std::ffi::CString;
use std::os::raw::c_char;
use std::ptr;

// Re-import the FFI symbols. Adjust path to match actual maho-ffi exports.
extern "C" {
    fn maho_core_create_conversation(ptr: *mut std::ffi::c_void, id: *const c_char, title: *const c_char, space_id: *const c_char, model: *const c_char) -> bool;
    fn maho_core_list_conversations(ptr: *mut std::ffi::c_void, limit: usize) -> *mut c_char;
    fn maho_core_get_conversation_messages(ptr: *mut std::ffi::c_void, session_id: *const c_char) -> *mut c_char;
    fn maho_core_save_conversation_message(ptr: *mut std::ffi::c_void, session_id: *const c_char, role: *const c_char, content: *const c_char, url_context: *const c_char) -> bool;
    fn maho_core_delete_conversation(ptr: *mut std::ffi::c_void, session_id: *const c_char) -> bool;
    fn maho_core_rename_conversation(ptr: *mut std::ffi::c_void, session_id: *const c_char, new_title: *const c_char) -> bool;
    fn maho_core_byok_set_key(ptr: *mut std::ffi::c_void, provider: *const c_char, key: *const c_char) -> bool;
    fn maho_core_byok_get_key(ptr: *mut std::ffi::c_void, provider: *const c_char) -> *mut c_char;
    fn maho_core_byok_delete_key(ptr: *mut std::ffi::c_void, provider: *const c_char) -> bool;
    fn maho_core_byok_get_providers(ptr: *mut std::ffi::c_void) -> *mut c_char;
    fn maho_core_byok_validate_key(ptr: *mut std::ffi::c_void, provider: *const c_char, key: *const c_char) -> bool;
    fn maho_chat_send_image(session: *mut std::ffi::c_void, mime: *const c_char, data: *const u8, data_len: usize) -> bool;
    fn maho_chat_send_text_with_image(session: *mut std::ffi::c_void, text: *const c_char, mime: *const c_char, data: *const u8, data_len: usize) -> bool;
    fn maho_core_cluster_tabs(ptr: *mut std::ffi::c_void, threshold: f32) -> *mut c_char;

    fn maho_string_free(s: *mut c_char);
}

#[test]
fn create_conversation_with_null_ptr_does_not_crash() {
    unsafe {
        let _result = maho_core_create_conversation(ptr::null_mut(), ptr::null(), ptr::null(), ptr::null(), ptr::null());
        // Stub returns true; just verify no segfault.
    }
}

#[test]
fn list_conversations_with_null_ptr_returns_some_string() {
    unsafe {
        let result = maho_core_list_conversations(ptr::null_mut(), 100);
        // Stub returns "[]" cstring. Free it.
        if !result.is_null() {
            maho_string_free(result);
        }
        // No segfault = pass.
    }
}

#[test]
fn get_conversation_messages_with_null_session_id() {
    unsafe {
        let result = maho_core_get_conversation_messages(ptr::null_mut(), ptr::null());
        if !result.is_null() {
            maho_string_free(result);
        }
    }
}

#[test]
fn save_conversation_message_with_all_null() {
    unsafe {
        let _ = maho_core_save_conversation_message(ptr::null_mut(), ptr::null(), ptr::null(), ptr::null(), ptr::null());
    }
}

#[test]
fn delete_conversation_with_null_session_id() {
    unsafe { let _ = maho_core_delete_conversation(ptr::null_mut(), ptr::null()); }
}

#[test]
fn rename_conversation_with_null_strings() {
    unsafe { let _ = maho_core_rename_conversation(ptr::null_mut(), ptr::null(), ptr::null()); }
}

#[test]
fn byok_set_key_with_null_strings() {
    unsafe { let _ = maho_core_byok_set_key(ptr::null_mut(), ptr::null(), ptr::null()); }
}

#[test]
fn byok_get_key_with_null_provider() {
    unsafe {
        let result = maho_core_byok_get_key(ptr::null_mut(), ptr::null());
        if !result.is_null() {
            maho_string_free(result);
        }
    }
}

#[test]
fn byok_delete_key_with_null() {
    unsafe { let _ = maho_core_byok_delete_key(ptr::null_mut(), ptr::null()); }
}

#[test]
fn byok_get_providers_with_null_ptr() {
    unsafe {
        let result = maho_core_byok_get_providers(ptr::null_mut());
        if !result.is_null() {
            maho_string_free(result);
        }
    }
}

#[test]
fn byok_validate_key_with_null() {
    unsafe { let _ = maho_core_byok_validate_key(ptr::null_mut(), ptr::null(), ptr::null()); }
}

#[test]
fn chat_send_image_with_null_session() {
    unsafe {
        let mime = CString::new("image/png").unwrap();
        let _ = maho_chat_send_image(ptr::null_mut(), mime.as_ptr(), ptr::null(), 0);
    }
}

#[test]
fn chat_send_image_with_null_mime() {
    unsafe {
        let data = vec![0u8; 4];
        let _ = maho_chat_send_image(ptr::null_mut(), ptr::null(), data.as_ptr(), data.len());
    }
}

#[test]
fn chat_send_image_with_null_data_but_nonzero_len() {
    unsafe {
        let mime = CString::new("image/png").unwrap();
        // Caller-side bug to send NULL data with nonzero len. Must not crash.
        let _ = maho_chat_send_image(ptr::null_mut(), mime.as_ptr(), ptr::null(), 100);
    }
}

#[test]
fn chat_send_text_with_image_full_null() {
    unsafe {
        let _ = maho_chat_send_text_with_image(ptr::null_mut(), ptr::null(), ptr::null(), ptr::null(), 0);
    }
}

#[test]
fn cluster_tabs_with_null_ptr() {
    unsafe {
        let result = maho_core_cluster_tabs(ptr::null_mut(), 0.75);
        if !result.is_null() {
            maho_string_free(result);
        }
    }
}

#[test]
fn cluster_tabs_with_negative_threshold() {
    unsafe {
        let result = maho_core_cluster_tabs(ptr::null_mut(), -1.0);
        if !result.is_null() {
            maho_string_free(result);
        }
    }
}

#[test]
fn cluster_tabs_with_nan_threshold() {
    unsafe {
        let result = maho_core_cluster_tabs(ptr::null_mut(), f32::NAN);
        if !result.is_null() {
            maho_string_free(result);
        }
    }
}
```

**Failure handling:** If any test segfaults, the corresponding stub function MUST be patched to add `if ptr.is_null() { return ...default... }` guard before reading. This is mandatory for Phase 1 exit.

---

## 8. V1-9 — maho_string_free leak audit

| Field | Value |
|-------|-------|
| **WHERE** | Audit `maho/ios-shell/Bridge/*.swift` + `maho-chromium/browser/**/*.{cc,mm}` |
| **WHY** | All FFI returns `*mut c_char` must be freed. Leak detection by code review. |
| **HOW** | grep for FFI calls returning strings. For each, verify caller frees. |
| **VERIFY** | Audit checklist filled in plan-output. No unfreed return paths. |
| **EFFORT** | 30 min |

```bash
# Step 1: list FFI functions returning *mut c_char
grep -E "pub unsafe extern \"C\" fn.*-> \*mut c_char" maho/crates/maho-ffi/src/lib.rs > /tmp/ffi_string_returns.txt

# Step 2: for each, find Swift call sites
for fn in $(awk '{print $5}' /tmp/ffi_string_returns.txt | tr -d '('); do
  echo "=== $fn ==="
  grep -rn "$fn" maho/ios-shell/ | grep -v "//"
done

# Step 3: verify each call site uses FFIString.consume / consumeJSON which calls maho_string_free
```

**Audit output format** — fill in this table during audit:

| FFI function | Swift caller | Uses FFIString.consume? | Verdict |
|--------------|--------------|-------------------------|---------|
| (fill during audit) | | | |

---

## 9. V1-10 — Android lint

| Field | Value |
|-------|-------|
| **WHERE** | `maho/android-shell/` |
| **WHY** | Lint catches Android-specific issues (unused resources, hardcoded strings, missing API checks). |
| **HOW** | `cd maho/android-shell && ./gradlew app:lintDebug` |
| **VERIFY** | Exit code 0. No new errors compared to baseline. Warnings tolerated. |
| **EFFORT** | 10 min |

**Failure handling:** New lint errors in W2-W8 code (BYOKSettingsScreen, ChatScreen, AgentScreen, etc.) must be fixed. Pre-existing warnings in unrelated files are tolerated and noted.

---

## 10. V1-11 — iOS warnings-as-errors

| Field | Value |
|-------|-------|
| **WHERE** | `maho/ios-shell/` |
| **WHY** | Catches Swift code quality issues (unused vars, deprecated APIs, force unwraps in new code). |
| **HOW** | `cd maho/ios-shell && xcodebuild build -project Maho.xcodeproj -scheme Maho -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' ARCHS=arm64 ONLY_ACTIVE_ARCH=YES CODE_SIGNING_ALLOWED=NO SWIFT_TREAT_WARNINGS_AS_ERRORS=YES` |
| **VERIFY** | BUILD SUCCEEDED with warnings-as-errors flag. |
| **EFFORT** | 15 min |

**Failure handling:** New warnings in W1-W8 Swift files (BYOKSettingsView, ChatView, ConversationListView, etc.) must be fixed. Pre-existing warnings in unrelated files: revert flag temporarily for that file or document acceptable.

---

## 11. V1-12 — SQLite migration sequence test

| Field | Value |
|-------|-------|
| **WHERE** | `maho/crates/maho-storage/src/sqlite.rs` (test module) |
| **WHY** | Verify all W2-W8 tables created when DB opens fresh + after `ensure_memory_tables()`. |
| **HOW** | Open in-memory DB, call `ensure_memory_tables()`, query sqlite_master for table names. |
| **VERIFY** | All expected tables + indexes present. |
| **EFFORT** | 20 min |

### Code skeleton (CORRECTED — no `conversation_sessions`, uses `ensure_memory_tables`)

```rust
#[cfg(test)]
mod migration_tests {
    use super::*;

    /// Helper: query all table names from sqlite_master.
    fn query_table_names(storage: &SqliteStorage) -> Vec<String> {
        // Uses storage's internal connection. Adjust based on actual API.
        storage.query_raw_table_names_for_test()
    }

    fn query_index_names(storage: &SqliteStorage) -> Vec<String> {
        storage.query_raw_index_names_for_test()
    }

    #[test]
    fn fresh_db_creates_always_present_tables() {
        // SqliteStorage::open_in_memory() runs initialize_schema()
        let storage = SqliteStorage::open_in_memory().expect("open in-memory DB");

        let tables = query_table_names(&storage);

        // W6-2/W6-3 always-created tables
        assert!(tables.contains(&"ai_usage_stats".to_string()),
            "ai_usage_stats missing. Found: {tables:?}");
        assert!(tables.contains(&"ai_response_cache".to_string()),
            "ai_response_cache missing. Found: {tables:?}");

        // Other expected tables from W0-W5 (api_keys, etc)
        assert!(tables.contains(&"api_keys".to_string()),
            "api_keys missing. Found: {tables:?}");
    }

    #[test]
    fn ensure_memory_tables_creates_lazy_tables() {
        let storage = SqliteStorage::open_in_memory().unwrap();

        // Trigger lazy creation
        storage.ensure_memory_tables().expect("ensure_memory_tables");

        let tables = query_table_names(&storage);

        // Memory feature tables
        assert!(tables.contains(&"memories".to_string()), "memories missing");
        assert!(tables.contains(&"memory_blocks".to_string()), "memory_blocks missing");

        // FTS5 virtual table
        assert!(tables.contains(&"memories_fts".to_string()), "memories_fts missing");

        // Conversation feature
        assert!(tables.contains(&"conversation_turns".to_string()),
            "conversation_turns missing");
    }

    #[test]
    fn fresh_db_creates_required_indexes() {
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage.ensure_memory_tables().unwrap();

        let indexes = query_index_names(&storage);

        // From schema migration files
        assert!(indexes.contains(&"idx_ai_usage_provider".to_string())
            || indexes.iter().any(|i| i.contains("ai_usage")),
            "ai_usage index missing. Found: {indexes:?}");
        assert!(indexes.iter().any(|i| i.contains("conversation_turns")),
            "conversation_turns index missing. Found: {indexes:?}");
    }

    #[test]
    fn migration_idempotent() {
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage.ensure_memory_tables().unwrap();
        // Second call must succeed (CREATE TABLE IF NOT EXISTS)
        storage.ensure_memory_tables().unwrap();
        let tables = query_table_names(&storage);
        // Count of memories should be 1, not duplicated
        assert_eq!(tables.iter().filter(|t| t.as_str() == "memories").count(), 1);
    }

    #[test]
    fn space_id_columns_exist_on_memory_tables() {
        // W4-1 added space_id to memory_blocks/memories/browse_sessions
        let storage = SqliteStorage::open_in_memory().unwrap();
        storage.ensure_memory_tables().unwrap();

        // Use PRAGMA table_info to verify column existence
        let memory_blocks_cols = storage.query_columns_for_test("memory_blocks");
        assert!(memory_blocks_cols.iter().any(|c| c == "space_id"),
            "memory_blocks.space_id missing");
    }
}
```

**Note:** The helper methods `query_raw_table_names_for_test`, `query_raw_index_names_for_test`, `query_columns_for_test` may need to be added behind `#[cfg(test)]` to expose internal connection state. If `SqliteStorage` already exposes the underlying `rusqlite::Connection`, use that instead.

---

## 12. Parallel Execution Groups

| Group | Tasks | Why parallel |
|-------|-------|--------------|
| **A** | V1-1, V1-2 | Independent workspaces (maho/ vs maho/relay/) |
| **B** | V1-3, V1-4, V1-7 | Same crate (maho-types) but independent test modules |
| **C** | V1-5, V1-6 | Same file (json_compat.rs) — sequential within, but parallel with Group A,B |
| **D** | V1-8 | Independent crate (maho-ffi tests/) |
| **E** | V1-12 | Independent crate (maho-storage) |
| **F** | V1-10, V1-11 | Different platforms (Android vs iOS) |
| **G** | V1-9 | Manual audit, runs anytime |

Optimal parallelization: 3 worker streams.

| Stream | Sequence |
|--------|----------|
| Stream 1 | V1-1 → V1-3 → V1-4 → V1-7 (maho-types focus) |
| Stream 2 | V1-2 → V1-5 → V1-6 → V1-8 (cross-crate tests) |
| Stream 3 | V1-12 → V1-10 → V1-11 → V1-9 (storage + shells + audit) |

---

## 13. Branch + Commit Strategy

**Branch:** `verify/phase-1-static`
**Base:** `main`

**Commit convention:**
```
verify/p1/{area}: {verb} {subject}

Body: 1-2 sentences explaining what this verifies and why it matters.
Mitigates: Risk #N (if applicable)
Gate: {pass/fail criterion}
```

**Per-task commits:**
```
verify/p1/clippy: enforce zero warnings on maho workspace                # V1-1
verify/p1/clippy: enforce zero warnings on relay workspace               # V1-2
verify/p1/types: add ChatContent + ChatMessage + ConversationMeta serde tests   # V1-3
verify/p1/types: add LLMStreamChunk variant serde tests                  # V1-4
verify/p1/compat: add Rust↔Swift↔Kotlin JSON format compatibility tests  # V1-5+V1-6
verify/p1/types: add SpaceAIConfig serde defaults verification           # V1-7
verify/p1/ffi: add null-safety smoke tests for 14 stub functions         # V1-8
verify/p1/audit: document maho_string_free coverage on iOS bridges       # V1-9
verify/p1/android: enforce zero new lint errors                          # V1-10
verify/p1/ios: enforce zero new Swift warnings (SWIFT_TREAT_WARNINGS_AS_ERRORS) # V1-11
verify/p1/storage: add migration sequence + memory tables presence test  # V1-12
```

**Squash-merge policy:** Each PR squash-merges into `main` with summary commit:
```
verify: Phase 1 (Static & Unit Hardening) complete

All 12 V1 tasks pass. Gates:
- cargo clippy clean on both workspaces
- 14 FFI stub functions safe under NULL input
- JSON wire format pinned for Rust↔Swift↔Kotlin
- All AI-related SQLite tables verified
- Test count: 701 → ~735+

Ready for Phase 2 (Integration Tests).
```

**PR description template:**
```markdown
## Phase 1 — Static & Unit Hardening

**Master plan:** `.omo/plans/maho-mobile-verification-plan.md`
**Detailed plan:** `.omo/plans/maho-mobile-verification-phase-1-execution.md`

### Tasks completed
- [x] V1-1 cargo clippy maho
- [x] V1-2 cargo clippy relay
- [x] V1-3 ChatContent/ChatMessage/ConversationMeta serde
- [x] V1-4 LLMStreamChunk variants
- [x] V1-5/6 JSON format compat (Rust↔Swift↔Kotlin)
- [x] V1-7 SpaceAIConfig defaults
- [x] V1-8 14 FFI stubs null-safety
- [x] V1-9 maho_string_free audit
- [x] V1-10 Android lint
- [x] V1-11 iOS warnings-as-errors
- [x] V1-12 SQLite migration sequence

### Gates met
- ✅ cargo clippy: 0 warnings (both workspaces)
- ✅ cargo test: NEW count = 701+N
- ✅ All 14 FFI stubs guarded
- ✅ JSON wire format pinned

### Risks mitigated
- Risk #1 (FFI stubs): null-safety proven
- Risk #2 (JSON mismatch): wire format contract tests

### Next phase
Phase 2 — Integration Tests (FFI/JNI data flow)
```

---

## 14. Failure Handling Matrix

| Task | Blocker for Phase 1? | Escalation |
|------|----------------------|------------|
| V1-1 | YES | Fix all clippy warnings before proceeding |
| V1-2 | YES | Fix all clippy warnings before proceeding |
| V1-3 | YES | Fix serde derives or types if tests fail |
| V1-4 | YES | Same |
| V1-5/6 | YES | If JSON mismatch found: update Rust serde rename_all OR shell decoders. Document choice in commit. |
| V1-7 | YES | Add `#[serde(default)]` to missing fields |
| V1-8 | YES | Add NULL guards to all 14 stubs. Mandatory. |
| V1-9 | NO (audit only) | Document leaks; defer fix to Phase 2 if non-trivial |
| V1-10 | NO | Pre-existing lint warnings tolerated; new errors must fix |
| V1-11 | NO | Same |
| V1-12 | YES | Fix migrations if tables missing or columns wrong |

---

## 15. Estimated Wall-Clock

| Scenario | Time |
|----------|------|
| Sequential (1 dev) | 4-6 hours best case, 1-2 days if fixes needed |
| 3 parallel streams | 2-3 hours best case, 4-8 hours with fixes |
| Worst case (all V1-1 + V1-2 lints to fix + V1-8 stubs need NULL guards) | 1-2 days |

---

## 16. Phase 1 Exit Criteria

Definitive checklist. ALL must be true to proceed to Phase 2:

- [ ] `cd maho && cargo clippy --workspace --all-targets -- -D warnings` exits 0
- [ ] `cd maho/relay && cargo clippy --workspace --all-targets -- -D warnings` exits 0
- [ ] `cd maho && cargo test --workspace --no-fail-fast` passes ALL (target: 701 + ~30 new = 731+)
- [ ] `cd maho/relay && cargo test --workspace --no-fail-fast` passes ALL (target: 171, no new tests planned)
- [ ] `cargo test --test json_compat -p maho-core` passes
- [ ] `cargo test --test null_safety -p maho-ffi` passes (no segfault, no panic)
- [ ] All 14 FFI stub functions verified to handle NULL input gracefully
- [ ] JSON wire format documented and matches Swift/Kotlin decoders
- [ ] V1-9 audit table filled and documented
- [ ] (optional) Android lint passes with no new errors
- [ ] (optional) iOS xcodebuild with SWIFT_TREAT_WARNINGS_AS_ERRORS=YES succeeds

---

## 17. Risk Register Specific to Phase 1 Execution

| # | Phase 1 Risk | Likelihood | Impact | Mitigation |
|---|--------------|------------|--------|------------|
| P1-R1 | clippy uncovers many issues in W2-W8 code | High | Medium | Allocate up to 1 day for lint fixes |
| P1-R2 | JSON format mismatch found between Rust and shells | Medium | High | Update either side; document trade-off |
| P1-R3 | FFI stub patches needed for NULL guards in 14 functions | High | Medium | Apply guards in one commit; small change |
| P1-R4 | iOS warnings-as-errors reveals Swift compilation issues | Medium | Low | Fix or revert flag for that file |
| P1-R5 | SqliteStorage doesn't expose connection for raw queries | Low | Medium | Add `#[cfg(test)]` helper methods |
| P1-R6 | Some tests need additional `#[derive(Eq)]` on enums | Low | Low | Add as needed during writing |
| P1-R7 | LLM crates (e.g. tokio-tungstenite, chromiumoxide) clippy in relay shows upstream issues | Medium | Low | Allow specific lints with documented reason |

---

## 18. Reference Links

Files to read while executing Phase 1:

| Purpose | File |
|---------|------|
| Master verification plan | `.omo/plans/maho-mobile-verification-plan.md` |
| Original feature roadmap | `.omo/plans/maho-mobile-arcsearch-v2.md` |
| W2-W8 implementation plan | `.omo/plans/maho-mobile-w2-w8-execution.md` |
| ChatContent / ChatMessage source | `maho/crates/maho-types/src/chat.rs` |
| LLMStreamChunk source | `maho/crates/maho-types/src/llm.rs` |
| SpaceAIConfig source | `maho/crates/maho-types/src/space.rs` |
| FFI stub catalog | `maho/crates/maho-ffi/src/lib.rs` (lines 5133-5290) |
| iOS conversation bridge | `maho/ios-shell/Bridge/MahoBridge+Conversations.swift` |
| Android Kotlin models | `maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt` |
| Storage schema | `maho/crates/maho-storage/src/sqlite.rs` (initialize_schema, ensure_memory_tables) |

---

## END OF PHASE 1 EXECUTION PLAN
