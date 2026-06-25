# Zen Parity Drag Image Controller — v2

**Date:** 2026-06-07
**Owner:** Sisyphus
**Status:** DRAFT → IMPLEMENTATION LANDED 2026-06-07.
Gap closure tracked in: sidebar-scope-creep-followups-and-retroactive-2026-06-07.md (Phase A-COMPLETE).
**Supersedes:** `zen-mid-drag-image-swap-cross-platform-2026-06-07.md` (v1 — Zen parity 부족했음, archive only)
**Goal:** Zen Browser 의 tab + essentials drag UX 와 100% UI parity 를 Maho 에 구현.

---

## 0. 검증된 Zen 동작 명세 (line cite 포함)

이 plan 의 모든 design 결정은 다음 Zen 코드 검증에 근거한다 (`zen-browser/desktop@stable`).

### 0.1 Drag start (`src/zen/drag-and-drop/ZenDragAndDrop.js`)
- **L138-159 `startTabDrag`**: `super.startTabDrag(event, tab, ...args)` 로 Firefox native HTML5 D&D 시작. 자체 wrapper 만들어 `dt.setDragImage(wrapper, offsetX, offsetY)` 설정.
- **L154 `originalDragImageArgs = [dragImage, offsetX, offsetY]`**: 후속 `updateDragImage` 호출용 보관.
- **L156-158**: `if (tab.hasAttribute("zen-essential")) tab.style.visibility = "hidden"` — *essentials drag 시에만* 원래 자리 hide.
- **L139 `this.ZenDragAndDropService.onDragStart(1.0)`**: drag image opacity = 1.0 (GTK default 0.5 override).

### 0.2 Wrapper 생성 `#createDragImageForTabs` (L161-200)
- 1-3 tabs clone (multi-select 시 stack).
- 위치: `position: fixed; top: -9999px;` `periphery` (`#tabbrowser-arrowscrollbox-periphery`) 안. OS 가 image 로 capture 후 DOM 에 노출 안 됨.
- i>0 clone: `transform: translate(${i*4}px, -${i*(tabRect.height-4)}px); opacity: 0.2; zIndex: -i`.
- macOS 외 platform 에서 `colorScheme: "light"` + `color: black` (가시성 hack — `nsZenDragAndDrop::mDragImageOpacity` 와 별개).

### 0.3 Single dispatcher `_animateTabMove(event)` (L246-261)
**핵심 architecture** — 매 dragover 마다 호출되어 zone 상태 관리 + image swap 결정의 *single source of truth*:

```javascript
_animateTabMove(event) {
  let draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
  if (
    event.target.closest("#zen-essentials") &&
    !isEssentialsPromo(event.target)
  ) {
    if (!isTab(draggedTab)) { this.clearDragOverVisuals(); return; }
    return this.#animateVerticalPinnedGridDragOver(event);  // → essentials swap
  } else if (this._fakeEssentialTab) {
    this.#makeDragImageNonEssential(event);                 // → revert
  }
  // ...일반 tab move animation...
}
```

### 0.4 `#makeDragImageEssential` (L1765-1794) — Zone-entry swap
```javascript
#makeDragImageEssential(event) {
  const dt = event.dataTransfer;
  const draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
  if (draggedTab.hasAttribute("zen-essential")) return;       // ⓐ essentials→essentials skip
  const dragData = draggedTab._dragData;
  const [wrapper] = this.originalDragImageArgs;
  const tab = wrapper.firstElementChild;
  tab.setAttribute("zen-essential", "true");
  tab.setAttribute("pinned", "true");
  tab.setAttribute("selected", "true");
  const draggedTabRect = window.windowUtils.getBoundsWithoutFlushing(this._fakeEssentialTab);
  tab.style.minWidth = tab.style.maxWidth = wrapper.style.width = draggedTabRect.width + "px";   // ⓑ dynamic cell dim
  tab.style.minHeight = tab.style.maxHeight = wrapper.style.height = draggedTabRect.height + "px";
  const offsetY = dragData.offsetY;
  const offsetX = dragData.offsetX;
  tab.style.transform = `translate(${(54 - offsetX) / 2}px, ${(50 - offsetY) / 2}px)`;
  gZenPinnedTabManager.setEssentialTabIcon(tab);              // ⓒ favicon set via CSS var
  dt.updateDragImage(wrapper, -16, -16);
}
```

### 0.5 `#makeDragImageNonEssential` (L1796-1819) — Zone-exit revert (50ms delay)
```javascript
#makeDragImageNonEssential(event) {
  const dt = event.dataTransfer;
  const draggedTab = event.dataTransfer.mozGetDataAt(TAB_DROP_TYPE, 0);
  if (draggedTab.hasAttribute("zen-essential")) return;
  const wrapper = this.originalDragImageArgs[0];
  const tab = wrapper.firstElementChild;
  tab.style.setProperty("transition", "none", "important");
  tab.removeAttribute("zen-essential");
  tab.removeAttribute("pinned");
  tab.style.minWidth = tab.style.maxWidth = "";
  tab.style.minHeight = tab.style.maxHeight = "";
  tab.style.transform = "";
  const rect = window.windowUtils.getBoundsWithoutFlushing(draggedTab);
  wrapper.style.width = rect.width + "px";
  wrapper.style.height = rect.height + "px";
  setTimeout(() => {                                          // ⓓ 50ms delay
    tab.style.transition = "";
    dt.updateDragImage(...this.originalDragImageArgs);
  }, 50);
}
```

### 0.6 Cross-window leave `handle_windowDragLeave` (L880-941)
```javascript
let allowedMargin = Services.prefs.getIntPref("zen.tabs.dnd-outside-window-margin", 5);
const isOutOfWindow =
    clientX <= allowedMargin || clientX >= winWidth - allowedMargin ||
    clientY <= allowedMargin || clientY >= winHeight - allowedMargin;
if (isOutOfWindow && !this.#isOutOfWindow) {
  this.#isOutOfWindow = true;
  // ... cleanup ...
  if (!this._browserDragImageWrapper) {
    const wrappingDiv = document.createXULElement("vbox");
    canvas.style.borderRadius = "8px";
    canvas.style.border = "2px solid white";
    wrappingDiv.style.width = 200 + "px";                    // ⓔ 200x130 canvas
    wrappingDiv.style.height = 130 + "px";
    wrappingDiv.style.position = "relative";
    this.#maybeCreateDragImageDot(movingTabs, wrappingDiv);
    wrappingDiv.appendChild(canvas);
    this._browserDragImageWrapper = wrappingDiv;
    document.documentElement.appendChild(wrappingDiv);
  }
  dt.updateDragImage(this._browserDragImageWrapper,
    this.originalDragImageArgs[1], this.originalDragImageArgs[2]);
}
```

### 0.7 Cross-window re-enter `handle_windowDragEnter` (L880-888)
```javascript
handle_windowDragEnter(event) {
  if (!this.#isMovingTab() || !this.#isOutOfWindow) return;
  this.#isOutOfWindow = false;
  const dt = event.dataTransfer;
  dt.updateDragImage(...this.originalDragImageArgs);          // ⓕ 원 image 복원
}
```

### 0.8 Space switch refresh (L692-708)
```javascript
let tabs = this.originalDragImageArgs[0].children;
const { isDarkMode, isExplicitMode } = gZenThemePicker.getGradientForWorkspace(spaceChanged, ...);
for (let tab of tabs) {
  if (isExplicitMode) tab.style.colorScheme = isDarkMode ? "dark" : "light";
  else tab.style.colorScheme = "";
}
requestAnimationFrame(() => {
  requestAnimationFrame(() => {
    dt.updateDragImage(...this.originalDragImageArgs);        // ⓖ 2-frame defer
  });
});
```

### 0.9 Drop reset (L1180-1192)
```javascript
thisFromGlobal.originalDragImageArgs = [];                   // ⓗ 모든 state clear
thisFromGlobal._fakeEssentialTab = null;
if (thisFromGlobal._browserDragImageWrapper) {
  thisFromGlobal._browserDragImageWrapper.remove();
  thisFromGlobal._browserDragImageWrapper = null;
}
```

### 0.10 Favicon source `setEssentialTabIcon` (`src/zen/tabs/ZenPinnedTabManager.mjs:112-115`)
```javascript
setEssentialTabIcon(tab, url = null) {
  const iconUrl = url ?? tab.getAttribute("image") ?? "";
  tab.style.setProperty("--zen-essential-tab-icon", `url(${iconUrl})`);
}
```
즉 `tab.getAttribute("image")` (favicon URL) 을 CSS variable 로 설정. essential tab 의 CSS 가 그걸 background-image 로 사용.

### 0.11 GTK opacity (`src/zen/drag-and-drop/nsZenDragAndDrop.cpp:11-21`)
```cpp
static constexpr auto kZenDefaultDragImageOpacity =
#if defined(MOZ_WIDGET_GTK)
    0.5f;  // matches DRAG_IMAGE_ALPHA_LEVEL in nsDragService.cpp
#else
    DRAG_TRANSLUCENCY;
#endif
```
즉 Linux GTK 만 0.5, mac/win 은 native default (1.0 근처).

---

## 1. Architecture Overview

### 1.1 두 layer 로 구성

**Layer A — Chromium fork patch (cross-platform native API)**
공유 mid-drag image swap API 를 추가. `Widget::UpdateDragImage(image, offset)` 호출 시 OS-native API 로 image 갱신.

**Layer B — Maho `SidebarDragImageController` (single dispatcher)**
Zen 의 `_animateTabMove` 와 동등한 single source of truth. State machine 으로 zone tracking + image swap timing 관리. Maho-side only, Chromium fork patch 와 분리.

### 1.2 Zone state machine

```
                 ┌─────────────┐
                 │   Initial   │ (drag start, image=tab_stack)
                 └──────┬──────┘
                        ↓
                 ┌─────────────┐
            ┌──→ │ InTabZone   │ (default sidebar area)
            │    └──┬───────┬──┘
   50ms delay│      │       │
            │      ↓       ↓
   ┌────────┴────────┐  ┌─────────────────┐
   │ InEssentialsZone│  │ CrossWindowOut  │ (cursor outside widget bounds + 5px margin)
   │ image=essentials│  │ image=200x130   │
   └────────┬────────┘  └────────┬────────┘
            │                    │
            └────── (drop) ──────┘
                        ↓
                 (controller cleared, source tile restored)
```

### 1.3 매 OnDragUpdated 호출에서 controller 가 결정

- Zen 의 `_animateTabMove` ≡ Maho 의 `SidebarDragImageController::OnDragUpdated` (controller 자체 함수, view 의 OnDragUpdated 와 다름)
- `MahoSidebarFavoritesGridView::OnDragUpdated`, `MahoSidebarTabListView::OnDragUpdated`, `MahoSidebarView::OnDragUpdated` 모두에서 controller 의 single 함수를 호출. controller 가 hit view + screen point 으로 zone 결정.

---

## 2. Layer A — Chromium Fork Patch

### 2.1 Public API

#### `ui/aura/client/drag_drop_client.h`
```cpp
class DragDropClient {
  // ... existing ...
  // Mid-drag image replacement. No-op if no drag active.
  virtual void UpdateDragImage(const gfx::ImageSkia& image,
                               const gfx::Vector2d& cursor_offset) = 0;
};
```

#### `ui/views/widget/widget.h/cc`
```cpp
class Widget {
  // Convenience wrapper.
  void UpdateDragImage(const gfx::ImageSkia& image,
                       const gfx::Vector2d& cursor_offset);
  // Query for controllers.
  bool HasActiveDrag() const;
};
```

#### Build flag
`build/config/features.gni`:
```
declare_args() { enable_maho_drag_update_image = true }
```
Patch 코드는 `BUILDFLAG(ENABLE_MAHO_DRAG_UPDATE_IMAGE)` gate.

### 2.2 macOS — `ui/views/cocoa/drag_drop_client_mac.{h,mm}`
- StartDragAndDrop 시 `[NSView beginDraggingSessionWithItems:...]` 반환값을 weak ref 로 저장.
- UpdateDragImage:
```objc
[draggingSession_ enumerateDraggingItemsWithOptions:NSDraggingItemEnumerationConcurrent
                                              forView:source_view_
                                              classes:@[NSPasteboardItem.class]
                                        searchOptions:@{}
                                          usingBlock:^(NSDraggingItem* item, NSInteger idx, BOOL* stop) {
  NSImage* nsImage = skia::SkBitmapToNSImage(*image.bitmap());
  [item setDraggingFrame:NSMakeRect(offset.x(), offset.y(),
                                     nsImage.size.width, nsImage.size.height)
                contents:nsImage];
}];
[draggingSession_ setDraggingFormation:NSDraggingFormationDefault];  // redraw 강제
```
- EndDrag 에서 `draggingSession_ = nil`.

### 2.3 Windows — `ui/aura/client/drag_drop_client_win.{h,cc}`
- Drag 시작 시 `IDataObject*` 보관.
- UpdateDragImage:
```cpp
Microsoft::WRL::ComPtr<IDragSourceHelper2> helper;
::CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                   IID_PPV_ARGS(&helper));
SHDRAGIMAGE shdi = {};
shdi.sizeDragImage.cx = image.width();
shdi.sizeDragImage.cy = image.height();
shdi.ptOffset.x = offset.x();
shdi.ptOffset.y = offset.y();
shdi.hbmpDragImage = skia::CreateHBITMAPFromSkBitmap(*image.bitmap());
shdi.crColorKey = CLR_NONE;
helper->InitializeFromBitmap(&shdi, data_object_.Get());
```
- 1px cursor jiggle workaround for stale Shell cache.

### 2.4 Linux X11 — `ui/base/x/x11_drag_drop_client.{h,cc}` + `ui/base/x/x11_drag_widget.{h,cc}`
- X11DragWidget 에 `UpdateImage(SkBitmap)` 메서드 추가.
- 호출 시 `XPutImage` 또는 layer 재paint + size 변경 시 widget resize.

### 2.5 Linux Wayland — `ui/ozone/platform/wayland/host/wayland_data_drag_controller.{h,cc}`
- icon_surface 를 controller 가 보관.
- UpdateDragImage:
```cpp
auto buffer = CreateShmBuffer(*image.bitmap());
wl_surface_attach(icon_surface_, buffer.get(), offset.x(), offset.y());
wl_surface_damage_buffer(icon_surface_, 0, 0, image.width(), image.height());
wl_surface_commit(icon_surface_);
```
- buffer-release 이벤트 처리 (이전 buffer 가 release 된 후 새 buffer attach).

### 2.6 Linux GTK opacity (Zen 0.11 매칭)
- Zen `nsZenDragAndDrop::mDragImageOpacity` 와 동등 — Maho 는 image 를 build 할 때 alpha multiply 로 처리.
- `BuildTabStackDragImage` 가 `is_linux_gtk ? 0.5f : 1.0f` opacity 인자.

---

## 3. Layer B — Maho Side

### 3.1 New class: `MahoSidebarDragImageController`

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_controller.{h,cc}`

```cpp
namespace maho {

class MahoSidebarDragImageController {
 public:
  enum class Zone {
    kInitial,
    kInTabZone,
    kInEssentialsZone,
    kCrossWindowOut,
  };

  struct DragSource {
    std::string tab_id;
    bool is_essential = false;
    gfx::ImageSkia favicon;
    SkColor theme_accent = SK_ColorTRANSPARENT;
    std::vector<TabSnapshot> stack_tabs;  // 1-3 for multi-select
    gfx::Vector2d original_offset;
  };

  explicit MahoSidebarDragImageController(views::Widget* widget);
  ~MahoSidebarDragImageController();

  // Zen `startTabDrag` equivalent — drag 시작 시 한 번 호출.
  void OnDragStart(const DragSource& source);

  // Zen `_animateTabMove` equivalent — 매 dragover 호출.
  // hit_view: drop target hit-test 결과
  // screen_point: cursor screen 좌표
  void OnDragUpdated(const gfx::Point& screen_point, views::View* hit_view);

  // Zen drop handler equivalent — 모든 state clear.
  void OnDragEnded();

  // Zen space switch refresh equivalent.
  void OnSpaceSwitched();

 private:
  Zone DetectZone(const gfx::Point& screen_point, views::View* hit_view) const;
  void TransitionTo(Zone new_zone);
  void SwapImageForCurrentZone();
  void ScheduleRevertToTabImage();  // 50ms delayed

  raw_ptr<views::Widget> widget_;
  Zone current_zone_ = Zone::kInitial;
  std::optional<DragSource> source_;
  base::OneShotTimer revert_timer_;

  // Cached images (rebuilt on space switch / placeholder size change)
  gfx::ImageSkia tab_stack_image_;
  gfx::ImageSkia essentials_image_;
  gfx::ImageSkia cross_window_image_;

  base::WeakPtrFactory<MahoSidebarDragImageController> weak_factory_{this};
};

}  // namespace maho
```

### 3.2 Image factory

`maho_sidebar_drag_image_factory.{h,cc}`:

```cpp
namespace maho {

// Zen `#createDragImageForTabs` equivalent. 1-3 tab clones with stack offset.
gfx::ImageSkia BuildTabStackDragImage(
    const std::vector<TabSnapshot>& tabs,    // up to 3
    const gfx::Size& tab_rect,
    bool is_dark_mode,
    bool is_linux_gtk = false);              // GTK 0.5 opacity

// Zen essentials swap equivalent. cell_size 는 동적 (Zen `_fakeEssentialTab.getBoundingClientRect()`)
gfx::ImageSkia BuildEssentialsDragImage(
    const gfx::ImageSkia& favicon,
    SkColor theme_accent,
    const gfx::Size& cell_size,             // dynamic from placeholder cell
    const gfx::Vector2d& center_offset);    // Zen 의 (54-offsetX)/2, (50-offsetY)/2 매핑

// Zen `_browserDragImageWrapper` equivalent.
gfx::ImageSkia BuildCrossWindowDragImage(
    const gfx::ImageSkia& page_thumbnail,    // canvas equivalent
    SkColor border_color,                   // white default
    int width = 200,                        // Zen hardcode
    int height = 130,
    int border_radius = 8);

}  // namespace maho
```

### 3.3 State machine transitions

| From | To | Action | Zen reference |
|---|---|---|---|
| Initial | InTabZone | (drag start image 이미 set) | L154 |
| Initial | InEssentialsZone | `if !source.is_essential` swap to essentials_image; if essential, skip (Zen ⓐ) | L1768-1770 |
| InTabZone | InEssentialsZone | swap to essentials_image (즉시) | L1493-1501 |
| InEssentialsZone | InTabZone | `revert_timer_.Start(50ms, RevertToTabImage)` (Zen ⓓ) | L1815-1818 |
| InTabZone | CrossWindowOut | swap to cross_window_image (200x130) (Zen ⓔ) | L920-940 |
| CrossWindowOut | InTabZone | swap to tab_stack_image (Zen ⓕ) | L887 |
| CrossWindowOut | InEssentialsZone | swap to essentials_image | (combined) |
| Any | (drop / cancel) | OnDragEnded clear all (Zen ⓗ) | L1180-1192 |

### 3.4 `DetectZone` 알고리즘

```cpp
Zone DetectZone(screen_point, hit_view) const {
  // Cross-window margin (Zen pref 5px)
  const int kCrossWindowMargin = 5;
  gfx::Rect widget_screen_bounds = widget_->GetWindowBoundsInScreen();
  widget_screen_bounds.Inset(kCrossWindowMargin);
  if (!widget_screen_bounds.Contains(screen_point)) {
    return Zone::kCrossWindowOut;
  }

  // Essentials zone hit-test
  for (auto* v = hit_view; v != nullptr; v = v->parent()) {
    if (v == favorites_grid_view_) return Zone::kInEssentialsZone;
  }

  return Zone::kInTabZone;
}
```

### 3.5 50ms delay transition (Zen ⓓ 매핑)

```cpp
void TransitionTo(Zone new_zone) {
  if (new_zone == current_zone_) return;
  if (current_zone_ == Zone::kInEssentialsZone &&
      new_zone == Zone::kInTabZone) {
    // Zen `#makeDragImageNonEssential` 의 setTimeout 50ms 매핑
    revert_timer_.Start(FROM_HERE, base::Milliseconds(50),
        base::BindOnce(&MahoSidebarDragImageController::SwapImageForCurrentZone,
                       weak_factory_.GetWeakPtr()));
    current_zone_ = new_zone;
    return;
  }
  revert_timer_.Stop();  // 다른 transition 은 즉시
  current_zone_ = new_zone;
  SwapImageForCurrentZone();
}
```

### 3.6 Drop reset (Zen ⓗ 매핑)

```cpp
void OnDragEnded() {
  revert_timer_.Stop();
  if (source_ && source_->is_essential) {
    // SidebarTabListView 가 source tile->SetVisible(true) 호출
    // (controller 는 widget 만 보유, source 복원은 view 가 직접)
  }
  source_.reset();
  current_zone_ = Zone::kInitial;
  tab_stack_image_ = essentials_image_ = cross_window_image_ = gfx::ImageSkia();
}
```

### 3.7 Space switch refresh (Zen ⓖ 매핑)

```cpp
void OnSpaceSwitched() {
  if (!source_) return;
  // 2-frame defer 매핑 (Zen requestAnimationFrame nested)
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoSidebarDragImageController::RebuildAndSwap,
                     weak_factory_.GetWeakPtr()));
}
void RebuildAndSwap() {
  // theme color update from new space, rebuild current zone's image
  RebuildImagesForCurrentTheme();
  SwapImageForCurrentZone();
}
```

---

## 4. Maho Integration Points

### 4.1 Drag source — `MahoSidebarTabListView::WriteDragDataForView`

```cpp
void WriteDragDataForView(View* sender, const gfx::Point& press_pt,
                          OSExchangeData* data) override {
  // 1. Build tab stack image (Zen #createDragImageForTabs)
  std::vector<TabSnapshot> snapshots = SnapshotSelectedTabs();  // 1-3
  gfx::Size tab_rect = sender->size();
  bool dark = IsDarkMode();
  bool gtk = IsLinuxGtkBuild();
  auto image = maho::BuildTabStackDragImage(snapshots, tab_rect, dark, gtk);

  // 2. Compute drag offset
  gfx::Vector2d offset = ComputeDragOffset(press_pt, tab_rect);

  // 3. Set OS drag image (initial)
  data->provider().SetDragImage(image, offset);

  // 4. Notify controller
  drag_image_controller_->OnDragStart({
    .tab_id = tab_id,
    .is_essential = sender->IsEssentialTile(),
    .favicon = ResolveFaviconForTabId(tab_id),
    .theme_accent = current_space_theme_color_,
    .stack_tabs = snapshots,
    .original_offset = offset,
  });

  // 5. Zen L156-158: essentials drag 시 source tile hide
  if (sender->IsEssentialTile()) {
    sender->SetVisible(false);
  }
}
```

### 4.2 Drop targets — controller dispatch

각 drop target view 의 `OnDragUpdated` 끝부분에 controller 호출:

```cpp
int MahoSidebarFavoritesGridView::OnDragUpdated(
    const ui::DropTargetEvent& event) {
  // ... 기존 placeholder/reorder logic ...
  gfx::Point screen_pt = event.location();
  ConvertPointToScreen(this, &screen_pt);
  drag_image_controller_->OnDragUpdated(screen_pt, this);
  return drop_op;
}
```

`MahoSidebarTabListView::OnDragUpdated`, `MahoSidebarView::OnDragUpdated` 도 동일 패턴.

`MahoSidebarView` 의 OnDragUpdated 는 cursor 가 widget 안 모든 영역에서 호출되므로 cross-window margin detection 에 적합.

### 4.3 Drop / cancel

```cpp
void OnPerformDrop(...) {
  // ... 기존 reorder ...
  drag_image_controller_->OnDragEnded();
}

// MahoSidebarTabListView 의 source 가 OnDragDone 받으면:
void OnDragDone() {
  if (source_was_essential_) source_view_->SetVisible(true);  // Zen 복원
  drag_image_controller_->OnDragEnded();
}
```

### 4.4 Space switch

```cpp
class MahoSpaceManager : public views::ViewObserver, ... {
  void OnSpaceSwitched(int new_space_id) {
    drag_image_controller_->OnSpaceSwitched();
    // ...
  }
};
```

### 4.5 Cell size dynamic (Zen ⓑ 매핑)

`MahoSidebarFavoritesGridView::placeholder_cell_size()` 추가:

```cpp
gfx::Size MahoSidebarFavoritesGridView::PlaceholderCellSize() const {
  if (!placeholder_active_) return gfx::Size();
  // grid layout 의 N+1 번째 cell 의 dimension
  return TileSize();  // 기존 TileSize 헬퍼 사용
}
```

Controller 의 `BuildEssentialsDragImage(...)` 는 이 size 를 인자로 받음. zone 진입 시 / cell size 변경 시 image rebuild.

---

## 5. Phasing (재추산)

| Phase | Deliverable | Time |
|---|---|---|
| 1 | Public API + BUILDFLAG (Layer A common) | 1d |
| 2 | macOS native impl (NSDraggingSession) | 4d |
| 3 | Maho image factory (TabStack/Essentials/CrossWindow) | 2d |
| 4 | `MahoSidebarDragImageController` 구현 + state machine | 3d |
| 5 | Maho integration (TabListView + FavoritesGridView + SidebarView) on macOS | 3d |
| 6 | macOS visual QA, Zen 동영상 side-by-side 비교 | 1d |
| 7 | Windows native impl (IDragSourceHelper2) + integration | 4d |
| 8 | Linux X11 impl (X11DragWidget) + integration | 4d |
| 9 | Linux Wayland impl (icon_surface) + integration | 3d |
| 10 | Linux GTK opacity 0.5 (image factory 분기) | 1d |
| 11 | Cross-window state + 200x130 canvas image + margin pref | 2d |
| 12 | Space switch image refresh + 2-frame defer | 1d |
| 13 | Drop reset + source tile visibility 복원 | 1d |
| 14 | Multi-tab stack visual fine-tune (translate offset, opacity, zIndex) | 1d |
| 15 | Tests (unit + interactive) — all platforms | 5d |
| 16 | momus plan-vs-impl review + bug bash | 2d |

**Total: 38일** (1 dev sequential).

Phases 7-10 (Windows/Linux) 은 macOS 검증 후 독립 트랙. CI matrix 가 있으면 6 → (7,8,9,10) 병렬 가능 → 약 30일.

---

## 6. Files (전체 목록)

### Layer A — Chromium fork
```
ui/aura/client/drag_drop_client.h                                     [+ virtual UpdateDragImage]
ui/views/widget/widget.h, widget.cc                                   [+ UpdateDragImage, HasActiveDrag]
ui/views/cocoa/drag_drop_client_mac.h, drag_drop_client_mac.mm        [+ macOS impl]
ui/aura/client/drag_drop_client_win.h, drag_drop_client_win.cc        [+ Win impl]
ui/base/dragdrop/os_exchange_data_provider_win.h, .cc                 [+ SHDRAGIMAGE re-init]
ui/base/x/x11_drag_drop_client.h, .cc                                 [+ X11 impl]
ui/base/x/x11_drag_widget.h, .cc                                      [+ UpdateImage]
ui/ozone/platform/wayland/host/wayland_data_drag_controller.h, .cc    [+ Wayland impl]
ui/ozone/platform/wayland/host/wayland_data_source.h, .cc             [+ surface attach/commit]
build/config/features.gni                                             [+ enable_maho_drag_update_image]
```

### Layer B — Maho overlay
```
maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_controller.h, .cc   [NEW]
maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_factory.h, .cc      [NEW]
maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc               [drag source swap]
maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h, .cc     [OnDragUpdated dispatch + PlaceholderCellSize]
maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc                        [OnDragUpdated dispatch]
maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_util.h, .cc               [eventual deprecation]
maho-chromium/browser/ui/views/sidebar/maho_space_manager.cc                       [+ OnSpaceSwitched hook]
maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc       [+ tests]
```

---

## 7. Tests

### Unit tests (`maho_sidebar_dnd_dispatch_unittest.cc`)
| Test name | What it verifies |
|---|---|
| `ControllerStartsInInitial` | OnDragStart 후 zone == kInitial |
| `ZoneTransitionTabToEssentialsInstant` | InTabZone → InEssentialsZone 즉시 image swap |
| `ZoneTransitionEssentialsToTab50msDelay` | 50ms timer fire 후 image revert (Zen ⓓ) |
| `EssentialsDragSkipsSwap` | source.is_essential 시 swap 발생 안 함 (Zen ⓐ) |
| `CrossWindowMarginDetection` | screen_point 가 widget bounds + 5px 바깥 → CrossWindowOut |
| `CrossWindowReentryRevert` | CrossWindowOut → InTabZone 시 즉시 tab_stack_image 복원 (Zen ⓕ) |
| `SpaceSwitchRefreshesImage` | OnSpaceSwitched 호출 후 current_zone 의 image rebuild |
| `OnDragEndedClearsAllState` | source_ reset, timer stop, images cleared |
| `EssentialsTileVisibilityRestoredOnDrop` | OnDragDone 시 source view->SetVisible(true) |
| `PlaceholderCellSizeChangeRebuildsEssentialsImage` | Cell dim 바뀌면 essentials_image rebuild (Zen ⓑ) |
| `DropDuring50msTimerCancelsRevert` | Drop 시 timer 가 fire 되기 전 stop |

### Interactive tests
- `BrowserInteractiveTest::ZenParityFavoritesEntryExitFlow` — 실제 cursor 이동 + image swap visual capture
- `BrowserInteractiveTest::ZenParityCrossWindowDragSwapsThumb` — 두 window 사이 drag

### Visual regression
- macOS, Win, Linux X11, Linux Wayland 각각 Zen browser 와 동시 실행 후 동영상 캡쳐 → side-by-side diff. `docs/zen-parity-vqa/` 에 reference videos.

---

## 8. Risks & Mitigations (확장)

| Risk | Severity | Mitigation |
|---|---|---|
| macOS NSDraggingSession enumerate 후 1 frame redraw lag | Medium | `setDraggingFormation:NSDraggingFormationDefault` 강제 호출 + UMA timing |
| Windows IDropTargetHelper image cache | Medium | 1px cursor jiggle workaround |
| X11 WM 호환성 (i3, dwm) | Low | Test matrix: gnome, kde, xfce, i3, dwm. Fallback: skip update on offending WMs |
| Wayland compositor variance (mutter, kwin, sway) | Low | 동일 fallback. Buffer-release 처리 필수 |
| Chromium upstream rebase 부담 | High | BUILDFLAG gate + per-file self-contained patch |
| Test infra OS-level drag mock | High | Mock `DragDropClient::UpdateDragImage` 호출 검증 |
| 50ms timer 가 drop 전 fire → image flicker | Medium | OnDragEnded 가 timer.Stop() 보장 |
| Space switch 중 drag active → colorScheme race | Low | 2-frame defer (Zen ⓖ) 모방 |
| Cross-window 200x130 dimension 모든 platform 통일 | Low | hardcode (Zen 동일) |
| Cell size dynamic rebuild 비용 | Low | image cache + size 변경 시만 rebuild |
| Linux GTK opacity 0.5 — alpha multiply 시 dark mode 가시성 손상 | Medium | Build 시 opacity 검증 visual snapshot |

---

## 9. Acceptance Criteria

### Functional
- [F1] macOS / Win / Linux X11 / Linux Wayland 모두에서 mid-drag image swap 작동.
- [F2] InTabZone ↔ InEssentialsZone 전이 시 visual swap (50ms delay on exit, 즉시 on entry).
- [F3] Essentials drag 시 swap skip.
- [F4] CrossWindowOut zone 진입 시 200x130 canvas image, 복귀 시 원 image.
- [F5] Space switch 시 colorScheme 갱신 후 image refresh.
- [F6] Drop 시 모든 state reset, source tile visibility 복원.
- [F7] Cross-app drag (Finder→sidebar) 는 영향 없음 (외부 drag 의 SetDragImage 만).

### Visual
- [V1] Zen browser 와 동영상 side-by-side 비교 시 frame-perfect 매칭 (±1 frame tolerance).
- [V2] 모든 platform 에서 동일 UX (Linux GTK opacity 0.5 만 platform-specific).

### Tests
- [T1] 모든 unit tests green.
- [T2] Interactive tests on each platform pass.
- [T3] Visual regression diff 0.

---

## 10. Open Questions

1. **macOS `imageComponentsProvider` vs `setDraggingFrame:contents:`** — 둘 다 지원되는데 어느 게 redraw timing 안정적인지 prototype 검증 필요.
2. **Windows `DSH_ALLOWDROPDESCRIPTIONTEXT`** 사용해서 essentials 진입 시 drop description 도 변경할까? Zen 동등 기능 없음 — 안 함.
3. **Wayland `wl_buffer_release`** 처리 — 이전 buffer release 이벤트 받기 전 새 buffer attach 시 protocol error. `WaylandShmBufferPool` 으로 double-buffer 관리.
4. **`MahoSpaceManager::OnSpaceSwitched`** hook 위치 — 기존 codebase 에 hook 이 이미 있는지, 신설 필요한지 explore 단계 필요.
5. **Multi-window drag vs cross-window-out** — Zen 의 `_browserDragImageWrapper` 는 window 떠난 후 재진입 시 다시 원래 image. 두 sidebar 사이 drag 는 어떻게 처리? Zen 도 동일 Wrapper 유지.
6. **Drag-over background highlight (#zen-dragover-background)** — Zen 의 grid 배경 highlight 는 별도 visual element. 본 plan 범위 외 (별도 phase). 우선 image swap 만 매칭, background highlight 는 phase 17 으로 연기.
7. **`setEssentialTabIcon` CSS 변수 vs ImageSkia composite** — Zen 은 CSS background-image, 우리는 ImageSkia 합성. Visual 결과 동등성 검증 필요.

---

## 11. Phase 0 — Prototype before commit

본 plan 의 macOS 부분을 1주 prototype 으로 먼저 검증:
- Layer A 의 macOS only impl (간이)
- Layer B 의 controller skeleton + InTabZone/InEssentialsZone 만
- macOS 에서 visual QA
- Prototype 결과로 Phase 1-15 의 time/risk 보정

Prototype 후 본 plan 확정 → Phase 1 정식 시작.
