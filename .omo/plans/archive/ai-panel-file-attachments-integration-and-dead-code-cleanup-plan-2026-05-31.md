# AI Panel — File Attachment Integration & Dead-Code Cleanup Plan

**Created:** 2026-05-31
**Scope:** `maho-chromium/browser/{ai,ui/webui/maho_ai,resources/maho_ai}/` + `maho/crates/{maho-acp,maho-ffi}/`
**Status:** Plan / Awaiting approval
**Estimated effort:** ~6 dev-hours (excluding full Chromium rebuild time)

---

## 0 — Background & Context

### 0.1 Why this plan exists

The frontend (composer.tsx + types.ts + textarea.tsx + composer.css) already exposes a complete UX for file/image attachments — `Cmd+V` paste capture, `Upload from computer` picker row, image-thumbnail attachment chips with X-remove buttons. **However, that data never reaches the AI**: when a user sends a prompt with an attached image, the chip renders but the `getSubmitContextAttachments` helper drops `kind: 'file'` entries on the floor, and even if it didn't, the Mojo `ContextAttachment` struct has no field to carry image bytes, the C++ page handler has no `kFile` switch arm, the adapter's `SubmitMessage` only accepts a `std::string`, and the Rust ACP wire layer hardcodes `type: "text"` content parts.

This plan delivers end-to-end attachment integration and removes the dead orchestration cluster discovered during the audit.

### 0.2 Production runtime path (verified)

```
WebUI (React/TS, chrome://maho-ai)
   │  Mojo IPC  (maho_ai.mojom)
   ▼
MahoAIPageHandler (browser/ui/webui/maho_ai/maho_ai_page_handler.cc:862)
   │
   ▼
MahoAiRuntimeRouter (browser/ai/maho_ai_runtime_router.cc)
   │  virtual dispatch
   ▼
MahoOpenCodeAcpAdapter (browser/ai/maho_opencode_acp_adapter.cc)
   │  C-ABI (maho_acp_submit_prompt)
   ▼
maho-ffi crate (maho/crates/maho-ffi/src/acp.rs:677)
   │
   ▼
maho-acp crate (maho/crates/maho-acp/src/{turn.rs, client.rs})
   │  JSON-RPC over stdio (newline-delimited)
   ▼
OpenCode runtime (external Bun/TypeScript process; `opencode acp`)
```

> Maho's `maho-core` Rust crate plays **zero role** in this path — confirmed by grep across all `.rs` files. The dead `MahoChatOrchestrationService` C++ class belongs to a deprecated direct-LLM-HTTP path that never used ACP.

### 0.3 OpenCode ACP image-part wire format (confirmed)

Source: [`sst/opencode@25edeaf/packages/opencode/src/acp/content.ts`](https://github.com/sst/opencode/blob/25edeaf4730b5053fa600f7fda3a98889dff0885/packages/opencode/src/acp/content.ts) (L41–93) and [agentclientprotocol.com/protocol/schema](https://agentclientprotocol.com/protocol/schema).

A `session/prompt` JSON-RPC request supports a `prompt: ContentBlock[]` discriminated union. Four `ContentBlock` variants exist; the three we need:

```json
// Image (image/png, image/jpeg, image/gif, image/webp) — primary use case
{ "type": "image", "mimeType": "image/png", "data": "<raw-base64-bytes>" }

// PDF / opaque binary
{ "type": "resource",
  "resource": { "uri": "file:///path", "mimeType": "application/pdf", "blob": "<base64>" } }

// Embedded text/* file
{ "type": "resource",
  "resource": { "uri": "file:///path", "mimeType": "text/plain", "text": "<utf8>" } }
```

OpenCode advertises capability via the `initialize` response (`promptCapabilities.image: true`, `embeddedContext: true` — `service.ts:118-121`). Vision routing is the upstream provider's responsibility — OpenCode passes through to whatever model the session uses. If the model lacks vision, OpenCode propagates a `session/update` error.

### 0.4 Scope boundaries

| In scope | Out of scope |
|---|---|
| Wire-protocol image/PDF/text-file ContentBlocks via ACP | Drag-drop file upload (already handled by Cmd+V paste + picker) |
| `kFile` Mojo + page-handler routing | OCR / image preprocessing (provider's job) |
| Dead-code deletion of `MahoChatOrchestrationService` cluster | Removing the legacy `attach_browser_context: bool` mojom field — can only be removed once all WebUI callers migrate to `attachments` array (separate plan) |
| Adapter-interface `SubmitMessage` parameter cleanup | Vision-capability check in WebUI (out-of-band; provider tells OpenCode at runtime) |

---

## 1 — Phase 0: Dead-Code Deletion (do this first)

This phase is fully self-contained, has no live callers, and unblocks the rest by simplifying the surface area we have to reason about. Land this PR before starting Phase 1.

### 1.1 `MahoChatOrchestrationService` — DELETE

**Verification basis:** Production grep returned zero `#include "maho_chat_orchestration_service.h"` matches outside the class's own `.cc` file. Page handler uses `maho_ai_runtime_router.h` (the live ACP path). The orchestration service was the pre-ACP direct-HTTP-LLM path.

| File | Action |
|---|---|
| `maho-chromium/browser/ai/maho_chat_orchestration_service.cc` | **Delete** |
| `maho-chromium/browser/ai/maho_chat_orchestration_service.h` | **Delete** |
| `maho-chromium/browser/BUILD.gn` (lines 86–107) | **Remove** the entire `source_set("maho_chat_orchestration") { ... }` block |
| `maho-chromium/browser/ui/webui/maho_ai/BUILD.gn` line 48 | **Remove** the `"//maho/browser:maho_chat_orchestration",` dep edge |

**Risk:** None. The orchestration service has dependencies on `:maho_ai_client`, `:maho_browser_ai_tools`, `:maho_prompt_utils` — all three remain live via other consumers (`maho_inline_edit_handler`, `maho_link_preview_manager`, page handler).

**Verification commands:**
```bash
# Before delete
grep -rn "maho_chat_orchestration" maho-chromium/ --include="*.cc" --include="*.h" --include="*.gn"

# After delete + GN edit
python3 maho-chromium/build/scripts/build_maho.py    # full rebuild succeeds
```

### 1.2 `MahoLegacyChatAdapter` — already absent

Files do not exist on disk. Search returned zero matches. **No action needed.**

### 1.3 `MahoAiRuntimeAdapter::SubmitMessage` `bool attach_browser_context` parameter — DROP from interface

**Verification basis:** `MahoOpenCodeAcpAdapter::SubmitMessage` receives the bool and **logs a DVLOG, then ignores it** (the page handler always resolves context to text before reaching the adapter — confirmed at `maho_ai_page_handler.cc:988-1056` for the legacy path). The bool only carries meaning at the page-handler layer.

| File | Action |
|---|---|
| `maho-chromium/browser/ai/maho_ai_runtime_adapter.h` | Remove `bool attach_browser_context` from the abstract `SubmitMessage(...)` declaration |
| `maho-chromium/browser/ai/maho_opencode_acp_adapter.h` line 44 | Drop the parameter from the override |
| `maho-chromium/browser/ai/maho_opencode_acp_adapter.cc` line 59 + DVLOG removal | Drop parameter + remove the DVLOG that printed it |
| `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc` (7 call sites) | Drop the `false` / `attach_browser_context` argument from each `adapter->SubmitMessage(...)` call: lines 926, 981, 1051, 1119, 1205 (and any others surfaced by build) |
| Tests (if any reference the parameter) | Update signatures |

**Out of scope here:** The mojom-level `SubmitPrompt(..., bool attach_browser_context, ...)` field at `maho_ai.mojom:385` and the page handler's `if (attach_browser_context)` branch at line 974 — those stay until the WebUI always sends a non-null `attachments` array (handled implicitly when Phase 1 ships, since `kFile` attachments mean `attachments` is always non-null when the user attaches anything; but text-only prompts still take the legacy bool path).

### 1.4 Phase 0 validation

```bash
# Sanity
cd maho-chromium && bunx tsc --noEmit -p browser/resources/maho_ai/react/tsconfig.json --ignoreDeprecations 6.0
cd maho && bun run typecheck                          # cargo check --workspace
python3 maho-chromium/build/scripts/build_maho.py    # full rebuild green
```

Phase 0 ships independently. **DO NOT** start Phase 1 until Phase 0 is merged or at least staged on the same branch.

---

## 2 — Phase 1: Mojo IDL Extension

Add file-attachment fields to the existing `ContextAttachment` struct. Additive only; no breaking changes.

### 2.1 File: `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`

**Edit at line 64 (`struct ContextAttachment`):**

```mojom
struct ContextAttachment {
  ContextSourceKind kind;
  // For kCurrentPage / kOpenTab: the tab_id returned by GetOpenTabs.
  // -1 means "active tab at submit time" (used by the legacy path).
  int32 tab_id;
  // Human-readable label shown in the composer chip (e.g. page title).
  string label;
  // URL associated with this attachment. May be empty for non-tab sources.
  string url;

  // === Phase C: file attachments (kind == kFile) ===
  // RFC 6838 MIME type. Empty for non-file kinds.
  string mime_type;
  // Raw bytes for inline file delivery. Used when:
  //   * mime_type starts with "image/" (any raster format)
  //   * mime_type is a binary format we want OpenCode to embed (e.g. application/pdf)
  // For text/* the page handler may instead populate text_content.
  array<uint8> bytes;
  // Decoded UTF-8 text payload, only for mime_type starting with "text/" or
  // application/json / application/xml / similar. Empty otherwise.
  string text_content;
};
```

> **Why two fields (`bytes` + `text_content`)?** OpenCode treats text-file resources distinctly from binary blobs (`content.ts:80-93`). Pre-decoding text on the C++ side avoids round-tripping bytes through base64 for text/* and matches OpenCode's expected `resource.text` shape directly.

**No changes needed to:** `ContextSourceKind` (already has `kFile`), the `attachments` parameter on `SubmitPrompt`, or any other struct.

### 2.2 Regenerated bindings (auto-managed)

| File | Mechanism |
|---|---|
| `maho-chromium/browser/resources/maho_ai/maho_ai.mojom-webui.js.d.ts` | Regenerated by Mojo build step |
| `maho-chromium/browser/resources/maho_ai/standalone/maho_ai.mojom-webui.js` | Manual sync if standalone preview must work without full Chromium build |

**The standalone bundle's mojom JS must be hand-updated** to mirror the new struct fields, because it's a checked-in stub used only by `dev_server.mjs`. Add the same fields with sensible defaults (empty string / empty array).

---

## 3 — Phase 2: Rust ACP Wire Layer

Add image and resource ContentBlock variants. The `prompt` array shape already supports it — only `PromptContentPart` and `to_value()` need to change.

### 3.1 File: `maho/crates/maho-acp/src/turn.rs`

**Replace the current `PromptContentPart` (line 11-16) and `to_value()` (line 19-27) with a discriminated enum:**

```rust
#[derive(Debug, Clone)]
pub enum PromptContentPart {
    Text {
        text: String,
    },
    Image {
        mime_type: String,
        // Raw base64 (NOT a data: URL) — see content.ts:41-72
        data_base64: String,
    },
    ResourceBlob {
        uri: String,
        mime_type: String,
        blob_base64: String,
    },
    ResourceText {
        uri: String,
        mime_type: String,
        text: String,
    },
}

impl PromptContentPart {
    fn to_json(&self) -> Value {
        match self {
            Self::Text { text } => json!({ "type": "text", "text": text }),
            Self::Image { mime_type, data_base64 } => json!({
                "type": "image",
                "mimeType": mime_type,
                "data": data_base64,
            }),
            Self::ResourceBlob { uri, mime_type, blob_base64 } => json!({
                "type": "resource",
                "resource": { "uri": uri, "mimeType": mime_type, "blob": blob_base64 },
            }),
            Self::ResourceText { uri, mime_type, text } => json!({
                "type": "resource",
                "resource": { "uri": uri, "mimeType": mime_type, "text": text },
            }),
        }
    }
}

#[derive(Debug, Clone)]
pub(crate) struct PromptParams {
    pub session_id: String,
    pub prompt: Vec<PromptContentPart>,
}

impl PromptParams {
    pub fn to_value(&self) -> Value {
        json!({
            "sessionId": self.session_id,
            "prompt": self.prompt.iter().map(PromptContentPart::to_json).collect::<Vec<_>>(),
        })
    }
}
```

**Test additions in `turn.rs` (or `turn_test.rs`):**
- Round-trip `PromptParams::to_value()` for each enum variant against fixture JSON matching OpenCode's `content.ts` parsing expectations.

### 3.2 File: `maho/crates/maho-acp/src/client.rs`

**Edit `submit_prompt` at line 256:**

```rust
pub async fn submit_prompt(
    &self,
    session_id: &str,
    parts: Vec<PromptContentPart>,
) -> Result<PromptHandle, Error> {
    let params = PromptParams {
        session_id: session_id.to_string(),
        prompt: parts,
    };
    let (msg, rx) = self
        .router
        .build_request("session/prompt", params.to_value())
        .await;
    // ... rest unchanged
}
```

**Backward compatibility helper** (kept temporarily for any in-tree callers that still pass plain text):

```rust
pub async fn submit_prompt_text(
    &self,
    session_id: &str,
    text: String,
) -> Result<PromptHandle, Error> {
    self.submit_prompt(session_id, vec![PromptContentPart::Text { text }]).await
}
```

### 3.3 File: `maho/crates/maho-acp/src/lib.rs`

Re-export the new enum so the FFI layer can name it:

```rust
pub use turn::PromptContentPart;
```

### 3.4 Test fixtures

| File | Action |
|---|---|
| `maho/crates/maho-acp/src/turn.rs` (or new `tests/turn_test.rs`) | Add unit tests for each variant's JSON output |
| `maho/crates/maho-acp/tests/integration.rs` (if exists) | Add an integration test that submits a mixed text+image prompt and asserts the wire bytes match the expected JSON-RPC frame |

---

## 4 — Phase 3: maho-ffi C-ABI Extension

The existing `maho_acp_submit_prompt(handle, session_id, text)` stays for backward compat (Phase 1.3 simplification path). Add a new function for multipart submission.

### 4.1 File: `maho/crates/maho-ffi/src/acp.rs`

**After line 826 (current end of `maho_acp_submit_prompt`), add:**

```rust
/// Submit a multi-part prompt (text + images + resources) to OpenCode.
///
/// `parts_json` must be a NUL-terminated UTF-8 C string containing a
/// JSON array matching OpenCode's ACP ContentBlock[] schema, e.g.:
///
/// ```json
/// [
///   { "type": "text", "text": "What is in this image?" },
///   { "type": "image", "mimeType": "image/png", "data": "<base64>" }
/// ]
/// ```
///
/// # Safety
/// All pointers must be valid NUL-terminated UTF-8 C strings allocated
/// by the caller. The handle must be one returned from `maho_acp_create`
/// and not yet destroyed.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_submit_prompt_parts(
    handle: *mut MahoAcpHandle,
    session_id: *const c_char,
    parts_json: *const c_char,
) -> i32 {
    // SAFETY: Caller contract; all FFI strings must be valid C strings.
    let Some(handle) = handle.as_mut() else { return -1; };
    let Ok(sid) = c_str_to_string(session_id) else { return -2; };
    let Ok(json_str) = c_str_to_string(parts_json) else { return -3; };

    let parts = match parse_parts_json(&json_str) {
        Ok(p) => p,
        Err(_) => return -4,
    };

    handle.runtime.spawn({
        let client = handle.client.clone();
        async move {
            let _ = client.submit_prompt(&sid, parts).await;
        }
    });
    0
}

fn parse_parts_json(s: &str) -> Result<Vec<PromptContentPart>, serde_json::Error> {
    use serde_json::Value;
    let arr: Vec<Value> = serde_json::from_str(s)?;
    let mut out = Vec::with_capacity(arr.len());
    for v in arr {
        let Some(t) = v.get("type").and_then(Value::as_str) else { continue };
        match t {
            "text" => {
                if let Some(text) = v.get("text").and_then(Value::as_str) {
                    out.push(PromptContentPart::Text { text: text.into() });
                }
            }
            "image" => {
                if let (Some(mt), Some(data)) = (
                    v.get("mimeType").and_then(Value::as_str),
                    v.get("data").and_then(Value::as_str),
                ) {
                    out.push(PromptContentPart::Image {
                        mime_type: mt.into(),
                        data_base64: data.into(),
                    });
                }
            }
            "resource" => {
                let r = v.get("resource").cloned().unwrap_or_default();
                let uri = r.get("uri").and_then(Value::as_str).unwrap_or("").to_string();
                let mt = r.get("mimeType").and_then(Value::as_str).unwrap_or("").to_string();
                if let Some(text) = r.get("text").and_then(Value::as_str) {
                    out.push(PromptContentPart::ResourceText {
                        uri, mime_type: mt, text: text.into(),
                    });
                } else if let Some(blob) = r.get("blob").and_then(Value::as_str) {
                    out.push(PromptContentPart::ResourceBlob {
                        uri, mime_type: mt, blob_base64: blob.into(),
                    });
                }
            }
            _ => {} // skip unknown
        }
    }
    Ok(out)
}
```

### 4.2 File: `maho-chromium/third_party/maho/maho_ffi.h`

**Add the C declaration alongside `maho_acp_submit_prompt` (line 1595):**

```c
// Submit a multi-part prompt. parts_json is the JSON ContentBlock array.
// Returns 0 on success, negative on error.
int32_t maho_acp_submit_prompt_parts(
    MahoAcpHandle* handle,
    const char* session_id,
    const char* parts_json);
```

### 4.3 Rebuild prebuilt static lib

```bash
python3 maho-chromium/build/scripts/build_maho_core.py
```

This regenerates `maho-chromium/third_party/maho/lib/libmaho_ffi.a` (and the `.h` if drift-checked). The new symbol must appear in `nm` output:

```bash
nm maho-chromium/third_party/maho/lib/libmaho_ffi.a | grep maho_acp_submit_prompt_parts
```

---

## 5 — Phase 4: C++ Adapter & Page Handler Wiring

### 5.1 File: `maho-chromium/browser/ai/maho_ai_runtime_adapter.h`

**Replace `SubmitMessage` (assuming Phase 0.3 already dropped the bool):**

```cpp
class MahoAiRuntimeAdapter {
 public:
  // ... existing methods ...

  struct PromptPart {
    enum class Kind { kText, kImage, kResourceBlob, kResourceText };
    Kind kind;
    std::string text;          // Text content or text_content for ResourceText
    std::string mime_type;     // Image / Resource only
    std::string data_base64;   // Image data / ResourceBlob blob (raw base64)
    std::string uri;           // Resource only (file:// or similar)
  };

  // Replaces the old single-string SubmitMessage. The first part is typically
  // the augmented user prompt; subsequent parts are image/resource attachments.
  virtual void SubmitPrompt(std::vector<PromptPart> parts,
                            maho_ai::mojom::ChatIntent chat_intent,
                            maho_ai::mojom::InteractionMode mode,
                            RuntimeEventCallback on_event) = 0;
};
```

> **Migration note:** `SubmitMessage(std::string)` can stay temporarily as a non-virtual helper in a base impl that wraps a single Text part — keeps the diff small. Or do the rename atomically. Recommended: atomic rename.

### 5.2 File: `maho-chromium/browser/ai/maho_opencode_acp_adapter.{h,cc}`

**Header edit** (matches new interface).

**Implementation edit** (`maho_opencode_acp_adapter.cc:59-110` and the deferred path at `:330-380`):

```cpp
void MahoOpenCodeAcpAdapter::SubmitPrompt(
    std::vector<PromptPart> parts,
    maho_ai::mojom::ChatIntent chat_intent,
    maho_ai::mojom::InteractionMode mode,
    RuntimeEventCallback on_event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // ... state-machine bookkeeping unchanged ...

  // First part is text; augment in place.
  if (!parts.empty() && parts[0].kind == PromptPart::Kind::kText) {
    parts[0].text = AugmentMessageWithMemory(parts[0].text);
  }

  base::Value::List parts_json = SerializePartsToAcpJson(parts);
  std::string parts_json_str;
  base::JSONWriter::Write(parts_json, &parts_json_str);

  // SAFETY: acp_handle_ is valid in kReady; session_id_ is non-empty.
  int32_t rc = maho_acp_submit_prompt_parts(
      acp_handle_,
      session_id_.c_str(),
      parts_json_str.c_str());
  if (rc != 0) {
    TransitionToError(base::StringPrintf("submit_prompt_parts failed: %d", rc));
    return;
  }
  // ... unchanged
}
```

**New helper** (private, in the same `.cc`):

```cpp
namespace {
base::Value::List SerializePartsToAcpJson(
    const std::vector<MahoAiRuntimeAdapter::PromptPart>& parts) {
  base::Value::List arr;
  for (const auto& p : parts) {
    base::Value::Dict obj;
    using Kind = MahoAiRuntimeAdapter::PromptPart::Kind;
    switch (p.kind) {
      case Kind::kText:
        obj.Set("type", "text");
        obj.Set("text", p.text);
        break;
      case Kind::kImage:
        obj.Set("type", "image");
        obj.Set("mimeType", p.mime_type);
        obj.Set("data", p.data_base64);
        break;
      case Kind::kResourceBlob: {
        obj.Set("type", "resource");
        base::Value::Dict r;
        r.Set("uri", p.uri);
        r.Set("mimeType", p.mime_type);
        r.Set("blob", p.data_base64);
        obj.Set("resource", std::move(r));
        break;
      }
      case Kind::kResourceText: {
        obj.Set("type", "resource");
        base::Value::Dict r;
        r.Set("uri", p.uri);
        r.Set("mimeType", p.mime_type);
        r.Set("text", p.text);
        obj.Set("resource", std::move(r));
        break;
      }
    }
    arr.Append(std::move(obj));
  }
  return arr;
}
}  // namespace
```

**Pending message** (the deferred submit path at `:330-380`): change `pending_message_` from `std::string` to `std::vector<PromptPart>`. Same drain semantics.

### 5.3 File: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.{h,cc}`

**`maho_ai_page_handler.h`:** Update `AttachmentSlot` struct to carry file payload:

```cpp
struct AttachmentSlot {
  maho_ai::mojom::ContextSourceKind kind;
  std::string label;
  std::string url;
  // === Existing fields for tab/page context (extracted text) ===
  std::optional<MahoAiPageContext> page_context;
  // === NEW: file attachment payload ===
  std::string file_mime_type;
  std::vector<uint8_t> file_bytes;     // for binary kinds
  std::string file_text_content;        // for text/*
};
```

**`maho_ai_page_handler.cc:910-969`** (the two switch blocks for `ContextSourceKind`):

Add `kFile` arms in both blocks:

```cpp
// First switch: slot construction
case maho_ai::mojom::ContextSourceKind::kFile:
  slot.kind = maho_ai::mojom::ContextSourceKind::kFile;
  slot.label = att->label;
  slot.url = att->url;
  slot.file_mime_type = att->mime_type;
  slot.file_bytes = std::move(att->bytes);
  slot.file_text_content = std::move(att->text_content);
  // No async work needed — file data is already in the Mojo message.
  break;

// Second switch: dispatch
case maho_ai::mojom::ContextSourceKind::kFile:
  // Synchronously mark slot complete.
  pending_->slots[slot_index].state = SlotState::kCompleted;
  break;
```

**`maho_ai_page_handler.cc:1126-1210`** (`OnAttachmentSlotCompleted` — multi-attachment batch builder):

This is where the prompt is currently built as a single string. Refactor it to build a `std::vector<MahoAiRuntimeAdapter::PromptPart>` instead:

```cpp
void MahoAIPageHandler::OnAttachmentSlotCompleted(/* ... */) {
  // ... existing slot bookkeeping unchanged ...
  if (!AllSlotsResolved()) return;

  std::vector<MahoAiRuntimeAdapter::PromptPart> parts;
  std::string text_prefix;     // accumulates page/tab/history/bookmark text

  for (const auto& slot : pending_->slots) {
    using Kind = maho_ai::mojom::ContextSourceKind;
    switch (slot.kind) {
      case Kind::kCurrentPage:
      case Kind::kOpenTab:
      case Kind::kHistory:
      case Kind::kBookmark:
        // Existing behavior: concatenate extracted text into prefix.
        text_prefix += FormatPageContextSlot(slot);
        break;
      case Kind::kFile: {
        if (slot.file_mime_type.starts_with("image/")) {
          parts.push_back({
            MahoAiRuntimeAdapter::PromptPart::Kind::kImage,
            /*text=*/{},
            slot.file_mime_type,
            base::Base64Encode(slot.file_bytes),
            /*uri=*/{},
          });
        } else if (slot.file_mime_type.starts_with("text/") ||
                   slot.file_mime_type == "application/json" ||
                   slot.file_mime_type == "application/xml") {
          parts.push_back({
            MahoAiRuntimeAdapter::PromptPart::Kind::kResourceText,
            slot.file_text_content,
            slot.file_mime_type,
            /*data_base64=*/{},
            /*uri=*/"file:///" + slot.label,  // synthetic; OpenCode uses for filename
          });
        } else {
          // application/pdf, application/zip, etc.
          parts.push_back({
            MahoAiRuntimeAdapter::PromptPart::Kind::kResourceBlob,
            /*text=*/{},
            slot.file_mime_type,
            base::Base64Encode(slot.file_bytes),
            "file:///" + slot.label,
          });
        }
        break;
      }
    }
  }

  // First part is the user's question, prefixed with any page/tab text.
  std::string final_text =
      text_prefix.empty()
          ? pending_->user_prompt
          : text_prefix + "\n\nUser Question:\n" + pending_->user_prompt;
  parts.insert(parts.begin(), {
    MahoAiRuntimeAdapter::PromptPart::Kind::kText,
    std::move(final_text), {}, {}, {},
  });

  adapter_->SubmitPrompt(std::move(parts), pending_->intent, pending_->mode,
                         std::move(pending_->on_event));
  pending_.reset();
}
```

**Update all 5 call sites** of the old `adapter_->SubmitMessage(string, ...)` (lines 926, 981, 1051, 1119, 1205) to wrap their string in a single-element `vector<PromptPart>` with `kText` kind.

### 5.4 File: `maho-chromium/browser/ai/maho_ai_runtime_router.{h,cc}`

If the router has its own `SubmitMessage` forwarding method (likely), update its signature in lockstep with the adapter interface.

### 5.5 BUILD.gn additions

The existing `maho_opencode_acp_adapter` source_set in `maho-chromium/browser/BUILD.gn` already depends on `:maho_ai_runtime_adapter` and `//base`. The new helper uses `base::JSONWriter` and `base::Base64Encode` — both already-transitive deps. **No GN changes expected**, but verify the link succeeds.

---

## 6 — Phase 5: WebUI Frontend

The frontend is mostly already wired — only `getSubmitContextAttachments` needs to actually push file attachments.

### 6.1 File: `maho-chromium/browser/resources/maho_ai/types.ts`

**Edit `getSubmitContextAttachments` at line 294:**

```typescript
export function getSubmitContextAttachments(
    attachments: ComposerAttachment[]): SubmitContextAttachmentRecord[]|null {
  const hasCurrentPage = hasCurrentPageAttachment(attachments);
  const tabAttachments = attachments.filter(isTabAttachment);
  const historyAttachments = attachments.filter(a => a.kind === 'history');
  const bookmarkAttachments = attachments.filter(a => a.kind === 'bookmark');
  const fileAttachments = attachments.filter(isFileAttachment);

  if (!tabAttachments.length && !historyAttachments.length &&
      !bookmarkAttachments.length && !fileAttachments.length) {
    return null;
  }

  const submitAttachments: SubmitContextAttachmentRecord[] = [];

  if (hasCurrentPage) {
    submitAttachments.push({
      kind: ContextSourceKind.kCurrentPage,
      tabId: -1,
      label: CURRENT_PAGE_ATTACHMENT.label,
      url: '',
      mimeType: '',
      bytes: [],
      textContent: '',
    });
  }

  // ... existing tab/history/bookmark pushes, each adding the new fields as empty ...

  for (const attachment of fileAttachments) {
    const {bytes, textContent} = decomposeFileAttachment(attachment);
    submitAttachments.push({
      kind: ContextSourceKind.kFile,
      tabId: -1,
      label: attachment.label,
      url: attachment.url || '',
      mimeType: attachment.mimeType || 'application/octet-stream',
      bytes,
      textContent,
    });
  }

  return submitAttachments;
}

function decomposeFileAttachment(
    attachment: ComposerAttachment): {bytes: number[]; textContent: string} {
  if (!attachment.dataUrl) {
    return {bytes: [], textContent: ''};
  }
  const isText = (attachment.mimeType ?? '').startsWith('text/') ||
      attachment.mimeType === 'application/json' ||
      attachment.mimeType === 'application/xml';
  if (isText) {
    // Decode the data URL's base64 payload to UTF-8.
    const base64 = attachment.dataUrl.split(',')[1] ?? '';
    return {bytes: [], textContent: atob(base64)};
  }
  // Decode base64 -> Uint8Array -> number[] for Mojo serialization.
  const base64 = attachment.dataUrl.split(',')[1] ?? '';
  const bin = atob(base64);
  const bytes = new Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return {bytes, textContent: ''};
}
```

> **Note:** `bytes` is `array<uint8>` in Mojo, which surfaces in TypeScript as `Uint8Array | number[]` — confirm by reading the regenerated `.d.ts`. Use whichever the binding expects.

### 6.2 File: `maho-chromium/browser/resources/maho_ai/standalone/maho_ai.mojom-webui.js`

Update the standalone stub to mirror the new fields (mimeType, bytes, textContent default values). Otherwise the dev server's mojom JS won't match the new TypeScript types and the bundle will fail to typecheck.

### 6.3 No changes needed

| File | Reason |
|---|---|
| `react/features/compact/composer.tsx` | Already correctly creates file attachments via `createFileAttachment` and passes them through `state.composer.attachments` |
| `react/ui/textarea.tsx` | Already wires `onPasteFiles` |
| `types.ts:createFileAttachment` | Already produces correct `{kind, label, mimeType, size, dataUrl}` shape |
| `styles/composer.css` | UI shipped; no changes |

### 6.4 Frontend validation

```bash
bunx tsc --noEmit -p maho-chromium/browser/resources/maho_ai/react/tsconfig.json --ignoreDeprecations 6.0
node maho-chromium/browser/resources/maho_ai/standalone/dev_server.mjs
# Open http://127.0.0.1:4071, paste a screenshot, send; verify the WebUI submit
# call surfaces the file attachment in the network tab (or its standalone equivalent).
```

---

## 7 — Phase 6: Build, Test, Verify

### 7.1 Full build sequence

```bash
# 1. Rust (regen prebuilt FFI)
python3 maho-chromium/build/scripts/build_maho_core.py

# 2. Chromium (regen mojo bindings + compile C++)
python3 maho-chromium/build/scripts/build_maho.py

# 3. WebUI typecheck
cd maho-chromium/browser/resources/maho_ai
bunx tsc --noEmit -p react/tsconfig.json --ignoreDeprecations 6.0
```

> **Build lock**: `build_maho.py` and `build_maho_core.py` both acquire the workspace `.build.lock`. Run sequentially. **Never `rm .build.lock`** (see root `AGENTS.md`).

### 7.2 Unit tests

| Layer | Test file | Adds |
|---|---|---|
| Rust ACP wire | `maho/crates/maho-acp/src/turn.rs` (inline `#[cfg(test)] mod tests`) | 4 round-trip tests, one per `PromptContentPart` variant; assert exact JSON shape |
| Rust FFI | `maho/crates/maho-ffi/tests/acp_ffi.rs` | One test that calls `maho_acp_submit_prompt_parts` with a hand-built JSON array fixture and asserts the call returns 0 + the wire-level message matches |
| C++ adapter | `maho-chromium/browser/ai/maho_opencode_acp_adapter_unittest.cc` (if exists) | Test `SerializePartsToAcpJson` for each `PromptPart::Kind` |
| C++ page handler | `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler_unittest.cc` (if exists) | Test that a submit with one image attachment produces a 2-part vector (text prompt + image part) |
| WebUI | `maho-chromium/browser/resources/maho_ai/types_test.ts` (or vitest) | Test `getSubmitContextAttachments` for image, PDF, text/plain attachments — assert correct field population |

### 7.3 Manual end-to-end QA

1. Build production Chromium (`python3 maho-chromium/build/scripts/build_maho.py`).
2. Launch `out/Default/Maho` (or platform equivalent).
3. Open AI panel (`chrome://maho-ai`).
4. Test sequence — for each, verify the model **actually sees** the attached content:
   - Paste a PNG screenshot via `Cmd+V` → ask "What is in this image?"
   - Click **Upload from computer** → pick a `.txt` file → ask "Summarize this"
   - Paste a JPEG → ask "Describe the colors"
   - Pick a `.pdf` → ask "What does page 1 say?" (requires the active model to support PDF — Claude 3.5 Sonnet does, GPT-4o does not natively, OpenCode passes through error if unsupported)
5. Capture `out/Default/Logs/chrome_debug.log` for any `submit_prompt_parts failed` errors.

### 7.4 Acceptance criteria

- [ ] `MahoChatOrchestrationService` files deleted, build green
- [ ] `attach_browser_context` parameter dropped from adapter interface
- [ ] `ContextAttachment` mojom struct has `mime_type`, `bytes`, `text_content` fields
- [ ] `maho_acp_submit_prompt_parts` C-ABI export exists in libmaho_ffi.a
- [ ] `PromptContentPart::Image` variant produces wire JSON `{"type":"image","mimeType":"...","data":"..."}` matching OpenCode's `content.ts:41-72` parser
- [ ] WebUI `getSubmitContextAttachments` includes file attachments in the Mojo payload
- [ ] Manual QA: image paste + send → assistant reply references image content
- [ ] No regression in plain-text prompt path (no attachments, no images)
- [ ] No regression in `kCurrentPage` legacy `attach_browser_context: bool` path until that is migrated separately

---

## 8 — Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| OpenCode model lacks vision capability | Medium | User confusion | Surface OpenCode's `session/update` error in BOTH a toast AND a permanent thread system message (see §10.1) |
| Renderer OOM from very large attachment | Medium | Browser tab crash | No client-side size cap (see §10.2). Page handler enforces 256 MB sanity ceiling per `att->bytes.size()`; above that, respond with mojo bad-message; provider-side limits surface as normal error events |
| ACP `LineTransport` truncates very large JSON-RPC line | Low | Wire-level corruption | Add stress test: submit 100 MB blob; assert no truncation. `transport.rs` is line-based but uses `Vec<u8>` reads — should handle arbitrary length |
| `field-sizing: content` CSS not supported in Chromium <123 | Already shipped | None | Chromium upstream is >=123 in this overlay; non-issue |
| FFI ABI version mismatch after rebuild | Low | Build/runtime crash | The `maho-ffi` crate exposes a `MAHO_FFI_VERSION` constant; bump it when adding `maho_acp_submit_prompt_parts`. C++ should DCHECK on init |
| Standalone preview drifts from production mojom | High | Standalone "lies" to developers | Add a CI check (or pre-commit hook) that diffs the hand-written stub against the regenerated bindings |
| User pastes SVG expecting it to work | Low | Confusion | Block at WebUI input/paste handlers with explicit toast (see §10.4) |

### 8.1 Rollback plan

Each phase is a separate commit. Rollback order if Phase 1+ fails post-merge:

1. **Phase 5 (WebUI)** — revert `types.ts` change → file attachments stop being submitted; UI chips still render but data drops on the floor. Same as today's behavior.
2. **Phase 4 (C++ adapter / page handler)** — revert; falls back to single-string `SubmitMessage`. WebUI types.ts changes will fail Mojo validation if not also reverted.
3. **Phase 3 (FFI)** — keep the new `maho_acp_submit_prompt_parts` symbol; just unused by C++.
4. **Phase 2 (Rust ACP)** — revert; restore `submit_prompt(text: String)`.
5. **Phase 1 (Mojom)** — last to revert; mojom changes are the contract spine.

Phase 0 (dead-code deletion) does not need rollback — it's strictly subtractive.

---

## 9 — Phase Sequencing & Time Estimate

| Phase | Task | Estimated effort | Blocks next? |
|---|---|---|---|
| 0 | Dead-code deletion (`MahoChatOrchestrationService` + adapter bool param) | 30 min | No (independent commit) |
| 1 | Mojom IDL extension | 15 min | Yes (regen blocks 4–5) |
| 2 | Rust `maho-acp` wire layer | 1 h | Yes (blocks 3) |
| 3 | Rust `maho-ffi` C-ABI + prebuilt rebuild | 45 min | Yes (blocks 4) |
| 4 | C++ adapter + page handler + router | 2 h | Yes (blocks 5) |
| 5 | WebUI `getSubmitContextAttachments` | 30 min | No |
| 6 | Tests + manual QA + Chromium full rebuild | 1 h + build wait | — |
| **Total dev time** | | **~6 h** | |
| **Build wait** | full Chromium rebuild on first GN regen | **30–90 min** | — |

---

## 10 — Resolved Decisions (2026-05-31)

User decisions for previously-open questions, applied throughout the plan above:

### 10.1 Vision-unsupported error UX → **Toast + thread error**

When OpenCode emits a `session/update` with an error indicating the underlying provider rejected the image (e.g. model lacks vision), the WebUI must:
1. Show a transient toast (`"This model doesn't support images"`) so the user sees it immediately even if scrolled away.
2. Insert a permanent system message into the conversation thread with `data-tone='danger'`, so the failure stays visible after scroll.

**Implementation surface** (additive to plan above):

- `maho-chromium/browser/resources/maho_ai/store.ts` — when an `error` event arrives whose `text` matches a vision-rejection pattern, dispatch BOTH `addToast({tone: 'danger', text: ...})` and the existing `appendSystemEvent({tone: 'danger', text: ...})`. Pattern detection: lower-case substring match on `'image'` AND (`'unsupported'` OR `'not supported'` OR `'vision'`). Conservative — false negatives are tolerable (the thread error still surfaces), false positives produce no harm (toast is short-lived).
- `maho-chromium/browser/resources/maho_ai/react/features/compact/toast.tsx` — if a toast component does not yet exist, add a minimal one. Position: top-center, auto-dismiss 4 seconds, manual close X. Respect `prefers-reduced-motion`.
- `styles/compact-panel.css` — toast styling using existing `--maho-ai-bg-glass` + `--maho-ai-shadow-glass` tokens for native consistency.

### 10.2 File size cap → **No client-side limit**

The WebUI does NOT enforce a max attachment size. Rationale: OpenCode/provider-side limits vary by model (Anthropic 5MB, GPT-4o 20MB, Gemini larger, etc.) and change over time. A hardcoded client cap risks blocking legitimate uploads. Provider rejection becomes a normal error path (handled by §10.1).

**Implications for the plan:**

- Remove the "10 MB cap" item from §8 Risks.
- Page handler: still validate `att->bytes.size()` ≤ a sanity ceiling (e.g. 256 MB) to avoid OOM-class issues from a single malicious WebUI message — but this is a guard, not a UX limit.
- ACP wire frame: a single very large image will produce a multi-megabyte JSON-RPC line. `LineTransport` (`maho-acp/src/transport.rs`) already handles arbitrary-length lines; verify by writing a stress test that submits a 100 MB blob and asserts the wire layer doesn't truncate.
- Memory: `std::vector<uint8_t>` round-trip is fine; Mojo's `array<uint8>` uses a shared-buffer fast path for large arrays in modern Chromium.

**Risk update**: Add row to §8 — "User pastes/attaches a huge image, browser memory pressure spikes" — Mitigation: monitor renderer memory in QA, add the 256 MB sanity guard, otherwise rely on provider-side rejection.

### 10.3 PR strategy → **Single PR**

All phases (0 through 6) ship as one PR. Rationale: user wants a single review cycle. Reviewers see the full picture (dead-code removal + file-attachment integration + tests).

**Implications:**

- Commit structure within the PR: keep one commit per phase (7 commits) so `git log --oneline` reads as a clear story and individual phases can be cherry-picked or reverted if a regression is bisected later.
- CI must pass on the final commit only; intermediate commits may have transient inconsistencies (e.g. mojom updated but page handler not yet) — that's acceptable for a squash-rebase or merge-commit landing.
- Reviewers should read commits in order. PR description must list the 7 commits with one-line summaries.

### 10.4 SVG support → **Blocked at WebUI**

`image/svg+xml` is rejected before it can become a `ComposerAttachment`. Rationale: SVG is XML, not raster — model behavior is inconsistent (some treat as image, some as text, some refuse). PNG conversion via the user's screenshot tool is the recommended path.

**Implementation surface:**

- `maho-chromium/browser/resources/maho_ai/react/features/compact/composer.tsx`:
  - Hidden `<input type="file">` (`composer.tsx:439-452`): change `accept="image/*,application/pdf,text/*,.md,.json,.csv,.txt"` → `accept="image/png,image/jpeg,image/gif,image/webp,application/pdf,text/*,.md,.json,.csv,.txt"` (drop generic `image/*`, list specific raster MIMEs explicitly to exclude SVG even on browsers that classify SVG under `image/*`).
  - File-handler (`onAttachFiles` wire in `app.tsx`): before calling `createFileAttachment`, reject any `File` whose `type === 'image/svg+xml'` OR whose name ends in `.svg` (defense-in-depth — the OS picker may not always send a MIME). On reject, dispatch a toast: `"SVG files aren't supported. Convert to PNG first."`
- Paste handler (`textarea.tsx:85-103`): same reject-on-svg check before adding to `files` array.
- `types.ts:createFileAttachment` (defense layer): if `file.type === 'image/svg+xml'`, throw a typed error so callers must handle it explicitly. Document the contract in a comment.

---

**End of plan.**
