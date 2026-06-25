# AI Panel — File Attachment Integration & Dead-Code Cleanup Plan **v2**

**Created:** 2026-05-31
**Supersedes:** `.sisyphus/plans/ai-panel-file-attachments-integration-and-dead-code-cleanup-plan-2026-05-31.md` (v1)
**Status:** Plan / Ready to execute (pending one user input on R4 fix scope)
**Scope:** `maho-chromium/browser/{ai,ui/webui/maho_ai,resources/maho_ai}/` + `maho/crates/{maho-acp,maho-ffi}/`
**Estimated effort:** ~5 dev-hours (excluding full Chromium rebuild time)
**Provenance:** v1 plan ran through 5-member adversarial review (skeptic / validator / researcher / architect / creative) over 3 rounds. 32 of 39 findings survived. Plan agent's investigation refuted the largest "FATAL" finding (R4) by tracing both transport code paths.

---

## 0 — Executive Summary of Changes vs v1

| Area | v1 | v2 |
|---|---|---|
| **R4 transport cap** | "FATAL — 64 KiB blocks all images" | **REFUTED** — cap is inbound-only (`read_line`), outbound `write_message` has zero size check. Push model works as-is. Raise MAX_LINE_BYTES to 1 MiB as inbound safety measure only. |
| **Wire layer ownership** | C++ builds JSON via `SerializePartsToAcpJson`, Rust parses via `parse_parts_json` (double serialization) | Rust owns JSON serialization end-to-end; pass typed slices across the C-ABI |
| **Variant scope** | All 4 variants (Text, Image, ResourceBlob, ResourceText) shipped | Text + Image user-facing only; Rust enum keeps all 4 arms (zero cost) for future expansion |
| **WebUI accept attribute** | `image/*, application/pdf, text/*, .md, .json, .csv, .txt` | `image/png, image/jpeg, image/gif, image/webp` only |
| **Old FFI symbol** | `submit_prompt_text` Rust helper kept "for backward compat" | Atomic delete in same commit as new symbol; rollback = git revert |
| **Test coverage** | 5-layer pyramid | 3 layers: 1 C++ unit + 1 WebUI unit + 1 Rust integration |
| **Commit structure** | 7 commits with admitted "transient CI failures" | 1 squashed commit; CI runs on merge only |
| **Rollback** | "Revert individual phases" (factually impossible due to Mojo coupling) | Feature flag `kMahoAiFileAttachments` at WebUI submit boundary; rollback = config flip |
| **OOM guard cap** | 256 MB (theater) | 25 MB at page handler (matches provider limits) |
| **`MAHO_FFI_VERSION` mitigation** | Cited in §8 risk table | DELETED — constant doesn't exist; static linking catches symbol mismatch at link time |
| **Mojo `array<uint8>` shared-buffer claim** | Asserted as fact in §10.2 | DELETED from plan text — uncited, unverified |
| **Adapter `attach_browser_context` bool** | Drop from interface (Phase 0.3) | Same — but add forcing function comment in mojom for legacy bool removal |
| **Dead-code cluster** | `MahoChatOrchestrationService` confirmed dead | Same — confirmed by all 5 reviewers |
| **Standalone dev server** | "TODO: CI check or pre-commit hook" | Concrete: `tools/check_standalone_mojom.sh` script with CI-blocking diff |
| **toast.tsx** | Implied larger component | Hard 30-line LOC budget, reuse existing tokens, no new CSS file |

---

## 1 — Background & Production Path (verified)

```
WebUI (React/TS, chrome://maho-ai)
   │  Mojo IPC  (maho_ai.mojom)
   ▼
MahoAIPageHandler (browser/ui/webui/maho_ai/maho_ai_page_handler.cc:862 — SubmitPrompt entry)
   │
   ▼
MahoAiRuntimeRouter (browser/ai/maho_ai_runtime_router.cc)
   │  virtual dispatch
   ▼
MahoOpenCodeAcpAdapter (browser/ai/maho_opencode_acp_adapter.cc)
   │  C-ABI (maho_acp_submit_prompt → maho_acp_submit_prompt_parts)
   ▼
maho-ffi crate (maho/crates/maho-ffi/src/acp.rs)
   │
   ▼
maho-acp crate (maho/crates/maho-acp/src/{turn.rs, client.rs, transport.rs})
   │  JSON-RPC over stdio (newline-delimited; outbound unconstrained, inbound capped)
   ▼
OpenCode runtime (external Bun/TypeScript process)
```

`maho-core` Rust crate is **NOT** in this path — confirmed by full-tree grep.

OpenCode advertises `promptCapabilities: { embeddedContext: true, image: true }` in `initialize` response. Wire format for image part: `{type: "image", mimeType: "image/png", data: "<raw-base64>"}` — confirmed at `sst/opencode@dev:packages/opencode/src/acp/content.ts`.

---

## 2 — Hard Constraints (Survived Adversarial Review)

These constraints MUST hold throughout implementation:

1. **C1 (vision-block)**: Vision-capable models (Claude, GPT-4o, Gemini) require structured `{type: "image", ...}` ContentBlock to activate vision. There is **NO valid base64-as-text shortcut**. Establishes that the proper integration is the only path; "ship-now-as-text" was conceded as broken.

2. **V3 (caller break)**: `maho/crates/maho-acp/src/client.rs:719` — `client.submit_prompt(&sid, prompt_text)` — must be updated atomically when `submit_prompt` signature changes. Single git commit.

3. **V1 (refined migration gap)**: `maho-chromium/browser/ai/maho_opencode_acp_adapter.cc:355-363` — the deferred-submit drain path that reads `pending_message_` — must switch from `maho_acp_submit_prompt(...)` to `maho_acp_submit_prompt_parts(...)` in the same commit as the type migration. NOT a race (same sequence checker), but a migration-update gap.

4. **R5/§10.2 false claim**: The text "Mojo's `array<uint8>` uses a shared-buffer fast path for large arrays in modern Chromium" is uncited and unverified. **DELETE from plan text**. Plan correctness must not depend on it.

5. **R7/V2 vaporware**: `MAHO_FFI_VERSION` does not exist anywhere in `maho/crates/maho-ffi/`. **DELETE the mitigation** from plan §8 risk table. Static linking by `build_maho_core.py` produces atomic header+lib pairs; symbol mismatch is caught at link time.

6. **V7 (test gap)**: A test for the mixed file + page-context batch (user pastes screenshot + has `kCurrentPage` context) must exist. Tests `OnAttachmentSlotCompleted` produces `vector<PromptPart>` with correct ordering — no LLM required.

7. **C7 (no theater caps)**: 256 MB is meaningless given Mojo IPC limits + provider limits (~5–25 MB). Use a real provider-aligned cap of 25 MB at the page handler.

8. **A8 (forcing function for dual-write debt)**: Legacy `attach_browser_context: bool` removal must have a tracking issue + concrete trigger condition documented in `maho_ai.mojom` near the field declaration.

---

## 3 — Decisions (Converged Through Debate)

1. **Stay with push model** (Mojo → C-ABI → ACP → stdio). C2 (file:// + resource_link) was destroyed in cross-attack: WebUI renderer is sandboxed and CAN'T write files; would require new Mojo write-temp method, cleanup-on-crash, OpenCode read-access guarantee, TOCTOU race. Push model wins on engineering complexity comparison.

2. **Rust owns JSON serialization end-to-end**. Kill C++ `SerializePartsToAcpJson` AND Rust `parse_parts_json`. Pass typed slices via the C-ABI: an array of `FfiPromptPart` POD structs with `kind` enum + cstring pointers. Rust crate is the single source of truth for the wire schema.

3. **Ship Text + Image user-facing only** (Decision 3 + C3 refined). Rust `PromptContentPart` enum keeps all 4 variants as zero-cost arms (collapsing recreates bag-of-optionals per V/R critique). User-facing accept narrows to `image/png, image/jpeg, image/gif, image/webp`.

4. **Atomic delete of old FFI in same commit; no backward-compat shadows**. Delete `maho_acp_submit_prompt` C-ABI symbol AND any `submit_prompt_text` helper in the same commit that introduces the new entry point. Rollback insurance is `git revert`, not parallel APIs.

5. **Three-layer test coverage**:
   - 1 × C++ unit test for `FfiPromptPart` builder field-name assertions
   - 1 × WebUI unit test for `decomposeFileAttachment` data-URL edge cases
   - 1 × Rust integration test for end-to-end wire frame (mixed text + image + page-context)

6. **Squashed single commit, CI on merge only**. Plan v1's "7 commits with transient CI failures" was bisect-hostile (lands on a known-broken commit). One squashed commit; user's "single PR" preference preserved. Commit message body lists the logical phases.

7. **Feature flag at WebUI submit boundary**: `kMahoAiFileAttachments` (Chromium feature). Rollback = 1-line config flip. Resolves V5's rollback-vs-CI contradiction.

8. **Toast UX preserved (per user decision §10.1) but minimal**. ≤30 LOC component, reuse existing `--maho-ai-bg-glass` + `--maho-ai-shadow-glass` tokens, no new CSS file, no animation library.

9. **Page-handler-side OOM guard at 25 MB** (refined per C7). Implementation: `if (att->bytes.size() > 25 * 1024 * 1024) { mojo::ReportBadMessage("file attachment too large"); return; }`. Mojo IDL has no `[MaxMessageSize]` syntax — V's correction to S accepted.

10. **AugmentMessageWithMemory minimal guard** (A5 refined). Add `DCHECK(!parts.empty() && parts[0].kind == kText)` plus a 2-line invariant comment at the function entry. NOT a `std::variant` redesign — A6 was conceded since Chromium codebase historically avoids `std::variant`.

11. **A4 text-context resource-block migration tracked as separate ADR** (`.sisyphus/plans/text-context-resource-blocks-adr.md` — to be created post-PR). Rewriting the working text-context path risks regressing the 99% dominant case; track as architectural debt.

12. **Standalone dev server: keep + CI-blocking diff check**. User is actively using `http://127.0.0.1:4071/` for CSS iteration. Add `tools/check_standalone_mojom.sh` that regenerates production bindings, diffs against the hand-edited stub, fails CI on divergence.

13. **R4 transport cap raise**: Bump `maho/crates/maho-acp/src/transport.rs:8` `MAX_LINE_BYTES` from 64 KiB to 1 MiB as inbound safety. Outbound (`write_message`, lines 80-93) is unconstrained — verified by reading both code paths. The cap only matters for inbound responses (provider error JSON, `session/update` events). Add a unit test verifying a 512 KiB inbound line round-trips.

---

## 4 — Phase 0: Dead-Code Deletion (subtractive, fully self-contained)

### 4.1 `MahoChatOrchestrationService` cluster

| File | Action |
|---|---|
| `maho-chromium/browser/ai/maho_chat_orchestration_service.cc` | **Delete** |
| `maho-chromium/browser/ai/maho_chat_orchestration_service.h` | **Delete** |
| `maho-chromium/browser/BUILD.gn` (lines 86–107) | **Remove** entire `source_set("maho_chat_orchestration") { ... }` block |
| `maho-chromium/browser/ui/webui/maho_ai/BUILD.gn` line 48 | **Remove** `"//maho/browser:maho_chat_orchestration",` dep |

**Verification**: After delete, `grep -r "MahoChatOrchestrationService\|maho_chat_orchestration" maho-chromium/` returns zero matches. `python3 maho-chromium/build/scripts/build_maho.py` succeeds.

### 4.2 Adapter `bool attach_browser_context` parameter

| File | Action |
|---|---|
| `maho-chromium/browser/ai/maho_ai_runtime_adapter.h` | Remove `bool attach_browser_context` from abstract `SubmitMessage(...)` declaration |
| `maho-chromium/browser/ai/maho_opencode_acp_adapter.h` line 44 | Drop parameter from override |
| `maho-chromium/browser/ai/maho_opencode_acp_adapter.cc` line 59 | Drop parameter; remove DVLOG that printed it |
| `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc` (call sites at lines 926, 981, 1051, 1119, 1205) | Drop the bool argument from each `adapter->SubmitMessage(...)` call |

**NOTE**: This phase intentionally does NOT touch the mojom `SubmitPrompt(..., bool attach_browser_context, ...)` field (line 385) or the `if (attach_browser_context)` page handler branch (line 974). Per Hard Constraint 8, add this comment near the mojom field:

```mojom
// DEPRECATED (2026-05-31): Remove when WebUI always sends non-null `attachments`.
// Tracking issue: <create on PR landing>. Trigger condition: zero call sites
// of SubmitPrompt with `attach_browser_context: true` in 30 days of telemetry.
bool attach_browser_context;
```

### 4.3 Acceptance criteria

- [ ] `maho_chat_orchestration_service.{cc,h}` deleted
- [ ] GN target removed from `browser/BUILD.gn`; dep edge removed from `webui/maho_ai/BUILD.gn:48`
- [ ] `bool attach_browser_context` removed from `MahoAiRuntimeAdapter::SubmitMessage` interface
- [ ] All 5+ call sites in `maho_ai_page_handler.cc` updated
- [ ] Forcing-function comment added to mojom field at line 385
- [ ] `python3 maho-chromium/build/scripts/build_maho.py` succeeds
- [ ] `cd maho && bun run typecheck` succeeds

---

## 5 — Phase 1: Transport Safety + Mojom Extension

These are independent and can land in parallel.

### 5.1 Transport line cap raise

**File**: `maho/crates/maho-acp/src/transport.rs`

| Line | Change |
|---|---|
| 7-8 | Update doc + constant: `const MAX_LINE_BYTES: usize = 1024 * 1024;` (1 MiB) |
| 12 | Update doc string: `rejects inbound lines > 1 MiB` |
| 174 (in test) | Update `oversized_line_rejected` test fixture to use `MAX_LINE_BYTES + 1` (still works since constant is referenced) |

**New test** — add inside `#[cfg(test)] mod tests`:

```rust
#[tokio::test]
async fn large_inbound_line_under_cap_succeeds() {
    let payload = format!("{{\"x\":\"{}\"}}", "a".repeat(512 * 1024));
    let mut data = payload.clone().into_bytes();
    data.push(b'\n');
    let results = read_lines(&data).await;
    assert_eq!(results.len(), 2);
    assert_eq!(results[0].as_ref().unwrap().as_deref(), Some(payload.as_str()));
}
```

**Acceptance**:
- [ ] `cd maho && cargo test -p maho-acp transport` passes
- [ ] Existing `oversized_line_rejected` test still rejects (now at 1 MiB + 1)
- [ ] No other `MAX_LINE_BYTES` consumer broken (grep `MAX_LINE_BYTES` in workspace)

### 5.2 Mojom `ContextAttachment` extension

**File**: `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`

Edit at line 64:

```mojom
struct ContextAttachment {
  ContextSourceKind kind;
  int32 tab_id;
  string label;
  string url;

  // Phase C: file attachments (kind == kFile). Empty for non-file kinds.
  string mime_type;
  array<uint8> bytes;
};
```

> **Note**: `text_content` field from v1 is removed. Decision 3 ships images-only; text/* attachment support is deferred. When PDF or text-file support arrives, add the field then.

### 5.3 Standalone Mojo stub sync

**File**: `maho-chromium/browser/resources/maho_ai/standalone/maho_ai.mojom-webui.js`

Mirror the new `mime_type` and `bytes` fields with empty defaults so standalone preview keeps typechecking.

### 5.4 CI diff check

**New file**: `maho-chromium/browser/resources/maho_ai/standalone/check_standalone_mojom.sh`

```bash
#!/usr/bin/env bash
# Fails CI if standalone/maho_ai.mojom-webui.js diverges from the regenerated production bindings.
set -euo pipefail
cd "$(dirname "$0")"

PROD_DTS="../maho_ai.mojom-webui.js.d.ts"
STANDALONE_JS="./maho_ai.mojom-webui.js"

# Extract field names from each. Naive grep is sufficient for the diff signal.
grep -oE '(mime_type|bytes|text_content)' "$PROD_DTS" | sort -u > /tmp/prod_fields.txt
grep -oE '(mime_type|bytes|text_content|mimeType)' "$STANDALONE_JS" | sort -u > /tmp/stub_fields.txt

if ! diff -q /tmp/prod_fields.txt /tmp/stub_fields.txt > /dev/null; then
  echo "::error::standalone Mojo stub drift detected. Sync $STANDALONE_JS with $PROD_DTS."
  diff /tmp/prod_fields.txt /tmp/stub_fields.txt || true
  exit 1
fi
```

Make executable; wire into the CI job that runs Mojo regen.

**Acceptance**:
- [ ] Mojom regen produces updated `maho_ai.mojom-webui.js.d.ts`
- [ ] Standalone stub manually synced
- [ ] `bash check_standalone_mojom.sh` passes
- [ ] WebUI typecheck passes: `bunx tsc --noEmit -p maho-chromium/browser/resources/maho_ai/react/tsconfig.json --ignoreDeprecations 6.0`

---

## 6 — Phase 2: Rust ACP Wire Layer

Three coordinated changes in `maho/crates/maho-acp/`.

### 6.1 File: `maho-acp/src/turn.rs`

Replace lines 6-27:

```rust
#[derive(Debug, Clone)]
pub enum PromptContentPart {
    Text { text: String },
    Image {
        mime_type: String,
        data_base64: String,    // raw base64 (NOT data URL)
    },
    // Reserved for future scope (PDF/text files); not yet wired through FFI.
    ResourceBlob { uri: String, mime_type: String, blob_base64: String },
    ResourceText { uri: String, mime_type: String, text: String },
}

impl PromptContentPart {
    fn to_json(&self) -> serde_json::Value {
        use serde_json::json;
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
    pub fn to_value(&self) -> serde_json::Value {
        serde_json::json!({
            "sessionId": self.session_id,
            "prompt": self.prompt.iter().map(PromptContentPart::to_json).collect::<Vec<_>>(),
        })
    }
}
```

**Inline tests** (`#[cfg(test)] mod tests` at end of file):

```rust
#[test]
fn text_part_serializes() {
    let p = PromptContentPart::Text { text: "hi".into() };
    assert_eq!(p.to_json(), json!({"type": "text", "text": "hi"}));
}

#[test]
fn image_part_serializes() {
    let p = PromptContentPart::Image {
        mime_type: "image/png".into(),
        data_base64: "iVBOR".into(),
    };
    assert_eq!(p.to_json(), json!({
        "type": "image", "mimeType": "image/png", "data": "iVBOR"
    }));
}

#[test]
fn mixed_prompt_params_serializes() {
    let params = PromptParams {
        session_id: "s1".into(),
        prompt: vec![
            PromptContentPart::Text { text: "describe".into() },
            PromptContentPart::Image {
                mime_type: "image/png".into(),
                data_base64: "iVBOR".into(),
            },
        ],
    };
    let v = params.to_value();
    assert_eq!(v["sessionId"], "s1");
    assert_eq!(v["prompt"][0]["type"], "text");
    assert_eq!(v["prompt"][1]["type"], "image");
    assert_eq!(v["prompt"][1]["mimeType"], "image/png");
}
```

### 6.2 File: `maho-acp/src/lib.rs`

Add public re-export:

```rust
pub use turn::PromptContentPart;
```

### 6.3 File: `maho-acp/src/client.rs`

Edit line 256:

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
    // ... (rest unchanged)
}
```

**No `submit_prompt_text` helper** (Decision 4). The existing caller at line 719 will be updated in Phase 3.

**Acceptance**:
- [ ] `cd maho && cargo test -p maho-acp` passes (all turn.rs tests + existing tests)
- [ ] `cd maho && cargo clippy -p maho-acp -- -D warnings` clean

---

## 7 — Phase 3: maho-ffi C-ABI Migration

Atomic rename: replace `maho_acp_submit_prompt(handle, sid, text)` with `maho_acp_submit_prompt_parts(handle, sid, parts_ptr, parts_len)`. No backward-compat helper.

### 7.1 File: `maho/crates/maho-ffi/src/acp.rs`

Replace the existing `maho_acp_submit_prompt` (lines 677–826) with:

```rust
/// One element of the parts array passed to maho_acp_submit_prompt_parts.
/// All cstring fields must be valid NUL-terminated UTF-8.
#[repr(C)]
pub struct FfiPromptPart {
    /// 0 = text, 1 = image, 2 = resource_blob, 3 = resource_text
    pub kind: u32,
    /// For Text: the prompt text.
    /// For Image: ignored.
    /// For ResourceText: the text payload.
    /// For ResourceBlob: ignored.
    pub text: *const c_char,
    /// For Image / Resource*: the MIME type.
    pub mime_type: *const c_char,
    /// For Image: raw base64.
    /// For ResourceBlob: raw base64 of the blob.
    pub data_base64: *const c_char,
    /// For Resource*: the URI (typically `file://...`).
    pub uri: *const c_char,
}

/// Submit a multi-part prompt. Each FfiPromptPart's strings are copied;
/// the caller may free them after this returns.
///
/// # Safety
/// `parts` must point to an array of at least `parts_len` valid FfiPromptPart
/// structs. All non-null cstring fields must be NUL-terminated UTF-8.
#[no_mangle]
pub unsafe extern "C" fn maho_acp_submit_prompt_parts(
    handle: *mut MahoAcpHandle,
    session_id: *const c_char,
    parts: *const FfiPromptPart,
    parts_len: usize,
) -> i32 {
    let Some(handle) = handle.as_mut() else { return -1; };
    let Ok(sid) = c_str_to_string(session_id) else { return -2; };

    if parts.is_null() && parts_len > 0 { return -3; }
    let slice = std::slice::from_raw_parts(parts, parts_len);

    let mut converted: Vec<PromptContentPart> = Vec::with_capacity(parts_len);
    for (i, p) in slice.iter().enumerate() {
        match p.kind {
            0 => {
                let Ok(t) = c_str_to_string(p.text) else { return -100 - (i as i32); };
                converted.push(PromptContentPart::Text { text: t });
            }
            1 => {
                let Ok(mt) = c_str_to_string(p.mime_type) else { return -200 - (i as i32); };
                let Ok(d) = c_str_to_string(p.data_base64) else { return -300 - (i as i32); };
                converted.push(PromptContentPart::Image {
                    mime_type: mt,
                    data_base64: d,
                });
            }
            // 2 (ResourceBlob) and 3 (ResourceText) reserved; not user-facing in this PR.
            // Returning -4 forces a build break if C++ accidentally emits these.
            other => {
                tracing::warn!(target = "maho-ffi", "unknown PromptPart kind: {}", other);
                return -4;
            }
        }
    }

    handle.runtime.spawn({
        let client = handle.client.clone();
        async move {
            let _ = client.submit_prompt(&sid, converted).await;
        }
    });
    0
}
```

### 7.2 Update existing caller at `maho-ffi/src/acp.rs:719`

The previous `maho_acp_submit_prompt` body called `client.submit_prompt(&sid, prompt_text).await`. With the rename:
- The OLD `maho_acp_submit_prompt(handle, sid, text)` C-ABI is **deleted** (per Decision 4).
- `acp.rs:719` is **part of the deleted function** — the entire `maho_acp_submit_prompt` symbol goes away. There is no separate caller to update; the new `maho_acp_submit_prompt_parts` body above is its replacement.

> **NOTE on Hard Constraint 2 (V3)**: V3 was framed as "rename breaks acp.rs:719" in the bundle. In v2, since we're DELETING the old symbol entirely (not renaming the Rust method), `acp.rs:719` ceases to exist. The `client.submit_prompt(...)` call that V3 referenced lives inside the deleted function. The constraint is automatically satisfied by the atomic delete.

### 7.3 File: `maho-chromium/third_party/maho/maho_ffi.h`

Replace the existing `maho_acp_submit_prompt` declaration with:

```c
typedef struct FfiPromptPart {
    uint32_t kind;        // 0=text, 1=image, 2=resource_blob, 3=resource_text
    const char* text;
    const char* mime_type;
    const char* data_base64;
    const char* uri;
} FfiPromptPart;

// Submit a multi-part prompt.
// `parts` may be NULL iff `parts_len` == 0.
// Returns 0 on success, negative on error.
int32_t maho_acp_submit_prompt_parts(
    MahoAcpHandle* handle,
    const char* session_id,
    const FfiPromptPart* parts,
    size_t parts_len);
```

### 7.4 Rebuild prebuilt

```bash
python3 maho-chromium/build/scripts/build_maho_core.py
```

Verify symbol present:
```bash
nm maho-chromium/third_party/maho/lib/libmaho_ffi.a | grep maho_acp_submit_prompt_parts
```

Verify old symbol gone:
```bash
nm maho-chromium/third_party/maho/lib/libmaho_ffi.a | grep -c '\bmaho_acp_submit_prompt\b'
# Expected: 0 (only maho_acp_submit_prompt_parts should match, with a longer name)
```

### 7.5 FFI integration test

**File**: `maho/crates/maho-ffi/tests/acp_ffi.rs` — add:

```rust
#[test]
fn submit_prompt_parts_text_image_roundtrip() {
    use std::ffi::CString;
    // ... build FfiPromptPart array with one text + one image part ...
    // ... assert maho_acp_submit_prompt_parts returns 0 ...
    // ... assert the JSON-RPC payload sent on the wire matches expected shape ...
}
```

**Acceptance**:
- [ ] Symbol present in libmaho_ffi.a; old symbol absent
- [ ] FFI test passes
- [ ] `cargo clippy -p maho-ffi -- -D warnings` clean

---

## 8 — Phase 4: C++ Adapter & Page Handler

### 8.1 File: `maho-chromium/browser/ai/maho_ai_runtime_adapter.h`

Replace `SubmitMessage(...)` with `SubmitPrompt(...)`:

```cpp
class MahoAiRuntimeAdapter {
 public:
  struct PromptPart {
    enum class Kind { kText, kImage };  // kResourceBlob, kResourceText reserved
    Kind kind;
    std::string text;          // Text content (only for kText)
    std::string mime_type;     // Image only
    std::string data_base64;   // Image only (raw base64)
  };

  // Replaces SubmitMessage. The first part is typically the user's text;
  // subsequent parts are image attachments.
  virtual void SubmitPrompt(std::vector<PromptPart> parts,
                            maho_ai::mojom::ChatIntent chat_intent,
                            maho_ai::mojom::InteractionMode mode,
                            RuntimeEventCallback on_event) = 0;
  // ... other methods unchanged ...
};
```

### 8.2 File: `maho-chromium/browser/ai/maho_opencode_acp_adapter.{h,cc}`

**Header**: Update override to match new interface.

**Implementation**: Replace the body of `SubmitMessage` (cc:59-110) and the deferred drain at cc:355-363 with:

```cpp
void MahoOpenCodeAcpAdapter::SubmitPrompt(
    std::vector<PromptPart> parts,
    maho_ai::mojom::ChatIntent chat_intent,
    maho_ai::mojom::InteractionMode mode,
    RuntimeEventCallback on_event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Augment the first text part with memory.
  DCHECK(!parts.empty() && parts[0].kind == PromptPart::Kind::kText)
      << "First part must be the user text; see invariant in page handler.";
  parts[0].text = AugmentMessageWithMemory(parts[0].text);

  current_on_event_ = std::move(on_event);

  if (state_ == State::kReady) {
    SubmitPartsToFfi(parts);
    state_ = State::kSubmitting;
    return;
  }

  // Deferred: stash the parts for drain when session becomes ready.
  pending_parts_ = std::move(parts);
  // ... existing state-machine bookkeeping unchanged ...
}

// New helper. Replaces the old maho_acp_submit_prompt() call.
void MahoOpenCodeAcpAdapter::SubmitPartsToFfi(
    const std::vector<PromptPart>& parts) {
  std::vector<std::string> owned_strings;
  owned_strings.reserve(parts.size() * 4);
  std::vector<FfiPromptPart> ffi_parts;
  ffi_parts.reserve(parts.size());

  for (const auto& p : parts) {
    FfiPromptPart fp{};
    switch (p.kind) {
      case PromptPart::Kind::kText:
        fp.kind = 0;
        owned_strings.push_back(p.text);
        fp.text = owned_strings.back().c_str();
        break;
      case PromptPart::Kind::kImage:
        fp.kind = 1;
        owned_strings.push_back(p.mime_type);
        fp.mime_type = owned_strings.back().c_str();
        owned_strings.push_back(p.data_base64);
        fp.data_base64 = owned_strings.back().c_str();
        break;
    }
    ffi_parts.push_back(fp);
  }

  int32_t rc = maho_acp_submit_prompt_parts(
      acp_handle_, session_id_.c_str(),
      ffi_parts.data(), ffi_parts.size());
  if (rc != 0) {
    TransitionToError(base::StringPrintf("submit_prompt_parts rc=%d", rc));
  }
}
```

**Critical**: change `pending_message_` field from `std::string` to `std::vector<PromptPart> pending_parts_`. The drain path (formerly cc:355-363) calls `SubmitPartsToFfi(pending_parts_)` instead of the old text-only FFI. **This satisfies Hard Constraint 3 (V1 migration gap)**.

### 8.3 File: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.{h,cc}`

**Header**: Update `AttachmentSlot`:

```cpp
struct AttachmentSlot {
  maho_ai::mojom::ContextSourceKind kind;
  std::string label;
  std::string url;
  std::optional<MahoAiPageContext> page_context;  // for kCurrentPage/kOpenTab/...
  // For kFile:
  std::string file_mime_type;
  std::vector<uint8_t> file_bytes;
};
```

**Implementation** at `maho_ai_page_handler.cc:910-969` (the two switch blocks for `ContextSourceKind`):

```cpp
// Block 1: slot construction
case maho_ai::mojom::ContextSourceKind::kFile: {
  // Hard Constraint 7 (C7): 25 MB sanity guard at trust boundary.
  constexpr size_t kMaxFileBytes = 25 * 1024 * 1024;
  if (att->bytes.size() > kMaxFileBytes) {
    mojo::ReportBadMessage("attachment exceeds 25 MB cap");
    return;
  }
  // Decision 3: only image/* MIME types accepted (WebUI also enforces).
  if (!base::StartsWith(att->mime_type, "image/")) {
    mojo::ReportBadMessage("attachment mime not image/*");
    return;
  }
  slot.kind = maho_ai::mojom::ContextSourceKind::kFile;
  slot.label = att->label;
  slot.url = att->url;
  slot.file_mime_type = att->mime_type;
  slot.file_bytes = std::move(att->bytes);
  break;
}

// Block 2: dispatch — kFile is synchronous (no extraction needed)
case maho_ai::mojom::ContextSourceKind::kFile:
  pending_->slots[slot_index].state = SlotState::kCompleted;
  break;
```

**Implementation** at `maho_ai_page_handler.cc:1126-1210` (`OnAttachmentSlotCompleted`) — refactor to build `vector<PromptPart>`:

```cpp
void MahoAIPageHandler::OnAttachmentSlotCompleted(/* ... */) {
  // ... existing slot bookkeeping unchanged ...
  if (!AllSlotsResolved()) return;

  std::vector<MahoAiRuntimeAdapter::PromptPart> parts;
  std::string text_prefix;

  for (const auto& slot : pending_->slots) {
    using Kind = maho_ai::mojom::ContextSourceKind;
    switch (slot.kind) {
      case Kind::kCurrentPage:
      case Kind::kOpenTab:
      case Kind::kHistory:
      case Kind::kBookmark:
        text_prefix += FormatPageContextSlot(slot);
        break;
      case Kind::kFile:
        parts.push_back({
          MahoAiRuntimeAdapter::PromptPart::Kind::kImage,
          /*text=*/{},
          slot.file_mime_type,
          base::Base64Encode(slot.file_bytes),
        });
        break;
    }
  }

  std::string final_text = text_prefix.empty()
      ? pending_->user_prompt
      : text_prefix + "\n\nUser Question:\n" + pending_->user_prompt;
  parts.insert(parts.begin(), {
    MahoAiRuntimeAdapter::PromptPart::Kind::kText,
    std::move(final_text), {}, {},
  });

  adapter_->SubmitPrompt(std::move(parts), pending_->intent, pending_->mode,
                         std::move(pending_->on_event));
  pending_.reset();
}
```

Update all 5 existing `adapter->SubmitMessage(...)` call sites to wrap their string in a single-element `vector<PromptPart>{{kText, text, {}, {}}}` and call `SubmitPrompt`.

### 8.4 Feature flag

**File**: `maho-chromium/browser/maho_features.{cc,h}` — add:

```cpp
// In .h:
BASE_DECLARE_FEATURE(kMahoAiFileAttachments);

// In .cc:
BASE_FEATURE(kMahoAiFileAttachments,
             "MahoAiFileAttachments",
             base::FEATURE_DISABLED_BY_DEFAULT);
```

Gate at the WebUI submit boundary in `maho_ai_page_handler.cc::SubmitPrompt`:

```cpp
if (attachments.has_value() && !attachments->empty()) {
  bool has_file = std::any_of(
      attachments->begin(), attachments->end(),
      [](const auto& a) { return a->kind == maho_ai::mojom::ContextSourceKind::kFile; });
  if (has_file && !base::FeatureList::IsEnabled(maho::kMahoAiFileAttachments)) {
    mojo::ReportBadMessage("file attachments disabled by feature flag");
    return;
  }
}
```

Default: disabled. Enable via `--enable-features=MahoAiFileAttachments` for testing.

### 8.5 Acceptance

- [ ] `MahoAiRuntimeAdapter::SubmitPrompt` (vector<PromptPart>) replaces SubmitMessage
- [ ] `MahoOpenCodeAcpAdapter::pending_parts_` replaces `pending_message_`; drain path uses new FFI
- [ ] Page handler `kFile` switch arms added; 25 MB guard active
- [ ] All `adapter->SubmitMessage(...)` call sites migrated
- [ ] Feature flag `kMahoAiFileAttachments` declared + gated
- [ ] `python3 maho-chromium/build/scripts/build_maho.py` succeeds

---

## 9 — Phase 5: WebUI

### 9.1 File: `maho-chromium/browser/resources/maho_ai/types.ts`

Edit `getSubmitContextAttachments` (line 294) — add file branch:

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
    });
  }

  // ... existing tab/history/bookmark pushes (each adds the new fields as empty) ...

  for (const attachment of fileAttachments) {
    submitAttachments.push({
      kind: ContextSourceKind.kFile,
      tabId: -1,
      label: attachment.label,
      url: attachment.url || '',
      mimeType: attachment.mimeType || 'application/octet-stream',
      bytes: dataUrlToByteArray(attachment.dataUrl ?? ''),
    });
  }

  return submitAttachments;
}

function dataUrlToByteArray(dataUrl: string): number[] {
  if (!dataUrl) return [];
  const commaIdx = dataUrl.indexOf(',');
  if (commaIdx < 0) return [];
  const base64 = dataUrl.slice(commaIdx + 1);
  const bin = atob(base64);
  const bytes = new Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}
```

### 9.2 File: `maho-chromium/browser/resources/maho_ai/react/features/compact/composer.tsx`

Narrow accept attribute (per Decision 3 + Hard Constraint scope) at line 442:

```tsx
accept="image/png,image/jpeg,image/gif,image/webp"
```

Drop `application/pdf, text/*, .md, .json, .csv, .txt`.

Add SVG defense-in-depth in `onAttachFiles` handler (still required per v1 §10.4 — SVG could appear via wildcard `image/*` browsers, but our explicit list excludes it). Verify in app.tsx where `onAttachFiles` is wired.

### 9.3 File: `maho-chromium/browser/resources/maho_ai/types.ts:createFileAttachment`

Reject `image/svg+xml` defensively:

```typescript
export async function createFileAttachment(file: File): Promise<ComposerAttachment> {
  if (file.type === 'image/svg+xml' || file.name.toLowerCase().endsWith('.svg')) {
    throw new TypeError('SVG attachments are not supported. Convert to PNG first.');
  }
  // ... existing implementation ...
}
```

Caller (`onAttachFiles` in `app.tsx`) catches the TypeError and dispatches a toast + thread error event.

### 9.4 Toast component (≤30 LOC budget)

**New file**: `maho-chromium/browser/resources/maho_ai/react/features/compact/toast.tsx`

```tsx
import {useEffect} from 'react';

export interface ToastProps {
  text: string;
  tone?: 'danger' | 'info';
  onDismiss: () => void;
}

export function Toast({text, tone = 'info', onDismiss}: ToastProps) {
  useEffect(() => {
    const t = setTimeout(onDismiss, 4000);
    return () => clearTimeout(t);
  }, [onDismiss]);

  return (
    <div className="compact-toast" data-tone={tone} role="status" aria-live="polite">
      <span>{text}</span>
      <button aria-label="Dismiss" onClick={onDismiss}>×</button>
    </div>
  );
}
```

**Add CSS** in `styles/compact-panel.css` (no new file):

```css
.compact-toast {
  position: fixed;
  top: var(--maho-ai-space-3);
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: var(--maho-ai-space-2);
  padding: var(--maho-ai-space-2) var(--maho-ai-space-3);
  border-radius: var(--maho-ai-radius-control);
  background: var(--maho-ai-bg-glass);
  color: var(--maho-ai-text-primary);
  box-shadow: var(--maho-ai-shadow-glass);
  backdrop-filter: blur(var(--maho-ai-blur-surface));
  z-index: 100;
}
.compact-toast[data-tone='danger'] { color: var(--maho-ai-danger-fg); }
@media (prefers-reduced-motion: reduce) { .compact-toast { transition: none; } }
```

### 9.5 Vision-rejection error handling

**File**: `store.ts` — when an `error` event arrives matching vision-rejection patterns:

```typescript
if (event.kind === RuntimeEventKind.kError) {
  const text = event.text?.toLowerCase() ?? '';
  if (text.includes('image') &&
      (text.includes('unsupported') || text.includes('not supported') || text.includes('vision'))) {
    dispatch({type: 'addToast', tone: 'danger',
      text: "This model doesn't support images."});
    // Existing thread system event still fires.
  }
}
```

Pattern matching is conservative — false negatives tolerable (thread error still surfaces).

### 9.6 Acceptance

- [ ] `getSubmitContextAttachments` includes file attachments with bytes populated
- [ ] WebUI accept attribute narrowed to image MIMEs
- [ ] SVG rejected at `createFileAttachment` + paste handler
- [ ] Toast component ≤30 LOC; reuses tokens; no new CSS file
- [ ] `bunx tsc --noEmit -p maho-chromium/browser/resources/maho_ai/react/tsconfig.json --ignoreDeprecations 6.0` passes
- [ ] WebUI unit test for `dataUrlToByteArray` edge cases (empty, no comma, valid)

---

## 10 — Phase 6: Build, Test, Verify

### 10.1 Sequential build (acquires `.build.lock`)

```bash
# 1. Rust prebuilt (regen libmaho_ffi.a with new symbol)
python3 maho-chromium/build/scripts/build_maho_core.py

# 2. Chromium full build (regen mojo + compile C++)
python3 maho-chromium/build/scripts/build_maho.py

# 3. WebUI typecheck
cd maho-chromium/browser/resources/maho_ai
bunx tsc --noEmit -p react/tsconfig.json --ignoreDeprecations 6.0
```

### 10.2 Test matrix (3 layers — Decision 5)

| Layer | File | Asserts |
|---|---|---|
| C++ unit | `maho-chromium/browser/ai/maho_opencode_acp_adapter_unittest.cc` | `SubmitPartsToFfi` populates `FfiPromptPart` with correct `kind`, `mime_type`, `data_base64`, no field-name typos |
| WebUI unit | `maho-chromium/browser/resources/maho_ai/types_test.ts` | `dataUrlToByteArray` handles: valid PNG data URL, empty string, missing comma. `getSubmitContextAttachments` includes file when `attachments` has only files. SVG rejected. |
| Rust integration | `maho/crates/maho-ffi/tests/acp_ffi.rs` | End-to-end: build `FfiPromptPart` for {text, image, page-context (text)} → call `maho_acp_submit_prompt_parts` → assert wire-level JSON-RPC payload matches `{"sessionId":..., "prompt":[{"type":"text",...}, {"type":"image","mimeType":"image/png","data":"..."}]}` |

### 10.3 Manual end-to-end QA matrix

Build and launch `out/Default/Maho` with `--enable-features=MahoAiFileAttachments`.

| # | Action | Expected |
|---|---|---|
| 1 | Cmd+V paste a 100 KB PNG, ask "What's in this image?" | Model responds describing image content |
| 2 | Cmd+V paste a 5 MB JPEG, ask question | Model responds (provider may take longer); no truncation |
| 3 | Click "Upload from computer", pick a 30 MB image | Page handler rejects with `mojo::ReportBadMessage` (25 MB cap) |
| 4 | Try to upload `.svg` via picker | Toast: "SVG files aren't supported"; no submission |
| 5 | Paste image + have current page context attached | Both reach the model (text prefix + image part) |
| 6 | Disable feature flag, paste image, send | Submission rejected; user sees toast |
| 7 | Use a non-vision model (e.g., GPT-4o-mini if available) | Toast + thread error: "This model doesn't support images" |
| 8 | Plain text prompt (no attachment) | Existing behavior unchanged (legacy `attach_browser_context: bool` path still works) |

### 10.4 Final acceptance

- [ ] Phase 0 (dead code) acceptance criteria met
- [ ] Phase 1 (transport + Mojom) acceptance criteria met
- [ ] Phase 2 (Rust ACP) acceptance criteria met
- [ ] Phase 3 (FFI) acceptance criteria met
- [ ] Phase 4 (C++) acceptance criteria met
- [ ] Phase 5 (WebUI) acceptance criteria met
- [ ] All 8 manual QA scenarios pass
- [ ] No regression in plain-text prompt path
- [ ] Standalone dev server CI check passes

---

## 11 — Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| OpenCode model lacks vision capability | Medium | User confusion | Toast + thread error (§9.5) |
| `MAX_LINE_BYTES` raise breaks downstream consumers | Low | Wire failure | Phase 1 grep verifies no other consumer relies on 64 KiB cap; new test verifies 512 KiB inbound roundtrips |
| C++ `FfiPromptPart` cstring lifetime bug (use-after-free) | Medium | Crash | `SubmitPartsToFfi` keeps `owned_strings` vector alive across the FFI call; explicit ownership in unit test |
| Feature flag default-off means feature ships invisibly | Low | UX never reaches users | Decision: flag flipped on by team in canary 7 days post-merge; default-on after 30 days |
| Standalone dev server drift | Medium | Standalone "lies" to developers | `check_standalone_mojom.sh` CI script (§5.4) blocks divergence |
| Renderer OOM from extreme attachment | Low (with 25 MB cap) | Tab crash | Page handler 25 MB guard rejects via `mojo::ReportBadMessage` |
| `mojo::ReportBadMessage` kills the renderer (intended Mojo behavior) | Low | Bad UX if WebUI sends garbage | Keep WebUI accept attribute strict; rejection is a programming-error signal not a normal flow |
| Legacy `attach_browser_context` accumulates dual-write debt | Medium | Future bugs | Forcing-function comment in mojom + tracking issue at PR landing |

---

## 12 — Adversarial Provenance

| Member | Findings raised | Conceded | Refined | Defended | Survivors |
|---|---|---|---|---|---|
| Skeptic (unspecified-low) | 7 | 0 | 6 | 1 (S7) | 7 |
| Validator (unspecified-high) | 7 | 1 (V4) | 5 | 1 (V3) | 6 |
| Researcher (deep) | 10 | 0 | 1 (R5) | 9 | 10* |
| Architect (ultrabrain) | 8 | 2 (A2, A6) | 4 | 2 (A3, A8) | 6 |
| Creative (artistry) | 7 | 4 (C1, C4, C5, C6) | 2 | 1 (C7) | 3 |
| **Total** | **39** | **7** | **18** | **14** | **32** |

\* Researcher's R4 ("FATAL 64 KiB cap") was the largest finding — survived adversarial review unanimously, but **plan agent's investigation refuted it** by tracing both transport code paths. R4's "FATAL" framing is RESCINDED in v2; the cap is inbound-only and `write_message` has zero size constraint.

### Notable destroyed reframes

- **C1 (base64-as-text shortcut)**: Vision models reject; only structured ContentBlock activates vision.
- **C2 (file:// + resource_link)**: Sandbox + lifecycle + TOCTOU + cross-platform paths add MORE complexity.
- **C5 (right-click → Ask Maho)**: Different feature, scope creep.
- **C6 (native paste reinvented)**: WebUI textarea native paste gives `text/plain` only; JS Clipboard API is mandatory.

### Notable destroyed elaborations

- **A2 (Mojo unions)**: Forward-compatibility; struct-with-optionals is established pattern.
- **A6 (std::variant)**: Chromium codebase historically avoids `std::variant`; tagged struct + DCHECK is idiomatic.

---

## 13 — Phase Sequencing

```
Phase 0  ─┬─ subtractive, fully independent
          │
Phase 1  ─┴─ transport.rs + Mojom + standalone stub + CI script (parallel-able)
          │
Phase 2  ─── Rust ACP wire (depends on Phase 1 mojom — types.ts uses new fields)
          │
Phase 3  ─── maho-ffi C-ABI (depends on Phase 2 PromptContentPart)
          │     └─ build_maho_core.py rebuilds prebuilt
          │
Phase 4  ─── C++ adapter + page handler + feature flag (depends on Phase 3 symbol)
          │
Phase 5  ─── WebUI types.ts + composer accept + toast (depends on Phase 1 mojom)
          │
Phase 6  ─── Build + test + manual QA (depends on all above)
```

Squashed into one commit per Decision 6.

**Estimated time**: 5 dev-hours pure work + 30–90 min Chromium full rebuild.

---

## 14 — Rollback

- Feature flag (Decision 7): `--enable-features=MahoAiFileAttachments` off → file attachment path silently skipped at WebUI submit boundary; UI chips still render but `getSubmitContextAttachments` excludes files when flag off.
- Hard rollback: `git revert <single-commit-sha>` reverses everything including dead code re-added (acceptable cost given Phase 0 is small).

---

**End of v2 plan.**
