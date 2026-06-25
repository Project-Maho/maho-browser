# Maho Arc Toolbar Button Provider — Bubble Anchor & Translate Wiring

**Status:** Ready for execution (planning)
**Surface:** `maho-chromium/browser/ui/views/sidebar/`, `maho-chromium/chromium_src/chrome/browser/ui/views/toolbar/`, `maho-chromium/chromium_src/chrome/browser/ui/views/frame/`
**Mode:** Architectural — replace upstream `ToolbarButtonProvider` with Maho-side provider in Arc layout, wire `secondary_label` state to real translate signals, anchor translate bubble to the sidebar pill
**Parent plans:** None directly. Related context:
- `.sisyphus/plans/address-bar-utility-panel-arc-parity-plan.md` (utility panel — long-tail page actions home, future)
- `.sisyphus/plans/address-bar-utility-panel-extension-icon-grid-plan-2026-05-25.md` (Phase 3 of utility panel)
**Verification:** Unit tests + canonical `python3 maho-chromium/build/scripts/build_maho.py` + manual smoke (open foreign-language page → secondary_label updates → click → translate bubble anchored to pill)

---

## 1. Overview

In Maho's Arc layout (`IsMahoArcLayoutActive() == true`), the upstream `ToolbarView` and `LocationBarView` are laid out at zero bounds with `visibility=false`. Yet `ToolbarView` still self-registers as the singleton `ToolbarButtonProvider` on `BrowserWindowInterface`. As a result, every bubble routed through `ToolbarButtonProvider::From(browser)->GetBubbleAnchor(...)` (translate, zoom, memory saver, sharing hub, send-tab-to-self, autofill memory promo, AT memory promo, find bar, plus extension/IPH dialogs — 25+ call sites) anchors to invisible / zero-bounded views.

The translate anchor path is the canonical example of how this fails: `BrowserView::ShowTranslateBubble` → `ToolbarButtonProvider::GetBubbleAnchor(kActionShowTranslate)` → `ToolbarView::GetAnchorView(kActionShowTranslate)`, which dispatches into the `LocationBarView` page-action path and resolves to the (zero-bounded, invisible) translate `PageActionView` inside `LocationBarView::page_action_container_`. `ToolbarView::GetBubbleAnchor` then checks `IsDrawn()` on that resolved view; because the page-action view is not drawn in Arc, it falls back to `top_container`. `top_container` is itself zero-bounded under Arc layout, so the bubble ends up either anchored to a zero-rect at window origin or failing to attach correctly. The `top_container` fallback is the *symptom*, not the entry point — every step of the chain (`GetAnchorView` resolution, the page-action path, the `!IsDrawn()` branch, and the `top_container` fallback) is broken in Arc, and the fix has to acknowledge that whole chain rather than treating any single hop as the root cause.

The translate feature is the immediately visible symptom: `MahoSidebarSearchView::BuildSecondaryText()` returns the hardcoded literal `u"Translation Available"` whenever the pill is in `kAddressCard` mode, regardless of any actual translate signal, and the text is not wired to invoke `chrome::ShowTranslateBubble()`. Clicking the text falls through to `OnIdleButtonPressed` which routes to omnibox-edit mode, never to translate.

This plan delivers a two-part fix that addresses both the **systemic anchor breakage** and the **translate-specific dummy text + missing click handler**, scoped narrowly enough to ship cleanly:

1. **`MahoToolbarButtonProvider`** — a Maho-owned implementation of the upstream `ToolbarButtonProvider` interface that provides valid bubble anchors pointing at sidebar pill subviews. Registered as the `UnownedUserData` provider on `BrowserWindowInterface` when `IsMahoArcLayoutActive()` is true. The upstream `ToolbarView` is patched (mirroring its existing WebApp branch) to skip its own self-registration when Arc layout is active.

2. **`MahoSidebarSearchView` translate wiring** — `secondary_label_` becomes a true page-action surface for translate: it observes `ContentTranslateDriver::TranslationObserver::OnTranslateEnabledChanged`, reflects real `LanguageState::translate_enabled()` state, hides when translate is not available, and routes its own click events to `chrome::ShowTranslateBubble(browser_window_interface)`. The translate bubble's anchor goes through the new provider and ends up attached to `secondary_label_` (or the pill itself as a stable fallback).

This plan **does not** generalize `secondary_label_` to multiple page actions (zoom, save-password, reader-mode, etc.) and **does not** redesign the long-tail page-action surface (the utility panel handles that and is its own plan). Translate is the worked example that proves the architectural seam; further page actions can adopt the same pattern in follow-up work.

Zen Browser's design (researched) consolidates everything into a single `#zen-site-data-icon-button` panel and drops translate entirely. Maho's existing direction (state-text in the pill itself) is **strictly more discoverable** than Zen's approach. This plan ratifies that direction architecturally without regressing it.

---

## 2. Scope

### Must ship

- **2A — `MahoToolbarButtonProvider` class.** A new C++ class implementing every pure-virtual on `ToolbarButtonProvider` (`GetBubbleAnchor`, `GetPageActionView`, `GetPageActionIconView`, `GetExtensionsToolbarDesktop`, `GetPinnedToolbarActions`, `GetToolbarButtonSize`, `GetDefaultExtensionDialogAnchor`, `GetAppMenuControl`, `GetFindBarBoundingBox`, `FocusToolbar`, `GetAsAccessiblePaneView`, `ZoomChangedForActiveTab`, `GetAvatarToolbarButtonInterface`, `GetBackButton`, `GetReloadButton`, `GetIntentChipButton`, `GetDownloadButton`, `GetWebUIToolbarViewForTesting`). Owned by `MahoSidebarContainerView`. Lifetime: same as the sidebar container.
- **2B — Provider registration.** In `MahoSidebarContainerView` constructor (or a new `Init()` that runs after the sidebar pill is constructed), instantiate the provider and register it as `UnownedUserData<ToolbarButtonProvider>` on `browser->GetUnownedUserDataHost()`. Use `ui::ScopedUnownedUserData<ToolbarButtonProvider>` with the same pattern as `ToolbarView::scoped_unowned_user_data_`.
- **2C — Upstream `ToolbarView` self-registration skip in Arc mode (gated on §5.0).** When `IsMahoArcLayoutActive()` is true, `ToolbarView` must NOT emplace its own `scoped_unowned_user_data_` for `ToolbarButtonProvider`; otherwise it will compete with `MahoToolbarButtonProvider` for the singleton slot on `BrowserWindowInterface`. The conceptual change mirrors the existing WebApp branch at upstream `toolbar_view.cc` lines 257-261, which already documents the precedent: *"WebApp type-browsers set their own ToolbarButtonProvider."* — Maho is the second variant.

  The exact override mechanism, however, is a chromium_src concern and **must be validated in the §5.0 grounding pass before any other Must-ship work proceeds**. Candidate techniques in preference order:
  1. A clean Brave-style chromium_src overlay (`maho-chromium/chromium_src/chrome/browser/ui/views/toolbar/toolbar_view.cc`) using the `#define` + `#include` pattern to substitute the constructor's self-registration conditional without forking the upstream file.
  2. A targeted hook upstream (e.g., a small predicate or virtual seam) overlay-patched via the same chromium_src convention. Acceptable only if §5.0 confirms the seam is minimal and reviewable.
  3. **No-go path** — if neither (1) nor (2) is viable without a casual direct edit to upstream `toolbar_view.cc`, this Must-ship item does *not* ship. Stop the systemic provider replacement (§2A–§2B, §2D, §2I) and route to the translate-only fallback in **§2.Contingency** instead. The repo's anti-pattern policy explicitly prohibits casual direct upstream edits, so this is a hard gate, not a hand-wave.

  Validation gate: §5.0 produces a written go/no-go on the chosen override technique. Go ⇒ proceed to §5.3 with the validated mechanism. No-go ⇒ drop §2A–§2I as a unit and ship only §2.Contingency. §2J (identifier dedup) and the translate observer/click wiring (§2E, §2F) are independent of `ToolbarView` and ship on either path.
- **2D — Anchor implementation.**
  - `GetBubbleAnchor(action_id)` for `kActionShowTranslate` → `views::BubbleAnchor(secondary_label_)` if visible, else `views::BubbleAnchor(idle_button_)` (the pill itself).
  - `GetBubbleAnchor(std::nullopt)` (default) → `views::BubbleAnchor(idle_button_)`.
  - `GetBubbleAnchor(other action_id)` → `views::BubbleAnchor(idle_button_)` for now (long-tail follow-up will refine per-action).
  - `GetDefaultExtensionDialogAnchor()` → `views::BubbleAnchor(idle_button_)` (same fallback).
- **2E — `MahoSidebarSearchView` translate state observer.** Add a `ContentTranslateDriver::TranslationObserver` member that:
  - Subscribes to the active tab's `ContentTranslateDriver` on construction and on `WillDiscardContents` / tab-switch.
  - Implements `OnTranslateEnabledChanged(WebContents*)` and `OnIsPageTranslatedChanged(WebContents*)` to recompute `secondary_label_` text and visibility.
  - Drops the `kAddressCard`-only literal `u"Translation Available"` and replaces it with a method that returns `u"Translation Available"` only when `LanguageState::translate_enabled() == true && !LanguageState::IsPageTranslated()`, `u"Translated"` (or hidden) when `IsPageTranslated()`, empty string otherwise.
  - Triggers `PreferredSizeChanged()` and a layout pass when state changes.
- **2F — `secondary_label_` click handler.** Adjust the pill's hit-test routing in `ShouldToggleUtilityPanelForEvent` (or add a new `ShouldRouteEventToTranslate` sibling check) so that:
  - When `secondary_label_` is visible AND the click landed in the secondary_label region, the event routes to `OnTranslateLabelClicked()` rather than `OnIdleButtonPressed()`.
  - `OnTranslateLabelClicked()` calls `chrome::ShowTranslateBubble(browser_window_interface)` (which goes through `BrowserView::ShowTranslateBubble()` → `toolbar_button_provider()->GetBubbleAnchor(kActionShowTranslate)` → our new provider → `secondary_label_` anchor).
  - utility_icon_slot_ hit zone (right side of pill) and primary_label/idle area continue to behave as today.
- **2G — Element identifier on `secondary_label_`.** Apply `views::kElementIdentifierKey = kTranslatePageActionElementId` (the upstream identifier from `chrome/browser/ui/browser_element_identifiers.h`) to `secondary_label_` so that:
  - `BrowserView::ShowTranslateBubble` line 3350-3351 `highlight_element` reference still resolves to a real, drawn view.
  - Existing UI tests that target `kTranslatePageActionElementId` still find a hit.
  - Note: this identifier is also set by the upstream `TranslatePageActionController` on its `PageActionView`. `views::ElementTracker` will accept two views advertising the same identifier in one widget scope, but lookups become ambiguous / first-match: in practice `highlight_element` and other `ElementTracker`-driven flows (UI tests, IPH/promo affordances) can resolve to the upstream zero-bounded, invisible translate `PageActionView` instead of Maho's drawn `secondary_label_`. §2J (must-ship) handles deduplication unconditionally so the Maho label is the only carrier of the identifier in Arc mode.
- **2H — Translate page action icon hidden in Arc.** `TranslatePageActionController::UpdatePageAction()` continues to call `Show(kActionShowTranslate)` and the upstream `PageActionView` continues to be created in the (invisible) `LocationBarView::page_action_container_`. We do NOT need to suppress this — the view is already invisible. We do need to ensure the Maho path does not depend on it for clicks or for bubble anchoring (handled by 2D and 2F).
- **2I — Provider delegation to upstream `ToolbarView` for non-anchor virtuals.** Concrete child-view accessors on `ToolbarButtonProvider` that are unrelated to bubble anchoring (`GetBackButton`, `GetReloadButton`, `GetAvatarToolbarButtonInterface`, `GetDownloadButton`, `GetIntentChipButton`, `GetExtensionsToolbarDesktop`, `GetPinnedToolbarActions`, `GetAppMenuControl`, `GetWebUIToolbarViewForTesting`, `FocusToolbar`, `GetAsAccessiblePaneView`, `ZoomChangedForActiveTab`, `GetFindBarBoundingBox`, `GetToolbarButtonSize`, `GetPageActionView`, `GetPageActionIconView`) must transparently forward to the still-constructed (zero-bounded but in-tree) upstream `ToolbarView` instance. The `ToolbarView` is still built in Arc mode — only its self-registration is skipped per §2C — so its child views are usable as opaque handles by callers that null-check against its accessors or that just need a stable view to anchor focus, drag, accessibility-pane traversal, or extension dialogs against. This delegation eliminates the entire class of `nullptr` deref and `CHECK` failures in callers that assume a non-null toolbar surface, including autofill, IPH, focus traversal, extension dialog hosts, and accessibility tools. **Implementation:** `MahoToolbarButtonProvider` holds a `raw_ptr<ToolbarView>` populated from `BrowserView::toolbar()` after construction, and each non-anchor virtual forwards to the corresponding `ToolbarView` accessor. The only virtuals NOT delegated are the anchor methods `GetBubbleAnchor` and `GetDefaultExtensionDialogAnchor`, which are owned by Maho per §2D.
- **2J — `kTranslatePageActionElementId` deduplication (chromium_src patch on `TranslatePageActionController`).** The upstream `PageActionView` for translate also carries this element identifier. `views::ElementTracker` tolerates two views advertising the same identifier in one widget scope, but lookups resolve to whichever view it finds first, so `highlight_element` (and any UI test or promo flow that asks `ElementTracker` for `kTranslatePageActionElementId`) can land on the upstream invisible translate `PageActionView` rather than Maho's drawn `secondary_label_`. That mis-resolution is the concrete failure mode we are preventing. Override `TranslatePageActionController` via chromium_src patch to skip identifier assignment when `IsMahoArcLayoutActive()` is true, so the identifier lives solely on Maho's `secondary_label_` while keeping the upstream page-action view functionally identical (just unidentified). This is required for §2G to work reliably and for `BrowserView::ShowTranslateBubble`'s `highlight_element` path (line 3350-3351) to resolve to the visible Maho label rather than the invisible upstream view. It also keeps existing UI tests targeting `kTranslatePageActionElementId` finding a hit (the visible Maho label) instead of the zero-bounded upstream page-action view. This patch lives on `TranslatePageActionController`, not on `ToolbarView`, so it is independent of the §2C validation gate and ships on both the §2C systemic path and the §2.Contingency translate-only path.

### Optional (justified, gated, may slip)

- **2K — Provider unit tests.** A small `MahoToolbarButtonProviderTest` (gtest) covering: `GetBubbleAnchor(kActionShowTranslate)` returns `secondary_label_` when visible, falls back to `idle_button_` when not; `GetBubbleAnchor(std::nullopt)` returns `idle_button_`; provider is the registered `ToolbarButtonProvider::From(browser)` when `IsMahoArcLayoutActive()`; non-anchor virtuals delegate to `ToolbarView` (per §2I). Ship together with 2A.

### Contingency: translate-only fallback (if §5.0 fails the §2C validation gate)

If §5.0 returns no-go on the chromium_src override mechanism for §2C — i.e., neither a clean `#define` overlay nor a minimal upstream seam is viable without a casual direct edit to `toolbar_view.cc` — the systemic provider replacement is blocked. The repo's anti-pattern policy forbids the casual-direct-edit route, so in that case we explicitly *do not* ship §2A–§2I as-is. Instead, ship a narrower translate-only path that still delivers the user-visible translate fix in Arc mode while leaving every other bubble's anchoring exactly as today (broken-but-unchanged):

- **2.Contingency.A — Translate observer + click wiring.** Unchanged from §2E and §2F: `MahoSidebarSearchView` becomes a `ContentTranslateDriver::TranslationObserver`, `secondary_label_` reflects real `LanguageState::translate_enabled()` state, and clicking the label routes to a translate code path. This work is independent of the `ToolbarButtonProvider` swap.
- **2.Contingency.B — Direct translate-bubble anchoring in Arc.** Instead of going through `BrowserView::ShowTranslateBubble` → `ToolbarButtonProvider::GetBubbleAnchor`, `OnTranslateLabelClicked()` calls `TranslateBubbleController::ShowBubble(web_contents, anchor_view = secondary_label_, ...)` directly (or the closest available controller-level entry point — verify in §5.0), bypassing the provider-resolved anchor for this single call site only. All other bubbles (zoom, memory saver, sharing hub, autofill memory promo, send-tab-to-self, AT memory promo, find bar, extension/IPH dialogs) continue to anchor via the unchanged upstream `ToolbarView`-registered provider; their visual breakage in Arc remains unfixed but is not regressed beyond current state.
- **2.Contingency.C — `kTranslatePageActionElementId` deduplication.** Unchanged from §2G/§2J: identifier set on `secondary_label_`, dedup chromium_src patch on `TranslatePageActionController` for Arc mode. This patch lives on `TranslatePageActionController`, not on `ToolbarView`, so it is independent of the §2C gate and is expected to be viable on this path as well.
- **2.Contingency.D — No `MahoToolbarButtonProvider`, no §2C patch, no §2I delegation, no §2D anchor methods.** The systemic anchor breakage for non-translate bubbles is filed as a follow-up plan with its own grounding pass on alternative seams; it is *not* addressed in this plan if the contingency path is taken.

Trade-off acknowledged: the contingency ships the translate fix the user actually feels, but leaves the underlying anchor architecture broken for every other bubble that routes through `ToolbarButtonProvider`. We choose this only if the systemic seam can't land cleanly, because shipping a translate fix is strictly better than shipping nothing while the systemic plan is rewritten. **The seam choice itself remains correct** — `ToolbarButtonProvider` replacement is the right architectural answer when feasible, and §4.3's argument for it does not weaken if §5.0 rejects the chromium_src mechanism. The contingency is a delivery-mechanism fallback, not an architectural retreat.

### Out of scope

- **Generalizing `secondary_label_` to other page actions** (zoom, save-password, reader-mode, cookie-controls). Each requires its own observer, priority arbitration with translate, and UX text decisions. Future plan.
- **Long-tail page-action panel.** Adding a Zen-style site-data panel inside the existing `MahoLocationBarUtilityPanelView` for the actions that don't fit `secondary_label_`. The utility panel exists; expanding its action coverage is a separate, larger plan.
- **Migrating other bubbles' anchors** beyond translate (zoom bubble, memory saver bubble, sharing hub, send-tab-to-self, AT memory promo). Once the provider exists, those bubbles will resolve through it automatically and *should* work with the `idle_button_` fallback. We will smoke-test those during §5.4 but will NOT design per-bubble anchor refinements here. If a specific bubble looks wrong, file a follow-up.
- **`partial_translate_bubble_view`** (right-click selection translate). Plumbed through `BrowserView::StartPartialTranslate` — same anchor path. Will resolve via provider; no separate work.
- **Supporting non-Arc Maho windows** (popup windows, app windows, devtools). The provider only registers when `IsMahoArcLayoutActive()` is true; non-Arc windows continue to use `ToolbarView`'s registration, unchanged.
- **Testing `kAddressCard` display mode transitions.** The existing `display_mode` state machine drives whether `secondary_label_` is visible at all. Extending Arc mode display-mode logic to add a "translate-active" mode is out of scope; we layer translate state into the existing `kAddressCard` mode.
- New Mojo IDL, FFI, or Rust-side changes.
- Adding `relay/` or `mail/` to the workspace.

---

## 3. Grounded Current State

Files in scope (verified to exist on disk):

- `chromium/src/chrome/browser/ui/views/frame/toolbar_button_provider.h` — interface definition (lines 39-111), 17 pure virtuals
- `chromium/src/chrome/browser/ui/views/frame/toolbar_button_provider.cc` — `From(BrowserWindowInterface*)` dispatcher via `UnownedUserData`
- `chromium/src/chrome/browser/ui/views/toolbar/toolbar_view.h` — declares `scoped_unowned_user_data_` member at line 424
- `chromium/src/chrome/browser/ui/views/toolbar/toolbar_view.cc` — self-registration at lines 257-261, `GetBubbleAnchor` at line 1701, `GetAnchorView` plumbing
- `chromium/src/chrome/browser/ui/views/frame/browser_view.cc` — `IsMahoArcLayoutActive()` at line 1544, `ShowTranslateBubble()` at line 3327-3366 (uses `toolbar_button_provider()->GetBubbleAnchor(kActionShowTranslate)`), `StartPartialTranslate` at line 3368-3384
- `chromium/src/chrome/browser/ui/browser_element_identifiers.h` — `kTranslatePageActionElementId`
- `chromium/src/components/translate/content/browser/content_translate_driver.h` — `TranslationObserver` interface (`OnTranslateEnabledChanged`, `OnIsPageTranslatedChanged`, `OnLanguageDetermined`)
- `chromium/src/chrome/browser/translate/chrome_translate_client.h` — `ChromeTranslateClient::FromWebContents(WebContents*)` and `translate_driver()` accessor
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.h` — `MahoSidebarSearchView` declaration, `secondary_label_` at line implied (verified in .cc:207)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc` — `BuildSecondaryText()` at line 660-666, `ShouldToggleUtilityPanelForEvent` at line 676-709, `OnIdleButtonPressed` at line 475
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.h` — `MahoSidebarContainerView` declaration at line 23
- `maho-chromium/chromium_src/chrome/browser/ui/views/frame/browser_view.cc` — existing chromium_src patch documenting `maho_sidebar_container_` and command overlay wiring
- `maho-chromium/chromium_src/chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc` — Arc layout description (top chrome zero-bounded + invisible)

Current behavior (verified):

- `ToolbarView::ToolbarView` at line 251-261 emplaces `scoped_unowned_user_data_` unless `web_app::AppBrowserController::IsWebApp(browser)` is true — this is the existing extension seam.
- `ToolbarView::GetBubbleAnchor` at line 1701-1715 calls `GetAnchorView(action_id)`, then returns `views::BubbleAnchor(view)` if the resolved view is drawn, otherwise falls back to `top_container`. `GetAnchorView(action_id)` for action-id'd anchors dispatches into the `LocationBarView` page-action path (resolving to the per-action `PageActionView` inside `LocationBarView::page_action_container_`), and for `std::nullopt` returns the toolbar / app-menu surface. In Arc, both the resolved page-action view and the `top_container` fallback are zero-bounded, so every step of the chain is broken — the `top_container` fallback only happens *after* the page-action view fails `IsDrawn()`.
- `BrowserView::ShowTranslateBubble` line 3354 calls `toolbar_button_provider()->GetBubbleAnchor(kActionShowTranslate)`. With Arc, this goes `ToolbarView::GetBubbleAnchor` → `GetAnchorView(kActionShowTranslate)` → `LocationBarView` page-action lookup (zero-bounded translate `PageActionView`) → `IsDrawn() == false` → fallback to `top_container` (also zero-bounded) — broken anchor end-to-end.
- `MahoSidebarSearchView::BuildSecondaryText` line 660-666 returns the literal `u"Translation Available"` whenever `display_mode == kAddressCard && has_active_tab`. No translate signal observed.
- `MahoSidebarSearchView::ShouldToggleUtilityPanelForEvent` line 676-709 routes hit tests for the utility icon area but does not consider `secondary_label_`. All non-utility-icon clicks fall through to `OnIdleButtonPressed`.
- `OnIdleButtonPressed` (line 475) routes the click to omnibox-edit mode (textfield_ focus) or command palette per the Arc unified-click-path feature flag. No translate path.
- `TranslatePageActionController` at `chromium/src/chrome/browser/ui/views/translate/translate_page_action_controller.cc` is created per-tab in `tab_features.cc` and observes `ContentTranslateDriver`. It calls `page_action_controller->Show(kActionShowTranslate)` which adds a `PageActionView` to `LocationBarView::page_action_container_` — both invisible in Arc. The view exists, just isn't visible.

Ungrounded assumptions to verify in §5.0 grounding pass:
- That `MahoSidebarContainerView` constructor runs after `ToolbarView` is constructed (so registration order is deterministic). If the order is reversed, the Arc skip in §2C is moot because Maho registers second and overwrites; but this is fragile. Prefer §2C as the canonical fix.
- That `kTranslatePageActionElementId` is set on the upstream `PageActionView` (expected per §2J — confirm exact set-site to size the chromium_src patch).
- That `secondary_label_` is a `views::Label` (not in a click-capturing container) such that hit tests reach the parent `idle_button_` cleanly. From .cc line 207, it is added directly to `text_stack_raw` which is inside `idle_button_raw`. Confirmed.

---

## 4. Architecture Walk-Through

### 4.1 Provider lifecycle

```
BrowserView ctor
  ├── ToolbarView ctor
  │     ├── if (!IsWebApp(browser) && !IsMahoArcLayoutActive(browser_view))
  │     │      scoped_unowned_user_data_.emplace(host, *this);   // ← skipped in Arc
  │     └── (toolbar still constructed for view-tree presence; just unregistered)
  │
  ├── (chromium_src patch already wires this) maho_sidebar_container_ = AddChildView(...)
  │     └── MahoSidebarContainerView ctor
  │            ├── builds child views (sidebar_top_bar, search pill, tab list, etc.)
  │            ├── sidebar_search_view_ = AddChildView<MahoSidebarSearchView>(...)
  │            └── toolbar_button_provider_ =
  │                   std::make_unique<MahoToolbarButtonProvider>(
  │                       browser_, sidebar_search_view_)
  │                // provider's ScopedUnownedUserData registers self on browser
  │
  └── BrowserView::AddedToWidget()
         └── (existing) layout_views.maho_sidebar_container = maho_sidebar_container_
```

Result: `ToolbarButtonProvider::From(browser)` returns `MahoToolbarButtonProvider*` for the lifetime of the Arc-mode browser window. All upstream callers are unaware of the swap.

### 4.2 Translate signal flow (after wiring)

```
[Renderer] LanguageDetectionAgent + TranslateAgent → ContentTranslateDriver::RegisterPage
   │
   ▼
[Browser] ContentTranslateDriver
   │     LanguageState::LanguageDetermined(lang, criteria_met)
   │     TranslateManager::InitiateTranslation(lang)
   │            └── filters → MaybeShowOmniboxIcon(decision)
   │                  └── LanguageState::SetTranslateEnabled(true)
   │                        └── ContentTranslateDriver::OnTranslateEnabledChanged()
   │                              └── notifies all TranslationObservers ─────┐
   │                                                                         │
   ┌──────────────────────────────── observer list ─────────────────────────┘
   │
   ├── TranslatePageActionController (upstream)  → invisible page action, no UI
   │
   └── MahoSidebarSearchView (NEW)
            ├── OnTranslateEnabledChanged(WebContents*)
            │     └── UpdateSecondaryLabelForTranslate()
            │            ├── chrome_translate_client = FromWebContents(...)
            │            ├── language_state = client->GetLanguageState()
            │            └── if language_state.translate_enabled():
            │                       secondary_label_->SetText(u"Translation Available")
            │                       secondary_label_->SetVisible(true)
            │                else:
            │                       secondary_label_->SetVisible(false)
            └── PreferredSizeChanged()
```

Click flow:
```
User clicks "Translation Available" in pill
   ↓
MahoSidebarSearchView (OnMousePressed via idle_button_ handler)
   ↓
ShouldRouteEventToTranslate(event) → returns true (hit landed in secondary_label_ region
   AND label is visible AND translate_enabled)
   ↓
OnTranslateLabelClicked()
   ↓
chrome::ShowTranslateBubble(browser_window_interface)
   ↓
BrowserView::ShowTranslateBubble(...)
   ↓
toolbar_button_provider()->GetBubbleAnchor(kActionShowTranslate)
   ↓
MahoToolbarButtonProvider::GetBubbleAnchor(kActionShowTranslate)
   ↓
views::BubbleAnchor(secondary_label_)
   ↓
TranslateBubbleController::ShowTranslateBubble(web_contents, anchor, ...)
   ↓
TranslateBubbleView created and shown, anchored to secondary_label_
```

### 4.3 Why this is the right seam

`ToolbarButtonProvider` is the **only** upstream chokepoint for bubble anchoring in `chrome/browser/ui/views`. The 25+ call sites all dispatch through `ToolbarButtonProvider::From(browser_window_interface)`. Replacing the implementation behind that interface — not patching call sites — keeps Maho's diff against upstream small and absorbs every future bubble that anchors through the provider for free.

The interface already supports multiple implementations; WebApp windows have done this since 2018. Maho is the second variant.

---

## 5. Implementation Steps

### 5.0 Grounding pass (no edits)

- **§2C validation gate (blocking).** Inspect `chromium/src/chrome/browser/ui/views/toolbar/toolbar_view.cc` lines 251-261 and the surrounding constructor body to determine whether the self-registration conditional has a clean `#define` substitution surface or a small upstream-friendly seam that can be overlay-patched via chromium_src. Produce a written go/no-go in the implementation PR description that names the chosen mechanism (overlay #define / hook seam) or declares no-go. **Go ⇒ proceed with §2A–§2I and §5.3. No-go ⇒ drop §2A–§2I and switch to §2.Contingency; §2J, §2E, §2F, §2G still ship.** Do NOT attempt a casual direct edit to upstream `toolbar_view.cc` as a workaround under any circumstance — that is a hard repo-level anti-pattern.
- Read `chrome/browser/ui/views/translate/translate_page_action_controller.cc` once more to confirm element identifier handling on the upstream `PageActionView` and to size the §2J chromium_src patch surface (which ships on both paths).
- Read `MahoSidebarContainerView` ctor end-to-end to confirm the search pill is constructed before any other view that might want to reach `ToolbarButtonProvider::From(browser)`.
- Read `views::ElementTracker` to confirm the resolution semantics for duplicate identifiers in one widget hierarchy — specifically that lookups are first-match / ambiguous rather than rejected — so we know exactly which view today's `highlight_element` and `ElementTracker`-driven test code resolve to before §2J lands (informs §2J implementation, not the gate).
- Verify `BrowserWindowInterface::GetUnownedUserDataHost()` is callable in `MahoSidebarContainerView` ctor (i.e., is the BWI fully constructed by then?). Only relevant on the §2C path.
- For the contingency path, confirm the `TranslateBubbleController` (or equivalent) entry point that accepts an explicit anchor `View*` from a non-`BrowserView` caller; this is the call site §2.Contingency.B uses to bypass `ToolbarButtonProvider`.

Outcome: a one-page grounding note in the implementation PR description, with the §2C go/no-go as the lead item.

### 5.1 Stand up `MahoToolbarButtonProvider` (no registration yet)

New files:
- `maho-chromium/browser/ui/views/sidebar/maho_toolbar_button_provider.h`
- `maho-chromium/browser/ui/views/sidebar/maho_toolbar_button_provider.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_toolbar_button_provider_unittest.cc` (gtest)

Class shape:
```cpp
class MahoToolbarButtonProvider : public ToolbarButtonProvider {
 public:
  MahoToolbarButtonProvider(BrowserWindowInterface* browser,
                            MahoSidebarSearchView* search_view);
  ~MahoToolbarButtonProvider() override;

  // ToolbarButtonProvider:
  views::BubbleAnchor GetBubbleAnchor(
      std::optional<actions::ActionId> action_id) override;
  IconLabelBubbleView* GetPageActionView(actions::ActionId) override;
  PageActionIconView* GetPageActionIconView(PageActionIconType) override;
  views::BubbleAnchor GetDefaultExtensionDialogAnchor() override;
  // ... 14 more virtuals (audit list at §2A)

 private:
  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<MahoSidebarSearchView> search_view_;
  raw_ptr<ToolbarView> upstream_toolbar_;  // delegation target for non-anchor virtuals (§2I)
  std::optional<ui::ScopedUnownedUserData<ToolbarButtonProvider>>
      scoped_unowned_user_data_;
};
```

`GetBubbleAnchor` impl:
```cpp
views::BubbleAnchor MahoToolbarButtonProvider::GetBubbleAnchor(
    std::optional<actions::ActionId> action_id) {
  if (action_id == kActionShowTranslate) {
    if (auto* label = search_view_->translate_label_view();
        label && label->GetVisible() && label->IsDrawn()) {
      return views::BubbleAnchor(label);
    }
  }
  // Default: anchor to the pill itself (always visible in Arc).
  if (auto* idle = search_view_->idle_button_view(); idle && idle->IsDrawn()) {
    return views::BubbleAnchor(idle);
  }
  return views::BubbleAnchor();  // last resort
}
```

Other virtuals: forward to the held `raw_ptr<ToolbarView>` (populated post-construction from `BrowserView::toolbar()`) per §2I. Anchor virtuals (`GetBubbleAnchor`, `GetDefaultExtensionDialogAnchor`) are owned by Maho and do NOT delegate; everything else does.

Add accessors on `MahoSidebarSearchView`:
- `views::Label* translate_label_view() { return secondary_label_; }`
- `views::View* idle_button_view() { return idle_button_; }`

Unit tests (§2K):
- Construct `MahoToolbarButtonProvider` with a fake `BrowserWindowInterface` and a mock `MahoSidebarSearchView`.
- Assert `GetBubbleAnchor(kActionShowTranslate)` returns `secondary_label_` when label is visible.
- Assert it returns `idle_button_` when label is hidden.
- Assert it returns the same when `action_id == std::nullopt`.

### 5.2 Register the provider in `MahoSidebarContainerView`

Edit:
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.h` —
  - Ensure `MahoSidebarContainerView` keeps a typed handle to the search view: a `raw_ptr<MahoSidebarSearchView> sidebar_search_view_ = nullptr;` member. If the existing code only stores the search view as a generic `views::View*` (or only via `AddChildView<...>` without retaining the typed return), add the typed member now — the provider constructor and the lifecycle diagram in §4.1 both assume a `MahoSidebarSearchView*` handle exists, so this member is part of the same change.
  - Add member `std::unique_ptr<MahoToolbarButtonProvider> toolbar_button_provider_;`.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc` — capture the `MahoSidebarSearchView*` returned from `AddChildView<MahoSidebarSearchView>(...)` into `sidebar_search_view_`, then immediately after instantiate the provider:
  ```cpp
  sidebar_search_view_ = AddChildView(std::make_unique<MahoSidebarSearchView>(...));
  toolbar_button_provider_ = std::make_unique<MahoToolbarButtonProvider>(
      browser_->browser_window_interface(), sidebar_search_view_);
  ```

Build target deps:
- `maho-chromium/browser/ui/views/sidebar/BUILD.gn` (or `maho-chromium/browser/BUILD.gn` per existing convention) — add `//chrome/browser/ui/views/frame:toolbar_button_provider` (or whichever GN target houses it).

### 5.3 Skip upstream `ToolbarView` self-registration (chromium_src, gated on §5.0)

This step is **gated on the §5.0 validation outcome for §2C**. If §5.0 returns no-go, skip this entire step and route to §2.Contingency / §5.3-Contingency below.

Goal: when `IsMahoArcLayoutActive()` is true, the upstream `ToolbarView` constructor must NOT emplace `scoped_unowned_user_data_<ToolbarButtonProvider>`. The conceptual diff is the conditional below; the *delivery mechanism* depends on what §5.0 validated:

```
// Conceptual change (delivery mechanism per §5.0):
//   OLD:
//     if (!web_app::AppBrowserController::IsWebApp(browser)) {
//       scoped_unowned_user_data_.emplace(...);
//     }
//   NEW:
//     const bool is_web_app =
//         web_app::AppBrowserController::IsWebApp(browser);
//     const bool is_maho_arc =
//         browser_view && browser_view->IsMahoArcLayoutActive();
//     if (!is_web_app && !is_maho_arc) {
//       scoped_unowned_user_data_.emplace(...);
//     }
```

Implementation paths, in §5.0 preference order:

1. **chromium_src overlay (preferred).** Add `maho-chromium/chromium_src/chrome/browser/ui/views/toolbar/toolbar_view.cc` that uses the `#define` + `#include` pattern to substitute the constructor's self-registration conditional without forking the upstream file. This is the canonical Brave-style approach. Take this path *only* if §5.0 confirms a clean substitution surface — i.e., the conditional is structured such that a `#define` can intercept it cleanly without rewriting unrelated logic.
2. **Targeted upstream-friendly seam.** If no clean `#define` site exists but a minimal hook (e.g., a static predicate function or virtual method on `ToolbarView` whose default returns `true`) can be added upstream-style and overlay-patched via the same chromium_src convention, take that path. Acceptable only if §5.0 confirms the seam is small enough to be reviewable as a single-file change.
3. **No-go ⇒ §5.3-Contingency.** If neither (1) nor (2) is viable without a casual direct edit to upstream `toolbar_view.cc`, this step does *not* ship. Stop, document the rejection in the §5.0 grounding note, and route the implementation to **§5.3-Contingency** below. Do not attempt a direct upstream patch as a workaround. The repo's anti-pattern policy is a hard gate.

`browser_view` is already a ctor parameter on `ToolbarView`, so no extra plumbing is required regardless of which delivery mechanism is chosen.

### 5.3-Contingency Translate-only direct anchoring (only if §5.0 returned no-go)

Skip this section if §5.3 above shipped. Otherwise:

- Do NOT create `MahoToolbarButtonProvider`, do NOT register a Maho-side `ToolbarButtonProvider` on `BrowserWindowInterface`, do NOT touch `toolbar_view.cc` overlay.
- Keep §5.4 (translate observer + state wiring) and the click-routing portion of §5.5 exactly as planned — these are independent of the provider.
- In `OnTranslateLabelClicked()`, instead of routing through `chrome::ShowTranslateBubble(bwi)`, call the translate-bubble controller entry point that accepts an explicit anchor `View*` (verified in §5.0). Pass `secondary_label_` as the anchor and the active `WebContents*` as the source. The exact entry point name is one of `TranslateBubbleController::ShowBubble(...)` or the `BrowserView`-internal helper that `ShowTranslateBubble` itself eventually calls — pick whichever is the smallest stable surface that does not re-enter `ToolbarButtonProvider::GetBubbleAnchor`.
- Keep §2J / §5.5 element-identifier dedup unchanged — the dedup patch on `TranslatePageActionController` ships on this path too.
- Document explicitly in the PR description that other bubbles' anchoring (zoom, memory saver, sharing hub, autofill memory promo, etc.) remains broken in Arc and is filed as a follow-up plan.

### 5.4 Wire `MahoSidebarSearchView` translate signal

Edit `maho_sidebar_search_view.h`:
- Forward declare `translate::ContentTranslateDriver` and `translate::LanguageState`.
- Add `class MahoSidebarSearchView : public ..., public translate::ContentTranslateDriver::TranslationObserver`.
- Add member `base::ScopedObservation<...> translate_observation_{this};` plus tab-discard subscription.

Edit `maho_sidebar_search_view.cc`:
- In ctor, after model_ is set up, observe the active tab's translate driver (if any).
- Hook `tab_strip_model_->AddObserver(this)` (or whatever mechanism `MahoSidebarSearchView` uses to track active tab — verify in §5.0) to re-attach observation on tab changes.
- Implement:
  ```cpp
  void OnTranslateEnabledChanged(content::WebContents* source) override {
    UpdateSecondaryLabelForTranslate();
  }
  void OnIsPageTranslatedChanged(content::WebContents* source) override {
    UpdateSecondaryLabelForTranslate();
  }
  ```
- New private:
  ```cpp
  void UpdateSecondaryLabelForTranslate() {
    auto* contents = browser_->tab_strip_model()->GetActiveWebContents();
    if (!contents) {
      SetSecondaryLabelVisible(false);
      return;
    }
    auto* client = ChromeTranslateClient::FromWebContents(contents);
    if (!client) {
      SetSecondaryLabelVisible(false);
      return;
    }
    const auto& language_state = client->GetLanguageState();
    if (language_state.translate_enabled() &&
        !language_state.IsPageTranslated()) {
      secondary_label_->SetText(
          l10n_util::GetStringUTF16(IDS_MAHO_SIDEBAR_TRANSLATION_AVAILABLE));
      SetSecondaryLabelVisible(true);
    } else {
      SetSecondaryLabelVisible(false);
    }
    PreferredSizeChanged();
  }
  ```
- Replace `BuildSecondaryText` literal with the real state read; or keep `BuildSecondaryText` returning empty string and let `UpdateSecondaryLabelForTranslate` drive the text directly. Choose at implementation time based on what minimizes diff.

Localization:
- Add `IDS_MAHO_SIDEBAR_TRANSLATION_AVAILABLE` to Maho's GRD file (find existing maho strings file in `maho-chromium/`).
- For now, English only.

### 5.5 Hit-test and click handler for translate label

Edit `maho_sidebar_search_view.cc`:
- New helper `bool ShouldRouteEventToTranslate(const ui::Event& event) const;` modeled on `ShouldToggleUtilityPanelForEvent`:
  - Returns true iff `secondary_label_` is visible, the click is a `LocatedEvent`, and the converted point lies inside `secondary_label_->bounds()`.
- In `OnIdleButtonPressed(event)`:
  ```cpp
  if (ShouldToggleUtilityPanelForEvent(event)) {
    // existing utility-panel toggle path
    return;
  }
  if (ShouldRouteEventToTranslate(event)) {
    OnTranslateLabelClicked();
    return;
  }
  // existing fall-through path (omnibox-edit mode)
  ```
- New `void OnTranslateLabelClicked();`:
  ```cpp
  auto* bwi = browser_->browser_window_interface();
  if (bwi) {
    chrome::ShowTranslateBubble(bwi);
  }
  ```
  (or call the helper that exists upstream — verify in §5.0)

Element identifier:
- After `secondary_label_` is created (line 207 in current code), add:
  ```cpp
  secondary_label_->SetProperty(views::kElementIdentifierKey,
                                 kTranslatePageActionElementId);
  ```

Apply the §2J chromium_src override on `TranslatePageActionController` so the upstream `PageActionView` does NOT also claim `kTranslatePageActionElementId` in Arc mode; the identifier lives solely on `secondary_label_`. This patch is must-ship and applies on both the §2C systemic path and the §2.Contingency translate-only path.

### 5.6 Build, run, and smoke

- `python3 maho-chromium/build/scripts/build_maho.py`
- Manual smoke matrix:
  1. Open `https://www.example.es/` (or any foreign-language page) in an Arc-mode window.
  2. Wait ~1s for language detection. Confirm `secondary_label_` shows "Translation Available".
  3. Click `secondary_label_`. Translate bubble should appear anchored to the label, NOT at the window origin.
  4. Click "Translate" in the bubble. Page translates. `secondary_label_` should hide (page is translated).
  5. Switch to a tab with English content. `secondary_label_` should hide (translate not enabled).
  6. (§2C path only) Open zoom controls (Cmd+/Cmd-) in an Arc window. Smoke-test: zoom bubble (memory saver bubble, sharing hub) should anchor sensibly to the pill — they will route through `MahoToolbarButtonProvider::GetBubbleAnchor(std::nullopt)` → `idle_button_` fallback. Visual check, not pixel-exact. Skip on §2.Contingency path; those bubbles are not addressed there.
- Run `bun run test` (proxies to `cargo test --workspace`) and any Chromium unit tests for the new provider class.

### 5.7 Cleanup pass

- Remove the literal `u"Translation Available"` from `BuildSecondaryText()` if §5.4 chose to drive the text via `UpdateSecondaryLabelForTranslate` directly.
- Audit `LOG(INFO) << "MahoPinnedDebug ..."` lines added during development; remove or demote to `DVLOG`.
- Confirm zero new `// TODO(maho)` left in shipped files; convert any remaining to GitHub issues.
- Update `maho-chromium/AGENTS.md` if a new subsystem (`browser/ui/views/sidebar/maho_toolbar_button_provider.{h,cc}`) materially changes the "Where to look" table.

---

## 6. Risks

1. **Duplicate `kTranslatePageActionElementId`.** The upstream `TranslatePageActionController` attaches the identifier to its `PageActionView` (zero-bounded but in the tree). `views::ElementTracker` tolerates two views with the same identifier in one widget scope, but resolves lookups to whichever view it finds first, so `highlight_element` and `ElementTracker`-driven flows (UI tests, IPH/promo affordances) can land on the upstream invisible translate `PageActionView` rather than Maho's `secondary_label_`. **Mitigation:** §2J is must-ship and handles this directly via a chromium_src patch on `TranslatePageActionController` to skip identifier assignment in Arc mode. §5.0 grounding pass sizes the patch surface and confirms the exact `views::ElementTracker` resolution semantics. §2J ships on both the §2C systemic path and the §2.Contingency translate-only path, so the dedup is unconditional.

2. **Provider registration ordering.** `MahoSidebarContainerView` must construct after `BrowserWindowInterface::GetUnownedUserDataHost()` is available and before any caller queries `ToolbarButtonProvider::From(browser)`. The chromium_src `BrowserView` patch documents that the sidebar is built in `BrowserView` ctor (line 11: "AddChildView<MahoSidebarContainerView>"). `ToolbarView::ToolbarView` runs earlier in the same ctor, so without §2C (the upstream skip patch), Maho's provider would overwrite `ToolbarView`'s registration — non-deterministic. **Mitigation:** §2C is the canonical fix; rely on it, not on ordering. Risk only applies on the §2C systemic path; under §2.Contingency, no Maho-side provider is registered, the upstream `ToolbarView` registration stands, and ordering is irrelevant.

3. **`ToolbarButtonProvider` virtuals returning `nullptr`.** Callers (autofill, IPH, extension dialogs, focus traversal, accessibility-pane traversal) may invoke `GetBackButton()` / `GetReloadButton()` / `GetAvatarToolbarButtonInterface()` / `GetIntentChipButton()` / `GetAppMenuControl()` / `GetAsAccessiblePaneView()` without null-checking. **Mitigation:** §2I (must-ship) eliminates this class of failure by transparently forwarding every non-anchor virtual to the still-constructed upstream `ToolbarView`. §5.0 enumerates the full virtual list and any that genuinely cannot delegate (e.g., test-only accessors); ASAN bot remains the safety net for any miss. Risk only applies on the §2C systemic path; under §2.Contingency, no `MahoToolbarButtonProvider` exists and the upstream `ToolbarView` remains the registered provider with its native virtuals intact.

4. **Translate bubble overlapping sidebar pill.** The bubble anchors to `secondary_label_` and renders to the right of the pill (default direction). The sidebar pill is in the leftmost column, so the bubble extends into the contents area. Fine visually, but if the bubble is wide enough to clip the right edge of the sidebar, looks awkward. **Mitigation:** `views::BubbleDialogDelegate::SetArrow(...)` to choose direction. May require chromium_src patch on `TranslateBubbleView::Init` if the existing arrow choice is hardcoded. Defer to QA pass.

5. **Click hit-test ambiguity in `secondary_label_`.** `views::Label` does not consume mouse events by default; clicks fall through to the parent `idle_button_` (a `LabelButton`). Our `ShouldRouteEventToTranslate` runs in `OnIdleButtonPressed` which receives the click. Confirmed by reading the existing `ShouldToggleUtilityPanelForEvent` pattern. Low risk.

6. **WebContents discard / tab switches.** `MahoSidebarSearchView` must re-attach the `TranslationObserver` when the active tab changes, not just on construction. The existing `ShouldToggleUtilityPanelForEvent` shows the view already has tab-aware logic. Verify in §5.0; pattern used by `TranslatePageActionController` is the gold reference.

7. **Bubble fallback for non-translate actions.** Other bubbles (zoom, memory saver, sharing) will resolve through `MahoToolbarButtonProvider::GetBubbleAnchor(std::nullopt)` → `idle_button_`. They will appear anchored to the pill but with default arrow direction — possibly visually awkward. This is acceptable for ship since the alternative (broken zero-bound anchor) is strictly worse. Per-bubble visual refinement is a follow-up.

8. **Element identifiers for UI tests.** `kTranslatePageActionElementId` is referenced in `chrome/browser/ui/views/translate/translate_page_action_interactive_uitest.cc` and others. Tests assume a specific view shape; with §2J in place the identifier moves from the upstream `PageActionView` to Maho's `secondary_label_` in Arc mode. **Mitigation:** Maho does not run upstream UI tests as part of CI (verify in §5.0). For UI tests that DO matter, the `secondary_label_` is a drawn, hit-testable view in Arc mode and should satisfy the same `ElementTracker` lookups; if any specific test still breaks, scope-tag the identifier with an Arc-only context tag or duplicate the identifier under a Maho-specific tag.

9. **Build-lock contention during dev.** `python3 maho-chromium/build/scripts/build_maho.py` serializes through `.build.lock`. Multi-iteration dev cycles will block. **Mitigation:** Standard; use `autoninja -C out/Default chrome` for narrower rebuilds when iterating on a single C++ file.

10. **§2C chromium_src override mechanism non-viable.** The override technique for skipping `ToolbarView`'s self-registration may not have a clean `#define` substitution surface, and adding even a minimal upstream-friendly seam may not be reviewable as a small overlay change. **Mitigation:** §5.0 validation gate forces a written go/no-go before any Must-ship work proceeds. No-go ⇒ route to §2.Contingency / §5.3-Contingency (translate-only path), which still ships the user-visible translate fix while leaving other bubbles' anchoring unchanged. Casual direct edits to upstream `toolbar_view.cc` are explicitly forbidden by repo policy and are not a fallback option. Acceptance: shipping §2.Contingency is preferred over either shipping nothing or violating the anti-pattern.

---

## 7. Verification & Success Criteria

**Path note:** verification depends on which path §5.0 produced. The §2C systemic path ships §2A–§2J + provider; the §2.Contingency path ships only §2E, §2F, §2G, §2J + direct translate-bubble anchoring. Functional and translate-specific non-regression criteria apply to both paths; the "other bubbles" criterion only applies on the systemic path.

### Functional (both paths)
- Foreign-language page in Arc mode → `secondary_label_` shows "Translation Available" within 2s of page load (matches upstream translate detection latency).
- Click on `secondary_label_` → translate bubble visible, anchored to the label, within 200ms.
- Click "Translate" in bubble → page translates, bubble closes, `secondary_label_` hides.
- Tab switch to English page → `secondary_label_` hides.
- Tab switch back to foreign page → `secondary_label_` reappears (state recomputed on tab change).
- Translate command via menu / shortcut → still works, bubble anchors to label (same provider path on §2C, same direct controller call on §2.Contingency since the menu shortcut also enters through `BrowserView::ShowTranslateBubble` — verify in §5.0 whether the shortcut path needs explicit handling on the contingency).

### Non-regression
- Non-Arc browser windows (popup, app, devtools): translate bubble anchors to upstream toolbar exactly as today.
- Sidebar pill behavior in non-translate cases: clicking primary label, leading icon, or empty pill area continues to open omnibox-edit mode (unchanged).
- Utility icon click → utility panel opens (unchanged).
- **§2C path only:** Other bubbles in Arc mode (zoom, memory saver, sharing hub, send-tab-to-self) anchor to the pill instead of window origin. Smoke pass: not visually broken.
- **§2.Contingency path only:** Other bubbles in Arc mode are explicitly *not* fixed; their behavior matches pre-plan state. Document this in the PR description and file the follow-up plan.

### Build / Tests
- `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
- `cd maho && bun run test` exits 0 (Rust workspace).
- New `maho_toolbar_button_provider_unittest` passes (§2C path only; on §2.Contingency, no provider exists and this test is not added).
- No new lint or type warnings.

### Code-health
- **§2C path:** `MahoToolbarButtonProvider` is the only `ToolbarButtonProvider` registered when `IsMahoArcLayoutActive()`. Confirmed via runtime log or test. The chromium_src overlay on `toolbar_view.cc` follows the existing overlay convention (no inline edits to upstream).
- **§2.Contingency path:** No chromium_src overlay on `toolbar_view.cc` exists; the `toolbar_view.cc` overlay-convention criterion is satisfied vacuously. Upstream `ToolbarView` remains the registered `ToolbarButtonProvider`.
- chromium_src patch on `TranslatePageActionController` (§2J) follows the overlay convention on both paths.
- No new `as any` / `@ts-ignore` / `unsafe { ... }` introduced.
- §5.0 grounding note (including the §2C go/no-go) is part of the implementation PR description.

---

## 8. Rollback Strategy

The full set of Must-ship items lands behind a single Maho feature flag `kMahoArcToolbarButtonProvider` (new, in `maho-chromium/browser/maho_features.{h,cc}`). The flag covers both shipping paths so rollback is uniform regardless of which §5.0 outcome was taken:

- **§2C systemic path (Must-ship 2A–2J):** flag enabled ⇒ `MahoToolbarButtonProvider` registers, `ToolbarView` self-registration is skipped, non-anchor virtuals delegate to `ToolbarView`, `kTranslatePageActionElementId` is deduplicated, translate observer/click wiring is live.
- **§2.Contingency translate-only path (2E, 2F, 2G, 2J + direct anchoring):** flag enabled ⇒ translate observer/click wiring is live, `kTranslatePageActionElementId` is deduplicated, click routes through the direct controller call. No provider, no `ToolbarView` overlay.

In both cases:

- Default: enabled.
- Disabled: provider does NOT register (systemic path) or direct anchoring is bypassed (contingency path); upstream `ToolbarView` registration runs as today; `MahoSidebarSearchView` falls back to literal `u"Translation Available"` and existing click routing. Effectively reverts to pre-fix behavior with one extra branch in two or three places.

Disable flag if any P0 regression appears in QA. Re-enable after fix. Remove flag after one full release with no incidents.

---

## 9. Decision Log

- **2026-05-29 — Selected hybrid (option 1) over Zen-mimic and over secondary_label-multi.** Rationale: secondary_label-as-state-text is more discoverable than Zen's hidden URL-bar icons; multi-state-text adds priority arbitration cost without proven UX benefit. Single contextual state text + utility panel for long-tail is the documented design direction.
- **2026-05-29 — Provider registration via §2C (upstream skip patch) over registration order.** Rationale: order-based wins are fragile against future upstream churn; explicit conditional skip mirrors the existing WebApp pattern at line 257-261. Caveat added in subsequent decision: §2C delivery mechanism is gated on §5.0 validation.
- **2026-05-29 — Translate is the only page action wired in this plan.** Rationale: scope discipline. Other actions follow the same architectural pattern but each carries UX text decisions and observer plumbing that warrants its own grounding pass.
- **2026-05-29 — `ToolbarButtonProvider` replacement remains the right architectural seam.** Rationale: §4.3 stands. The 25+ call-site chokepoint is the canonical place to absorb Arc's anchor breakage; future-proofing argument unchanged. The seam choice is not weakened by §5.0 potentially rejecting a specific delivery mechanism — that rejection is about chromium_src override technique, not about the seam.
- **2026-05-29 (review correction) — §2I (provider delegation to upstream `ToolbarView` for non-anchor virtuals) promoted from Optional to Must-ship.** Rationale: returning `nullptr` for a long list of toolbar accessors and relying on ASAN/CHECK to surface assumption violations later is not a shipping-grade approach. The upstream `ToolbarView` is still constructed in Arc mode; delegating to its existing accessors is cheap, transparent, and eliminates the entire class of `nullptr`-deref / `CHECK`-failure regressions in callers (autofill, IPH, extension dialogs, focus traversal, accessibility-pane traversal). The audit-then-decide approach was a planning shortcut that skipped doing the audit; flipping to "always delegate" makes the audit unnecessary for the default path.
- **2026-05-29 (review correction) — §2J (`kTranslatePageActionElementId` deduplication via chromium_src patch on `TranslatePageActionController`) promoted from Optional to Must-ship.** Rationale: §2G (set the identifier on `secondary_label_`) is required for `BrowserView::ShowTranslateBubble`'s `highlight_element` path and for upstream UI-test compatibility. `views::ElementTracker` tolerates duplicate identifiers in a widget scope but resolves lookups ambiguously / first-match, so leaving the upstream `PageActionView` to keep claiming the same identifier means `highlight_element` and tracker-driven flows can attach to the invisible upstream view instead of Maho's drawn label. The conflict is structural and known up-front, so the dedup patch is unconditional. §2J also lives on `TranslatePageActionController` (independent of `ToolbarView`), so it ships on both the §2C systemic path and the §2.Contingency translate-only path.
- **2026-05-29 (review correction) — §2C / §5.3 reframed as a §5.0 validation gate with concrete fallback.** Rationale: the original plan said "extend the existing WebApp branch... Maho extends the same conditional" without specifying the chromium_src override mechanism, implicitly hand-waving over the question of whether a clean overlay technique exists. The repo's anti-pattern policy explicitly prohibits casual direct upstream edits, so leaving the mechanism vague risked the implementer either inventing a non-reviewable patch or stalling. The corrected plan makes §5.0 produce a written go/no-go on the override mechanism (preferring `#define` overlay → minimal hook → no-go) and routes no-go to §2.Contingency. This converts a hand-wave into a concrete decision point with a defined fallback.
- **2026-05-29 (review correction) — Translate-only contingency path added (§2.Contingency / §5.3-Contingency).** Rationale: if §5.0 rejects the chromium_src override mechanism, the systemic provider replacement is blocked, but the user-visible translate fix (observer wiring, click routing, label state) is still independently shippable. Direct anchoring of the translate bubble via the controller-level entry point (rather than through `ToolbarButtonProvider`) lets us deliver the translate fix without changing other bubble behavior. This trades architectural ambition for shippability under the constraint that we will not violate the casual-direct-edit anti-pattern. The seam choice itself is not abandoned; only its delivery is deferred to a follow-up plan if the mechanism gate fails.
- **2026-05-29 (review correction) — Diagnosis (§1, §3) corrected to describe the full anchor chain.** Rationale: the original wording said `ToolbarView::GetBubbleAnchor` "returns `top_container`" as the fallback, which compressed three intermediate hops (`GetAnchorView` → `LocationBarView` page-action lookup → `IsDrawn()` check) into a single misleading step. The corrected diagnosis names the full path so that implementers and reviewers can locate every hop in upstream code, and so that the reasoning for replacing the provider (rather than patching one specific hop) is grounded in the full chain.

---

## 10. Out-of-band Notes

- The existing `address-bar-utility-panel-*` plans cover the *long-tail* page action surface. This plan does not contradict them; they extend the utility panel to host actions that don't fit `secondary_label_`. Once both ship, Maho's Arc-mode page-action coverage is structurally complete.
- The `bug` `crbug.com/404190582` referenced in `TranslatePageActionController::UpdatePageAction` (line 102-107 comment about the manager not updating the action synchronously) does not affect Maho's path — we drive `secondary_label_` directly from `LanguageState`, bypassing the `kActionShowTranslate` ActionItem's enabled state entirely.
