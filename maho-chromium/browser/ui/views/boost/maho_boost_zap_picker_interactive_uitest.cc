// Copyright 2026 Maho Browser. All rights reserved.

#include <array>
#include <set>
#include <string>
#include <string_view>

#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/net/maho_boost_injection_handler.h"
#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"
#include "ui/base/test/ui_controls.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

class MahoBoostZapPickerInteractiveUiTest
    : public MahoBoostInteractiveUiTest {
 public:
  bool IsMode(content::WebContents* target_web_contents, const char* mode) {
    const std::optional<base::Value> result = EvaluateInIsolatedWorld(
        target_web_contents,
        content::JsReplace("window.__mahoBoost?.currentMode === $1", mode));
    return result && result->is_bool() && result->GetBool();
  }

  bool HasOverlay(content::WebContents* target_web_contents) {
    const std::optional<base::Value> result = EvaluateInIsolatedWorld(
        target_web_contents,
        "Boolean(document.querySelector('[data-maho-boost-zap]'))");
    return result && result->is_bool() && result->GetBool();
  }

  bool HasZapSelector(content::WebContents* target_web_contents,
                      std::string_view selector) {
    const std::optional<base::Value> result = EvaluateInIsolatedWorld(
        target_web_contents,
        content::JsReplace(
            "window.__mahoBoost?.getZapSelectors().includes($1) === true",
            std::string(selector)));
    return result && result->is_bool() && result->GetBool();
  }

  size_t ZapSelectorCount(content::WebContents* target_web_contents) {
    const std::optional<base::Value> result = EvaluateInIsolatedWorld(
        target_web_contents,
        "window.__mahoBoost?.getZapSelectors().length ?? 0");
    return result && result->is_int() ? static_cast<size_t>(result->GetInt())
                                     : 0u;
  }

  std::optional<gfx::Point> GetFirstUnzapButtonPointInViewport(
      content::WebContents* target_web_contents) {
    const std::optional<base::Value> result = EvaluateInIsolatedWorld(
        target_web_contents,
        "Math.round(window.innerHeight - 35)");
    if (!result || !result->is_int()) {
      return std::nullopt;
    }
    constexpr int kFirstUnzapButtonCenterX = 36;
    return gfx::Point(kFirstUnzapButtonCenterX, result->GetInt());
  }

  bool ClickRendererAt(content::WebContents* target_web_contents,
                       const gfx::Point& point) {
    content::SimulateMouseClickAt(target_web_contents, 0,
                                  blink::WebMouseEvent::Button::kLeft, point);
    base::RunLoop().RunUntilIdle();
    return true;
  }

  bool MoveRendererMouseTo(content::WebContents* target_web_contents,
                           const gfx::Point& point) {
    content::SimulateMouseEvent(target_web_contents,
                                blink::WebInputEvent::Type::kMouseMove, point);
    base::RunLoop().RunUntilIdle();
    return true;
  }

  bool DispatchRecognizerSelection(content::WebContents* target_web_contents,
                                   std::string_view target_selector,
                                   bool confirm) {
    const std::optional<base::Value> result = EvaluateInIsolatedWorld(
        target_web_contents,
        content::JsReplace(R"js(
          (() => {
            const target = document.querySelector($1);
            if (!(target instanceof Element)) return false;
            const bounds = target.getBoundingClientRect();
            const init = {
              bubbles: true,
              cancelable: true,
              composed: true,
              clientX: bounds.left + Math.max(1, bounds.width / 2),
              clientY: bounds.top + Math.max(1, bounds.height / 2),
            };
            target.dispatchEvent(new PointerEvent('pointerdown', init));
            target.dispatchEvent(new PointerEvent('pointermove', init));
            target.dispatchEvent(new MouseEvent('mousemove', init));
            target.dispatchEvent(new MouseEvent('click', init));
            if ($2) {
              document.dispatchEvent(new KeyboardEvent('keydown', {
                key: 'Enter', bubbles: true, cancelable: true, composed: true,
              }));
            }
            return true;
          })()
        )js",
                           std::string(target_selector), confirm));
    return result && result->is_bool() && result->GetBool();
  }

  bool ConfirmRecognizerSelection(content::WebContents* target_web_contents) {
    const std::optional<base::Value> result = EvaluateInIsolatedWorld(
        target_web_contents,
        "document.dispatchEvent(new KeyboardEvent('keydown', {"
        "key: 'Enter', bubbles: true, cancelable: true, composed: true})); true");
    return result && result->is_bool() && result->GetBool();
  }

  bool MoveMouseTo(const gfx::Point& point) {
    if (!ui_controls::SendMouseMove(point.x(), point.y())) {
      return false;
    }
    base::RunLoop().RunUntilIdle();
    return true;
  }

  bool ClickMouseAt(const gfx::Point& point) {
    if (!MoveMouseTo(point) ||
        !ui_controls::SendMouseClick(ui_controls::LEFT)) {
      return false;
    }
    base::RunLoop().RunUntilIdle();
    return true;
  }

  bool ClickEnabledEditorControl(content::WebContents* editor_web_contents,
                                 std::string_view selector) {
    const std::string selector_string(selector);
    if (!WaitForCondition(
            base::StringPrintf("%s to be enabled", selector_string.c_str()),
            [editor_web_contents, selector_string]() {
              const auto result = content::EvalJs(
                  editor_web_contents, content::JsReplace(R"js(
                    (() => {
                      const control = document.querySelector($1);
                      return Boolean(control) &&
                        control.disabled !== true &&
                        control.getAttribute('aria-busy') !== 'true' &&
                        !control.closest('[inert]');
                    })()
                  )js", selector_string));
              return result.is_ok() && result.ExtractBool();
            })) {
      return false;
    }
    const auto result = content::EvalJs(
        editor_web_contents, content::JsReplace(R"js(
          (() => {
            const control = document.querySelector($1);
            if (!control || control.disabled === true) return false;
            control.click();
            return true;
          })()
        )js", selector_string));
    return result.is_ok() && result.ExtractBool();
  }

  bool HandlerModeIs(
      content::WebContents* target_web_contents,
      MahoBoostInjectionHandler::ContentScriptMode expected_mode) {
    auto* handler =
        MahoBoostInjectionHandler::FromWebContents(target_web_contents);
    return handler && handler->content_script_mode() == expected_mode;
  }

  bool HandlerPollingStopped(content::WebContents* target_web_contents) {
    auto* handler =
        MahoBoostInjectionHandler::FromWebContents(target_web_contents);
    return !handler ||
           (handler->content_script_mode() ==
                MahoBoostInjectionHandler::ContentScriptMode::kNone &&
            !handler->poll_timer_is_running_for_testing() &&
            !handler->poll_in_flight_for_testing());
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest, RequiredWiringIsPresent) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-zap').click()"));
  ASSERT_TRUE(WaitForCondition(
      "Zap content-script wiring to expose its isolated-world API",
      [this, target_web_contents]() {
        const std::optional<base::Value> result = EvaluateInIsolatedWorld(
            target_web_contents,
            "typeof window.__mahoBoost?.enterZapMode === 'function' && "
            "typeof window.__mahoBoost?.drain === 'function'");
        return result && result->is_bool() && result->GetBool();
      }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest,
                       ZapActivationFocusesTargetWithoutPageClick) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(editor_web_contents->GetRenderWidgetHostView());

  GetController().GetWidgetForTesting()->Activate();
  editor_web_contents->Focus();
  ASSERT_TRUE(WaitForCondition(
      "Boost editor renderer to receive focus before entering Zap",
      [editor_web_contents]() {
        return editor_web_contents->GetRenderWidgetHostView()->HasFocus();
      }));

  ASSERT_TRUE(ClickEnabledEditorControl(editor_web_contents,
                                        "#zen-boost-zap"));
  ASSERT_TRUE(WaitForCondition(
      "Zap content script to enter isolated-world mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "zap");
      }));
  ASSERT_TRUE(target_web_contents->GetRenderWidgetHostView());
  EXPECT_TRUE(WaitForCondition(
      "target page renderer to receive focus without a page click",
      [target_web_contents]() {
        return target_web_contents->GetRenderWidgetHostView()->HasFocus();
      }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest,
                       RelatedControlChangesSelectionScopeAcrossItsWidth) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(ClickEnabledEditorControl(editor_web_contents,
                                        "#zen-boost-zap"));
  ASSERT_TRUE(WaitForCondition(
      "selector recognizer scripts to load",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "zap");
      }));

  const std::optional<base::Value> result = EvaluateInIsolatedWorld(
      target_web_contents, R"js(
        (() => {
          const fixture = document.createElement('section');
          fixture.innerHTML = `
            <article class="related-card" data-card="first">
              <button class="related-action"><span id="related-target" class="related-label">One</span></button>
              <small class="related-note">First</small>
            </article>
            <article class="related-card" data-card="second">
              <button class="related-action"><span class="related-label">Two</span></button>
              <small class="related-note">Second</small>
            </article>
            <article class="related-card" data-card="third">
              <button class="related-action"><span class="related-label">Three</span></button>
              <small class="related-note">Third</small>
            </article>
            <article class="related-card" data-card="fourth">
              <button class="related-action">
                <span class="related-label">Four</span>
                <span class="related-badge">new</span>
              </button>
              <small class="related-note">Fourth</small>
            </article>
            <aside class="related-side">
              <span class="related-label">Aside</span>
            </aside>`;
          document.body.appendChild(fixture);

          const component = new window.__mahoBoost.SelectorComponent(
            document, [], () => {});
          component.initialize();
          component.setState(
            window.__mahoBoost.SelectorComponent.STATES.SELECTED,
            document.getElementById('related-target'));

          const related = component.shadowRoot?.getElementById('select-related');
          const preview = component.shadowRoot?.getElementById(
            'selector-element-preview-text');
          if (!(related instanceof HTMLInputElement) || !preview) return null;

          const rect = related.getBoundingClientRect();
          const snapshots = [];
          related.dispatchEvent(new MouseEvent('mouseenter', {
            bubbles: true, clientX: rect.left + 1,
          }));
          // Sample every relation level across the control's width so a level
          // that silently collapses onto its neighbour is caught.
          for (let level = 1; level <= 7; level += 1) {
            const fraction = (level - 1) / 6;
            related.dispatchEvent(new MouseEvent('mousemove', {
              bubbles: true,
              clientX: rect.left + rect.width * fraction,
            }));
            const selector = preview.getAttribute('data-selector');
            snapshots.push({
              selector,
              text: preview.textContent,
              matchCount: selector
                ? document.querySelectorAll(selector).length
                : 0,
            });
          }
          component.tearDown();
          fixture.remove();
          return snapshots;
        })()
      )js");
  ASSERT_TRUE(result && result->is_list());
  const auto& snapshots = result->GetList();
  ASSERT_EQ(7u, snapshots.size());
  std::set<std::string> selectors;
  std::set<int> match_counts;
  for (const base::Value& snapshot : snapshots) {
    ASSERT_TRUE(snapshot.is_dict());
    const std::string* selector = snapshot.GetDict().FindString("selector");
    const std::optional<int> match_count =
        snapshot.GetDict().FindInt("matchCount");
    ASSERT_TRUE(match_count.has_value());
    // A level whose scope has no safe candidate legitimately reports no
    // selector; levels that do resolve must expose a real match set.
    if (!selector || selector->empty()) {
      continue;
    }
    EXPECT_GT(*match_count, 0) << "level selector: " << *selector;
    selectors.insert(*selector);
    match_counts.insert(*match_count);
  }
  std::string observed;
  for (const base::Value& snapshot : snapshots) {
    const std::string* selector = snapshot.GetDict().FindString("selector");
    const std::optional<int> match_count =
        snapshot.GetDict().FindInt("matchCount");
    observed += "[" + (selector ? *selector : std::string("<none>")) + " x" +
                base::NumberToString(match_count.value_or(0)) + "]";
  }
  // Zen exposes seven distinct relation levels; the ported control must offer
  // several genuinely different scopes rather than one cached policy.
  EXPECT_GE(selectors.size(), 4u) << observed;
  EXPECT_GE(match_counts.size(), 3u) << observed;
}

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest,
                       ZapUnzapEscapeAndPickerInsertRoundTrip) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  ASSERT_TRUE(content::ExecJs(
      target_web_contents,
      "document.body.insertAdjacentHTML('beforeend', "
      "'<button id=\"maho-zap-target\" class=\"maho-zap-target\">zap target</button>')"));

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(ClickEnabledEditorControl(editor_web_contents, "#zen-boost-zap"));
  ASSERT_TRUE(WaitForCondition(
      "Zap content script to enter isolated-world mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "zap");
      }));
  ASSERT_TRUE(WaitForCondition(
      "Zap overlay to attach to the target document",
      [this, target_web_contents]() {
        return HasOverlay(target_web_contents);
      }));

  ASSERT_TRUE(DispatchRecognizerSelection(
      target_web_contents, ".maho-zap-target", true));
  ASSERT_TRUE(WaitForTargetVisibility(".maho-zap-target", false));
  ASSERT_TRUE(WaitForCoreBoostCss("maho-zap-target"));

  const std::optional<gfx::Point> unzap_center =
      GetFirstUnzapButtonPointInViewport(target_web_contents);
  ASSERT_TRUE(unzap_center.has_value());
  ASSERT_TRUE(ClickRendererAt(target_web_contents, *unzap_center));
  ASSERT_TRUE(WaitForTargetVisibility(".maho-zap-target", true));

  ASSERT_TRUE(ClickEnabledEditorControl(editor_web_contents, "#zen-boost-zap"));
  ASSERT_TRUE(WaitForCondition(
      "Zap exit to remove isolated-world overlay",
      [this, target_web_contents]() {
        return !IsMode(target_web_contents, "zap") &&
               !HasOverlay(target_web_contents);
      }));

  ASSERT_TRUE(ClickEnabledEditorControl(editor_web_contents, "#zen-boost-code"));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));
  ASSERT_TRUE(
      ClickEnabledEditorControl(editor_web_contents, "#zen-boost-css-picker"));
  ASSERT_TRUE(WaitForCondition(
      "picker content script to enter isolated-world mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "picker");
      }));
  const std::optional<base::Value> generated_selector = EvaluateInIsolatedWorld(
      target_web_contents, R"js(
        (() => {
          const target = document.querySelector('.maho-zap-target');
          if (!(target instanceof Element) || !window.__mahoBoost) return '';
          const component = new window.__mahoBoost.SelectorComponent(
            document, [], () => {});
          const selector = component.getSelectionPath(document, 0, target) || '';
          component.tearDown();
          return selector;
        })()
      )js");
  ASSERT_TRUE(generated_selector && generated_selector->is_string());
  const std::string selected_selector = generated_selector->GetString();
  ASSERT_FALSE(selected_selector.empty());
  LOG(INFO) << "MAHO_BOOST_ZAP_PICKER_SELECTED selector=" << selected_selector;
  ASSERT_TRUE(DispatchRecognizerSelection(
      target_web_contents, ".maho-zap-target", false));
  EXPECT_TRUE(IsMode(target_web_contents, "picker"));
  ASSERT_TRUE(ConfirmRecognizerSelection(target_web_contents));
  ASSERT_TRUE(WaitForCondition(
      "picker to append the selected CSS selector once",
      [editor_web_contents, selected_selector]() {
        const auto result = content::EvalJs(
            editor_web_contents,
            content::JsReplace(
                "window.__mahoBoostTest?.readCss().includes($1) ?? false",
                selected_selector));
        return result.is_ok() && result.ExtractBool();
      }));
  ASSERT_TRUE(FlushCodeMirror(editor_web_contents));
  ASSERT_TRUE(WaitForCondition(
      "picker completion to leave isolated-world mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "none");
      }));

  ASSERT_TRUE(
      ClickEnabledEditorControl(editor_web_contents, "#zen-boost-css-picker"));
  ASSERT_TRUE(WaitForCondition(
      "second picker entry to be active",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "picker");
      }));
  ASSERT_TRUE(EvaluateInIsolatedWorld(
      target_web_contents,
      "document.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', "
      "bubbles: true}))"));
  ASSERT_TRUE(WaitForCondition(
      "Escape to remove picker listeners and mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "none");
      }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest,
                       NavigationAndRepeatedModeTeardown) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  for (int iteration = 0; iteration != 2; ++iteration) {
    ASSERT_TRUE(
        ClickEnabledEditorControl(editor_web_contents, "#zen-boost-zap"));
    ASSERT_TRUE(WaitForCondition(
        "repeated Zap entry to become active",
        [this, target_web_contents]() {
          return HandlerModeIs(
              target_web_contents,
              MahoBoostInjectionHandler::ContentScriptMode::kZap);
        }));
    ASSERT_TRUE(
        ClickEnabledEditorControl(editor_web_contents, "#zen-boost-zap"));
    ASSERT_TRUE(WaitForCondition(
        "repeated Zap exit to tear down its overlay",
        [this, target_web_contents]() {
          return HandlerPollingStopped(target_web_contents);
        }));
  }

  ASSERT_TRUE(ClickEnabledEditorControl(editor_web_contents, "#zen-boost-zap"));
  ASSERT_TRUE(WaitForCondition(
      "Zap mode before navigation teardown",
      [this, target_web_contents]() {
        return HandlerModeIs(
            target_web_contents,
            MahoBoostInjectionHandler::ContentScriptMode::kZap);
      }));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  ASSERT_TRUE(WaitUntilHidden());
  ASSERT_TRUE(WaitUntilDestroyed());
  EXPECT_EQ(nullptr, GetController().GetWidgetForTesting());
  auto* handler =
      MahoBoostInjectionHandler::FromWebContents(GetTargetWebContents());
  EXPECT_TRUE(!handler || handler->content_script_mode() ==
                               MahoBoostInjectionHandler::ContentScriptMode::kNone);
  EXPECT_TRUE(!handler || !handler->poll_timer_is_running_for_testing());
  EXPECT_TRUE(!handler || !handler->poll_in_flight_for_testing());
}

IN_PROC_BROWSER_TEST_F(
    MahoBoostZapPickerInteractiveUiTest,
    NestedSvgUsesSemanticButtonStableDataAndSuppressesPointerDown) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  ASSERT_TRUE(content::ExecJs(target_web_contents, R"js(
    (() => {
      window.mahoZapPointerDownCount = 0;
      document.body.insertAdjacentHTML('beforeend', `
        <button data-action="checkout" type="button">
          <span><svg viewBox="0 0 10 10"><path id="nested-zap-path" d="M0 0h10v10z"></path></svg></span>
        </button>
      `);
      document.querySelector('[data-action="checkout"]')?.addEventListener(
        'pointerdown', () => { window.mahoZapPointerDownCount += 1; });
      return true;
    })()
  )js"));

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-zap').click()"));
  ASSERT_TRUE(WaitForCondition(
      "semantic nested target Zap mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "zap");
      }));

  ASSERT_TRUE(DispatchRecognizerSelection(target_web_contents,
                                          "#nested-zap-path", false));
  EXPECT_FALSE(HasZapSelector(target_web_contents,
                              "[data-action=\"checkout\"]"));
  const auto page_pointerdowns = content::EvalJs(
      target_web_contents, "window.mahoZapPointerDownCount");
  ASSERT_TRUE(page_pointerdowns.is_ok());
  EXPECT_EQ(0, page_pointerdowns.ExtractInt());

  ASSERT_TRUE(ConfirmRecognizerSelection(target_web_contents));
  ASSERT_TRUE(WaitForCoreBoostCss("[data-action=\"checkout\"]"));
  ASSERT_TRUE(WaitForTargetVisibility("[data-action=\"checkout\"]", false));
}

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest,
                       StableCandidateOrderingRelatedAndStructuralFallback) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-zap').click()"));
  ASSERT_TRUE(WaitForCondition(
      "selector recognizer scripts to load",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "zap");
      }));

  const std::optional<base::Value> result = EvaluateInIsolatedWorld(
      target_web_contents, R"js(
        (() => {
          const root = document.createElement('div');
          root.innerHTML = `
            <button id="stable-id" data-action="ignored" aria-label="Ignored" class="ignored">ID</button>
            <button data-action="save">Data</button>
            <button aria-label="Close">Aria</button>
            <a href="/docs">Docs</a>
            <div class="card featured">Class</div>
            <article data-card="offer">One</article>
            <article data-card="offer">Two</article>
            <article data-card="offer">Three</article>
            <section data-region="hero"><span class="title">Hero</span></section>
            <section data-region="other"><span class="title">Other</span></section>
          `;
          document.body.appendChild(root);
          const component = new window.__mahoBoost.SelectorComponent(
            document, [], () => {});
          const selectors = [
            component.getSelectionPath(document, 0, root.querySelector('#stable-id')),
            component.getSelectionPath(document, 0, root.querySelector('[data-action="save"]')),
            component.getSelectionPath(document, 0, root.querySelector('[aria-label="Close"]')),
            component.getSelectionPath(document, 0, root.querySelector('a')),
            component.getSelectionPath(document, 0, root.querySelector('.card')),
            // Zen level 1 widens to the target's exact parent, level 4 to the
            // target's own element type.
            component.getSelectionPath(document, 1, root.querySelector('[data-card="offer"]')),
            component.getSelectionPath(document, 4, root.querySelector('[data-card="offer"]')),
            component.getSelectionPath(document, 0, root.querySelector('[data-region="hero"] .title')),
          ];
          root.remove();
          return selectors;
        })()
      )js");
  ASSERT_TRUE(result && result->is_list());
  const auto& selectors = result->GetList();
  ASSERT_EQ(8u, selectors.size());
  EXPECT_EQ("#stable-id", selectors[0].GetString());
  EXPECT_EQ("[data-action=\"save\"]", selectors[1].GetString());
  EXPECT_EQ("[aria-label=\"Close\"]", selectors[2].GetString());
  EXPECT_EQ("[href=\"/docs\"]", selectors[3].GetString());
  EXPECT_EQ("div.card.featured", selectors[4].GetString());
  // Level 1 resolves the parent container, not the hovered article itself.
  EXPECT_EQ("div", selectors[5].GetString());
  // Level 4 resolves every element sharing the target's type.
  EXPECT_EQ("article", selectors[6].GetString());
  EXPECT_EQ("[data-region=\"hero\"] > span.title",
            selectors[7].GetString());
}

IN_PROC_BROWSER_TEST_F(
    MahoBoostZapPickerInteractiveUiTest,
    RemovedZeroBroadFailWhileEscapedAndStructuralFallbackSelect) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-zap').click()"));
  ASSERT_TRUE(WaitForCondition(
      "failure-state selector scripts to load",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "zap");
      }));
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-zap').click()"));
  ASSERT_TRUE(WaitForCondition(
      "standalone selector scenarios to own event listeners",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "none");
      }));
  const std::optional<base::Value> result = EvaluateInIsolatedWorld(
      target_web_contents, R"js(
        (() => {
          const runScenario = (kind) => {
            const fixture = document.createElement('div');
            fixture.id = `fixture-${kind}`;
            const target = document.createElement('span');
            if (kind === 'removed') target.id = 'removed-before-confirm';
            if (kind === 'zero') target.id = 'zero-before-confirm';
            if (kind === 'invalid') target.id = '123';
            if (kind === 'broad') target.className = 'broad-target';
            if (kind === 'unsafe') target.className = 'css-abcdef123456';
            fixture.appendChild(target);
            document.body.appendChild(fixture);

            const statuses = [];
            const selected = [];
            const component = new window.__mahoBoost.SelectorComponent(
              document,
              [],
              (selector) => selected.push(selector),
              undefined,
              (status) => statuses.push(status.code));
            component.initialize();
            const eventHandler = (event) => component.handleEvent(event, false);
            document.addEventListener('click', eventHandler, true);
            document.addEventListener('keydown', eventHandler, true);
            target.dispatchEvent(new MouseEvent('click', {
              bubbles: true, cancelable: true, composed: true,
            }));

            if (kind === 'removed') target.remove();
            if (kind === 'zero') target.id = 'zero-after-rerender';
            if (kind === 'broad') {
              const duplicate = document.createElement('span');
              duplicate.className = 'broad-target';
              fixture.appendChild(duplicate);
            }
            document.dispatchEvent(new KeyboardEvent('keydown', {
              key: 'Enter', bubbles: true, cancelable: true, composed: true,
            }));
            document.removeEventListener('click', eventHandler, true);
            document.removeEventListener('keydown', eventHandler, true);
            component.tearDown();
            fixture.remove();
            return [statuses.at(-1) ?? '', selected.length];
          };

          return [
            ...runScenario('removed'),
            ...runScenario('zero'),
            ...runScenario('invalid'),
            ...runScenario('broad'),
            ...runScenario('unsafe'),
          ];
        })()
      )js");
  ASSERT_TRUE(result && result->is_list());
  const auto& outcomes = result->GetList();
  ASSERT_EQ(10u, outcomes.size());
  const std::array<std::string_view, 5> expected_statuses = {
      "removed_target", "zero_match", "ready", "over_broad_selector",
      "ready"};
  const std::array<int, 5> expected_selection_counts = {0, 0, 1, 0, 1};
  for (size_t index = 0; index < expected_statuses.size(); ++index) {
    EXPECT_EQ(expected_statuses[index], outcomes[index * 2].GetString());
    EXPECT_EQ(expected_selection_counts[index],
              outcomes[index * 2 + 1].GetInt());
  }
  EXPECT_EQ(0u, ZapSelectorCount(target_web_contents));
}

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest,
                       UnzapHoverUsesCanonicalUnhideAndRestores) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  ASSERT_TRUE(content::ExecJs(
      target_web_contents,
      "document.body.insertAdjacentHTML('beforeend', "
      "'<button id=\"canonical-unhide-target\">Hide me</button>')"));
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-zap').click()"));
  ASSERT_TRUE(WaitForCondition(
      "canonical unhide Zap mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "zap");
      }));
  ASSERT_TRUE(DispatchRecognizerSelection(
      target_web_contents, "#canonical-unhide-target", true));
  ASSERT_TRUE(WaitForTargetVisibility("#canonical-unhide-target", false));

  const std::optional<gfx::Point> unzap_center =
      GetFirstUnzapButtonPointInViewport(target_web_contents);
  ASSERT_TRUE(unzap_center.has_value());
  ASSERT_TRUE(MoveRendererMouseTo(target_web_contents, *unzap_center));
  ASSERT_TRUE(WaitForCondition(
      "unzap hover to apply canonical temporary-unhide attribute",
      [target_web_contents]() {
        const auto result = content::EvalJs(
            target_web_contents,
            "document.getElementById('canonical-unhide-target')?."
            "hasAttribute('maho-zap-unhide') === true");
        return result.is_ok() && result.ExtractBool();
      }));
  ASSERT_TRUE(WaitForTargetVisibility("#canonical-unhide-target", true));

  ASSERT_TRUE(MoveRendererMouseTo(target_web_contents, gfx::Point(300, 300)));
  ASSERT_TRUE(WaitForCondition(
      "unzap hover exit to restore hidden state and remove temporary attribute",
      [target_web_contents]() {
        const auto result = content::EvalJs(
            target_web_contents,
            "document.getElementById('canonical-unhide-target')?."
            "hasAttribute('maho-zap-unhide') === false");
        return result.is_ok() && result.ExtractBool();
      }));
  ASSERT_TRUE(WaitForTargetVisibility("#canonical-unhide-target", false));
}

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest,
                       PickerNestedTargetUsesRecognizerBeforeInsert) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  ASSERT_TRUE(content::ExecJs(target_web_contents, R"js(
    document.body.insertAdjacentHTML('beforeend', `
      <button data-picker="headline"><span id="picker-nested-span">Pick</span></button>
    `)
  )js"));
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-code').click()"));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.getElementById('zen-boost-css-picker').click()"));
  ASSERT_TRUE(WaitForCondition(
      "nested target picker mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "picker");
      }));

  ASSERT_TRUE(DispatchRecognizerSelection(
      target_web_contents, "#picker-nested-span", false));
  EXPECT_TRUE(IsMode(target_web_contents, "picker"));
  ASSERT_TRUE(ConfirmRecognizerSelection(target_web_contents));
  ASSERT_TRUE(WaitForCondition(
      "stable picker selector to insert after explicit confirmation",
      [editor_web_contents]() {
        const auto result = content::EvalJs(
            editor_web_contents,
            "window.__mahoBoostTest?.readCss().includes("
            "'[data-picker=\"headline\"]') ?? false");
        return result.is_ok() && result.ExtractBool();
      }));
  ASSERT_TRUE(WaitForCondition(
      "picker confirmation to tear down isolated-world mode",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "none");
      }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostZapPickerInteractiveUiTest,
                       ZapPollingCadenceSingleFlightImmediateDrainAndTeardown) {
  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-zap').click()"));
  ASSERT_TRUE(WaitForCondition(
      "Zap mode to start timer and immediate drain",
      [this, target_web_contents]() {
        return IsMode(target_web_contents, "zap");
      }));

  auto* handler =
      MahoBoostInjectionHandler::FromWebContents(target_web_contents);
  ASSERT_TRUE(handler);
  EXPECT_EQ(MahoBoostInjectionHandler::ContentScriptMode::kZap,
            handler->content_script_mode());

  for (int sample = 0; sample != 10; ++sample) {
    const std::string selector =
        base::StringPrintf("#maho-zap-timing-sample-%d", sample);
    ASSERT_TRUE(content::ExecJs(
        target_web_contents,
        content::JsReplace(
            "document.body.insertAdjacentHTML('beforeend', "
            "'<button id=\"' + $1 + '\">timing</button>')",
            base::StringPrintf("maho-zap-timing-sample-%d", sample))));
    const base::TimeTicks started_at = base::TimeTicks::Now();
    ASSERT_TRUE(DispatchRecognizerSelection(
        target_web_contents, selector, true));
    ASSERT_TRUE(WaitForTargetVisibility(selector, false));
    const base::TimeDelta elapsed = base::TimeTicks::Now() - started_at;
    LOG(INFO) << "MAHO_BOOST_ZAP_TIMING_SAMPLE enqueue_to_hide_ms="
              << elapsed.InMillisecondsF() << " selector=" << selector;
  }

  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-zap').click()"));
  ASSERT_TRUE(WaitForCondition(
      "Zap mode exit to tear down polling timer",
      [this, target_web_contents]() {
        return HandlerPollingStopped(target_web_contents);
      }));
  EXPECT_EQ(MahoBoostInjectionHandler::ContentScriptMode::kNone,
            handler->content_script_mode());
}
