# 구현 계획서 — 동기화 E2EE 생성-측 결함(F1) 수정 패치

**작성일:** 2026-07-15
**상태:** 착수 준비 완료 (정적 근거 확정, 참조 구현/테스트 존재)
**우선순위:** Critical — E2EE 무력화 + 생성 측 접속 불가 동시 해소
**선행 보고서:** `docs/security/sync-passphrase-relay-review-2026-07-15.md`

---

## 0. 목표와 비목표

**목표 (이 PR):**
- 생성(origin) 측 "새 동기화 시작 → Continue" 경로가 **암호화 키를 반드시 설정**하고 **가입 측과 동일한
  32자 room_id로 접속**하도록 배선을 고친다.
- 회귀를 잠그는 단위 테스트(키 없이 push하면 평문이 나가는 현 동작 차단) 추가.
- 런타임 수용 기준으로 두 디바이스 왕복 E2EE 검증.

**비목표 (별도 후속):**
- F2(시드/키 비휘발성 저장 및 재시작 자동 재개) — 별도 설계 필요, 범위 분리.
- R1(공유 rate limiter), R4(REQUIRE_AUTH 배포 강제) — 릴레이 인프라/배포 이슈, 별도 트랙.
- R2(서비스 토큰 상수시간 비교)는 1줄 수정이라 **선택적으로 이 PR에 동봉 가능**(§6).

---

## 1. 근본 원인 (확정)

| 경로 | 파일:라인 | 동작 | 상태 |
|---|---|---|---|
| 가입 | `maho_core.rs:8364` `join_sync` | 문구→시드→키 유도→`start_sync(room_id)`→`set_encryption_key` | ✅ 정상 |
| 생성 | `domain_panes.tsx:2192` → `maho_settings_page_handler.cc:692` → `maho_core.rs:8106` `start_sync` | `syncKey`(64자 시드 hex)를 room_id로 저장만, 키 유도/설정 없음 | ❌ 결함 |

확정된 사실:
- `set_encryption_key`의 **프로덕션 호출처는 `join_sync` 단 하나**. 생성 경로엔 없음.
- 올바른 헬퍼 `configure_sync_encryption_for_recovery_phrase(server_url, phrase)`
  (`maho_core.rs:8409`)는 존재하나 **테스트에서만 호출**(`persistence.rs:514,526`) — 프로덕션 미배선.
- `room.rs:350` `is_valid_room_id`는 정확히 32 hex만 허용 → 64자 시드 hex는 서버가 거부.
- 부수 결함: `join_sync`는 `wss://sync.mahobrowser.com` 하드코딩, 설정은 `kSettingsSyncRelayUrl`
  (`https://sync.mahobrowser.com`) 사용 — URL 소스 불일치.

---

## 2. 설계 결정

**원칙: 생성 경로를 가입 경로와 동일한 "문구→키 유도" 프리미티브로 수렴시킨다.**
`start_sync(url, key)`의 계약("key = 이미 유도된 32자 room_id")은 그대로 두고, 생성 경로가 그 계약을
지키도록 상위에서 키를 유도하게 만든다. `start_sync`를 오버로드하지 않는다(가입 경로 파손 방지).

생성 측 UI는 `pendingKeyInfo.recoveryPhrase`를 이미 보유하므로, "Continue"가 **복구 문구를 넘겨
`configure_sync_encryption_for_recovery_phrase`를 타는 것**이 최소 위험 경로다. 이 헬퍼는
`persistence.rs`가 이미 왕복 검증하므로 신규 로직이 아니라 **기존 검증된 코드의 재배선**이다.

### 선택지 비교

| 안 | 내용 | 장점 | 단점 | 채택 |
|---|---|---|---|---|
| A | `start_sync`가 입력을 시드로 재해석해 내부 유도 | 프론트 변경 최소 | `start_sync` 계약 오염 → 가입 경로 파손 위험 | ✕ |
| **B** | 신규 FFI+mojom `ConfigureSyncEncryption(url, phrase)`, "Continue"가 문구로 호출 | 검증된 헬퍼 재사용, 계약 분리, `maho_sync`/`maho_settings` 공통 | 표면(FFI/mojom/ts) 3계층 변경 | **✔** |
| C | "Continue"가 그냥 `joinSync(recoveryPhrase)` 호출 | 코드 추가 0 | `join_sync`의 URL 하드코딩·의미상 "가입"이라 혼동, 서버 URL 주입 불가 | △(대안) |

→ **B 채택.** 시간이 촉박하면 C를 임시 스톱갭으로 쓰되 `join_sync` URL을 파라미터화해야 함.

---

## 3. 변경 사항 (파일별)

### 3.1 Rust core — `maho/crates/maho-core/src/maho_core.rs`
- `configure_sync_encryption_for_recovery_phrase`는 그대로 사용(변경 불필요).
- `join_sync`(8398)의 하드코딩 `wss://sync.mahobrowser.com`을 인자/상수로 정리해 설정 경로와 URL
  소스를 일치시킨다(부수 결함 해소). 최소한 두 경로가 같은 상수를 참조하도록.

### 3.2 FFI — `maho/crates/maho-ffi/src/lib.rs`
- 신규 `maho_core_configure_sync_encryption(ptr, server_url, recovery_phrase) -> *mut c_char`
  추가. 기존 `maho_core_join_sync`(6398) 패턴 그대로: null 체크 + `import_gate::is_active()` 가드
  + `CStr` 파싱 + 결과 JSON 반환.
- 헤더 재생성: `maho-chromium/third_party/maho/maho_ffi.h`에 심볼 반영(빌드 파이프라인 확인).

### 3.3 Mojom — 설정 & 동기화 WebUI
- `maho_settings/maho_settings.mojom`: `ConfigureSyncEncryption(string recovery_phrase) => (SyncStatus status)` 추가
  (또는 기존 `StartSync`를 문구 기반으로 교체하고 `syncKey` 파라미터 제거).
- `maho_sync/maho_sync.mojom`도 동일 처리(공통 core 경유이므로 동시 정리).

### 3.4 페이지 핸들러 (C++)
- `maho_settings_page_handler.cc`: `StartSyncOnCoreSequence`(689)를
  `maho_core_configure_sync_encryption(core, kSettingsSyncRelayUrl, recovery_phrase)` 호출로 교체.
  `kSettingsSyncRelayUrl`을 서버 URL로 넘기므로 URL 소스 일원화.
- `maho_sync_page_handler.cc`: `StartSyncOnPool`(171) 동일 교체.

### 3.5 프론트엔드 — `maho_settings/react/domain_panes.tsx`
- 2192 `handler.startSync(pendingKeyInfo.syncKey)` →
  `handler.configureSyncEncryption(pendingKeyInfo.recoveryPhrase)` (또는 교체된 `startSync(phrase)`).
- `.mojom-webui.js.d.ts` 타입 정의 갱신은 mojom 재생성 산출물.

### 3.6 정리
- 더 이상 쓰이지 않는 `syncKey`(시드 hex) 노출을 UI/모조 계약에서 축소. `generate_sync_key`가 반환하는
  `syncKey` 필드는 표시/전송 불필요 시 제거 검토(복구 문구·room_id만으로 충분).

---

## 4. 테스트 전략

### 4.1 단위 (Rust) — 회귀 잠금
`maho-core`에 추가:
1. **평문 유출 차단 회귀:** `SyncManager`에 키 미설정 상태로 `push_entity` 시 나가는 메시지가
   `Encrypted`가 아니면 실패하도록 명시(현 동작을 잠그는 것이 목적 — 수정 후엔 생성 경로가 이 상태에
   도달하지 못함을 상위에서 보장).
2. **생성 경로 왕복:** `persistence.rs:514` 패턴을 차용해, "생성 측이
   `configure_sync_encryption`을 거친 뒤 push하면 `Encrypted`만 나가고, 동일 문구를 가진 dest가
   복호화·적용 성공"을 검증.
3. **room_id 일치:** 같은 시드로 생성 측 room_id == 가입 측 `derive_room_id` == 32 hex 확인.

### 4.2 런타임 수용 기준 (계획서 산출물 아님 — 패치 검증 게이트)
- **수정 전 1회 재현(회귀 앵커):** 로컬 릴레이(`ws://127.0.0.1`) 상대로 생성 측이 (a) 접속 실패
  또는 (b) 평문 blob 전송함을 캡처.
- **수정 후:**
  - 디바이스 A(생성) + B(가입, A의 복구 문구)가 **같은 32자 방**에 접속.
  - A→릴레이로 나가는 모든 메시지 타입이 `Encrypted`.
  - A에서 만든 탭이 B에 나타나고, 릴레이 D1에 저장된 blob이 복호화 없이는 판독 불가.

### 4.3 기존 테스트 영향
- `persistence.rs`의 E2EE 왕복 테스트는 헬퍼를 직접 호출하므로 영향 없음(오히려 참조 구현).
- mojom 시그니처 변경 시 관련 WebUI 단위/E2E 테스트 갱신 필요.

---

## 5. 롤아웃 & 리스크

- **하위호환:** 기존에 (깨진) 생성 경로로 "동기화 중"이라 믿던 디바이스는 실제로 방에 없었으므로 잃을
  세션 상태가 없음 — 마이그레이션 불필요. 사용자는 문구로 재-Continue만 하면 정상화.
- **보안 게이트:** 배포 전 §4.2 수정-후 기준(모든 outbound = `Encrypted`)을 CI/수동으로 통과해야 함.
- **부수 위험:** mojom 재생성/FFI 헤더 동기화 누락 시 빌드 실패 → 로컬 풀빌드로 확인.
- **URL 일원화:** `join_sync` URL 변경이 가입 경로 실환경 접속에 영향 없는지 확인(상수 값 자체는 동일
  호스트).

---

## 6. 선택적 동봉 — R2 (서비스 토큰 상수시간 비교)

`maho/relay/src/auth/routes.rs`의 `introspect`/`consume_bypass`/`admin_*` 4개소
`service_token != expected_service_token`를 `subtle::ConstantTimeEq`(같은 파일 742행 `ct_eq`
선례)로 교체. 1줄 성격, 리스크 낮음. F1과 무관하므로 리뷰 부담 원치 않으면 분리.

---

## 7. 작업 순서 (체크리스트)

1. [ ] (사전) 로컬 릴레이 띄우고 **수정 전 재현** 캡처 — 회귀 앵커 확보
2. [ ] core: `join_sync` URL 소스 일원화, 회귀/왕복 단위 테스트 추가
3. [ ] ffi: `maho_core_configure_sync_encryption` 추가 + 헤더 재생성
4. [ ] mojom(settings/sync) 메서드 추가·교체 + 재생성
5. [ ] 페이지 핸들러 2곳 교체(`configure_sync_encryption` 호출, 서버 URL 주입)
6. [ ] 프론트 `domain_panes.tsx` "Continue" 배선 교체 + 타입 갱신
7. [ ] `syncKey`(시드 hex) 노출 축소 정리
8. [ ] 풀빌드 + 단위 테스트 + **§4.2 수정-후 런타임 검증**
9. [ ] (선택) R2 상수시간 비교 동봉

---

## 부록 — 참조 좌표

| 대상 | 위치 |
|---|---|
| 결함 진입점(프론트) | `maho-chromium/browser/resources/maho_settings/react/domain_panes.tsx:2192` |
| 결함 배선(C++) | `maho_settings_page_handler.cc:689`, `maho_sync_page_handler.cc:171` |
| 잘못된 core 진입 | `maho_core.rs:8106` `start_sync` |
| 올바른 참조 구현 | `maho_core.rs:8409` `configure_sync_encryption_for_recovery_phrase` |
| 참조 테스트 | `maho/crates/maho-core/tests/persistence.rs:500~535` |
| room_id 검증 규칙 | `maho/relay/src/room.rs:350` `is_valid_room_id` |
| FFI 패턴 선례 | `maho/crates/maho-ffi/src/lib.rs:6398` `maho_core_join_sync` |
