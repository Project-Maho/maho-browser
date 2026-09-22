# Routines UI Surface Decision (2026-09-21)

**Status:** Decided — the AI panel Routines workspace is the canonical Routines UI.

## Decision

- **Canonical surface:** `chrome://maho-ai` AI panel → Routines workspace
  (`browser/resources/maho_ai/react/features/compact/routine-workspace.tsx`),
  reached from the panel topbar Routines item. The workspace covers list, create
  (manual / cron / event), manual run, approve-deny for paused runs, run history,
  the Max-tier gate, and error + retry.
- **Entry points (redirects into the panel — no standalone frontend):**
  - `chrome://maho-routines` (`MahoRoutinesUI`, `maho_routines_ui.cc`): requests
    the AI-panel `CompactSurface::kRoutines` pending surface, opens the AI side
    panel, then closes the tab.
  - `chrome://maho-ai?view=routines`: same pending-surface mechanism.
  - Public URL `maho://routines/` maps to `kMahoRoutinesURL`
    (`components/constants/webui_url_constants.h`).
- **Mojo ownership:** the `maho_routines.mojom` `PageHandler` is implemented by
  `MahoRoutinesPageHandler`, instantiated by `MahoAIPageHandler`
  (`maho_ai_page_handler.cc`), which re-exposes
  list/create/start/status/history/approval over the panel's Mojo surface.
- `browser/resources/maho_routines/` intentionally ships no frontend bundle.

## Rationale

- Routines are an agent capability (recipes executed by the in-browser AI agent);
  creating and approving runs benefits from chat/agent context.
- A standalone frontend would duplicate the workspace UI + Mojo client wiring +
  tests (project convention: no per-feature UI duplication).
- Evaluated with TypeSafe `choice` (jev-1.13.0): `ai_panel` at confidence 1.0.

## Consequential notes

- Event triggers (`OnNotification`, `OnInboxHeartbeat`) remain deferred: no
  emitters exist, and the agent tool path fails closed for event triggers until
  emitters are wired (ADR 0015 item 16).
- The BrowserSkill adoption roadmap (Phase 4) plans a Trace v3 recorder feeding
  `chrome://maho-routines`. That runner is expected to surface through the same
  AI panel workspace; choosing a standalone runner later requires superseding
  this decision.
