# Zen Boost Rust-First Parity Implementation Plan

**Created:** 2026-05-25  
**Status:** Planning-ready, Rust-first architecture plan  
**Reference baseline:** Zen Browser Boosts (`zen-browser/desktop` @ `8f905e7abd9d4b0c145ea9f5ad3b02190308831f`)  
**Goal level:** Zen-equivalent user-visible behavior, with Chromium-native internals where Gecko internals cannot be ported literally  
**Implementation bias:** Maximize Rust ownership; keep Chromium/C++ as thin execution and renderer-hook layers

---

## 0. Goal

Zen Browser의 Boost 기능을 Maho에서 가능한 한 완전히 동일한 수준으로 재현한다.

여기서 “동일”의 의미는 다음과 같다.

1. 사용자에게 보이는 주요 기능과 편집 흐름이 Zen과 동일해야 한다.
2. 저장 모델, 런타임 payload, 편집 state, 템플릿/모드, zap 규칙, color engine 파라미터는 Rust가 소유해야 한다.
3. Chromium/C++ 코드는 WebContents/renderer 훅, isolated-world 실행, Mojo/WebUI host, Blink/cc 패치 포인트만 담당해야 한다.
4. Gecko 내부를 그대로 포팅하는 것이 아니라, Chromium에서 같은 결과를 내는 것을 목표로 한다.

---

## 1. Grounded Current State

### 1.1 Zen side

Zen Boost는 두 층으로 구성되어 있다.

- **브라우저/privileged UI 계층**
  - editor window
  - zap UI / selector logic
  - persistence / import-export
  - actor 기반 content ↔ parent 통신
- **엔진/렌더링 계층**
  - `BrowsingContext` 필드 추가
  - `StyleAbsoluteColor::ToColor` 수준의 color filtering
  - smart invert
  - Oklab 기반 color transform

즉, Zen의 핵심 fidelity는 단순 CSS 주입이 아니라 렌더링 파이프라인에도 걸쳐 있다.

### 1.2 Maho side

Maho는 기반은 이미 있다.

- `maho-types/src/boost.rs`
  - `Boost`, `BoostUpdate`, `ColorOverride`, `FontOverride`
- `maho-core/src/boost_manager.rs`
  - CRUD
  - URL pattern matching
  - templates
  - CSS generation
  - global/js enable flags
- `maho-storage/src/sqlite.rs`
  - boosts persistence
  - css_mods persistence
- `maho-ffi/src/lib.rs`
  - boost read/query/template 관련 C ABI 일부 존재
- `maho-chromium/browser/net/maho_boost_injection_handler.cc`
  - CSS/JS injection 경로 존재
- `maho-chromium/browser/resources/maho_boost/store.ts`
  - richer editor/store exists

그러나 현재 가장 큰 문제는 **계약 drift**다.

### 1.3 Blocking mismatch

현재 세 surface가 서로 다르다.

1. `maho_boost.mojom`
   - `SiteRule` 중심의 오래된 domain-based surface
2. generated mojom TS typings
   - stale
3. `store.ts`
   - `BoostEntry` 중심의 richer API를 기대

이 상태에서는 Zen parity 기능을 계속 추가할수록 drift만 커진다.

---

## 2. Architecture Principle: Rust Owns the Product

### 2.1 Ownership rule

Rust가 소유해야 하는 것:

- Boost schema
- Boost update schema
- Boost editor state
- Boost runtime payload
- template/mod registry
- zap selector state
- zap selector normalization/rebuild logic
- CSS synthesis
- JS synthesis metadata
- color engine parameters
- color algorithm math
- smart invert math
- persistence-facing model
- sync-facing model

C++가 소유해야 하는 것:

- WebContents/frame lifecycle
- Mojo bindings
- isolated world JS execution
- page/renderer targeting
- Blink/cc patch point integration
- native bubble/sidebar/page-info surfaces

WebUI가 소유해야 하는 것:

- editing UX only
- presentation state only

WebUI는 business logic의 소유자가 아니어야 한다.

### 2.2 No new crate by default

새 workspace crate는 만들지 않는다.

이유:

- 현재 `maho-types` / `maho-core` / `maho-storage` / `maho-ffi` 분리가 이미 자연스럽다.
- Boost logic은 별도 dependency graph를 요구하지 않는다.
- crate를 쪼개면 오히려 type movement와 ABI surface만 늘어난다.

권장 방식은 `maho-core` 안에서 Boost submodule을 확장하는 것이다.

---

## 3. Crate Ownership Map

## 3.1 `maho-types`

Boost 관련 계약 타입의 단일 원본.

### Keep here

- `Boost`
- `BoostUpdate`
- `ColorOverride`
- `FontOverride`
- `BoostId`
- `UrlPattern`
- `Color`

### Add here

- `ZapSelector`
- `BoostEditorState`
- `BoostColorEngineConfig`
- `BoostRuntimeFlags`
- `CompiledBoostRuntime`
- `BoostTemplateView`
- 확장된 `BoostViewModel`

원칙: 브라우저, WebUI, Rust core가 공유하는 schema는 전부 여기 있다.

## 3.2 `maho-core`

Boost engine의 중심.

### Existing file to expand

- `src/boost_manager.rs`

### Recommended internal submodules

- `boost_manager.rs` — orchestration, top-level API
- `boost_css.rs` — final CSS synthesis
- `boost_zap.rs` — zap selector parse/rebuild/normalization
- `boost_color.rs` — Oklab / smart invert / parameter math
- `boost_templates.rs` — built-in templates and catalog
- `boost_runtime.rs` — persisted Boost → runtime payload compile

### Why here

- manager-per-concern is already the repo pattern
- product logic belongs in `maho-core`
- FFI and Chromium should delegate here immediately

## 3.3 `maho-storage`

Persistence only.

### Keep here

- boosts table
- css_mods table

### Extend here

- additional boost columns if needed:
  - `zap_selectors_json`
  - `color_engine_config_json`
  - `runtime_flags_json`
  - `created_at`
  - `updated_at`
- global boost settings persistence

Rule: no business logic, no compile logic.

## 3.4 `maho-ffi`

Thin ABI shims only.

### Add here

- boost CRUD/query functions that mirror typed `maho-core` methods
- runtime payload query functions
- zap add/remove helpers
- compiled CSS/JS query helpers

Rule: no `unwrap`, no domain logic, no string-based ad hoc orchestration beyond JSON boundary conversion.

## 3.5 `maho-chromium`

Thin execution shell.

### Keep here

- Mojo/WebUI host
- WebContentsObserver
- frame targeting
- isolated world script execution
- native bubble/sidebar/page-info entry
- renderer hook points

### Remove from here where possible

- zap parsing/rebuild logic
- boost-specific string munging
- template logic
- CSS compilation logic
- business state transitions

---

## 4. Data Model Refactor Required for Zen Parity

Current `Boost` is too thin for a Rust-first Zen-equivalent system.

## 4.1 Target persisted model

```rust
pub struct Boost {
    pub id: BoostId,
    pub url_pattern: UrlPattern,
    pub custom_css: Option<String>,
    pub custom_js: Option<String>,
    pub color_theme: Option<ColorOverride>,
    pub font_override: Option<FontOverride>,
    pub enabled: bool,
    pub space_scope: Option<SpaceId>,

    pub zap_selectors: Vec<ZapSelector>,
    pub editor_state: Option<BoostEditorState>,
    pub color_engine_config: Option<BoostColorEngineConfig>,
    pub runtime_flags: Option<BoostRuntimeFlags>,
}
```

핵심은 `custom_css`가 더 이상 모든 상태를 억지로 품는 저장 포맷이 되면 안 된다는 점이다.

## 4.2 Separate source model and runtime model

권장 분리:

- `Boost` = persisted/source-of-truth model
- `CompiledBoostRuntime` = injection/render payload

```rust
pub struct CompiledBoostRuntime {
    pub boost_id: BoostId,
    pub compiled_css: String,
    pub compiled_js: Option<String>,
    pub zap_selectors: Vec<String>,
    pub color_engine: Option<BoostColorEngineRuntime>,
}
```

이렇게 하면:

- 저장 모델을 바꿔도 injection 경로가 안정적이고
- deep Chromium color hook에도 같은 runtime payload를 재사용할 수 있다.

---

## 5. Blocking Phase 0: Contract Reconciliation

이 단계가 끝나기 전에는 parity 기능 개발을 시작하면 안 된다.

### Goals

1. `maho_boost.mojom`
2. generated TS bindings
3. `store.ts`
4. page handler
5. FFI query/update surface
6. Rust view/runtime models

이 여섯 개를 하나의 canonical BoostEntry/BoostTemplate 중심 계약으로 통합한다.

### Required decisions

- old `SiteRule` 중심 mojom surface 폐기
- `BoostEntry` 중심의 ID-based contract 채택
- `BoostTemplate`를 first-class mojom type으로 승격
- `getBoosts`, `getBoostsForUrl`, `createBoostForPattern`, `createBoostFromTemplate`, `updateBoostPattern`, `updateBoostCss`, `updateBoostJs`, `updateBoostColorTheme`, `updateBoostFontOverride`, `toggleBoost`, `deleteBoost`, `enterZapMode`, `removeZapSelector`를 canonical surface로 확정

### Delete or retire

- stale React fork under `browser/resources/maho_boost/react/`
- legacy/orphaned Boost UI paths that are not canonical

### Rust-first rule for Phase 0

Mojom은 `maho-core` truth를 반영해야 한다. WebUI에 맞춰 Rust를 비틀지 않는다.

---

## 6. Phase 1: Promote BoostManager into a Real Boost Engine

### Goal

`boost_manager.rs`를 Zen parity의 핵심 engine으로 승격한다.

### Tasks

1. `Boost` persisted model 확장
2. `BoostUpdate` 확장
3. `BoostViewModel` 확장
4. `CompiledBoostRuntime` 추가
5. pure Rust compile path 도입:
   - `compile_boost_runtime(boost: &Boost) -> CompiledBoostRuntime`
6. templates/mods registry 정식화

### Rust work

- `boost_css.rs`
  - color/font/zap/custom_css 병합
- `boost_zap.rs`
  - zap selector 추가/삭제/정규화
  - selector serialization 정책
- `boost_runtime.rs`
  - runtime payload 생성

### Result

Chromium은 더 이상 boost의 내부 규칙을 이해할 필요가 없다.

---

## 7. Phase 2: Zap as a First-Class Rust Feature

### Current problem

현재 zap은 C++에서 CSS comment를 파싱/재조립하는 식으로 처리된다.

이건 Rust-first 구조와 정면으로 충돌한다.

### Goal

zap selector를 Boost의 first-class field로 승격한다.

### Tasks

1. `ZapSelector`를 `maho-types`에 추가
2. `Boost`에 `zap_selectors` 필드 추가
3. Rust에서:
   - add zap
   - remove zap
   - normalize selector
   - compile zap CSS
4. C++ page handler는 picker 결과만 Rust에 전달
5. live reinjection path 추가

### C++ stays thin

- picker overlay trigger
- selector capture
- page execution only

### Rust owns

- zap state
- zap persistence
- zap CSS generation
- zap selector policy

---

## 8. Phase 3: JS Injection Parity

### Goal

Zen parity + Maho advantage를 모두 확보한다.

### Tasks

1. Rust가 `compiled_js`를 runtime payload에 포함
2. FFI가 runtime payload를 C++에 노출
3. C++ injection handler가 isolated world에서 JS 실행
4. global JS enable persistence 추가
5. 필요 시 per-boost runtime safety flags 추가

### Rust owns

- JS source-of-truth
- enable/disable policy model
- runtime payload generation

### C++ owns

- execution timing
- frame targeting
- isolated world execution

---

## 9. Phase 4: Color Engine v1 — Rust Approximation Layer

### Goal

Zen과 최대한 동일한 사용자 경험을 빠르게 확보한다.

### Strategy

Rust가 color math와 parameters를 소유하고, 1차 적용은 CSS/filter approximation으로 한다.

### Tasks

1. `boost_color.rs`에서 구현
   - Oklab helpers
   - dual-accent parameter model
   - smart invert parameter model
   - brightness/saturation/contrast state
2. `compile_boost_runtime()`이 다음 생성
   - coarse filter CSS
   - targeted element override CSS
   - link/text/background override CSS
3. WebUI editor는 Zen과 같은 control surface 제공

### Why this phase exists

Zen의 렌더링 파이프라인 훅은 바로 Chromium에 1:1 이식할 수 없다.
하지만 algorithm ownership을 Rust로 먼저 고정하면, 이후 deeper hook으로 진화시켜도 제품 로직은 그대로 유지된다.

---

## 10. Phase 5: Color Engine v2 — Deep Chromium Patch with Rust Math

### Goal

Zen에 최대한 근접한 rendering fidelity 확보.

### Key principle

깊은 렌더링 훅이 필요하더라도 algorithm ownership은 Rust에 둔다.

### C++/Chromium responsibilities

- Blink/renderer or compositor patch point 확보
- current page/frame boost runtime state 노출
- color resolution/paint path에서 hook 수행
- Rust algorithm 호출 결과를 적용

### Rust responsibilities

- Oklab transform
- complementary accent logic
- smart invert math
- parameter normalization
- runtime config serialization

### Important note

이 단계는 가장 위험하고 가장 비싸다. 하지만 “Zen처럼 완전히 똑같이”를 목표로 한다면 결국 필요하다.

---

## 11. Phase 6: Editor / Native Surface Unification

### Goal

Zen처럼 coherent한 하나의 product surface 완성.

### Tasks

1. page-info / site-control / bubble / sidebar / manager flow 통합
2. explicit target WebContents plumbing 도입
3. `?url=` 같은 임시 targeting 제거
4. live preview / live reapply 경로 정식화
5. editor state persistence
6. template/mod picker 통합

### Result

사용자는 여러 개의 따로 노는 Boost UI가 아니라 하나의 Zen-grade Boost product를 보게 된다.

---

## 12. Required FFI Expansion

`maho-ffi`는 Boost에 대해 richer typed ABI를 제공해야 한다.

### Minimum additions

- `maho_core_get_boosts`
- `maho_core_get_boosts_for_url`
- `maho_core_get_boost_templates`
- `maho_core_create_boost_for_pattern`
- `maho_core_create_boost_from_template`
- `maho_core_update_boost_pattern`
- `maho_core_update_boost_css`
- `maho_core_update_boost_js`
- `maho_core_update_boost_color_theme`
- `maho_core_update_boost_font_override`
- `maho_core_toggle_boost`
- `maho_core_delete_boost`
- `maho_core_add_zap_selector`
- `maho_core_remove_zap_selector`
- `maho_core_get_boost_runtime_payload`

원칙은 동일하다.

- FFI는 thin shim only
- business logic 없음
- JSON/C boundary만 처리

---

## 13. Storage Changes

### Existing foundation

`maho-storage/src/sqlite.rs`는 이미 boosts와 css_mods persistence를 갖고 있다.

### Recommended additions

- `zap_selectors_json`
- `editor_state_json`
- `color_engine_config_json`
- `runtime_flags_json`
- global boost settings storage
- timestamps where useful

복잡한 nested type은 기존 `color_theme` / `font_override`처럼 JSON blob로 저장하는 방식을 우선 사용한다.

---

## 14. Testing and Verification Strategy

## 14.1 Rust tests

Add coverage in `maho-core` for:

- URL pattern matching
- template application
- zap selector add/remove/normalize
- CSS synthesis snapshots
- JS payload generation
- color math/Oklab conversion
- smart invert math
- compiled runtime generation

## 14.2 Storage tests

Add coverage in `maho-storage` for:

- extended boost row round-trip
- zap/editor_state/config persistence

## 14.3 Chromium tests

Add or extend:

- injection handler tests
- bubble lifecycle tests
- sidebar/library interaction tests
- zap picker integration tests
- live reinjection after save
- deep color hook tests once Phase 5 lands

---

## 15. Risks

### Risk 1: Contract drift returns

If mojom, generated TS, WebUI store, Rust models drift again, parity work stalls.

**Mitigation:** Phase 0 is mandatory and must define one canonical contract.

### Risk 2: Color engine turns into a Chromium fork project

Phase 5 can become expensive quickly.

**Mitigation:** keep algorithm in Rust first; do Phase 4 approximation before engine patches.

### Risk 3: UI forks persist

Multiple Boost UIs will destroy product coherence.

**Mitigation:** choose one canonical WebUI/editor path and retire the others.

### Risk 4: C++ re-absorbs product logic

Easy short-term fixes in page handler/injection code will recreate technical debt.

**Mitigation:** any new non-lifecycle Boost logic should be rejected from C++ unless impossible in Rust.

---

## 16. Non-Goals

These are not the first target even if Zen has related adjacent behavior.

- unrelated shield/privacy panel work
- browser-wide non-Boost theming refactors
- new crate explosion for Boost internals
- speculative cross-product boost sharing before core parity lands

---

## 17. Final Recommendation

이 작업은 “Boost 기능 추가”가 아니라 **Boost 아키텍처를 Rust-first 제품 엔진으로 재정의하는 작업**으로 다뤄야 한다.

핵심 순서는 바뀌면 안 된다.

1. **Phase 0:** 계약 통합
2. **Phase 1:** Rust Boost engine 정식화
3. **Phase 2:** zap을 Rust first-class feature로 승격
4. **Phase 3:** JS injection 완성
5. **Phase 4:** Rust-owned approximate color engine
6. **Phase 5:** deep Chromium hook + Rust math
7. **Phase 6:** native/editor/product surface 통합

이 순서를 지키면 Zen과 같은 수준의 사용자 경험을 향해 가면서도, Maho의 코드베이스는 Rust를 중심으로 유지할 수 있다.
