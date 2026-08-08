# Arc Space parity contract

This document is the contract for the first implementation wave of Arc-style Space parity in Maho. It is written to guide implementation subagents without requiring them to guess intent.

## Evidence labels

- **Confirmed** means the behavior is already established by the current repo evidence in this conversation, including architecture notes, the existing Arc parity notes, and the current semantic decision.
- **Inferred** means the behavior is a reasonable implementation rule derived from that evidence, but it is not directly proven by current code or a cited reference.
- **Unknown** means the evidence is not enough to state a rule yet. Unknown items must not be silently filled in by implementation work.

## Scope

### Confirmed

- The feature in scope is Arc-style Spaces for the macOS shell.
- Spaces are tab containers with reorder, rename, active tracking, and persistence responsibilities in `maho-core` and `maho-types`.
- Maho Mini already exists as a separate behavior track, and its Space selection flow is part of the parity work.
- The repo already has a core `space_manager.rs` and shared `SpaceId` and space types, so this work extends an existing model rather than inventing a new one.

### Inferred

- The first wave should define the canonical product contract for future code changes, not just a visual shell pass.
- The contract should cover both user-visible behavior and the core state model because Space behavior crosses core, shell, and persistence boundaries.

## Semantic decision

### Confirmed

- **Normal windows use the global active-space model.**
- **Isolated window behavior is deferred to a future Blank-window track.**

### Implication

- Any implementation that tries to make normal windows behave like isolated per-window spaces is out of scope for this wave.
- Spaces should still feel Arc-like, but the window model stays globally active for the normal browser experience.

## Non-goals

### Confirmed

- Do not implement the future Blank-window track in this wave.
- Do not add AI features, downloads, or other unrelated Arc parity work here.
- Do not change the normal window semantic decision above.

### Inferred

- Do not optimize for a minimal viable version. This document is the parity contract for the remaining waves.
- Do not leave core behaviors undefined just because the UI exists.

## Canonical behaviors

### Confirmed

1. Spaces exist as named containers for tabs.
2. Spaces can be reordered.
3. Spaces can be renamed.
4. Active space state is tracked and persisted.
5. Maho Mini can open content in a selected Space.
6. The core already exposes space management as a concern, so the shell must reflect core truth instead of maintaining a separate hidden model.

### Inferred

1. A tab belongs to exactly one Space at a time in the normal window model.
2. The active Space drives the tab set visible in the main window.
3. Reordering spaces must preserve the identity of each Space, not recreate them.
4. Renaming a Space changes only its label and any linked presentation fields, not its identity.

## UX requirements

### Confirmed

- The app must expose Space controls in a way that supports rename, reorder, and active-state switching.
- The current repo notes identify a Space selection pop-up flow for Maho Mini.

### Inferred

- Space controls should behave like durable browser state, not temporary UI state.
- Active-space changes should feel immediate and should not require recreating tabs.
- Any Space presentation in shell UI must stay in sync with core state changes.

## Keyboard behaviors

### Confirmed

- Maho Mini already has keyboard-driven open behaviors in the repo notes.
- `⌘⌥O` is tied to opening in a different Space through a Space selection menu flow.

### Inferred

- Keyboard commands that act on Space should resolve the current active Space in the global model first.
- Keyboard flows must not create hidden per-window state for normal windows.
- If a keyboard action opens content in a target Space, that target Space must be the same one persisted in core state.

## Maho Mini behaviors

### Confirmed

- `MahoMiniWindowController` already owns the Space selection menu flow for `⌘⌥O`.
- The repo notes confirm `⌘⌥N` for open, `⌘O` for open in current Space, and `⌘⌥O` for open in a different Space.

### Inferred

- Maho Mini is the place where users choose a Space before opening a tab when they do not want the current active Space.
- Maho Mini must respect the same Space ordering and naming that the main shell exposes.
- If the user selects a Space from Maho Mini, that action should be reflected in the shared core state and not just in the pop-up UI.

## Profile linkage rules

### Confirmed

- `ProfileId` exists as a shared domain type.
- Spaces are core entities, so profile-related behavior must be represented through core state rather than shell-only caches.

### Inferred

- A Space should be associated with a profile context, not treated as a free-floating label.
- When a profile changes, Space presentation should remain deterministic for that profile.
- If a profile owns multiple Spaces, Space order and names should round-trip with that profile’s persisted state.
- If profile linkage is not yet fully resolved in code, that gap must be surfaced in implementation work rather than hidden behind shell assumptions.

## Multi-window semantics

### Confirmed

- The semantic decision is that normal windows use the global active-space model.
- The blank, isolated window track is deferred.

### Inferred

- Multiple normal windows must observe the same active Space state.
- A Space switch in one normal window should be reflected in the other normal windows because the model is global.
- Window-specific isolation should not appear as an accidental side effect of the current wave.
- If a future Blank-window track introduces isolated behavior, it must be layered on separately and not folded into this contract.

## Known ambiguities

### Confirmed

- The current repo evidence does not fully define every Space edge case.

### Inferred / unknown

- Whether Space reordering is purely local or sync-backed is not yet proven here.
- Whether Space order is deterministic across all profiles and sessions or only within a profile is not fully proven here.
- Whether Maho Mini selection should auto-switch the active Space or only target the open action is not fully proven here.
- Whether Space icons are editable separately from names, and how those edits persist, is only partially proven here.

## Known current-code gaps that must be closed

### Confirmed from the repo notes in this conversation

- The Space rename prompt path is connected through the canonical Space config update flow.
- The Space reorder path is connected through the canonical event/config path.
- The `spaceOrderChanged` consumer is wired up through the existing update flow.
- The icon and config edit path is wired through `SpaceConfigUpdated`.
- Duplicate core update emission has been consolidated onto one canonical path for Space actions.

### Inferred implementation impact

- These gaps are not cosmetic. They block a full parity contract because they affect persistence, ordering, and state propagation.
- Implementation subagents should treat them as blocking gaps, not optional polish.

## Acceptance criteria

### Confirmed

- The normal-window active-space behavior matches the global model in this document.
- Maho Mini can target a Space using the existing selection flow.
- Space rename, reorder, and active tracking behave as a single coherent model across core and shell.
- Space icon edits are handled as part of Space config updates and round-trip through the shared update model.

### Inferred

- Space changes should persist across relaunch.
- Window state, tab state, and Space state should not drift apart.
- A Space action should have one source of truth and one emitted state transition path.
- The implementation is not complete if any of the gaps listed above are still papered over by shell-only behavior.

## Implementation notes for subagents

- Treat `space_manager.rs`, shared Space types, and shell event plumbing as the primary seam.
- Keep the global active-space model for normal windows.
- Do not introduce isolated window semantics in this wave.
- Mark any place where the evidence is weak as `inferred` or `unknown` in code review notes and implementation notes.
