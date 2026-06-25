# Zen-style Mid-Drag Image Swap — Cross-Platform Chromium Patch + Maho Integration

**Date:** 2026-06-07
**Owner:** Sisyphus
**Status:** SUPERSEDED — see `zen-parity-drag-image-controller-v2-2026-06-07.md`. v1 은 Zen parity 검증 부족 (`OnDragEntered` 한 번만 가정, 50ms delay 누락, essentials→essentials skip 누락, dynamic cell size 누락 등 11개 항목). v2 가 line-by-line Zen reference 검증 후 architecture 재설계.
**Scope:** Chromium fork patch (mac/win/linux) + Maho sidebar integration
**Goal:** 100% UI parity with Zen Browser's tab + essentials drag UX inside Maho.

---

## 0. Problem Statement

Zen Browser achieves a distinctive drag UX:
- During a sidebar tab drag, the OS drag image follows the cursor as a small tab clone wrapper.
- The instant the cursor enters the essentials/favorites zone, the OS drag image is *swapped mid-drag* to a favicon-only round shape.
- Leaving the zone restores the original tab thumbnail.
- Cross-window drag swaps to a small canvas thumb when leaving the source window.

Zen does this via Firefox's non-standard `dt.updateDragImage(wrapper, x, y)` API. Quoted from `src/zen/drag-and-drop/ZenDragAndDrop.js`:

```javascript
// stable@ZenDragAndDrop.js:155 (dragstart)
dt.setDragImage(...this.originalDragImageArgs);

// stable@ZenDragAndDrop.js:1793 (essentials zone entry)
dt.updateDragImage(wrapper, -16, -16);

// stable@ZenDragAndDrop.js:1815 (zone exit)
dt.updateDragImage(...this.originalDragImageArgs);

// stable@ZenDragAndDrop.js:887, 934 (cross-window drag)
dt.updateDragImage(this._browserDragImageWrapper, ...);
```

**Chromium does not expose `updateDragImage`-equivalent API**, even though every supported OS supports mid-drag image change at the native layer:
- **macOS**: `NSDraggingSession::enumerateDraggingItemsWithOptions:forView:classes:searchOptions:usingBlock:` — `[NSDraggingItem setDraggingFrame:contents:]` or `imageComponentsProvider`.
- **Windows**: `IDataObject::SetData(CFSTR_DRAGIMAGEBITS, SHDRAGIMAGE*)` re-fired during drag, picked up by Shell's `IDropTargetHelper`.
- **Linux X11**: drag icon window (XdndDragSource self-managed by Chromium) — repaint with new bitmap.
- **Linux Wayland**: `wl_data_source` drag icon `wl_surface` — attach new buffer + commit.

Adding the missing API is a **fork-level Chromium patch**. This plan covers it end-to-end.

---

## 1. Public API Design

### 1.1 `ui/aura/client/drag_drop_client.h`

```cpp
class DragDropClient {
  // ... existing ...

  // Mid-drag image replacement. No-op if no drag active.
  // |image| = new drag image (premultiplied), |cursor_offset| = top-left
  // offset relative to cursor hotspot (matches SetDragImage convention).
  virtual void UpdateDragImage(const gfx::ImageSkia& image,
                               const gfx::Vector2d& cursor_offset) = 0;
};
```

### 1.2 `views/widget/widget.h`

```cpp
class Widget {
  // Convenience wrapper for callers in views land.
  void UpdateDragImage(const gfx::ImageSkia& image,
                       const gfx::Vector2d& cursor_offset);
};
```

Implementation routes to `aura::client::GetDragDropClient(GetNativeWindow()->GetRootWindow())->UpdateDragImage(...)`.

### 1.3 Build flag (gated patch)

`build/config/features.gni`:
```
declare_args() {
  enable_maho_drag_update_image = true
}
```

All patch sites guard on `BUILDFLAG(ENABLE_MAHO_DRAG_UPDATE_IMAGE)` so the fork can be disabled per build for upstream-rebase debugging.

---

## 2. Per-Platform Implementation

### 2.1 macOS — `ui/views/cocoa/drag_drop_client_mac.{h,mm}`

**State to add**:
- Hold `NSDraggingSession*` (strong ref) when `StartDragAndDrop` is invoked. Cleared in `EndDrag`.

**Method**:
```objc
void DragDropClientMac::UpdateDragImage(const gfx::ImageSkia& image,
                                       const gfx::Vector2d& offset) {
  if (!dragging_session_) return;
  NSImage* nsImage = skia::SkBitmapToNSImageWithColorSpace(
      *image.bitmap(), base::apple::GetSRGBColorSpace());
  NSPoint dragPoint = NSMakePoint(offset.x(), offset.y());
  NSSize imgSize = [nsImage size];
  __weak NSView* view = source_view_;
  [dragging_session_ enumerateDraggingItemsWithOptions:NSDraggingItemEnumerationConcurrent
                                                forView:view
                                                classes:@[ NSPasteboardItem.class ]
                                          searchOptions:@{}
                                            usingBlock:^(NSDraggingItem* item, NSInteger idx, BOOL* stop) {
    [item setDraggingFrame:NSMakeRect(dragPoint.x, dragPoint.y,
                                       imgSize.width, imgSize.height)
                  contents:nsImage];
  }];
}
```

**Risk**: 일부 macOS 버전 (Sonoma 14.x 초기) 에서 enumerate 후 cursor redraw 까지 1 frame lag 보고됨. mitigation: `[draggingSession_ setDraggingFormation:NSDraggingFormationDefault]` 다시 set 으로 redraw 강제.

**Source view tracking**: `StartDragAndDrop` 의 `view` 인자 저장. Already passed via `views::View*` / `NSView*`.

### 2.2 Windows — `ui/aura/client/drag_drop_client_win.{h,cc}` + `ui/base/dragdrop/os_exchange_data_provider_win.{h,cc}`

**Approach**: Shell drag image is set via `IDataObject::SetData(CFSTR_DRAGIMAGEBITS, SHDRAGIMAGE*)` during `DoDragDrop`. Shell's `IDropTargetHelper` picks it up. Mid-drag re-set:

```cpp
void DesktopDragDropClientWin::UpdateDragImage(const gfx::ImageSkia& image,
                                               const gfx::Vector2d& offset) {
  if (!data_object_) return;
  Microsoft::WRL::ComPtr<IDragSourceHelper2> helper;
  if (FAILED(::CoCreateInstance(CLSID_DragDropHelper, nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&helper)))) return;
  SHDRAGIMAGE shdi = {};
  shdi.sizeDragImage.cx = image.width();
  shdi.sizeDragImage.cy = image.height();
  shdi.ptOffset.x = offset.x();
  shdi.ptOffset.y = offset.y();
  shdi.hbmpDragImage = ToHBITMAP(image);  // helper from skia/ext
  shdi.crColorKey = CLR_NONE;
  helper->InitializeFromBitmap(&shdi, data_object_.Get());
}
```

**Risk**: Shell caches first-set image. `InitializeFromBitmap` 다시 호출 후 `IDropTarget::DragOver` 다음 cycle 에서 적용. drag 가 stationary 상태면 cursor 가 살짝 움직여야 redraw 트리거.

### 2.3 Linux X11 — `ui/base/x/x11_drag_drop_client.{h,cc}` + `ui/base/x/x11_drag_widget.{h,cc}`

Chromium 자체 cursor-following window (`X11DragWidget` / `XdndDragSource`) 가 drag icon 을 paint. 현재 `SetDragImage` 시 한 번 그리고 끝.

**Approach**:
- `X11DragWidget` 에 `UpdateImage(SkBitmap)` 추가.
- 호출 시 internal SkBitmap replace + `XClearArea` + `XCopyArea` 또는 compositor frame submit.

```cpp
void X11DragWidget::UpdateImage(const SkBitmap& bitmap) {
  current_bitmap_ = bitmap;
  Resize(bitmap.width(), bitmap.height());
  RequestRedraw();
}
```

**Risk**: 일부 X11 WM (i3, dwm) 에서 drag widget override-redirect 외 추가 hint 무시 → image refresh 시 잠깐 깜빡임. mitigation: double-buffered XPixmap 사용.

### 2.4 Linux Wayland (Ozone) — `ui/ozone/platform/wayland/host/wayland_data_source.{h,cc}` + `wayland_data_drag_controller.{h,cc}`

`wl_data_device::start_drag(source, origin, icon_surface, serial)` 의 `icon_surface` 는 client-managed `wl_surface`. attach 된 `wl_buffer` 만 교체하면 됨.

```cpp
void WaylandDataDragController::UpdateDragImage(const gfx::ImageSkia& image,
                                                const gfx::Vector2d& offset) {
  if (!icon_surface_) return;
  auto buffer = CreateShmBuffer(image.bitmap());
  wl_surface_attach(icon_surface_, buffer.get(), offset.x(), offset.y());
  wl_surface_damage_buffer(icon_surface_, 0, 0,
                            image.width(), image.height());
  wl_surface_commit(icon_surface_);
}
```

**Risk**: compositor 별 (mutter, kwin, sway) drag icon repaint timing 다름. Sway 는 즉시, mutter 는 가끔 1 frame 지연. acceptable.

---

## 3. Maho Integration

### 3.1 New helper — `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_factory.{h,cc}`

```cpp
namespace maho {

// Zen-style multi-tab stack for normal drag (1–3 clones with -translate offset).
gfx::ImageSkia BuildTabStackDragImage(
    const std::vector<TabSnapshot>& tabs,
    const gfx::Size& tab_rect,
    bool is_dark_mode);

// Favicon-only round shape for essentials/favorites zone (matches Zen
// `setEssentialTabIcon` + 32x32 round mask).
gfx::ImageSkia BuildEssentialsDragImage(
    const gfx::ImageSkia& favicon,
    SkColor theme_accent_color,
    int diameter_dip = 32);

// Small thumbnail for cross-window drag (matches Zen `_browserDragImageWrapper`
// with 200x130 canvas + 8px radius border).
gfx::ImageSkia BuildCrossWindowDragImage(
    const gfx::ImageSkia& page_thumbnail,
    SkColor border_color);

}  // namespace maho
```

### 3.2 Drag source — `MahoSidebarTabListView::WriteDragDataForView`

Replace current `CreateSidebarDragImage(view_)` (view capture) with:

```cpp
auto image = maho::BuildTabStackDragImage(
    SnapshotSelectedTabs(), tab_rect, IsDarkMode());
data->provider().SetDragImage(image, drag_offset);
```

Store `widget_` for subsequent `UpdateDragImage` calls. Original tab `SetVisible(false)` (matches Zen `style.visibility = "hidden"`).

### 3.3 Zone-entry hooks — `MahoSidebarFavoritesGridView`

```cpp
void OnDragEntered(const ui::DropTargetEvent& event) override {
  // ... existing logic ...
  GetWidget()->UpdateDragImage(
      maho::BuildEssentialsDragImage(favicon, theme_color),
      gfx::Vector2d(-16, -16));
}

void OnDragExited() override {
  // ... existing logic ...
  GetWidget()->UpdateDragImage(
      maho::BuildTabStackDragImage(...),
      original_offset_);
}
```

### 3.4 Cross-window — `MahoSidebarView::OnDragLeftWindow` (new)

매칭: Zen `handle_windowDragLeave`. 기존 `OnDragExited` 중 cursor 가 widget 바깥으로 나간 경우만 detect:

```cpp
if (CursorOutsideWidget(event, kAllowedMargin)) {
  GetWidget()->UpdateDragImage(
      maho::BuildCrossWindowDragImage(thumb, kBorderColor),
      original_offset_);
}
```

Re-entry 시 (다른 window 의 sidebar 진입) 원 image 복원.

### 3.5 Revert `CreateSidebarDragImage`

기존 `maho_sidebar_drag_util.cc` 의 view-capture impl 은 단계적으로 deprecate. 모든 callsite 가 `BuildTabStackDragImage` / `BuildEssentialsDragImage` 로 교체될 때 함수 + helper 삭제.

---

## 4. Phasing

| Phase | Deliverable | Time | Owner |
|---|---|---|---|
| 1 | Public API (`drag_drop_client.h`, `widget.h`, build flag) | 1d | self |
| 2 | macOS impl + integration + visual QA | 4d | self |
| 3 | Windows impl + integration | 4d | self |
| 4 | Linux X11 impl + integration | 4d | self |
| 5 | Linux Wayland impl + integration | 3d | self |
| 6 | Cross-window + cross-zone polish | 2d | self |
| 7 | Unit + interactive tests on all 3 platforms | 4d | self |
| 8 | Plan-vs-impl review + bug bash | 2d | momus + user |

**Total: 24일** (1 dev, sequential). Phases 2–5 can partially parallelize if a CI matrix is set up.

---

## 5. Files to Create / Modify

### Chromium overlay (chromium_src or direct override)
- `ui/aura/client/drag_drop_client.h` — virtual method
- `ui/views/widget/widget.h/cc` — convenience wrapper
- `ui/views/cocoa/drag_drop_client_mac.h/mm` — macOS impl
- `ui/aura/client/drag_drop_client_win.h/cc` — Windows impl
- `ui/base/dragdrop/os_exchange_data_provider_win.h/cc` — Win SHDRAGIMAGE re-init
- `ui/base/x/x11_drag_drop_client.h/cc` — X11 impl
- `ui/base/x/x11_drag_widget.h/cc` — repaint
- `ui/ozone/platform/wayland/host/wayland_data_drag_controller.h/cc` — Wayland impl
- `ui/ozone/platform/wayland/host/wayland_data_source.h/cc` — surface attach/commit

### Maho overlay
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_image_factory.h/cc` — new helper
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` — drag source swap
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h/cc` — zone-entry/exit hooks
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` — cross-window detection
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_drag_util.cc` — eventual deprecation
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc` — tests

---

## 6. Risks & Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| macOS 1 frame lag after enumerateDraggingItems | Medium | Force `setDraggingFormation:` after enumerate to trigger redraw. Add UMA timing histogram. |
| Windows Shell IDropTargetHelper image cache | Medium | Re-call `IDataObject::SetData(CFSTR_DRAGIMAGEBITS)` + small cursor jiggle (1px nudge) if redraw doesn't happen within 16ms. |
| X11 WM compatibility (i3, dwm) | Low | Test matrix: gnome, kde, xfce, i3, dwm. Fallback: skip update on offending WMs and accept first-set image. |
| Wayland compositor variance | Low | Same fallback. Test mutter, kwin, sway. |
| Chromium upstream rebase cost | High | Build flag `enable_maho_drag_update_image` lets fork be disabled. Patch self-contained per file. |
| Test infra for OS-level drag | High | Use `EventGenerator::PressLeftButton + DragMouseTo` + mock `DragDropClient` to verify `UpdateDragImage` is called with right args. Visual snapshot tests on each OS. |
| Cross-app drag (e.g., Finder file → favorites) | Medium | Native drag still works; we only swap our own outgoing drags. External drops unaffected. |
| User aborts mid-drag (Esc) | Low | `EndDrag` clears stored session/data_object/icon_surface; UpdateDragImage no-op safely. |

---

## 7. Testing Strategy

### 7.1 Unit
- Mock `DragDropClient::UpdateDragImage` and verify it's called with correct image + offset on:
  - Drag start (initial image)
  - `OnDragEntered` favorites grid (essentials image)
  - `OnDragExited` favorites grid (revert to initial)
  - Cross-widget cursor position (cross-window image)

### 7.2 Interactive (existing unittest runners)
- `maho_sidebar_dnd_dispatch_unittest.cc` 에 추가:
  - `MidDragImageSwapsOnFavoritesEntry`
  - `MidDragImageRestoresOnFavoritesExit`
  - `CrossWindowDragImageSwapsOnLeave`

### 7.3 Visual regression (per platform, manual)
- Recorded video comparison Maho vs Zen, side-by-side.

---

## 8. Rollout

1. Merge behind `enable_maho_drag_update_image` flag (default ON for darwin, OFF for win/linux until tested).
2. macOS canary build → internal QA 1 week.
3. Enable on Windows canary after Phase 3.
4. Linux X11 / Wayland after Phase 4–5.

---

## 9. Open Questions

1. macOS NSDraggingSession 의 `imageComponentsProvider` 와 `setDraggingFrame:contents:` 중 어느 게 더 안정적인가? (둘 다 시도 후 결정)
2. Windows 에서 `IDragSourceHelper2::SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT)` 사용 여부 — drop description 도 zen-essential 진입 시 변하면 좋음.
3. Wayland 에서 buffer-release 이벤트 처리 — `wl_buffer_release` 후에만 새 buffer attach 안전. 현재 `WaylandShmBuffer` pool 확인 필요.

---

## 10. Acceptance Criteria

- macOS: cursor 가 sidebar 안 일반 영역에 있으면 tab clone 모양 image follows. favorites grid 진입 즉시 favicon-only round image 로 swap. exit 시 즉시 복원.
- Windows: 동일.
- Linux X11/Wayland: 동일.
- 모든 platform 에서 cross-window drag 시 small canvas thumb 으로 swap, re-entry 시 복원.
- 0 regression on existing drag flows (cross-app, bookmark drag, link drag).
- 모든 test (unit + interactive) green.
- Visual side-by-side 비교에서 사용자 OK.
