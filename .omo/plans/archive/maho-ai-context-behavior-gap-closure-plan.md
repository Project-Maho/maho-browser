# Maho AI: Context Behavior Gap Closure Plan

**Priority:** Behavior and plumbing first. No decorative UI.

## Goal

Close the biggest remaining gap between Maho AI and Dia's browser-native intelligence: structured browser context flowing into the runtime. The runtime should see what the user is looking at without manual copying. Build this with invisible plumbing, keeping the composer surface minimal. No surfaces that don't earn their space.

## Why This Is Next

1. The AI panel already works for bare prompt/response flows (session lifecycle, tool calls, approvals all wired).
2. The single largest Dia differentiator isn't UI chrome; it's that the model sees page content, selection, and navigation state without the user manually copying anything.
3. `attachBrowserContext: boolean` exists in composer state and is already wired through to the adapter layer. The plumbing gap is narrower than originally estimated.
4. Building context plumbing before UI prevents the "helper chip tray" trap where we ship decorative surfaces that aren't backed by real data flow.

## Non-Goals / Anti-Culprit Guardrails

These are explicitly NOT part of this wave:

- **Persistent context chip trays** or always-visible attachment indicators in the composer footer.
- **Helper cards** showing "what Maho sees" in the timeline.
- **Status bars** or connection state decorations beyond what already exists.
- **Multi-modal context** (screenshots, canvas captures). Text and metadata only for this phase.
- **New WebUI surfaces** or panels. Everything routes through the existing `chrome://maho-ai` panel and its Mojo interface.
- **Prompt template UI** (slash commands, mode selectors beyond the existing `modeLabel`).

If any workstream produces code that adds always-visible UI elements not required for plumbing validation, it should be rejected at review.

## Current Architecture (Confirmed)

### Adapter-Owned Extraction at Submit Time

The chosen architecture is **browser-side, adapter-owned**. TS does not fetch or assemble context. The flow:

```
User clicks Submit
  → store.submitPrompt(sessionId, prompt, attachBrowserContext)
    → pageHandler.SubmitPrompt(sessionId, prompt, attachBrowserContext)  [Mojo]
      → adapter->SubmitMessage(prompt, mode, attach_browser_context, callback)
        [adapter internally calls extractor if attach_browser_context=true]
        [adapter formats extracted context + prompt for the LLM backend]
```

Key facts:
- **TS sends state/intent only.** The boolean `attachBrowserContext` is a preference signal, not a payload. TS never touches page content.
- **Page handler stays thin.** It forwards the boolean to the adapter without interpreting it.
- **Extraction lives in `maho_ai_page_context_extractor.cc`.** JS injection into the active tab's WebContents pulls URL, title, selected text, headings, links, and body text.
- **The adapter owns the decision** to extract, format, and inject context into the prompt before forwarding to the OpenCode runtime service.

### Extractor Capabilities (Already Implemented)

- **Truncation:** 50,000 chars for body text (`kMaxMainTextLength`), 50 headings (`kMaxHeadings`), 20 links (`kMaxLinks`). JS does the primary slice; C++ enforces caps post-return.
- **Deny-list:** `chrome://`, `chrome-untrusted://`, `devtools://`, `about://` schemes are blocked before any JS runs. Extractor returns `kCannotAccess` immediately.
- **Session-start behavior:** `StartSession` with an `initial_prompt` hardcodes `attach_browser_context=true`, always attaching context on the first prompt regardless of toggle state.

### TS Store (`store.ts`)

```ts
await this.pageHandler.submitPrompt(sessionId, prompt, attachBrowserContext);
```

TS passes the boolean from `state.composer.attachBrowserContext`. That's the entire TS responsibility for context.

### Separate `GetPageContent` Method

A separate Mojo method `GetPageContent()` exists on the page handler for explicit "what's on this page?" queries. This is **not** called during normal submit flow. It's a standalone inspection tool.

## Workstreams

### A. Verify End-to-End Context Flow

**What:** Confirm that the adapter actually invokes the extractor when `attach_browser_context=true` and that extracted context reaches the LLM prompt.

**Candidate files:**
- `browser/ai/maho_ai_runtime_adapter.cc` (verify extraction call + formatting)
- `browser/ai/maho_opencode_runtime_service.cc` (verify augmented prompt arrives)

**Atomic steps:**
1. Trace `SubmitMessage` in the adapter: confirm it calls the extractor when the boolean is true.
2. Verify the formatted output (context + user prompt) reaches the OpenCode runtime service in a form the LLM can consume.
3. Confirm the null-context path (boolean=false) produces the same runtime call as before context was added.
4. Log or inspect the augmented prompt to validate fields: URL, title, selected text, body text (truncated).

**Success criteria:** With context enabled, a prompt submitted while viewing a known page (e.g., `chrome://version`) results in the LLM receiving page title, URL, and body text. With context disabled, the LLM receives only the raw prompt.

---

### B. Structured Context Contract (Reporting/Inspection)

**What:** Define a structured `BrowserContextPayload` type for reporting what was extracted. This is for inspection and debugging, not for TS-owned submission.

**Candidate files:**
- `browser/ui/webui/maho_ai/maho_ai.mojom` (optional struct for reporting)
- `browser/resources/maho_ai/types.ts` (TS mirror for rendering in inspector)
- `browser/resources/maho_ai/views/inspector.ts` (render context post-submission)

**Atomic steps:**
1. Define `BrowserContextPayload` Mojo struct if needed for inspector: `url`, `title`, `selectedText`, `pageTextPreview` (first N chars), `timestamp`.
2. Add corresponding TS interface for the inspector view.
3. After submission, the adapter (or page handler) can optionally report the attached context back to the UI for inspector display.
4. The inspector's existing "context" selection kind shows what was sent, post-submission only.

**Success criteria:** After a context-augmented submission, the inspector panel can display what the adapter extracted. No new persistent UI. No TS involvement in the extraction pipeline itself.

---

### C. Composer Behavior (Minimal Visibility)

**What:** The composer surface supports context toggling with near-zero visual footprint. TS renders browser-reported state, not browser-assembled payloads.

**Candidate files:**
- `browser/resources/maho_ai/views/composer.ts`
- `browser/resources/maho_ai/store.ts`

**Atomic steps:**
1. Ensure `attachBrowserContext` toggle works correctly and its state flows through the Mojo call.
2. Update placeholder text dynamically: if context is attached, show "Asking about [page title]..." while composing. Disappears on send.
3. A single subtle toggle (inline icon, not a chip tray) lets users disable context per-prompt. No persistent new elements.
4. Page title for the placeholder can come from a lightweight signal (tab title observation), not from running the full extractor.

**Success criteria:** Composer visually unchanged at rest. Toggle state flows correctly to the adapter. Users can opt out per-prompt. No TS-owned context assembly or payload construction.

---

### D. Runtime/Backend Contract Preservation

**What:** Guarantee the existing OpenCode runtime path isn't broken by context additions.

**Candidate files:**
- `browser/ai/maho_ai_runtime_adapter.cc`
- `browser/ai/maho_opencode_runtime_service.cc`

**Atomic steps:**
1. When `attach_browser_context=false`, the adapter must call the runtime with the same signature and payload as today. No new fields, no new dependencies.
2. When true, the adapter formats extracted context as a structured prefix to the prompt (or as a system message). The runtime service doesn't need schema changes.
3. Verify: `cancelTurn`, `respondToApproval`, session create/resume calls are completely unaffected by context additions.
4. Verify: the 50k truncation limit prevents oversized payloads from reaching the LLM.

**Success criteria:** Existing integration tests pass unchanged. Context-augmented path produces valid augmented prompts. No regression in session lifecycle flows.

## Parallelization Plan

| Phase | Workstreams | Dependencies |
|-------|-------------|--------------|
| Phase 1 | A (verify flow) | None. Start immediately. |
| Phase 2 | B + D (in parallel) | A confirms the adapter path works. |
| Phase 3 | C | Depends on A being validated. |

## Verification Plan

1. **End-to-end flow (covers A):** Submit a prompt with context enabled while viewing a known page. Verify via logging or inspection that the LLM receives page URL, title, and body text in the augmented prompt.
2. **Null-context preservation (covers D):** Submit with context disabled. Confirm the runtime call is identical to pre-context behavior. Lifecycle calls (`cancelTurn`, `respondToApproval`, session create/resume) unaffected.
3. **Truncation enforcement:** Submit while viewing a very long page. Confirm body text is capped at 50,000 chars in the extracted output.
4. **Deny-list enforcement:** Navigate to `chrome://settings` and submit with context enabled. Confirm extraction is skipped (returns `kCannotAccess`) and the prompt goes through without context.
5. **Visual regression (covers C):** Composer looks identical at rest. No new persistent elements visible in the panel. Inspector shows context payload only post-submission.
6. **Type safety:** `bun run typecheck` passes. Mojo IDL compiles via `autoninja` with no errors on any new or modified structs.

## Risks / Open Questions

1. **Adapter extraction timing.** If the adapter calls the extractor asynchronously and the page navigates mid-extraction, the payload might be stale. Mitigation: the extractor captures URL + timestamp so the runtime can detect staleness.

2. **Cross-origin frames.** Current extractor runs JS in the top frame only. Revisit multi-frame extraction later.

3. **Runtime format evolution.** Currently context is prepended as a system-level note. If OpenCode later supports structured context natively, the adapter formatting can be swapped without changing the extraction pipeline or TS layer.

4. **Privacy.** Page content flows to the LLM provider. This is expected behavior (same as Dia). The per-prompt opt-out toggle is the user's control. The deny-list (`chrome://`, `chrome-untrusted://`, `devtools://`, `about://`) prevents extraction on sensitive internal pages. Incognito handling is a follow-up concern.

5. **Session-start asymmetry.** `StartSession` hardcodes `attach_browser_context=true` for the initial prompt, ignoring the user's toggle. Decide if this is intentional UX or a bug.

---

*This plan reflects the adapter-owned architecture. TS sends intent (a boolean), the browser does extraction, and the adapter formats context for the runtime. No TS-owned context assembly. No round-trip `GetCurrentPageContext()` in the submit path. Decorative UI is deferred. The goal is invisible plumbing that makes Maho AI contextually aware without shipping surfaces that don't earn their space.*
