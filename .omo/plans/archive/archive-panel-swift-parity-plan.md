# Archive Panel — Swift Parity Implementation Plan (Oracle-Revised)

## Objective
Bring `maho_sidebar_archive_view.cc/h` to full feature parity with `SidebarArchivePanelView.swift`.

## Reference Files
- **Swift**: `maho/macos-shell/Sources/Sidebar/SidebarArchivePanelView.swift` (1176 lines)
- **C++ current**: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc/h`
- **Existing patterns**: `maho_sidebar_tab_list_view.cc` (hover, context menu, UpdateAppearance)
- **Rust model**: `ArchivedTabViewModel { id, title, url, favicon: Option<ImageData>, archivedAt }` where `ImageData { data: Vec<u8>, width, height, format }`

## Scope — 9 Phases (Oracle-revised ordering)

---

### Phase 0: Header/Body Container Structural Refactor

**Why first**: Swift has `headerSurfaceView` + `bodyContainerView` as rounded card surfaces. Filter button lives in header, empty state overlays inside body. All subsequent phases depend on this structure.

**Changes in archive_view.cc BuildUi()**:
- Refactor root layout from flat vertical to:
  ```
  root (BoxLayout vertical)
  ├── header_surface_ (rounded 18dp, border 1px, bg archivePanelBackground)
  │   └── controls_container_ (horizontal: search_field_ flex=1 + filter_button_ fixed 96dp)
  ├── body_container_ (rounded 18dp, FillLayout — scroll + empty overlay as siblings)
  │   ├── scroll_view_ (fills body)
  │   └── empty_state_view_ (centered overlay, hidden when results exist)
  ```
- `header_surface_`: `views::View` with `CreateRoundedRectBackground(ui::kColorSysSurface1, 18)` + `CreateRoundedRectBorder(1, 18, ui::kColorSysSurfaceVariant)`.
- `body_container_`: `views::View` with `FillLayout`, rounded rect bg, `flex=1` on root layout.
- Move `scroll_view_` and `empty_state_` as children of `body_container_` (not root).
- Scroll content inset: `gfx::Insets::VH(6, 0)` matching Swift's `scrollContentInset: 6`.

**Changes in archive_view.h**:
- Add members: `raw_ptr<views::View> header_surface_`, `raw_ptr<views::View> body_container_`.

---

### Phase 1: Filter Menu

**What**: Filter button next to search field in `header_surface_`. 4 modes: "Any time" (default), "Today", "Past week", "Older". Active filter changes button styling.

**Changes in archive_view.h**:
- Add `enum class ArchiveFilterMode { kAll, kToday, kPastWeek, kOlder }`.
- Implement `ui::SimpleMenuModel::Delegate` on `MahoSidebarArchiveView`.
- Add members: `ArchiveFilterMode filter_mode_`, `raw_ptr<views::LabelButton> filter_button_`, `std::unique_ptr<ui::SimpleMenuModel> filter_menu_model_`, `std::unique_ptr<views::MenuRunner> filter_menu_runner_`.
- Add methods: `OnFilterPressed()`, `UpdateFilterButtonAppearance()`.
- Delegate methods: `ExecuteCommand(int)`, `IsCommandIdChecked(int)`.

**Changes in archive_view.cc**:
- Filter button: `views::LabelButton` (NOT MdTextButton — too opinionated for 96×32 pill). Label "Filter", icon from vector_icons. Rounded corners 16dp, custom bg/border for active/inactive states.
- `ApplyFilters()`: Apply filter_mode_ AFTER search:
  - kAll → always true
  - kToday → day_offset == 0
  - kPastWeek → day_offset >= 1 && day_offset <= 7
  - **kOlder → day_offset < 0 || day_offset > 7** (Oracle fix: unknown dates = older)
- `OnFilterPressed()`: Build `ui::SimpleMenuModel` with 4 items + checkmark on current. Show via `views::MenuRunner`.
- `ResetState()`: Also reset filter_mode_ to kAll + `UpdateFilterButtonAppearance()`.
- Empty state text: "Clear the search or adjust the filter." when `filter_mode_ != kAll || has_search`.
- Filter button accessibility: `SetAccessibleName()`, update value on mode change.

---

### Phase 2: Row Interactions (Hover + Focus Ring + Mouse-Up Restore) — MERGED from old P2/P10/P11

**What**: Full row interaction parity — hover effects, focus ring, proper mouse-up restore semantics.

**ArchiveRowView changes**:

**Hover**:
- Override `OnMouseEntered()` / `OnMouseExited()` → call `UpdateAppearance()`.
- `UpdateAppearance()`:
  - **Idle**: low-alpha background `ui::kColorSysSurface` + nearly-transparent border (NOT transparent — Swift uses faint idle fill)
  - **Hovered**: `ui::kColorSysSurface2` bg, visible border `ui::kColorSysSurfaceVariant`
  - **Focused**: `ui::kColorSysSurface3` bg, visible border

**Focus Ring** (Oracle: use `FocusRing::Install`, NOT `SetInstallFocusRingOnFocus`):
- `views::FocusRing::Install(this)` in constructor.
- `views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(), kRowCornerRadius)` — shapes focus ring to row corners.
- Override `OnFocus()` / `OnBlur()` / `OnThemeChanged()` → call `UpdateAppearance()`.

**Mouse-Up Restore** (Oracle: left-button guard):
- Add `bool restore_pending_ = false` member.
- `OnMousePressed()`: If left button → `RequestFocus()`, set `restore_pending_ = true`, return true. Else → return `views::View::OnMousePressed(event)`.
- `OnMouseReleased()`: If `restore_pending_` AND `event.changed_button_flags() & ui::EF_LEFT_MOUSE_BUTTON` AND `HitTestPoint(event.location())` → fire restore. Reset `restore_pending_`. Call base.

**Accessibility**:
- `GetViewAccessibility().SetRole(ax::mojom::Role::kButton)` — row acts as button.
- `SetAccessibleName(title)`.

---

### Phase 3: Row Context Menu (Restore / Delete)

**What**: Right-click shows "Restore" / "Delete" menu.

**ArchiveRowView changes**:
- Implement `views::ContextMenuController` AND `ui::SimpleMenuModel::Delegate` on ArchiveRowView.
- `set_context_menu_controller(this)` in constructor.
- `ShowContextMenuForViewImpl()`: Build 2-item `SimpleMenuModel` (enum: `kRestore = 0`, `kDelete = 1`), run via `views::MenuRunner(CONTEXT_MENU)`.
- `ExecuteCommand()`: dispatch restore/delete callbacks.
- Store `menu_model_` and `menu_runner_` as members for lifetime safety.

---

### Phase 4: Favicon Display + Fallback Globe Icon

**What**: 14×14 favicon left of text stack. Fallback globe for missing favicon.

**Model change** (Oracle fix: nested dict, NOT base64):
- Add to `ArchivedTabItem`: `std::vector<uint8_t> favicon_png_data` (empty = no favicon).
- Parse from JSON: `favicon` is a nested dict `{ "data": [...], "width": N, "height": N, "format": "png" }`.
  ```cpp
  const auto* favicon_dict = dict->FindDict("favicon");
  if (favicon_dict) {
    const auto* data_list = favicon_dict->FindList("data");
    if (data_list) {
      std::vector<uint8_t> bytes;
      bytes.reserve(data_list->size());
      for (const auto& v : *data_list) {
        bytes.push_back(static_cast<uint8_t>(v.GetInt()));
      }
      tab.favicon_png_data = std::move(bytes);
    }
  }
  ```
- In current pipeline, favicon bytes are PNG-encoded (WebViewManager re-encodes to PNG). No JPEG/WebP decode needed. No BUILD.gn changes.

**ArchiveRowView changes**:
- Add `views::ImageView* favicon_view_` (14×14).
- If `favicon_png_data` not empty: `gfx::Image::CreateFrom1xPNGBytes(data.data(), data.size())` → set image.
- If empty: fallback globe icon. **Verify actual vector icon constant** in `components/vector_icons/` before coding (don't guess `kGlobeIcon`). Tinted `ui::kColorSysOnSurfaceSubtle`.
- Layout: `[favicon 14] [8dp gap] [text_container flex=1] [restore_glyph]`

---

### Phase 5: Restore Glyph Indicator

**What**: Small trailing icon (↩) with alpha that increases on hover/focus.

**ArchiveRowView changes**:
- Add `views::ImageView* restore_glyph_` (9×9).
- **Verify actual vector icon constant** before coding.
- Alpha handling: Since `ImageView` has no simple alpha API, use `SetPaintToLayer(true)` + `layer()->SetOpacity()`:
  - Idle: 0.22
  - Hovered/focused: 0.68
- `UpdateAppearance()` updates glyph opacity.

---

### Phase 6: Row Divider Line

**What**: Hairline divider between rows (hidden on last row per section).

**ArchiveRowView changes**:
- Add `views::View* divider_view_` — **1dp** height (Oracle: 0.5dp not integer-friendly), low-alpha `ui::kColorSysSurfaceVariant`.
- Position at row bottom, aligned from text stack leading to row trailing. Since main row layout is horizontal BoxLayout, divider must be **manually positioned** (not a BoxLayout child) — either custom-paint in `OnPaintBackground()` or add as overlay with absolute bounds in `Layout()`.
- Add `SetShowsDivider(bool)` method.
- `RebuildList()`: `row->SetShowsDivider(index < section.tabs.size() - 1)`.

---

### Phase 7: Metrics + Spacing

**What**: Match Swift metrics exactly.

**Constants**:
- `kRowHeight`: 44 → **48**
- `kRowSpacing`: 2 → **6**
- `kSectionSpacing`: 12 → **14**
- `kSectionHeaderSpacing`: (new) **7** (gap between section header and first row)
- `kRowInsets`: → `TLBR(0, 12, 0, 12)`
- `kRowCornerRadius`: 8 (unchanged)

**Spacing** (Oracle: single BoxLayout can't express 3 gaps):
- Wrap each section in its own vertical container (spacing=6 for rows).
- Insert 7dp spacer after section header.
- Insert 14dp spacer between section containers.
- OR use explicit spacer views between items.

---

### Phase 8: Subtitle Path Compaction + Title Trim

**What**: Subtitle shows `host / compacted_path`. Title trims whitespace before fallback.

**Changes**:
- Rename `CompactHost()` → `CompactLocation()`:
  ```cpp
  GURL url(url_str);
  std::string host = url.host();
  std::string path = url.path();
  // Strip leading/trailing "/"
  while (!path.empty() && path.front() == '/') path.erase(0, 1);
  while (!path.empty() && path.back() == '/') path.pop_back();
  if (path.empty()) return UTF8ToUTF16(host);
  if (path.length() > 28) path = path.substr(0, 25) + "…";
  return UTF8ToUTF16(host + " / " + path);
  ```
- Title: Trim whitespace before deciding fallback to host (Oracle: Swift does this).
- Row subtitle uses `CompactLocation(tab.url)`.

---

### Phase 9: Empty State View

**What**: Dedicated `ArchiveEmptyStateView` (private class in same .cc) with archive icon, silhouette card, filter-aware text.

**New class** `ArchiveEmptyStateView : public views::View` (in archive_view.cc):
- Silhouette container: rounded rect bg (corner radius 16), subtle fill + 1px border, height ~100dp, padded 16dp.
- Archive box icon: **verify actual vector icon constant** (13dp, muted tint).
- Title label: "No archived tabs yet" or "No matching tabs".
- Subtitle label: "Clear the search or adjust the filter." (visible only when filters active).
- `Configure(bool has_archived_tabs, bool has_active_filters)` method.
- Centered vertically in `body_container_` (FillLayout overlay).

**Replace** current inline empty state members with single `raw_ptr<ArchiveEmptyStateView> empty_state_view_`.

---

## Files Modified
- `maho_sidebar_archive_view.h` — model fields, filter enum, delegate interface, new members
- `maho_sidebar_archive_view.cc` — all 9 phases, ArchiveEmptyStateView private class

## Files NOT Modified
- No new files created
- No changes to BUILD.gn (PNG decode already available, no new source files)
- No changes to Rust/FFI layer

## Validation
- Compile single file: `autoninja -C out/Default obj/chrome/browser/ui/ui/maho_sidebar_archive_view.o`
- Full build: `autoninja -C out/Default chrome`
- Visual: Archive panel matches Swift reference in structure, row polish, filter, favicon, hover, context menu

## Oracle Review Notes (incorporated)
- ✅ CRITICAL: Favicon JSON is nested dict `{data, width, height, format}`, not base64
- ✅ MAJOR: Focus ring uses `FocusRing::Install()` + `InstallRoundRectHighlightPathGenerator()`, NOT `SetInstallFocusRingOnFocus()`
- ✅ MAJOR: Mouse-up restore gated on left-button via `changed_button_flags()` + `restore_pending_`
- ✅ MAJOR: `RequestFocus()` on mouse down + `OnFocus()/OnBlur()/OnThemeChanged()` overrides
- ✅ MAJOR: Header/body container refactor moved to Phase 0 (before all other phases)
- ✅ MAJOR: `SimpleMenuModel::Delegate` explicitly required on both archive view and row
- ✅ MEDIUM: `views::LabelButton` instead of `MdTextButton` for filter
- ✅ MEDIUM: Older filter matches `day_offset < 0 || day_offset > 7`
- ✅ MEDIUM: Idle row uses faint fill, not transparent
- ✅ MEDIUM: Empty state extracted to private `ArchiveEmptyStateView` class
- ✅ MEDIUM: 1dp low-alpha divider instead of 0.5dp
- ✅ MEDIUM: Per-section containers for mixed spacing (6/7/14)
- ✅ MEDIUM: Verify vector icon constants before coding
- ✅ MINOR: Trim title whitespace before fallback
- ✅ MINOR: Glyph alpha via layer opacity
