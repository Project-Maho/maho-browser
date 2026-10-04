// Copyright 2026 Maho Browser. All rights reserved.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/test/ui_controls.h"
#include "ui/base/hit_test.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/widget/widget.h"

namespace {

class MahoBoostBehaviorTest : public MahoBoostInteractiveUiTest {
 protected:
  bool DragWindowFromPoint(const gfx::Point& start,
                           const gfx::Vector2d& delta);
  bool DragHandle(content::WebContents* editor_web_contents,
                  std::string_view selector,
                  const gfx::Vector2d& delta);
  bool SendKeyToHandle(content::WebContents* editor_web_contents,
                       std::string_view selector,
                       ui::KeyboardCode key,
                       bool shift = false);

};

std::optional<gfx::Point> ElementDragStartInScreen(
    content::WebContents* editor_web_contents,
    std::string_view selector) {
  if (!editor_web_contents) {
    return std::nullopt;
  }
  // At zero distance the handles overlap, so their shared center can hit the
  // other handle. Prefer an inset point that DOM hit-testing assigns to this
  // element. Inert descendants have no hit target, but unavailable tests still
  // send native input at their geometry to verify that callbacks stay blocked.
  const auto result = content::EvalJs(
      editor_web_contents,
      content::JsReplace(
          "(() => { const element = document.querySelector($1); "
          "if (!(element instanceof HTMLElement)) return []; "
          "const rect = element.getBoundingClientRect(); "
          "const centerX = rect.left + rect.width / 2; "
          "const centerY = rect.top + rect.height / 2; "
          "const points = [[rect.right - 2, centerY], "
          "[rect.left + 2, centerY], [centerX, rect.bottom - 2], "
          "[centerX, rect.top + 2], [centerX, centerY]]; "
          "const hit = points.find(([x, y]) => "
          "element.contains(document.elementFromPoint(x, y))); "
          "if (hit) return hit; "
          "return element.closest('[inert]') ? points[0] : []; })()",
          std::string(selector)));
  if (!result.is_ok()) {
    return std::nullopt;
  }
  const base::ListValue& drag_start = result.ExtractList();
  if (drag_start.size() != 2u) {
    return std::nullopt;
  }
  gfx::Point point = editor_web_contents->GetContainerBounds().origin();
  point.Offset(static_cast<int>(drag_start[0].GetDouble()),
               static_cast<int>(drag_start[1].GetDouble()));
  return point;
}

bool MahoBoostBehaviorTest::DragHandle(
    content::WebContents* editor_web_contents,
    std::string_view selector,
    const gfx::Vector2d& delta) {
  const std::optional<gfx::Point> start =
      ElementDragStartInScreen(editor_web_contents, selector);
  return start && DragWindowFromPoint(*start, delta);
}

bool MahoBoostBehaviorTest::DragWindowFromPoint(
    const gfx::Point& start,
    const gfx::Vector2d& delta) {
  views::Widget* widget = GetController().GetWidgetForTesting();
  if (!widget) {
    return false;
  }
  const gfx::Point end = start + delta;
  const gfx::NativeWindow window = widget->GetNativeWindow();
  base::RunLoop move_to_start_loop;
  if (!ui_controls::SendMouseMoveNotifyWhenDone(
          start.x(), start.y(), move_to_start_loop.QuitClosure(), window)) {
    return false;
  }
  move_to_start_loop.Run();
  base::RunLoop mouse_down_loop;
  if (!ui_controls::SendMouseEventsNotifyWhenDone(
          ui_controls::LEFT, ui_controls::DOWN,
          mouse_down_loop.QuitClosure(), ui_controls::kNoAccelerator,
          window)) {
    return false;
  }
  mouse_down_loop.Run();
  base::RunLoop drag_loop;
  if (!ui_controls::SendMouseMoveNotifyWhenDone(
          end.x(), end.y(), drag_loop.QuitClosure(), window)) {
    return false;
  }
  drag_loop.Run();
  base::RunLoop mouse_up_loop;
  if (!ui_controls::SendMouseEventsNotifyWhenDone(
          ui_controls::LEFT, ui_controls::UP, mouse_up_loop.QuitClosure(),
          ui_controls::kNoAccelerator, window)) {
    return false;
  }
  mouse_up_loop.Run();
  return true;
}

bool MahoBoostBehaviorTest::SendKeyToHandle(
    content::WebContents* editor_web_contents,
    std::string_view selector,
    ui::KeyboardCode key,
    bool shift) {
  views::Widget* widget = GetController().GetWidgetForTesting();
  if (!widget || !content::ExecJs(
                     editor_web_contents,
                     content::JsReplace(
                         "document.querySelector($1)?.focus()",
                         std::string(selector)))) {
    return false;
  }
  return ui_controls::SendKeyPress(widget->GetNativeWindow(), key, false,
                                   shift, false, false);
}

bool Click(content::WebContents* editor_web_contents,
           std::string_view selector) {
  const auto result = content::EvalJs(
      editor_web_contents,
      content::JsReplace(
          "(() => { const control = document.querySelector($1); "
          "if (!(control instanceof HTMLElement) || control.matches(':disabled')) "
          "return false; control.click(); return true; })()",
          std::string(selector)));
  return result.is_ok() && result.ExtractBool();
}

bool SetSliderWithKeyboard(content::WebContents* editor_web_contents,
                           std::string_view selector,
                           std::string_view key) {
  const auto result = content::EvalJs(
      editor_web_contents,
      content::JsReplace(
          "(() => { const control = document.querySelector($1)?.querySelector("
          "'[role=slider]'); if (!(control instanceof HTMLElement)) return false; "
          "control.focus(); control.dispatchEvent(new KeyboardEvent('keydown', "
          "{key: $2, bubbles: true})); return true; })()",
          std::string(selector), std::string(key)));
  return result.is_ok() && result.ExtractBool();
}

std::string ActiveBoostJsonForTarget(
    content::WebContents* target_web_contents) {
  MahoCore* core = maho::GetCore();
  if (!core || !target_web_contents) {
    return std::string();
  }
  const std::string domain(target_web_contents->GetLastCommittedURL().host());
  char* json = maho_core_boost_get_active(core, domain.c_str());
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest,
                       NeutralTitleStripPublishesNativeDragRegion) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);
  views::Widget* widget = GetController().GetWidgetForTesting();
  ASSERT_TRUE(widget);

  const content::EvalJsResult neutral_point = content::EvalJs(editor_wc, R"js(
    (() => {
      const surface = document.getElementById('zen-boost-name');
      if (!(surface instanceof HTMLElement)) return [];
      const rect = surface.getBoundingClientRect();
      const y = rect.top + rect.height / 2;
      for (const x of [rect.left + 2, rect.right - 2]) {
        const hit = document.elementFromPoint(x, y);
        if (!(hit instanceof HTMLElement) || !surface.contains(hit) ||
            hit.closest('button, input, select, textarea, [role="menu"]')) {
          continue;
        }
        return [
          x,
          y,
          getComputedStyle(hit).getPropertyValue('-webkit-app-region').trim(),
          hit.id,
        ];
      }
      return [];
    })()
  )js");
  ASSERT_TRUE(neutral_point.is_ok()) << neutral_point.ExtractError();
  const base::ListValue& point_data = neutral_point.ExtractList();
  ASSERT_EQ(4u, point_data.size());
  EXPECT_EQ("drag", point_data[2].GetString())
      << "Neutral title-strip hit target #" << point_data[3].GetString()
      << " must remain a native drag region";

  for (const char* selector :
       {"#zen-boost-close", "#zen-boost-name-container"}) {
    EXPECT_EQ("no-drag", content::EvalJs(
                             editor_wc,
                             content::JsReplace(
                                 "getComputedStyle(document.querySelector($1))"
                                 ".getPropertyValue('-webkit-app-region').trim()",
                                 selector))
                             .ExtractString())
        << selector << " must remain interactive inside the drag strip";
  }

  const gfx::Point widget_local_start(
      static_cast<int>(point_data[0].GetDouble()),
      static_cast<int>(point_data[1].GetDouble()));
  EXPECT_EQ(HTCAPTION,
            widget->GetNonClientComponent(widget_local_start));

  for (const char* selector :
       {"#zen-boost-close", "#zen-boost-name-container"}) {
    const content::EvalJsResult point_result = content::EvalJs(
        editor_wc,
        content::JsReplace(
            "(() => { const element = document.querySelector($1); "
            "if (!(element instanceof HTMLElement)) return []; "
            "const rect = element.getBoundingClientRect(); "
            "return [rect.left + rect.width / 2, "
            "rect.top + rect.height / 2]; })()",
            selector));
    ASSERT_TRUE(point_result.is_ok()) << point_result.ExtractError();
    const base::ListValue& point = point_result.ExtractList();
    ASSERT_EQ(2u, point.size());
    EXPECT_NE(HTCAPTION,
              widget->GetNonClientComponent(gfx::Point(
                  static_cast<int>(point[0].GetDouble()),
                  static_cast<int>(point[1].GetDouble()))))
        << selector << " must not initiate native window dragging";
  }
}

IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, ColorWheelHandlesEnterManualModeFromOffAndAuto) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);
  ASSERT_TRUE(content::EvalJs(editor_wc, R"js(
    (() => {
      const autoButton = document.getElementById('zen-boost-magic-theme');
      const primary = document.getElementById('zen-boost-color-picker-dot-primary');
      const secondary = document.getElementById('zen-boost-color-picker-dot-secondary');
      return autoButton?.textContent?.includes('Auto') === true &&
        autoButton?.getAttribute('aria-label') === 'Use automatic theme colors' &&
        autoButton?.querySelector('[data-icon-slot=sparkles]') !== null &&
        primary instanceof HTMLButtonElement &&
        secondary instanceof HTMLButtonElement &&
        document.querySelectorAll('#dot-primary').length === 1 &&
        document.querySelectorAll('#dot-secondary').length === 1;
    })()
  )js").ExtractBool());

  ASSERT_TRUE(Click(editor_wc, "#zen-boost-disable"));
  ASSERT_TRUE(WaitForCondition("Color Off persisted before primary drag", [
      this]() {
    return ActiveBoostJsonForTarget(GetTargetWebContents())
               .find("\"colorBoostEnabled\":false") != std::string::npos;
  }));
  const std::string primary_before =
      content::EvalJs(
          editor_wc,
          "document.getElementById('zen-boost-color-picker-dot-primary')"
          "?.getAttribute('aria-valuetext') ?? ''")
          .ExtractString();
  ASSERT_TRUE(DragHandle(editor_wc,
                         "#zen-boost-color-picker-dot-primary",
                         gfx::Vector2d(18, 12)));
  ASSERT_TRUE(WaitForCondition(
      "primary drag entered manual color mode and updated value",
      [this, editor_wc, primary_before]() {
        const std::string boost =
            ActiveBoostJsonForTarget(GetTargetWebContents());
        const auto value = content::EvalJs(
            editor_wc,
            "document.getElementById('zen-boost-color-picker-dot-primary')"
            "?.getAttribute('aria-valuetext') ?? ''");
        return boost.find("\"colorBoostEnabled\":true") != std::string::npos &&
               boost.find("\"magicTheme\":false") != std::string::npos &&
               value.is_ok() && value.ExtractString() != primary_before;
      }));

  ASSERT_TRUE(Click(editor_wc, "#zen-boost-magic-theme"));
  ASSERT_TRUE(
      WaitForCondition("Auto persisted before secondary drag", [this]() {
        return ActiveBoostJsonForTarget(GetTargetWebContents())
                   .find("\"magicTheme\":true") != std::string::npos;
      }));
  const std::string secondary_before =
      content::EvalJs(
          editor_wc,
          "document.getElementById('zen-boost-color-picker-dot-secondary')"
          "?.getAttribute('aria-valuetext') ?? ''")
          .ExtractString();
  ASSERT_TRUE(DragHandle(editor_wc, "#zen-boost-color-picker-dot-secondary",
                         gfx::Vector2d(-16, 10)));
  ASSERT_TRUE(WaitForCondition(
      "secondary drag exited Auto to manual and updated value",
      [this, editor_wc, secondary_before]() {
        const std::string boost =
            ActiveBoostJsonForTarget(GetTargetWebContents());
        const auto value = content::EvalJs(
            editor_wc,
            "document.getElementById('zen-boost-color-picker-dot-secondary')"
            "?.getAttribute('aria-valuetext') ?? ''");
        return boost.find("\"colorBoostEnabled\":true") != std::string::npos &&
               boost.find("\"magicTheme\":false") != std::string::npos &&
               value.is_ok() && value.ExtractString() != secondary_before;
      }));

  ASSERT_TRUE(Click(editor_wc, "#zen-boost-disable"));
  ASSERT_TRUE(SendKeyToHandle(editor_wc,
                              "#zen-boost-color-picker-dot-primary",
                              ui::VKEY_RIGHT));
  ASSERT_TRUE(WaitForCondition("primary Arrow entered manual mode", [this]() {
    return ActiveBoostJsonForTarget(GetTargetWebContents())
               .find("\"colorBoostEnabled\":true") != std::string::npos;
  }));
  ASSERT_TRUE(SendKeyToHandle(editor_wc,
                              "#zen-boost-color-picker-dot-primary",
                              ui::VKEY_HOME));
  EXPECT_TRUE(WaitForCondition("primary Home key settled at origin",
                               [editor_wc]() {
    const auto value = content::EvalJs(
        editor_wc,
        "document.getElementById('zen-boost-color-picker-dot-primary')"
        "?.getAttribute('aria-valuetext')?.includes("
        "'Hue 0 degrees, saturation 0 percent') === true");
    return value.is_ok() && value.ExtractBool();
  }));

  ASSERT_TRUE(Click(editor_wc, "#zen-boost-magic-theme"));
  ASSERT_TRUE(SendKeyToHandle(editor_wc,
                              "#zen-boost-color-picker-dot-secondary",
                              ui::VKEY_RIGHT, true));
  ASSERT_TRUE(WaitForCondition("secondary Shift+Arrow exited Auto", [this]() {
    const std::string boost = ActiveBoostJsonForTarget(GetTargetWebContents());
    return boost.find("\"colorBoostEnabled\":true") != std::string::npos &&
           boost.find("\"magicTheme\":false") != std::string::npos;
  }));
  ASSERT_TRUE(SendKeyToHandle(editor_wc,
                              "#zen-boost-color-picker-dot-secondary",
                              ui::VKEY_HOME));
  EXPECT_TRUE(WaitForCondition("secondary Home key settled at zero offset",
                               [editor_wc]() {
    const auto value = content::EvalJs(
        editor_wc,
        "document.getElementById('zen-boost-color-picker-dot-secondary')"
        "?.getAttribute('aria-valuetext')?.includes('offset 0 degrees') === true");
    return value.is_ok() && value.ExtractBool();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest,
                       ColorOffButtonUsesSelectedVisualState) {
  NavigateTo("/title1.html");
  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  ASSERT_TRUE(Click(editor_wc, "#zen-boost-disable"));
  ASSERT_TRUE(WaitForCondition(
      "color off button to expose the selected visual state", [editor_wc]() {
        const auto result = content::EvalJs(
            editor_wc,
            "(() => { const b = document.getElementById('zen-boost-disable');"
            "return b?.getAttribute('aria-pressed') === 'true' &&"
            "b.classList.contains('zen-boost-button-active') &&"
            "getComputedStyle(b).opacity === '1' &&"
            "getComputedStyle(b).backgroundColor === 'rgb(58, 58, 58)'; })()");
        return result.is_ok() && result.ExtractBool();
      }));
}


IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, ColorWheelGlobalUnavailableBlocksNativePointerAndKeyboard) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);
  const std::string before = ActiveBoostJsonForTarget(GetTargetWebContents());
  ASSERT_FALSE(before.empty());
  ASSERT_TRUE(content::ExecJs(editor_wc, R"js(
    (() => {
      const body = document.getElementById('zen-boost-filter-wrapper');
      if (!(body instanceof HTMLElement)) return false;
      body.inert = true;
      body.setAttribute('aria-disabled', 'true');
      body.dataset.interactionState = 'disabled';
      return true;
    })()
  )js"));
  ASSERT_TRUE(DragHandle(editor_wc,
                         "#zen-boost-color-picker-dot-primary",
                         gfx::Vector2d(20, 14)));
  EXPECT_TRUE(SendKeyToHandle(editor_wc,
                              "#zen-boost-color-picker-dot-secondary",
                              ui::VKEY_RIGHT));
  ASSERT_TRUE(content::EvalJs(editor_wc, R"js(
    new Promise(resolve => requestAnimationFrame(
      () => requestAnimationFrame(() => resolve(true))))
  )js").ExtractBool());
  EXPECT_EQ(before, ActiveBoostJsonForTarget(GetTargetWebContents()));
  ASSERT_TRUE(content::ExecJs(editor_wc, R"js(
    (() => {
      const body = document.getElementById('zen-boost-filter-wrapper');
      if (!(body instanceof HTMLElement)) return false;
      body.inert = false;
      body.removeAttribute('aria-disabled');
      body.dataset.interactionState = 'ready';
      return true;
    })()
  )js"));
}

IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, ColorWheelDuplicateCancelAndLostCaptureDiscardStaleFrames) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);
  ASSERT_TRUE(content::EvalJs(editor_wc, R"js(
    (async () => {
      const handle = document.getElementById('zen-boost-color-picker-dot-primary');
      if (!(handle instanceof HTMLButtonElement)) return false;
      const originalRequest = window.requestAnimationFrame.bind(window);
      const originalCancel = window.cancelAnimationFrame.bind(window);
      const originalSetCapture = handle.setPointerCapture.bind(handle);
      const originalHasCapture = handle.hasPointerCapture.bind(handle);
      const originalReleaseCapture = handle.releasePointerCapture.bind(handle);
      const callbacks = [];
      let cancelCount = 0;
      let releaseCount = 0;
      let nextFrame = 1;
      window.requestAnimationFrame = callback => {
        callbacks.push(callback);
        return nextFrame++;
      };
      window.cancelAnimationFrame = () => { cancelCount += 1; };
      handle.setPointerCapture = () => {};
      handle.hasPointerCapture = () => true;
      handle.releasePointerCapture = () => { releaseCount += 1; };
      const dispatch = (type, pointerId, x, y) => handle.dispatchEvent(
        new PointerEvent(type, {
          bubbles: true,
          clientX: x,
          clientY: y,
          pointerId,
        }));
      const beforeCancel = handle.getAttribute('aria-valuetext');
      dispatch('pointerdown', 51, 40, 40);
      dispatch('pointermove', 51, 92, 76);
      dispatch('pointercancel', 51, 92, 76);
      dispatch('lostpointercapture', 51, 92, 76);
      const cancelledCallbacks = callbacks.splice(0);
      cancelledCallbacks.forEach(callback => callback(performance.now()));
      const afterCancel = handle.getAttribute('aria-valuetext');

      const scheduledBeforeDuplicate = callbacks.length;
      dispatch('pointerdown', 52, 44, 44);
      dispatch('pointermove', 52, 100, 82);
      dispatch('pointerup', 52, 100, 82);
      dispatch('pointerup', 52, 100, 82);
      const duplicateCallbacks = callbacks.splice(0);
      const scheduledForDuplicate =
        duplicateCallbacks.length - scheduledBeforeDuplicate;
      duplicateCallbacks.forEach(callback => callback(performance.now()));

      window.requestAnimationFrame = originalRequest;
      window.cancelAnimationFrame = originalCancel;
      handle.setPointerCapture = originalSetCapture;
      handle.hasPointerCapture = originalHasCapture;
      handle.releasePointerCapture = originalReleaseCapture;
      await new Promise(resolve => originalRequest(
        () => originalRequest(resolve)));
      return beforeCancel === afterCancel &&
        scheduledForDuplicate === 1 &&
        cancelCount >= 2 &&
        releaseCount === 2 &&
        handle.dataset.dragging === 'false';
    })()
  )js").ExtractBool());
  ASSERT_TRUE(WaitForCondition("duplicate final events settle in manual mode", [
      this]() {
    const std::string boost = ActiveBoostJsonForTarget(GetTargetWebContents());
    return boost.find("\"colorBoostEnabled\":true") != std::string::npos &&
           boost.find("\"magicTheme\":false") != std::string::npos;
  }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, ColorWheelUnmountReleasesCaptureAndIgnoresStaleFinalFrame) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);
  const std::string before = ActiveBoostJsonForTarget(GetTargetWebContents());
  ASSERT_FALSE(before.empty());
  ASSERT_TRUE(content::ExecJs(editor_wc, R"js(
    (() => {
      const handle = document.getElementById('zen-boost-color-picker-dot-secondary');
      if (!(handle instanceof HTMLButtonElement)) return false;
      const originalRequest = window.requestAnimationFrame.bind(window);
      const originalCancel = window.cancelAnimationFrame.bind(window);
      const callbacks = [];
      let released = 0;
      let nextFrame = 1;
      window.requestAnimationFrame = callback => {
        callbacks.push(callback);
        return nextFrame++;
      };
      window.cancelAnimationFrame = () => {};
      handle.setPointerCapture = () => {};
      handle.hasPointerCapture = () => true;
      handle.releasePointerCapture = () => { released += 1; };
      handle.dispatchEvent(new PointerEvent('pointerdown', {
        bubbles: true, clientX: 50, clientY: 50, pointerId: 61,
      }));
      handle.dispatchEvent(new PointerEvent('pointermove', {
        bubbles: true, clientX: 106, clientY: 86, pointerId: 61,
      }));
      window.__mahoColorWheelCleanupProbe = {
        callbacks,
        released: () => released,
        restore: () => {
          window.requestAnimationFrame = originalRequest;
          window.cancelAnimationFrame = originalCancel;
        },
      };
      return true;
    })()
  )js"));
  ASSERT_TRUE(Click(editor_wc, "#zen-boost-code"));
  ASSERT_TRUE(WaitForCondition("ColorSection to unmount in Code mode", [
      editor_wc]() {
    const auto result = content::EvalJs(editor_wc,
        "document.getElementById('zen-boost-code-editor-root')?.hidden === false");
    return result.is_ok() && result.ExtractBool();
  }));
  ASSERT_TRUE(content::EvalJs(editor_wc, R"js(
    (() => {
      const probe = window.__mahoColorWheelCleanupProbe;
      if (!probe) return false;
      probe.restore();
      probe.callbacks.forEach(callback => callback(performance.now()));
      return probe.released() === 1;
    })()
  )js").ExtractBool());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(before, ActiveBoostJsonForTarget(GetTargetWebContents()));
}

IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, ColorAndTypographyFourBoundaryMatrix) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  ASSERT_TRUE(Click(editor_wc, "#zen-boost-disable"));
  ASSERT_TRUE(WaitForCoreBoostCss(":root { filter:"));
  ASSERT_TRUE(WaitForCondition(
      "Color Off persisted and removed target overlay", [this]() {
        const std::string boost =
            ActiveBoostJsonForTarget(GetTargetWebContents());
        const auto target = content::EvalJs(
            GetTargetWebContents(),
            "!(document.getElementById('maho-boost-style')?.textContent ?? '')"
            ".includes('html::after')");
        return boost.find("\"colorBoostEnabled\":false") != std::string::npos &&
               target.is_ok() && target.ExtractBool();
      }));
  ASSERT_TRUE(DragHandle(editor_wc, "#zen-boost-color-picker-dot-primary",
                         gfx::Vector2d(21, 17)));
  ASSERT_TRUE(SendKeyToHandle(editor_wc,
                              "#zen-boost-color-picker-dot-secondary",
                              ui::VKEY_RIGHT));
  ASSERT_TRUE(WaitForCoreBoostCss("saturate("));
  ASSERT_TRUE(WaitForTargetStyle("mix-blend-mode: multiply"));

  ASSERT_TRUE(Click(editor_wc, "#zen-boost-controls"));
  for (const auto& slider : std::array<std::string_view, 3>{
           "#zen-boost-color-contrast", "#zen-boost-color-brightness",
           "#zen-boost-color-saturation"}) {
    ASSERT_TRUE(SetSliderWithKeyboard(editor_wc, slider, "ArrowRight"));
  }
  ASSERT_TRUE(WaitForCoreBoostCss("brightness("));
  ASSERT_TRUE(WaitForTargetStyle("brightness("));
  ASSERT_TRUE(WaitForCoreBoostCss("contrast("));
  ASSERT_TRUE(WaitForTargetStyle("saturate("));

  ASSERT_TRUE(Click(
      editor_wc,
      "#zen-boost-font-grid button[aria-label='Courier New']"));
  ASSERT_TRUE(WaitForCoreBoostCss("font-family: Courier New"));
  ASSERT_TRUE(WaitForTargetStyle("font-family: Courier New"));
  ASSERT_TRUE(Click(editor_wc, "#zen-boost-case"));
  ASSERT_TRUE(WaitForCoreBoostCss("text-transform: uppercase"));
  ASSERT_TRUE(Click(editor_wc, "#zen-boost-size"));
  ASSERT_TRUE(WaitForCoreBoostCss(":root { zoom: 1.1000; }"));
  ASSERT_TRUE(WaitForTargetStyle(":root { zoom: 1.1000; }"));
  ASSERT_TRUE(WaitForCondition(
      "returned Boost reflects color and typography updates",
      [this]() {
        const std::string boost =
            ActiveBoostJsonForTarget(GetTargetWebContents());
        return boost.find("\"colorBoostEnabled\":true") !=
                   std::string::npos &&
               boost.find("\"fontFamily\":\"Courier New\"") !=
                   std::string::npos &&
               boost.find("\"caseMode\":\"upper\"") !=
                   std::string::npos &&
                boost.find("\"sizeMode\":\"k110\"") !=
                    std::string::npos;
      }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest,
                       ReorderedUpdatesDisableResetAndUnsafeCss) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  ASSERT_TRUE(Click(editor_wc, "#zen-boost-disable"));
  ASSERT_TRUE(WaitForCoreBoostCss(":root { filter:"));
  ASSERT_TRUE(WaitForCondition(
      "Color Off persisted and removed target overlay", [this]() {
        const std::string boost =
            ActiveBoostJsonForTarget(GetTargetWebContents());
        const auto target = content::EvalJs(
            GetTargetWebContents(),
            "!(document.getElementById('maho-boost-style')?.textContent ?? '')"
            ".includes('html::after')");
        return boost.find("\"colorBoostEnabled\":false") != std::string::npos &&
               target.is_ok() && target.ExtractBool();
      }));
  ASSERT_TRUE(DragHandle(editor_wc,
                         "#zen-boost-color-picker-dot-primary",
                         gfx::Vector2d(18, 12)));
  ASSERT_TRUE(WaitForCoreBoostCss("brightness("));
  ASSERT_TRUE(WaitForTargetStyle("mix-blend-mode: multiply"));
  ASSERT_TRUE(
      Click(editor_wc, "#zen-boost-font-grid button[aria-label='Georgia']"));
  ASSERT_TRUE(WaitForCoreBoostCss("font-family: Georgia"));
  ASSERT_TRUE(WaitForTargetStyle("font-family: Georgia"));
  ASSERT_TRUE(Click(editor_wc, "#zen-boost-magic-theme"));
  ASSERT_TRUE(WaitForCoreBoostCss("color-scheme"));
  ASSERT_TRUE(WaitForTargetStyle("color-scheme"));
  ASSERT_TRUE(Click(editor_wc, "#zen-boost-invert"));
  ASSERT_TRUE(WaitForCoreBoostCss("invert(1)"));
  ASSERT_TRUE(WaitForTargetStyle("hue-rotate(180deg)"));
  ASSERT_TRUE(Click(editor_wc, "#zen-boost-disable"));
  ASSERT_TRUE(WaitForCondition("disabled color DOM state and removed overlay", [
      this, editor_wc]() {
    const auto editor = content::EvalJs(editor_wc,
        "document.getElementById('zen-boost-disable')?.getAttribute('aria-pressed') === 'true'");
    const auto target = content::EvalJs(GetTargetWebContents(),
        "!(document.getElementById('maho-boost-style')?.textContent ?? '').includes('html::after')");
    return editor.is_ok() && editor.ExtractBool() && target.is_ok() && target.ExtractBool();
  }));
  ASSERT_TRUE(WaitForCoreBoostCss("font-family: Georgia"));
  ASSERT_TRUE(WaitForTargetStyle("font-family: Georgia"));
  ASSERT_TRUE(WaitForCondition(
      "returned Boost preserves typography while color is disabled",
      [this]() {
        const std::string boost =
            ActiveBoostJsonForTarget(GetTargetWebContents());
        return boost.find("\"colorBoostEnabled\":false") !=
                   std::string::npos &&
                boost.find("\"fontFamily\":\"Georgia\"") !=
                    std::string::npos;
      }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest, TypographyCaseSizeAndFontPersistAcrossReopen) {
  NavigateTo("/title1.html");

  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  for (const auto& font : std::array<std::string_view, 15>{
           "Arial", "Times New Roman", "Courier New", "Georgia",
           "Comic Sans MS", "Verdana", "Trebuchet MS", "Impact",
           "Palatino Linotype", "Tahoma", "Helvetica", "Garamond",
           "Century Gothic", "Arial Black", "Papyrus"}) {
    ASSERT_TRUE(Click(editor_wc,
                      base::StringPrintf("#zen-boost-font-grid button[aria-label='%s']",
                                         font.data())));
    ASSERT_TRUE(WaitForCoreBoostCss(base::StringPrintf("font-family: %s", font.data())));
    ASSERT_TRUE(WaitForTargetStyle(base::StringPrintf("font-family: %s", font.data())));
  }
  for (const auto& expected : std::array<std::string_view, 4>{
           "uppercase", "lowercase", "capitalize", "none"}) {
    ASSERT_TRUE(Click(editor_wc, "#zen-boost-case"));
    ASSERT_TRUE(WaitForCondition("returned Boost case mode", [
        editor_wc, expected]() {
      const auto result = content::EvalJs(editor_wc,
          "document.getElementById('zen-boost-case')?.getAttribute('case-mode')");
      return result.is_ok() && result.ExtractString() == expected;
    }));
  }
  for (const auto& expected : std::array<std::string_view, 5>{
           "110%", "125%", "150%", "90%", "100%"}) {
    ASSERT_TRUE(Click(editor_wc, "#zen-boost-size"));
    ASSERT_TRUE(WaitForCondition("returned Boost size mode", [
        editor_wc, expected]() {
      const auto result = content::EvalJs(editor_wc, R"js(
        (() => document.getElementById('zen-boost-size-value')?.style.display === 'none'
          ? '100%' : document.getElementById('zen-boost-size-value')?.textContent ?? '')()
      )js");
      return result.is_ok() && result.ExtractString() == expected;
    }));
  }
  ASSERT_TRUE(WaitForCoreBoostCss("font-family: Papyrus"));
  ASSERT_TRUE(WaitForTargetStyle("Text Format"));
  ASSERT_TRUE(WaitForCondition(
      "returned Boost reflects final typography values",
      [this]() {
        const std::string boost =
            ActiveBoostJsonForTarget(GetTargetWebContents());
        return boost.find("\"fontFamily\":\"Papyrus\"") !=
                   std::string::npos &&
               boost.find("\"caseMode\":\"none\"") !=
                   std::string::npos &&
                boost.find("\"sizeMode\":\"k100\"") !=
                    std::string::npos;
      }));
  HideBoostEditor();
  ASSERT_TRUE(WaitUntilHidden());
  editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);
  ASSERT_TRUE(WaitForCondition("persisted font returned after reopen", [
      editor_wc]() {
    const auto result = content::EvalJs(editor_wc,
        "document.querySelector('#zen-boost-font-grid button[aria-label=Papyrus]')?.getAttribute('aria-pressed') === 'true'");
    return result.is_ok() && result.ExtractBool();
  }));
}


// Regression: the handle must land under the pointer. The hook derived the
// input radius from the element's pixel size minus a fixed padding while the
// rendered position used a percentage locked to a 152px reference, so at any
// size other than that reference the handle trailed the cursor.
IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest,
                       ColorWheelHandleLandsUnderPointerAcrossWheelSizes) {
  NavigateTo("/title1.html");
  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  const auto result = content::EvalJs(editor_wc, R"js(
    (async () => {
      const wheel = document.getElementById('color-wheel');
      const handle = document.getElementById(
        'zen-boost-color-picker-dot-primary');
      if (!(wheel instanceof HTMLElement) ||
          !(handle instanceof HTMLElement)) {
        return null;
      }
      // The hook coalesces pointer input into one animation frame, so wait for
      // the frame that applies the value instead of guessing with a timeout.
      const nextFrame = () => new Promise(resolve => {
        requestAnimationFrame(() => requestAnimationFrame(resolve));
      });
      const offsets = [];
      for (const size of [152, 260]) {
        wheel.style.width = `${size}px`;
        wheel.style.height = `${size}px`;
        await nextFrame();
        const wheelRect = wheel.getBoundingClientRect();
        const centerX = wheelRect.left + wheelRect.width / 2;
        const centerY = wheelRect.top + wheelRect.height / 2;
        // Aim inside the reachable ring so clamping cannot mask the error.
        // The reachable radius is a fixed share of the element, matching the
        // percentage the handle is positioned with.
        const reachablePercent = (152 - 50) / 152 * 50;
        const reach = Math.min(wheelRect.width, wheelRect.height) *
          reachablePercent / 100 * 0.6;
        const targetX = centerX + reach;
        const targetY = centerY;
        const pointerId = 1;
        for (const type of ['pointerdown', 'pointermove', 'pointerup']) {
          handle.dispatchEvent(new PointerEvent(type, {
            bubbles: true, pointerId, clientX: targetX, clientY: targetY,
          }));
        }
        await nextFrame();
        const handleRect = handle.getBoundingClientRect();
        const handleX = handleRect.left + handleRect.width / 2;
        const handleY = handleRect.top + handleRect.height / 2;
        offsets.push(Math.hypot(handleX - targetX, handleY - targetY));
      }
      return offsets;
    })()
  )js");
  ASSERT_TRUE(result.is_ok());
  const base::ListValue& offsets = result.ExtractList();
  ASSERT_EQ(2u, offsets.size());
  for (const base::Value& offset : offsets) {
    ASSERT_TRUE(offset.is_double() || offset.is_int());
    const double distance = offset.GetDouble();
    // A few pixels of rounding is fine; trailing the cursor is not.
    EXPECT_LT(distance, 6.0)
        << "handle settled " << distance << "px away from the pointer";
  }
}

// Regression: at distance 0 both handles sit on the wheel center. The smaller
// secondary handle used to stack above the primary one, so pressing the visible
// center circle grabbed the secondary handle, which only rotates the hue offset
// and therefore appeared completely stuck.
IN_PROC_BROWSER_TEST_F(MahoBoostBehaviorTest,
                       ColorWheelCenterPressGrabsThePrimaryHandle) {
  NavigateTo("/title1.html");
  content::WebContents* editor_wc = OpenEditorAndWait();
  ASSERT_TRUE(editor_wc);

  const auto result = content::EvalJs(editor_wc, R"js(
    (() => {
      const wheel = document.getElementById('color-wheel');
      const primary = document.getElementById(
        'zen-boost-color-picker-dot-primary');
      const secondary = document.getElementById(
        'zen-boost-color-picker-dot-secondary');
      if (!(wheel instanceof HTMLElement) ||
          !(primary instanceof HTMLElement) ||
          !(secondary instanceof HTMLElement)) {
        return null;
      }
      const primaryRect = primary.getBoundingClientRect();
      const secondaryRect = secondary.getBoundingClientRect();
      const centerX = primaryRect.left + primaryRect.width / 2;
      const centerY = primaryRect.top + primaryRect.height / 2;
      // Confirm the handles really do overlap, otherwise the assertion below
      // would pass for the wrong reason.
      const overlapping =
        Math.hypot(
          secondaryRect.left + secondaryRect.width / 2 - centerX,
          secondaryRect.top + secondaryRect.height / 2 - centerY) < 2;
      const hit = document.elementFromPoint(centerX, centerY);
      return {
        overlapping,
        hitsPrimary: primary === hit || primary.contains(hit),
        hitId: hit instanceof HTMLElement ? hit.id : '',
      };
    })()
  )js");
  ASSERT_TRUE(result.is_ok());
  const base::DictValue& state = result.ExtractDict();
  EXPECT_TRUE(state.FindBool("overlapping").value_or(false))
      << "handles are expected to coincide at distance 0";
  const std::string* hit_id = state.FindString("hitId");
  ASSERT_TRUE(hit_id);
  EXPECT_TRUE(state.FindBool("hitsPrimary").value_or(false))
      << "center press landed on '" << *hit_id << "' instead of the primary handle";
}


}  // namespace
