# Per-Site Shield Panel Implementation

## Goal

주소바의 site info 아이콘(🔒) 클릭 시 나타나는 page info bubble에 Content Blocker 섹션을 추가한다. Brave의 Shields 패널처럼 사이트별 adblock on/off 토글과 차단 통계를 보여주되, Maho의 기존 native Views 패턴(BubbleDialogDelegate)을 따른다.

## Architecture Decision

- **WebUI bubble (X)** — Maho에 WebUIBubbleDialogView 패턴 없음
- **Native Views bubble (O)** — `maho_sidebar_action_popover_view.cc`의 `BubbleDialogDelegate` 패턴 재사용
- Page info hook은 `MahoPageInfoUI::CustomizePageInfo()` (현재 stub)를 확장

## Scope

### Part A: Per-tab blocked count tracking (C++ throttle layer)

현재 throttle은 block/redirect/rewrite만 하고 카운트를 안 함. 탭별 차단 수를 추적해야 패널에 표시할 수 있다.

**새 파일**: `maho-chromium/browser/net/maho_ad_block_tab_helper.h`, `maho_ad_block_tab_helper.cc`

WebContents에 붙는 TabHelper로, 현재 페이지의 blocked count를 추적.

```
class MahoAdBlockTabHelper : public content::WebContentsUserData<MahoAdBlockTabHelper> {
  // 페이지 navigation 시 카운트 리셋
  // IncrementBlockedCount() — throttle에서 호출
  // GetBlockedCount() — UI에서 조회
  // AddObserver/RemoveObserver — UI 업데이트 알림용
};
```

**문제**: throttle은 `URLLoaderThrottle`이라 `WebContents`에 직접 접근 불가. 해결 방법:
- `MahoAdBlockThrottle` 생성 시 `WebContents`의 weak ref 또는 tab helper pointer 전달
- `ChromeContentBrowserClient::CreateURLLoaderThrottles`에서 `wc` 파라미터 활용하여 tab helper 조회 후 콜백 바인딩
- 또는 throttle에서 블록 시 콜백 호출 → holder에서 active tab의 tab helper로 라우팅

**참고 구현**: Brave의 `brave_shields_tab_helper.cc`가 동일 패턴. `GetTotalBlockedCount()` = ads + redirects + JS + fingerprint 합산.

### Part B: Shield bubble view (Native Views)

**새 파일**: `maho-chromium/browser/ui/views/shields/maho_shield_bubble_view.h`, `maho_shield_bubble_view.cc`

`BubbleDialogDelegate` 패턴 (`maho_sidebar_action_popover_view.cc` L68-88 참고).

UI 구성:
```
┌─────────────────────────────┐
│  🛡️  example.com            │
│                             │
│  ● Content Blocker   [ON]   │  ← Toggle (site exception)
│                             │
│  12 requests blocked        │  ← MahoAdBlockTabHelper::GetBlockedCount()
│                             │
│  ──────────────────────     │
│  Content Blocker Settings → │  ← chrome://maho-settings 의 content-blocker pane으로 이동
└─────────────────────────────┘
```

컨트롤:
- **Toggle**: `maho::core::AddSiteException(core, origin)` / `RemoveSiteException` 호출
- **Blocked count**: `MahoAdBlockTabHelper::GetBlockedCount()` 조회
- **Settings link**: `chrome::ShowSettingsSubPage(browser, "content-blocker")` 또는 동등한 내비게이션

스타일: 기존 `kBubbleCornerRadius=18`, `kBubbleContentsInsets=10`, `ui::kColorSysSurface2` 배경 패턴 따름.

### Part C: Page info hook 확장

**수정 파일**: `maho-chromium/browser/ui/site_control/maho_page_info_ui.h`, `maho_page_info_ui.cc`

현재 stub인 `CustomizePageInfo()`를 확장하여 shield bubble를 열거나, page info bubble 내에 Content Blocker 섹션을 삽입.

두 가지 접근:
1. **Separate bubble**: page info 아이콘 옆에 shield 아이콘 추가 → 별도 bubble (Brave 스타일)
2. **Page info 내 섹션**: 기존 page info bubble에 Content Blocker row 추가 (Firefox 스타일)

→ **권장: 옵션 2 (Page info 내 섹션)**. Maho에 새 toolbar 아이콘 추가는 upstream Chromium 패치가 필요하여 복잡. Page info hook(`CustomizePageInfo`)이 이미 있으므로 이를 활용.

### Part D: BUILD.gn 업데이트

- `browser/net/BUILD.gn`에 tab_helper 소스 추가
- `browser/ui/views/BUILD.gn`에 shields 디렉토리 추가 (또는 기존 site_control BUILD.gn 확장)
- deps에 `//maho/third_party/maho` (bridge), `//content/public/browser` 추가

## Files to Create

```
maho-chromium/browser/net/maho_ad_block_tab_helper.h
maho-chromium/browser/net/maho_ad_block_tab_helper.cc
maho-chromium/browser/ui/views/shields/maho_shield_bubble_view.h
maho-chromium/browser/ui/views/shields/maho_shield_bubble_view.cc
```

## Files to Modify

```
maho-chromium/browser/ui/site_control/maho_page_info_ui.h
maho-chromium/browser/ui/site_control/maho_page_info_ui.cc
maho-chromium/browser/net/maho_ad_block_throttle.h    (add blocked callback)
maho-chromium/browser/net/maho_ad_block_throttle.cc   (call callback on block)
maho-chromium/browser/net/BUILD.gn                    (add tab_helper sources)
```

## Reference Files (DO NOT MODIFY — read for patterns only)

```
maho-chromium/browser/ui/views/sidebar/maho_sidebar_action_popover_view.cc  — BubbleDialogDelegate 패턴
maho-chromium/browser/ui/views/sidebar/maho_sidebar_action_popover_view.h
maho-chromium/third_party/maho/maho_bridge.h          — AddSiteException/RemoveSiteException/CheckRequest API
maho-chromium/third_party/maho/maho_bridge.cc
maho-chromium/browser/maho_core_holder.h               — maho::GetCore() 싱글톤
maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom — FilterListInfo, ContentBlockerStats
```

## Constraints

- UI thread only: MahoCore는 `Rc<RefCell<>>` 기반. 모든 bridge 호출은 UI thread에서.
- `DCHECK_CURRENTLY_ON(content::BrowserThread::UI)` 필수.
- Chromium C++ style: explicit includes, namespace-local helpers, DCHECK.
- 기존 `ShouldBlockRequest`/`CheckRequest` FFI는 변경하지 않음.
- 새 FFI 함수 추가 불필요 — `AddSiteException`/`RemoveSiteException`/`GetSiteExceptions` 이미 bridge에 있음.
- `cargo check --workspace`가 깨지면 안 됨 (Rust 변경 없을 예정이지만 확인 필수).

## Verification

- [ ] Tab helper: 페이지 로드 시 카운트 0으로 리셋, block 시 increment
- [ ] Shield bubble: anchor view에서 정상 표시, toggle 동작, count 표시
- [ ] Page info hook: `CustomizePageInfo()`가 bubble 또는 섹션을 올바르게 트리거
- [ ] BUILD.gn: `autoninja -C out/Default maho_ad_block_tab_helper` 단위 빌드 성공
- [ ] Thread safety: 모든 bridge 호출에 DCHECK
- [ ] Existing tests: `cargo test --workspace` 통과 (Rust side 미변경 확인)
