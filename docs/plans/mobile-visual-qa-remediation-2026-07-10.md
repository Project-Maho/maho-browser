# Maho Mobile — Visual QA Remediation Plan

**Date:** 2026-07-10
**Provenance:** Derived from iOS Simulator visual QA (8 screens captured via `VisualQAScreenshots` UITest on iPhone 17 Pro sim, iOS 26.5).
**Scope:** Fix P0-P2 issues surfaced in the visual analysis of `docs/plans/mobile-implementation-plan-2026-07-09.md` implementation.
**Target sequence:** P0 first (blocks credible TestFlight demo), P1 next (quality bar), P2 opportunistic (polish).

---

## Baseline Findings (Reference)

Captured screens: `01-onboarding`, `02-home-empty`, `03-menu-open`, `04-tab-grid`, `05-url-bar-focused`, `06-url-bar-typed`, `07-settings-root`, `08-settings-scrolled`, `09-private-mode`.

Full analysis: attached to xcresult bundle at `~/Library/Developer/Xcode/DerivedData/Maho-*/Logs/Test/Test-Maho-2026.07.10_08-07-56-+0900.xcresult`. UITest source at `maho/ios-shell/UITests/VisualQAScreenshots.swift` (produces these attachments on every run).

**Critical infrastructure discovered during analysis:**
- `MahoBridge+CommandBar.swift` already exposes `commandBarQuery(query:mode:) → CommandBarUpdateHandler.didReceiveCommandBarResults(suggestions: [SuggestionViewModel])`. Suggestions pipeline is WIRED at the bridge but **not consumed by any view**.
- `SuggestionViewModel` (`Models/ViewModels.swift:143`) carries `kind`, `key`, `title`, `subtitle`, `executionPayload`, `icon`, `relevanceScore`, `matchRanges` — sufficient for a full suggestions list.
- `MainBrowserView.swift:154` — the `menuButton.onMenuTapped` callback is wired **directly to `showSettings = true`**. There is no intermediate menu sheet, which explains why the visual QA saw a stub-looking "Private + Settings" popover — that popover is a SwiftUI `Menu {}` context render with only two actions.
- 15 settings sub-screens exist under `Views/Settings/` — they are complete; the issue is discovery (no icons/section headers) not implementation.

---

## User Decisions (Locked 2026-07-10)

| Gate | Decision | Impact |
|------|----------|--------|
| **Q-VQA-1** | **단일 시트에 15개 전부** | T-VQA-2: MahoMenuSheet 4-section 스크롤 시트, `.large` detent 우선 |
| **Q-VQA-2** | **검색바 중심 Arc 스타일** | T-VQA-3: recentTabs/topSites 제거, 브랜드 마크 + 검색 hero + 짧은 힌트만. **작업량 최소, 스코프 축소** |
| **Q-VQA-3** | **다크 웹뷰 + 상단 배너 (default)** | T-VQA-5: WKUserScript 페이지 배경 dark + `PrivateBanner` 컴포넌트 |
| **Q-VQA-4** | **3단계 온보딩 재설계** | T-VQA-9 **확장**: page indicator + 3-step swipe (Welcome / Key Features / BYOK setup or Sign-in). **1-2일 작업** |
| **Q-VQA-5** | **Monogram 원 (첫 글자 + 도메인 색상)** | T-VQA-6: `DomainMonogram` 헬퍼 |

### Q-VQA-2 스코프 변경 노트
Arc-style 검색바 중심 홈 → recentTabs/topSites 그리드 미구현. 결과적으로 **T-VQA-3 작업량 감소**:
- 삭제: recentTabs horizontal scroll 렌더링
- 삭제: topSites 4-column grid
- 유지: 브랜드 마크 (작은 로고)
- 유지: 검색 hero (URL 바가 이미 하단 chrome에 있으므로 홈 중앙에는 tagline + subtle CTA만)

### Q-VQA-4 스코프 확장 노트
3-step onboarding으로 결정 → **T-VQA-9가 P0-adjacent로 승격** (첫 인상 임팩트가 큼).
- Screen 1: "Welcome to Maho" + 브랜드 hero + "Continue"
- Screen 2: "Bring Your Own Key" — BYOK 설명 + "Set up AI" or "Skip"
- Screen 3: "Set as Default Browser" — 기존 이미 있는 CTA + "Not Now"
- Page indicator: 3-dot pagination bottom-center
- Swipe gesture: `TabView(selection:)` with `.page` style

---

## Hard Constraints

- **HC-VQA-1: TestFlight demo credibility.** Screens that make the app look broken (empty home, blank suggestion area, "Private + Settings" only menu) are non-negotiable P0. A TestFlight tester's first 30 seconds must not read as "unfinished".
- **HC-VQA-2: Reuse existing bridge/model surface.** Do NOT introduce new Rust core APIs for anything that can be served by existing `MahoBridge+CommandBar`, `MahoBridge+History`, `MahoBridge+Bookmarks`, `MahoBridge+Tabs`, `MahoBridge+ReadingList`, `MahoBridge+Downloads`, `MahoBridge+Notes`, `MahoBridge+Profiles`. Every P0/P1 task in this plan must be shell-only Swift work unless explicitly noted.
- **HC-VQA-3: Do not regress Android.** Android shell was not visually reviewed in this pass; any shared-core changes must be Android-compatible or gated via `#[cfg(target_os = "ios")]`.
- **HC-VQA-4: Do not break Rust tests.** `cargo test -p maho-core --lib` must remain 212/212 green.
- **HC-VQA-5: Do not break existing UITests.** `MahoUITests` (7 tests, includes `ConversationE2ETests`) must remain 7/7 green.
- **HC-VQA-6: Accessibility identifiers preserved.** Existing UITest identifiers (`homeSearchField`, `tabsButton`, `menuButton`, `mainBrowserBottomBar`, `incognitoStateBanner`, `e2eVerifyResult`) must not be renamed or removed.

---

## Decisions (from analysis + discovery)

- **D-VQA-1: URL bar suggestion pipeline is a view-layer wiring task, not a Rust task.** `MahoBridge+CommandBar` already returns real suggestions; the fix is a new SwiftUI view above the keyboard consuming `SuggestionViewModel`.
- **D-VQA-2: The "menu" popover currently is not a real menu sheet.** Introduce a `MahoMenuSheet` composable that presents 10+ actions in a proper bottom sheet, and re-wire the hamburger button through it. Settings becomes ONE row in that sheet (not the tap target itself).
- **D-VQA-3: Private mode uses page-background tinting, not full dark theme swap.** Chrome is already dark-tinted correctly on private tabs. The page background is what looks wrong — because WKWebView renders `about:blank` white by default. Fix by injecting a dark background on the new-tab document in private mode.
- **D-VQA-4: Home screen minimum viable content = recent tabs (existing `recentTabs` computed prop) + top sites (existing `topSites` computed prop) + a brand mark.** Both computed props already exist in `MainBrowserView.swift` but are only rendered inside `HomeSearchView` in a limited fashion. Extend `HomeSearchView` to render both prominently.
- **D-VQA-5: Tab grid favicon fetch is deferred to shared-core.** The orange-star placeholder is a UI fallback. Real favicon fetching lives in `TabPreview` / bridge scope. For this remediation, replace the star placeholder with a monogrammed circle (first letter of domain) as a visual bridge; real favicon pipeline is a separate follow-up.
- **D-VQA-6: Settings section headers use in-code section titles, no localization refactor needed for v1.** Ship English section headers now; L10n comes with the Language settings feature completion.
- **D-VQA-7: Onboarding page indicator is deferred to a proper multi-step redesign.** For this remediation, only add a brand hero (logo + tagline) to fill the empty middle. Multi-step flow is a separate design task, not P0.
- **D-VQA-8: URL bar Cancel button in focus state is added as a floating "Cancel" text button on the left of the suggestions overlay,** not inline in the pill (which would push the tab-switcher/menu off screen).

---

## USER INPUT GATES

**Q-VQA-1: Menu action set for `MahoMenuSheet`.** Which of the following belong in the primary menu vs. secondary "More" section?
- Candidate items: New Tab, New Private Tab, Bookmarks, History, Downloads, Reading List, Notes, Share Page, Find in Page, Reload, Desktop Site, Zoom, Reader Mode, Settings, Sign In / Sync Status, Profile Switcher.
- Default proposal (if not answered): all 16 in a single scrollable sheet, ordered by frequency-of-use estimate.

**Q-VQA-2: Home screen brand hero content.** What goes in the empty middle of `02-home-empty`?
- (a) Small Maho logo + tagline + shortcut grid (Top Sites)
- (b) Recent Tabs prominent + Top Sites secondary + no logo
- (c) Both — logo header + recent + top sites
- Default proposal (if not answered): (c).

**Q-VQA-3: Private mode dark theme scope.** Should private mode:
- (a) Only tint the WebView background (new-tab document dark) — minimal
- (b) Also add a persistent "Private Browsing" banner at top of screen
- (c) Also switch chrome/text colors to a dedicated dark private theme
- Default proposal (if not answered): (a) + (b).

**Q-VQA-4: Onboarding scope.** Fix in this pass or defer?
- (a) Add brand hero (logo + tagline) only — 1 hour
- (b) Full multi-step onboarding with 3 screens + page indicators — 1-2 days
- Default proposal (if not answered): (a). (b) becomes a separate follow-up plan.

**Q-VQA-5: Tab grid favicon fallback.** Which fallback while the real favicon pipeline is pending?
- (a) Monogram circle (first letter of eTLD+1) with domain-derived color
- (b) Generic globe icon (current preview zone glyph)
- (c) Blank / no icon
- Default proposal (if not answered): (a).

---

## Task Dependency Graph

```
Wave 1 (foundations, independent, batch-able in one sprint):
├── T-VQA-1: SuggestionsListView + wiring [depends: HC-VQA-1, D-VQA-1]
├── T-VQA-2: MahoMenuSheet + wiring [depends: Q-VQA-1]
├── T-VQA-3: HomeSearchView content expansion [depends: Q-VQA-2, D-VQA-4]
└── T-VQA-4: Settings section headers + Web Agent icon + version footer

Wave 2 (private mode + tab grid, independent):
├── T-VQA-5: Private mode dark page background + banner [depends: Q-VQA-3, D-VQA-3]
├── T-VQA-6: Tab grid favicon monogram fallback [depends: Q-VQA-5, D-VQA-5]
├── T-VQA-7: Tab grid dedupe pin control + add space switcher + incognito toggle
└── T-VQA-8: URL bar focused-state Cancel button [depends: T-VQA-1]

Wave 3 (polish, low-risk):
├── T-VQA-9: Onboarding brand hero [depends: Q-VQA-4]
├── T-VQA-10: Menu action-sheet → list restyle within MahoMenuSheet
├── T-VQA-11: Sync row status subtitle
└── T-VQA-12: Reset All Settings icon color to red

Wave 4 (verification):
└── T-VQA-13: Re-run VisualQAScreenshots UITest + attach delta screenshots + verify no regressions
```

---

## Phase 1: P0 (TestFlight Demo Blockers)

### T-VQA-1: URL Bar Suggestions List
**WHERE:**
- New file: `maho/ios-shell/Views/CommandBar/SuggestionsListView.swift`
- New file: `maho/ios-shell/Views/CommandBar/CommandBarViewModel.swift`
- Modify: `maho/ios-shell/Views/HomeSearchView.swift` — add overlay above keyboard when field is focused
- Modify: `maho/ios-shell/Views/Chrome/UrlBarView.swift` (if exists) — same overlay pattern for browsing state

**HOW:**
1. Create `CommandBarViewModel: ObservableObject, CommandBarUpdateHandler`:
   - `@Published var suggestions: [SuggestionViewModel] = []`
   - `func query(_ text: String, mode: String? = nil)` → calls `MahoBridge.shared.commandBarQuery(query: text, mode: mode)` and iterates `CoreUpdate`s through `MahoBridge.shared.processCommandBarUpdate(_:handler:self)`
   - `func didReceiveCommandBarResults(suggestions:)` → assign to `@Published`
   - `func selectItem(at index: Int)` → `commandBarSelectItem(index:, key: suggestions[index].key)`
2. Create `SuggestionsListView(viewModel: CommandBarViewModel, onSelect: (SuggestionViewModel) -> Void)`:
   - `LazyVStack` of `SuggestionRow`s
   - Each row: leading `ImageData` (if present) via existing `Image(imageData:)` helper OR SF Symbol per `kind`
   - Title (primary), subtitle (secondary), match-range emphasis via `AttributedString` if `matchRanges` non-nil
   - Tap → invoke `onSelect(suggestion)` which navigates via `executionPayload`
3. In `HomeSearchView`:
   - `@FocusState private var searchFocused: Bool`
   - `@StateObject private var commandBarVM = CommandBarViewModel()`
   - `.overlay(alignment: .bottom) { if searchFocused { SuggestionsListView(...) } }` — positioned above keyboard, height clamped to remaining safe area
   - `onChange(of: searchQuery) { newValue in commandBarVM.query(newValue) }` (debounced 150ms via `Task { try await Task.sleep(...); commandBarVM.query(newValue) }`)
   - On selection: dismiss focus, navigate to `executionPayload` URL via `createTabAndNavigate`

**WHY:** HC-VQA-1 (empty suggestion area makes app look broken), D-VQA-1 (bridge already returns data), P0 blocker.

**SUCCESS CRITERIA:**
- Focus URL bar → suggestions area appears within 200ms
- Type "apple" → at minimum 1 suggestion appears (recent history, Google search suggestion, or bookmarks match) within 500ms
- Tap a suggestion → navigates to that URL / performs that search
- Empty query state → shows recent history OR top sites (whichever `commandBarQuery("", mode: nil)` returns)
- VisualQAScreenshots `05-url-bar-focused` and `06-url-bar-typed` no longer show blank canvas above keyboard
- Zero new Rust code

**DEPENDS ON:** None
**USER GATES:** None

### T-VQA-2: MahoMenuSheet
**WHERE:**
- New file: `maho/ios-shell/Views/Menu/MahoMenuSheet.swift`
- New file: `maho/ios-shell/Views/Menu/MenuAction.swift` (enum of ~15 actions)
- Modify: `maho/ios-shell/Views/MainBrowserView.swift:154` — change `onMenuTapped: { showSettings = true }` to `onMenuTapped: { showMenu = true }`; add `@State private var showMenu = false` + `.sheet(isPresented: $showMenu) { MahoMenuSheet(...) }`
- Retain `showSettings` sheet for the inner Settings navigation from the menu

**HOW:**
1. `MenuAction` enum with cases: `.newTab`, `.newPrivateTab`, `.bookmarks`, `.history`, `.downloads`, `.readingList`, `.notes`, `.sharePage`, `.findInPage`, `.reload`, `.desktopSite`, `.readerMode`, `.settings`, `.signIn`, `.profileSwitcher`. Each has `title: String`, `iconName: String` (Lucide), `isEnabled(state: BrowserState) -> Bool`.
2. `MahoMenuSheet`:
   - `.presentationDetents([.medium, .large])`
   - Grouped by section: **Actions** (New Tab, Private Tab, Reload, Share, Find, Desktop Mode, Reader Mode), **Library** (Bookmarks, History, Downloads, Reading List, Notes), **Account** (Sign In / Profile), **App** (Settings)
   - Each row: 44pt height, left-aligned Lucide icon (24pt) + label (17pt) + trailing chevron for drill-down items
   - Section headers: 13pt uppercase gray
3. Tap handlers dispatch to existing view state (e.g., `.settings` → `showSettings = true`; `.bookmarks` → `showBookmarks = true`; etc.). Existing sheet infrastructure already exists in `MainBrowserView`.

**WHY:** HC-VQA-1 (a 2-action popover in a "browser" reads as broken), Q-VQA-1.

**SUCCESS CRITERIA:**
- Tap hamburger → MahoMenuSheet appears at medium detent
- All 15 actions visible or reachable via scroll
- Each action's tap invokes existing view/sheet state (no dead entries)
- Settings row is one entry among many (not the direct tap target)
- Visual QA `03-menu-open` shows a real menu list, not a 2-button popover

**DEPENDS ON:** None
**USER GATES:** Q-VQA-1 (menu action set)

### T-VQA-3: Home Screen — Arc-Style Search Focus (Q-VQA-2 revised)
**WHERE:**
- Modify: `maho/ios-shell/Views/HomeSearchView.swift` (currently 198 lines)

**HOW:**
1. Refactor to render **only 2 elements** when NOT focused:
   - **Top**: small centered Maho logo (60pt) + tagline "A beautiful, fast browser."
   - **Middle**: a "hint" pill pointing to the bottom URL bar ("↓ Tap the search bar to begin") OR simply empty subtle whitespace with only the logo
2. **NO** recentTabs horizontal scroll (Q-VQA-2 rejected)
3. **NO** topSites 4-column grid (Q-VQA-2 rejected)
4. When search field focused (via T-VQA-1) → hero collapses / hides, suggestions overlay takes over
5. Ensures the visual hierarchy directs attention to the URL bar (Arc-style)

**WHY:** Q-VQA-2 = "검색바 중심 Arc 스타일", D-VQA-4 revised.

**SUCCESS CRITERIA:**
- Fresh install → home shows small logo + tagline centered vertically
- No recentTabs or topSites grid on home
- Focus URL bar → hero fades out, suggestions overlay appears (T-VQA-1)
- Visual QA `02-home-empty` shows brand identity + intentional minimalism, not accidental blankness

**DEPENDS ON:** None
**USER GATES:** None (Q-VQA-2 locked)

### T-VQA-5: Private Mode Dark Page Background + Banner
**WHERE:**
- Modify: `maho/ios-shell/Views/WebViewContainer.swift` — inject CSS user script that sets `body { background: #1c1c1e; color: #f2f2f7; }` when private mode is active
- Modify: `maho/ios-shell/Views/MainBrowserView.swift` — add persistent private-mode banner at top when `isIncognito == true`
- Reuse: existing `isIncognito` state in `MainBrowserView`

**HOW:**
1. Add `WKUserScript` conditional injection: when `WebViewContainer` init detects `viewModel.isPrivate == true`, register a `.atDocumentStart` script that patches `document.documentElement.style.background` and injects a `<style>` rule for `body:not([data-maho-page]) { background: #1c1c1e; color: #f2f2f7; }`. Sites with explicit CSS override this automatically; only blank/unstyled pages inherit the dark canvas.
2. New file: `maho/ios-shell/Views/Chrome/PrivateBanner.swift` — 32pt-height pill at top: purple/indigo tint, "Private Browsing" label, small "×" to exit. Anchored below status bar via safe area.
3. In `MainBrowserView.body`, add `PrivateBanner()` above the browser view when `isIncognito`.
4. Tapping "×" on banner calls existing exit-private logic.

**WHY:** HC-VQA-1 (only signal is a tiny crossed-eye icon = privacy misread risk), D-VQA-3, Q-VQA-3 default (a) + (b).

**SUCCESS CRITERIA:**
- Enter private mode from menu → visible banner appears at top
- New tab in private mode → page background is dark, not white
- Chrome (bottom toolbar) already darkens correctly (existing behavior)
- Visual QA `09-private-mode` shows unmistakable private state
- Non-private mode unchanged

**DEPENDS ON:** T-VQA-2 (menu provides "New Private Tab" entry point)
**USER GATES:** Q-VQA-3

---

## Phase 2: P1 (Quality Bar for TestFlight)

### T-VQA-4: Settings Polish
**WHERE:**
- Modify: `maho/ios-shell/Views/Settings/SettingsRootView.swift`

**HOW:**
1. Add section headers above each grouped card: `"Content"` (General / Appearance / Privacy & Security), `"Account"` (Sync), `"Browsing"` (Reader / Autofill / Notifications / Language), `"AI"` (Max / Web Agent / Advanced), `"App"` (App Icon / Feedback), `"Danger"` (Reset).
2. Add Lucide `robot` (or `bot`) icon to Web Agent row.
3. Add version + build footer at bottom: `Text("Maho \(Bundle.main.appVersion) (\(Bundle.main.buildNumber))")` in gray, centered, 12pt.
4. Change Reset All Settings icon color to red (matching label) via `.foregroundStyle(.red)`.

**WHY:** Q-VQA-1 default answers + observed defects (Web Agent icon missing, alignment breaks).

**SUCCESS CRITERIA:**
- All 6 section headers render
- Web Agent row has icon aligned with siblings
- Version footer visible at bottom of scroll
- Reset icon is red
- Visual QA `07-settings-root` + `08-settings-scrolled` show grouped, labeled, footer-ed list

**DEPENDS ON:** None
**USER GATES:** None

### T-VQA-6: Tab Grid Favicon Monogram Fallback
**WHERE:**
- Modify: `maho/ios-shell/Views/TabGrid/TabCardView.swift` (or wherever favicon is rendered — grep for the orange-star source)
- New helper: `maho/ios-shell/Support/DomainMonogram.swift`

**HOW:**
1. `DomainMonogram.color(for host: String) -> Color` — deterministic HSL from host hash
2. `DomainMonogram.letter(for host: String) -> Character` — first letter of eTLD+1
3. Fallback view: circle with `color(for:)` background + centered white letter
4. Replace the orange-star placeholder when `tab.faviconData == nil`

**WHY:** Visual QA showed orange star (bookmark glyph) rendered as favicon — a confusing misplacement.

**SUCCESS CRITERIA:**
- Tab with no favicon → shows colored circle + letter (e.g., "G" on blue circle for google.com)
- Tab with real favicon → unchanged
- Star icon no longer appears as favicon

**DEPENDS ON:** None
**USER GATES:** Q-VQA-5

### T-VQA-7: Tab Grid UX Fixes
**WHERE:**
- Modify: `maho/ios-shell/Views/TabGrid/TabGridSheet.swift`
- Modify: `maho/ios-shell/Views/TabGrid/TabCardView.swift` (dedupe pin controls)

**HOW:**
1. Remove one of the duplicated pin controls (keep the pin icon on the right, remove the checkbox-style circle+"Pinned" label on the left)
2. Add space switcher: horizontal tab-strip at the top of the tab grid showing all spaces; tap to switch active space; long-press to reorder
3. Add incognito toggle: small pill above "Close All" — "Personal | Private" segmented control
4. Remove the redundant "1" badge next to "1 tab" header (keep only the subtitle)

**WHY:** Visual QA observed duplicate pin, missing space switcher, missing incognito toggle, redundant tab counter.

**SUCCESS CRITERIA:**
- No duplicate pin control on tab cards
- Space switcher visible at top of tab grid, functional
- Incognito toggle visible, switches to private tab list
- No redundant "1" pill next to "1 tab" subtitle

**DEPENDS ON:** None
**USER GATES:** None

### T-VQA-8: URL Bar Focused-State Cancel Button
**WHERE:**
- Modify: `maho/ios-shell/Views/CommandBar/SuggestionsListView.swift` (from T-VQA-1)
- OR: `maho/ios-shell/Views/HomeSearchView.swift` — add Cancel button as sibling of URL bar when focused

**HOW:**
- Add `Button("Cancel") { searchFocused = false }` as leading item in the top-of-suggestions bar (or trailing sibling of the URL field)
- Style: 17pt system-blue text, 44pt tap target, appears only when `searchFocused == true`

**WHY:** Visual QA — no visible dismiss affordance from URL focus state.

**SUCCESS CRITERIA:**
- URL bar focused → Cancel visible
- Tap Cancel → focus dismissed, keyboard hidden, home content restored

**DEPENDS ON:** T-VQA-1
**USER GATES:** None

---

## Phase 3: P2 (Polish)

### T-VQA-9: Onboarding — 3-Step Redesign (Q-VQA-4 scope-expanded to P0-adjacent)
**WHERE:**
- Modify: `maho/ios-shell/Views/Onboarding/OnboardingView.swift`
- New file: `maho/ios-shell/Views/Onboarding/OnboardingPage.swift` (per-step page enum + view)
- Reuse: existing `@AppStorage("onboardingCompleted")` in `MahoApp.swift`

**HOW:**
1. `OnboardingPage` enum: `.welcome`, `.byok`, `.defaultBrowser`
2. Refactor `OnboardingView` to use `TabView(selection: $currentPage)` with `.tabViewStyle(.page(indexDisplayMode: .always))`
3. Each page:
   - **Welcome**: centered Maho logo (SF Symbol placeholder `globe.badge.chevron.backward` in system blue, 120pt) + "Welcome to Maho" title + "A beautiful, fast browser for iOS." body + "Continue" primary button (advances to page 2)
   - **BYOK**: SF Symbol `key.horizontal.fill` accent + "Bring Your Own Key" title + "Use your OpenAI, Anthropic, or local model key for AI features. Your key stays on-device in the iOS Keychain." body + "Set Up AI" secondary link + "Skip" text button (advances to page 3)
   - **Default Browser**: existing SF Symbol from current onboarding + "Set as Default" title + iOS-default-browser explainer + "Set as Default Browser" primary (existing action) + "Not Now" text (finishes onboarding)
4. Bottom-anchored page indicator dots (3 dots, current one filled blue)
5. Swipe left/right between pages OR tap CTAs to advance
6. Final page's "Not Now" and "Set as Default Browser" both set `onboardingCompleted = true`

**WHY:** Q-VQA-4 "3단계 온보딩 재설계" - first impression is critical, brand-only hero is insufficient. BYOK screen is a strategic differentiator for Maho.

**SUCCESS CRITERIA:**
- Onboarding shows 3 pages with page indicator
- Swipe between pages works
- CTA buttons advance correctly
- Final "Not Now" or "Set as Default" finishes onboarding
- Page 2 links to BYOK settings if "Set Up AI" tapped

**DEPENDS ON:** None
**USER GATES:** None (Q-VQA-4 locked)

### T-VQA-10: Menu Row Restyle
**WHERE:**
- Modify: `maho/ios-shell/Views/Menu/MahoMenuSheet.swift` (from T-VQA-2)

**HOW:**
- Ensure rows are left-aligned (not centered), full-width, with hairline dividers between them
- Section headers in uppercase gray, 13pt
- Rows: 44pt height, icon + label + optional trailing chevron
- No pill/action-sheet styling

**WHY:** Visual QA — the current menu popover reads as an iOS alert action sheet, not a browser menu.

**SUCCESS CRITERIA:**
- Menu rows are flat, left-aligned, list-style (matching standard iOS Settings list)

**DEPENDS ON:** T-VQA-2
**USER GATES:** None

### T-VQA-11: Sync Row Status Subtitle
**WHERE:**
- Modify: `maho/ios-shell/Views/Settings/SettingsRootView.swift` — Sync row cell

**HOW:**
- Fetch sync state: `MahoBridge.shared.getSyncStatus()` (already exists in bridge)
- Render subtitle beneath "Sync" label: `"Signed in as \(email)"` OR `"Off — tap to sign in"` OR `"Syncing…"`

**WHY:** Visual QA — Sync row currently has no state indication.

**SUCCESS CRITERIA:**
- Sync row shows current account state
- No account → "Not signed in"
- Signed in → shows email address

**DEPENDS ON:** T-VQA-4
**USER GATES:** None

### T-VQA-12: Reset Icon Red
Covered inside T-VQA-4 step 4.

---

## Phase 4: Verification

### T-VQA-13: Visual QA Re-Capture
**WHERE:**
- Reuse: `maho/ios-shell/UITests/VisualQAScreenshots.swift` (already exists from this session)

**HOW:**
1. After each Wave completes, run:
   ```bash
   cd maho/ios-shell && xcodebuild \
     -project Maho.xcodeproj \
     -scheme Maho \
     -destination 'platform=iOS Simulator,name=iPhone 17 Pro' \
     -only-testing:MahoUITests/VisualQAScreenshots test
   ```
2. Extract screenshots via `xcrun xcresulttool export attachments`
3. Diff against baseline set in `.tmp/screenshots/baseline/` (create baseline from this plan's Reference set)
4. Also run full UITest suite to confirm no regression:
   ```bash
   xcodebuild ... -only-testing:MahoUITests test
   ```

**WHY:** HC-VQA-5 (no UITest regression) + HC-VQA-1 (must verify fixes actually landed visually).

**SUCCESS CRITERIA:**
- All 8 VQA screenshots differ meaningfully from baseline (each shows the intended fix)
- Full UITest suite 7/7 pass (existing tests) + VisualQAScreenshots 1/1 pass
- Rust tests still 212/212

**DEPENDS ON:** All prior T-VQA tasks
**USER GATES:** None

---

## Commit Strategy (Atomic PRs)

| PR | Tasks | Branch | Commit |
|----|-------|--------|--------|
| PR-VQA-1 | T-VQA-1, T-VQA-8 | `feat/ios-url-bar-suggestions` | `feat(ios): URL bar suggestion list + Cancel button on focus` |
| PR-VQA-2 | T-VQA-2, T-VQA-10 | `feat/ios-menu-sheet` | `feat(ios): MahoMenuSheet with proper action list + list-style rows` |
| PR-VQA-3 | T-VQA-3 | `feat/ios-home-content` | `feat(ios): home screen recent tabs + top sites + brand hero` |
| PR-VQA-4 | T-VQA-5 | `feat/ios-private-mode-differentiation` | `feat(ios): private mode dark page background + persistent banner` |
| PR-VQA-5 | T-VQA-4, T-VQA-11, T-VQA-12 | `fix/ios-settings-polish` | `fix(ios): settings section headers, Web Agent icon, sync status, version footer, red reset icon` |
| PR-VQA-6 | T-VQA-6, T-VQA-7 | `fix/ios-tab-grid` | `fix(ios): tab grid favicon monogram fallback + space switcher + incognito toggle + dedupe pin` |
| PR-VQA-7 | T-VQA-9 | `feat/ios-onboarding-hero` | `feat(ios): onboarding brand hero + centered layout` |
| PR-VQA-8 | T-VQA-13 | `chore/visual-qa-regression` | `chore(ios): re-run visual QA after remediation + attach baseline screenshots` |

---

## Timeline (Solo Developer, Revised per Q-VQA-4 scope expansion)

| Phase | Wave | Tasks | Days |
|-------|------|-------|------|
| P0 | Wave 1 | T1, T3 (Arc-style, reduced), T9 (3-step, expanded) | 3 |
| P0 | Wave 2 | T2 (15-action sheet), T5 | 2 |
| P1 | Wave 3 | T4, T6, T7, T8 | 2 |
| P2 | Wave 4 | T10, T11 | 0.5 |
| Verify | Wave 5 | T13 | 0.5 |
| **Total** | | | **~8 days** |

**Net timeline impact of Q-VQA decisions:**
- Q-VQA-2 (Arc-style home) → T-VQA-3 shrinks by ~1 day (no recent/top-sites work)
- Q-VQA-4 (3-step onboarding) → T-VQA-9 grows by ~1 day (page indicator + swipe + 3 pages)
- Net: unchanged from original 7.5 days estimate.

---

## Risks & Mitigations

- **R-VQA-1: Suggestions overlay may compete with keyboard for space.** Mitigation: use `.keyboardShortcut` avoidance + `GeometryReader` to clamp height to `min(400, availableHeight - keyboardHeight - safeArea)`. Test on iPhone 15 Mini + iPhone 17 Pro Max.
- **R-VQA-2: MahoMenuSheet actions rely on sheet-state routing that may conflict with existing sheets.** Mitigation: dismiss the menu sheet before presenting downstream sheets (bookmarks, history, etc.) via `showMenu = false; DispatchQueue.main.async { showBookmarks = true }`.
- **R-VQA-3: Private mode CSS injection could break existing sites.** Mitigation: use `@media (prefers-color-scheme: dark)` guard OR gate to `about:blank`/`data:` URLs only.
- **R-VQA-4: Domain monogram color may collide with real favicon color and confuse users.** Mitigation: use a distinct visual signal (dashed border or "no favicon" pill overlay).
- **R-VQA-5: UITest identifiers may accidentally get renamed when refactoring HomeSearchView.** Mitigation: T-VQA-13 fails fast if any UITest breaks.

---

## Adversarial Provenance
This plan was NOT run through hyperplan. It is a direct remediation plan based on visual evidence from 8 simulator screenshots. If the user wants adversarial review of this plan, run `/hyperplan` before starting Wave 1.

---

## Awaiting Decisions

**All 5 gates resolved (2026-07-10):**
- Q-VQA-1 → 단일 시트 15개
- Q-VQA-2 → Arc-style 검색바 중심 (recentTabs/topSites 제외)
- Q-VQA-3 → 다크 웹뷰 + 상단 배너
- Q-VQA-4 → 3단계 온보딩 재설계
- Q-VQA-5 → Monogram 원

Plan is fully unblocked. Ready to start Wave 1.
