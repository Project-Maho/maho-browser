# Maho Android — Home & Search Design System

Authoritative design contract for the Android home surface and search overlay, extracted from the shipping iOS SwiftUI implementation. A Jetpack Compose implementer should be able to build to full iOS parity from this document without reading the Swift source or guessing values.

**Reference contract (iOS = source of truth):**
- `maho/ios-shell/Views/HomeSearchView.swift` — home hero, Continue, Top sites
- `maho/ios-shell/Views/SearchSheet.swift` — search overlay, field, suggestion rows, Browse for Me pill
- `maho/ios-shell/Views/ShellTheme.swift` — all color / spacing / radius / size tokens
- `maho/ios-shell/Views/ArcBottomBar.swift` — home footer (`[Tabs] [blank] [New Tab +] [Agent] [Settings]`)
- `maho/ios-shell/Views/MainBrowserView.swift` — search present/dismiss, routing, focus handling
- `maho/ios-shell/Views/CommandBar/CommandBarViewModel.swift` + `Models/ViewModels.swift` (`SuggestionType`, `SuggestionViewModel`) — suggestion model, `matchRanges`

**Android targets:**
- `app/src/main/java/dev/maho/browser/ui/HomeSearchScreen.kt`
- `app/src/main/java/dev/maho/browser/ui/search/SearchSheet.kt`
- `app/src/main/java/dev/maho/browser/ui/MainBrowserScreen.kt`
- `app/src/main/java/dev/maho/browser/ui/ArcBottomBar.kt`
- `app/src/main/java/dev/maho/browser/ui/theme/BrowserShellTheme.kt`
- Logo asset: `res/drawable-nodpi/ic_maho_star.png` (`R.drawable.ic_maho_star`)
- Icons: `app/src/main/java/dev/maho/browser/ui/icons/MahoIcon.kt`

**Parity status:** NOT achieved. Today `HomeSearchScreen.kt` renders only a centered logo (no Continue / Top sites), and `SearchSheet.kt` uses grey incognito (`#1E1E1E/#282828/#333333`), a purple AI accent (`#A855F7`), a 20dp panel corner, a single "Ask Maho AI" row, and history-only results with no primary/commands sections or Browse-for-Me pill. This document is the target the implementation must converge on, not a description of current behavior.

---

## 1. Atmosphere & Identity

Maho's home is a calm, near-empty canvas built around a single glowing star mark; the search overlay is a focused, bottom-anchored sheet that slides up over a dimmed page. Two moods:

- **Normal** — light-first, adaptive to dark. Soft vertical gradient canvas, translucent white cards, hairline separators, a cool blue accent (`#84B9FF`). Restrained and native-feeling.
- **Incognito** — a deep indigo world. Diagonal navy gradient (`#1A2352`), frosted white-on-navy cards, muted white text. Distinct enough that the user always knows the mode at a glance.

The identity anchor is the `MahoStar` mark, rendered twice (a blurred material "halo" behind a crisp foreground copy) at 120dp. Motion is spring-based and quiet; nothing bounces for decoration. Every animation maps to a real state change (present, dismiss, press, focus).

---

## 2. Color

All tokens below are the iOS contract. Where iOS uses a semantic system color (`Color.primary`, `.secondary`, `secondarySystemBackground`, `tertiarySystemBackground`, `separator`), Android must map to the adaptive equivalent from `BrowserShellColors` (see mapping table) so light/dark both work — do **not** hardcode a single value for adaptive roles.

### 2.1 Home canvas

| Token | Normal (light) | Normal (dark) | Incognito |
|---|---|---|---|
| Canvas gradient | `#F2F2F7` → `#FFFFFF`, vertical top→bottom | `homeBackgroundStart` → `homeBackgroundEnd` (`#11161D`→`#141920`), vertical | `#1A2352` → `#1A2352 @82%` → `#1A2352`, **diagonal** topLeading→bottomTrailing |
| Card surface | `secondarySystemBackground` @ 85% (adaptive) → Android `overlaySurface`/`tabCard` @ 85% | same adaptive | `#1A2352`-world white @ **8%** (`incognitoSurface` = `Color.White.copy(alpha=0.08f)`) |
| Card border | `primary @ 8%` (adaptive hairline) | same | white @ **14%** |
| Primary text | `primary` (adaptive) | adaptive | white |
| Secondary/muted text | `secondary` (adaptive) | adaptive | white @ **70%** |
| Section accent (Continue globe, Top-site sparkle, chevrons) | system accent (`shellColors.accent`) | `shellColors.accent` | system accent |

The incognito diagonal gradient (topLeading→bottomTrailing) is intentional and differs from the normal vertical gradient. Keep the three-stop `[base, base@82%, base]` recipe.

### 2.2 Search overlay

| Token | Value |
|---|---|
| Backdrop scrim | **black @ 58%** (`searchOverlayBackdrop`), full-bleed, tap-to-dismiss |
| Panel background | adaptive **secondary surface** (iOS `secondarySystemBackground`; both gradient stops are identical so it renders flat) → Android `overlaySurface` (normal), navy `#1A2352` world (incognito) |
| Panel border | adaptive `separator` hairline (normal) / white @ 14% (incognito) |
| Field fill | adaptive **tertiary surface** (iOS `tertiarySystemBackground`) → Android `windowBackground`/`overlaySurfaceHigh` |
| Field text | `primary` (adaptive) / white (incognito) |
| Field placeholder + muted glyphs | `secondary` (adaptive) / white @ 70% (incognito) |
| Section header text | adaptive `tertiaryLabel` (`searchOverlaySecondaryForeground`) |
| Row pressed fill | adaptive `secondarySystemBackground` (normal) / `primary @ 6%` fallback |
| Leading icon tile fill | adaptive `secondarySystemBackground` |
| Divider | adaptive `separator` hairline |
| **Accent** | **`#84B9FF`** (rgb 132,185,255) in normal; **white** in incognito. Used for the text cursor/tint and for highlighted match ranges. |
| Browse-for-Me pill fill | **black @ 44%** |
| Browse-for-Me pill text | **white @ 92%** |

> Parity fix: the AI accent must be `#84B9FF` (or white in incognito), **not** the current `#A855F7` purple. Incognito surfaces must use the `#1A2352` navy family, **not** the current grey `#1E1E1E/#282828/#333333`.

### 2.3 Home footer (ArcBottomBar home mode) — preserve existing adaptive colors

These are already correct in `ArcBottomBar.kt` (`HomeModeBottomBar`); keep them:

| Role | Light | Dark |
|---|---|---|
| Content (icons/text) | `Color.Black` | `Color.White` |
| Footer background | `Color.White` | `homeBackgroundEnd` (`#141920`) |
| Icon button fill | content @ 3.5% | content @ 3.5% |
| New Tab (+) primary fill | `#E5EEFB` | `accent @ 14%` |
| Border | content @ 8% | content @ 8% |
| Tab badge text | `Color.White` | `Color.Black` |

### 2.4 Adaptive role → `BrowserShellColors` mapping

| iOS semantic role | Android `BrowserShellColors` field |
|---|---|
| `Color.primary` (text) | `textPrimary` |
| `Color.secondary` (muted) | `textSecondary` |
| `tertiaryLabel` | `textSecondary` @ ~70% |
| `separator` / hairline border | `divider` |
| `systemBackground` | `windowBackground` / `browsingBackground` |
| `secondarySystemBackground` | `overlaySurface` |
| `tertiarySystemBackground` | `overlaySurfaceHigh` |
| system accent | `accent` (`#A8C4FF` dark); search accent is fixed `#84B9FF` |

---

## 3. Typography

Map iOS SwiftUI text roles to `MaterialTheme.typography` roles + explicit `FontWeight`. **Do not introduce a custom font family**; use the app's Material type scale. iOS point sizes (Dynamic Type Large) are listed as calibration references only.

| Usage (iOS symbol) | iOS role (pt) | Compose role | Weight |
|---|---|---|---|
| Search field text + placeholder | `.title3.weight(.medium)` (20) | `titleMedium` | `Medium` |
| Section header title (Continue / Top sites) | `.headline` (17) | `titleMedium` | `SemiBold` |
| Incognito empty headline ("You're browsing Incognito") | `.headline.weight(.semibold)` | `titleMedium` | `SemiBold` |
| Continue row title | `.subheadline.weight(.semibold)` (15) | `bodyMedium` | `SemiBold` |
| Suggestion row title | `.subheadline` regular (15) | `bodyMedium` | `Normal` |
| Top-site title | `.footnote.weight(.semibold)` (13) | `labelLarge` | `SemiBold` |
| Field icon glyphs / footer glyphs | `.footnote/.body.weight(.semibold)` | `labelLarge` | `SemiBold` |
| Host / subtitle / section subtitle | `.caption` (12) | `bodySmall` | `Normal` |
| Top-site host | `.caption2` (11) | `labelSmall` | `Normal` |
| Suggestion section header (UPPERCASE) | `.caption2.weight(.bold)`, tracking 0.8 | `labelSmall` | `Bold`, `letterSpacing = 0.8.sp`, `text.uppercase()` |
| Browse-for-Me pill text | `.caption.weight(.semibold)` | `labelMedium` | `SemiBold` |
| Empty-state prompt | `.subheadline.weight(.semibold)` | `bodyMedium` | `SemiBold` |
| Tab badge count | `.caption2.weight(.bold)` | `labelSmall` | `SemiBold`/`Bold` |

Line limits: Continue/Top-site/suggestion titles and hosts are all `maxLines = 1` with tail truncation. Suggestion title truncates tail; Browse pill text uses `minimumScaleFactor 0.85` → Compose `autoSize`/`softWrap=false` with a small min.

---

## 4. Spacing & Layout

**Base unit: 4dp.** All spacing is a multiple/step of 4 in spirit; the exact iOS step scale is preserved below.

### 4.1 Spacing tokens (gaps, padding, margins)

| dp | iOS token | Use |
|---|---|---|
| 2 | `badgeVertical` | inner text-stack gap (title/subtitle), badge vertical padding |
| 4 | `xxSmall` | base grid unit |
| 6 | `xSmall` | suggestion-row vertical padding, suggestion-list top inset, section-header bottom padding, incognito toggle inner gap |
| 8 | `small` | field trailing-control gap, row-button leading/vertical padding, section-header leading inset |
| 12 | `medium` | field↔content gap, icon↔title gap, grid inter-cell spacing, Continue row vertical/rows, Browse-pill horizontal padding, section-header horizontal padding |
| 16 | `large` / `field` | panel horizontal + top padding, home horizontal padding, home top spacer, Continue row horizontal padding, Continue divider leading inset, card corner radius (see §5) |
| 20 | `page` / `section` | panel bottom padding |
| 22 | `hero` | incognito/empty-state top & bottom spacer minLength |

### 4.2 Sizes (fixed component dimensions — NOT spacing)

| dp | iOS token | What it sizes |
|---|---|---|
| 34 | `searchOverlayLeadingIcon`, `searchOverlayIconButton` | suggestion leading icon tile (w=h), field trailing icon-button hit box |
| 52 | `homeBottomSpacer` | bottom `Spacer` minLength on returning home |
| 54 | `searchOverlayFieldHeight` | search field height |
| 120 | (literal) | Maho star logo width & height |
| 460 | (literal) | search panel max height cap |
| 640 | `searchOverlayMaxWidth` | search panel max width |

Also referenced: home content max width `520`, top-site icon `40`, touch target `44`.

### 4.3 Home layout geometry

**Empty home** (`recentTabs.isEmpty && topSites.isEmpty`):
- Full-bleed canvas gradient, `Box` centered.
- Centered `MahoStar` logo at **120dp** only. No text, no cards, no search row.
- Horizontal padding 16.
- `testTag("homeSearchView")`.

**Returning home** (any recent tab or top site):
- Vertical scroll. Column spacing **16** between blocks.
- Order: top `Spacer` minLength **16** → logo (120dp) → `Continue` (if recents) → `Top sites` (if top sites) → bottom `Spacer` minLength **52**.
- Horizontal padding **16**.

**There is NO inline home search row and NO onboarding/promo cards.** The logo, Continue, and Top sites are the only home content. Search is opened exclusively through the footer **New Tab (+)** button (see §5 HomeFooterButton, §6 present flow). The dead `HomeSearchField` composable currently in `ArcBottomBar.kt`'s non-home layout branch must not surface on the home route.

### 4.4 Search overlay geometry

- Panel **bottom-anchored** (`Box` / `Column.align(BottomCenter)`), scrim fills the rest.
- Panel width: `fillMaxWidth` capped at **640dp** (`widthIn(max = 640.dp)`), centered.
- Panel height: `min(screenHeight * 0.5, 460dp)`, content top-aligned inside.
- Panel top corners radius **28dp**; bottom corners **0** (anchored to screen edge). Use `RoundedCornerShape(topStart=28, topEnd=28, bottomStart=0, bottomEnd=0)`.
- Panel padding: horizontal **16**, top **16**, bottom **20**.
- Field↔content vertical gap: **12**.
- Field: height **54**, corner radius **20**, hairline border, leading padding 16, trailing padding 12, internal control gap 8.
- Leading suggestion icon tile: **34×34**, corner radius **10**; inner glyph inset ≈ tile − 12 (≈22dp) for image favicons, centered lucide glyph otherwise.
- Browse-for-Me pill: height **30**, `Capsule`/full-round, horizontal padding 12, hairline border.
- Suggestion list top inset: 6.
- Row divider: hairline (1dp), leading inset **62dp** = leadingIcon(34) + large(16) + medium(12). Right-aligned full-bleed otherwise.
- Section header: horizontal padding 12, top 12, bottom 6.
- Apply `navigationBarsPadding()` **only**. Do **NOT** apply `imePadding()` to the panel — the panel is bottom-anchored with a fixed height of `min(screenHeight * 0.5, 460dp)` and must not resize or lift to track the keyboard. This matches the canonical iOS `SearchSheet` (which uses `GeometryReader` height + `.ignoresSafeArea` on the backdrop, not a keyboard-avoidance inset) and the implementation contract. The soft-keyboard / window-resize interaction (e.g. `adjustResize` vs `adjustPan`, IME insets) must be verified so the field stays visible and usable **without** changing the fixed panel geometry — solve visibility via window soft-input mode / scroll, not by inflating or offsetting the panel.

---

## 5. Components & Primitives

Each primitive lists variants, states, accessibility, and motion. Use the exact iOS `accessibilityIdentifier` as the Compose `testTag` so shared semantics and the Appium/Compose test suite match.

### 5.1 HomeHeroLogo
- **Android implementation:** a **crisp 120dp foreground** `Image` of `ic_maho_star` (`ContentScale.Fit`). Optionally, behind it, a scaled-1.13 **alpha-copy** of the same asset softened with **alpha-aware blur only** (e.g. `Modifier.blur(radius, edgeTreatment = BlurredEdgeTreatment.Unbounded)` on the transparent PNG, so only the star's own pixels blur). **Do NOT use layout-bound `Modifier.shadow`** — it shadows the composable's rectangular bounds and produced an ugly octagonal box/plate artifact behind the star. If alpha-aware blur cannot be guaranteed on the target API levels, render **only the crisp foreground image** and drop the halo copy entirely. Preserve the iOS visual intent (a soft glow hugging the star's silhouette) but **explicitly forbid any visible box, plate, rectangle, or hard-edged halo shape** behind the logo. Frame **120×120**.
- Variants: none (same in normal & incognito; tinting comes from the asset).
- States: static.
- A11y: `contentDescription = "Maho"`.
- Motion: fades in with the home surface (`transition(.opacity)`); no idle animation.

### 5.2 HomeContinueSection
- Header (`SectionHeader`, title "Continue", subtitle "Pick up where you left off") + a single rounded card containing up to **3** recent tabs (most-recently-active first).
- Row: leading globe glyph (accent) · title (`New tab` if blank) + host subtitle · trailing chevron-right (muted). Row padding vertical 12, horizontal 16.
- Between rows: `RowDivider` inset leading 16. No divider after the last row.
- Card: surface + hairline border, corner radius **16**.
- States: pressed (row highlight via ripple/plain), normal.
- A11y: each row is a button; label = title, value = host; whole section reachable in order.
- Motion: standard press ripple only.

### 5.3 HomeTopSitesGrid
- Header (title "Top sites", subtitle "Fast ways back in") + `LazyVerticalGrid`, **2 columns**, inter-cell spacing **12**, up to **8** unique-URL tabs.
- Cell: leading sparkles glyph (accent) + title (host if title blank) + host caption; padding 12; surface + hairline border; corner radius 16.
- States: pressed, normal.
- A11y: each cell a button; label = title; opens the URL.
- Motion: press ripple.

### 5.4 HomeFooterButton (ArcBottomBar home mode)
- **Visual slot pattern:** `[Tabs] [flexible blank balance] [New Tab +] [Agent] [Settings]`. Identical layout on **iOS and Android**.
- **Controls:** **Tabs** (copy glyph + count badge, 44dp, rounded-16) · **New Tab (+)** (plus glyph, **112×44**, rounded-18, primary fill) · **Agent** (bot glyph, 44dp circle) · **Settings** (settings glyph, 44dp circle).
- **Centering contract:** the New Tab (+) button is **mathematically centered on the full screen/bar width**, independent of the asymmetric side controls. It must NOT drift because the right group (Agent + Settings) is wider than the lone left control (Tabs).
- **Layout technique:** implement as a **layered `Box`/`ZStack`, NOT a sequential evenly-spaced `Row`/`HStack`**:
  - New Tab (+) is placed centered (overlay/`Alignment.Center`) on the full bar width.
  - **Tabs** anchored left (`Alignment.CenterStart`).
  - **Agent + Settings** grouped right (`Alignment.CenterEnd`) as a `Row` with a **12dp** gap, Agent immediately left of Settings.
  - The "flexible blank balance" is the empty space between Tabs and the centered +; it is not an evenly divided gap.
- New Tab (+) is the **sole entry point to the search overlay** (`onSearchClick`).
- **Agent button:** bot icon and the existing AI callback. Keep the stable IDs (`homeAiButton` on iOS, `arcBottomBarAiButton` on Android); the visible/accessibility label may be **"Agent"** or **"AI agent"**.
- Colors: §2.3 (preserve).
- States: pressed; Tabs shows count badge when `tabCount > 0` (`99+` cap).
- Sizing/margins: Tabs 44dp (left), Agent 44dp and Settings 44dp (right group), standard **12dp** horizontal margins. Verify **no overlap** between the centered + and the side controls at supported phone widths (narrowest first).
- A11y `testTag` / `contentDescription`: `arcBottomBarTabsButton` "Tabs" (+ state "N tabs"), `arcBottomBarAiButton` "Agent"/"AI agent", `arcBottomBarPageButton` "New Tab", `arcBottomBarMoreButton` "Settings". Container `arcBottomBarContainer`.
- Touch target: 44dp (meets minimum; see §8).
- Motion: press ripple; bar itself has no home-mode slide.

### 5.5 SearchOverlayScrim
- Full-screen **black @ 58%**, `clickable{ onDismiss() }` (no ripple).
- Consumes background taps; the panel stops propagation (`clickable(enabled=false){}` / no-op tap).
- A11y: dismiss affordance; the scrim should expose a "Close" action for TalkBack.
- Motion: fades with the panel present/dismiss.

### 5.6 SearchPanel
- Bottom sheet container per §4.4 (640 max width, `min(0.5·h, 460)` height, top corners 28, gradient/flat secondary surface, hairline border).
- Contains `SearchField` then scrollable content (suggestions or an empty state).
- Variants: normal / incognito (navy). Content states in §7.
- A11y: `testTag("searchSheet")`; children contained.
- Motion: present = slide up from bottom + fade, spring `response 0.32, damping 0.88`; dismiss = slide down + fade, spring `response 0.24, damping 0.90`. Compose: `slideInVertically(spring…){ it } + fadeIn` / `slideOutVertically + fadeOut`.

### 5.7 SearchField
- Row: text field (weight 1) + trailing controls. Height 54, corner 20, tertiary-surface fill, hairline border. Leading padding 16, trailing 12, gap 8.
- Placeholder "Search…" when empty (muted). Text style titleMedium Medium. Cursor/selection tint = accent (`#84B9FF` / white incognito).
- Keyboard: `KeyboardType.Uri`, `ImeAction.Search`, autocorrect off, no autocapitalization; submit trims and calls `onSubmit` only when non-empty.
- Trailing controls:
  - **Normal:** voice mic button (`searchSheetMicButton`) then incognito-enter eye button (`searchSheetPrivateToggle`, label "Enter Incognito"). A clear (×) button shows when text is non-empty.
  - **Incognito:** a single "Incognito" text + eyes glyph toggle (`searchSheetPrivateToggle`, label "Exit Incognito").
- Icon buttons sized 34dp (expand ripple/hit area to 44/48 — see §8).
- A11y: field `searchSheetTextField` (contained in `searchSheetField`), labeled toggles as above.
- Motion: focus is requested after an **80ms** delay on appear (`delay(80)` then `focusRequester.requestFocus()`); this is a hard requirement to let the present animation settle before the keyboard opens.

### 5.8 SearchSuggestionRow
- Left: `SearchSuggestionRow` select button (leading icon tile 34 + title/subtitle stack, `fillMaxWidth`). Right (optional): `BrowseForMePill`.
- Leading icon: favicon image if `suggestion.icon` present (rounded-10 clip, inset ≈ tile−12), else the fallback lucide glyph (muted) per §7.3.
- Title: highlighted match ranges rendered bold + accent color (§7.4). Subtitle (if any): caption, secondary.
- Row padding: vertical 6, trailing 8; select-button inner padding vertical 8 / leading 8; row corner 14 pressed fill.
- States: normal, pressed (row fill = `searchOverlayRowPressed`).
- A11y: row is a contained element; select button opens the suggestion; the icon is `accessibilityHidden`.
- Motion: press fill; no entrance animation per row beyond list fade.

### 5.9 BrowseForMePill
- Capsule, height 30, text "Browse for Me", fill black @44%, text white @92%, hairline border, horizontal padding 12.
- Visibility (`showsBrowseForMe`): shown for `search`, `aiAnswer`, `navigation` always; for `history` only when the query is non-empty; hidden otherwise.
- Tapping routes to the AI destination (see §8 accepted debt — Android currently opens the Web Agent overlay).
- A11y: `testTag("searchSheetBrowseForMeButton")`, label "Browse for Me".
- Motion: press ripple.

### 5.10 SectionHeader (home)
- Title (titleMedium SemiBold) over subtitle (bodySmall, secondary), gap 2, leading inset 8.

### 5.11 Suggestion section header (search)
- UPPERCASE label (labelSmall Bold, letterSpacing 0.8sp, `tertiaryLabel` color), padding horizontal 12 / top 12 / bottom 6. Rendered before HISTORY and COMMANDS groups.

### 5.12 RowDivider
- Home: `HorizontalDivider`, hairline, leading inset 16 (Continue rows).
- Search: hairline rectangle, color = adaptive `separator` / white@14% incognito, leading inset **62dp** (§4.4). No trailing divider after the last row in a group.

---

## 6. Motion & Interaction

- **Search present:** slide-up + fade, spring `response 0.32 / damping 0.88`. Focus field after **80ms**.
- **Search dismiss:** slide-down + fade, spring `response 0.24 / damping 0.90`. Tap scrim, back gesture, submit, or suggestion-select all dismiss.
- **Submit:** trims query; empty → no-op. Non-empty → dismiss overlay, then `submitSearch` (new tab unless reusing active tab).
- **Suggestion select:** dismiss, then navigate (or open AI for `aiAnswer`).
- **Voice handoff:** mic tap (when not `processing`) requests the voice assistant; a returned transcript fills the field and re-focuses after ~100ms. Preserve existing Android `VoiceSearchButton` behavior (see §8).
- **Press feedback:** rows/cards use a subtle fill/ripple; footer buttons ripple. No decorative or idle motion. All animation is `transform`/`opacity`-class (GPU-composited) — never animate layout.
- **Reduced motion:** honor the system "remove animations" setting — cross-fade present/dismiss instead of translate; keep the 80ms focus delay.

---

## 7. State Matrix

Search suggestions come from `CommandBarViewModel.suggestions` (populated via the bridge query, debounced ~150ms on text change). Grouping and order are fixed.

### 7.1 Home & search states

| State | Trigger | Rendering |
|---|---|---|
| Empty home | no recent tabs & no top sites | canvas + centered 120dp logo only |
| Returning home | ≥1 recent tab or top site | 16 top spacer · logo · Continue(≤3) · Top sites(≤8, 2-col) · 52 bottom spacer |
| Incognito home | incognito mode on home | diagonal `#1A2352` gradient; cards white@8% / border white@14%; same layout rules |
| Search normal empty | overlay open, query empty | field focused; content = empty state "Search or enter a website" |
| Search typed w/ suggestions | query non-empty, results exist | Primary group → HISTORY → COMMANDS |
| History group | history suggestions exist | "HISTORY" section header + history rows |
| Commands group | query non-empty & action suggestions exist | "COMMANDS" section header + action rows (hidden when query empty) |
| Incognito search empty | incognito, query empty | centered "You're browsing Incognito" (headline semibold, muted), hero spacers 22 |
| Incognito search typed | incognito, query non-empty | same suggestion grouping as normal, navy palette |
| Search typed, no suggestions | query non-empty, zero results | empty state "Press Search to go" |
| Voice handoff | mic tap (normal only) | assistant overlay; transcript fills field, re-focus ~100ms |
| Dismiss | scrim tap / back / submit / select | slide-down + fade |
| Submit | IME search / New Tab flow | trim → new tab (or reuse active) → navigate; overlay closes |

### 7.2 Suggestion ordering (exact)
1. **Primary** — everything whose kind is **not** `action` and **not** `history` (i.e. `search`, `navigation`, `aiAnswer`, `tab`, `bookmark`, `folder`, `archivedTab`, `closedTab`, `calculator`, `unitConversion`). No section header.
2. **HISTORY** — kind `history`. Section header "HISTORY".
3. **COMMANDS** — kind `action`, **only when the query is non-empty**. Section header "COMMANDS".

Dividers separate rows **within** a group (leading inset 62dp), not between groups (the section header provides the break).

### 7.3 Fallback icon mapping (`SuggestionType` → `MahoIcon`)
Used when `suggestion.icon` favicon is absent. Source enum: `Models/ViewModels.swift`.

| `SuggestionType` | iOS lucide | Android `MahoIcon` |
|---|---|---|
| `tab` | copy | `Copy` |
| `bookmark` | bookmark | `Bookmarks` / `BookmarkBorder` |
| `history` | clock | `Clock` |
| `action` | sparkles | `SparklesAi` |
| `navigation` | search | `Search` |
| `aiAnswer` | search | `Search` |
| `search` | search | `Search` |
| `folder` | folder | `Folder` |
| `archivedTab` | archive | `Archive` |
| `closedTab` | circleX | `CircleAlert` (nearest; add a circle-x drawable for exact parity) |
| `calculator` | search | `Search` |
| `unitConversion` | search | `Search` |

> `closedTab` has no exact `circle-x` in `MahoIcon`; either add `ic_circle_x` or accept `CircleAlert` as a tracked minor deviation.

### 7.4 Highlighted match ranges
`suggestion.matchRanges` is `[[start, length], ...]` in UTF-16/NSString offsets. For each valid range (length 2, in-bounds), render that substring **bold + accent color** (`#84B9FF` / white incognito) via an `AnnotatedString` `SpanStyle`. Skip malformed/out-of-bounds ranges silently.

### 7.5 Empty-state copy (exact strings)
- Normal, query empty: **"Search or enter a website"**
- Normal, query non-empty, no results: **"Press Search to go"**
- Incognito, query empty: **"You're browsing Incognito"**

---

## 8. Accessibility Constraints & Accepted Debt

### 8.1 Accessibility
- **Shared identifiers:** reuse iOS `accessibilityIdentifier`s as Compose `testTag`s so semantics and the test suites (`app/src/androidTest`, `maho/appium-tests`) line up: `homeSearchView`, `searchSheet`, `searchSheetField`, `searchSheetTextField`, `searchSheetSuggestions`, `searchSheetEmptyState`, `searchSheetIncognitoEmptyState`, `searchSheetBrowseForMeButton`, `searchSheetMicButton`, `searchSheetPrivateToggle`, plus existing Android footer tags (`arcBottomBarContainer`, `arcBottomBarTabsButton`, `arcBottomBarAiButton`, `arcBottomBarPageButton`, `arcBottomBarMoreButton`) and the current `homeSearchField`/`homeSearchBar`/`searchSheetVoiceButton`.
- **Touch targets:** minimum **44dp** (iOS parity) / **48dp** (Android/Material recommended). Footer buttons already 44dp. The **34dp** field icon buttons and **30dp** Browse pill are below the floor — expand the interactive/ripple bounds to ≥44dp (visual size may stay) via `Modifier.minimumInteractiveComponentSize()` or padding.
- **Contrast:** verify muted tokens against their surfaces — white@70% on `#1A2352`, `#84B9FF` accent text, and black@44% pill with white@92% text should meet WCAG AA for the text sizes used; run a contrast pass in both themes.
- **Labels:** toggles carry explicit labels ("Enter Incognito"/"Exit Incognito", "Voice search"/"Stop voice search"/"Processing speech"); leading suggestion icons are decorative (`accessibilityHidden`).
- **Reduced motion:** §6.
- **Focus order:** field → suggestions top-to-bottom (Primary → History → Commands); scrim exposes a Close action.

### 8.2 Accepted debt (recorded explicitly, not silent)
1. **Shared conversation/chat destination.** Both mobile shells expose the shared web-ai Conversations host, and selecting or creating a conversation routes within that host to `#chat?sessionId=...`. Browse-for-Me / AI answer remains a separate Web Agent flow.
2. **Voice stack preserved as-is.** The Android voice manager, `VoiceSearchButton`, and `VoiceAssistantOverlay` are kept as the existing implementation. Do **not** redesign the voice flow while achieving home/search parity; treat it as a constraint. iOS mic-state icon semantics (mic / ellipsis / mic-off, accent when listening) are the target if/when the button is revisited, but that is not part of this parity pass.
3. **`closedTab` fallback icon** has no exact `circle-x` drawable in `MahoIcon` (see §7.3) — minor deviation until an `ic_circle_x` is added.
4. **Current Android SearchSheet is pre-parity** (grey incognito, purple AI accent, 20dp corner, history-only, no primary/commands groups, no Browse pill). These are the concrete gaps this document exists to close; they are not accepted long-term.
