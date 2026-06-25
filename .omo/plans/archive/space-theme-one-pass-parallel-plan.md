# Space Theme One-Pass Parallel Plan

**Created:** 2026-05-11  
**Branch:** main  
**Status:** Draft, implementation-ready  
**Scope:** per-space Arc and Zen style theming, with the current native chooser UI left unchanged  
**Non-goal:** do not add new controls, do not rewrite the chooser editor, do not split this work into multiple PRs

---

## Objective

Ship one complete pass that makes per-space theming work end to end, from chooser input to persisted SpaceTheme JSON, activation, retheme, native tint propagation, and real switching. The chooser UI contract stays exactly as it is today. The native chooser remains the only chooser surface, and this plan changes the data path, native theme application path, and native visual expression path around it so the final result is reference-faithful rather than merely schema-correct.

---

## Current File-State Facts This Plan Must Honor

1. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` currently emits malformed Zen and gradient JSON. It must be changed to generate canonical `SpaceTheme` JSON using the existing chooser inputs only.
2. `maho-chromium/browser/ui/theme/maho_space_theme_io.cc` is still the strict schema contract. It must stay strict. No weakening, no fallback parsing that accepts broken theme shapes, no widening the contract to hide bad output.
3. `maho-chromium/browser/maho_space_profile_bridge.cc` must become the single activation and retheme chokepoint.
4. `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` currently bypasses the bridge and must be routed through it.
5. `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc` is an adjacent direct activation path and should be unified with the bridge so behavior is complete and consistent.
6. `maho-chromium/browser/ui/theme/maho_color_mixer.cc` currently tints too few surfaces and must expand native token propagation.
7. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` still consumes upstream system surface colors and must switch to tinted Maho tokens.
8. Multiple sidebar surfaces still use upstream `ui::kColorSys*` values directly, including archive, tab list, media, downloads, library action pane, action popover, and portions of the create-space surface. Reference-level fidelity requires a deliberate token-adoption sweep, not just a sidebar shell recolor.
9. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.cc` currently paints active dots from static light colors and ignores the configured `dot_color_` in its active-state render path. Reference-level fidelity requires active/inactive space identity to be driven by theme-derived color, not only generic system text colors.
10. The WebUI space create/config surfaces already contain the canonical gradient, harmony, opacity, and grain semantics in `browser/resources/maho_space_config/app.ts` and `browser/resources/maho_common/maho_theme_utils.ts`; the native pipeline must align with those semantics without adding new chooser controls.
11. Tests must cover real persistence and real switching. No existing command-action-specific test file was found, so the coverage plan must extend the existing sidebar interactive test surface instead of assuming a separate test file already exists.

---

## Contract to Preserve

### Must stay unchanged

- The current native chooser UI layout, controls, and interaction model.
- The chooser input contract already exposed by the existing native chooser.
- The strict `SpaceTheme` schema validation rules in `maho_space_theme_io.cc`.
- The single-PR delivery constraint.
- The rule that Zen richness must be synthesized from the existing inputs instead of exposing hidden advanced controls.

### Must not happen

- No new theme controls.
- No rewritten chooser editor.
- No weakening of validation to make malformed JSON pass.
- No fallback behavior that hides bad activation or retheme routing.
- No UI expansion beyond the chooser inputs that already exist.
- No “reference parity” claim based only on stronger tint values without real gradient/texture-aware visual differentiation in the shipped chrome.

---

## Implementation Shape

This work is intentionally split into a small serial seam and then parallel batches that can be edited without stepping on each other. The seam is about routing and contract correctness. The parallel batches are about JSON generation, bridge unification, token propagation, view tinting, render-layer fidelity, and test coverage.

### File ownership, to minimize merge conflicts

#### Workstream A, contract and JSON ownership

Owned files:

- `maho-chromium/browser/ui/theme/maho_space_theme_io.cc`
- `maho-chromium/browser/ui/theme/maho_space_theme_state.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

Primary responsibility:

- keep schema strict
- generate canonical `SpaceTheme` JSON from existing chooser inputs only
- preserve round-tripping with real persistence

#### Workstream B, activation and retheme chokepoint

Owned files:

- `maho-chromium/browser/maho_space_profile_bridge.h`
- `maho-chromium/browser/maho_space_profile_bridge.cc`
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`

Primary responsibility:

- route all activation through the bridge
- make the bridge the only place that activates or rethemes a space
- unify both command-action and WebUI entry points on the same path

#### Workstream C, native tint propagation

Owned files:

- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

Primary responsibility:

- expand tinted Maho token coverage
- stop consuming upstream system surface colors in sidebar chrome

#### Workstream D, reference-fidelity render expression

Owned files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_media_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_downloads_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_action_pane_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_action_popover_view.cc`

Primary responsibility:

- make the active theme visibly own the sidebar identity beyond the root shell
- ensure space identity, selected states, and accent-bearing surfaces actually read theme-aware tokens instead of generic system colors
- add the minimum render-expression work needed so Zen mode is visually richer than Solid mode without changing the chooser UI

#### Workstream E, validation and switching coverage

Owned files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`
- `maho-chromium/browser/BUILD.gn` only if test wiring needs to change

Primary responsibility:

- cover persistence, switching, and routing regressions with real behavior tests

---

## Serial Gates

These gates must happen in order. Each gate creates a stable seam so later work can run in parallel without reworking the same files.

### Gate 1, lock the bridge as the single chokepoint

**Goal:** make every activation and retheme path enter through `maho_space_profile_bridge`.

**Why this is first:** if entry points still bypass the bridge, later persistence and retheme verification can pass in one path and fail in another.

**Target files:**

- `maho-chromium/browser/maho_space_profile_bridge.h`
- `maho-chromium/browser/maho_space_profile_bridge.cc`
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`

**Required edits:**

- define or confirm the bridge methods that own activation plus retheme
- remove direct activation calls from `maho_command_action_handler.cc`
- route `maho_space_create_page_handler.cc` through the same bridge path
- keep any caller specific prep logic outside the bridge, but only the bridge may perform the actual activation and retheme decision

**Exit criteria:**

- no remaining direct activation path bypasses `maho_space_profile_bridge`
- the command-action path and the WebUI page-handler path both converge on the same bridge behavior

### Gate 2, make theme JSON canonical before any downstream polish

**Goal:** fix `maho_sidebar_create_space_view.cc` so it emits valid `SpaceTheme` JSON from the chooser inputs already present.

**Why this is gated:** downstream persistence and switching are meaningless if the source JSON is malformed.

**Target files:**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- `maho-chromium/browser/ui/theme/maho_space_theme_io.cc`

**Required edits:**

- remove malformed Zen and gradient JSON assembly
- use the existing chooser inputs only, no new controls, no new selector model
- serialize to canonical `SpaceTheme` JSON that matches the strict IO contract
- keep validation strict in `maho_space_theme_io.cc`

**Exit criteria:**

- the create-space view generates canonical theme JSON for every supported chooser state
- invalid shapes still fail validation, they are not normalized away by relaxing the schema

### Gate 3, confirm native tint propagation targets

**Goal:** establish the tint surfaces that must change before the work splits into parallel batches.

**Why this is gated:** `maho_color_mixer.cc` and `maho_sidebar_view.cc` are coupled by the token surface they read and write.

**Target files:**

- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

**Required edits:**

- enumerate the native tokens that need to inherit Maho tinting
- identify the sidebar surfaces still using upstream system colors
- keep the plan focused on token propagation, not on new visual controls

**Exit criteria:**

- the team has a fixed list of surfaces that will switch to tinted Maho tokens
- no hidden dependency remains on upstream system surface colors in the sidebar chrome path

### Gate 4, lock the reference-fidelity expression strategy

**Goal:** define how Zen richness becomes visibly different from Solid mode using the current chooser controls only.

**Why this is gated:** without a fixed expression strategy, later work can devolve into “stronger tint only,” which is not true reference-level parity.

**Target files:**

- `maho-chromium/browser/ui/theme/maho_space_theme_state.cc`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.cc`
- `maho-chromium/browser/resources/maho_space_config/app.ts`
- `maho-chromium/browser/resources/maho_common/maho_theme_utils.ts`

**Required decisions:**

- define the representative accent used for native tinting from gradient themes
- define how Zen differs visually from Solid with no extra controls: richer gradient-derived secondary surfaces, stronger accent-bearing selected states, and texture-aware supporting treatment
- define whether dark/light mode remains global or whether dominant-color-based adjustment is introduced for the active theme path

**Exit criteria:**

- one written strategy exists for gradient-derived accent, texture/grain usage, and dark/light handling
- the implementation team agrees on which native surfaces must visibly distinguish Zen from Solid

---

## Parallel Workstreams After the Gates

Once Gates 1 through 3 are complete, the remaining work can run in parallel. The workstreams below are intentionally narrow and file scoped.

### Workstream A, canonical SpaceTheme JSON and strict persistence

**Owned by:** `maho_sidebar_create_space_view.cc`, `maho_space_theme_io.cc`, `maho_space_theme_state.cc`

**Purpose:** make the chooser output, theme IO, and persisted state agree on the same real schema.

**Function-level guidance:**

- In `maho_sidebar_create_space_view.cc`, update the function that currently assembles the Zen and gradient payload so it calls the canonical SpaceTheme builder instead of hand-rolling malformed JSON.
- Keep the builder limited to the existing chooser inputs only, which means the current picker state feeds the JSON shape and nothing else.
- In `maho_space_theme_io.cc`, preserve strict parsing and validation. If the current code already rejects malformed schema, keep that behavior. If helper code exists for round-tripping, use it to enforce canonical output, not to accept more shapes.
- In `maho_space_theme_state.cc`, verify the persisted state model still round-trips the exact schema produced by the chooser and the bridge.

**Dependencies:** Gate 2 must be done first.

**Why this can run in parallel with the other workstreams:** it owns theme serialization and state round-tripping, not activation routing or tint application.

**Exit criteria:**

- chooser output becomes canonical `SpaceTheme` JSON
- persisted state reads and writes the same schema without drift
- strict schema rejection still catches malformed input

### Workstream B, bridge unification for activation and retheme

**Owned by:** `maho_space_profile_bridge.h`, `maho_space_profile_bridge.cc`, `maho_command_action_handler.cc`, `maho_space_create_page_handler.cc`

**Purpose:** ensure all activation and retheme behavior flows through one chokepoint.

**Function-level guidance:**

- In `maho_space_profile_bridge.cc`, make the public bridge entry point own the activation and retheme sequence end to end.
- Make the bridge the only place that decides when to activate, when to retheme, and what state to apply after a create or switch.
- In `maho_command_action_handler.cc`, remove the bypass path and forward command actions into the bridge instead.
- In `maho_space_create_page_handler.cc`, treat direct page activation as another caller of the same bridge method so the WebUI path behaves the same as the command path.

**Dependencies:** Gate 1 must be done first.

**Exit criteria:**

- no direct activation path remains outside the bridge
- command action and WebUI direct activation both produce identical bridge behavior for the same inputs
- retheme calls are issued from one chokepoint only

### Workstream C, expand native token propagation and sidebar tinting

**Owned by:** `maho_color_mixer.cc`, `maho_sidebar_view.cc`

**Purpose:** push Maho tinting into the native surfaces that still rely on upstream system colors.

**Function-level guidance:**

- In `maho_color_mixer.cc`, expand the token set so more native surfaces inherit the intended tinted palette. This is a coverage expansion, not a new theme system.
- Keep the color mixer limited to the native token propagation path. Do not create a new chooser control or an alternate theme model.
- In `maho_sidebar_view.cc`, replace reads of upstream system surface colors with the tinted Maho tokens that the mixer provides.
- Keep existing structural sidebar behavior intact. Only the color source should change.
- Include explicit token targets for shell, selected states, hover states, card-like surfaces, strip borders, and accent-bearing supporting surfaces.

**Dependencies:** Gate 3 must be done first.

**Exit criteria:**

- sidebar chrome uses tinted Maho tokens instead of upstream system surface colors
- the mixer covers the surfaces needed for real switching feedback, not just a subset of the UI

### Workstream D, reference-fidelity render expression

**Owned by:** `maho_sidebar_space_dot_view.cc`, `maho_sidebar_top_bar_view.cc`, `maho_sidebar_tab_list_view.cc`, `maho_sidebar_archive_view.cc`, `maho_sidebar_media_view.cc`, `maho_sidebar_downloads_view.cc`, `maho_sidebar_library_action_pane_view.cc`, `maho_sidebar_action_popover_view.cc`

**Purpose:** make the active theme visibly own the sidebar experience so the result feels reference-faithful instead of merely retinted.

**Function-level guidance:**

- In `maho_sidebar_space_dot_view.cc`, stop painting the active dot from a hardcoded light fill; use theme-aware color or token-driven color that reflects the current space identity, while preserving readability and selection semantics.
- In `maho_sidebar_top_bar_view.cc`, move button icon color sources off generic system-on-surface IDs where needed so active-theme contrast and accent feel can reach the navigation cluster.
- In `maho_sidebar_tab_list_view.cc`, `maho_sidebar_archive_view.cc`, `maho_sidebar_media_view.cc`, `maho_sidebar_downloads_view.cc`, `maho_sidebar_library_action_pane_view.cc`, and `maho_sidebar_action_popover_view.cc`, replace the remaining high-visibility `ui::kColorSysSurface*` and nearby generic state colors with Maho tokens where those surfaces materially participate in the visible theme identity.
- Preserve interaction behavior and layout. This workstream owns color-source migration and selective render-expression upgrades only.
- If no explicit multi-stop native background renderer is added in this pass, then Zen mode must still gain visibly richer expression through secondary theme-derived surfaces, selected-state treatment, and texture-aware supporting accents rather than a trivial accent-only tint.

**Dependencies:** Gates 3 and 4 must be done first.

**Exit criteria:**

- the active space dot, top bar, and major sidebar panes visibly reflect the active theme
- Zen mode is visibly richer than Solid mode with the same unchanged chooser inputs
- remaining generic system color usage is limited to surfaces that are genuinely out of scope for the shipped parity target

### Workstream E, real persistence and real switching tests

**Owned by:** `maho_sidebar_spaces_interactive_test.cc`, optionally `browser/BUILD.gn`

**Purpose:** prove the end-to-end path with behavior tests instead of helper-only assertions.

**Function-level guidance:**

- Extend `maho_sidebar_spaces_interactive_test.cc` with tests that exercise actual persistence and actual switching through the existing sidebar flow.
- Add coverage for a create, save, close, reopen, and switch cycle so the test observes the real stored theme, not just a local preview object.
- Add a regression test for the command-action path if the current test file already covers command action surfaces. If it does not, note that no existing command-action-specific test file was found and keep the new coverage in the existing sidebar interactive test surface.
- Keep test assertions tied to behavior that proves the bridge, JSON, and tint path all work together.
- Add assertions or proof artifacts that distinguish Solid and Zen visual outcomes using the same unchanged chooser UI. If automated pixel assertions are too brittle, capture controlled snapshots or theme-state observables that prove richer Zen expression reached the visible chrome.

**Dependencies:** Workstreams A, B, and C should be in place before final assertions are written, because the test needs the real path, not a mocked one.

**Exit criteria:**

- real persistence is covered
- real switching is covered
- no hidden command-action-specific test file is assumed to exist
- reference-level fidelity is asserted by the available test surface rather than assumed from token rewiring alone

---

## Function-Level Edit Map

### `maho_sidebar_create_space_view.cc`

- Replace the malformed Zen and gradient JSON builder.
- Serialize canonical `SpaceTheme` JSON from the chooser inputs that already exist.
- Keep the chooser UI and controls unchanged.
- Preserve any existing input plumbing needed to feed the canonical builder.

### `maho_space_theme_io.cc`

- Keep schema checks strict.
- Preserve rejection of malformed or incomplete theme shapes.
- Do not add permissive parsing just to make bad payloads pass.

### `maho_space_profile_bridge.cc`

- Consolidate activation and retheme into the single bridge path.
- Make callers enter through one public bridge API.
- Ensure the bridge owns the actual state transition, not just part of the setup.

### `maho_command_action_handler.cc`

- Remove direct activation routing.
- Call into the bridge instead.
- Keep command-specific preprocessing only if it is required before the bridge call.

### `maho_space_create_page_handler.cc`

- Route direct activation through the bridge.
- Align page-handler behavior with the command-action path.

### `maho_color_mixer.cc`

- Expand the list of surfaces and tokens that receive tinted Maho colors.
- Preserve existing color-provider architecture.
- If dominant-color-based dark/light adjustment is introduced, keep it inside the existing state-plus-mixer architecture rather than inventing a second theme pipeline.

### `maho_sidebar_view.cc`

- Replace upstream system surface color reads with Maho-tinted token reads.
- Keep sidebar structure and view hierarchy unchanged.

### `maho_sidebar_space_dot_view.cc`

- Replace hardcoded active-dot fill logic with theme-aware color usage.
- Ensure active and inactive space indicators still remain legible and interactive while reflecting space identity.

### `maho_sidebar_top_bar_view.cc`

- Audit icon color IDs so visible top-bar controls can participate in the active theme identity where appropriate.

### Other sidebar panes that still consume generic system colors

- Audit and selectively retoken these high-visibility files if they materially affect the shipped theme identity:
  - `maho_sidebar_tab_list_view.cc`
  - `maho_sidebar_archive_view.cc`
  - `maho_sidebar_media_view.cc`
  - `maho_sidebar_downloads_view.cc`
  - `maho_sidebar_library_action_pane_view.cc`
  - `maho_sidebar_action_popover_view.cc`
- The goal is not a full redesign; it is to prevent obvious reference-breaking islands of generic system color inside an otherwise themed sidebar.

### `maho_space_theme_state.cc`

- Keep persisted state aligned with the canonical JSON shape.
- Confirm round-trip behavior matches the bridge and chooser output.
- If needed, add the minimum dominant-color or representative-accent logic required for gradient-aware native expression.

### `maho_sidebar_spaces_interactive_test.cc`

- Add real persistence and real switching tests.
- Add bridge-path regression coverage where the existing test surface can observe it.
- Validate that the chooser UI still behaves the same while the backend path changes.

---

## Validation Plan

### Required validation order

1. Schema and JSON validation first, because bad JSON must fail early.
2. Bridge routing validation second, because all activation and retheme must converge there.
3. Token propagation and sidebar tint validation third, because native surface colors must reflect the new theme state.
4. Real persistence and switching tests last, because they need the full path in place.

### Expected validation coverage

- create-space JSON generation produces canonical `SpaceTheme` payloads
- strict schema validation still rejects malformed input
- command-action and WebUI activation both use the bridge
- sidebar chrome adopts tinted Maho tokens
- persistence survives close and reopen
- switching applies the correct theme to the current space
- Zen and Solid produce visibly distinct outcomes without changing the chooser UI
- reference-level theme identity reaches the visible sidebar chrome instead of stopping at the root shell

### Suggested test surface

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`
- any existing nearby test helper files already used by that suite

### Build and run checks

- run the targeted interactive sidebar tests after the bridge and JSON seam is in place
- run the repository build command used for Chromium-side validation
- if a failing assertion appears in the command-action path, keep the fix inside the bridge route instead of adding a parallel special case
- generate at least one controlled proof capture comparing Solid vs Zen outcomes after the native theme path is complete

---

## Completion Gates

The implementation is complete only when all of the following are true:

1. `maho_sidebar_create_space_view.cc` emits canonical `SpaceTheme` JSON from the existing chooser inputs only.
2. `maho_space_theme_io.cc` remains strict and still rejects malformed schema.
3. `maho_space_profile_bridge.cc` is the single activation and retheme chokepoint.
4. `maho_command_action_handler.cc` no longer bypasses the bridge.
5. `maho_space_create_page_handler.cc` uses the same bridge path for direct activation.
6. `maho_color_mixer.cc` propagates tinted Maho tokens to the intended native surfaces.
7. `maho_sidebar_view.cc` no longer depends on upstream system surface colors for those surfaces.
8. High-visibility sidebar panes and space identity affordances no longer visually fall back to generic system colors where that would break reference-level parity.
9. Zen and Solid are visibly distinguishable in the shipped chrome without changing the chooser UI.
10. Tests prove real persistence and real switching, not just mocked state changes.
11. The native chooser UI contract is unchanged, with no new controls added.
12. All edits stay within this one PR.

---

## Explicit Non-Goals

- Do not change the visible native chooser UI.
- Do not add new controls, sliders, buttons, or theme modes.
- Do not rewrite the chooser editor.
- Do not weaken schema validation.
- Do not add a new command-action-specific test file just because one does not already exist.
- Do not split the work into separate PRs.
- Do not broaden scope into unrelated WebUI or sidebar redesign work.
- Do not claim reference parity if Zen mode only changes a single representative accent while the rest of the visible sidebar remains generic.

---

## Merge Order Guidance

1. Land the bridge seam and routing unification first.
2. Land the canonical JSON and strict schema work next.
3. Land native tint propagation and sidebar token migration after the contract is stable.
4. Land reference-fidelity render expression upgrades after token migration reveals the remaining generic islands.
5. Land tests after the real path exists, so the assertions cover the finished behavior instead of a temporary intermediate state.

This order keeps the highest-risk coupling points isolated and lets the parallel workstreams move without stepping on the same functions while still leaving room for the extra render-expression work needed for true reference fidelity.
