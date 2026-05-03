# Arc mobile parity matrix and execution contract

This document is the implementation contract for the remaining Arc mobile parity phases in Maho. It locks the target, the current repo status, and the order of work so subagents can implement without referring back to chat history. Desktop-only Arc scope stays out of this spec unless it affects mobile sync or continuity.

## Status legend

- **implemented** means the item exists in the repo as a real product path and is not just a placeholder.
- **partial** means the item exists, but behavior, coverage, or parity is still incomplete.
- **stubbed** means the repo has a visible hook or surface, but the real behavior is not wired through yet.
- **missing** means there is no meaningful implementation evidence for the item in the current repo state.

## Parity definition

Parity is not just “the screen exists.” Each target item must meet all four checks below before it can be called done.

### Visual parity

Visual parity means the mobile surface matches Arc’s intended mobile shape closely enough that users can recognize the same product area at a glance.

For this project, visual parity requires:

- the same surface exists on the same platform target
- labels, layout, and hierarchy are consistent with the mobile design intent already present in the repo
- empty, loading, and error states do not visibly break the feature model
- cross-screen transitions do not expose fallback UI that contradicts the target feature

### Interaction parity

Interaction parity means the user can complete the same action path with the same input model and step order.

For this project, interaction parity requires:

- the entry point is reachable from the expected mobile location
- gestures, taps, menus, and deep link handling behave consistently with the current mobile shell
- state changes happen through the same canonical path on every supported device class
- the feature does not depend on a hidden workaround or one-off shell behavior

### Feature parity

Feature parity means the user-facing capability exists, not just a stub or a shell element.

For this project, feature parity requires:

- the core behavior exists on the intended platform
- the feature uses real data and real state transitions
- any platform-specific variant is explicit in this document
- missing companion behavior is called out in the matrix, not implied

### Reliability parity

Reliability parity means the feature stays correct under normal app usage, relaunch, sync, and navigation churn.

For this project, reliability parity requires:

- state survives app restarts when the feature is supposed to persist
- sync-backed data does not drift from local UI state
- deep links, share entry points, and background returns do not corrupt the current session
- the feature can be exercised in tests or manual review without relying on an undocumented path

## Canonical Arc mobile target checklist

This checklist is the target contract. A row can be fully implemented in the matrix below, but the target is not complete until every row is at least implemented or intentionally deferred in a later phase.

### Shared mobile

| Target item | Why it belongs here | Contract status |
| --- | --- | --- |
| App shell and launch flow | Base entry point for both mobile platforms | partial |
| Tabs and tab switching | Core browsing navigation | partial |
| New tab flow | Primary creation path on mobile | partial |
| Settings | User control surface for the app | partial |
| Sync | Required for continuity across devices | partial |
| Spaces | Core organizational model on mobile | partial |
| Incognito | Separate browsing mode expected on mobile | partial |
| Browse for Me | Mobile AI surface that must remain reachable | partial |
| History | Common recovery and revisit workflow | partial |
| Deep links | Required for app continuity and external entry | partial |
| Reader mode | Known parity area that still needs stronger evidence | partial |

### iOS-only

| Target item | Why it belongs here | Contract status |
| --- | --- | --- |
| Pinch summary | iOS-specific AI interaction | implemented |
| Handoff | Apple continuity feature | implemented |
| App intents | System integration on iOS | implemented |
| Share extension | Native iOS sharing entry point | implemented |

### Android-only

| Target item | Why it belongs here | Contract status |
| --- | --- | --- |
| Widget | Android home screen entry point | implemented |
| Share target | Native Android share entry point | implemented |

### Must-have workflows

| Workflow | What must work | Contract status |
| --- | --- | --- |
| Open the app from a cold start and reach the shell | The main mobile shell must load with the expected landing state | partial |
| Open a new tab from the shell | New tab creation must use the shared mobile state path | partial |
| Switch spaces and return to the same session state | Space switching must not orphan tabs or profile context | partial |
| Open from deep link into the right mobile surface | Deep links must resolve to the right in-app destination | partial |
| Enter and leave incognito without corrupting normal state | Incognito must stay isolated from normal browsing state | partial |
| Reach Browse for Me and complete the core flow | The AI entry point must function as a real workflow | partial |
| Open history and revisit an existing page | History must be usable as a recovery path | partial |
| Share into the app from the platform share surface | Share entry must land in a usable browser state | partial |

## Current repo status matrix

This matrix captures the current repo state as known in this conversation. It is intentionally narrow and only points at the most important evidence anchors for each item.

| Surface | Status | Evidence pointers | Notes |
| --- | --- | --- | --- |
| Mobile shell parity on both platforms | partial | `ios-shell`, `android-shell`, `maho-core`, `maho-types` | Broad shell parity exists, but shell presence alone does not close parity. |
| Tabs | partial | `ios-shell`, `android-shell`, tab state wiring in shared core | Present in some form on both platforms, but not parity complete. |
| New tab | partial | `ios-shell`, `android-shell`, shared tab creation path | Entry point exists, but it still needs workflow-level confirmation. |
| Settings | partial | `ios-shell`, `android-shell`, shared settings state | Settings exist in some form on both platforms. |
| Sync | partial | `maho-core`, `maho-storage`, shell sync entry points | Sync exists, but mobile continuity needs stronger verification. |
| Spaces | partial | `maho-core`, `maho-types`, `ios-shell`, `android-shell` | Core mobile space support exists, but the model is not complete. |
| Space reorder | missing | `maho-core`, `ios-shell`, `android-shell` | Known gap. Reorder is not fully implemented. |
| Incognito | partial | `ios-shell`, `android-shell`, private state plumbing | Surface exists, but behavior still needs full parity proof. |
| Browse for Me | partial | `ios-shell`, `android-shell`, AI surface entry points | Present in some form on both platforms. |
| History | partial | `ios-shell`, `android-shell`, history surface and state wiring | Present in some form, but not yet closed out as complete parity. |
| Deep links | partial | `ios-shell`, `android-shell`, deep link router | Deep link support exists in some form. |
| Reader mode | partial | `ios-shell`, `android-shell`, reader mode entry and rendering path | Only partially evidenced. Do not mark complete yet. |
| Android space and profile linkage | stubbed | `android-shell`, shared profile and space model | Known gap. Android linkage is still stubbed. |
| iOS pinch summary | implemented | `ios-shell` | Known iOS-only feature. Treat as implemented unless later evidence proves a regression. |
| iOS handoff | implemented | `ios-shell` | Known iOS-only feature. |
| iOS app intents | implemented | `ios-shell` | Known iOS-only feature. |
| iOS share extension | implemented | `ios-shell` | Known iOS-only feature. |
| Android widget | implemented | `android-shell` | Known Android-only feature. |
| Android share target | implemented | `android-shell` | Known Android-only feature. |

## Execution phases

These phases are the required implementation order. Phase 1 is this document. Later phases should not invent a new order without updating this spec first.

### Phase 1, spec lock

Objective: freeze the target matrix, the parity definition, and the evidence model.

Done when:

- this document exists in-repo
- the target list is stable enough for implementation sequencing
- every known item has a status and an evidence pointer
- open ambiguity is written down instead of being guessed away

### Phase 2, core and bridge alignment

Objective: align shared mobile state, bridge boundaries, and shell entry points so both platforms read from the same model.

Done when:

- the mobile shell consumes the same core state model where expected
- bridge code no longer hides important feature state behind platform-only behavior
- known cross-platform gaps are reduced, not pushed into UI-only workarounds

### Phase 3, shared workflows

Objective: close the workflows that should feel the same on both mobile platforms.

Focus areas:

- tabs
- new tab
- spaces
- incognito
- browse for me
- history
- deep links
- reader mode

Done when:

- each workflow can be exercised end to end on both platforms
- the behavior is consistent enough that platform tests can assert it
- the workflow does not depend on a platform-specific accident

### Phase 4, privacy, settings, and utilities

Objective: close the control surfaces that users rely on for safe, repeatable mobile use.

Focus areas:

- settings
- sync
- private browsing controls
- continuity and persistence helpers
- any utility path that mobile workflows depend on directly

Done when:

- the settings and sync surfaces behave consistently with the mobile shell contract
- privacy-related flows do not leak state across modes
- utilities needed by the core workflows are not left as ad hoc stubs

### Phase 5, AI differentiators

Objective: close the AI-specific mobile workflows that distinguish the product.

Focus areas:

- Browse for Me
- reader mode, where it overlaps with AI-assisted reading behavior
- pinch summary on iOS

Done when:

- the AI surfaces are reachable from the expected mobile locations
- the feature paths are reliable enough to support review and regression testing
- any platform-specific AI behavior is explicit in the matrix

### Phase 6, iOS-specific

Objective: finish the Apple-only work that cannot be shared with Android.

Focus areas:

- pinch summary
- handoff
- app intents
- share extension

Done when:

- each Apple-only feature is wired into the mobile shell without placeholder behavior
- the feature works through the native iOS entry point and not just from internal navigation
- the iOS-specific surface does not break shared workflow assumptions

### Phase 7, Android-specific

Objective: finish the Android-only work that cannot be shared with iOS.

Focus areas:

- widget
- share target
- Android space and profile linkage

Done when:

- each Android-only feature is wired into the Android shell end to end
- the Android space and profile model is no longer stubbed
- shared workflows still behave the same after the Android-specific work lands

### Phase 8, verification and review

Objective: prove the parity contract instead of assuming it.

Done when:

- the target matrix matches the actual repo state
- known gaps are either closed or explicitly deferred with a reason
- each high-priority workflow has a test or review path
- the implementation can be reviewed against this document without extra context

## Acceptance criteria for parity complete

Parity is complete only when all of the following are true:

- every item in the canonical mobile target checklist is implemented, or a later documented exception exists with an explicit reason
- every status in the matrix has moved out of stubbed and missing, except for items explicitly outside the mobile parity contract
- the known gaps in this document are closed, including space reorder and Android space and profile linkage
- reader mode has a single, coherent mobile story across platforms, not two unrelated partial implementations
- iOS-only features and Android-only features are present and behave like intentional platform features, not accidental leftovers
- the core workflows, launch, new tab, spaces, incognito, Browse for Me, history, deep links, and settings, survive relaunch and normal app churn
- no parity item depends on an undocumented shell-only workaround
- the final implementation can be reviewed and verified against this matrix without adding new scope

## Implementation note

This document is the contract for the remaining parity phases. If a later implementation discovers a real mismatch with the matrix, the matrix must be updated before the code changes are treated as done.
