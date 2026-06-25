# Maho Settings — Comprehensive Normalization Plan

**Status:** APPROVED — all decisions resolved (2026-06-07)
**Author:** Sisyphus
**Date:** 2026-06-07
**Target branch:** main
**Estimated effort:** 14-18 hours across 6 phases
**Strategy:** 6 phase-by-phase incremental PRs (sequential, dependency-ordered)
**Execution mode:** A — 각 phase 완료 후 사용자 검증 받고 다음 phase 진행

**Audit source:** 3 parallel reviews of `domain_panes.tsx` (3013 lines) + `settings.css` (1385 lines) + `store.ts` (205 lines) + `panes.ts` + `setting_schema.ts`. Total 40+ issues identified.

---

## 1. Goal

Maho Settings 패널 전반 (`chrome://maho-settings`) 의 일관성, 안정성, 접근성을 일괄 정상화한다. 현재 산재한 silent failure / hardcoded 색상 / a11y gap / pattern divergence 를 design token + 공유 컴포넌트 + 표준 패턴으로 수렴시킨다.

## 2. Out of scope

- 새 기능 추가 (provider CRUD 등 이미 완성된 기능)
- mojom RPC 변경 / 추가
- Rust / FFI / page handler C++ 변경 (전부 WebUI / CSS / TS 영역)
- Browser-side store 구조 대대적 리팩터 (lastActionError 활성화는 포함 OK)
- 설정 schema (`setting_schema.ts`, `panes.ts`) 변경
- 색상 팔레트 자체 디자인 변경 (기존 토큰 + missing 토큰 보강만)

## 3. Constraints

- **Type safety**: `as any` / `@ts-ignore` / `@ts-expect-error` 절대 금지. `useRef<any>` 도 금지.
- **AGENTS.md anti-pattern**: 메모성 주석 금지, GN 강제 수정 금지, 다른 세션 working tree dirty cleanup 금지.
- **CSS naming**: 기존 `.maho-*` / `.setting-*` 두 prefix 패턴 유지. 새 클래스도 동일 prefix 따름.
- **컴포넌트 재사용**: `ActionButton`, `AsyncToggleButton`, `ManagedSettingRow`, `SectionHeader`, `SectionDivider`, `EmptyDescription`, `PaneLoading`, `PaneRetry`, `Combobox`, `TextFieldShell`, `TextareaControl` — 표준 슬롯에 맞으면 100% 재사용.
- **Modal 패턴**: `.maho-modal-*` CSS class 그대로. 새 modal 만들지 않음.
- **단일 PR 회피**: 각 phase 가 독립 build/run 가능해야. 의존성 명시.
- **회귀 0 보장**: 각 phase 별 수동 QA pass.

## 4. Architecture decisions

### D1 — Semantic CSS token system 보강

`--maho-settings-error` / `--maho-settings-error-surface` / `--maho-settings-error-border` 신규 (light + dark 변종). Optional: `--maho-settings-success-bright` 로 `#4ade80` 정렬. 이후 모든 raw hex/rgba 색상 literal → 토큰 ref.

**근거:** 9곳의 `#f87171` 으로 인한 light mode 깨짐 + 색상 변경 시 단일 변경점 확보. 기존 `--maho-settings-success` / `--maho-settings-warning` 패턴 follow.

### D2 — Status banner 공유 컴포넌트

`<StatusBanner tone="ok|warning|error" message={...} action={?}>` 공통화. 현재 4개 패턴 (AiPane actionError, AiPane testResult, OAuthFlowModal error, ConnectWellKnownModal error) 모두 통합.

**근거:** 4곳 분산 = 4곳 따로 수정. 공유 컴포넌트 = single source. `aria-live="polite"` + `role="status"` 또는 `role="alert"` (tone 별) 자동 적용.

### D3 — A11y baseline

모든 modal: `role="dialog"` + `aria-modal="true"` + `aria-labelledby={titleId}`. Focus trap 추가 (Radix Dialog 또는 manual focus management). Close `<button>×</button>` 모두 `aria-label="Close"` + `type="button"`. Live updating text (countdown, password reveal, recovery phrase, result counts) 모두 `aria-live` region.

**근거:** AT users 가 modal 자체를 인식 못 하는 critical gap. 보안 critical 흐름 (password reveal, OAuth) 에서 announcement 없음.

### D4 — Error handling pattern 통일

모든 mutation `onClick` 콜백 wrap pattern:
```tsx
onClick={async () => {
  try {
    await handler.someMutation(...);
    await onReload();
  } catch (e) {
    setActionError(extractErrorMessage(e));
  }
}}
```

또는 store-level: `commit()` helper (이미 AiPane 에 있음) 를 모든 panes 가 채택.

**근거:** 현재 ATCPane / ProfilesPane / ContentBlocker / Extensions / Sync 모두 silent failure. `try/catch` + `<StatusBanner tone="error">` 표시 = single uniform pattern.

### D5 — `store.lastActionError` 활성화

`MahoSettingsStore.lastActionError` channel 이 정의되어 있지만 어떤 pane 도 안 읽음. 두 옵션:
- **Option A**: 활성화 — pane 들이 `store.subscribe` 통해 read, top-level toast 표시.
- **Option B**: 제거 — dead state, 각 pane 이 자체 handle.

**Option A 채택** — toggle (`AsyncToggleButton`) 흐름 들이 store 통해서만 commit 함. 그 흐름 의 silent failure 도 커버 가능.

### D6 — Provider row 컴포넌트 추출

`AiPane:1659-1703` (custom inline) ↔ `ManageProvidersModal:1356-1419` (full list) 의 provider row 렌더 코드 중복. `<ProviderRow provider={p} isActive={...} actions={...} />` 추출.

**근거:** 중복 코드 60줄 × 2 = 120줄. 변경 시 두 곳 동기화 필요. 한 곳에서 수정해도 다른 곳 깨짐.

---

## 5. Issue Inventory (40 items)

### CRITICAL (5)

| ID | Pane / Location | Issue | File:Line |
|---|---|---|---|
| C1 | PasswordsPane | `addPassword`/`updatePasswordUsername`/`deletePassword` silent failure | 2747, 2801, 2823 |
| C2 | OAuthFlowModal | Stale closure `pollHandle` cleanup never fires → OAuth session leak | 1110-1117 |
| C3 | PasswordsPane | `<span className="setting-switch">` static badge — semantic abuse | 2620, 2639 |
| C4 | PasswordsPane | `ManagedSettingRow` nested in `content-card__copy` → CSS color cascade | 2600-2631 |
| C5 | PasswordsPane | Dev capabilities debug string in production UI | 2629 |

### HIGH (12)

| ID | Pane / Location | Issue | File:Line |
|---|---|---|---|
| H1 | settings.css | `--maho-settings-error*` tokens missing (9 raw `#f87171` literals) | settings.css :root |
| H2 | All 4 AiPane modals | No `role="dialog"` / `aria-modal` / `aria-labelledby` | 895, 1171, 1260, 1423 |
| H3 | All 4 modal close buttons | `<button>×</button>` no `aria-label` | 899, 1175, 1264, 1427 |
| H4 | AiPane modals | 17 raw `<Button>` stragglers (should be `ActionButton`) | 976, 1020, 1040-1070, 1201-1217, 1305-1313, 1371-1391, 1471, 1674, 1677 |
| H5 | OAuthFlowModal | `useRef<any>` × 2 timer refs | 1093-1094 |
| H6 | OAuth/Password/Sync | Live updating text no `aria-live` (countdown, reveal, recovery) | 1211, 2788, 2209 |
| H7 | ExtensionsPane | `<SectionHeader>` outside `loaded` branch (floats above loading/error) | 2968 |
| H8 | ContentBlockerPane | "Rebuild" in row instead of `SectionHeader action=` | 2869-2878 |
| H9 | ContentBlocker, Extensions, ATC, Profiles | Mutations silent failure (no try/catch) | 729-740, 676-689, 2875-2926, 2993-3004 |
| H10 | SyncPane | `joinSync` silent failure → user phrase lost | 2173-2180 |
| H11 | ATCPane, ShortcutsPane | Empty state missing | 724-726, 1988-1994 |
| H12 | store.ts | `lastActionError` channel unused — silent toggle errors | store.ts:16, 177, 187 |

### MEDIUM (14)

| ID | Pane / Location | Issue | File:Line |
|---|---|---|---|
| M1 | AiPane | `actionError` inline style + hardcoded `#f87171` + no `role="alert"` | 1584 |
| M2 | AddEditProviderModal | testResult feedback inline style + hardcoded colors | 991-1005 |
| M3 | OAuthFlowModal | OAuth user-code inline style 9 properties, `'32px'` orphan | 1187-1198 |
| M4 | ManageProvidersModal | `#4ade80` "✓ Connected" diverges from `--maho-settings-success` | 1376 |
| M5 | OAuthFlowModal | `.spinner` class undefined, inline style fallback | 1210 |
| M6 | AddEditProviderModal | Radio group no `<fieldset>/<legend>` | 902-917 |
| M7 | Shared | `PaneLoading`/`EmptyDescription` no `aria-live` | 391-416 |
| M8 | ShortcutsPane | per-row Save/Reset no contextual `ariaLabel` | 2031, 2038 |
| M9 | MemoryManagerSection | `window.confirm()` — should use confirm-chip | 190 |
| M10 | MemoryManagerSection | No empty state when `items.length === 0` | ~224 |
| M11 | ThemeStorePane | `onClick={() => loadThemeStore()}` discards Promise → `isBusy` broken | 2268 |
| M12 | AiPane / ManageProvidersModal | Provider row render duplicated 120 lines | 1356-1419 ↔ 1659-1703 |
| M13 | PasswordsPane | Bypass `SectionHeader` (manual `content-card__header`) | 2594 |
| M14 | ExtensionsPane | `toggleExtension` callback + manual reload double-update | 2993-2995 |

### LOW (8)

| ID | Pane / Location | Issue | File:Line |
|---|---|---|---|
| L1 | AddEditProviderModal | `useEffect` missing `baseUrl` dep | 834 |
| L2 | AutofillPane | "Add Address"/"Add Payment" no `ariaLabel` | 2436, 2497 |
| L3 | ProfilesPane | Empty message no call-to-action | 615 |
| L4 | AiPane | Dead layout wrapper around `SectionDivider` | 1649-1651 |
| L5 | SyncPane | "Connected Devices" `ManagedSettingRow` as divider | 2129-2130 |
| L6 | ManageProvidersModal | Delete `ariaLabel` no provider name | 1391 |
| L7 | All 4 modal close | `<button>×</button>` no `type="button"` | 899, 1175, 1264, 1427 |
| L8 | PersonalizationPane | Child textarea not disabled when parent toggle off | 312-330 |

**Total: 5 + 12 + 14 + 8 = 39 items.**

---

## 6. Phase decomposition

### Phase 1 — Critical Bug Fix (P1, ~2-3h)

**Goal:** silent failure 제거, OAuth session leak 차단, PasswordsPane semantic abuse 정정.

**Items:** C1, C2, C3, C4, C5

**File changes:**
- `domain_panes.tsx`:
  - PasswordsPane `AddPasswordRow`/`PasswordRow` 에 `errorMessage` state 추가 + try/catch wrapping (C1)
  - OAuthFlowModal `pollHandleRef = useRef<string|null>(null)` 추가, `setState` 직전 `pollHandleRef.current = s.pollHandle`, cleanup 에서 ref 통해 cancel (C2)
  - PasswordsPane `<span className="setting-switch">` × 2 → `<span className="setting-row__description">` 또는 새 `.setting-badge` class (C3)
  - PasswordsPane provider info 영역 → `content-card__copy` wrapper 제거, 직접 `preferences-card__rows` 또는 `ManagedSettingRow` 사용 (C4)
  - PasswordsPane capabilities debug string `<p>` 제거 또는 structured capability rows (C5)

**Verification:**
- ✅ `bun run typecheck` clean
- ✅ Build green
- ✅ 비밀번호 추가 실패 시 inline 에러 메시지 표시
- ✅ Settings 닫기 → OAuth child 프로세스 SIGTERM (`pgrep opencode` 결과 0)
- ✅ Passwords pane provider 영역 텍스트 색상 정상 (text-primary, not text-secondary)
- ✅ Passwords pane 에 capabilities debug string 안 보임

### Phase 2 — Design Token + Style Cleanup (P2, ~3-4h)

**Goal:** raw color/size literal → CSS token. AiPane modal 17개 Button → ActionButton.

**Items:** H1, H4, M1, M2, M3, M4, M5

**File changes:**

`settings.css`:
```css
:root {
  /* dark scheme */
  --maho-settings-error: #f87171;
  --maho-settings-error-surface: rgba(239, 68, 68, 0.1);
  --maho-settings-error-border: rgba(239, 68, 68, 0.25);
}
@media (prefers-color-scheme: light) {
  :root {
    --maho-settings-error: #c44a4a;
    --maho-settings-error-surface: rgba(220, 38, 38, 0.06);
    --maho-settings-error-border: rgba(220, 38, 38, 0.18);
  }
}
```

신규 클래스:
```css
.maho-status-banner { /* shared variant of maho-active-status */ }
.maho-status-banner--error { ... }
.maho-status-banner--warning { ... }
.maho-status-banner--ok { ... }
.maho-oauth-code-display { ... }  /* OAuth user-code */
.maho-spinner { ... }              /* spinner with @keyframes spin */
.setting-badge { ... }             /* static badge (replaces setting-switch abuse) */
```

`domain_panes.tsx`:
- 신규 `<StatusBanner>` 공유 컴포넌트 (~25 lines) — `tone`, `message`, `action?` props
- AiPane actionError → `<StatusBanner tone="error" message={actionError} />`
- AddEditProviderModal testResult → `<StatusBanner tone={testResult.ok ? "ok" : "error"} message={...} />`
- OAuthFlowModal error → `<StatusBanner tone="error" message={errorMessage} />`
- ConnectWellKnownModal error → 동일
- OAuth user-code 블록 → `<div className="maho-oauth-code-display">{userCode}</div>`
- Spinner inline style → `<div className="maho-spinner" role="status" aria-label="Loading" />`
- ManageProvidersModal `#4ade80` "✓ Connected" → `<span className="setting-badge setting-badge--success">✓ Connected</span>`
- 17 raw `<Button>` → `<ActionButton>` (자세한 list: H4 참조)

**Verification:**
- ✅ light/dark 모드 모두 에러 색상 정상
- ✅ raw `#f87171` / `#4ade80` / `rgba(239,...)` / `rgba(74,...)` grep 결과 0 (CSS class 정의 제외)
- ✅ raw `fontSize:` inline style 0 (모든 token 사용)
- ✅ AiPane modal 17 ActionButton 모두 isBusy 작동 (test/save/connect 클릭 시 disabled)
- ✅ OAuth modal spinner 회전 작동

### Phase 3 — A11y Sweep (P3, ~3-4h)

**Goal:** modal accessibility, live region announcements, contextual aria-labels.

**Items:** H2, H3, H5, H6, M6, M7, M8, L7, L6

**File changes:**

`domain_panes.tsx`:
- 4 modal outer div: `role="dialog" aria-modal="true" aria-labelledby={titleId}` 추가
  - 각 modal 의 `<h2>` 에 `id={titleId}` 추가 (titleId = useId() 또는 string literal)
- 4 modal close `<button>`: `type="button"` + `aria-label="Close"` 추가
- OAuthFlowModal `useRef<any>` × 2 → `useRef<ReturnType<typeof setInterval> | null>(null)`
- OAuth countdown text: `<span aria-live="polite">Expires in {formatTime(countdown)}</span>`
- PasswordRow reveal text: `aria-live="polite"` 추가
- SyncPane recovery phrase: `aria-live="polite"` (또는 focus 이동) 추가
- AddEditProviderModal protocol radio: `<fieldset><legend>Protocol</legend>...</fieldset>` wrap
- ShortcutsPane row Save/Reset: `ariaLabel={`Save shortcut for ${binding.action}`}` / `ariaLabel={`Reset shortcut for ${binding.action}`}`
- ManageProvidersModal Delete: `ariaLabel={`Delete ${p.displayName}`}`

`PaneLoading` / `EmptyDescription` (lines 391-416):
```tsx
function PaneLoading({message}) {
  return <p role="status" aria-live="polite" className="setting-row__description">{message}</p>;
}
function EmptyDescription({message}) {
  return <p role="status" className="setting-row__description">{message}</p>;
}
```

**Verification:**
- ✅ macOS VoiceOver 로 modal 열기 → "dialog, Manage Providers" 로 announce
- ✅ password reveal 클릭 → "Password: xxx" announce
- ✅ OAuth countdown announce
- ✅ shortcut row save 클릭 시 "Save shortcut for Cmd+T" announce
- ✅ all `<button>` (modal close 포함) 에 `type="button"` 또는 `type="submit"` 명시

### Phase 4 — Error Handling Sweep (P4, ~2h)

**Goal:** 모든 mutation handler 에 try/catch + StatusBanner. `store.lastActionError` 활성화.

**Items:** H9, H10, H12, M11

**File changes:**

`store.ts` — 변경 없음 (lastActionError 이미 존재). 대신 pane 들이 read.

`domain_panes.tsx`:
- ATCPane: `onRemove`/`onToggle` try/catch
- ProfilesPane: `Switch`/`Delete` try/catch + pane-level `actionError` state
- ContentBlockerPane: `rebuildContentRules`/`toggleFilterList`/`removeFilterList`/`addFilterList` try/catch
- ExtensionsPane: `toggleExtension`/`removeExtension` try/catch
- SyncPane: `joinSync`/`stopSync`/`generateSyncKey` try/catch + `joinPhrase` 보존 (실패 시 안 비움)
- ThemeStorePane: `onClick={loadThemeStore}` (wrapper 제거) — `isBusy` 정상 작동

각 pane 마다 동일 패턴:
```tsx
const [actionError, setActionError] = useState<string | null>(null);
// ...
{actionError ? <StatusBanner tone="error" message={actionError} /> : null}
```

`AiPane` 의 기존 `commit()` helper 패턴 (line 1526-1533) 을 다른 panes 에도 도입.

`store.ts` 의 `lastActionError` 활성화 옵션:
- 모든 pane 가 `store.subscribe(state => setLastError(state.lastActionError))` 구독
- 또는 `MahoSettingsStore` 에 `getLastActionError()` getter 추가

→ Phase 4 단계에서는 pane-level local state 만 도입 (단순). store-level toast 는 Phase 5+ 또는 별도.

**Verification:**
- ✅ ATC rule remove 실패 시 (mock RPC throw) banner 표시
- ✅ Sync joinSync 잘못된 phrase 입력 시 banner + phrase 보존
- ✅ ContentBlocker rebuild 실패 시 banner
- ✅ ThemeStore Refresh 클릭 시 button "Working…" disabled

### Phase 5 — Structure Consistency (P5, ~3h)

**Goal:** 패턴 일관성 (SectionHeader action=, empty states, ProviderRow extract).

**Items:** H7, H8, H11, M9, M10, M12, M13, M14, L4, L5

**File changes:**

`domain_panes.tsx`:
- ExtensionsPane: `<SectionHeader>` 를 `state.status === 'loaded'` branch 안으로 이동 (H7)
- ContentBlockerPane: "Rebuild" → `<SectionHeader title="Content Blocker" action={<ActionButton label="Rebuild" .../>} />` + "Engine Status" `ManagedSettingRow` 제거 (H8)
- ATCPane empty state: `<EmptyDescription message="No URL routing rules. Add one above." />` (H11)
- ShortcutsPane empty state: `<EmptyDescription message="No keyboard shortcuts." />` (H11)
- MemoryManagerSection empty state: `{items.length === 0 && !loading && <EmptyDescription message="No memories stored. Maho will learn as you chat." />}` (M10)
- MemoryManagerSection `window.confirm` → inline confirm-chip pattern (M9):
  ```tsx
  {confirmingClearAll ? (
    <div className="setting-confirm-chip">
      <span>Clear all memories?</span>
      <ActionButton ... />
    </div>
  ) : (
    <ActionButton onClick={() => setConfirmingClearAll(true)} ... />
  )}
  ```
- 신규 `<ProviderRow>` 컴포넌트 추출 (M12) — `provider`, `isActive`, `actions` (Edit/Delete/Connect/SetActive 등) props
  - AiPane custom row 영역 + ManageProvidersModal renderRow 둘 다 사용
- PasswordsPane manual `content-card__header` → `<SectionHeader title="Saved Passwords" />` (M13)
- ExtensionsPane: `loadExtensions()` 호출 제거 (callback router 가 자동 update — M14)
- AiPane dead layout wrapper 제거 (L4): `<div style={{...justifyContent: space-between}}><SectionDivider /></div>` → `<SectionDivider />`
- SyncPane "Connected Devices" `ManagedSettingRow` → `<SectionDivider title="Connected Devices" />` (L5)

**Verification:**
- ✅ Extensions pane loading 중에 "Installed Extensions" 헤더 안 보임
- ✅ ContentBlocker pane "Rebuild" 가 헤더 우측에 있음
- ✅ ATC / Shortcuts pane 빈 상태 메시지 표시
- ✅ Memory pane "Clear all" 클릭 → inline confirm chip
- ✅ Provider row UI 가 AiPane 와 ManageProvidersModal 에서 동일
- ✅ PasswordsPane SectionHeader 적용 후 visual 차이 없음
- ✅ Extension toggle 클릭 시 list refresh 1번만 (callback)

### Phase 6 — Polish (P6, ~1-2h)

**Goal:** low-priority 8 items.

**Items:** L1, L2, L3, L6, L8

**File changes:**

`domain_panes.tsx`:
- AddEditProviderModal `useEffect` deps: `[protocol, provider, baseUrl]` (L1) — 또는 `if (!baseUrl)` 검사 제거 (mount 시 한 번만)
- AutofillPane "Add Address" / "Add Payment" → `ariaLabel="Add new address"` / `ariaLabel="Add new payment method"` (L2)
- ProfilesPane empty: "No profiles. Enter a name above and click Create." (L3)
- ManageProvidersModal Delete: `ariaLabel={`Delete ${p.displayName}`}` (L6)
- PersonalizationPane: `<TextareaControl disabled={!personalizeNewChats} />` for child textareas (L8)

**Verification:**
- ✅ typecheck clean
- ✅ AT 가 "Delete OpenAI" / "Delete Anthropic" 등 row 별 다르게 announce
- ✅ Personalize toggle off → child textarea greyed + interaction 차단

---

## 7. File-by-file change summary

| File | Phase 1 | Phase 2 | Phase 3 | Phase 4 | Phase 5 | Phase 6 |
|---|---|---|---|---|---|---|
| `domain_panes.tsx` | ~80 lines | ~150 lines | ~120 lines | ~100 lines | ~100 lines | ~30 lines |
| `settings.css` | 0 | ~80 lines (tokens + classes) | ~10 lines (aria) | 0 | ~20 lines (StatusBanner) | 0 |
| `store.ts` | 0 | 0 | 0 | 0 (Option A 시 ~10 lines) | 0 | 0 |
| **Total per phase** | ~80 | ~230 | ~130 | ~100 | ~120 | ~30 |

전체 약 **690 줄** 변경. Net 증가는 적음 (refactor 위주).

---

## 8. Verification matrix

### 자동 (모든 phase 별)

- ✅ `cd maho && bun run typecheck` clean
- ✅ `lsp_diagnostics` on `domain_panes.tsx` 0 errors
- ✅ `python3 maho-chromium/build/scripts/build_maho.py` exit 0
- ✅ Type 위반 grep:
  - `: any` 0
  - `@ts-ignore` 0
  - `@ts-expect-error` 0
  - `useRef<any>` 0
- ✅ Hardcoded color literal grep (settings.css 정의 제외):
  - `#f87171` 0
  - `#4ade80` 0
  - `rgba(239, 68, 68` 0
  - `rgba(74, 222, 128` 0
- ✅ Raw `fontSize:` inline style 0
- ✅ Raw `<Button>` count (이전 17 → 0 in modals after Phase 2)

### 수동 (per pane × per phase)

각 phase 완료 시 영향 받은 panes 의 정상 작동 확인:

**Phase 1:**
- PasswordsPane: 비밀번호 추가/수정/삭제 시 success/failure 모두 인지 가능
- AiPane: GitHub Copilot Connect → settings 닫기 → opencode 자식 프로세스 종료 확인
- PasswordsPane: provider 정보 영역 색상 / capabilities 영역 정상

**Phase 2:**
- 다크/라이트 모드 모두 에러 banner 색상 자연
- AiPane test connection 클릭 → 버튼 disabled → 결과 표시
- OAuth modal 의 user-code visual 동일 (회귀 없음)

**Phase 3:**
- VoiceOver/NVDA 로 modal 인식 + 닫기 버튼 announce
- 비밀번호 reveal / OAuth countdown / recovery phrase announce
- 키보드만으로 modal 진입 → focus trap 작동

**Phase 4:**
- 모든 toggle / mutation 의 실패 시 인지 가능
- ThemeStore Refresh 의 isBusy 정상

**Phase 5:**
- ContentBlocker / Extensions header pattern 일관
- Memory pane window.confirm 사라짐
- Provider row 시각적 동일성 (AiPane vs Modal)

**Phase 6:**
- AT 가 contextual label 정확히 announce
- Personalize toggle off → 자식 비활성화 자연

### 회귀 테스트

각 phase 완료 시 다음 회귀 없음 확인:
- AI 패널 채팅 흐름 (file attachment 포함)
- Settings 다른 pane 들 (Phase 별 영향 없는 곳)
- Build size / load time 큰 변화 없음

---

## 9. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Phase 1 의 PasswordsPane provider 영역 refactor (C4) 가 visual regression 야기 | Medium | 각 변경 후 manual visual check, screen-reader test |
| Phase 2 의 modal 17 Button → ActionButton 변환 시 className 호환성 (`setting-button--primary` modifier 우선순위) | Medium | ActionButton 의 `className` prop 이 base + 추가 className 병합. 기존 사용 패턴 (`<ActionButton className="setting-button--primary" />`) 동일 |
| Phase 3 의 focus trap 도입이 기존 키보드 nav 흐름 깨뜨림 | Medium | Radix Dialog 사용 또는 manual `focusable elements` query — 기존 modal 4개 모두 적용. 첫 PR 후 사용자 테스트 |
| Phase 4 의 `lastActionError` Option A 채택 시 store contract 변경 → AiPane 의 기존 `actionError` state 와 충돌 | Low | Phase 4 에서는 pane-level `actionError` 만 도입. store-level 은 별도 PR |
| Phase 5 의 ProviderRow 추출 시 props 인터페이스 미스 | Low | 두 사용처 (AiPane, ManageProvidersModal) 모두 같은 시점에 변경 |
| Phase 5 의 ExtensionsPane callback router 가 다른 시나리오에서 미발동 | Medium | callback 미발동 케이스 발견 시 Phase 5 의 manual reload 제거 revert + 별도 fix |
| 14-18시간 단일 PR 부담 | High | 6 PR 분할 (Phase 별) |

## 10. Resolved Decisions (formerly Open Questions)

모든 결정 baked in. 추가 사용자 확정 불필요.

- **Q1 → Modal focus trap = Radix `@radix-ui/react-dialog`** — 이미 Radix Popover 사용 중, focus trap / Esc / aria / portal / scroll lock 모두 자동. P3 a11y 작업량 1/3 로 감소.
- **Q2 → light mode error 색상 = `#c44a4a`** — 기존 muted-warm 톤 시스템 (warning amber-warm `#d97706`, success forest `#2e9e6a`) 과 일관. dark `#f87171` 의 light 대응으로 hue 동일.
- **Q3 → `store.lastActionError` Option A2 (Top-level toast)** — `app.tsx` 또는 settings shell 에서 store subscribe → 상단 toast bar (`<StatusBanner tone="error" message={...} onDismiss={...} />`). 모든 toggle silent failure 차단.
- **Q4 → 6 PR (phase 별 분할)** — Critical hotfix 빠른 land + 80~230줄 PR 으로 review 집중도 + phase 단위 revert 안전망. 의존성 chain (P4 → P2, P5 → P1+P2) 따라 순차.
- **Q5 → StatusBanner tone 3개 (ok/warning/error)** — YAGNI. info 사용처 0건, ok 가 informational cover. 향후 필요 시 5분 추가.
- **Q6 → Momus 검토 P1 / P2 / P5 (3 phase)** — Q1=Radix 채택으로 P3 작업량 감소 → P3 momus 불필요. P4/P6 은 mechanical → self-review.

## 11. Execution mode

**Mode A** — Phase 별 사용자 검증 ON. 각 phase 완료 후:

1. 자동 검증 (typecheck, build, type 위반 grep) pass
2. 수동 QA 항목 (`§8 Verification matrix` 참조) 사용자 직접 확인
3. 회귀 0 확인
4. 사용자 OK 받고 다음 phase 진행

회귀 발견 시: 해당 phase revert + plan 조정 + 재시도. 다음 phase 진행 차단.

## 12. Non-goals (다시 한 번 명시)

- mojom RPC / page handler / Rust 변경 0
- 새 panes 추가 / 기존 pane 제거 0
- Color palette 디자인 변경 (token 추가만)
- Settings store 의 큰 리팩터 (Phase 4 의 lastActionError read 만 추가)
- 외부 라이브러리 추가 (Radix Dialog 가 이미 있는 패키지)
- 가상화 (137 provider list 는 Phase 5 의 ProviderRow extract 만)

## 13. Phase dependencies

```
Phase 1 (Critical) ──┐
                     ├──> Phase 4 (Error handling) ──┐
Phase 2 (Tokens) ────┤                                 ├──> Phase 6 (Polish)
                     ├──> Phase 5 (Structure) ────────┤
Phase 3 (A11y) ──────┘                                 │
                                                       └──> done
```

- Phase 1 / 2 / 3 병렬 가능 (서로 독립)
- Phase 4 는 Phase 2 의 `<StatusBanner>` 컴포넌트 의존
- Phase 5 는 Phase 1 / 2 의 변경 위에 build (modal struct, token usage)
- Phase 6 는 모든 phase 완료 후 polish

권장 순서: **1 → 2 → 3 → 4 → 5 → 6** (선형, 각 phase 독립 PR)

## 14. References

- Audit reports:
  - `bg_5a9e8129` — Profiles/ATC/Shortcuts/Sync/ThemeStore (Session ses_16087b62dffeSRgJdV22H0EtU6)
  - `bg_169a1328` — Autofill/Passwords/ContentBlocker/Extensions (Session ses_160879666ffdsNOmJsZ8A2wKvX)
  - `bg_f44f9778` — AI + Personalization deep + cross-cutting (Session ses_160874d42ffeyunvJ18Bn6Vz81)
- 이전 plans:
  - `.sisyphus/plans/maho-ui-custom-provider-plan-2026-06-04.md`
  - `.sisyphus/plans/maho-manage-providers-modal-plan-2026-06-05.md`
  - `.sisyphus/plans/maho-settings-ui-improvement-plan-2026-06-01.md`
- 핵심 파일:
  - `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx` (3013줄)
  - `maho-chromium/browser/resources/maho_settings/settings.css` (1385줄)
  - `maho-chromium/browser/resources/maho_settings/react/store.ts` (205줄)
  - `maho-chromium/browser/resources/maho_settings/react/ui/combobox.tsx`
  - `maho-chromium/browser/resources/maho_common/react/primitives/index.ts` (Button)
- 디자인 시스템 토큰 (settings.css):
  - text scale: `--maho-settings-text-2xs/xs/sm/base/md/lg/xl/3xl`
  - color: `--maho-settings-text-primary/secondary/muted/accent`, `--maho-settings-warning`, `--maho-settings-success`, `--maho-settings-success-surface`
  - layout: `--maho-settings-space-1..6`, `--maho-settings-input-radius`, `--maho-settings-radius-pill`, `--maho-settings-control-height`
  - transition: `--maho-settings-transition`
