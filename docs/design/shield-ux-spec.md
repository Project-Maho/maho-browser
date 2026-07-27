# Maho Shield — Utility Row + Shield Bubble UX Spec

Status: design spec, ready to implement
Surfaces: native Chromium Views only (no WebUI, no new `.grd`)
Owning files:

- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.{h,cc}`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc`
- `maho-chromium/browser/ui/views/shields/maho_shield_bubble_view.{h,cc}`
- Tests: `maho_location_bar_utility_panel_provider_unittest.cc`, `maho_shield_bubble_interactive_uitest.cc`

Design direction: Brave Shields' legibility (site identity + one hero metric + one switch) with Maho/Arc restraint (tonal band, no chrome, no dialog furniture).

---

## 0. What the current UI gets wrong

Concrete defects, tied to the code that produces them.

### Utility row (`BuildBlockAdsAndTrackersRowForOrigin`, provider.cc:435)

1. **Title is a setting name, not a destination.** `u"Block Ads & Trackers"` is the exact same string as the bubble's toggle label. A row whose title equals a switch label reads as a switch. The row must be named for the place it opens.
2. **Subtitle crams two facts into one line.** `u"0 blocked · Shields on"` (provider.cc:321) makes the count a mid-sentence token. The count is the single most glanceable value and it is 6 pixels of gray text between a `0` and a `·`.
3. **Icon never changes with state.** Always `kShieldHalfIcon` at `kColorSysOnSurfaceSubtle`, 12dp (view.cc:224, 256). On, off, extension-managed and unavailable are visually identical.
4. **Disclosure affordance is invisible.** 12dp chevron at `kColorSysOnSurfaceSubtle`, no hover reaction of its own (view.cc:308). Hover only swaps the row background `kColorSysSurface1 → kColorSysSurface2`, a near-imperceptible delta.
5. **Disabled = 55% layer opacity on the whole row** (view.cc:188). Text, icon and chevron all fade together; the row looks like a rendering bug rather than an unavailable feature.
6. **Icon is 12dp** — same size as the chevron. Nothing anchors the row.

### Bubble (`MahoShieldBubbleView`)

7. **Brand line steals the title slot.** `u"Maho Shield"` is `STYLE_BODY_3_MEDIUM` / `kColorSysOnSurface`; the host is `STYLE_BODY_5` / `kColorSysOnSurfaceVariant` beneath it (bubble.cc:180–192). The user already knows which browser they are in. **The site is the subject** and it is currently the smallest, faintest text in the header.
8. **The metric is a gray box inside a gray box.** `kColorSysSurface1` panel on the bubble's own surface, `kRadiusMd`, centered text (bubble.cc:200–223). Centered text inside a rounded gray rectangle is the universal shape of a settings card. That is precisely why it "looks like a generic dark settings card."
9. **Zero has full visual weight.** `0` renders at `STYLE_HEADLINE_1` / `kColorSysOnSurface` — maximum emphasis on the least informative value.
10. **The count is shown even when it is meaningless.** With shields off for the site or blocking managed by an extension, the hero still prints a number that no longer describes anything the Shield is doing.
11. **Status pill is decorative, not informative.** 4dp dot (`kStatusIndicatorSize = tokens::kSpace1`) — below the perceptual floor at typical DPI — with `kColorSysOnSurfaceVariant` text, centered under the number.
12. **The toggle floats.** No divider, no separation from the metric panel, 4dp vertical inset (`kToggleRowInsets`). It reads as a fourth line of the card, not as the one control.
13. **Footer is a bare right-aligned text link** with `CreateEmptyBorder(gfx::Insets())` (bubble.cc:306) — zero padding, so the hit target is the glyph bounds, flush to the bubble edge. Small target, weak affordance, and the alignment (right) contradicts every other element (centered/left).
14. **Everything is the same surface.** Bubble body, metric panel, status pill: `surface`, `surface1`, `surface2`. Three greys, no state color anywhere except a 20dp primary-tinted shield glyph. There is no visual difference between "you are protected" and "protection is off" beyond one sentence of body text — hence "what changed?".

---

## 1. Shared state model

Both surfaces derive from the same four inputs already available:

| Input | Source |
|---|---|
| `mode` | `MahoShieldSiteState::GetEffectiveContentBlockingMode()` — `0` native, `1` extension, `2` off, `-1`/null unavailable |
| `is_excepted` | `MahoShieldSiteState::IsSiteExceptedForOrigin(origin)` |
| `blocked_count` | `MahoAdBlockTabHelper::blocked_count()` |
| `capabilities.site_exception_toggle` | `MahoShieldSiteState::GetCapabilities()` |

Collapse them into **one enum, defined once, shared by both surfaces**. Put it in the provider header so the bubble can include it:

```cpp
// maho_location_bar_utility_panel_provider.h
namespace maho {

enum class MahoShieldPresentationState {
  kActive,        // mode == 0, !is_excepted        — blocking, count is meaningful
  kSiteOff,       // mode == 0,  is_excepted        — user paused this site
  kExtension,     // mode == 1                      — an extension owns blocking
  kGloballyOff,   // mode == 2                      — content blocking off in settings
  kUnavailable,   // mode < 0, no state, no origin  — nothing to report
};

MahoShieldPresentationState ResolveShieldPresentationState(int mode,
                                                           bool is_excepted);
}  // namespace maho
```

Every color, icon, and string below is a pure function of this enum plus `blocked_count`. No surface may invent a sixth state.

**Never derive presentation from `data_state`.** `MahoLocationBarUtilityPanelDataState` stays as-is for the generic row plumbing; it does not carry Shield semantics.

---

## 2. Surface 1 — Utility panel Shield row

### 2.1 Anatomy

```
 ← 304 panel, 14 outer padding → row content width 276 →
┌──────────────────────────────────────────────────────────┐
│  ╭────╮                                                  │
│  │ 🛡  │  Shields                              ╭────╮     │  44
│  ╰────╯  128 blocked on this site              │128 │  ›  │
│   28dp                                         ╰────╯     │
└──────────────────────────────────────────────────────────┘
   ↑10  ↑10   ↑                                   ↑      ↑8
  pad  gap  text column (flex 1)                badge  chevron 14
```

Two-line row, **44dp tall** (the other settings rows stay at 38dp — the Shield row is deliberately the tallest item in the zone, because it is the only one that opens a detail surface).

### 2.2 Layout, exact

| Element | Spec |
|---|---|
| Row | height 44, corner radius `kSettingsRowCornerRadius` (10), horizontal padding 10 (`kSettingsRowHorizontalPadding`) |
| Icon tile | 28×28, radius `tokens::kRadiusSm` (8), centered glyph |
| Glyph | 16dp vector icon inside tile |
| Tile → text gap | 10 |
| Text column | flex 1, vertical, 2dp inter-line gap, `CrossAxisAlignment::kStart`, main axis centered |
| Title | `views::style::STYLE_BODY_4_MEDIUM`, `ELIDE_TAIL`, single line |
| Subtitle | `views::style::STYLE_BODY_5`, `ELIDE_TAIL`, single line |
| Text → badge gap | 8 |
| Count badge | height 20, min width 20, radius `tokens::kRadiusPill`, horizontal padding 6, `STYLE_BODY_5_MEDIUM` |
| Badge → chevron gap | 8 |
| Chevron | `kChevronRightIcon`, **14dp** (up from 12) |

The count badge is present **only in `kActive` with `blocked_count > 0`**. In every other state the badge is omitted and the chevron sits 8dp from the text column.

### 2.3 Per-state visuals

| State | Glyph | Tile background | Glyph color | Badge |
|---|---|---|---|---|
| `kActive`, count > 0 | `kShieldCheckIcon` | `kColorSysTonalContainer` | `kColorSysOnTonalContainer` | shown: bg `kColorSysTonalContainer`, text `kColorSysOnTonalContainer` |
| `kActive`, count == 0 | `kShieldCheckIcon` | `kColorSysTonalContainer` | `kColorSysOnTonalContainer` | hidden |
| `kSiteOff` | `kShieldXIcon` | `kColorSysSurface2` | `kColorSysOnSurfaceSubtle` | hidden |
| `kGloballyOff` | `kShieldXIcon` | `kColorSysSurface2` | `kColorSysOnSurfaceSubtle` | hidden |
| `kExtension` | `kShieldHalfIcon` | `kColorSysSurface2` | `kColorSysOnSurfaceSubtle` | hidden |
| `kUnavailable` | `kShieldQuestionIcon` | `kColorSysSurface2` | `kColorSysOnSurfaceSubtle` | hidden |

Title color is `kColorSysOnSurface` in all enabled states; subtitle `kColorSysOnSurfaceSubtle`; chevron `kColorSysOnSurfaceSubtle`.

Only the four shipped shield icons are used: `shield_check`, `shield_half`, `shield_x`, `shield_question` (`browser/ui/views/maho_lucide_icons/BUILD.gn:34–37`). **Do not add new `.icon` files for this work.**

### 2.4 Copy — exact strings

Title in every state: **`Shields`**

One word. It names a destination, not an action. The verb phrase "Block Ads & Trackers" moves to the bubble's toggle, where it belongs, and stops competing with it.

| State | Subtitle |
|---|---|
| `kActive`, count == 0 | `No trackers blocked yet` |
| `kActive`, count == 1 | `1 tracker blocked on this site` |
| `kActive`, count > 1 | `<N> trackers blocked on this site` |
| `kSiteOff` | `Paused for this site` |
| `kGloballyOff` | `Turned off in settings` |
| `kExtension` | `Managed by an extension` |
| `kUnavailable`, no page | `No page loaded` |
| `kUnavailable`, other | `Status unavailable` |

`<N>` uses `base::FormatNumber(blocked_count)` (grouped: `1,284`). Clamp the *badge* text at `99+` when `blocked_count > 99`; the subtitle always shows the true grouped number.

Copy variants considered and rejected, for the record:

- `Shields · 128 blocked` — reintroduces the two-facts-one-line problem.
- `Protection` — vaguer than "Shields", and loses continuity with the bubble title.
- `128 blocked` as the title — makes a volatile number the row's identity; the row jumps every navigation.

### 2.5 Interaction states

| State | Row background | Chevron color |
|---|---|---|
| Rest | `kColorSysSurface1` | `kColorSysOnSurfaceSubtle` |
| Hover | `kColorSysSurface2` | `kColorSysOnSurface` |
| Pressed | `kColorSysSurface3` | `kColorSysOnSurface` |
| Focused (keyboard) | `kColorSysSurface2` + `views::FocusRing` (already installed) | `kColorSysOnSurface` |
| Disabled (`kUnavailable`, `kGloballyOff`) | `kColorSysSurface1`, no hover/press reaction | `kColorSysOnSurfaceSubtle`, chevron **hidden** |

**Two changes to the shared row view are required here:**

1. Add a pressed background. `MahoLocationBarUtilityPanelActionView` currently has only `selection_background_` (view.cc:193). Add a second overlay `pressed_background_`, shown on `OnMousePressed` and hidden on `OnMouseReleased`/`OnMouseCaptureLost`, or repaint `selection_background_` with `kColorSysSurface3`. The latter is fewer views — prefer it.
2. Chevron must react. Store the `views::ImageView*` (currently a bare local, view.cc:309) as `disclosure_view_` and re-`SetImage` with the hovered color from `SetHighlighted()`.

**Replace the 0.55 layer opacity for disabled rows** (view.cc:188–191). Opacity on the layer fades the whole row including the background, which is what makes it look broken. Instead:

- title → `kColorSysStateDisabled`
- subtitle → `kColorSysStateDisabled`
- tile background → `kColorSysSurface1` (flat, no contrast against the row)
- glyph → `kColorSysStateDisabled`
- chevron → not added to the view tree at all

This is a behavior change for the other disabled rows in the settings zone too. That is intended — it is the same fix everywhere.

### 2.6 Accessibility

| Property | Value |
|---|---|
| Role | `ax::mojom::Role::kButton` (unchanged; the row has no inline toggle) |
| Name | `"Shields, <subtitle>"` — e.g. `"Shields, 128 trackers blocked on this site"` |
| Tooltip | same string as Name |
| Description | `kActive`/`kSiteOff` only: `"Opens Shield details for <host>"` |
| Focusable | yes when enabled; `FocusBehavior::NEVER` when disabled (already correct, view.cc:164) |
| Disabled states | Name still carries the reason: `"Shields, managed by an extension"` |

Use `GetViewAccessibility().SetDescription(...)`; do **not** append the description to the name — screen readers already announce the button role and the description separately.

### 2.7 Provider changes needed

`MahoLocationBarUtilityPanelAction` gains three optional fields (all default-off, so no other row changes):

```cpp
  // Shield row (and any future detail-opening row): renders the leading icon
  // inside a filled tile instead of as a bare glyph.
  bool use_icon_tile = false;
  ui::ColorId icon_tile_background = ui::kColorSysSurface2;
  ui::ColorId icon_color = ui::kColorSysOnSurfaceSubtle;

  // Trailing pill. Empty = no badge.
  std::u16string trailing_badge_text;
```

`BuildBlockAdsAndTrackersRowForOrigin` is rewritten to:

1. resolve `MahoShieldPresentationState`,
2. set `title = u"Shields"`, `subtitle = ShieldRowSubtitle(state, blocked_count)`,
3. set `icon = &ShieldIconForState(state)`, `use_icon_tile = true`, tile/glyph colors per §2.3,
4. set `trailing_badge_text` when `state == kActive && blocked_count > 0`,
5. keep `anchor_callback = ShowShieldBubbleForUtilityRow`, `show_disclosure_indicator = true`, `close_after_activate = true` — **unchanged**,
6. set `enabled = false` for `kUnavailable` and `kGloballyOff` (no bubble to open that the user can act on),
7. **keep `enabled = true` and the bubble callback for `kSiteOff` and `kExtension`** — the bubble is exactly where the user un-pauses or reads who's managing it. This is a change: today `mode != 0` returns a dead, unclickable row (provider.cc:456–464).

Keep the function name and both existing entry points so call sites and the `internal::` test surface are untouched.

### 2.8 Wireframes, all states

```
kActive, count > 0
┌────────────────────────────────────────────────────┐
│ ╭──╮ Shields                          ╭───╮        │
│ │🛡✓│ 128 trackers blocked on this site│128│  ›     │
│ ╰──╯                                  ╰───╯        │
└────────────────────────────────────────────────────┘
   tonal tile                          tonal badge

kActive, count == 0
┌────────────────────────────────────────────────────┐
│ ╭──╮ Shields                                       │
│ │🛡✓│ No trackers blocked yet                  ›    │
│ ╰──╯                                               │
└────────────────────────────────────────────────────┘

kSiteOff
┌────────────────────────────────────────────────────┐
│ ╭──╮ Shields                                       │
│ │🛡✗│ Paused for this site                     ›    │
│ ╰──╯  (neutral tile)                               │
└────────────────────────────────────────────────────┘

kExtension
┌────────────────────────────────────────────────────┐
│ ╭──╮ Shields                                       │
│ │🛡◐│ Managed by an extension                  ›    │
│ ╰──╯                                               │
└────────────────────────────────────────────────────┘

kGloballyOff / kUnavailable  (disabled: no chevron, no hover)
┌────────────────────────────────────────────────────┐
│ ╭──╮ Shields                                       │
│ │🛡✗│ Turned off in settings                        │
│ ╰──╯  (flat tile, kColorSysStateDisabled text)     │
└────────────────────────────────────────────────────┘
```

---

## 3. Surface 2 — Shield bubble

### 3.1 Structural change

The bubble stops being *a stack of cards on a surface* and becomes *a tinted status band with a control list below it*. That single structural move is what separates it from a settings dialog.

- Root layout insets → **`gfx::Insets()`** (zero). Sections own their own padding. This is required for the full-bleed hero.
- `kBubbleWidth` stays **296**.
- `set_corner_radius(tokens::kRadiusLg)` stays (16).
- Hero band background uses `views::CreateRoundedRectBackground(color, /*top_radius=*/tokens::kRadiusLg, /*bottom_radius=*/0)` — the two-radius overload at `ui/views/background.h:112`. Top corners match the bubble's own 16dp exactly; bottom edge is square so the band reads as a band, not a card.
- **Delete the `kColorSysSurface1` metric panel entirely.** No nested rounded rectangle.

### 3.2 Wireframe — `kActive`, count > 0

```
┌────────────────────────────────────────────────┐ ← radius 16
│▓▓▓▓▓▓▓▓▓▓▓ kColorSysTonalContainer ▓▓▓▓▓▓▓▓▓▓▓▓│
│▓                                              ▓│  16 top pad
│▓  🛡  example.com                             ▓│  16dp icon + BODY_3_MEDIUM
│▓                                              ▓│  10
│▓  128                                         ▓│  HEADLINE_1, left
│▓  trackers & ads blocked                      ▓│  BODY_4
│▓                                              ▓│  12
│▓  ╭──────────────────────────────╮            ▓│
│▓  │ ● Shields are up on this site│            ▓│  pill, h 24
│▓  ╰──────────────────────────────╯            ▓│
│▓                                              ▓│  14 bottom pad
├────────────────────────────────────────────────┤ ← 1dp kColorSysDivider
│                                                │
│  Block ads & trackers                 ╭─────╮  │  52 row
│  On for example.com                   │  ●──│  │  ToggleButton
│                                       ╰─────╯  │
├────────────────────────────────────────────────┤ ← 1dp kColorSysDivider
│  Shield settings                            ›  │  44 row
└────────────────────────────────────────────────┘
   ↑16                                        ↑16
```

Total height ≈ 242dp. Compact.

### 3.3 Hero band — exact spec

| Element | Spec |
|---|---|
| Insets | `gfx::Insets::TLBR(16, 16, 14, 16)` |
| Layout | vertical `BoxLayout`, `CrossAxisAlignment::kStretch`, inter-child spacing 0 (each child carries its own top border for precise gaps) |
| Identity row | horizontal, `CrossAxisAlignment::kCenter`, spacing `tokens::kSpace2` (8) |
| ↳ glyph | 16dp, state icon (§3.5) |
| ↳ host label | `STYLE_BODY_3_MEDIUM`, `ALIGN_LEFT`, `ELIDE_MIDDLE`, flex 1 |
| Gap | 10 (top border on the number label) |
| Number | `STYLE_HEADLINE_1`, `ALIGN_LEFT` |
| Gap | 2 |
| Caption | `STYLE_BODY_4`, `ALIGN_LEFT` |
| Gap | 12 |
| Status pill | horizontal, self-sized (wrap in a `BoxLayout` container with `MainAxisAlignment::kStart` so the pill hugs its content and left-aligns) |
| ↳ pill insets | `gfx::Insets::VH(4, 10)`, radius `tokens::kRadiusPill` |
| ↳ dot | **6×6**, radius pill (up from 4 — `kStatusIndicatorSize` must change) |
| ↳ dot → label gap | 6 |
| ↳ label | `STYLE_BODY_5` |

**Everything is left-aligned.** The current centered metric is the strongest "settings card" signal in the bubble; left alignment plus a tinted band reads as a status readout.

### 3.4 Hero colors, per state

| State | Band background | Host / Number | Caption | Pill bg | Pill label | Dot |
|---|---|---|---|---|---|---|
| `kActive`, count > 0 | `kColorSysTonalContainer` | `kColorSysOnTonalContainer` | `kColorSysOnTonalContainer` | `kColorSysSurface` | `kColorSysOnSurface` | `kColorSysPrimary` |
| `kActive`, count == 0 | `kColorSysTonalContainer` | host `kColorSysOnTonalContainer`; **number `kColorSysOnSurfaceVariant`** | `kColorSysOnTonalContainer` | `kColorSysSurface` | `kColorSysOnSurface` | `kColorSysPrimary` |
| `kSiteOff` | `kColorSysSurface2` | `kColorSysOnSurface` | `kColorSysOnSurfaceVariant` | `kColorSysSurface3` | `kColorSysOnSurface` | `kColorSysOnSurfaceSubtle` |
| `kExtension` | `kColorSysSurface2` | `kColorSysOnSurface` | `kColorSysOnSurfaceVariant` | `kColorSysSurface3` | `kColorSysOnSurface` | `kColorSysOnSurfaceSubtle` |
| `kGloballyOff` | `kColorSysSurface2` | `kColorSysOnSurface` | `kColorSysOnSurfaceVariant` | `kColorSysSurface3` | `kColorSysOnSurface` | `kColorSysOnSurfaceSubtle` |
| `kUnavailable` | `kColorSysSurface2` | `kColorSysOnSurface` | `kColorSysOnSurfaceVariant` | `kColorSysSurface3` | `kColorSysOnSurface` | `kColorSysOnSurfaceSubtle` |

Two colored things exist in the whole bubble: the tonal band and the primary dot, and both appear **only when protection is actually running**. Opening the bubble on a protected site vs. a paused site is now a difference you see before you read.

**Deemphasized zero** (row 2): the `0` drops to `kColorSysOnSurfaceVariant` while the band stays tonal. Protection is on; there is simply nothing to report yet. The layout does not change, so the bubble does not resize between 0 and 1.

### 3.5 Hero content, per state

| State | Glyph | Number | Caption | Pill text |
|---|---|---|---|---|
| `kActive`, count == 0 | `kShieldCheckIcon` | `0` | `trackers & ads blocked` | `Shields are up on this site` |
| `kActive`, count == 1 | `kShieldCheckIcon` | `1` | `tracker or ad blocked` | `Shields are up on this site` |
| `kActive`, count > 1 | `kShieldCheckIcon` | `FormatNumber(n)` | `trackers & ads blocked` | `Shields are up on this site` |
| `kSiteOff` | `kShieldXIcon` | `—` (em dash) | `Blocking paused here` | `You turned Shields off for this site` |
| `kExtension` | `kShieldHalfIcon` | `—` | `Handled by an extension` | `An extension is blocking content` |
| `kGloballyOff` | `kShieldXIcon` | `—` | `Blocking is off` | `Turn Shields on in settings` |
| `kUnavailable` | `kShieldQuestionIcon` | `—` | `No data for this page` | `Shield status unavailable` |

The em dash replaces the count wherever the count does not describe Maho's own blocking. Printing `0` next to "managed by an extension" is a lie about what happened on the page; `—` is not.

`FormatNumber` clamps: `blocked_count > 99999` renders `99,999+`.

Caption is a **noun phrase, never a sentence** — it is the unit label under a numeral, and the pill carries the sentence. Do not merge them.

### 3.6 Toggle row — exact spec

Rendered only when `capabilities.site_exception_toggle` is true (unchanged gate, bubble.cc:261).

| Element | Spec |
|---|---|
| Row height | 52, insets `gfx::Insets::VH(0, 16)` |
| Layout | horizontal, `CrossAxisAlignment::kCenter`, spacing 12 |
| Text column | vertical, 2dp gap, flex 1 |
| ↳ label | `Block ads & trackers`, `STYLE_BODY_4_MEDIUM`, `kColorSysOnSurface` |
| ↳ sublabel | state string below, `STYLE_BODY_5`, `kColorSysOnSurfaceVariant` |
| Control | `views::ToggleButton`, right-aligned |
| Rest bg | transparent |
| Hover bg | `kColorSysSurface1`, radius 0 (full-bleed strip) |
| Pressed bg | `kColorSysSurface2` |
| Click target | the whole row toggles, matching the utility panel's inline-toggle behavior |

Sublabel copy:

| Condition | Sublabel |
|---|---|
| toggle on | `On for <host>` |
| toggle off | `Off for <host>` |

`<host>` is the elided host (`ELIDE_TAIL` on the sublabel). This is the only place the site scope of the switch is stated — without it, users read the switch as global. That ambiguity is the most likely source of a support complaint from this surface.

Accessibility:

- Row: `ax::mojom::Role::kGroup`, no name of its own.
- `ToggleButton`: accessible name `"Block ads & trackers, on for example.com"` / `"…, off for example.com"`. Update it inside `OnTogglePressed` after the effective state is read back.
- The two labels: `GetViewAccessibility().SetIsIgnored(true)` so the switch is announced once, not three times.

**Do not add a second toggle, a "for this site / everywhere" segmented control, or an "Advanced" expander.** One switch.

### 3.7 Settings row — exact spec

Replaces the right-aligned `LabelButton` link.

| Element | Spec |
|---|---|
| Row height | 44, insets `gfx::Insets::VH(0, 16)` |
| Label | left, `STYLE_BODY_4`, `kColorSysOnSurface`, flex 1 |
| Trailing | `kArrowUpRightIcon`, 14dp, `kColorSysOnSurfaceSubtle` — it leaves the bubble and opens a settings pane, so the out-arrow is more truthful than a chevron |
| Rest bg | transparent |
| Hover bg | `kColorSysSurface1` |
| Pressed bg | `kColorSysSurface2` |
| Hover trailing | `kColorSysOnSurface` |
| Focus | `views::FocusRing`, inset 2 |
| Role | `kButton` |

Label copy:

| State | Label | Target pane |
|---|---|---|
| `kExtension` | `Extension settings` | `"extensions"` |
| all others | `Shield settings` | `"content-blocker"` |

Both already implemented in `OpenSettings()` (bubble.cc:426–435) — keep that logic, only the presentation changes.

### 3.8 Dividers

Two 1dp full-bleed separators, `kColorSysDivider`:

1. hero band → toggle row (or → settings row when the toggle is absent)
2. toggle row → settings row (omit when the toggle is absent)

Implement as `views::View` with `SetPreferredSize({0, 1})` and a solid background; a bottom `Border` on the preceding section also works and is one fewer view — either is acceptable, be consistent.

### 3.9 Hero accessibility

The hero is a readout, not four controls. Compose one node:

- Hero container: `Role::kGroup`, name = `"<host>. <number> <caption>. <pill text>."`
  e.g. `"example.com. 128 trackers & ads blocked. Shields are up on this site."`
  For `—` states, drop the numeral: `"example.com. Blocking paused here. You turned Shields off for this site."`
- Glyph, host, number, caption, dot, pill label: all `SetIsIgnored(true)`.

Bubble root keeps its default dialog semantics; set the widget's accessible title to `"Shields for <host>"`.

### 3.10 Bubble wireframes, remaining states

```
kActive, count == 0                       kSiteOff
┌──────────────────────────────┐          ┌──────────────────────────────┐
│▓▓▓ tonal ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│          │░░░ surface2 ░░░░░░░░░░░░░░░░░│
│▓ 🛡✓ example.com            ▓│          │░ 🛡✗ example.com            ░│
│▓                            ▓│          │░                            ░│
│▓ 0        ← onSurfaceVariant▓│          │░ —                          ░│
│▓ trackers & ads blocked     ▓│          │░ Blocking paused here       ░│
│▓ ╭─────────────────────────╮▓│          │░ ╭─────────────────────────╮░│
│▓ │● Shields are up on this │▓│          │░ │○ You turned Shields off │░│
│▓ │  site                   │▓│          │░ │  for this site          │░│
│▓ ╰─────────────────────────╯▓│          │░ ╰─────────────────────────╯░│
├──────────────────────────────┤          ├──────────────────────────────┤
│ Block ads & trackers   ╭───╮ │          │ Block ads & trackers ╭───╮   │
│ On for example.com     │ ●─│ │          │ Off for example.com  │─●  │  │
├──────────────────────────────┤          ├──────────────────────────────┤
│ Shield settings            ↗ │          │ Shield settings            ↗ │
└──────────────────────────────┘          └──────────────────────────────┘

kExtension                                kGloballyOff
┌──────────────────────────────┐          ┌──────────────────────────────┐
│░ 🛡◐ example.com            ░│          │░ 🛡✗ example.com            ░│
│░ —                          ░│          │░ —                          ░│
│░ Handled by an extension    ░│          │░ Blocking is off            ░│
│░ ╭─────────────────────────╮░│          │░ ╭─────────────────────────╮░│
│░ │○ An extension is        │░│          │░ │○ Turn Shields on in     │░│
│░ │  blocking content       │░│          │░ │  settings               │░│
│░ ╰─────────────────────────╯░│          │░ ╰─────────────────────────╯░│
├──────────────────────────────┤          ├──────────────────────────────┤
│ Extension settings         ↗ │          │ Shield settings            ↗ │
└──────────────────────────────┘          └──────────────────────────────┘
   (no toggle row, no 2nd divider)           (no toggle row)

kUnavailable
┌──────────────────────────────┐
│░ 🛡? example.com            ░│   ← host may be empty; then use the
│░ —                          ░│      raw origin, and if that is empty
│░ No data for this page      ░│      the bubble must not be shown at all
│░ ╭─────────────────────────╮░│      (ShowShieldBubbleForUtilityRow
│░ │○ Shield status          │░│       already early-returns on empty
│░ │  unavailable            │░│       origin — keep that guard)
│░ ╰─────────────────────────╯░│
├──────────────────────────────┤
│ Shield settings            ↗ │
└──────────────────────────────┘
```

Pill labels wrap to two lines where shown above — the pill label must be `SetMultiLine(true)` with `max_lines = 2`, or the pill will force the bubble wider than 296. Alternative: keep single-line and shorten the long variants to `Shields off for this site`, `Blocked by an extension`, `Shields off in settings`. **Prefer the shorter single-line copy** — a wrapping pill looks like a bug. Final pill strings:

| State | Pill text (single line, final) |
|---|---|
| `kActive` | `Shields are up on this site` |
| `kSiteOff` | `Shields off for this site` |
| `kExtension` | `Blocked by an extension` |
| `kGloballyOff` | `Shields off in settings` |
| `kUnavailable` | `Status unavailable` |

---

## 4. What NOT to implement

Hard scope fence. Anything below requires backend that does not exist, and shipping a stub of it is worse than not shipping it.

1. **No category rows.** No scripts, no fingerprinting, no cookies, no HTTPS upgrade, no social-media blocking — not as rows, not as chips, not as disabled placeholders, not as a "Coming soon" section.
2. **No per-category counts.** The hero number is one aggregate from `MahoAdBlockTabHelper::blocked_count()`. Do not add "12 ads / 4 trackers".
3. **No global/lifetime stats.** No "total blocked", no time-saved, no bandwidth-saved.
4. **No expander / "Advanced controls" disclosure.** The bubble has exactly three sections.
5. **No blocked-request list, no domain list, no "view details".**
6. **No second toggle and no global-vs-site scope switch.**
7. **No "Report broken site" / feedback affordance.**
8. **No count-up animation** on the hero number, no shield pulse, no confetti. State changes are instant repaints.
9. **No new vector icons.** Four shields already exist; use them.
10. **No new `.grd` strings.** These are native Views `u"…"` literals, consistent with the rest of the Maho UI and with the standing English-only decision.
11. **No WebUI**, no `chrome://` surface, no HTML fallback.
12. **No `views::BubbleDialogDelegate` buttons.** `DialogButton::kNone` stays — adding OK/Cancel is the fastest way back to "generic settings dialog".
13. **Do not widen the bubble past 296** to fit copy. Shorten the copy.

---

## 5. Acceptance criteria

Each is independently checkable.

**Utility row**

- A1. Row title is exactly `Shields` in all states.
- A2. Row is 44dp tall and shows a 28dp filled icon tile with a 16dp glyph.
- A3. Icon is `shield_check` / `shield_x` / `shield_half` / `shield_question` per §2.3, and changes when the state changes.
- A4. Tile is `kColorSysTonalContainer` iff the state is `kActive`; `kColorSysSurface2` otherwise.
- A5. A pill badge with the blocked count appears iff `kActive && blocked_count > 0`, capped at `99+`.
- A6. Subtitle matches §2.4 exactly, including the singular form at count 1 and grouped digits above 999.
- A7. Hover changes both the row background and the chevron color; pressed uses a third, distinct background.
- A8. Disabled rows use `kColorSysStateDisabled` text and no chevron — no layer opacity anywhere in the row.
- A9. Clicking the row in `kActive`, `kSiteOff`, and `kExtension` opens the bubble; the row is non-interactive in `kGloballyOff` and `kUnavailable`.
- A10. Accessible name is `"Shields, <subtitle>"`.

**Bubble**

- B1. Bubble width is 296 and contains no nested rounded card. `kColorSysSurface1` appears only as a hover background.
- B2. The top band is full-bleed, with 16dp top corner radius and square bottom corners.
- B3. Band is `kColorSysTonalContainer` iff `kActive`; `kColorSysSurface2` otherwise.
- B4. The host is the largest text in the identity row; the string `Maho Shield` does not appear anywhere in the bubble.
- B5. Hero content is left-aligned.
- B6. `blocked_count == 0` with shields on renders `0` in `kColorSysOnSurfaceVariant`, not `kColorSysOnSurface`, and the bubble height is identical to the count == 1 case.
- B7. Non-`kActive` states render an em dash, never a numeral.
- B8. Status dot is 6dp and is `kColorSysPrimary` only in `kActive`.
- B9. Pill text is single-line in every state at width 296, at 100% and 200% scale.
- B10. Toggle row exists iff `capabilities.site_exception_toggle`; it shows the site-scoped sublabel `On for <host>` / `Off for <host>`, updated after `OnTogglePressed` reads back the effective state.
- B11. Clicking anywhere in the toggle row toggles the switch; the row shows hover and pressed backgrounds.
- B12. Settings row is full-width, 44dp, with a trailing 14dp out-arrow, labeled `Extension settings` in `kExtension` and `Shield settings` otherwise, opening `extensions` / `content-blocker` respectively.
- B13. Two 1dp `kColorSysDivider` separators when the toggle is present; one when it is absent.
- B14. Total bubble height ≤ 260dp in every state at 100% scale.
- B15. The hero announces as a single a11y node with the composed name from §3.9; the individual hero labels are ignored.
- B16. Toggling shields off and back on updates icon, band color, hero number, caption, pill text, and dot color within the same bubble instance without reopening it.

**Cross-surface**

- C1. Row and bubble never disagree: the enum is resolved once per surface from the same `MahoShieldSiteState`, and the strings for a given state come from the shared helpers.
- C2. No new `.icon`, `.grd`, or WebUI file is added by this change.

---

## 6. How to test visually

### 6.1 Build

```
autoninja -C chromium/src/out/Default chrome
```

### 6.2 Manual matrix

Open the utility panel from the location bar, then the Shields row, and confirm each cell.

| # | Setup | Expect (row) | Expect (bubble) |
|---|---|---|---|
| 1 | Fresh `about:blank` | `Shields, No page loaded`, disabled, no chevron | not openable |
| 2 | Navigate to a clean page (e.g. `example.com`), shields on | `No trackers blocked yet`, tonal tile, no badge | tonal band, gray `0`, `Shields are up on this site` |
| 3 | Navigate to an ad-heavy page, reload once | `<N> trackers blocked on this site`, badge `<N>` | tonal band, `<N>` in `onTonalContainer`, same pill |
| 4 | In the bubble, switch **Block ads & trackers** off | row → `Paused for this site`, neutral tile, `shield_x`, no badge | band → surface2, `—`, `Blocking paused here`, `Shields off for this site`, sublabel `Off for example.com` — **all without reopening the bubble** |
| 5 | Switch back on | page reloads, row returns to state 3 | band returns to tonal |
| 6 | `MahoShieldBubbleView::SetContentBlockingModeForTesting(1)` | `Managed by an extension`, `shield_half` | `—`, `Blocked by an extension`, **no toggle row**, footer `Extension settings` |
| 7 | `SetContentBlockingModeForTesting(2)` | `Turned off in settings`, disabled | bubble not openable from the row |
| 8 | A host long enough to elide (`averyveryverylongsubdomain.example.com`) | subtitle elides tail | host elides **middle**; pill stays one line; bubble stays 296 wide |
| 9 | Count > 999 | subtitle `1,284 trackers blocked on this site`, badge `99+` | hero `1,284` |

### 6.3 Rendering conditions to sweep

Run cases 2, 3 and 4 under each:

- Light theme and dark theme (`chrome://settings/appearance`) — verify `onTonalContainer` text on `tonalContainer` band in both.
- 100%, 150%, 200% device scale factor — verify B9 and B14.
- `--force-prefers-reduced-motion` — nothing should animate anyway; confirm.
- macOS increased-contrast mode — the divider and the neutral band must remain distinguishable.

### 6.4 Screenshots to attach to the PR

A 2×3 grid at 200% scale, dark theme: row + bubble for `kActive` (count 0), `kActive` (count > 0), `kSiteOff`. Plus one light-theme `kActive` (count > 0). This is the evidence that answers "what changed?".

---

## 7. Implementation notes (native Views)

### 7.1 Shared helpers — put them in the provider, not duplicated

`maho_location_bar_utility_panel_provider.{h,cc}` exports, and the bubble includes:

```cpp
MahoShieldPresentationState ResolveShieldPresentationState(int mode,
                                                           bool is_excepted);
const gfx::VectorIcon& ShieldIconForState(MahoShieldPresentationState state);
```

Row-only strings (`ShieldRowSubtitle`) stay private to the provider `.cc`; bubble-only strings (`ShieldHeroCaption`, `ShieldPillText`) stay private to the bubble `.cc`. Only the enum and the icon mapping are shared — that is enough to satisfy C1 without coupling the copy.

### 7.2 Hero band background

```cpp
hero->SetBackground(views::CreateRoundedRectBackground(
    band_color_id, /*top_radius=*/tokens::kRadiusLg, /*bottom_radius=*/0));
```

The `(color, top_radius, bottom_radius)` overload is `ui/views/background.h:112`. Do **not** hand-roll an `SkRRect` painter.

### 7.3 Root layout must be zero-inset

`kBubbleContentsInsets` is removed. Replace:

```cpp
auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
    views::BoxLayout::Orientation::kVertical, gfx::Insets(), /*between=*/0));
layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
```

`set_margins(gfx::Insets())` on the delegate is already correct (bubble.cc:108) — keep it.

### 7.4 Precise vertical gaps inside the hero

`BoxLayout`'s `between_child_spacing` is uniform, and the hero needs 10 / 2 / 12. Set inter-child spacing to 0 and give each child a top `views::CreateEmptyBorder(gfx::Insets::TLBR(gap, 0, 0, 0))`. This is the pattern already used for `status_container` (bubble.cc:227).

### 7.5 Pill must hug its content

A pill inside a `kStretch` vertical layout will span the full width. Wrap it:

```cpp
auto* pill_row = hero->AddChildView(std::make_unique<views::View>());
auto* pill_row_layout = pill_row->SetLayoutManager(
    std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal));
pill_row_layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kStart);
// pill goes inside pill_row
```

### 7.6 One repaint entry point

`UpdateStatusPresentation(int mode)` (bubble.cc:411) currently touches two views. Widen it to a single `ApplyState()` that recomputes the enum from `MahoShieldSiteState` and updates, in order: band background, identity glyph + color, host color, number text + color, caption text + color, pill background, dot color, pill text, toggle sublabel, toggle a11y name. Call it from the constructor and from `OnTogglePressed`. B16 depends on this being one function — the current split is why partial updates are easy to introduce.

Store the views it needs as members: `hero_`, `hero_icon_`, `number_label_`, `caption_label_`, `pill_`, `pill_label_`, `status_indicator_`, `toggle_sublabel_`.

### 7.7 Elision

- Bubble host: `SetElideBehavior(gfx::ELIDE_MIDDLE)` (already correct at bubble.cc:192) — keep. Middle elision preserves the TLD, which is the security-relevant part.
- Toggle sublabel host: `ELIDE_TAIL` on the whole sublabel.
- Row subtitle: `ELIDE_TAIL` (already correct).

### 7.8 Row view changes, minimal diff

In `MahoLocationBarUtilityPanelActionView`:

- Add `use_icon_tile` branch: when set, wrap `icon_image_` in a 28dp `views::View` with a rounded background, and use `action_.icon_color` instead of the hardcoded `kColorSysOnSurfaceSubtle` (view.cc:259).
- Add a trailing badge view when `!action_.trailing_badge_text.empty()`, inserted before the disclosure indicator.
- Promote the disclosure `ImageView` to a member (`disclosure_view_`) and recolor it from `SetHighlighted()`.
- Replace the disabled-state layer opacity with per-element `kColorSysStateDisabled` colors.
- Add `OnMouseReleased` / `OnMouseCaptureLost` overrides for the pressed background.

Do not change `DisplayMode`; the Shield row remains `kSettingsRow` and the new fields are opt-in, so `AutoPip` and `Site settings` rows are unaffected except for the shared disabled-state fix.

### 7.9 New testing getters

`MahoShieldBubbleView` (keep the existing ones, add):

```cpp
views::View* GetHeroForTesting() const { return hero_; }
views::Label* GetCaptionLabelForTesting() const { return caption_label_; }
views::Label* GetPillLabelForTesting() const { return pill_label_; }
views::View* GetStatusIndicatorForTesting() const { return status_indicator_; }
views::Label* GetToggleSublabelForTesting() const { return toggle_sublabel_; }
views::LabelButton* GetSettingsRowForTesting() const { return settings_row_; }
```

`GetBlockedCountDescriptionLabelForTesting()` is superseded by `GetCaptionLabelForTesting()` — rename rather than keep both.

### 7.10 Existing tests this breaks

Update, do not delete:

| File:line | Current assertion | New expectation |
|---|---|---|
| `provider_unittest.cc:350` | `row.icon == &kShieldHalfIcon` | `&kShieldCheckIcon` for the active case; add cases per §2.3 |
| `provider_unittest.cc:385` | `subtitle == u"0 blocked · Shields on"` | `u"No trackers blocked yet"` |
| `provider_unittest.cc:451` | `subtitle == u"3 blocked · Shields on"` | `u"3 trackers blocked on this site"` |
| `provider_unittest.cc:502` | `subtitle == u"0 blocked · Shields off"` | `u"Paused for this site"` |
| `provider_unittest.cc:537` | `subtitle == u"0 blocked · Shields off"` | `u"Paused for this site"` |
| all of the above | `title == u"Block Ads & Trackers"` | `u"Shields"` |
| `provider_unittest.cc:456–464` path | `mode != 0` ⇒ disabled row | `kExtension` is now **enabled** with a bubble callback; only `kGloballyOff`/`kUnavailable` stay disabled |

Add:

- `ShieldRowShowsBadgeOnlyWhenBlockingAndNonZero`
- `ShieldRowSubtitleUsesSingularAtOne`
- `ShieldRowSubtitleGroupsThousands`
- `ShieldRowIconTracksPresentationState` (five cases)
- `ShieldRowOpensBubbleWhenSiteExcepted`

`maho_shield_bubble_interactive_uitest.cc` — add:

- `HeroShowsEmDashWhenExtensionManaged`
- `HeroDeemphasizesZeroCount` (asserts the number label's color id)
- `TogglingUpdatesHeroWithoutReopening` (asserts caption, pill text, and dot color after `ToggleForTesting()`)
- `ToggleSublabelNamesTheSite`
- `SettingsRowLabelSwitchesForExtensionMode`

### 7.11 Order of work

1. Enum + icon mapping in the provider; unit tests for the mapping. (No UI change yet.)
2. Provider row copy + new action fields; update `provider_unittest.cc`.
3. `maho_location_bar_utility_panel_view.cc`: icon tile, badge, chevron hover, pressed state, disabled-state fix.
4. Bubble restructure: zero-inset root, hero band, dividers, settings row.
5. `ApplyState()` consolidation + bubble uitests.
6. Screenshot sweep (§6.4).

Steps 1–3 are independently shippable and already resolve "the row doesn't look clickable"; steps 4–5 resolve "what changed?".
