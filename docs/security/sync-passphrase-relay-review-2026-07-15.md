# Maho 동기화 보안 분석 보고서 — 패스프레이즈 & 릴레이 서버

**작성일:** 2026-07-15
**대상:** `maho/crates/maho-core`(sync_crypto, sync_manager, maho_core), `maho/relay`(Cloudflare Worker), `maho-chromium/browser`(sync relay client, WebUI 핸들러)
**범위:** (1) 크로스 디바이스 동기화용 패스프레이즈/E2EE, (2) 릴레이 서버 보안

---

## 0. 요약 (Executive Summary)

Maho 동기화는 **256비트 시드 → BIP39 24단어 복구 문구 → Argon2id → AES-256-GCM** 방식의
클라이언트 측 E2EE로 설계되어 있고, 릴레이 서버(Cloudflare Worker + Durable Object + D1)는
암호화된 페이로드를 중계·저장만 하도록 되어 있습니다. 암호 프리미티브 선택 자체는 견고합니다.

그러나 **설계 의도와 실제 배선(wiring) 사이에 치명적 결함**이 하나 있습니다. "새 동기화 시작"을
누른 **생성 측(origin) 디바이스는 암호화 키를 설정하지 않으며**, 또한 방(room) 식별자로 잘못된 값을
전달합니다. 그 결과 (a) 생성 측이 릴레이에 아예 접속하지 못하고, (b) 접속되더라도 **평문으로 push**하게
됩니다. E2EE의 핵심 보장이 생성 측에서 깨집니다.

| # | 항목 | 심각도 |
|---|---|---|
| F1 | "새 동기화 시작" 경로에서 암호화 키 미설정 + room_id 불일치 | **Critical** |
| F2 | 시드/암호화 키가 비휘발성 저장되지 않아 재시작 시 동기화 미복구 | High |
| R1 | 릴레이 rate limiter가 isolate-로컬 HashMap이라 사실상 우회 가능 | Medium |
| R2 | 서비스 토큰 비교가 비-상수시간(`!=`) — 타이밍 사이드채널 | Medium |
| F5 | 복구 문구 클립보드 노출 + "저장 확인" 검증 단계 없음 | Low |
| R3~R7 | 아래 상세 (URL 쿼리 키, 미사용 제한값, 방 소유권 등) | Low/Info |

---

## 1. 동기화용 패스프레이즈 (E2EE)

### 1.1 암호 설계 (양호)

`maho/crates/maho-core/src/sync_crypto.rs` 기준:

- **시드:** `OsRng`로 32바이트(256비트) 난수 생성 → 복구 문구는 BIP39 24단어.
- **키 유도:** Argon2id(m=19 MiB, t=2, p=1)로 시드에서 32바이트 AES 키 유도.
  고정 솔트 `b"maho-sync-v1-key"` 사용 — 동일 시드가 모든 디바이스에서 같은 키를 만들도록 결정적.
- **방 ID 유도:** 같은 시드 + 다른 솔트 `b"maho-sync-v1-room"`로 16바이트(32 hex) 방 ID 생성.
  방 ID는 서버에 공유해도 안전(시드 → 방 ID는 Argon2 단방향).
- **대칭 암호:** AES-256-GCM, 메시지마다 12바이트 난수 nonce. 형식 = `nonce(12) || ciphertext || tag(16)`.
- **테스트:** roundtrip, 변조/절단 탐지, 잘못된 키 거부, BIP39 24단어 검증 등 커버됨.

평가: 프리미티브(Argon2id, AES-256-GCM, 256비트 엔트로피, BIP39)와 파라미터는 업계 권고
수준을 만족합니다. 고정 솔트는 크로스 디바이스 결정성을 위해 불가피하며 시드가 256비트 난수이므로
사전공격 위험은 무의미합니다.

### 1.2 [F1 — Critical] "새 동기화 시작" 경로가 암호화 키를 설정하지 않고 room_id도 불일치

두 개의 진입 경로가 있는데, **가입(join) 경로는 올바르지만 생성(generate/continue) 경로는 깨져 있습니다.**

**올바른 경로 — `join_sync` (`maho_core.rs:8364`):**
복구 문구 → 시드 디코드 → `derive_encryption_key` → `derive_room_id`(32 hex) → `start_sync` →
`sm.set_encryption_key(...)`. 암호화 키가 정상 설정됩니다.

**깨진 경로 — 생성 측 UI:**

1. `domain_panes.tsx:2192` — "Continue" 클릭 시 `handler.startSync(pendingKeyInfo.syncKey)` 호출.
   여기서 `syncKey`는 **32바이트 시드의 hex 문자열(64자)** 입니다(`maho_core.rs:8348`, `generate_sync_key`가
   반환하는 `syncKey`).
2. `maho_settings_page_handler.cc:692` → `maho_core_start_sync(core, relay_url, sync_key)`.
3. `maho_core.rs:8106` `start_sync`는 전달받은 문자열을 **그대로 `sync_key`(=room_id)에 저장**할 뿐,
   `derive_encryption_key`도 `set_encryption_key`도 호출하지 않습니다.

결과:

- **방 불일치 & 접속 거부:** 생성 측 room_id = 64자 hex(시드). 그러나 릴레이 `room.rs:350`
  `is_valid_room_id`는 **정확히 32자**만 허용 → `ws_upgrade`가 `invalid_room_key`로 거부.
  설령 통과하더라도 가입 측은 `derive_room_id`로 얻은 **32자** 방에 접속하므로 서로 다른 방이 되어
  절대 만나지 못합니다.
- **평문 전송(E2EE 무력화):** 생성 측 `SyncManager.encryption_key == None`. 이 상태에서
  `push_entity`/`send_tab`/`pull_entities`(`sync_manager.rs:105,512,139`)는 `Encrypted` 대신
  **평문 `SyncMessage`를 릴레이로 전송**합니다. 릴레이는 이 blob을 D1에 최대 30일 저장합니다
  (`lib.rs:160` 보존 GC). 즉 생성 측 디바이스의 탭/북마크/노트 등이 평문으로 서버에 남습니다.

정정 방향: 생성 측 "Continue"도 가입과 동일하게 `configure_sync_encryption_for_recovery_phrase`
(`maho_core.rs:8409`, 이미 올바르게 구현되어 있으나 **어디에서도 호출되지 않음**)를 타거나,
`start_sync` 내부에서 시드로부터 키·room_id를 유도해 `set_encryption_key`를 반드시 호출하도록
해야 합니다. `startSync`에는 room_id(32자)를, 키 설정에는 시드를 넘기도록 인터페이스를 분리하는 것이
안전합니다.

### 1.3 [F2 — High] 시드/암호화 키 비휘발성 저장 부재 → 재시작 시 동기화 미복구

`EncryptionKey`는 `SyncManager`에 **메모리로만** 보관됩니다. 시드나 복구 문구를 (OS 키체인 등에)
저장하는 코드가 없고, 부팅 경로(`maho_browser_main_extra_parts.cc:1969`)의 `StartPolling()`는
`maho_core_get_sync_status`가 `idle`이면 그대로 접속을 끊습니다. 재시작하면 `sync_manager == None`
→ 상태 idle → **사용자가 복구 문구로 다시 join 하지 않는 한 동기화가 재개되지 않습니다.**
가용성/UX 결함이자, 사용자가 매번 복구 문구를 재입력하도록 유도해 문구 취급 위험을 키웁니다.

### 1.4 [F5 — Low] 복구 문구 취급

- `domain_panes.tsx:2242` — 복구 문구를 `navigator.clipboard.writeText`로 복사. 클립보드는 다른 앱/
  클립보드 매니저가 읽을 수 있어 24단어 시드가 노출될 수 있습니다.
- 화면에 문구를 표시(`description={keyInfo.recoveryPhrase}`)하지만 **"문구를 저장했는지 확인"하는
  재입력/검증 단계가 없습니다.** 사용자가 문구를 분실하면(F2와 결합 시) 복구 불가.

### 1.5 [Info] AES-GCM nonce 재사용 경계

메시지마다 난수 12바이트 nonce를 쓰므로 동일 키에서 약 2³² 메시지에 근접하면 이론적 nonce 충돌
경계에 도달합니다. 현실적 동기화 볼륨에서는 문제되지 않으나, 장기적으로 결정적 nonce(카운터+device_id)
전환을 고려할 수 있습니다.

---

## 2. 릴레이 서버 보안 (`maho/relay`)

### 2.1 아키텍처 (양호)

- Cloudflare Worker 라우터(`lib.rs`) + 방마다 Durable Object(`RoomDO`) + D1(`MAHO_DB`).
- 인증: 이메일/비밀번호. 비밀번호는 **HMAC-SHA256 pepper + Argon2id**(`auth/passwords.rs`),
  pepper는 `env.secret("ARGON2_PEPPER")`. 액세스/리프레시 토큰은 32바이트 난수, **SHA-256 해시로만 저장**
  (`auth/tokens.rs`), 세션 만료 검사(`db/mod.rs:323`).
- 방 접근 제어: 유효 room_id 형식 검사(32 hex), **토큰 기반 room 소유권 클레임**(`room.rs:273`,
  `claim_room_ownership` ON CONFLICT DO NOTHING → 최초 소유자 고정), 다른 사용자 소유 방 접속 거부.
- 남용 방지: 메시지 크기 1 MiB, 방 크기 50 MiB, 방당 연결 5개, 분당 100 메시지 rate limit
  (`room.rs:9-13`), 인증 타임아웃 5초.
- 로깅: `redact_key`로 room 키를 SHA-256 앞 4바이트로만 로그(`room.rs:353`).
- 유지보수: 스케줄러가 메시지/만료 세션 주기적 삭제(`lib.rs:160`, 기본 30일).
- 개발 토큰 fail-closed(`routes.rs:452`), 이메일 인증 우회 토큰은 **상수시간 비교**(`ct_eq`, `routes.rs:742`).
- 로그인 실패 10회 시 15분 계정 잠금(`routes.rs:168`).

### 2.2 [R1 — Medium] Rate limiter가 isolate-로컬이라 사실상 우회 가능

`routes.rs:22` `check_rate_limit`은 `static OnceLock<Mutex<HashMap>>` — **워커 isolate 프로세스
메모리**에 한정됩니다. Cloudflare Workers는 요청을 다수의 isolate/엣지 PoP에 분산하므로 이 카운터는
전역 상태가 아니며, 공격자는 여러 연결/지역으로 손쉽게 상한을 넘길 수 있습니다. signup/login 무차별
대입 방어가 설계 의도보다 약합니다(로그인은 계정 잠금이 부분 완화). 견고하게 하려면 D1/KV/Durable
Object 기반의 공유 카운터로 이전해야 합니다.

### 2.3 [R2 — Medium] 서비스 토큰 비교가 비-상수시간

`introspect`/`consume_bypass`/`admin_force_verify_email`/`admin_set_credit_balance`에서
`service_token != expected_service_token`(예: `routes.rs:390`)로 **일반 `!=` 비교**를 사용합니다.
bypass 토큰은 `ct_eq`를 쓰면서도 정작 이 관문의 `MAHO_RELAY_SERVICE_TOKEN` 비교는 타이밍
사이드채널에 노출됩니다. `subtle::ConstantTimeEq`로 통일해야 합니다.

### 2.4 [R3 — Low] Room 키가 URL 쿼리로 전달

`?key=<room_id>`(`room.rs:38`) 형태라 중간 프록시/액세스 로그에 남을 수 있습니다. 설계상 room_id는
시드에서 단방향 유도되어 암호화 키를 노출하지 않으므로 위험은 제한적이나, 헤더(`X-Room-Key`, 이미 대체
경로로 지원됨) 사용을 기본으로 권장합니다.

### 2.5 [R4 — Low] REQUIRE_AUTH 비활성화 시 개방 + 서버 측 blob 보존

`REQUIRE_AUTH`는 env로 끌 수 있고(`room.rs:260`, 기본 true), 꺼지면 room_id를 아는 누구나 방에
접속·구독할 수 있습니다. 릴레이는 blob을 D1에 30일 보존하므로, **기밀성은 전적으로 클라이언트 암호화에
의존**합니다 — F1로 인해 생성 측이 평문을 보내는 상황과 결합되면 서버에 평문이 남습니다. 배포 구성에서
`REQUIRE_AUTH=true` 강제와 F1 수정이 함께 필요합니다.

### 2.6 [R6 — Low] 정의됐지만 미집행되는 제한값

`config.rs`의 `max_connections_per_ip`, `max_rooms_per_ip`는 파싱되지만 코드 어디에서도 강제되지
않습니다(`room.rs`는 방당 연결 수만 검사). IP 단위 남용 제어가 사실상 비어 있습니다.

### 2.7 [R5 / R7 — Info]

- **R5:** 방 크기 상한 50 MiB(`room.rs:12`)와 스냅샷 업로드 상한 100 MiB(`sync/routes.rs:23`)가
  불일치하고, 스냅샷은 별도 `snapshots` 테이블로 방 바이트 캡을 우회합니다. 스토리지 회계 일관성 검토 권장.
- **R7:** 방 소유권은 "최초 토큰이 소유"(`claim_room_ownership`). room_id는 시드 유도라 예측이 어렵지만,
  소유자가 클레임하기 전에 room_id가 유출되면 선점 위험이 이론적으로 존재합니다.

---

## 3. 권고 우선순위

1. **F1 (Critical) 즉시 수정** — 생성 측 "Continue"가 암호화 키를 반드시 설정하고 32자 room_id로
   접속하도록 배선. 미사용 `configure_sync_encryption_for_recovery_phrase` 재사용 또는 `start_sync`
   내부 유도. E2EE 무력화 + 접속 불가를 동시에 해소.
2. **R4 + R2 (High/Medium)** — `REQUIRE_AUTH=true` 배포 강제, 서비스 토큰 상수시간 비교로 통일.
3. **F2 (High)** — 시드/키를 OS 보안 저장소에 보관하고 부팅 시 자동 재개.
4. **R1 (Medium)** — rate limiter를 공유 스토리지 기반으로 이전.
5. **F5 / R3 / R6 / R5 (Low)** — 복구 문구 저장 확인 단계, 헤더 기반 room 키, IP 제한 집행, 스토리지 캡 정합.

---

## 부록 — 주요 파일

| 영역 | 파일 |
|---|---|
| 암호 프리미티브 | `maho/crates/maho-core/src/sync_crypto.rs` |
| 동기화 상태기계 | `maho/crates/maho-core/src/sync_manager.rs` |
| 키 생성/가입/스냅샷 | `maho/crates/maho-core/src/maho_core.rs` (8104~8427, 8949~) |
| Chromium WS 클라이언트 | `maho-chromium/browser/sync/maho_sync_relay_client.cc` |
| 설정 UI 배선 | `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc`, `resources/maho_settings/react/domain_panes.tsx` |
| 릴레이 방/인증 | `maho/relay/src/room.rs`, `auth/{routes,passwords,tokens,middleware}.rs`, `config.rs`, `sync/routes.rs` |
