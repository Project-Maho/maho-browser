# 패스워드 매니저 보안 수정 구현 계획서

작성일: 2026-07-15
대상: Maho 크로미움 포크 (자체 매니저 · 1Password · Bitwarden 1급 시민)
근거 분석: 코드 감사(2026-07-15), 로컬 `maho.db` 평문 확인

---

## 0. 요약

패스워드 저장 스택은 기능적으로 동작하나, **암호화가 실제로 활성화되어 있지 않고** 열람·provider 선택 경로에 인가 공백이 있다. 아래 6개 결함을 심각도 순으로 3개 마일스톤에 나누어 수정한다.

| # | 결함 | 심각도 | 마일스톤 |
|---|---|---|---|
| F1 | 패스워드가 평문으로 디스크 저장 (SQLCipher 키 미배선) | 🔴 심각 | M1 |
| F2 | 마이그레이션 후 평문 백업(`*.db.plain_backup`) 영구 잔존 | 🔴 심각 | M1 |
| F3 | 패스워드 열람 시 OS/생체 재인증 없음 | 🟠 높음 | M2 |
| F4 | provider 탐지 비결정적(HashMap) + 권한 기반 오탐 | 🟠 높음 | M2 |
| F5 | FFI CRUD 인가 게이트 부재 | 🟡 중간 | M3 |
| F6 | provider 메타데이터 7계층 하드코딩(확장성) | 🟡 중간 | M3 |

핵심 원칙: **M1이 배포되기 전에는 자체 패스워드 매니저의 신규 저장 기능을 사용자에게 노출하지 않는다.**

---

## 마일스톤 1 — 저장 시 암호화 활성화 (F1, F2)

### F1. SQLCipher 키를 브라우저 코어에 배선

**현재 상태**
- `maho/crates/maho-storage/src/sqlite.rs:16` `set_sqlcipher_key` 존재하나 워크스페이스 전체에서 **호출자 0개**.
- `maho/crates/maho-ffi/src/lib.rs:2442` `maho_core_new_with_storage`가 키 없이 `SqliteStorage::open()` 호출 → 키 미설정 시 `KeyNotConfigured`.
- `maho_ffi.h`에 키 주입 export 없음.
- 이미 존재하는 프리미티브: `maho_core_derive_oscrypt_key`(`lib.rs:7258`)가 PBKDF2-HMAC-SHA1로 프로필 키 파일에서 16바이트 키 파생(`oscrypt.rs:10`). C++ 호출자는 아직 없음.

**순서 제약 (중요)**
`maho_core_new_with_storage`는 **생성 시점에 storage를 open** 한다. 따라서 키는 코어 생성 **이전에** 프로세스 전역으로 주입되어야 한다.

**변경 사항**

1. **Rust FFI 신규 export** — `maho/crates/maho-ffi/src/lib.rs`
   ```rust
   /// # Safety
   /// `key_hex` must be a valid null-terminated C string (hex-encoded key).
   /// Call once, before `maho_core_new_with_storage`.
   #[no_mangle]
   pub unsafe extern "C" fn maho_storage_set_sqlcipher_key(key_hex: *const c_char) -> bool {
       ffi_safe!({
           if key_hex.is_null() { return false; }
           let key = match CStr::from_ptr(key_hex).to_str() {
               Ok(s) => s, Err(_) => return false,
           };
           maho_storage::sqlite::set_sqlcipher_key(key).is_ok()
       }, false)
   }
   ```
   - 헤더 `maho-chromium/third_party/maho/maho_ffi.h`에 선언 추가(자동 생성 시 cbindgen 재실행).

2. **C++ 배선** — `maho-chromium/browser/maho_browser_main_extra_parts.cc` `InitializeRegularServicesOnce`(현재 1918~)
   - `base::CreateDirectory(storage_path_)` **직후**, `CreateWithStorage` **이전**에:
     1. 키 파일 경로 결정: `storage_path_.AppendASCII("maho_storage.key")`.
     2. `maho_core_oscrypt_key_path_is_secure`로 퍼미션(0600) 검증.
     3. `maho_core_derive_oscrypt_key(path, key16, &status)` → 16바이트 파생.
     4. hex 인코딩 후 `maho_storage_set_sqlcipher_key(hex)` 호출.
     5. 실패 시: **코어를 storage 없이 생성하지 않는다.** 대신 명시적 에러 로깅 + 패스워드 기능 비활성 플래그. (평문 폴백 금지)
   - `maho_bridge.cc`에 `SetStorageKey`/`DeriveAndSetStorageKey` 헬퍼 추가해 배선을 캡슐화.

3. **폴백 정책 변경** — `maho_browser_main_extra_parts.cc:1926`
   - 현재: `CreateWithStorage` 실패 시 `Create()`(storage 없음)로 폴백.
   - 변경: 키 파생 실패와 storage open 실패를 구분. 키가 없으면 조용한 평문 폴백 대신 **하드 실패로 취급**(패스워드 저장 비활성 상태 유지).

4. **플랫폼 키 소스 정합성**
   - 현 `derive_oscrypt_key`는 자체 키 파일(0600) 기반. 크로미움 OSCrypt(Keychain/DPAPI/libsecret)와는 별개다.
   - M1 범위: 자체 키 파일 방식 유지(이식성). 후속(별도 티켓): 플랫폼 keystore(OSCrypt::EncryptString로 키 파일 자체를 래핑)로 승격.

**검증**
- 단위: 키 주입 후 `open()` 성공, 미주입 시 `KeyNotConfigured` 반환(회귀 테스트).
- E2E: 실행 후 `sqlite3 <profile>/MahoCore/maho.db "SELECT * FROM sqlite_master"`가 **키 없이는 실패**해야 함(현재는 성공 = 평문). `verify` 스킬로 실제 앱 기동 후 확인.

---

### F2. 평문 백업 파일 즉시 삭제

**현재 상태** — `maho/crates/maho-storage/src/sqlite.rs`
- `migrate_plaintext_to_encrypted_inner`(106~)가 원본을 `*.db.plain_backup`으로 rename(139) 후 **삭제하지 않음**.
- 테스트(`sqlite.rs:2722`)가 `assert!(backup_path.exists())`로 잔존을 보장 → 이 단언이 결함을 고착.

**변경 사항**

1. `migrate_plaintext_to_encrypted_inner` 성공 경로(현 159 `Ok(())` 직전)에서:
   - 암호화 DB가 verify를 통과한 뒤 `backup_path`를 **베스트-에포트 secure-delete** 후 unlink.
   - 최소 구현: 파일 크기만큼 0 덮어쓰기 + `sync_all` + `remove_file`. (SSD 완전 소거는 불가하나, 일반 파일 시스템 노출은 제거.)
   - 실패는 마이그레이션을 실패시키지 않되 `tracing::warn!`으로 경고.

2. 롤백 경로(141~157)는 백업이 복원에 필요하므로 **그대로 유지**. 삭제는 최종 스왑+verify 성공 이후에만.

3. 테스트 수정 — `sqlite.rs:2708~2725`
   - `assert!(backup_path.exists())` → 마이그레이션 성공 후 `assert!(!backup_path.exists())`로 반전.
   - 롤백(verify 실패) 케이스에서는 원본이 복원되고 백업이 정리되는지 별도 검증.

4. 기존 잔존물 청소(마이그레이션 코드): 부팅 시 `*.db.plain_backup`가 존재하고 암호화 DB가 정상이면 시작 시 정리하는 1회성 스텝 추가.

**검증**
- 단위: 성공 시 백업 부재, 롤백 시 원본 무결.
- 수동: 평문 DB를 심은 뒤 최초 기동 → 마이그레이션 → `.plain_backup` 부재 확인.

---

## 마일스톤 2 — 열람 인가 & provider 신뢰성 (F3, F4)

### F3. 패스워드 열람 시 OS 재인증

**현재 상태**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc:2156 RevealPassword`가 provider==native && enabled만 확인 후 평문 반환.
- `maho_core_reveal_password`(`lib.rs:1038`)는 `password_by_id`로 즉시 평문 반환. OS 인증 게이트 없음.
- 크로미움 내장 PM은 열람 전 `device_reauth::DeviceAuthenticator`로 OS 인증 요구 → 이 경로가 이를 우회.

**변경 사항** — `maho_settings_page_handler.cc`

1. `RevealPassword` 진입 시 `ChromeDeviceAuthenticatorFactory`로 `DeviceAuthenticator` 획득.
2. `AuthenticateWithMessage(u"저장된 비밀번호 보기", callback)` 호출.
3. 인증 성공 콜백에서만 `maho_core_reveal_password` 실행 후 결과 반환. 실패/취소 시 `std::nullopt`.
4. 재인증 유효창(예: 60초) 캐싱 — 짧은 시간 내 반복 열람은 재프롬프트 생략(크로미움 관행과 일치, `password_access_authenticator` 패턴 참고).
5. 플랫폼 미지원(생체/OS 인증 부재 리눅스 등) 시 정책: 기본은 인증 불가 시 열람 거부. 데스크톱 정합성 위해 폴백 여부는 리뷰에서 결정.

**검증**
- 수동: 열람 클릭 시 OS 프롬프트 노출, 취소 시 비노출.
- E2E: 자동화 스위치(`kEnableAutomation`) 하에서의 동작 정의(테스트 우회 경로가 프로덕션에 새지 않도록).

---

### F4. provider 탐지 결정화 + 검증 강화

**현재 상태** — `maho/crates/maho-core/src/extension_bridge.rs`
- `extensions`가 `HashMap`(`extension_bridge.rs:89`) → `find_password_provider`(399) 순회 순서 **비결정적**.
- `passwords` 권한을 가진 **임의 확장**을 provider 후보로 인식(403).
- C++ 측(`maho_password_provider_utils.cc:50~56`)은 하드코딩 ID 2종만 벤더로 매핑, 그 외엔 native 폴백.

**변경 사항**

1. **결정적 순회** — `find_password_provider`
   - `values()` 대신 확장 ID 정렬 순회, 또는 등록 순서 보존(BTreeMap/Vec index). 다중 후보 시 선택 규칙 명문화.

2. **선택-탐지 정합 검증** — `maho_password_provider_utils.cc` / 코어
   - 사용자가 고른 provider(`autofill.password_provider`)와 실제 탐지된 확장 ID가 **일치할 때만** 해당 벤더 활성.
   - 불일치(예: 1Password 선택했으나 확장 미설치/비활성) 시:
     - 크로미움 PM을 무조건 끄지 않는다. `IsSavingAndFillingEnabled`가 provider 불가용을 감지하면 **안전한 상태(native 폴백 또는 명시적 비활성 배너)**로 처리해 fill 공백/다운그레이드 방지.
   - `chrome_password_manager_client.cc:273,291`의 `ShouldEnableChromiumPasswordManager` 조건을 "native active" 단일 조건에서 "native active **또는** 선택 provider 불가용"으로 확장.

3. **권한만으로 벤더 승격 금지**
   - `passwords` 권한 보유는 "후보"일 뿐, 벤더 매핑은 알려진 ID 화이트리스트로만. 미지 확장은 provider로 승격하지 않고 native 유지(현 동작 유지하되 명시화 + 로깅).

**검증**
- 단위: 다중 provider-권한 확장 존재 시 결정적 선택(회귀 테스트 `extension_bridge.rs:465~`에 케이스 추가).
- 단위: 선택≠설치 상태에서 fill이 native로 안전 폴백.

---

## 마일스톤 3 — 인가 심화 & 확장성 (F5, F6)

### F5. FFI CRUD 인가 게이트

**현재 상태**
- `maho_core_add/delete/reveal/update_password`는 `import_gate::is_active()`만 확인(`lib.rs:1038~,1073~` 등). 사용자 신원 검증 없음.

**변경 사항**
- 쓰기/열람 계열 FFI는 M2의 재인증(F3)과 결합: 열람은 C++ 계층에서 이미 게이트되므로, **add/update/delete도 민감 작업으로 분류**하고 UI 계층에서 세션 유효성(잠금 해제) 확인 후 호출.
- 코어 레벨에서는 최소한 `import_gate` 외에 "storage 초기화 완료 & 키 주입됨" 전제 조건을 assert(키 없이 평문 경로로 add 되는 것 차단).

**검증**: 키 미주입 상태에서 add 계열이 실패하는 회귀 테스트.

---

### F6. provider 메타데이터 레지스트리화 (확장성)

**현재 상태** — 새 provider 추가 시 7계층 수정 필요:
`passwords.rs:5`(enum) · `extension_bridge.rs:404`(ID) · `maho_password_provider_utils.cc:26-56,103-108`(매핑) · `maho_settings.mojom:84`(enum) · `maho_settings_password_helpers.cc:18`(ToKind) · `maho_settings_page_handler.cc:2044`(옵션) · `domain_panes.tsx:185`(TS).

**변경 사항 (리팩터링, 기능 무변)**
1. Rust 단일 소스: `provider_id`, `display_name`, `extension_ids: &[&str]`, `capabilities`를 담는 provider 디스크립터 테이블을 코어에 도입.
2. `GetPasswordProviderOptions`/`GetPasswordProviderStatus`가 하드코딩 블록 대신 이 테이블을 순회하도록 변경.
3. C++ `maho_password_provider_utils.cc`의 ID↔벤더 매핑을 테이블 조회로 대체.
4. mojom/TS는 여전히 enum이 필요하나, "provider 목록"은 데이터 기반이 되어 신규 벤더 추가 시 코드 변경 표면을 대폭 축소.

**주의**: F6는 순수 리팩터링이므로 M1/M2 이후 착수. 동기화(`sync_models.rs`)에 패스워드는 **계속 제외**(현 설계 유지).

---

## 착수 순서 & 리스크

1. **M1(F1,F2)** 먼저 — 평문 저장이 최대 위험. F1은 키 주입 순서(코어 생성 전 전역 주입)가 관건이며, 폴백 정책 변경으로 인해 **기존 평문 DB 사용자에 대한 1회 마이그레이션 경로**(sqlite.rs 마이그레이터)가 반드시 함께 검증되어야 한다.
2. **M2(F3,F4)** — 열람 인가 및 provider 신뢰성. 크로미움 `device_reauth` 의존.
3. **M3(F5,F6)** — 인가 심화 및 확장성 리팩터링.

**공통 회귀 위험**
- `maho-storage/src/sqlite.rs`는 현재 **uncommitted(working tree)** 상태 → 변경 전 베이스라인 커밋 권장.
- 키 주입 실패 시 조용한 평문 폴백을 제거하므로, 키 파생 실패가 곧 "패스워드 기능 비활성"이 된다. 사용자 안내(설정 배너) 필요.
- mail 앱(`mail/crates/maho-core/src/ffi.rs:41 MahoMailInjectKeys`)은 별도 스택이며 현재 키를 실제로 사용하지 않는 스텁 — 본 계획 범위 외이나, 동일 결함이 있으므로 별도 티켓 등록 권장.

## 완료 기준 (DoD)
- [ ] 실행 중 `maho.db`가 키 없이 열리지 않음(평문 아님) — 실측.
- [ ] 마이그레이션 후 `*.db.plain_backup` 부재.
- [ ] 패스워드 열람 시 OS 재인증 프롬프트.
- [ ] provider 다중 후보 하에 결정적 선택 + 선택≠설치 시 안전 폴백.
- [ ] 키 미주입 상태에서 add/update/reveal 실패.
- [ ] 신규 provider 추가가 데이터 테이블 + enum 최소 수정으로 가능.
