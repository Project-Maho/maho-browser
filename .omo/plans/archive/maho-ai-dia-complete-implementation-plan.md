# Maho AI: Dia-Like Chat Surface — Complete Implementation Plan (v1)

## Overview

Transform the Maho AI side panel's compact/assistant React branch into a minimal, conversational chat surface inspired by Dia. The backend (opencode integration) stays completely unchanged. All work targets the existing side-panel host at 340px–520px width. No Mojo, store, or host rewrites.

This document is the canonical, self-contained implementation plan. It supersedes prior related plans in this directory.

## Scope & Assumptions

**In scope:**
- Compact-surface React components (the assistant/chat branch)
- Default entry point into the AI panel (what users see on open)
- Message rendering (assistant, user, tool/system)
- Idle/empty state
- Working/thinking state
- Composer area
- Developer-mode entry affordance

**Assumptions:**
- The opencode backend transport, session management, and Mojo bridge all remain as-is
- The side panel host (WebUI bubble, panel frame, resize behavior) stays unchanged
- The developer workspace (full IDE-like surface with file trees, terminal, etc.) continues to exist but is not the default view
- Width will range from roughly 340px to 520px; designs must be fluid within that band

## Architecture Ground Truth

```
┌─────────────────────────────────────────────────┐
│  Chromium Side Panel Host (unchanged)           │
│  ┌───────────────────────────────────────────┐  │
│  │  MahoAI WebUI (React)                     │  │
│  │  ┌─────────────────────────────────────┐  │  │
│  │  │  Compact Chat Surface  ← THIS PLAN  │  │  │
│  │  │  (default entry)                     │  │  │
│  │  └─────────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────────┐  │  │
│  │  │  Developer Workspace (existing)      │  │  │
│  │  │  (behind overflow/quiet affordance)  │  │  │
│  │  └─────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────┘  │
│  Mojo Bridge → opencode backend (UNCHANGED)     │
└─────────────────────────────────────────────────┘
```

## Resolved Design Decisions

| Decision | Resolution |
|----------|-----------|
| Default view on panel open | Compact chat (idle state) |
| Developer mode access | Demoted: overflow menu or quiet secondary affordance, not a visible toggle |
| Assistant message styling | No bubble/card surface. Plain markdown text, left-aligned, inherits panel background |
| User message styling | Compact right-aligned bubbles with subtle background differentiation |
| Tool/system activity | Minimal humanized inline text (e.g., "Searched 3 files", "Reading page.tsx"). No collapsible JSON dumps in compact view |
| Approval prompts | Remain visible and actionable; these are the exception to minimal treatment |
| Working/thinking state | Faint pulsing or static text ("Thinking..." or contextual verb), no graphical spinner or animated dots |
| Idle state | Header + empty conversational space + composer at bottom. Nothing else. |
| Panel width | Fluid 340px–520px. No fixed breakpoints. |
| Backend changes | None. Zero. |

## Implementation Phases

### Phase 1: Idle State & Composer

**Goal:** When the panel opens with no active conversation, show a clean minimal surface.

**Work:**
- Strip existing welcome/onboarding chrome from the compact branch
- Render: thin header (Maho AI label, overflow menu icon) + blank middle + bottom-anchored composer
- Composer: single-line text input that grows to multi-line, send button, no attachment UI in v1
- Overflow menu contains: "Developer Mode", "New Chat", "Settings" (or subset)

**Likely files/areas:**
- `maho-chromium/browser/resources/side_panel/ai/` (React components)
- Compact/assistant entry component, layout wrapper, composer component

**Verification gate:** Panel opens to blank + composer. No visual artifacts. Developer mode reachable from overflow. Composer accepts text and submits to existing opencode backend.

---

### Phase 2: Message Thread Rendering

**Goal:** Conversation messages render in the Dia-like minimal style.

**Work:**
- Assistant messages: left-aligned markdown text, no card/bubble wrapper, appropriate line-height and max-width for readability at 340–520px
- User messages: right-aligned, compact bubble with subtle background fill, constrained max-width (~80%)
- Thread scrolls naturally; newest message at bottom with auto-scroll
- Timestamps hidden by default (available on hover or long-press, not required for v1)

**Likely files/areas:**
- Message list component, message bubble components, markdown renderer integration
- CSS/Tailwind for message layout and alignment

**Verification gate:** Send a message via composer, receive assistant response. Both render correctly. Markdown (code blocks, lists, bold) renders in assistant messages. Scroll behavior works with 20+ messages.

---

### Phase 3: Tool & System Activity

**Goal:** Backend tool calls and system events show as minimal inline text rather than verbose expandable blocks.

**Work:**
- Map tool-call events to short humanized strings (e.g., `read_file` → "Read config.ts", `grep` → "Searched for X")
- Render as faint left-aligned text between assistant messages, smaller font, muted color
- Approval-required actions remain full-width with clear accept/reject buttons
- No collapsible detail panels in compact view (developer workspace has those)

**Likely files/areas:**
- Tool activity renderer, approval prompt component
- Mapping layer from opencode tool event types to display strings

**Verification gate:** Trigger a multi-tool response. Tool activity lines appear as brief text. Approval prompts are tappable and functional. No layout jank between tool lines and assistant prose.

---

### Phase 4: Working State

**Goal:** While the assistant is processing, show a calm textual indicator.

**Work:**
- Replace any existing spinner/animation with faint text: "Thinking..." or a contextual verb derived from the current tool (e.g., "Reading..." during file reads)
- Text appears in the message thread at the position where the next assistant message will land
- Disappears when the first assistant content token arrives

**Likely files/areas:**
- Streaming/loading state component within the message list

**Verification gate:** Submit a query. "Thinking..." appears. It transitions smoothly to streamed response text. No flicker, no layout shift.

---

### Phase 5: Developer Mode Demotion & Polish

**Goal:** The developer workspace is accessible but quiet. Final polish pass.

**Work:**
- Overflow menu item "Developer Mode" transitions to the full workspace view
- Back affordance from developer workspace returns to compact chat
- Visual polish: consistent spacing, hover states, focus rings for accessibility
- Dark/light theme verification (both must work)
- Edge cases: very long messages, code blocks wider than panel, empty tool names

**Likely files/areas:**
- Navigation/routing between compact and developer views
- Theme variables, spacing tokens

**Verification gate:** Full round-trip: open panel → idle → send message → see response → open developer mode → return to chat. Both themes. No regressions in developer workspace functionality.

## Acceptance Criteria

1. Panel opens to idle state: header + empty space + composer. Nothing else visible.
2. User messages render as compact right-aligned bubbles.
3. Assistant messages render as plain markdown text, no card/bubble surface.
4. Tool activity shows as minimal humanized text lines.
5. Approval prompts remain visible and actionable.
6. Working state is faint text, not animation.
7. Developer mode is reachable from overflow menu, not a prominent toggle.
8. Layout is fluid and usable across 340px–520px width.
9. opencode backend integration is completely untouched.
10. Dark and light themes both render correctly.
11. No regressions in developer workspace when accessed via overflow.

## Explicit Out-of-Scope

- **Backend/opencode changes** — no transport, session, or Mojo modifications
- **Side panel host rewrite** — no floating window, no detached panel, no resize handle changes
- **Developer workspace internals** — file tree, terminal, diff viewer all untouched
- **Mojo IDL changes** — existing interface contract stays as-is
- **Store/state management rewrite** — use existing state layer
- **Attachment/file-upload UI** — not in v1 composer
- **Conversation history/list UI** — v1 is single active conversation
- **Voice input** — not in scope
- **Mobile/responsive below 340px** — not a target

## Future Note

A floating, detachable host (panel pops out of the sidebar into its own window, resizable beyond 520px) is architecturally feasible and a natural v2 evolution. The compact chat surface built here will carry over directly into that host. This plan deliberately avoids any host-level changes to keep scope tight and shippable.
