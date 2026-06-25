# Maho AI: OpenCode-backed Dia-like Assistant — Final Plan

## Objective

Ship a Dia-like assistant experience on `chrome://maho-ai` that is visually polished, input-correct, icon-compliant, and behaviorally intelligent. The experience runs on the existing OpenCode runtime. This plan folds UI polish, Unicode/IME correctness, and SF Symbols compliance into the previously established assistant-first + behavior-shaping scope as first-class workstreams, not afterthoughts.

This plan supersedes `maho-ai-dia-like-assistant-parity-plan.md` conceptually by incorporating everything that plan covers plus the three new workstreams. The earlier file remains in place for reference history.

## Source of truth

- **This file** is the authoritative execution plan for the maho-ai user-facing surface.
- **Reference image** (provided separately) is the visual authority for user-facing parity. When this plan's text conflicts with the reference image, the image wins.
- `docs/frontend-architecture.md` remains a valid *backend* technical reference only, not a product target.

## Foundational commitments

1. **OpenCode backend stays.** The runtime adapter (`maho_opencode_acp_adapter.cc`, ACP transport via the `maho-acp` Rust crate behind `maho-ffi`), session model, and event stream contract remain the engine. No replacement, no alternative runtime. (See `.sisyphus/plans/maho-ai-acp-migration-plan.md` for the Rust-first migration history.)
2. **Assistant-first default surface stays.** The console/timeline/inspector layout is an opt-in developer mode, never the default.
3. **Reference image is the visual authority.** The target is meaningful Dia-like feel grounded in that reference, not guaranteed exact or full Dia parity.
4. **UI polish is now explicitly in scope.** Not deferred to "later." Structural parity gaps (floating card, width cap, empty-state hero, streaming animation, markdown rendering, theme coherence) are tracked and resolved.
5. **Unicode/IME correctness is a first-class workstream.** Not a backend problem. Not a nice-to-have.
6. **SF Symbols compliance is a first-class workstream.** The icon system is macOS SF Symbols-backed, not an arbitrary SVG set. Defects in the allowlist are real bugs.
7. **Behavior/intelligence shaping remains in scope.** Response shaping, persona injection, suggestion quality gating, tool authorization, conversation continuity/memory.

## Verified repo facts

| Fact | Evidence |
|------|----------|
| Active UI path | `app.ts` → `renderConsoleShell()` → `views/console_shell.ts` |
| Main visible surface files | `console_shell.ts`, `composer.ts`, `conversation_thread.ts`, `maho_ai.css` |
| Orphaned console-era components | `left_rail.ts`, `timeline.ts`, `inspector.ts` — compiled, no active CSS-backed default path |
| Icon system | SF Symbols via `views/icons.ts` + `maho_ai.css` mask-image + `maho_ai_sf_symbol_source.mm` |
| Icon defect | `globe` used in TS, missing from native allowlist in `.mm` file |
| Non-macOS icon stub | Returns transparent PNG (correct fallback, but visually empty) |
| Unicode/IME surface | `composer.ts` keydown submit path likely ignores `event.isComposing`; `console_shell.ts` DOM replacement may break composition |
| Prompt transport | Mojo/C++/OpenCode path is UTF-8-safe; session-title truncation may need UTF-8-aware clamping |
| Structural parity gaps | Current `.compact-panel` IS a floating card with border-radius and border; current CSS already sets `--maho-ai-shell-width: 420px` width cap — both contribute to divergence from the reference (reference uses a different container geometry and does not hard-cap at 420px). Remaining gaps: no empty-state hero/stage, quick actions only on no-session, raw system rows in thread, flat markdown rendering, no streaming animation, theme conflict with `maho_common/maho_theme.css` |

## Non-goals / anti-drift rules

1. Do NOT replace or bypass OpenCode as the runtime.
2. Do NOT build the four-panel console layout as the default surface.
3. Do NOT drift back to a console-first or operator-console target.
4. Do NOT treat icons as arbitrary SVGs. They are SF Symbols. Compliance means native allowlist + mask-image CSS.
5. Do NOT hand-wave Unicode/IME as a backend or "later" problem. The bug is frontend-local and must be fixed in the input path.
6. Do NOT claim full Dia parity. The target is meaningful Dia-like feel, verified against the reference image.
7. Do NOT expose sessions, tool arguments, or runtime metadata by default.
8. Do NOT modify the runtime adapter's connection model, session lifecycle, or event stream contract structurally.
9. Do NOT add new WebUI surfaces or URLs. Keep `chrome://maho-ai` and side-panel host.
10. Do NOT add Rust-side AI logic.
11. Do NOT remove console/developer mode. It survives behind a toggle.
12. Do NOT delete or modify existing plan files.

## Phased workstreams

---

### Phase A: Unicode/IME Input Correctness

**Goal:** CJK and accented-character input via IME composes and submits correctly without interruption, character loss, or premature send.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/composer.ts`
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`

**Atomic steps:**

1. Audit `composer.ts` keydown/keypress handlers. Identify every submit-triggering path (Enter key, button click, keyboard shortcut).
2. Guard every keyboard-triggered submit with `if (event.isComposing || event.keyCode === 229) return;` or the equivalent `compositionstart`/`compositionend` state flag.
3. Audit `console_shell.ts` for DOM mutations that replace or re-parent the textarea/contenteditable during active composition. Identify re-render triggers (state changes, event handlers) that could destroy the IME composition session.
4. Ensure the textarea element is never removed from DOM or replaced while `compositionstart` is active and `compositionend` has not fired.
5. If `console_shell.ts` uses full re-render on state change, introduce a guard: skip re-render of the composer region while composition is active, or use a stable DOM node that persists across re-renders.
6. Audit session-title display for UTF-8-safe truncation. If truncation uses byte slicing or simple `.substring()` without grapheme awareness, fix to use `Intl.Segmenter` or equivalent.
7. Test with Japanese (hiragana → kanji conversion), Korean (jamo composition), and Chinese pinyin IME workflows.

**Verification:**
- Type a multi-character Japanese word via IME. Confirm composition underline appears, candidate window functions, and final confirmed text lands in the composer without character loss.
- Press Enter during active composition. Confirm it selects the IME candidate (not submits the prompt).
- Rapid state changes from incoming events during composition do not destroy the textarea.

**Exit criteria:** IME composition is never interrupted by the submit path or DOM re-renders. Session titles display correctly for all Unicode text.

---

### Phase B: SF Symbols Compliance

**Goal:** Every icon rendered in the maho-ai surface is backed by a valid SF Symbol in the native allowlist, with correct mask-image CSS treatment. No broken/invisible icons.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/icons.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css` (mask-image declarations)
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_sf_symbol_source.mm` (native allowlist)
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_sf_symbol_source.h`

**Atomic steps:**

1. Enumerate every icon name referenced in `icons.ts` and `maho_ai.css`.
2. Cross-reference against the native allowlist in `maho_ai_sf_symbol_source.mm`.
3. Identify mismatches: icons used in TS/CSS but missing from .mm (confirmed: `globe` is one).
4. For each missing icon, add it to the native allowlist with the correct SF Symbol name.
5. Verify that the CSS mask-image path for each icon resolves correctly (the URL scheme that fetches from the native source).
6. Audit the non-macOS stub path. Confirm it returns a valid (even if empty) image resource that does not break layout. If icons are critical for understanding (like a send button), add a platform-appropriate fallback (text label or bundled minimal SVG for non-macOS only).
7. If any reference-image icons require SF Symbols not currently in the codebase, add them to both the TS enum and the .mm allowlist.

**Verification:**
- Build on macOS. Confirm every icon in the maho-ai panel renders as the correct SF Symbol glyph.
- Check the `globe` icon specifically renders after the allowlist fix.
- Build on non-macOS (or test stub path). Confirm no broken image indicators or layout collapse.

**Exit criteria:** Zero icon mismatches between TS usage and native allowlist. All icons render correctly on macOS. Non-macOS degrades gracefully without layout breakage.

---

### Phase C: Structural UI Parity (Reference Image Authority)

**Goal:** Close the structural parity gaps between the current surface and the reference image. The reference image is the visual authority for layout, spacing, and component presence.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts`
- `maho-chromium/browser/resources/maho_ai/views/composer.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/resources/maho_ai/maho_ai.html`
- `maho-chromium/browser/resources/maho_common/maho_theme.css` (theme conflict)

**Atomic steps:**

1. **Floating card container:** The current `.compact-panel` already renders as a floating card with border-radius and border. Reshape its geometry (corner radius, padding, inset, shadow) to match the reference image's container treatment. Do NOT add a new card wrapper; modify the existing one.
2. **Width cap:** The current CSS already sets `--maho-ai-shell-width: 420px`. Evaluate whether the reference image uses a different width or a fluid/responsive approach. If the reference shows a wider or fluid container, remove or adjust the hard 420px cap to match. Do NOT blindly re-add a width cap that already exists.
3. **Empty-state hero/stage:** Redesign the no-session idle state to match the reference image's hero treatment (centered branding, calm whitespace, personality).
4. **Quick actions placement:** Quick actions appear only in the no-session state (already correct); verify they match reference styling (pill shape, spacing, icon treatment).
5. **System rows in thread:** Replace raw/interruptive system event rows with brief inline indicators matching the reference image's treatment (subtle, non-breaking, part of conversation flow).
6. **Markdown rendering:** Upgrade flat markdown to styled rendering with proper code blocks, lists, and emphasis. Match reference image's typography and spacing for assistant messages.
7. **Streaming animation:** Add a typing/thinking indicator visible during token streaming. Match reference image's animation treatment (pulsing dot, shimmer line, or whatever the reference shows).
8. **Theme conflict resolution:** Audit conflict between `maho_ai.css` custom properties and `maho_common/maho_theme.css`. Resolve by either scoping maho-ai custom properties within the panel's root selector or by aligning with the common theme where appropriate. The reference image is the arbiter of correct color/contrast.

**Verification:**
- Screenshot the panel in idle state and active-conversation state.
- Compare side-by-side with reference image.
- Confirm: floating card geometry matches reference ✓, width behavior matches reference (cap adjusted or removed) ✓, hero state ✓, system rows are subtle ✓, markdown renders styled ✓, streaming shows animation ✓, no theme bleed from common CSS ✓.

**Exit criteria:** A reasonable observer comparing the live panel to the reference image would say "these are the same design" for structure and layout. Fine color/animation polish deferred to Phase F.

---

### Phase D: Conversation Thread & Response Shaping

**Goal:** The active-session view renders a conversational thread where assistant responses are primary content, tool activity is inline and brief, and raw runtime artifacts never appear in the default view.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts`
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/ai/maho_opencode_acp_adapter.cc` (shaping integration point)
- `maho-chromium/browser/ai/maho_chat_orchestration_service.cc`

**Atomic steps:**

1. Ensure the active-session path in `console_shell.ts` renders `conversation_thread.ts` as primary content (not `timeline.ts`).
2. Conversation thread renders user prompts as sent messages, assistant token streams as received messages.
3. Tool-running state shows as a brief inline indicator ("Analyzing page...") with subtle animation. No structured card.
4. Tool results appear as a collapsed summary line, expandable on tap.
5. Streaming assistant text appends live within the current assistant message region.
6. Thread auto-scrolls on new content; pauses on user scroll-up.
7. Implement response shaping: strip internal reasoning markers, collapse verbose tool narration, normalize markdown for side-panel readability.
8. Shaping location: prefer frontend `shapeEvent()` utility in `store.ts` for iteration speed; preserve unshaped event in state for developer mode.
9. Developer mode bypasses shaping entirely.

**Verification:** Start a session, submit a prompt that triggers tool use. Confirm response renders as natural conversation, tool activity is a brief inline hint, no event cards or timeline rows appear.

**Exit criteria:** The default active-session view is a conversation, not a timeline. Raw runtime artifacts never leak to the assistant-mode surface.

---

### Phase E: Behavior & Intelligence Layer

**Goal:** The assistant demonstrates contextual awareness, appropriate autonomy, conversational memory, and mode-distinct personality. This phase covers the orchestration-layer behavior work.

**Candidate files:**
- `maho-chromium/browser/ai/maho_opencode_acp_adapter.cc` (prompt construction, persona injection, memory injection)
- `maho-chromium/browser/ai/maho_chat_orchestration_service.cc` (authorization policy, suggestion generation)
- `maho-chromium/browser/ai/maho_browser_tool_executor.cc` (authorization decision point)
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts` (inline approval UX)
- `maho-chromium/browser/resources/maho_ai/views/composer.ts` (suggestion chips)
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/types.ts`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`

**Sub-workstreams:**

#### E1: Tool Authorization Policy

1. Define three tiers: auto-act (read-only tools), inline-confirm (moderate-impact), full-approval (irreversible).
2. Implement tier routing in the tool executor.
3. Auto-act tools show post-hoc indicator in conversation.
4. Inline-confirm tools show compact approval bubble with approve/deny.
5. Full-approval tools show expanded context.
6. Policy map keyed by tool category, initially conservative.

#### E2: Proactive Suggestions & Quality Gating

1. Tier 1 (instant, local): pattern-matched suggestions from page category signals.
2. Tier 2 (async, model-backed): lightweight prompt generates 1-3 contextual suggestions.
3. Quality gate: suppress Tier 2 if it duplicates Tier 1.
4. Rate limit: once per distinct URL. Cache on rapid navigation.
5. Low-confidence suppression: minimal page content → Tier 1 only.

#### E3: Persona & System Prompt Injection

1. Assistant mode: conversational, concise, no code blocks unless asked, no tool mechanics.
2. Developer mode: technically precise, code-forward, reasoning exposed.
3. Persona definitions in structured config (JSON resource), not hardcoded C++ strings.
4. Mid-session mode switch is forward-only (new persona on next prompt).

#### E4: Conversation Continuity & Memory

1. Panel reopen resumes most recent active session for that tab/origin.
2. Per-profile memory store: distilled facts from explicit user statements.
3. Memory injection: orchestration service prepends relevant facts to system context.
4. User-visible "What you remember" surface for viewing and deleting facts.
5. Conservative write policy: only explicit user statements or assistant-confirmed facts.

**Verification:** Trigger a read-only tool → auto-executes silently. Trigger a moderate tool → inline approval appears. Navigate to a content page → contextual suggestions appear. Toggle developer mode → response style changes. Tell the assistant a preference, reopen → preference persists.

**Exit criteria:** The assistant demonstrates autonomy tiers, contextual awareness, mode-appropriate personality, and cross-session memory.

---

### Phase F: Visual Polish & Animation

**Goal:** The surface feels premium, cohesive, and alive. Every state transition is smooth. The panel matches the reference image's visual quality, not just its structure.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`
- `maho-chromium/browser/resources/maho_ai/views/conversation_thread.ts`
- `maho-chromium/browser/resources/maho_ai/views/composer.ts`
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`

**Atomic steps:**

1. Refine conversation bubble styling: assistant messages calm and readable, user messages lightweight.
2. Thinking/streaming indicator: subtle pulse or shimmer matching reference image.
3. Suggestion chip appearance/dismissal transitions.
4. Composer focus states, send button animation.
5. Idle-to-active state transition (hero fades, thread enters).
6. Dark mode full parity check against reference image (if reference provides dark variant).
7. Typography audit: ensure font stack, sizes, and weights match reference.
8. Color audit: ensure palette matches reference exactly, resolving any theme-var conflicts remaining from Phase C.
9. Scroll behavior: overscroll treatment, fade at top/bottom if reference shows it.

**Verification:** Visual review of idle, active, tool-running, approval, and suggestion states. Side-by-side with reference image. Coherence check across all states and both color schemes.

**Exit criteria:** The surface feels like one cohesive product that a reasonable observer would identify as "the same design" as the reference image.

---

### Phase G: Developer Mode & Legacy Cleanup

**Goal:** Console/observability features live behind a clean toggle. Dead code from the console-first era is removed.

**Candidate files:**
- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/views/timeline.ts`
- `maho-chromium/browser/resources/maho_ai/views/inspector.ts`
- `maho-chromium/browser/resources/maho_ai/views/left_rail.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`

**Atomic steps:**

1. Developer mode toggle accessible from top bar or settings.
2. Toggle activates: timeline replaces conversation thread, inspector pane appears, session list becomes persistent.
3. Console views (`timeline.ts`, `inspector.ts`, `left_rail.ts`) mount only in developer mode.
4. Toggle state persists in prefs.
5. Remove orphaned CSS for compact-panel hero, status card, console-grid assumptions that no longer apply.
6. Remove dead imports and unused view branches from the default path.
7. Confirm build passes and LSP diagnostics are clean.

**Verification:** Toggle developer mode on → console layout appears. Toggle off → assistant view returns. Build succeeds. No dead code in the default rendering path.

**Exit criteria:** Console features are fully accessible behind a toggle. No dead code remains from the console-first era.

---

## Recommended execution order

```
Phase A: Unicode/IME Correctness        ← start immediately, unblocks nothing but fixes a critical defect
Phase B: SF Symbols Compliance           ← start immediately, parallel with A
Phase C: Structural UI Parity           ← start after A+B, or parallel if different engineers
Phase D: Conversation Thread & Shaping  ← start after C (depends on structural container)
Phase E: Behavior & Intelligence        ← start E1/E2 after D; E3/E4 can overlap
Phase F: Visual Polish                  ← after C+D+E are functionally complete
Phase G: Developer Mode & Cleanup       ← strictly last
```

**Parallelism notes:**
- A and B are independent of each other and all other phases. Start both Day 1.
- C can start in parallel with A/B if the engineer is different (no file conflicts for the icon/IME work vs. structural CSS).
- D depends on C's floating card container being in place.
- E sub-workstreams (E1-E4) depend on D's conversation thread being the active rendering path.
- F depends on all functional surfaces existing (C+D+E).
- G is always last.

**What NOT to do first:** Do not start Phase E (behavior) before the visible surface (C+D) exists. Intelligence without a polished surface to show it through is invisible work. Do not start Phase F (polish) before structural gaps (C) are closed.

## Risks / mitigations

| Risk | Mitigation |
|------|------------|
| IME fix requires rethinking composer DOM lifecycle | Isolate composer as a stable DOM subtree that survives parent re-renders |
| SF Symbol names differ between macOS versions | Pin to symbols available in macOS 13+ (deployment target) |
| Reference image shows elements not yet in codebase | Implement only what the reference shows; do not invent beyond it |
| Theme conflict harder than expected | Scope all maho-ai custom properties under a panel root selector; do not fight the common theme |
| Response shaping strips useful detail | Full unshaped event preserved in store; expand-on-demand in assistant mode; raw in developer mode |
| Authorization policy too conservative → assistant feels powerless | Ship conservative, iterate; read-only auto-act provides baseline utility |
| Memory store privacy concern | Conservative write policy, explicit user statements only, clear "forget" affordance, per-profile scope |
| Dia parity expectation mismatch | Plan explicitly states "meaningful Dia-like feel, not guaranteed exact parity." Reference image is the measurable bar. |

## Honesty statement

This plan targets meaningful Dia-like feel as measured against the reference image. It does not promise exact or full Dia feature parity. The reference image is the measurable bar for visual work. Behavioral intelligence (Phase E) is bounded by what OpenCode can deliver as a runtime, the expressiveness of the system prompt injection, and the quality of the underlying model. This plan shapes the presentation and policy layer; it cannot guarantee model output quality.
