# Create Space Current Regressions Remediation Plan

**Created:** 2026-05-22  
**Status:** Draft — pending Momus review  
**Primary scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc/.h`  
**Secondary reference scope:** `maho-chromium/browser/ui/theme/maho_space_theme_io.cc`, `maho-chromium/browser/resources/maho_space_create/react/types.ts`, `maho-chromium/browser/maho_space_profile_bridge.cc`, `maho-chromium/browser/maho_browser_main_extra_parts.cc`

---

## 1. Objective

Fix the current native sidebar create-space regressions without changing the settled architecture:

1. Remove the incorrect extra UI elements in the Profile row.
2. Make the profile dropdown chevron visually correct and vertically aligned.
3. Rebuild the `Choose a Theme` row so it matches the reference as a filled block row, not plain button text.
4. Fix the create flow so creating a space actually activates the new space instead of appearing to no-op.
5. Fix the theme payload emitted by the native form so non-solid selections are not silently rejected.

This plan is for the **current regression pass only**. It is not a new architecture plan.

---

## 2. Non-Negotiable Constraints

1. **Native inline create form stays** in `MahoSidebarCreateSpaceView`.
2. **Theme picker stays WebUI** at `chrome://maho-space-create`, opened from the native form.
3. **No GN edits** as part of this remediation plan.
4. **No resurrection of the old native theme picker.**
5. **No speculative backend refactor** unless the targeted fixes below fail to resolve the create regression.
6. **Do not regress the existing name-field icon picker and post-create `set_space_icon` path.**

These constraints follow the permanent theme-picker ADR in `.sisyphus/plans/theme-picker-architecture-decision.md`.

---

## 3. Code-Grounded Current Diagnosis

### 3.1 UI defects in the native form

All of the current screenshot mismatches are directly caused by hard-coded glyphs and `MdTextButton` shortcuts in `maho_sidebar_create_space_view.cc`.

#### A. Profile row left icon should not exist

**File:** `maho_sidebar_create_space_view.cc:682-683`

```cpp
AddLabel(profile_row, u"☺", ...);
```

This literally inserts the left-side icon visible in the screenshot.

#### B. Profile row trailing info icon should not exist

**File:** `maho_sidebar_create_space_view.cc:701-702`

```cpp
AddLabel(profile_row, u"ⓘ", ...);
```

This literally inserts the far-right `i` icon visible in the screenshot.

#### C. Profile dropdown chevron is a text glyph, not a dedicated trailing affordance

**Files:**
- `maho_sidebar_create_space_view.cc:689-699`
- `maho_sidebar_create_space_view.cc:945-950`

```cpp
profile_button_->SetMinSize(gfx::Size(0, 22));
profile_button_->SetMaxSize(gfx::Size(0, 22));
...
profile_button_->SetText(selected_profile_label_ + u" ⌄");
```

The chevron is not an icon view. It is a text suffix inside a height-clamped `MdTextButton`, which is why it does not align cleanly.

#### D. `Choose a Theme` is built as plain text instead of a block row

**File:** `maho_sidebar_create_space_view.cc:704-714`

```cpp
theme_button_ = ... MdTextButton(..., u"🖌  Choose a Theme");
...->SetStyle(ui::ButtonStyle::kText);
```

The row is currently just an `MdTextButton` with embedded glyph text. That is structurally different from the reference block-row composition.

#### E. Theme feedback views are orphaned

**Files:**
- `maho_sidebar_create_space_view.cc:716-722`
- `maho_sidebar_create_space_view.cc:1108-1112`

`theme_summary_label_` and `theme_preview_dot_` are created hidden, updated later, but never made visible. They currently do not participate in the visible form and should not drive the remediation shape.

---

### 3.2 Creation-flow defects

#### F. Default-profile fallback uses the wrong id casing

**Files:**
- `maho_sidebar_create_space_view.cc:65`
- `maho_sidebar_create_space_view.cc:930-934`
- `maho_sidebar_create_space_view.cc:1128-1133`

Current code:

```cpp
constexpr char kDefaultProfileId[] = "Default";
...
fallback.id = kDefaultProfileId;
...
(profile_id.empty() || profile_id == "default")
    ? base::FilePath::FromASCII("Default")
    : base::FilePath::FromASCII("MahoProfile_" + profile_id);
```

If the fallback path is used, `selected_profile_id_` becomes `"Default"`, but the success path only treats lowercase `"default"` as the default-profile sentinel. That means the code can register the new space against `MahoProfile_Default`, which is the wrong profile path.

This is the most likely cause of “space created but nothing visibly happened.”

#### G. Native non-solid theme serialization is invalid for the theme validator

**Files:**
- `maho_sidebar_create_space_view.cc:186-203`
- `maho_space_theme_io.cc:96-132`
- `browser/resources/maho_space_create/react/types.ts:181-222`

Current native serializer emits:

```cpp
theme_dict.Set("type", ... ? "solid" : "gradient");
theme_dict.Set("color", ...);
```

But `ApplyThemeJsonToSpace()` validates non-solid themes by requiring `gradientColors`, `opacity`, and `texture`. The WebUI’s canonical serializer already emits `type: 'zen'` with the full non-solid shape. The native path is therefore hand-rolling an invalid payload for the non-solid case.

Result: even when creation succeeds, non-solid theme application can be silently dropped.

---

## 4. Remediation Strategy

The plan is intentionally serial at the high-risk seams and minimal elsewhere.

### Gate 1 — Remove accidental glyph UI and rebuild the row composition primitives

**Purpose:** Eliminate the screenshot mismatches caused by placeholder glyph labels and text-suffix affordances.

**Files:**
- `maho_sidebar_create_space_view.cc`
- `maho_sidebar_create_space_view.h` (only if new helper view members are needed)

**Required changes:**
1. Delete the hard-coded `☺` leading Profile glyph.
2. Delete the hard-coded `ⓘ` trailing Profile glyph.
3. Stop appending `⌄` to the profile button text.
4. Stop embedding `🖌` inside the theme-row text label.

**Acceptance criteria:**
- No extra glyphs remain in the Profile row.
- No glyph-based chevron remains in the profile label text.
- No glyph-based icon remains embedded in the theme row label.

---

### Gate 2 — Rebuild the Profile row to match the reference silhouette

**Purpose:** Make the Profile row visually match the reference and avoid `MdTextButton` baseline drift.

**Files:**
- `maho_sidebar_create_space_view.cc`
- `maho_sidebar_create_space_view.h`

**Implementation direction:**
1. Keep the outer filled row container (`40dp`, rounded background).
2. Keep the left text label `Profile`.
3. Replace the current profile-chip composition with a structure that gives explicit control over the trailing chevron.

Preferred implementation order:
- **Option A (preferred):** Replace the current `MdTextButton` chip with a small custom `views::Button`/composed view that contains:
  - a label view for `selected_profile_label_`
  - a separate trailing chevron view
  - explicit `BoxLayout` center alignment
- **Option B (acceptable fallback):** Keep `profile_button_` but remove the fixed-height clamp and move the chevron into a sibling child view rather than button text.

**Acceptance criteria:**
- Profile row contains only the row label plus the dropdown control.
- Chevron is vertically centered as a dedicated trailing affordance.
- Clicking the control still opens the same `SimpleMenuModel` profile menu.

---

### Gate 3 — Rebuild `Choose a Theme` as a real block row

**Purpose:** Match the reference composition and remove the “plain text button” look.

**Files:**
- `maho_sidebar_create_space_view.cc`
- `maho_sidebar_create_space_view.h` (only if a new concrete button/view type is introduced)

**Implementation direction:**
1. Replace the `MdTextButton` `theme_button_` with a composed block row that uses:
   - filled rounded background
   - explicit horizontal layout
   - a dedicated leading icon/tile view
   - a title label for `Choose a Theme`
   - a trailing disclosure affordance
2. Keep `OnThemePressed()` as the action callback.
3. Do **not** reintroduce extra summary rows or secondary text that are not in the reference default state.
4. If visible feedback for selected theme is needed, keep it inside the same row silhouette (for example through the leading tile/preview), not as a separate always-visible block below.

**Acceptance criteria:**
- The row reads as a filled block row, not a text-only button.
- The row opens the existing WebUI theme dialog on click.
- Default-state silhouette matches the reference screenshot.

---

### Gate 4 — Fix the default-profile mapping so create actually activates the new space

**Purpose:** Remove the most likely cause of the observed “space not created” behavior.

**Files:**
- `maho_sidebar_create_space_view.cc`

**Required changes:**
1. Normalize the create-form fallback profile id to the same sentinel used by the rest of the codepath (`"default"`).
2. Make the success path defensive by treating both `"default"` and legacy `"Default"` as the default-profile sentinel when building the target `profile_path`.
3. Keep the resulting Chromium profile path as `Default` on disk.

**Recommended minimal implementation:**
- Change `kDefaultProfileId` to lowercase `"default"`.
- Make the `OnSpaceCreated()` mapping tolerant to both lowercase and legacy capitalized values.

**Acceptance criteria:**
- The fallback profile path resolves to `Default`, not `MahoProfile_Default`.
- A newly created space is registered and switched using the correct profile path.

---

### Gate 5 — Make native theme serialization match the WebUI contract

**Purpose:** Stop non-solid theme application from being silently rejected.

**Files:**
- `maho_sidebar_create_space_view.cc`
- Reference only: `browser/resources/maho_space_create/react/types.ts`

**Required changes:**
1. Replace the current ad-hoc non-solid serializer with a payload shape that matches the WebUI contract.
2. For the non-solid/native-zen case, emit a valid theme object with:
   - `type: "zen"`
   - `gradientColors`
   - `opacity`
   - `texture`
   - optional `harmony: "analogous"`
3. Mirror the stop-generation logic from the WebUI serializer closely enough that the native form and WebUI agree on the same conceptual theme.

**Deliberate non-goal:**
- Do **not** expand this into a generic cross-surface serializer refactor in the first pass. The first pass should make the native create path valid and compatible.

**Acceptance criteria:**
- `ApplyThemeJsonToSpace()` no longer rejects the native non-solid payload.
- The created space keeps the selected theme rather than silently falling back.

---

### Gate 6 — Keep the fix narrow unless the create bug survives

**Purpose:** Prevent scope creep into speculative refactors.

**Potential follow-up only if Gate 4 does not resolve runtime creation:**
1. Move `BuildProfilesOnPool()` off the UI thread to avoid synchronous FFI in the open/reset path.
2. Inspect whether the bridge is receiving the new space but the sidebar refresh path is not surfacing it.

These are **fallback investigations**, not part of the first implementation pass.

---

## 5. File-by-File Planned Changes

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

Primary implementation file.

Planned edit clusters:
1. Remove placeholder glyph labels in the Profile row.
2. Rebuild the Profile control composition.
3. Rebuild the Theme row composition.
4. Normalize default-profile fallback handling.
5. Replace invalid non-solid theme serialization.
6. Keep `OnThemePressed()`, icon picker flow, `CreateSpaceOnPool()`, and post-create `set_space_icon` behavior intact except where required by Gates 4-5.

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.h`

Only update if the row composition change requires:
- new helper view forward declarations
- different member pointer types
- small helper methods for dedicated row/button views

### Files explicitly not targeted in the first pass

- `maho_space_theme_io.cc` — validator remains the contract authority; first pass fixes the emitted payload instead.
- `maho_space_profile_bridge.cc` — use as-is unless post-fix evidence shows the bridge path is still wrong.
- GN files — out of scope.

---

## 6. Verification Plan

### 6.1 Static verification

1. `lsp_diagnostics` on changed files — zero errors.
2. Confirm no dead members remain if the old theme summary/dot plumbing is removed or repurposed.

### 6.2 Narrow build verification

Compile the touched sidebar object first:

```bash
autoninja -C out/Default obj/maho/browser/ui/views/sidebar/maho_sidebar_library/maho_sidebar_create_space_view.o
```

### 6.3 Runtime verification once the app is launchable

Manual checklist:
1. Create form shows no Profile-row left icon.
2. Create form shows no Profile-row trailing `i` icon.
3. Profile dropdown chevron is vertically aligned.
4. `Choose a Theme` renders as a filled row and opens the WebUI theme picker.
5. Creating a space with the fallback/default profile actually switches to or reveals the new space.
6. The chosen non-solid theme persists on the created space.
7. Name-field Unicode icon picker still works and the selected icon still propagates after create.

### 6.4 Canonical full-build verification

```bash
python3 maho-chromium/build/scripts/build_maho.py
```

If this build is blocked by an unrelated file outside the create-space cluster, record that blocker separately rather than folding it into this remediation scope.

---

## 7. Definition of Done

This regression pass is done when all of the following are true:

1. The Profile row no longer contains accidental glyph icons.
2. The Profile dropdown control has a properly aligned trailing chevron.
3. `Choose a Theme` is a reference-style block row rather than a text-only button.
4. The default-profile fallback no longer routes through `MahoProfile_Default`.
5. Creating a space visibly results in the new space being registered/switched instead of appearing to no-op.
6. Native non-solid theme serialization is valid and survives `ApplyThemeJsonToSpace()`.
7. The Unicode icon picker path for the name field still works after the row/layout cleanup.

---

## 8. Recommended Execution Order

1. Gate 1 — remove accidental glyph UI
2. Gate 2 — rebuild Profile row composition
3. Gate 3 — rebuild Theme row composition
4. Gate 4 — normalize default-profile create path
5. Gate 5 — fix native non-solid theme serialization
6. Verify object compile
7. Verify runtime flow when build/launch environment allows

This order keeps the visible UI cleanup separate from the creation-path correctness fix, while still landing both in the same narrow file cluster.
