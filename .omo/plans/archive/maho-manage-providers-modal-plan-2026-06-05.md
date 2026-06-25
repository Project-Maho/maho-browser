# Manage AI Providers Modal — Plan

**Status:** DRAFT (pending momus review + user approval)
**Author:** Sisyphus
**Date:** 2026-06-05
**Target branch:** main
**Single PR:** YES (≤2시간)
**Builds on:** `.sisyphus/plans/maho-ui-custom-provider-plan-2026-06-04.md`

---

## 1. Goal

`AiPane` 안의 137 builtin + custom 카드 inline list 를 별도 **Manage AI Providers 모달**로 분리한다. AiPane 은 일상 흐름 (Active 변경 + Custom 편집/삭제 + Behavior) 만 표시한다. Built-in 검색/Connect/관리는 모달에서 한다.

## 2. Out of scope

- Active Selection Combobox 변경 — 이미 cmdk fuzzy search 작동, 137+custom 모두 거기서 active 변경 가능
- Provider CRUD logic — 이미 구현됨 (이전 PR)
- OAuth flow logic — 이미 구현됨, modal 안에서 trigger 만 변경
- 새 mojom RPC 추가 없음
- Built-in 의 inline display 완전 제거 (Custom 은 inline 유지)

## 3. Constraints

- **Type safety**: `as any` / `@ts-ignore` / `@ts-expect-error` 금지
- **AGENTS.md anti-pattern**: 메모성 주석 금지, GN 강제 수정 금지
- **모달 패턴**: 기존 `.maho-modal-*` CSS 클래스 재사용 (AddEditProviderModal 와 동일)
- **State 관리**: filter state 는 모달 안에서만 (AiPane 에서 제거)

## 4. Architecture decisions

### D1 — Modal-based browsing for built-in (CONFIRMED)

Built-in 137개 list 는 모달에 격리. AiPane 은 평소 짧은 상태 유지.

### D2 — Custom Providers stay inline (CONFIRMED)

Custom 은 보통 0~10개라 inline 적절. AiPane 에 짧은 list + Add/Edit/Delete 액션 그대로.

### D3 — Filter logic 모달로 이전 (CONFIRMED)

기존 AiPane 의 `providerFilter` state + filter `<input>` + group split 로직을 모달 안으로 이전. 이전 PR 의 `.maho-provider-search` / `.maho-provider-group-heading` CSS 그대로 사용.

### D4 — Modal 안에서 Set Active 가능 (NEW)

Built-in row 에 "Set as Active" 버튼 추가. 클릭 시:
1. `handler.setActiveAI(p.id, p.models[0])` 호출
2. AiPane 의 settings refetch trigger
3. Modal 안 active indicator (`• Active`) 갱신
4. Modal 자동 close (옵션 — UX 결정 필요)

### D5 — Modal close 시 filter 초기화 (CONFIRMED)

모달 닫으면 filter state 리셋. 다음 진입 시 fresh start. 같은 작업 재시도가 흔치 않으므로 단순함이 우선.

## 5. UI structure

### AiPane (after refactor)

```
┌─ Maho AI ─────────────────────────────────────┐
│ AI Configuration                              │
│                                               │
│ ── Active Selection ──                        │
│   Active Provider  [Combobox: 137+custom] ▾  │
│   Active Model     [Combobox] ▾              │
│                                               │
│ ── Custom Providers ──                        │
│   [Inline rows for custom only]               │
│   • my-ollama   Custom (openai-compat) • ... │
│                 [Edit] [Delete]              │
│   • internal-gw                              │
│                                               │
│   [+ Add Custom Provider]                     │
│   [Manage All Providers]                      │
│   [Import Well-Known]                         │
│                                               │
│ ── Behavior ──                                │
│   Approval Policy ...                         │
│   Session Persistence ...                     │
└───────────────────────────────────────────────┘
```

### ManageProvidersModal

```
┌─ Manage AI Providers ─────────────────────[×]┐
│ ┌──────────────────────────────────────────┐ │
│ │ 🔍 Filter providers by name, ID, npm…    │ │
│ └──────────────────────────────────────────┘ │
│                                               │
│ CUSTOM (3)                                    │
│ • my-ollama  • Active                        │
│   Custom (openai-compat) • localhost:11434   │
│                          [Set Active] [Edit] [Delete]
│ • internal-gw                                │
│ • my-anthropic                               │
│                                               │
│ BUILT-IN (137)                                │
│ • OpenAI                                      │
│   Built-in Provider                           │
│                          [Set Active]         │
│ • Anthropic                                   │
│ • GitHub Copilot                              │
│                          [Connect] [Set Active]
│ ...                                           │
│                                               │
│                                       [Close] │
└───────────────────────────────────────────────┘
```

## 6. Component design

### New: `ManageProvidersModal` (in `domain_panes.tsx`)

```typescript
interface ManageProvidersModalProps {
  handler: PageHandlerRemote;
  providers: AIProvider[];
  activeProviderId: string;
  onClose: () => void;
  onSetActive: (providerId: string, modelId: string) => Promise<void>;
  onEdit: (provider: AIProvider) => void;
  onDelete: (providerId: string) => void;
  onConnectOAuth: (providerId: string) => void;
}
```

내부 state:
- `filter: string` — 검색어

내부 로직 (AiPane 에서 이전):
```typescript
const q = filter.trim().toLowerCase();
const matches = (p: AIProvider) => !q ||
  p.displayName.toLowerCase().includes(q) ||
  p.id.toLowerCase().includes(q) ||
  (p.npm || '').toLowerCase().includes(q);
const filtered = providers.filter(matches);
const customs = filtered.filter(p => p.kind === 'custom');
const builtins = filtered.filter(p => p.kind !== 'custom');
```

각 row 에 액션 버튼:
- **Custom**: Set Active (활성 아닐 때) / Edit / Delete
- **Built-in (no creds + auth_type='oauth')**: Connect (OAuth flow 시작)
- **Built-in (creds 있음)**: ✓ Connected indicator + Set Active
- **Built-in (no auth, e.g. ollama)**: Set Active

모달 wrapper: 기존 `.maho-modal-overlay` / `.maho-modal-content` / `.maho-modal-header` / `.maho-modal-body` / `.maho-modal-footer` 재사용.

### Modified: `AiPane`

**제거:**
- `providerFilter` state (line 1334)
- 검색 input + aria-live region + builtin/custom 그룹 rendering 영역 (line 1462-1564)
- 인라인 builtin 카드 list

**유지:**
- Active Selection (Combobox × 2)
- Custom Providers inline list (filter 없는, 짧은 form)
- "Add Custom Provider" / "Import Well-Known" 버튼
- Behavior section
- 기타 modals (AddEdit, Delete confirm, OAuth, WellKnown)

**추가:**
- `showManageModal` state
- "Manage All Providers" 버튼 (Custom Providers 섹션의 button row)
- ManageProvidersModal 렌더링

### Inline custom list rendering (AiPane 안)

```tsx
const customs = providers.filter(p => p.kind === 'custom');

<SectionDivider title="Custom Providers" />
{customs.length === 0 && (
  <div className="maho-provider-empty" style={{padding: '12px 0'}}>
    No custom providers. Click "Add Custom Provider" to add one.
  </div>
)}
{customs.map(p => /* same row pattern as before */)}

<div style={{display: 'flex', gap: '8px', marginTop: '8px'}}>
  <Button onClick={() => setShowAddModal(true)}>+ Add Custom Provider</Button>
  <Button onClick={() => setShowManageModal(true)}>Manage All Providers</Button>
  <Button onClick={() => setShowWellKnownModal(true)}>Import Well-Known</Button>
</div>
```

## 7. File changes

- `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx`:
  - 새 `ManageProvidersModal` 컴포넌트 (~120줄)
  - `AiPane` refactor (~50줄 제거, ~30줄 추가)
- `maho-chromium/browser/resources/maho_settings/settings.css`:
  - 변경 거의 없음 — `.maho-modal-*`, `.maho-provider-*`, `.maho-provider-search` 등 모두 재사용
  - 추가 가능: modal body 의 max-height (`.maho-modal-content--scrollable` 또는 inline) 정도

## 8. Phases (단일 PR, 선형)

| Phase | 변경 | 검증 |
|---|---|---|
| **P1** ManageProvidersModal 컴포넌트 작성 | 새 함수 + props + filter/group/row JSX | typecheck pass / 컴포넌트 자체 import 됨 |
| **P2** AiPane refactor | filter state/JSX 제거, custom-only inline, "Manage" 버튼 + state | typecheck pass / AiPane 정상 render / custom 0개일 때 empty state |
| **P3** 통합 검증 | 빌드 + 수동 QA | 아래 §9 |

## 9. Verification

### 자동
- `cd maho && bun run typecheck` clean
- `lsp_diagnostics` on `domain_panes.tsx` 0 errors
- `build_maho.py` exit 0
- `:any` / `@ts-ignore` grep 결과 0

### 수동 QA
- ✅ AiPane 가 짧음 (custom 0개일 때 Active + 빈 메시지 + 3 버튼 + Behavior 만)
- ✅ "Manage All Providers" 버튼 클릭 → 모달 open
- ✅ 모달 안 검색 input 동작 (display_name / id / npm 모두 매칭)
- ✅ 모달 안 Built-in / Custom 그룹 분리 + 카운트 표시
- ✅ Built-in 의 OAuth Connect 버튼 → OAuthFlowModal trigger
- ✅ Built-in 의 Set Active → AiPane 의 Active Selection 즉시 갱신
- ✅ Custom 의 Edit/Delete → 모달 안에서 또는 AiPane inline 둘 다 동작
- ✅ 모달 close (×, Esc, outside click) — filter state 리셋
- ✅ AiPane 에서 Add Custom 추가 → Custom 섹션 inline 즉시 보임 + 모달 안에서도 보임 (refetch)
- ✅ 회귀: Active Selection Combobox 검색 그대로 작동
- ✅ 회귀: Behavior toggle 영향 없음
- ✅ 회귀: Approval Policy combobox 영향 없음
- ✅ 폰트 사이즈 / 일관성 유지

## 10. Risks

| Risk | Mitigation |
|---|---|
| **OAuth Connect 발견성 저하** — built-in 의 Connect 버튼이 모달 안에만 있으면 처음 사용자가 못 찾을 수도 | "Manage All Providers" 버튼을 prominent 하게 (primary 색상 또는 prominent label). Active Selection Combobox 의 placeholder 메시지 활용 가능 |
| **Set Active 이중 경로** — Combobox 또는 모달 안 둘 다 가능 → 혼란? | OK — 둘 다 같은 mojom RPC 호출. 사용자 자유 |
| **모달 안에서 OAuth flow 진행 시 모달 닫음** — 흐름 복잡 | OAuth 시작 시 ManageProvidersModal 가 자체 close 하고 OAuthFlowModal 가 takeover. 완료 후 사용자가 다시 모달 열거나 AiPane 에서 "✓ Connected" 확인 |
| **Modal stack** — Manage → Edit → AddEdit modal 중첩 | 기존 AddEditProviderModal 도 모달이라 z-index 충돌 가능. Manage 모달 안에서 Edit 클릭 시 Manage 자동 close 후 AddEdit open (단일 modal layer 유지) |
| **Filter state 모달 안 → AiPane 변경 다시 못 봄** | refetch 가 모달 close 시점에서도 일어남. AiPane 의 `providers` state 가 항상 fresh |

## 11. Open questions (구현 전 확정)

- [ ] **Q1** Set Active 후 모달 자동 close? (권장: NO — 사용자가 다른 builtin 도 보고 싶을 수 있음)
- [ ] **Q2** "Manage All Providers" 버튼 label — `Manage All Providers` / `Browse Providers` / `Manage Providers`? (권장: `Manage Providers` — Custom 도 안에 보이므로 "All" 의미 충분)
- [ ] **Q3** Custom 0개일 때 inline 메시지 vs 그냥 안 보임? (권장: 보임 — "No custom providers. Click '+ Add Custom Provider'" 가 발견성 좋음)
- [ ] **Q4** Modal body 높이 — viewport 의 80% / 600px / dynamic? (권장: `max-height: 70vh` + `overflow-y: auto`)

## 12. Non-goals

- 가상화 (137개라 불필요)
- 페이지네이션 (검색으로 충분)
- Provider 카드 detail expansion (현재 row 형식 유지)
- 모달 안 keyboard nav 개선 (기본 Tab/Esc/Enter 만)

## 13. References

- 이전 plan: `.sisyphus/plans/maho-ui-custom-provider-plan-2026-06-04.md`
- AddEditProviderModal: `domain_panes.tsx:799–1048` (모달 패턴 참고)
- OAuthFlowModal: `domain_panes.tsx:1051–1200` (close 패턴 참고)
- 기존 inline filter logic: `domain_panes.tsx:1462–1564` (이전할 logic)
- CSS modal classes: `settings.css:1108–1202`
- CSS provider classes: `settings.css:1207–1290`
