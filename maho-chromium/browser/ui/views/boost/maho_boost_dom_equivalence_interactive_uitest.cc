// Copyright 2026 Maho Browser. All rights reserved.

#include <string>
#include <string_view>

#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"

namespace {

class MahoBoostDomEquivalenceTest : public MahoBoostInteractiveUiTest {};

void ExpectNoDomContractFailures(content::WebContents* editor_web_contents,
                                 std::string_view contract_name,
                                 std::string_view script) {
  const content::EvalJsResult result =
      content::EvalJs(editor_web_contents, std::string(script));
  ASSERT_TRUE(result.is_ok())
      << contract_name << " DOM evaluation failed: " << result.ExtractError();
  EXPECT_EQ("[]", result.ExtractString())
      << contract_name << " live DOM contract failures: "
      << result.ExtractString();
}

constexpr char kBoostRouteContract[] = R"(
  (() => {
    const failures = [];
    const selector = value => document.querySelector(value);
    const visible = element => {
      if (!element || !element.isConnected || element.hidden ||
          element.getAttribute('aria-hidden') === 'true' ||
          element.closest('[inert]')) {
        return false;
      }
      const style = getComputedStyle(element);
      const rect = element.getBoundingClientRect();
      return style.display !== 'none' && style.visibility !== 'hidden' &&
          Number(style.opacity) !== 0 && rect.width > 0 && rect.height > 0;
    };
    const validateLiveUnique = (id, owner) => {
      const hookFailures = [];
      const elements = document.querySelectorAll(`#${CSS.escape(id)}`);
      if (elements.length !== 1) {
        hookFailures.push(`${id}: expected one live owner, found ${elements.length}`);
        return hookFailures;
      }
      const element = elements[0];
      if (!visible(element)) {
        hookFailures.push(`${id}: hook is hidden, inert, or has a zero rect`);
      }
      if (!element.closest(owner)) {
        hookFailures.push(`${id}: hook is not owned by ${owner}`);
      }
      return hookFailures;
    };
    const validateUniqueOwned = (id, owner) => {
      const hookFailures = [];
      const elements = document.querySelectorAll(`#${CSS.escape(id)}`);
      if (elements.length !== 1) {
        hookFailures.push(`${id}: expected one live owner, found ${elements.length}`);
        return hookFailures;
      }
      if (!elements[0].closest(owner)) {
        hookFailures.push(`${id}: hook is not owned by ${owner}`);
      }
      return hookFailures;
    };
    const validateInteractiveNoDrag = element => {
      if (getComputedStyle(element).getPropertyValue('-webkit-app-region').trim() === 'drag') {
        return [`${element.id || element.ariaLabel}: interactive control is draggable`];
      }
      return [];
    };
    window.__mahoBoostTest ??= {};
    window.__mahoBoostTest.validateLiveUnique = validateLiveUnique;
    window.__mahoBoostTest.validateInteractiveNoDrag = validateInteractiveNoDrag;
    const root = document.documentElement;
    if (root.id !== 'zenBoostWindow' || root.getAttribute('editor') !== 'boost') {
      failures.push('Boost root must be html#zenBoostWindow[editor="boost"]');
    }
    const boost = selector('#boost-editor.boost-mode');
    if (!visible(boost) || !boost.matches('main')) {
      failures.push('Boost main route must be visible and semantic');
    }
    const code = selector('#zen-boost-code-editor-root');
    if (code && (visible(code) || [...code.querySelectorAll(
        'button,[href],input,select,textarea,[tabindex]:not([tabindex="-1"])')]
        .some(element => !element.disabled && !element.hidden))) {
      failures.push('inactive Code route must not participate in visible focus traversal');
    }
    [
      ['boost-editor', '#boost-editor'],
      ['zen-boost-editor-root', '#boost-editor'],
      ['color-section', '#boost-editor'],
      ['color-wheel', '#color-section'],
      ['zen-boost-color-picker-dot-primary', '#color-wheel'],
      ['zen-boost-color-picker-dot-secondary', '#color-wheel'],
      ['typography-section', '#boost-editor'],
      ['font-grid', '#typography-section'],
      ['size-cycle-btn', '#typography-section'],
      ['case-cycle-btn', '#typography-section'],
      ['zap-section', '#boost-editor'],
      ['zen-boost-zap', '#zap-section'],
      ['zen-boost-code', '#zap-section'],
      ['advanced-color-toggle', '#color-section'],
      ['zen-boost-controls', '#advanced-color-toggle'],
      ['magic-theme-toggle', '#color-wheel'],
      ['zen-boost-magic-theme', '#magic-theme-toggle'],
      ['smart-invert-toggle', '#color-section'],
      ['zen-boost-invert', '#smart-invert-toggle'],
    ].forEach(([id, owner]) => failures.push(...validateLiveUnique(id, owner)));
    [
      ['dot-primary', '#zen-boost-color-picker-dot-primary'],
      ['dot-secondary', '#zen-boost-color-picker-dot-secondary'],
    ].forEach(([id, owner]) => failures.push(...validateUniqueOwned(id, owner)));
    const fontButtons = [...document.querySelectorAll('#font-grid .font-button')];
    if (fontButtons.length !== 15 || fontButtons.some(element => !visible(element))) {
      failures.push('font grid must expose 15 visible live font buttons');
    }
    document.querySelectorAll('button,input,select,textarea').forEach(element =>
        failures.push(...validateInteractiveNoDrag(element)));
    return JSON.stringify(failures);
  })()
)";

constexpr char kCodeRouteContract[] = R"(
  (() => {
    const failures = [];
    const visible = element => {
      if (!element || !element.isConnected || element.hidden ||
          element.getAttribute('aria-hidden') === 'true' || element.closest('[inert]')) {
        return false;
      }
      const style = getComputedStyle(element);
      const rect = element.getBoundingClientRect();
      return style.display !== 'none' && style.visibility !== 'hidden' &&
          Number(style.opacity) !== 0 && rect.width > 0 && rect.height > 0;
    };
    const requireLiveUnique = (id, owner) => {
      const elements = document.querySelectorAll(`#${CSS.escape(id)}`);
      if (elements.length !== 1) {
        failures.push(`${id}: expected one live owner, found ${elements.length}`);
        return;
      }
      const element = elements[0];
      if (!visible(element)) {
        failures.push(`${id}: hook is hidden, inert, or has a zero rect`);
      }
      if (!element.closest(owner)) {
        failures.push(`${id}: hook is not owned by ${owner}`);
      }
    };
    const root = document.documentElement;
    if (root.id !== 'zenBoostWindow' || root.getAttribute('editor') !== 'code') {
      failures.push('Code root must be html#zenBoostWindow[editor="code"]');
    }
    const code = document.querySelector('#zen-boost-code-editor-root.code-mode');
    if (!visible(code) || !code.matches('main')) {
      failures.push('Code main route must be visible and semantic');
    }
    const boost = document.querySelector('#boost-editor');
    if (boost && (visible(boost) || [...boost.querySelectorAll(
        'button,[href],input,select,textarea,[tabindex]:not([tabindex="-1"])')]
        .some(element => !element.disabled && !element.hidden))) {
      failures.push('inactive Boost route must not participate in visible focus traversal');
    }
    [
      ['zen-boost-code-editor-root', '#zen-boost-code-editor-root'],
      ['code-editor-container', '#zen-boost-code-editor-root'],
      ['zen-boost-code-editor', '#code-editor-container'],
      ['custom-css-editor', '#zen-boost-code-editor'],
      ['zen-boost-code-top-bar', '#code-editor-container'],
      ['zen-boost-back', '#zen-boost-code-top-bar'],
      ['zen-boost-code-bottom-bar', '#code-editor-container'],
      ['zen-boost-css-picker', '#zen-boost-code-bottom-bar'],
      ['zen-boost-css-inspector', '#zen-boost-code-bottom-bar'],
    ].forEach(([id, owner]) => requireLiveUnique(id, owner));
    const editor = document.querySelector('#custom-css-editor .cm-editor');
    if (!visible(editor) || !editor.querySelector('.cm-content')) {
      failures.push('CodeMirror must be a visible live descendant of #custom-css-editor');
    }
    return JSON.stringify(failures);
  })()
)";

constexpr char kFailureFixtureContract[] = R"(
  (() => {
    const violations = [];
    const validateLiveUnique = window.__mahoBoostTest?.validateLiveUnique;
    const validateInteractiveNoDrag =
        window.__mahoBoostTest?.validateInteractiveNoDrag;
    if (!validateLiveUnique || !validateInteractiveNoDrag) {
      return JSON.stringify(['live validator was not installed']);
    }
    const original = document.getElementById('zen-boost-zap');
    const duplicate = original.cloneNode(false);
    duplicate.id = original.id;
    document.body.append(duplicate);
    const hidden = document.createElement('button');
    hidden.id = 'hidden-required-hook';
    hidden.hidden = true;
    document.body.append(hidden);
    const draggable = document.createElement('button');
    draggable.id = 'draggable-control-fixture';
    draggable.style.webkitAppRegion = 'drag';
    document.body.append(draggable);
    violations.push(...validateLiveUnique('zen-boost-zap', '#zap-section'));
    violations.push(...validateLiveUnique('hidden-required-hook', 'body'));
    violations.push(...validateInteractiveNoDrag(draggable));
    duplicate.remove();
    hidden.remove();
    draggable.remove();
    return JSON.stringify(violations);
  })()
)";

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoBoostDomEquivalenceTest,
                       LiveUniqueVisibleStateBearingHooks) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  ExpectNoDomContractFailures(editor_web_contents, "Boost route",
                              kBoostRouteContract);
  ASSERT_TRUE(SwitchToCodeMode(editor_web_contents));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));
  ExpectNoDomContractFailures(editor_web_contents, "Code route",
                              kCodeRouteContract);
}

IN_PROC_BROWSER_TEST_F(MahoBoostDomEquivalenceTest,
                       RejectsDuplicateHiddenAndDraggableFailureFixtures) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  ExpectNoDomContractFailures(editor_web_contents, "Boost route",
                              kBoostRouteContract);

  const content::EvalJsResult result =
      content::EvalJs(editor_web_contents, kFailureFixtureContract);
  ASSERT_TRUE(result.is_ok()) << result.ExtractError();
  EXPECT_EQ("[\"zen-boost-zap: expected one live owner, found 2\","
            "\"hidden-required-hook: hook is hidden, inert, or has a zero rect\","
            "\"draggable-control-fixture: interactive control is draggable\"]",
            result.ExtractString())
      << "The live DOM validator must reject each intentional failure fixture.";
}
