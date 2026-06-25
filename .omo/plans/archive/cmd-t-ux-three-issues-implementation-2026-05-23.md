# 상세 구현 계획 — Cmd+T UX 3-Issue Fix

**Generated**: 2026-05-23
**Author**: Sisyphus
**Scope**: Issue B (kTab switch bug), Issue A (Search row), Issue C (history populate)
**Total estimated diff**: ~210 lines across 9 files

---

## Issue B — kTab activation switch by stable_tab_id fallback

**원인 (확정)**: Rust `command_bar.rs:377` 가 `key = format!("tab:{}", tab.id)` 로 단일 UUID 만 emit 하지만, C++ parser (`maho_command_model.cc:561-572`) 는 `tab:{browser_session}:{tab_session}` 또는 `tab:tab_{index}` 만 인식. 결과적으로 `browser_session_id`/`tab_session_id`/`tab_index` 모두 empty로 들어가 `OpenCommandSuggestion` 의 switch 분기 (action_handler.cc:486-510) 가 발동 못 하고 fallthrough 로 빠져 새 탭을 만든다.

**핵심 관찰**:
- `suggestion.stable_tab_id` 는 이미 `id` JSON 필드에서 `kTab` type 한정으로 파싱돼 들어옴 (`maho_command_model.cc` 의 `stable_id_field` 블록).
- `MahoTabIdHelper::FromWebContents(wc)->stable_tab_id()` 가 이 값과 **같은 UUID** 를 반환 (이번 구조적 fix 로 보장됨).
- 따라서 stable_tab_id 만으로 탭 위치를 찾는 fallback 만 추가하면 해결.

### 변경 파일

#### 1. `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`

**(a) 새 helper 함수 두 개 추가** — 기존 `FindBrowserForStableTab` (line 61) 바로 아래.

```cpp
// Added after line 103 (after FindTabIndexForStableId).

// Locate any same-profile browser whose TabStripModel contains a WebContents
// whose MahoTabIdHelper carries the given stable_id. Used for the Rust-emitted
// `tab:{stable_id}` key shape where browser/tab session IDs are absent.
BrowserWindowInterface* FindBrowserForHelperStableId(
    Browser* origin_browser,
    const std::string& stable_id) {
  if (stable_id.empty()) {
    return nullptr;
  }
  Profile* origin_profile = origin_browser ? origin_browser->profile() : nullptr;
  BrowserWindowInterface* match = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&match, &stable_id, origin_profile](BrowserWindowInterface* candidate) {
        if (!candidate || !candidate->GetTabStripModel()) {
          return true;
        }
        if (origin_profile && candidate->GetProfile() != origin_profile) {
          return true;
        }
        TabStripModel* model = candidate->GetTabStripModel();
        for (int i = 0; i < model->count(); ++i) {
          auto* contents = model->GetWebContentsAt(i);
          if (!contents) continue;
          auto* helper = MahoTabIdHelper::FromWebContents(contents);
          if (helper && helper->stable_tab_id() == stable_id) {
            match = candidate;
            return false;  // stop iteration
          }
        }
        return true;
      });
  return match;
}

int FindTabIndexForHelperStableId(BrowserWindowInterface* browser,
                                   const std::string& stable_id) {
  if (!browser || stable_id.empty()) {
    return TabStripModel::kNoTab;
  }
  TabStripModel* model = browser->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    auto* contents = model->GetWebContentsAt(i);
    if (!contents) continue;
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == stable_id) {
      return i;
    }
  }
  return TabStripModel::kNoTab;
}
```

**(b) `OpenCommandSuggestion` fallback 추가** — line 501 (`}`) 와 line 503 (`if (suggestion.tab_index >= 0)`) 사이에 삽입.

```cpp
  // Fallback: kTab suggestions emitted by Rust use key="tab:{stable_id}" with
  // no browser/tab session IDs. Resolve via MahoTabIdHelper UUID match.
  if (suggestion.type == CommandSuggestionType::kTab &&
      !suggestion.stable_tab_id.empty()) {
    BrowserWindowInterface* target_browser =
        FindBrowserForHelperStableId(browser, suggestion.stable_tab_id);
    if (target_browser) {
      if (target_browser->GetBrowserForMigrationOnly() != browser) {
        target_browser->GetWindow()->Show();
        target_browser->GetWindow()->Activate();
      }
      int tab_index = FindTabIndexForHelperStableId(
          target_browser, suggestion.stable_tab_id);
      if (tab_index >= 0) {
        target_browser->GetTabStripModel()->ActivateTabAt(tab_index);
        return;
      }
    }
  }
```

**(c) include 추가** (header section, line ~30 근처):

```cpp
#include "maho/browser/maho_tab_id_helper.h"
```

**(d) BUILD.gn dep 추가** — `maho-chromium/browser/ui/views/command/BUILD.gn` 의 `maho_command_overlay` source_set deps 에 `//maho/browser:maho_tab_id_helper` 가 이미 있는지 확인. 없으면 추가.

### 검증

1. `cargo check --workspace` (영향 없음, no-op 예상)
2. `python3 maho-chromium/build/scripts/build_maho.py`
3. 사용자 검증:
   - YouTube 같은 페이지를 한 탭에서 연다
   - Cmd+T → "youtube" 입력
   - 결과 row 중 "YouTube ..." (kTab row) 선택 → 새 탭 생성이 아니라 **기존 탭으로 전환** 되어야 함
   - 별도로 `Go to https://youtube.com` (kNavigation row) 선택 → 새 탭 생성

### 위험

- `ForEachCurrentBrowserWindowInterfaceOrderedByActivation` 가 helper 일치하는 browser 를 반환하는데, 그 browser 가 closing 중이면 `GetWindow()` 가 null 일 수 있음. 기존 `FindBrowserForStableTab` 도 동일한 패턴 (`browser->GetWindow()`) 을 쓰므로 안전.
- 동일 helper UUID 가 두 browser 에 동시에 존재할 수 없음 (UUID 고유성). 첫 매치 즉시 break.
- profile 일치 가드로 incognito ↔ regular 사이 cross-leak 방지.

---

## Issue A — Search suggestion row (web search)

**Goal**: 사용자가 입력한 query 가 URL이 아니든 URL이든, "Search the web for [query]" 또는 "Search [engine] for [query]" 류 row 를 항상 노출. Reference: Arc/Chrome 의 omnibox 첫 row.

**랭킹 정책**:
- URL 입력 (`is_url(query) == true`): Navigation (score 2.0+) > Search (1.5) — Navigation 이 위.
- 비-URL 입력 (`is_url == false`): Search (1.5) 가 사실상 첫 결과. Tab/Bookmark/History/Action 매치는 fuzzy_score 에 따라 위/아래.

### 변경 파일

#### 1. `maho/crates/maho-types/src/traits/shell_renderer.rs`

```rust
// Line 114-125 — add Search variant.
pub enum SuggestionType {
    Tab,
    Bookmark,
    History,
    Action,
    Navigation,
    Search,           // ← NEW: web search using default search engine
    Calculator,
    UnitConversion,
    ArchivedTab,
    ClosedTab,
    Folder,
}
```

#### 2. `maho/crates/maho-core/src/command_bar.rs`

**(a) Search row emit** — `is_url` 분기 (line 347-367) 바로 아래에 무조건 추가:

```rust
        // Always offer a "search the web for X" row when query is non-empty.
        // Score 1.5 < Navigation (2.0) so URL input shows Navigation first.
        // Above History/Bookmark fuzzy matches when query is non-URL.
        if !query.trim().is_empty() {
            let search_url = self.get_search_url(query);
            let engine_name = self
                .get_default_search_engine()
                .map(|e| e.name.clone())
                .unwrap_or_else(|| "Google".to_string());
            results.push(SearchResult {
                suggestion: SuggestionViewModel {
                    kind: SuggestionType::Search,
                    key: format!("search:{}", query),
                    title: format!("Search {} for \"{}\"", engine_name, query),
                    subtitle: Some("Search".to_string()),
                    execution_payload: Some(search_url),
                    icon: None,
                    relevance_score: 1.5,
                    match_ranges: None,
                    shortcut: None,
                },
                score: 1.5,
            });
        }
```

**(b) `is_url_bearing_type` 에는 Search 미포함** — Search 는 dedup 대상 아님. 변경 없음.

**(c) `type_priority` 추가** (line 772-786):

```rust
SuggestionType::Tab => 0,
SuggestionType::Bookmark => 1,
SuggestionType::ClosedTab => 2,
SuggestionType::ArchivedTab => 3,
SuggestionType::History => 4,
SuggestionType::Navigation => 5,
SuggestionType::Search => 6,        // ← NEW
SuggestionType::Action => 7,        // shifted
SuggestionType::Folder => 8,        // shifted
SuggestionType::Calculator => 9,    // shifted
SuggestionType::UnitConversion => 10, // shifted
```

**(d) Mode-specific score boost** (line 654-678) — `kSearch` 도 mode 별 boost 추가:

```rust
"newTab" => match result.suggestion.kind {
    SuggestionType::Navigation => 0.5,
    SuggestionType::Search => 0.3,    // ← NEW (less than Navigation)
    SuggestionType::Action => 0.2,
    _ => 0.0,
},
"addressBar" => match result.suggestion.kind {
    SuggestionType::Navigation => 0.5,
    SuggestionType::Search => 0.3,    // ← NEW
    SuggestionType::History => 0.3,
    _ => 0.0,
},
"normal" => match result.suggestion.kind {
    SuggestionType::Action => 0.5,
    SuggestionType::Tab => 0.3,
    SuggestionType::Search => 0.2,    // ← NEW
    SuggestionType::Folder => 0.2,
    _ => 0.0,
},
```

#### 3. `maho-chromium/browser/ui/views/command/maho_command_model.h`

```cpp
// Line 36-48 — add kSearch to enum.
enum class CommandSuggestionType {
  kTab,
  kBookmark,
  kHistory,
  kAction,
  kNavigation,
  kSearch,           // ← NEW
  kCalculator,
  kUnitConversion,
  kArchivedTab,
  kClosedTab,
  kFolder,
  kRecentSearch,
};
```

#### 4. `maho-chromium/browser/ui/views/command/maho_command_model.cc`

**(a) `ParseType()` 추가** (line 610-634):

```cpp
  if (kind == "navigation")
    return CommandSuggestionType::kNavigation;
  if (kind == "search")                              // ← NEW
    return CommandSuggestionType::kSearch;            // ← NEW
  if (kind == "calculator")
    return CommandSuggestionType::kCalculator;
```

**(b) Result row 클릭 시 `actions_only_` filter 등 영향 없음** — Search type 은 actions_only 모드에서 자연스럽게 제외됨 (line 645-649 `s.type != kAction` 필터).

#### 5. `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`

**`OpenCommandSuggestion`** — `kRecentSearch` 분기 (line 447-477) 바로 아래에 `kSearch` 분기 추가:

```cpp
  if (suggestion.type == CommandSuggestionType::kSearch) {
    if (suggestion.execution_payload.empty())
      return;
    GURL gurl(suggestion.execution_payload);
    if (!gurl.SchemeIsHTTPOrHTTPS())
      return;
    NavigateParams params(browser, gurl, ui::PAGE_TRANSITION_GENERATED);
    params.disposition = disposition;
    Navigate(&params);
    return;
  }
```

이미 Rust 가 `execution_payload` 에 search URL 을 만들어 주므로 단순 navigate. registry 가 자동으로 새 tab 을 Rust core 에 등록함.

#### 6. `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`

**(a) `MapTypeToColor`** (line 196-220) — Search 색상 추가:

```cpp
case CommandSuggestionType::kSearch:
  return SkColorSetRGB(0xA8, 0xC4, 0xF0);  // soft blue (search)
```

**(b) `DrawLineIcon`** (line 240-302) — Search 돋보기 아이콘 추가 (kHistory/kRecentSearch 의 시계+화살표 와 다른, 돋보기 형태):

```cpp
case CommandSuggestionType::kSearch: {
  // Magnifying glass: circle + handle.
  const float r = rect.width() * 0.28f;
  const auto center = gfx::PointF(
      rect.x() + rect.width() * 0.42f,
      rect.y() + rect.height() * 0.42f);
  canvas->DrawCircle(center, r, stroke_flags);
  canvas->DrawLine(
      gfx::PointF(center.x() + r * 0.7f, center.y() + r * 0.7f),
      gfx::PointF(rect.x() + rect.width() * 0.78f,
                  rect.y() + rect.height() * 0.78f),
      stroke_flags);
  break;
}
```

### 검증

1. `cd maho && bun run test` (Rust unit tests; 실패 시 `command_bar` test 가 새 row 를 모르고 실패할 수 있음 → 그 경우 test fixture 업데이트)
2. `python3 maho-chromium/build/scripts/build_maho.py`
3. 사용자 검증:
   - Cmd+T → "youtube.com" 입력 → 두 row 노출:
     - "Go to https://youtube.com" (Navigation, 위)
     - "Search Google for \"youtube.com\"" (Search, 아래)
   - Cmd+T → "hello world" 입력 (URL 아님) → Search row 가 첫 결과 (Navigation 안 뜸)
   - Search row Enter → Google 검색 페이지가 새 탭으로 열림

### 위험

- `SuggestionType` enum 은 `#[non_exhaustive]` 라 외부 crate 추가 안전.
- C++ `kSearch` 추가는 모든 switch 문 대응 필요. 이미 `MapTypeToColor` 와 `DrawLineIcon` 에서 처리. **추가로 `result_row_view.cc:259` 의 `kRecentSearch` 부근 favicon 처리 분기도 확인 필요** (Search type 은 favicon 없으므로 fallback glyph 만 그리면 됨).
- 새 type 추가 후 unit test 가 enum exhaustiveness 위반으로 실패할 수 있음 → switch 마다 default 가 있는지 확인. 없으면 추가.

---

## Issue C — History populate on navigation commit

**Goal**: 사용자가 페이지를 navigate 할 때마다 `maho_core_add_history_entry(url, title)` 호출. 사용자가 같은 URL 을 다음에 검색하면 History row 가 노출되도록.

**구현 위치**: `MahoTabRegistry::StripObserver` 에 per-WebContents observer 를 추가 — observer 가 `WebContentsObserver::DidFinishNavigation` 을 듣고 dispatch.

**Lifecycle**: tab 이 닫힐 때 observer 자동 정리 (raw_ptr 가 아니라 `std::map<content::WebContents*, std::unique_ptr<NavObserver>>` 로 owned).

### 변경 파일

#### 1. `maho-chromium/browser/maho_tab_registry.h`

```cpp
// Forward-declare NavObserver as inner type.
class MahoTabRegistry : public BrowserCollectionObserver {
  // ... existing ...
 private:
  class StripObserver;
  class NavObserver;        // ← NEW
  // ...
};
```

#### 2. `maho-chromium/browser/maho_tab_registry.cc`

**(a) include 추가**:

```cpp
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
```

**(b) anonymous namespace 에 helper 추가** — `DispatchCloseTabForContents` 아래:

```cpp
void DispatchHistoryEntry(content::WebContents* contents) {
  if (!contents) return;
  const GURL& url = contents->GetLastCommittedURL();
  if (!url.is_valid()) return;
  if (!url.SchemeIsHTTPOrHTTPS()) return;  // skip chrome://, about:, etc.
  MahoCore* core = maho::GetCore();
  if (!core) return;
  std::string title = base::UTF16ToUTF8(contents->GetTitle());
  maho_core_add_history_entry(core, url.spec().c_str(), title.c_str());
}
```

**(c) `NavObserver` 클래스 정의** — `StripObserver` 클래스 위:

```cpp
class MahoTabRegistry::NavObserver : public content::WebContentsObserver {
 public:
  explicit NavObserver(content::WebContents* contents)
      : content::WebContentsObserver(contents) {}

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle) return;
    if (!navigation_handle->IsInPrimaryMainFrame()) return;
    if (!navigation_handle->HasCommitted()) return;
    if (navigation_handle->IsErrorPage()) return;
    if (navigation_handle->IsSameDocument()) return;
    DispatchHistoryEntry(web_contents());
  }

  void TitleWasSet(content::NavigationEntry* /*entry*/) override {
    // Title arrives async after navigation commits; refresh the entry so
    // history search can match by title too. add_history_entry is dedup-safe
    // for the same URL because Rust history is append-only with cap.
    DispatchHistoryEntry(web_contents());
  }
};
```

**(d) `StripObserver` 에 NavObserver 관리 추가**:

```cpp
class MahoTabRegistry::StripObserver : public TabStripModelObserver {
 public:
  StripObserver(Browser* browser, TabStripModel* model)
      : browser_(browser), model_(model) {
    DCHECK(browser_);
    DCHECK(model_);
    model_->AddObserver(this);
    Bootstrap();
  }

  ~StripObserver() override {
    if (model_) {
      model_->RemoveObserver(this);
    }
    nav_observers_.clear();   // ← NEW
  }

  void OnTabStripModelChanged(...) override {
    if (change.type() == TabStripModelChange::kInserted) {
      const auto* insert = change.GetInsert();
      if (!insert) return;
      for (const auto& inserted : insert->contents) {
        DispatchCreateTabForContents(browser_, inserted.contents);
        AttachNavObserver(inserted.contents);   // ← NEW
      }
    } else if (change.type() == TabStripModelChange::kRemoved) {
      const auto* remove = change.GetRemove();
      if (!remove) return;
      for (const auto& removed : remove->contents) {
        nav_observers_.erase(removed.contents);  // ← NEW (always, regardless of reason)
        if (!TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) {
          continue;
        }
        DispatchCloseTabForContents(removed.contents);
      }
    }
  }

 private:
  void Bootstrap() {
    for (int i = 0; i < model_->count(); ++i) {
      content::WebContents* contents = model_->GetWebContentsAt(i);
      DispatchCreateTabForContents(browser_, contents);
      AttachNavObserver(contents);   // ← NEW
    }
  }

  void AttachNavObserver(content::WebContents* contents) {
    if (!contents) return;
    if (nav_observers_.contains(contents)) return;
    nav_observers_.emplace(contents, std::make_unique<NavObserver>(contents));
  }

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> model_;
  base::flat_map<content::WebContents*,
                 std::unique_ptr<NavObserver>> nav_observers_;   // ← NEW
};
```

#### 3. `maho-chromium/browser/BUILD.gn`

`maho_tab_registry` source_set deps 에 추가 (이미 있는 것 외):

```gn
"//content/public/browser",  # navigation_handle.h, web_contents_observer.h
```

(이미 있을 가능성 높음 — 확인 필요.)

### 검증

1. `python3 maho-chromium/build/scripts/build_maho.py`
2. 사용자 검증:
   - 새 탭에서 google.com → youtube.com → github.com 등 여러 사이트 방문
   - Cmd+T → "git" 입력 → "github.com" 류 History row 가 등장해야 함
   - 일부러 incognito 윈도우에서 사이트 방문 → Cmd+T 에서 그 URL 이 history 에 안 떠야 함 (registry 가 off-the-record browser 이미 제외)

### 위험

- `WebContentsObserver` 가 `RenderFrameHost` lifecycle 와 함께 dyld됨 — `web_contents()` nullptr check 자동 (base class 에서 처리).
- Tab discard / restore 시 WebContents 가 바뀔 수 있음 → `OnTabStripModelChanged` 의 `kReplaced` 도 처리해야 완벽. 1차 구현은 Inserted/Removed 만, 추후 보완.
- `TitleWasSet` 도 hook 하는 이유: 첫 navigation 시점에 title 이 비어있을 수 있어서 (e.g. SPA 라우팅). title 들어온 뒤 다시 dispatch 해서 history 매치 정확도를 높임. add_history_entry 는 같은 URL 이라도 새 entry 로 추가되지만 `MAX_HISTORY_ENTRIES` cap 으로 오래된 것 자동 삭제.
- 아주 큰 사이트 (예: 1초에 navigation 여러 번) 에서 history flood 가능 → 실측 후 debounce 추가 검토.

---

## 통합 빌드 / 검증 순서

1. **Issue B 만 먼저 구현 → build → smoke launch → 사용자 검증** (kTab switch 가 실제 작동하는지)
2. **Issue A 구현 → build → smoke launch → 사용자 검증** (Search row 가 보이고 동작)
3. **Issue C 구현 → build → smoke launch → 사용자 검증** (history 페이지 방문 후 검색 시 노출)

세 개를 한꺼번에 빌드해도 되지만 단계별로 검증하면 회귀 발생 시 원인 격리가 쉬움.

## 롤백 전략

세 변경 모두 작은 외과적 수정이라 git revert 단위로 깨끗하게 분리 가능:
- Issue B: `maho_command_action_handler.cc` 만 수정
- Issue A: 6개 파일 (Rust 2, C++ 4)
- Issue C: 1-2개 파일 (`maho_tab_registry.{h,cc}`)

각각 별도 commit 으로 만들면 문제 시 개별 revert 가능.

## 비-목표 (스코프 외)

- search engine 선택 UI (default engine 외에 @prefix 로 다른 engine 호출은 이미 동작)
- history 검색 ranking 조정 (Rust trigram index 가 이미 처리; 별도 task)
- bookmark / closed tab / archived tab row 의 활성화 동작 (Issue B 의 kTab fix 와 별개; 필요시 후속)

---

**End of plan.**
