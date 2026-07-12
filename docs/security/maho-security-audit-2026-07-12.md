# Maho 브라우저(Chromium 포크) 보안 취약점 점검 보고서

- 작성일: 2026-07-12
- 대상: `maho-chromium/browser` 이하 Maho 고유 코드 (MCP 로컬 서버, 자동 업데이트, os_crypt, FFI 브리지 등)
- 범위: 포크가 추가한 커스텀 공격 표면 중심의 소스 코드 리뷰 (동적 분석/퍼징 미포함)
- 조치: **코드 직접 수정 없음.** 아래는 발견 사항과 권고안만 기술함.

## 요약

| # | 심각도 | 제목 | 위치 |
|---|--------|------|------|
| 1 | Medium (High 가능) | `browser_navigate`가 `data:` URL에 대해 도메인 허용목록(allowlist)을 우회 | `maho_mcp_session.cc:889` |
| 2 | Medium | Windows 업데이트 경로에 롤백(다운그레이드) 방지 검증 부재 + `http://` 업데이트 서버 허용 | `maho_update_delegate_win.cc:280`, `maho_update_server_url.cc:18` |
| 3 | Low | macOS os_crypt 키 파일 권한 검증이 릴리스 빌드에서 무력화(DCHECK) | `maho_file_key_provider_mac.mm:77` |
| 4 | Low | Windows 업데이트 설치 파일의 TOCTOU (고정 경로 + 검증 후 지연 실행) | `maho_update_delegate_win.cc:342`, `:112` |

전반적으로 MCP 자격증명 스크러빙(firewall), FFI 콜백 수명 관리(`FfiCallbackHandle`), 업데이트 서명 검증(ed25519 + SHA-256 + Authenticode), 이름있는 파이프/유닉스 소켓의 접근 제어는 상당히 견고하게 작성되어 있습니다. 아래 항목은 그 경계에 존재하는 불일치·누락 위주입니다.

---

## 1. `browser_navigate`의 `data:` 스킴 도메인 허용목록 우회 — Medium

**위치:** `maho-chromium/browser/mcp/maho_mcp_session.cc:889`

```cpp
if (!nav_url.SchemeIs("data") && !IsHostAllowed(nav_url.host())) {
  // ... 차단 ...
}
```

**설명**

MCP 세션은 자동화 에이전트(로컬 MCP 클라이언트)가 사용자가 승인한 도메인 집합(`allowed_domains_`)으로만 탭을 이동시킬 수 있도록 `IsHostAllowed()`로 통제합니다. 이 허용목록은 프롬프트 인젝션 등으로 오작동한 에이전트가 임의의 위치로 브라우저를 몰아가는 것을 막는 **신뢰 경계**입니다.

그런데 `browser_navigate` 핸들러는 스킴이 `data:`이면 `IsHostAllowed()` 검사를 **명시적으로 건너뜁니다.** `data:` URL은 host가 비어 있어 정상적으로는 `IsHostAllowed("")`가 `false`를 반환해 차단되어야 하지만, 이 조건 때문에 예외적으로 통과합니다.

같은 파일의 `browser_tab_new` 핸들러(`maho_mcp_session.cc:1042`)는 동일한 `data:` 예외 없이 `IsHostAllowed(nav_url.host())`만 호출하므로 `data:`가 정상 차단됩니다. 즉 두 내비게이션 경로 간 정책이 **불일치**하며, `browser_navigate`만 허점을 가집니다.

**영향**

로컬 MCP 클라이언트(또는 인젝션된 에이전트)가 승인 도메인과 무관하게 활성 탭을 `data:text/html,<script>...</script>` 로 이동시켜 임의의 공격자 제어 HTML/JS를 최상위 문서로 렌더링할 수 있습니다.
- `data:`는 opaque origin이라 허용 도메인의 쿠키/스토리지 탈취는 불가하지만,
- 신뢰된 브라우저 창 내부에 임의 콘텐츠(피싱 화면)를 그리거나,
- 스크립트로 임의 서버에 데이터를 유출(fetch)하는 등, **도메인 승인 게이트 자체를 무력화**합니다.

`data:` 콘텐츠 페이로드는 재귀적 신뢰가 없는 사용자 입력이므로, 허용목록의 목적(승인된 도메인만)에 정면으로 반합니다.

**권고**

- `browser_navigate`에서 `data:` 예외를 제거하거나(권장), 최소한 `browser_tab_new`와 정책을 일치시킬 것.
- `data:` 상위 프레임 내비게이션을 허용해야 하는 정당한 사용례가 있다면, 별도의 명시적 옵트인 플래그로 분리하고 기본은 차단.
- 아울러 `javascript:`, `file:`, `chrome:` 등 host 없는 위험 스킴 전반에 대한 명시적 거부 목록을 두어 방어를 이중화할 것(현재는 빈 host로 인해 우발적으로 막히는 구조).

---

## 2. Windows 업데이트 롤백 방지 부재 + `http://` 업데이트 서버 허용 — Medium

**위치:** `maho-chromium/browser/updates/maho_update_delegate_win.cc:237-312` (`OnManifestFetched`), `maho_update_server_url.cc:14-20`

**설명**

Windows 커스텀 다운로더 경로 `OnManifestFetched()`는 서명 매니페스트에서 `version`, `installer_url`, `installer_sha256`, `installer_signer_cn`, `rollout_bucket`을 파싱한 뒤 **롤아웃 버킷만** 비교하고 곧바로 다운로드/설치를 진행합니다. 매니페스트의 `version`을 **현재 실행 중인 버전과 비교하지 않습니다.**

대조적으로 Linux 경로(`maho_update_delegate_linux_pkg.cc:133-139`)는 다음과 같이 명시적 다운그레이드 방지 로직을 갖고 있습니다:

```cpp
base::Version latest(*latest_str);
base::Version current(version_info::GetVersionNumber());
if (!latest.IsValid() || !current.IsValid() || latest <= current) {
  // kUpToDate — 다운그레이드 안 함
}
```

또한 `IsAcceptableUpdateServerUrl()`은 `http://`와 `https://`를 모두 허용하며(`maho_update_server_url.cc:18`), 서버 URL은 환경변수(`kMahoUpdateServerOverrideEnvVar`)와 프로필 pref(`kMahoUpdateServerUrl`)로도 덮어쓸 수 있습니다.

**영향 (롤백/다운그레이드 공격)**

매니페스트는 ed25519로 서명되므로 위조는 불가하지만, **과거에 정상 서명되었던 매니페스트+설치 파일**은 여전히 유효합니다. 설치 파일도 매니페스트의 서명자 CN·SHA-256으로 검증되나, 예전 Maho 설치 파일은 정상 서명되어 있으므로 이 검증을 통과합니다.

따라서 공격자가(평문 `http://` 구간의 MITM 또는 악성 미러) **오래된 유효 서명 매니페스트를 재생(replay)** 하면, 버전 신선도 검사가 없는 Windows 클라이언트는 이를 "업데이트"로 받아들여 **알려진 취약점을 가진 구버전으로 다운그레이드**할 수 있습니다. 이후 그 구버전의 취약점으로 추가 공격이 가능합니다.

**권고**

- Windows `OnManifestFetched()`에도 `latest <= current`(또는 `<`) 다운그레이드 방지 검증을 추가할 것. Linux 경로와 동일 정책으로 통일.
- 업데이트 서버 URL은 `https://`만 허용(`http://` 제거). 최소한 개발/테스트 오버라이드에만 한정.
- 매니페스트에 만료 시각(nonce/timestamp) 또는 단조 증가하는 발행 카운터를 넣어 재생 공격을 원천 차단하는 것을 검토.

---

## 3. macOS os_crypt 키 파일 권한 검증이 릴리스 빌드에서 무력화 — Low

**위치:** `maho-chromium/browser/os_crypt/maho_file_key_provider_mac.mm:76-79`

```cpp
uint32_t secure_status = 0;
DCHECK(maho_core_oscrypt_key_path_is_secure(password_file.value().c_str(),
                                            &secure_status));
```

**설명**

os_crypt 대칭키를 파생하는 키 파일(`maho_oscrypt_key`)의 경로/권한 안전성을 확인하는 `maho_core_oscrypt_key_path_is_secure()` 호출이 `DCHECK()` 안에만 존재합니다.

- 릴리스 빌드에서 `DCHECK`는 표현식 전체가 컴파일에서 제거되어 **호출 자체가 이뤄지지 않습니다.**
- 디버그 빌드에서도 반환값(`secure_status`)이 이후 로직에 사용되지 않아 **어떤 흐름도 게이트하지 않습니다.**

즉 이 보안 점검은 실질적으로 죽은 코드입니다. 키 파일이 예상보다 느슨한 권한(예: group/other 접근 가능)으로 존재하더라도 아무런 조치 없이 키를 파생·사용합니다. 이 키는 저장 자격증명 등 로컬 암호화의 루트이므로, 권한 검증 누락은 방어심층(defense-in-depth) 약화입니다.

**권고**

- 검증을 `DCHECK`에서 꺼내 실제 런타임 검사로 만들고, `secure_status`가 안전하지 않으면 `KeyError`를 반환하거나 권한을 강제 재설정(0600)한 뒤 진행하도록 할 것.

---

## 4. Windows 업데이트 설치 파일 TOCTOU (검증 후 지연 실행) — Low

**위치:** `maho-chromium/browser/updates/maho_update_delegate_win.cc:340-350`(다운로드 경로), `:353-399`(검증), `:107-132`(실행)

**설명**

- 설치 파일은 고정·예측 가능한 경로 `%TEMP%\maho_update.exe`로 다운로드됩니다(`DIR_TEMP` + 리터럴 파일명).
- 다운로드 직후 SHA-256 + Authenticode를 검증하고 `verified_installer_path_`에 저장하지만, **실제 실행은 이후 사용자가 재시작을 트리거하는 `ApplyUpdateAndRestart()` 시점**에 일어납니다.
- 검증과 실행 사이에 파일 무결성을 재확인하지 않습니다.

`%TEMP%`는 Windows에서 사용자별 디렉터리라 동일 사용자 신뢰 경계 내이지만, 같은 사용자로 실행 중인 다른(악성) 프로세스가 **검증 통과 후 실행 전 사이에 `maho_update.exe`를 악성 바이너리로 교체**할 수 있습니다. NSIS 사일런트 설치(`/S`)가 UAC 승격을 요구하는 경우, 이 교체는 사용자→관리자 권한 상승 통로가 될 수 있습니다.

**권고**

- 검증된 파일을 다른 프로세스가 쓸 수 없는 위치(랜덤 하위 디렉터리, ACL로 현재 사용자 전용, 혹은 검증 중 배타적 잠금 유지)로 이동한 뒤 그 핸들/경로로 실행.
- 실행 직전에 SHA-256(또는 Authenticode)을 **재검증**.
- 가능하면 열어둔 파일 핸들 기반으로 실행하여 경로 교체 여지를 제거.

---

## 참고: 점검했으나 문제 없거나 견고했던 항목

- **MCP 자격증명 스크러빙(`maho_mcp_firewall.cc`):** 헤더/쿼리/JWT/Bearer/AX 트리 자동완성 등 다층 레닥션. HAR 결과는 `Redacted<T>` 타입으로 강제되어(`browser_network_stop_capture`) 미레닥션 데이터가 egress되기 어려운 구조.
- **유닉스 도메인 소켓 서버(`maho_mcp_socket_server.cc`):** peer UID == `getuid()` 인증, 소켓 0600, 세션 진입 시 UID 재확인.
- **Windows 이름있는 파이프(`maho_mcp_pipe_server_win.cc`):** 현재 사용자 SID 전용 DACL, `FILE_FLAG_FIRST_PIPE_INSTANCE`로 스쿼팅 방지, `PIPE_REJECT_REMOTE_CLIENTS`, DPAPI 세션 토큰 이중 확인. 서버가 클라이언트를 impersonate하지 않음(올바름).
- **세션 토큰(`maho_mcp_session_token_win.cc`):** DPAPI 사용자 스코프 암호화, 원자적 파일 쓰기, ACL 제한, `CRYPTO_memcmp` 상수시간 비교, 크기 불일치 조기 반환.
- **업데이트 서명(`maho_update_signature.cc`):** ed25519 서명 길이/영(zero)키 검증, 스트리밍 SHA-256, 대소문자 무시 hex 비교. 서명 대상 바이트가 수신 바이트와 정확히 일치하도록 설계(canonicalization 회피).
- **FFI 콜백 수명(`maho_ffi_callback_handle.h`):** shared_ptr 브리지로 in-flight 콜백 UAF 방지(과거 감사 C2/C5/H6 대응). 단, `FromUserData()`가 `Cancel()` 이후 호출되면 UB라는 계약이 있으므로, Rust 측이 취소 후 콜백을 절대 호출하지 않음을 별도로 보장하는지 확인 권장.

---

## 다음 단계 제안 (우선순위)

1. #1 `data:` 우회 수정 — 사용자 승인 경계 직접 관련, 즉시 조치 권장.
2. #2 Windows 다운그레이드 방지 + `https` 강제 — 자동 업데이트는 RCE급 파급이므로 우선.
3. #3, #4 — 방어심층 강화 차원에서 정리.

> 본 보고서는 정적 리뷰 기반이며 런타임 검증(PoC)은 수행하지 않았습니다. #1, #2는 로컬 MCP 클라이언트 및 업데이트 매니페스트를 이용한 재현 테스트로 후속 검증을 권장합니다.
