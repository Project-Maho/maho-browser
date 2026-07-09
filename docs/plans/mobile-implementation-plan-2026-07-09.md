# Maho Mobile Implementation Plan — iOS TestFlight → Android Public Beta

**Date:** 2026-07-09
**Provenance:** Derived from hyperplan adversarial review (5 members: skeptic/validator/researcher/architect/creative — 1 full round + 1 compressed attack/defend pass) and formalized by the plan agent.
**Scope:** Mobile app gaps identified in the 2026-07-08 mobile status analysis, EXCLUDING browser extension / WebExtensions support.
**Target sequence:** iOS TestFlight beta FIRST → Android public beta AFTER iOS.

---

## User Decisions (Locked 2026-07-09)

| Gate | Decision | Impact |
|------|----------|--------|
| **Q1** | **10/10 엔티티 전부 검증 + 통합 테스트 추가** | T4 = full audit, all 10 sync entities, integration test per entity |
| **Q2** | **미확인 — 먼저 iOS e2e 테스트 필요** | T10 (Android JNI) BLOCKED until iOS conversation e2e verified. New task T10a inserted BEFORE T10. |
| **Q3** | **10명 미만 (지인/친구/얼리 알파)** | T2 CI = nice-to-have, NOT blocker for T9 TestFlight submission. Land CI opportunistically. |
| **Q4** | **풀 UX — 타이포/폰트 크기/다크모드 토글** | T6/T15 include `ReaderSettingsSheet` with font picker, size slider, dark/light/sepia toggle. +2 days per platform. |
| **Q5** | **Android WebView는 이미 완전히 구현되어 있음** (조사 결과) | 🎯 MAJOR SCOPE REDUCTION — see below |

### Q5 Discovery: Android web rendering is ALREADY implemented

Post-decision exploration found the plan-agent's original scan missed the actual rendering path:

- **`WebViewHost.kt`** — creates `android.webkit.WebView` in Compose `AndroidView`, calls `loadUrl()`
- **`WebViewController.kt`** — `MahoWebViewClient extends WebViewClient` fully implemented (`shouldOverrideUrlLoading`, SSL error handling, all lifecycle callbacks) + `MahoWebChromeClient`
- **`MainBrowserScreen.kt` line 822** — invokes `WebViewHost(...)` for the active browsing tab
- **`MainBrowserScreen.kt` lines 1122-1154** — **`buildDisplayOptionsScript()` already injects reader mode CSS via `evaluateJavascript()`** → T15 largely complete
- **`MainBrowserScreen.kt` lines 189-199** — `clearBrowsingData()` uses `CookieManager.removeAllCookies()`, `WebStorage.deleteAllData()`, `WebView.clearCache()` → T14 mechanism exists

**Architectural fact:** Single shared `WebView` instance across all tabs (`webViewRef: WebView?` in `MainBrowserScreen` state), content swapped via `loadUrl()` on tab activation. NO per-tab WebView isolation.

**Impact on original tasks:**
- **T12** (WebViewClient skeleton) → **DELETE task** — MahoWebViewClient already exists with all callbacks. Folded into T11.
- **T11** (Android content blocking) → **SMALL** — just add `shouldInterceptRequest` override to existing `MahoWebViewClient` (~20 LOC).
- **T14** (Android incognito clear-on-exit) → **DESIGN DECISION NEEDED** — existing `clearBrowsingData()` is global wipe, not per-session. Since architecture is single-WebView, per-tab isolation would require major refactor. Recommendation: for v1 beta, incognito close triggers global clear (accept UX limitation; document in changelog).
- **T15** (Android reader mode) → **VERIFY EXISTING** — likely already done via `buildDisplayOptionsScript()`; task becomes "add Readability.js layer + Q4 typography sheet on top of existing infrastructure" instead of building from zero.

---

## Context

Maho Browser's mobile shells (iOS at `maho/ios-shell/`, Android at `maho/android-shell/`) have functional cores but several gaps preventing beta shipment. Sync remediation Phases 1-2+ are already committed. This plan closes remaining gaps for iOS TestFlight submission first, then Android.

**Key findings from codebase exploration:**
- iOS code signing is disabled (`project.yml:21-23`: `CODE_SIGN_IDENTITY: ""`, `CODE_SIGNING_REQUIRED: "NO"`)
- `tick()` has 2 overloads: `MahoBridge.swift:93` (→ `String?`) and `MahoBridge+Lifecycle.swift:22` (→ `[CoreUpdate]`)
- iOS conversations fully wired (6 FFI functions confirmed at `maho-ffi/src/lib.rs:6772-6956`)
- Android `MahoBridge.kt:1013-1018` has 6 conversation stubs returning constants — zero JNI extern functions for conversations exist
- **Android has NO WebViewClient / WebView implementation** (Compose + JNI bridge only) — see Q5
- `ContentBlocker::should_block_request()` exists at `content_blocker.rs:123` — ready for JNI query
- 3 Appium test files (9 test cases), 9 Android unit test files, 8 iOS test files
- No mobile CI workflow exists in `.github/workflows/`

---

## Hard Constraints (Survived Adversarial Review)

- **HC1: Sync remediation Phase 1-2+ is ALREADY committed** (git `a689cb28`, `30136207`). Mobile sync work == AUDIT + gap-close, NOT fresh implementation.
- **HC2: iOS code signing is prerequisite ZERO**. Without it, TestFlight submission is impossible. First iOS task, blocks all iOS shipping.
- **HC3: Web Push on in-app WKWebView is TECHNICALLY INFEASIBLE per Apple**. iOS 16.4 Web Push is Safari home-screen PWAs only. Gap #7 dropped from v1 for iOS.
- **HC4: Chat conversation APIs already exist in maho-core + maho-ffi + iOS `MahoBridge+Conversations.swift`**. Only Android needs ~8 JNI extern functions.
- **HC5: Content blocking rule schema DIVERGES between iOS WKContentRuleList and Android WebViewClient.shouldInterceptRequest**. Plan defines "mobile subset" schema in maho-core.
- **HC6: ASCredentialProviderExtension is a multi-week iOS effort**, not a bridge wire. Drives D8 (system autofill for beta).
- **HC7: Android AutofillService requires manifest + user manual system-settings enable**. Not passive registration.
- **HC8: Every new bridge symbol MUST land in BOTH maho-ffi AND maho-jni together**, even when UI is iOS-only. JNI extern exists as stub returning defined error until Android UI catches up.
- **HC9: iOS `tick()` overload uses Swift return-type discrimination** — fragile latent defect. MUST be renamed before any AI/chat work adds more callsites.
- **HC10: `maho-core` owns filter list compilation, dual output format** — WebKit JSON for iOS + per-request query API for Android. NOT a static bundled hostlist.
- **HC11: No mobile CI currently exists**. Minimal PR-run GHA must land BEFORE feature-work commits.

---

## Decisions (Converged Through Debate)

- **D1:** Sync mobile work is AUDIT + gap-close (see HC1 + Q1).
- **D2:** Web push DROPPED for v1 on both platforms.
- **D3:** iPad/landscape DROPPED for TestFlight beta.
- **D4:** Custom Tabs postMessage DROPPED from Android v1.
- **D5:** x86_64 ABI DROPPED from Android release; kept in CI/debug variant for emulator tests.
- **D6:** Reader mode via Mozilla Readability.js drop-in (Apache 2.0), NOT a native extractor.
- **D7:** Tab clustering as pure UI grouping (domain + time-window heuristic), NO on-device ML.
- **D8:** Password autofill DELEGATES to system autofill (iCloud Keychain / Google Password Manager / 1Password / Bitwarden) for v1 beta on BOTH platforms. `ASCredentialProviderExtension` and `AutofillService` deferred post-beta.
- **D9:** Offline AI model — keep core state machine + FFI/JNI stubs; NO new UI work in v1.
- **D10:** Android content blocking via ~20-40 LOC `WebViewClient.shouldInterceptRequest` over existing maho-core dynamic filter list infrastructure. Staged rollout flag default OFF + per-site exception UX.
- **D11:** Android incognito = clear-on-exit non-persistent WebView state; full profile isolation post-beta.
- **D12:** Android chat/conversation persistence via ~8 JNI extern functions mirroring iOS FFI. No maho-core work needed.
- **D13:** Trivial fixes bundled into a single "hygiene" PR (tick rename, manifest permissions, profile handler, FAQ copy).
- **D14:** Mobile CI = single GHA workflow running existing tests on PR. No matrix, no Appium, no screenshots, no release automation.

---

## USER INPUT GATES — Questions Requiring Your Decision

**Q1: Sync entity coverage target for mobile**
The sync roadmap declares 10 entity types in `SyncEntityType`. Post-commits `a689cb28` and `30136207`, Phases 1-2 protocol fixes + entity coverage landed. Should the mobile sync audit task:
- **(a)** Verify all 10/10 entities are wired end-to-end and add integration tests for each?
- **(b)** Accept "current coverage is enough for beta" and only add tests for what already works?

**Q2: iOS conversation persistence — manual verification gate**
`MahoBridge+Conversations.swift` is fully wired (6 FFI symbols confirmed). Before mirroring this to Android JNI, can you confirm that iOS conversations work end-to-end? (Create → close app → reopen → conversation restored?) This validates the spec that Android JNI will mirror.

**Q3: TestFlight beta audience size**
How many TestFlight testers are planned?
- If <10: CI (D14) becomes nice-to-have, not pre-launch blocker
- If ≥50: CI is mandatory before feature work

**Q4: Reader mode UX scope for v1**
Readability.js will handle article extraction. Is reader mode v1:
- **(a)** Just the extracted content in a clean wrapper (minimal styling)?
- **(b)** Full reader UX with typography controls, dark/light toggle, font size slider?

**Q5 (NEW — discovered during exploration): Android tab rendering architecture**
The Android shell currently has NO WebView or CustomTabs code. It's pure Compose + JNI bridge. Tasks T11 (content blocking via `shouldInterceptRequest`), T14 (incognito clear), and T15 (reader mode JS injection) all assume a WebView exists. How do Android tabs currently render web content? This determines whether T12 is "add WebViewClient to existing WebView" or "implement WebView-based tab rendering from scratch" (much larger scope).

---

## Task Dependency Graph (Updated per Q1-Q5)

```
Wave 1 (No dependencies — batch-able):
├── T1: Hygiene PR (tick rename, manifest fixes, profile handler, FAQ)
├── T2: Mobile CI workflow [Q3=<10 → nice-to-have, non-blocking]
├── T3: iOS Code Signing setup
├── T5: Content blocking dual-output in maho-core + JNI query
├── T6: Reader Mode iOS — Readability.js + FULL UX (Q4=b)
└── T7: Tab Clustering UI
   [T12 DELETED — MahoWebViewClient already exists]

Wave 2 (After Wave 1):
├── T4: Sync Audit + Gap-Close — 10/10 ENTITIES (Q1=a)
├── T8: iOS Content Blocking Integration (depends T5)
├── T10a: iOS Conversation e2e Verification (Q2 unblocks T10)
└── T13: Android Manifest + Permissions

Wave 3 (After Wave 2):
├── T9: iOS TestFlight Submission (depends T1+T3+T4+T8; T2 optional per Q3)
├── T10: Android Conversation JNI (depends T1+T10a)
└── T14: Android Incognito Clear (global-clear approach; no isolation for v1)

Wave 4 (After Wave 3 — Android feature work):
├── T11: Android Content Blocking — add shouldInterceptRequest to existing MahoWebViewClient (depends T5)
└── T15: Android Reader Mode — verify existing + add Readability.js + FULL UX (Q4=b)

Wave 5 (Final milestone):
└── T16: Android Public Beta (depends T10+T11+T13+T14+T15)
```

**Solo developer note:** Waves represent logical dependency ordering. Execute serially within each wave.

---

## Phase 1: iOS TestFlight Prep

### T1: Hygiene PR (Batch of Trivial Fixes)
**WHERE:** `MahoBridge.swift:93` + `MahoBridge+Lifecycle.swift:22` (rename `tick()` → `tickJson()` / `tickDrain()`); `MahoBridge+Profiles.swift:44-57` (wire `handleProfileUpdate` cases); `AndroidManifest.xml:4-6,27`; `website/browser/faq.mdx`
**HOW:** Rename overloaded tick + update callsites; wire profile update side-effects; add `CAMERA` + `POST_NOTIFICATIONS`, remove `extractNativeLibs="true"`; update FAQ copy
**WHY:** HC9, D13, R3
**SUCCESS:** `make build-app` succeeds; `grep -r "func tick()" ios-shell/` = 0 hits; `./gradlew app:testDebugUnitTest` passes
**DEPENDS ON:** None **USER GATES:** None

### T2: Mobile CI Workflow
**WHERE:** `.github/workflows/mobile-ci.yml` (new)
**HOW:** PR-triggered GHA — `bun run test`, `bun run typecheck`, `xcodebuild test`, `./gradlew app:testDebugUnitTest`. No matrix, Appium, screenshots, or release automation.
**WHY:** HC11, R6, D14
**SUCCESS:** Workflow passes `actionlint`; all 4 steps green on current codebase
**DEPENDS ON:** None **USER GATES:** Q3

### T3: iOS Code Signing
**WHERE:** `maho/ios-shell/project.yml:21-23` + `exportOptions.plist`
**HOW:** Provisioning profile for `dev.maho.browser`; `CODE_SIGN_IDENTITY: "Apple Distribution"`, `CODE_SIGNING_REQUIRED: "YES"`, `DEVELOPMENT_TEAM: <TEAM_ID>`; verify `xcodebuild archive` + `-exportArchive`
**WHY:** HC2 (prerequisite ZERO)
**SUCCESS:** `xcodebuild archive` produces signed `.xcarchive`; `.ipa` passes `xcrun altool --validate-app`
**DEPENDS ON:** None **USER GATES:** None

### T5: Content Blocking — Dual Output (maho-core + JNI Query)
**WHERE:** `content_blocker.rs:123` (existing); `maho-jni/src/lib.rs` (new extern `nativeQueryFilterDecision`)
**HOW:** TDD-first test asserting known ad-URL blocked; add JNI extern wrapping `core.should_block_request(url, source_url, resource_type)`; existing iOS `get_content_rules()` JSON path unchanged
**WHY:** HC10, HC5, D10, R4
**SUCCESS:** `cargo test -p maho-jni -- test_filter_decision` passes; existing content_blocker tests still pass
**DEPENDS ON:** None **USER GATES:** None

### T6: Reader Mode iOS (Readability.js + Full UX per Q4=b)
**WHERE:** `maho/ios-shell/Support/ReaderMode/` (new); Views/ReaderView + ReaderSettingsSheet
**HOW:** Vendor Mozilla Readability.js (Apache 2.0); WKUserScript injection + WKScriptMessageHandler for extracted article; SwiftUI ReaderView with `ReaderSettingsSheet` — font picker (system/serif/mono), size slider, dark/light/sepia toggle. Persist user prefs via `@AppStorage`.
**WHY:** D6 + Q4=b (full UX)
**SUCCESS:** Reader button on article → clean content view; typography settings persist across app launches; non-article graceful fallback; unit test mock HTML → expected extracted fields
**DEPENDS ON:** None **USER GATES:** None (Q4 answered: full UX)

### T12: ~~Android WebViewClient Skeleton~~ **DELETED**
Q5 investigation confirmed `MahoWebViewClient` already exists at `maho/android-shell/app/src/main/java/dev/maho/browser/ui/WebViewController.kt` with full `WebViewClient` overrides (`shouldOverrideUrlLoading`, `onPageStarted/Finished`, `onReceivedError/SslError`, `onReceivedTitle`). No skeleton work needed. Downstream tasks (T11, T14, T15) add hooks to this existing class.

---

## Phase 2: iOS TestFlight Ship

### T4: Sync Audit + Gap-Close — 10/10 Entities (Q1=a)
**WHERE:** `docs/plans/sync-remediation-roadmap-2026-07-07.md`; `maho-core/src/sync_manager.rs`; `maho-core/src/tests/`
**HOW:** Confirm all sync tests pass post `a689cb28`/`30136207`. For EACH of 10 entity types (Tab, Space, Bookmark, Note, Boost, ReadingListItem, SearchEngine, Memory, Shortcut, Extension) — verify push function + apply handler + mutation site wired. Write ≥1 round-trip integration test per entity. Close any gaps found.
**WHY:** HC1, D1, Q1=a (full 10/10)
**SUCCESS:** All 10 entities have push+apply+test coverage; `bun run test` passes with new integration tests; coverage checklist committed at `docs/plans/sync-entity-coverage-2026-07-09.md`
**DEPENDS ON:** T1 **USER GATES:** None (Q1 answered)

### T8: iOS Content Blocking Integration
**WHERE:** `MahoBridge+ContentRules.swift`; WKWebViewConfiguration; PrivacySettings UI
**HOW:** `WKContentRuleListStore.default().compileContentRuleList(...)` with existing `maho_core_get_content_rules()`; attach to userContentController; `PrivacySettings.contentBlockingEnabled` (**default OFF**); per-site whitelist UX; ship with EasyList top-100 subset first
**WHY:** HC5, HC10, R1 (staged rollout mitigation)
**SUCCESS:** Toggle exists (default OFF); flag ON = doubleclick.net blocked; whitelist per-domain works; graceful fallback on malformed JSON
**DEPENDS ON:** T5 **USER GATES:** None

### T10a: iOS Conversation E2E Verification (NEW — Q2 gate)
**WHERE:** `maho/ios-shell/Tests/ConversationPersistenceTests.swift` (new); manual test protocol
**HOW:** (1) Automated XCUITest: create conversation → send message → force-quit app → relaunch → verify conversation restored with correct messages. (2) Manual test on physical device recorded via Xcode. If e2e FAILS: iOS FFI has a bug — FIX iOS BEFORE T10 (Android JNI mirror).
**WHY:** Q2 answer requires verification before Android mirror; validates spec that JNI will replicate
**SUCCESS:** Automated test passes 3× consecutively; manual recording shows restoration; if failing, iOS bug fix PR lands and test passes green
**DEPENDS ON:** T1 **USER GATES:** None

### T13: Android Manifest + Permissions
**WHERE:** `AndroidManifest.xml`; `MainActivity.kt`
**HOW:** Add `CAMERA` + `POST_NOTIFICATIONS`; remove `extractNativeLibs="true"`; runtime prompt for `POST_NOTIFICATIONS` on Android 13+
**WHY:** D13
**SUCCESS:** `./gradlew lint` clean; Android 13+ shows notification prompt on first launch
**DEPENDS ON:** None (bundle with T1 optional) **USER GATES:** None

### T9: iOS TestFlight Submission
**WHERE:** Xcode archive → App Store Connect
**HOW:** Verify T1/T3/T4/T8 done (T2 optional per Q3=<10 testers); increment `CFBundleVersion`; `xcodebuild archive` → `-exportArchive` → `xcrun altool --upload-app`; submit TestFlight review; invite ≤10 alpha testers
**WHY:** Shipping target #1
**SUCCESS:** Upload no validation errors; TestFlight review passes; app launches on physical iPhone; ≤10 alpha invites delivered
**DEPENDS ON:** T1, T3, T4, T8 (T2 recommended but not blocking per Q3) **USER GATES:** None

---

## Phase 3: Android Beta Prep

### T10: Android Conversation JNI (Post-T10a)
**WHERE:** `maho-jni/src/lib.rs` (6 new externs); `MahoBridge.kt:1013-1018`
**HOW:** TDD-first round-trip test; add 6 JNI functions mirroring `maho-ffi/src/lib.rs:6772-6956` (`nativeCreateConversation`, `nativeListConversations`, `nativeGetConversationMessages`, `nativeSaveConversationMessage`, `nativeDeleteConversation`, `nativeRenameConversation`); replace Kotlin stubs; sync `ConversationSession`/`ConversationTurn` data classes with iOS
**WHY:** HC4, HC8, D12, R5
**SUCCESS:** `cargo test -p maho-jni -- conversation` passes; `./gradlew app:testDebugUnitTest` passes; lines 1013-1018 no longer hardcoded; Android e2e mirrors iOS T10a spec
**DEPENDS ON:** T1, T10a (Q2 e2e verification) **USER GATES:** None (Q2 answered — verify via T10a)

### T11: Android Content Blocking — Hook Existing MahoWebViewClient
**WHERE:** `MahoBridge.kt` (add `nativeQueryFilterDecision` extern + Kotlin wrapper); `WebViewController.kt` (add `shouldInterceptRequest` override to existing `MahoWebViewClient` class); Settings UI
**HOW:** Q5 confirmed `MahoWebViewClient` already exists. Just override `shouldInterceptRequest(view: WebView, request: WebResourceRequest)`:
```kotlin
override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
    if (!privacySettings.contentBlockingEnabled) return null
    if (isWhitelisted(request.url.host)) return null
    val blocked = bridge.queryFilterDecision(request.url.toString(), view.url ?: "", mapResourceType(request))
    return if (blocked) WebResourceResponse("text/plain", "utf-8", 204, "Blocked", emptyMap(), null) else null
}
```
Default OFF (R1); per-site whitelist; TDD mock test.
**WHY:** HC5, HC10, D10, R1
**SUCCESS:** `ContentBlockingTest` passes; flag ON = ad URL intercepted; flag OFF = pass-through; whitelist bypasses
**DEPENDS ON:** T5 **USER GATES:** None

### T14: Android Incognito — Global Clear (v1 compromise per Q5 architecture)
**WHERE:** `MainBrowserScreen.kt:189-199` (existing `clearBrowsingData()`); tab close hook
**HOW:** Q5 revealed single shared `WebView` architecture — full per-tab isolation would require major refactor. For v1 beta: on last-incognito-tab close, call existing `clearBrowsingData()` (`CookieManager.removeAllCookies()`, `WebStorage.deleteAllData()`, `WebView.clearCache/History/FormData`). Accept global clear as beta limitation. Beta changelog: "closing all incognito tabs also clears normal tabs' session state — full profile isolation coming post-beta".
**WHY:** D11 (accept beta limitation per Q5 constraint), R2
**SUCCESS:** Closing last incognito tab triggers clear; unit test verifies clear methods invoked; changelog documents limitation
**DEPENDS ON:** None (WebView + clearBrowsingData already exist) **USER GATES:** None

### T15: Android Reader Mode — Verify + Enhance (Q4=b Full UX)
**WHERE:** `MainBrowserScreen.kt:1122-1154` (existing `buildDisplayOptionsScript()`); `app/src/main/assets/readability.js` (new — same as T6); `ReaderModeExtractor.kt` (new); Compose `ReaderSettingsSheet`
**HOW:** (1) VERIFY existing `buildDisplayOptionsScript()` behavior — does it already extract article content, or just apply CSS to raw page? (2) If just CSS, layer Readability.js on top: bundle asset, inject via `evaluateJavascript()`, receive extracted content via `@JavascriptInterface`, render in Compose ReaderView. (3) Add `ReaderSettingsSheet` — font picker, size slider, dark/light/sepia toggle. Persist prefs via DataStore.
**WHY:** D6 + Q4=b
**SUCCESS:** Test article renders extracted content in Compose ReaderView; typography settings apply and persist; dismiss returns to WebView; non-article graceful fallback
**DEPENDS ON:** T6 (Readability.js validated on iOS) **USER GATES:** None

---

## Phase 4: Android Beta Ship

### T16: Android Public Beta Submission
**WHERE:** `build.gradle.kts`; Play Console (or GitHub release-only repo per AGENTS.md)
**HOW:** Verify T10/T11/T12/T13/T14/T15 done; release variant = `arm64-v8a` only (D5); debug/CI variant adds `x86_64`; `./gradlew app:bundleRelease`; sign with upload key; upload to open testing track
**WHY:** Shipping target #2
**SUCCESS:** Play Console pre-launch report passes (no crashes); open testing distributable; CI x86_64 emulator tests pass; release APK/AAB is arm64-v8a only
**DEPENDS ON:** T10, T11, T13, T14, T15 **USER GATES:** None

---

## Commit Strategy (Atomic PRs)

| PR | Tasks | Branch |
|----|-------|--------|
| PR1 | T1 | `fix/mobile-hygiene` |
| PR2 | T2 | `ci/mobile-pr-checks` |
| PR3 | T3 | `build/ios-code-signing` |
| PR4 | T5 | `feat/content-blocking-jni-query` |
| PR5 | T7 | `feat/tab-clustering` |
| PR6 | T6 | `feat/ios-reader-mode` |
| PR7 | T4 | `test/sync-entity-audit` |
| PR8 | T8 | `feat/ios-content-blocking` |
| PR9 | T13 | `fix/android-permissions` |
| PR9a | T10a | `test/ios-conversation-e2e` |
| PR10 | T10 | `feat/android-conversation-jni` |
| PR11 | T14 | `feat/android-incognito-clear` |
| PR12 | T11 | `feat/android-content-blocking` |
| PR13 | T15 | `feat/android-reader-mode` |

**Rules:** Each PR independently reviewable; CI (PR2) must be green before any feature merge; HC8 compliance (T5 lands JNI symbol before Android UI uses it); squash-merge to main.

---

## Estimated Timeline (Solo Developer, Full-Time)

| Phase | Weeks | Milestone |
|-------|-------|-----------|
| Wave 1 (T1,T2,T3,T5,T6,T7) | 1.5-2 | Foundation complete (T12 dropped per Q5) |
| Wave 2 (T4,T8,T10a,T13) | 1.5-2 | iOS feature-complete + conversation e2e verified |
| Wave 3 (T9,T10,T14) | 1 | **iOS TestFlight submitted** (~4-5 wks) |
| Wave 4 (T11,T15) | 1 | Android feature-complete (both smaller per Q5) |
| Wave 5 (T16) | 0.5 | **Android Public Beta** (~5-7 wks total, faster than original) |

---

## Risks & Mitigations

| Risk | Where Mitigated |
|------|-----------------|
| R1: Content blocking breaks currently-tolerated pages | T8, T11: default OFF, per-site exception, reduced ruleset |
| R2: Incognito flip breaks in-flight requests | T14: clear-on-exit only, no isolation |
| R3: iOS `tick()` silent nil returns | T1: rename before feature work |
| R4: Filter schema mobile subset must be defined | T5: dual-output architecture in maho-core |
| R5: JNI parity gap for chat conversations | T10: 6 JNI functions mirroring iOS FFI |
| R6: No CI safety net for regressions | T2: lands before feature PRs |
| R7: Stub public API for offline AI | D9 preserved — stubs stay, no deletion |
| R8: Solo founder burnout / bandwidth | Sequential waves, iOS-first, no parallel streams |

---

## Adversarial Provenance

- **skeptic** findings that survived: 4 of 10 (S6/S7/S8/S9 stand; S1 "delete sync" defeated by git-log evidence; S2 absorbed into iOS-first sequencing; S3 "delete offline AI" refined; S4 "reader mode CSS as-is" superseded by C1 Readability.js; S5 absorbed into D8; S10 softened by V5)
- **validator** findings that survived: 7 of 7 (V1-V7 all uncontested or defended)
- **researcher** findings that survived: 6 of 6 (R1-R6 all defended with citation)
- **architect** findings that survived: 3 of 4 (A1 refined into D12; A2 refined into HC10/D10; A4 stands as HC8; A3 conceded — sync Phase 1-2+ done)
- **creative** findings that survived: 6 of 10 (C1/C4/C6/C7/C10 stand; C2/C3 refined into D8; C8 refined into D9; C5 defeated by researcher R4 + architect A2; C9 partially conceded — minimal CI lands per D14)
- **Total findings filtered out (conceded/defeated):** ~10

---

## Decisions Locked (2026-07-09)

| Gate | Locked Decision | Plan Impact |
|------|-----------------|-------------|
| **Q1** | 10/10 엔티티 전부 검증 + 통합 테스트 | T4 = full audit, ≥10 integration tests |
| **Q2** | 미확인 — 먼저 iOS e2e 테스트 | Insert **T10a** (iOS conversation e2e) BEFORE T10 |
| **Q3** | 10명 미만 알파 | T2 CI = optional, not blocking T9 TestFlight |
| **Q4** | 풀 UX (타이포/폰트/다크모드) | T6, T15 include ReaderSettingsSheet |
| **Q5** | Android WebView 이미 완전 구현됨 | **T12 DELETED**, T11/T14/T15 significantly reduced |

**Net timeline impact:** Original ~6-8 weeks → **~5-7 weeks** (T12 dropped, T11/T14/T15 reduced, T4 slightly expanded per Q1=a, +T10a for Q2).

**First action:** Start **PR1 (T1 hygiene)** and **PR3 (T3 iOS code signing)** in parallel — both are unblocked, independent, and small.
