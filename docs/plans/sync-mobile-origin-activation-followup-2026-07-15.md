# 후속 이슈 — 모바일(iOS/Android) 동기화 생성-측 활성화 갭 & 데드 배선

**작성일:** 2026-07-15
**성격:** F1(데스크톱 E2EE 생성-측 결함) 수정 리뷰 중 파생된 **모바일 후속 조사**
**선행:** `docs/plans/sync-e2ee-origin-fix-2026-07-15.md` (데스크톱 F1 수정), `docs/security/sync-passphrase-relay-review-2026-07-15.md`
**우선순위:** Medium — 보안 무력화 아님(아래 §1), UX 갭 + 데드코드

---

## 0. TL;DR

데스크톱 F1은 "생성 측 Continue가 키를 설정하지 않고 시드 hex를 room_id로 저장" = **E2EE 무력화**였다.
모바일은 **그 결함을 그대로 갖고 있지 않다.** 모바일 "복구 문구 생성"은 **표시 전용**이고, 실제 E2EE
활성화는 iOS·Android 모두 `joinSync → join_sync`(키 유도 + `set_encryption_key` 정상)로만 일어나기
때문이다. 따라서 **모바일에서 평문 유출은 발생하지 않는다.**

남는 문제는 두 가지:
1. **UX 갭:** 생성 디바이스도 자기가 만든 문구를 직접 "Join" 칸에 다시 입력해야만 동기화가 시작된다.
   데스크톱의 원탭 "Continue"(= `configureSyncEncryption`)에 해당하는 생성-측 활성화 액션이 없다.
2. **데드 배선:** 키를 인자로 받는 `start_sync` 경로(`MahoBridge.startSync`/`nativeStartSync`,
   `maho_core_start_sync`)가 모바일 UI에서 **아무도 호출하지 않는다.**

---

## 1. 왜 보안 결함이 아닌가 (근거)

| 플랫폼 | 생성("문구 생성") | 활성화 경로 | 키 설정 여부 |
|---|---|---|---|
| iOS | `SyncSettingsView.swift:324` "Generate Sync Key" → `bridge.generateSyncKey()` **표시만** | `:330` "Join with Recovery Phrase" → `MahoBridge+Sync.swift:47` → `maho_core_join_sync(ptr, phrase)` | ✅ join_sync가 유도+설정 |
| Android | `SyncSettingsScreen.kt:355` "Generate Recovery Phrase" → `BridgeSync.generateSyncKey()` **표시만** | `:378` "Join" → `BridgeSync.joinSync` → `MahoBridge.joinSync` → `nativeJoinSync` → `core.join_sync("wss://sync.mahobrowser.com", phrase)` (`maho-jni/src/lib.rs:1184`) | ✅ join_sync가 유도+설정 |

핵심: 모바일 UI에는 "생성한 문구 → `start_sync(url, syncKey)`" 배선이 **존재하지 않는다.** 데스크톱 F1의
평문 유출은 바로 그 잘못된 배선(`startSync(syncKey_seed_hex)`) 때문이었는데, 모바일엔 그 배선 자체가
없다. 모든 실제 활성화는 `join_sync`(가입 경로 = 검증된 정상 경로)를 탄다.

---

## 2. 문제 1 — 생성-측 활성화 UX 갭

**증상:** 새 동기화를 시작하려는 디바이스에서 문구를 "생성"해도 동기화가 시작되지 않는다. 사용자는
표시된 문구를 복사해 같은 화면의 "Join/복구 문구로 참여" 입력란에 **직접 붙여넣어야** 비로소
`join_sync`가 돌며 E2EE 세션이 활성화된다.

**데스크톱과의 대비:** 데스크톱은 F1 수정 후 "Continue" 한 번으로
`handler.configureSyncEncryption(pendingKeyInfo.recoveryPhrase)`를 호출해 즉시 활성화된다
(`domain_panes.tsx:2192`). 모바일엔 이 원탭 활성화가 없다.

**권장 수정:** 데스크톱과 동일 프리미티브(`configure_sync_encryption_for_recovery_phrase`)를 모바일에
노출하고, "문구 생성" 뒤 "이 디바이스에서 새 동기화 시작" 액션이 그것을 호출하게 한다.

배선 필요(현재 모바일 FFI/JNI에 `configure_sync_encryption` 바인딩이 **없음** — 데스크톱 브리지에만 존재):
- **iOS:** `ios-shell/include/maho_ffi.h`에 `maho_core_configure_sync_encryption` 선언 추가(현재 없음,
  cbindgen 재생성) + `Bridge/MahoBridge+Sync.swift`에 래퍼 + `SyncSettingsView.swift`에 버튼.
- **Android:** `maho-jni/src/lib.rs`에 `Java_..._nativeConfigureSyncEncryption` 추가 →
  `MahoBridge.kt` 래퍼 → `BridgeSync.kt` 래퍼 → `SyncSettingsScreen.kt`에 버튼.
- **core/ffi:** `maho_core_configure_sync_encryption`(`maho-ffi/src/lib.rs:6468`)과
  `configure_sync_encryption_for_recovery_phrase`(`maho_core.rs:8422`)는 이미 존재 — 재사용.

**대안(코드 추가 최소):** "문구 생성" 성공 직후 생성된 문구로 곧바로 `joinSync`를 호출해 자동 활성화.
의미상 "가입"이라 혼동 여지가 있으나 데스크톱 계획서 §2의 안 C와 동일한 스톱갭.

---

## 3. 문제 2 — 키 인자 `start_sync` 데드 배선

모바일 UI에서 호출자가 없는 경로:
- **Android:** `MahoBridge.kt:603 startSync(serverUrl, syncKey)` → `:1214 nativeStartSync` →
  JNI `maho-jni/src/lib.rs:1052` → `core.start_sync(&url, &key)`. `MahoBridge.startSync`의 호출자 **없음**
  (`SyncManager.kt:88 startSync()`는 폴링 토글용 별개 메서드).
- **iOS:** `ios-shell/include/maho_ffi.h:1595 maho_core_start_sync` 선언 존재하나 Swift 호출자 **없음**.

주의: **core의 `start_sync`는 제거 금지** — `join_sync`/`configure_sync_encryption_for_recovery_phrase`가
내부에서 호출하고, 다수 테스트가 직접 사용한다(`maho_core.rs:8411,8435`, `tests/sync_*`).

**권장:** 위 §2 활성화를 구현하면 이 데드 배선을 (a) `configure_sync_encryption`으로 대체하거나
(b) 순수 제거. 단독 정리라면 (b).

---

## 4. 참조 좌표

| 대상 | 위치 |
|---|---|
| iOS 생성(표시만) | `maho/ios-shell/Views/Settings/SyncSettingsView.swift:324` |
| iOS 가입(정상) | `SyncSettingsView.swift:330` → `Bridge/MahoBridge+Sync.swift:47` |
| iOS FFI(configure 없음) | `maho/ios-shell/include/maho_ffi.h` (join_sync만 `:1668`, start_sync `:1595`) |
| Android 생성(표시만) | `maho/android-shell/.../ui/settings/SyncSettingsScreen.kt:355` |
| Android 가입(정상) | `SyncSettingsScreen.kt:378` → `bridge/BridgeSync.kt:73` → `MahoBridge.kt:639 joinSync` |
| Android 데드 startSync | `MahoBridge.kt:603` → `:1214 nativeStartSync` → `maho-jni/src/lib.rs:1052` |
| 재사용할 core 프리미티브 | `maho-core/src/maho_core.rs:8422 configure_sync_encryption_for_recovery_phrase` |
| 재사용할 FFI | `maho-ffi/src/lib.rs:6468 maho_core_configure_sync_encryption` |

---

## 5. 검증 게이트 (구현 시)

- 단위(core): 데스크톱과 동일 — `configure_sync_encryption` 후 push가 `Encrypted`만 나가는지
  (`tests/persistence.rs:818` 패턴 재사용).
- iOS/Android 런타임: 생성 디바이스 A(원탭 활성화) + 가입 디바이스 B가 **같은 32자 방** 접속, A→릴레이
  모든 메시지 `Encrypted`, A 탭이 B에 반영.
- FFI/JNI ABI: iOS cbindgen 헤더 재생성 + Android JNI 심볼 일치 확인.

---

## 부록 — 이 문서의 범위 밖(별도 트랙)

- 데스크톱 §3.6 잔여 정리(`SyncKeyInfo.sync_key` 필드 축소)는 **다른 세션이 해당 WebUI 핸들러를 대규모
  수정 중**(2026-07-15 20:5x, `maho_settings_page_handler.cc` +291/-100)이라 충돌 위험으로 보류.
  그 세션 완료 후 별도 진행 권장.
