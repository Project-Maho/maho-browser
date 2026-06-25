# LMDB Immediate Save — 탭/스페이스 변경시 즉시 저장

**작성일:** 2026-06-07
**대상 컴포넌트:** maho/crates/maho-core, maho-chromium/browser
**관련 코드:** `maho_core.rs:1063` (save_state), `maho_core.rs:2246` (handle_event), `maho_browser_main_extra_parts.cc:38,244`

---

## 1. 배경 (Why)

### 현재 동작 (직접 코드 검증 완료)

Maho Browser의 영속화 레이어는 두 backend로 나뉘어 있다:

**SQLite (즉시 저장 — write-through):**
- history, bookmarks, settings, notes, boosts, permissions, passwords, downloads, reading list, ATC rules, memory
- 각 mutation 직후 `storage.X_persisted()` → SQLite write가 동기적으로 발생
- 강제 종료/크래시에 안전

**LMDB (종료시 일괄 저장 — write-on-exit):**
- tabs, spaces, active_space_id, profiles, active_profile_id
- `save_state()` 호출은 코드베이스 전체에서 단 2곳뿐:
  - `maho_browser_main_extra_parts.cc:38` (소멸자)
  - `maho_browser_main_extra_parts.cc:244` (PostMainMessageLoopRun)
- Rust 측 추가: `maho_core.rs:2294` (`AppWillTerminate` 이벤트), `maho_core.rs:3582` (`update_profile_persisted` — 유일한 즉시저장 예외)
- **탭 생성/닫기/이동, 스페이스 CRUD 모두 in-memory만 변경. LMDB 즉시 저장 없음.**

### 문제 시나리오

| 종료 유형 | LMDB 데이터 결과 |
|---|---|
| Cmd+Q, 정상 종료 | 저장됨 ✅ |
| `kill -9 <pid>` | **유실** ❌ |
| 앱 크래시 | **유실** ❌ |
| OS 강제 재부팅 / 전원 차단 | **유실** ❌ |

### 미구현 인프라

`maho/crates/maho-core/src/crash_recovery.rs`에 `auto_save_interval_secs: 60`, `mark_dirty()`, `needs_save()` API가 정의되어 있으나, **이를 실행하는 timer loop가 코드베이스 어디에도 존재하지 않는다.** 인터페이스만 있고 구현체 미연결 상태.

---

## 2. 목표 (What)

### Must

- M1. `tabs`, `spaces`, `active_space_id`, `profiles`, `active_profile_id` 5종 데이터를 변경 발생 즉시 LMDB에 flush.
- M2. `kill -9` 후 재실행시 마지막으로 본 탭/스페이스 상태가 정확히 복구되어야 함.
- M3. 기존 종료시 save 경로(소멸자, PostMainMessageLoopRun, AppWillTerminate)는 백업으로 유지.
- M4. SQLite 즉시 저장 동작은 일체 변경하지 않음 (현재 정상이므로 손대지 않음).
- M5. 100탭 환경에서 단일 mutation 후 LMDB write wall time < 5ms.

### Must Not

- MN1. C++ 레이어(maho-chromium/browser/) 코드 변경 금지. Rust 측에서만 해결.
- MN2. LMDB 스키마 변경 금지 (forward/backward compat 유지).
- MN3. SQLite 관련 코드 어떤 것도 건드리지 않음.
- MN4. 에러 무음 처리 금지. 저장 실패는 반드시 stderr 로깅.
- MN5. Drop trait에서 save_state 호출 추가 금지 (소멸 순서 문제 우려).

### Out of Scope (이번 plan에서 다루지 않음)

- save_state 자체의 atomic transaction 처리 (현재 4번 put이 따로 호출됨 — 별도 plan 필요)
- crash_recovery.rs의 dirty flag + timer 시스템 완성 (Option 2 — 이번엔 Option 1만)
- iOS/Android shell의 동작 (해당 shell들도 `AppWillTerminate`만 의존하지만 별도 검토 필요)
- 사용자가 보고한 "재시작마다 데이터 초기화" 증상 자체의 원인 (이건 별개 — Layer 1~4 무음실패 가능성. 이번 plan은 "강제종료 안전성"만 다룸)

---

## 3. 설계 결정

### 3.1 진입점 분석

`maho_core.rs:2246`의 `handle_event(&mut self, event: ShellEvent) -> Vec<CoreUpdate>`이 거의 모든 mutation의 단일 진입점. ShellEvent enum에 정의된 변형들이 LMDB 영향 mutation을 트리거함.

다만 일부 직접 메서드는 `handle_event`를 거치지 않음:
- `MahoCore::create_space` (3323)
- `MahoCore::delete_space` (3348)
- `MahoCore::reorder_space` (3388)
- `MahoCore::favorite_tab` (3884)
- `MahoCore::update_profile_persisted` (3570) — **이미 `save_state()` 호출함**

이들은 테스트 및 일부 FFI 경로에서 직접 호출되므로 별도 보완 필요.

### 3.2 채택안: Decorator wrapper + 직접메서드 보완

**Approach:** `handle_event`를 `handle_event_inner`로 rename하고, 새 public `handle_event` wrapper에서 LMDB-dirty 이벤트 검출 후 `persist_lmdb_now()` 호출.

```rust
pub fn handle_event(&mut self, event: ShellEvent) -> Vec<CoreUpdate> {
    let dirties_lmdb = Self::event_dirties_lmdb(&event);
    let result = self.handle_event_inner(event);
    if dirties_lmdb {
        self.persist_lmdb_now();
    }
    result
}
```

**거부된 대안:**

| 대안 | 거부 이유 |
|---|---|
| 각 ShellEvent arm 내부에 save_state 호출 산재 | 변경 지점 30+개. 누락 위험 + 가독성 저하 |
| TabLifecycleManager / SpaceManager에 storage 주입 | 매니저 추상화 깨짐. 기존 layered 설계 위반 |
| Drop trait에서 save_state | drop 순서 보장 안 됨. 소멸 도중 LMDB 핸들이 살아있을지 보장 못함 |
| `crash_recovery.rs` timer loop 활성화 (Option 2) | 사용자가 명시적으로 Option 1 (즉시 저장) 선택. 이번 plan 범위 밖 |

### 3.3 `event_dirties_lmdb` whitelist

**원칙:** `tabs`, `spaces`, `active_space_id`, `profiles`, `active_profile_id` 중 하나라도 건드리는 ShellEvent 변형은 모두 포함.

**1차 후보 (`ShellEvent` enum 검토 후 확정 예정):**

```rust
fn event_dirties_lmdb(event: &ShellEvent) -> bool {
    matches!(event,
        // === Tab CRUD ===
        ShellEvent::CreateTab { .. }
        | ShellEvent::CloseTab { .. }
        | ShellEvent::ReopenLastClosed
        | ShellEvent::RestoreTab { .. }
        | ShellEvent::MoveTab { .. }
        | ShellEvent::MoveTabToSpace { .. }
        | ShellEvent::DuplicateTab { .. }
        // === Tab attributes ===
        | ShellEvent::PinTab { .. } | ShellEvent::UnpinTab { .. }
        | ShellEvent::FavoriteTab { .. } | ShellEvent::UnfavoriteTab { .. }
        | ShellEvent::ReorderFavorite { .. }
        | ShellEvent::FreezeTab { .. } | ShellEvent::UnfreezeTab { .. }
        | ShellEvent::ArchiveTab { .. } | ShellEvent::UnarchiveTab { .. }
        | ShellEvent::SuspendTab { .. } | ShellEvent::ResumeTab { .. }
        | ShellEvent::Navigate { .. }  // URL 변경도 Tab 직렬화 영향
        | ShellEvent::ActivateTab { .. }
        // === Tab folders & ordering ===
        | ShellEvent::ReorderTab { .. }
        | ShellEvent::MoveTabToFolder { .. }
        | ShellEvent::RemoveTabFromFolder { .. }
        | ShellEvent::ReorderTabInFolder { .. }
        | ShellEvent::MoveFolderIntoFolder { .. }
        // === Space CRUD ===
        | ShellEvent::CreateSpace { .. }
        | ShellEvent::DeleteSpace { .. }
        | ShellEvent::RenameSpace { .. }
        | ShellEvent::RecolorSpace { .. }
        | ShellEvent::ReorderSpace { .. }
        | ShellEvent::SetActiveSpace { .. }
        | ShellEvent::UpdateSpaceConfig { .. }
        | ShellEvent::ExportSpaceIntoFolder { .. }
        // === Profiles ===
        | ShellEvent::CreateProfile { .. }
        | ShellEvent::DeleteProfile { .. }
        | ShellEvent::UpdateProfile { .. }
        | ShellEvent::SwitchProfile { .. }
    )
}
```

**검증 방법 (Phase 1.2):** `maho-types/src/shell_event.rs`의 ShellEvent enum 전체를 dump하고 변형마다 `tab_manager` / `space_manager` / `profile_manager` mutation 여부 확인. exhaustive `match` (or wildcard에 디버그 assert)으로 컴파일 타임 누락 방지.

### 3.4 `persist_lmdb_now()` 헬퍼

```rust
fn persist_lmdb_now(&self) {
    if self.lmdb_storage.is_none() {
        return;  // in-memory / test mode: silent skip은 OK (영속화 의도 없음)
    }
    if let Err(e) = self.save_state() {
        eprintln!("[Maho] persist_lmdb_now failed: {}", e);
    }
}
```

**핵심 결정:**
- `lmdb_storage.is_none()` 케이스에서는 무음 skip — 테스트와 in-memory 모드 둘 다 영속화 의도가 없음. 에러 아님.
- 실제 `save_state()` 실패는 stderr로 로깅. 향후 telemetry로 격상 가능하도록 마킹.
- 동기 호출 — UI thread 블로킹 가능성 인지하고 진행 (Phase 4에서 측정 후 결정).

### 3.5 직접 메서드 보완 (handle_event 우회 경로)

다음 메서드들에 `self.persist_lmdb_now()` 호출 1줄 추가:

| 메서드 | 파일:라인 | 추가 위치 |
|---|---|---|
| `MahoCore::create_space` | maho_core.rs:3323 | return 직전 |
| `MahoCore::delete_space` | maho_core.rs:3348 | return 직전 |
| `MahoCore::reorder_space` | maho_core.rs:3388 | 함수 끝 |
| `MahoCore::favorite_tab` | maho_core.rs:3884 | return true/false 직전 |
| `MahoCore::update_profile_persisted` | maho_core.rs:3582 | **이미 호출함 — 통일성을 위해 `save_state()` → `persist_lmdb_now()`로 교체** |

다른 직접 메서드는 grep으로 전수 조사 (Phase 1.3).

### 3.6 종료 경로 처리

**유지 (백업 안전망 역할):**
- `maho_browser_main_extra_parts.cc:38` (소멸자) — 변경 없음
- `maho_browser_main_extra_parts.cc:244` (PostMainMessageLoopRun) — 변경 없음
- `maho_core.rs:2294` (`AppWillTerminate` arm의 `let _ = self.save_state()`) — 변경 없음

**근거:** 새 즉시저장이 어떤 이유로든 실패해도(권한, 디스크풀, 일시적 LMDB lock 등), 종료시 한 번 더 시도하면 복구 가능. 비용 없음.

**코멘트 추가:** `AppWillTerminate` arm 위에 docstring 추가 — "Phase 2 immediate save 후 마지막 보험. 정상 케이스에선 이미 모든 mutation이 LMDB에 반영돼 있어야 한다."

---

## 4. 단계별 작업 (How)

### Phase 1 — 사전 조사 & 화이트리스트 확정 (예상 1h)

**1.1 ShellEvent enum 전체 인벤토리**
- `read maho/crates/maho-types/src/shell_event.rs`
- 모든 변형 나열 (예상 60~80개)
- 각 변형이 LMDB-dirty인지 (tabs/spaces/profiles 영향) 표로 정리

**1.2 `event_dirties_lmdb` whitelist 확정**
- 1.1 결과 기반으로 §3.3 whitelist 보강
- 누락 검증: `match event { ... whitelist 변형 ... _ => false }` 형태로 작성. wildcard 의존하므로 새 변형 추가시 누락 가능 → exhaustive로 변경 검토.

**1.3 handle_event 우회 mutation 전수 조사**
```bash
ast-grep --lang rust --pattern 'self.tab_manager.$METHOD($$$)' maho/crates/maho-core/src/maho_core.rs
ast-grep --lang rust --pattern 'self.space_manager.$METHOD($$$)' maho/crates/maho-core/src/maho_core.rs
ast-grep --lang rust --pattern 'self.profile_manager.$METHOD($$$)' maho/crates/maho-core/src/maho_core.rs
```
호출자가 `handle_event_inner` 내부면 OK. 외부면 `persist_lmdb_now` 추가 필요.

**1.4 산출물:** `event_dirties_lmdb` 최종 코드 + 보완 필요 메서드 목록.

---

### Phase 2 — 코드 구현 (예상 1.5h)

**2.1 `persist_lmdb_now` helper 추가**
- 위치: `maho_core.rs`, `save_state` 함수 직후 (line 1108 근처)
- 내용: §3.4 코드

**2.2 handle_event 분리**
- 기존 `pub fn handle_event` → `fn handle_event_inner` rename (`pub` 제거, 모듈 내부 접근만)
- 새 `pub fn handle_event` wrapper 추가 (§3.2 코드)
- `event_dirties_lmdb` 함수 추가 (§3.3 코드)

**2.3 직접 메서드 보완**
- Phase 1.3에서 식별된 메서드들에 `self.persist_lmdb_now()` 추가
- `update_profile_persisted`의 기존 `let _ = self.save_state()`을 `self.persist_lmdb_now()`로 교체 (통일성 + 에러 로깅 일관성)

**2.4 docstring 추가**
- `handle_event` wrapper에 한국어/영어 docstring으로 의도 명시
- `AppWillTerminate` arm 위에 "백업 안전망" 코멘트
- `crash_recovery.rs`의 미연결 인프라에 "이 모듈은 현재 미연결. timer loop 추가시 활성화 가능" 코멘트

**2.5 산출물 — 변경 파일:**
- `maho/crates/maho-core/src/maho_core.rs` (수정, +60~80줄)

---

### Phase 3 — 테스트 (예상 1.5h)

**3.1 단위 테스트 신규 작성**
파일: `maho/crates/maho-core/tests/persistence.rs` (신규)

테스트 케이스:
```rust
#[test]
fn lmdb_persists_tab_create_immediately() {
    let temp = tempdir().unwrap();
    let path = temp.path();

    {
        let mut core = MahoCore::new()
            .with_storage(path.join("maho.db").to_str().unwrap())
            .with_lmdb_storage(&path.join("state"));
        core.handle_event(ShellEvent::CreateTab { /* ... */ });
        // 명시적 save_state 호출 없이 drop
    }

    // 새 인스턴스로 로드
    let mut core2 = MahoCore::new()
        .with_storage(path.join("maho.db").to_str().unwrap())
        .with_lmdb_storage(&path.join("state"));
    core2.load_state().unwrap();
    assert_eq!(core2.tab_manager.get_all_tabs().len(), 1);
}

#[test]
fn lmdb_persists_close_tab_immediately() { /* ... */ }

#[test]
fn lmdb_persists_space_create_immediately() { /* ... */ }

#[test]
fn lmdb_persists_active_space_change_immediately() { /* ... */ }

#[test]
fn lmdb_persists_move_tab_immediately() { /* ... */ }

#[test]
fn no_lmdb_io_when_storage_absent() {
    // lmdb_storage가 None일 때 mutation이 panic/error 없이 동작하는지
    let mut core = MahoCore::new();  // no with_lmdb_storage
    core.handle_event(ShellEvent::CreateTab { /* ... */ });
    // assert no panic, no stderr error
}

#[test]
fn lmdb_skips_save_for_non_dirty_event() {
    // SearchHistory 같은 read-only 이벤트는 LMDB write 트리거하지 않음
    // (timestamp 비교 또는 mock 방식으로 검증)
}
```

**3.2 회귀 테스트**
- `cd maho && bun run test` 통과 (workspace 전체)
- `cd maho && bun run typecheck` 통과

**3.3 통합 테스트 (수동, Manual QA)**
1. `python3 maho-chromium/build/scripts/build_maho.py` 빌드 (또는 기존 빌드 사용)
2. `~/code/project/maho-workspace/chromium/src/out/Default/Maho.app/Contents/MacOS/Maho 2>&1 | grep '\[Maho\]'` 실행
3. 탭 5개 신규 생성 → stderr에 `[Maho] save_state: persisting 5 tabs` 5번 출력되는지 확인
4. `kill -9 $(pgrep Maho)` 강제 종료
5. 재실행 → 탭 5개 복원 + stderr에 `[Maho] load_state: restoring 5 tabs` 출력
6. 스페이스 생성/삭제, 활성 스페이스 변경에 대해서도 동일 절차 반복

**3.4 산출물:**
- `maho/crates/maho-core/tests/persistence.rs` 신규
- 수동 QA 체크리스트 결과

---

### Phase 4 — 성능 측정 & 가드 (예상 1h)

**4.1 벤치마크 추가**
파일: `maho/crates/maho-bench/benches/persistence_bench.rs` (또는 기존 bench 파일에 추가)

```rust
#[bench]
fn bench_handle_event_create_tab_with_lmdb(b: &mut Bencher) {
    let temp = tempdir().unwrap();
    let mut core = setup_core_with_lmdb(temp.path());
    // 100탭 미리 생성
    for i in 0..100 { core.handle_event(...); }
    b.iter(|| {
        core.handle_event(ShellEvent::CreateTab { /* ... */ });
    });
}

#[bench]
fn bench_save_state_100_tabs(b: &mut Bencher) {
    /* 100탭 환경에서 save_state() 단독 비용 */
}
```

**4.2 합격 기준**
- 단일 mutation wall time (50탭 환경) < 5ms
- 단일 mutation wall time (200탭 환경) < 15ms

**4.3 초과시 대응 (조건부)**
- 5ms 초과시: `persist_lmdb_now`에 100ms debounce 추가 (단일 in-flight task로 batch)
- 이는 별도 follow-up plan으로 분리. 이번 plan에선 측정만 하고 결과 기록.

**4.4 산출물:**
- 벤치마크 코드
- 측정 결과 표 (PR 설명에 포함)

### Phase 4 측정 결과 (2026-06-07 실행)

| 벤치 | median | mean | 합격 기준 | 결과 |
|---|---|---|---|---|
| `lmdb_save_state_100_tabs` | **22.95ms** | 22.96ms | < 5ms | ❌ 초과 (4.6×) |
| `lmdb_handle_event_activate_tab_100_tabs` | **22.84ms** | 22.99ms | < 5ms | ❌ 초과 (4.6×) |

**진단:**
- `handle_event` 자체 비용은 무시할 수준 (두 벤치 시간이 거의 동일 → 비용 거의 전부가 `save_state` 에서 발생)
- 병목 추정: `save_state` 내부 4번의 `serde_json::to_vec` (tabs/spaces/profiles/active_id) — LMDB `put` 자체는 mmap이라 빠르지만 100탭 JSON 직렬화 비용이 누적됨
- 60Hz frame budget(16.67ms) 초과 → 100탭 환경에서 활성 탭 전환 시 1 프레임 hitch 발생 위험

**대응:** §4.3 사양대로 **별도 follow-up plan으로 분리**. 후보 접근:
- (a) Throttle/debounce: 50-100ms 합치기 (가장 단순)
- (b) Granular save: tabs/spaces/profiles 중 변경된 키만 직렬화
- (c) Async write: tokio task로 LMDB write를 background thread로 이전
- (d) Diff-based serialization: 변경된 탭만 직렬화 후 마지막 스냅샷에 merge

→ 후속 plan: `lmdb-immediate-save-perf-followup-2026-06-XX.md` (별도 작성 필요)

---

### Phase 5 — 문서화 & 마무리 (예상 30m)

**5.1 AGENTS.md 업데이트 검토**
- `maho/crates/maho-core/AGENTS.md` — write-on-mutation 패턴 명시. 새 mutation 추가시 `event_dirties_lmdb` whitelist 갱신 필요함을 코드 컨벤션에 추가.

**5.2 PR 설명 작성**
- Before / After 동작 비교
- 벤치마크 결과
- 변경 범위 요약
- 테스트 결과

---

## 5. 위험 & 완화

| ID | 위험 | 영향 | 가능성 | 완화 |
|---|---|---|---|---|
| R1 | 매 이벤트 sync LMDB write로 UI hitch | 사용자 체감 lag | 낮음 | LMDB는 mmap+append, 직접 측정으로 < 1ms 예상. Phase 4 결과 따라 debounce |
| R2 | `event_dirties_lmdb` whitelist 누락 | 일부 변경 미저장 | 중간 | exhaustive match 사용. 새 ShellEvent 추가시 컴파일러가 누락 감지 |
| R3 | handle_event 우회 mutation 누락 | 일부 변경 미저장 | 중간 | Phase 1.3 ast-grep 전수조사. 신규 메서드 추가시 코드리뷰에서 체크 |
| R4 | save_state 도중 panic → LMDB inconsistency | 부분 저장 | 매우 낮음 | save_state 자체가 atomic 아님(별도 plan). 본 plan 범위 외 |
| R5 | 테스트 환경에서 의도치 않은 LMDB write | CI 느려짐 / disk 사용 | 낮음 | `lmdb_storage.is_none()` 가드로 with_lmdb_storage 안 부른 테스트는 영향 없음 |
| R6 | 종료 시 새 immediate save가 IsTryingToQuit 가드와 충돌 | 종료 직전 빈 상태 저장 | 낮음 | tab_registry.cc의 IsTryingToQuit 가드는 close_tab dispatch 자체를 막음 → 우리 wrapper 도달 안 함. 안전 |
| R7 | LMDB write 빈도 폭증 (100탭 빠르게 닫기) | I/O 부하 | 낮음 | 측정 후 Phase 4에서 debounce 도입 결정. 기본 LMDB는 OS page cache로 흡수 |
| R8 | iOS/Android shell이 동일 코어를 쓰지만 동작 검증 안 함 | 모바일 회귀 | 중간 | shell의 AppWillTerminate 동작 별개로 검증 필요 — 본 plan에서 변경하지 않으므로 회귀 없음 |

---

## 6. 검증 (Done = ?)

다음이 모두 만족되어야 완료:

- [ ] D1. `cargo test --workspace` 통과 (기존 + 신규 persistence 테스트)
- [ ] D2. `cargo clippy --workspace -- -D warnings` 통과
- [ ] D3. `bun run typecheck` 통과
- [ ] D4. 신규 persistence.rs 테스트 6개 모두 통과
- [ ] D5. 수동 QA: kill -9 시나리오에서 탭 5개 복원 (Phase 3.3)
- [ ] D6. 수동 QA: 스페이스 생성/삭제/활성변경 시나리오 복원
- [ ] D7. 벤치마크 결과 < 5ms (50탭 mutation), 결과 기록
- [ ] D8. PR 설명에 Before/After + 벤치 결과 포함
- [ ] D9. C++ 코드 변경 0줄 (확인)
- [ ] D10. `event_dirties_lmdb` whitelist에 ShellEvent 모든 LMDB-dirty 변형 포함 (Phase 1 산출물 기반 review)

---

## 7. 변경 파일 요약

| 파일 | 종류 | 예상 라인 수 |
|---|---|---|
| `maho/crates/maho-core/src/maho_core.rs` | 수정 | +80줄 (wrapper, helper, whitelist), `~5` 곳 수정 |
| `maho/crates/maho-core/tests/persistence.rs` | 신규 | +120줄 (테스트 6개) |
| `maho/crates/maho-bench/benches/persistence_bench.rs` | 신규 또는 수정 | +40줄 (벤치 2개) |
| `maho/crates/maho-core/AGENTS.md` | 수정 | +5줄 (컨벤션 명시) |
| **합계** | | **~245줄, 신규 2 파일** |

C++ 변경 0. SQLite 코드 변경 0. LMDB 스키마 변경 0.

---

## 8. 후속 작업 (이번 plan 후 별도)

- F1. **save_state atomic transaction 처리** — 현재 4개 LMDB put이 따로 호출됨. 부분 저장 시 inconsistency 가능. LMDB `RwTxn`으로 묶기.
- F2. **crash_recovery.rs 활성화** — Option 2와 결합 가능. 60초 주기 dirty flush로 추가 안전망.
- F3. **iOS/Android shell의 AppWillTerminate 동작 검증** — 모바일에서도 즉시 저장이 동작하는지 별도 확인.
- F4. **save_state 비동기화 검토** — Phase 4 결과로 hitch 발생시 background thread 이전.
- F5. **사용자 보고 "재시작마다 데이터 초기화" 별개 진단** — Layer 1~4 무음실패 시나리오 재검증. 본 plan과 별도 root cause 가능성.
- F6. **direct API 메서드들 handle_event로 통일** — 장기적으로 mutation 진입점 단일화.

---

## 9. 의존성 / 차단 요인

- 의존: 없음. 외부 라이브러리/API 변경 없음.
- 차단: 없음. 즉시 진행 가능.
- 환경: macOS 빌드 환경 + Maho.app 빌드 (Phase 3.3 수동 QA용).
