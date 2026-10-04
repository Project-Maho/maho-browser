// Copyright 2026 Maho Browser. All rights reserved.

#include <string>
#include <string_view>

#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"

namespace {

class MahoBoostVisualAccessibilityTest : public MahoBoostInteractiveUiTest {
 public:
  void CaptureTask4StateSnapshot(content::WebContents* editor_web_contents,
                                 std::string_view state);
  void CaptureTodo13StateSnapshot(content::WebContents* editor_web_contents,
                                  std::string_view state);
};

const gfx::Size kTask4CaptureSize(184, 582);
const gfx::Size kTodo13CaptureSize(452, 582);

std::string JsonStringField(std::string_view serialized,
                            std::string_view field) {
  const std::string marker = "\"" + std::string(field) + "\":\"";
  const size_t value_start = serialized.find(marker);
  if (value_start == std::string_view::npos) {
    return std::string();
  }
  const size_t content_start = value_start + marker.size();
  const size_t value_end = serialized.find('"', content_start);
  return value_end == std::string_view::npos
             ? std::string()
             : std::string(serialized.substr(content_start,
                                             value_end - content_start));
}

std::string CreateBoostForDomain(MahoCore* core,
                                 std::string_view domain,
                                 std::string_view name) {
  std::string domain_value(domain);
  std::string name_value(name);
  char* boost_json =
      maho_core_boost_create(core, domain_value.c_str(), name_value.c_str());
  if (!boost_json) {
    return std::string();
  }
  const std::string serialized(boost_json);
  maho_string_free(boost_json);
  return JsonStringField(serialized, "id");
}

std::string ActiveBoostIdForDomain(MahoCore* core, std::string_view domain) {
  std::string domain_value(domain);
  char* boost_json = maho_core_boost_get_active(core, domain_value.c_str());
  if (!boost_json) {
    return std::string();
  }
  const std::string serialized(boost_json);
  maho_string_free(boost_json);
  return JsonStringField(serialized, "id");
}

bool OpenBoostChooser(content::WebContents* editor_web_contents) {
  const content::EvalJsResult result =
      content::EvalJs(editor_web_contents, R"js(
    (async () => {
      const trigger = document.getElementById('zen-boost-name-container');
      if (!(trigger instanceof HTMLButtonElement) || trigger.disabled) return false;
      const nextFrame = () => new Promise(resolve => requestAnimationFrame(resolve));
      const waitForState = async predicate => {
        if (predicate()) return true;
        for (let frame = 0; frame < 30; ++frame) {
          await nextFrame();
          if (predicate()) return true;
        }
        return false;
      };
      if (trigger.getAttribute('aria-expanded') !== 'true') {
        if (document.getElementById('zenBoostContextMenu') &&
            !await waitForState(
                () => !document.getElementById('zenBoostContextMenu'))) {
          return false;
        }
        trigger.dispatchEvent(new KeyboardEvent('keydown', {
          bubbles: true, cancelable: true, code: 'ArrowDown', key: 'ArrowDown',
        }));
      }
      return waitForState(() =>
        trigger.getAttribute('aria-expanded') === 'true' &&
        document.getElementById('zenBoostContextMenu')?.dataset.state === 'open');
    })()
  )js");
  return result.is_ok() && result.ExtractBool();
}

void MahoBoostVisualAccessibilityTest::CaptureTask4StateSnapshot(
    content::WebContents* editor_web_contents,
    std::string_view state) {
  const content::EvalJsResult accessibility_snapshot = content::EvalJs(
      editor_web_contents,
      content::JsReplace("window.__mahoBoostTest?.captureTodo13State($1) ?? ''",
                         state));
  ASSERT_TRUE(accessibility_snapshot.is_ok())
      << "Unable to collect the " << state << " chooser AX state hook: "
      << accessibility_snapshot.ExtractError();
  EXPECT_NE(std::string::npos,
            accessibility_snapshot.ExtractString().find(std::string(state)))
      << "The chooser AX state hook must label the " << state << " snapshot.";
  EXPECT_TRUE(CaptureEditorScreenshot(
      editor_web_contents, kTask4CaptureSize,
      download_directory().AppendASCII("task-4-" + std::string(state) +
                                       ".png")))
      << "Unable to capture Todo 4 chooser state: " << state;
}

void MahoBoostVisualAccessibilityTest::CaptureTodo13StateSnapshot(
    content::WebContents* editor_web_contents,
    std::string_view state) {
  const content::EvalJsResult accessibility_snapshot = content::EvalJs(
      editor_web_contents,
      content::JsReplace("window.__mahoBoostTest?.captureTodo13State($1) ?? ''",
                         state));
  ASSERT_TRUE(accessibility_snapshot.is_ok())
      << "Unable to collect the " << state << " AX state hook: "
      << accessibility_snapshot.ExtractError();
  EXPECT_NE(std::string::npos,
            accessibility_snapshot.ExtractString().find(std::string(state)))
      << "The Todo 13 AX state hook must label the " << state << " snapshot.";
  EXPECT_TRUE(CaptureEditorScreenshot(
      editor_web_contents, kTodo13CaptureSize,
      download_directory().AppendASCII(std::string(state) + ".png")))
      << "Unable to capture Todo 13 state: " << state;
}

void ExpectNoVisualAccessibilityFailures(
    content::WebContents* editor_web_contents,
    std::string_view contract_name,
    std::string_view script) {
  const content::EvalJsResult result =
      content::EvalJs(editor_web_contents, std::string(script));
  ASSERT_TRUE(result.is_ok())
      << contract_name << " evaluation failed: " << result.ExtractError();
  EXPECT_EQ("[]", result.ExtractString())
      << contract_name << " visual/accessibility failures: "
      << result.ExtractString();
}

constexpr char kTitleStripContract[] = R"(
  (() => {
    const failures = [];
    const visible = element => {
      const rect = element?.getBoundingClientRect();
      const style = element && getComputedStyle(element);
      return Boolean(element && !element.hidden && style.display !== 'none' &&
          style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0);
    };
    const button = (selector, name) => {
      const element = document.querySelector(selector);
      if (!visible(element) || element.tagName !== 'BUTTON' ||
          element.getAttribute('aria-label') !== name) {
        failures.push(`${selector} must be a visible button named ${name}`);
      }
      return element;
    };
    const title = document.querySelector('#zen-boost-head-wrapper');
    if (!visible(title) || title.tagName !== 'HEADER' ||
        Math.round(title.getBoundingClientRect().height) !== 40 ||
        getComputedStyle(title).getPropertyValue('-webkit-app-region').trim() !== 'drag') {
      failures.push('title strip must be a visible 40px draggable header');
    }
    button('#zen-boost-close', 'Close Boost editor');
    const name = button('#zen-boost-name-container',
                        `${document.querySelector('#zen-boost-name-text')?.textContent} Boost actions`);
    if (document.querySelector('#zen-boost-shuffle')) {
      failures.push('title strip must not duplicate actions that already live in the Boost actions menu');
    }
    if (name && getComputedStyle(name).getPropertyValue('-webkit-app-region').trim() !== 'no-drag') {
      failures.push('Boost action trigger must opt out of the drag region');
    }
    if ([...title.querySelectorAll('button')].some(element =>
        getComputedStyle(element).getPropertyValue('-webkit-app-region').trim() !== 'no-drag')) {
      failures.push('every title-strip button must compute to no-drag');
    }
    if (name?.getAttribute('aria-expanded') !== 'false' ||
        document.getElementById('zenBoostContextMenu')) {
      failures.push('closed Boost chooser must be absent and expose collapsed trigger state');
    }
    const editor = document.getElementById('boost-editor');
    const editorRect = editor?.getBoundingClientRect();
    if (!editorRect || Math.round(editorRect.width) !== 184 ||
        Math.round(editorRect.height) !== 582) {
      failures.push('Boost editor must retain its exact 184x582 geometry');
    }
    return JSON.stringify(failures);
  })()
)";

constexpr char kBoostLucideContract[] = R"MAHOJS(
  (() => {
    const failures = [];
    const visible = element => {
      const rect = element?.getBoundingClientRect();
      const style = element && getComputedStyle(element);
      return Boolean(element && !element.hidden && style.display !== 'none' &&
          style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0);
    };
    const iconControl = (selector, expectedName, pressed = false) => {
      const control = document.querySelector(selector);
      if (!visible(control) || control.tagName !== 'BUTTON') {
        failures.push(`${selector} must be a visible icon-bearing button`);
        return null;
      }
      const accessibleName = control.getAttribute('aria-label') ||
          control.textContent?.trim();
      if (!accessibleName || (expectedName && accessibleName !== expectedName)) {
        failures.push(`${selector} must preserve its accessible name`);
      }
      if (pressed && !['true', 'false'].includes(
          control.getAttribute('aria-pressed'))) {
        failures.push(`${selector} must expose its pressed state`);
      }
      const icons = control.querySelectorAll(':scope > svg.boost-control-icon');
      if (icons.length !== 1) {
        failures.push(`${selector} must contain exactly one inline Lucide SVG`);
        return control;
      }
      const iconStyle = getComputedStyle(icons[0]);
      if (iconStyle.display === 'none' || iconStyle.visibility === 'hidden' ||
          Number(iconStyle.opacity) === 0) {
        failures.push(`${selector} Lucide SVG must remain visible`);
      }
      const before = getComputedStyle(control, '::before');
      const webkitMask = before.getPropertyValue('-webkit-mask-image').trim();
      if (!['none', 'normal'].includes(before.content) ||
          before.maskImage !== 'none' ||
          (webkitMask !== '' && webkitMask !== 'none')) {
        failures.push(`${selector} must not retain a pseudo-element icon mask`);
      }
      if (control.getAttribute('aria-busy') === 'true' &&
          !icons[0].classList.contains('animate-spin')) {
        failures.push(`${selector} busy state must expose the Loader2 spinner`);
      }
      control.focus();
      const focusedIconStyle = getComputedStyle(icons[0]);
      if (focusedIconStyle.display === 'none' ||
          focusedIconStyle.visibility === 'hidden' ||
          Number(focusedIconStyle.opacity) === 0) {
        failures.push(`${selector} Lucide SVG must remain visible on focus`);
      }
      return control;
    };

    const root = document.getElementById('boost-editor');
    const rootRect = root?.getBoundingClientRect();
    if (!rootRect || Math.round(rootRect.width) !== 184 ||
        Math.round(rootRect.height) !== 582) {
      failures.push('Lucide migration must retain Boost 184x582 geometry');
    }
    iconControl('#zen-boost-close', 'Close Boost editor');
    iconControl('#zen-boost-name-container',
        `${document.querySelector('#zen-boost-name-text')?.textContent} Boost actions`);
    const auto = iconControl(
        '#zen-boost-magic-theme', 'Use automatic theme colors', true);
    iconControl('#zen-boost-invert', 'Smart Invert Colors', true);
    iconControl('#zen-boost-controls', 'Advanced Color Controls');
    iconControl('#zen-boost-disable', 'Disable Color Adjustments', true);
    iconControl('#zen-boost-zap', null, true);
    iconControl('#zen-boost-code', 'Open Code editor');

    const wheel = document.getElementById('color-wheel');
    const wheelRect = wheel?.getBoundingClientRect();
    const autoRect = auto?.getBoundingClientRect();
    if (!wheelRect || !autoRect || auto?.textContent?.trim() !== 'Auto' ||
        Math.round(autoRect.height) !== 24 ||
        Math.abs(autoRect.left + autoRect.width / 2 -
                 (wheelRect.left + wheelRect.width / 2)) > 1 ||
        Math.abs(autoRect.top - wheelRect.top - 12) > 1) {
      failures.push('Sparkles + Auto must retain its fixed top-center wheel geometry');
    }
    return JSON.stringify(failures);
  })()
)MAHOJS";

constexpr char kBoostChooserContract[] = R"js(
  (() => {
    const failures = [];
    const visible = element => {
      const rect = element?.getBoundingClientRect();
      const style = element && getComputedStyle(element);
      return Boolean(element && !element.hidden && style.display !== 'none' &&
          style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0);
    };
    const menu = document.getElementById('zenBoostContextMenu');
    if (!visible(menu) || menu.getAttribute('role') !== 'menu' ||
        menu.getAttribute('aria-label') !== 'Boosts and actions') {
      failures.push('open Boost chooser must be a visible named menu');
      return JSON.stringify(failures);
    }
    const menuRect = menu.getBoundingClientRect();
    if (menuRect.left < 8 || menuRect.right > innerWidth - 8 ||
        menuRect.top < 8 || menuRect.bottom > innerHeight - 8 ||
        menuRect.width > 168) {
      failures.push('Boost chooser must remain bounded inside the 184px viewport');
    }
    if (getComputedStyle(menu).getPropertyValue('-webkit-app-region').trim() !== 'no-drag') {
      failures.push('Boost chooser must opt out of the title drag region');
    }
    const siteToggle = document.getElementById('zen-boost-site-toggle');
    if (!visible(siteToggle) || siteToggle.getAttribute('role') !== 'menuitemcheckbox' ||
        siteToggle.getAttribute('aria-label') !== 'Apply Boost on this site' ||
        !['true', 'false'].includes(siteToggle.getAttribute('aria-checked')) ||
        !/SiteBoosts(On|Off)/.test(siteToggle.textContent.replace(/\s/g, ''))) {
      failures.push('site toggle must expose visible name, checked state, and On or Off text');
    }
    const list = document.getElementById('zen-boost-list');
    const rows = [...document.querySelectorAll(
        '#zen-boost-list [role="menuitemradio"][data-boost-id]')];
    const ids = rows.map(row => row.dataset.boostId);
    if (!visible(list) || list.getAttribute('aria-label') !== 'Boost to edit' ||
        rows.length === 0 || rows.some(row => !visible(row)) ||
        ids.some(id => !id) || new Set(ids).size !== rows.length) {
      failures.push('chooser must expose each visible Boost row exactly once');
    }
    const selectedRows = rows.filter(row => row.dataset.selected === 'true');
    if (selectedRows.length !== 1 ||
        selectedRows[0].getAttribute('aria-checked') !== 'true' ||
        !selectedRows[0].getAttribute('aria-label')?.includes('selected for editing')) {
      failures.push('chooser must expose exactly one checked selected Boost row');
    }
    const activeRows = rows.filter(row => row.dataset.active === 'true');
    const siteEnabled = siteToggle?.getAttribute('aria-checked') === 'true';
    if ((siteEnabled && activeRows.length !== 1) ||
        (!siteEnabled && activeRows.length !== 0) ||
        activeRows.some(row => row.getAttribute('aria-current') !== 'true' ||
            !row.getAttribute('aria-label')?.includes('active on this site'))) {
      failures.push('site checked state and active Boost row must agree');
    }
    if (!siteEnabled && !selectedRows[0].textContent.includes('Selected')) {
      failures.push('Off chooser must visibly distinguish the selected editing row');
    }
    const actionIds = ['zen-boost-edit-rename', 'zen-boost-edit-shuffle',
      'zen-boost-edit-reset', 'zen-boost-load', 'zen-boost-save',
      'zen-boost-edit-delete'];
    const actions = actionIds.map(id => document.getElementById(id));
    const lastRow = rows.at(-1);
    if (actions.some(action => !visible(action)) || !lastRow ||
        actions.some(action => lastRow.compareDocumentPosition(action) !==
            Node.DOCUMENT_POSITION_FOLLOWING) ||
        menu.querySelectorAll('[role="separator"]').length < 3) {
      failures.push('selected Boost actions must remain below chooser separators');
    }
    return JSON.stringify(failures);
  })()
)js";

constexpr char kBoostChooserLucideContract[] = R"MAHOJS(
  (() => {
    const failures = [];
    const menu = document.getElementById('zenBoostContextMenu');
    const menuRect = menu?.getBoundingClientRect();
    if (!menuRect || menu.offsetWidth !== 168 ||
        menuRect.left < 8 || menuRect.right > innerWidth - 8) {
      failures.push('Lucide action menu must retain its fixed 168px bounded geometry');
      return JSON.stringify(failures);
    }
    for (const id of ['zen-boost-edit-rename', 'zen-boost-edit-shuffle',
      'zen-boost-edit-reset', 'zen-boost-load', 'zen-boost-save',
      'zen-boost-edit-delete']) {
      const control = document.getElementById(id);
      const rect = control?.getBoundingClientRect();
      if (!rect || control.offsetHeight !== 28 ||
          !control.textContent?.trim()) {
        failures.push(`#${id} must retain its fixed menu geometry and AX name`);
        continue;
      }
      const icons = control.querySelectorAll(':scope > svg.boost-control-icon');
      if (icons.length !== 1) {
        failures.push(`#${id} must contain exactly one inline Lucide SVG`);
      }
      const before = getComputedStyle(control, '::before');
      const webkitMask = before.getPropertyValue('-webkit-mask-image').trim();
      if (!['none', 'normal'].includes(before.content) ||
          before.maskImage !== 'none' ||
          (webkitMask !== '' && webkitMask !== 'none')) {
        failures.push(`#${id} must not retain a pseudo-element icon mask`);
      }
    }
    return JSON.stringify(failures);
  })()
)MAHOJS";

constexpr char kBoostChooserKeyboardContract[] = R"js(
  (async () => {
    const failures = [];
    const trigger = document.getElementById('zen-boost-name-container');
    const nextFrame = () => new Promise(resolve => requestAnimationFrame(resolve));
    const waitForState = async predicate => {
      if (predicate()) return true;
      for (let frame = 0; frame < 30; ++frame) {
        await nextFrame();
        if (predicate()) return true;
      }
      return false;
    };
    const key = value => document.activeElement?.dispatchEvent(new KeyboardEvent(
        'keydown', {bubbles: true, cancelable: true, code: value, key: value}));
    const escape = () => document.activeElement?.dispatchEvent(
        new KeyboardEvent('keydown', {bubbles: true, key: 'Escape'}));
    if (!(trigger instanceof HTMLButtonElement)) {
      return JSON.stringify(['Boost chooser trigger must exist for keyboard navigation']);
    }
    escape();
    await waitForState(() =>
      trigger.getAttribute('aria-expanded') === 'false' &&
      !document.getElementById('zenBoostContextMenu'));
    trigger.focus();
    trigger.dispatchEvent(new KeyboardEvent('keydown', {
      bubbles: true, cancelable: true, code: 'ArrowDown', key: 'ArrowDown',
    }));
    await waitForState(
        () => document.activeElement?.id === 'zenBoostContextMenu');
    if (document.activeElement?.id !== 'zenBoostContextMenu' ||
        document.activeElement?.getAttribute('role') !== 'menu') {
      failures.push('ArrowDown must open chooser and focus its menu content');
    }
    key('ArrowDown');
    await waitForState(
        () => document.activeElement?.id === 'zen-boost-site-toggle');
    if (document.activeElement?.id !== 'zen-boost-site-toggle') {
      failures.push('ArrowDown from menu content must focus the site toggle first');
    }
    key('ArrowDown');
    await waitForState(() => document.activeElement?.matches(
        '#zen-boost-list [role="menuitemradio"][data-boost-id]'));
    if (!document.activeElement?.matches(
        '#zen-boost-list [role="menuitemradio"][data-boost-id]')) {
      failures.push('ArrowDown must move from site toggle to the first Boost row');
    }
    key('End');
    await waitForState(
        () => document.activeElement?.id === 'zen-boost-edit-delete');
    if (document.activeElement?.id !== 'zen-boost-edit-delete') {
      failures.push('End must focus the last selected-Boost action');
    }
    key('Home');
    await waitForState(
        () => document.activeElement?.id === 'zen-boost-site-toggle');
    if (document.activeElement?.id !== 'zen-boost-site-toggle') {
      failures.push('Home must return focus to the first site control');
    }
    escape();
    await waitForState(() =>
      trigger.getAttribute('aria-expanded') === 'false' &&
      !document.getElementById('zenBoostContextMenu') &&
      document.activeElement === trigger);
    if (trigger.getAttribute('aria-expanded') !== 'false' ||
        document.getElementById('zenBoostContextMenu') ||
        document.activeElement !== trigger) {
      failures.push('Escape must close chooser and return focus to its trigger');
    }
    return JSON.stringify(failures);
  })()
)js";

constexpr char kMalformedChooserFixtureContract[] = R"js(
  (() => {
    const failures = [];
    const list = document.getElementById('zen-boost-list');
    const firstRow = list?.querySelector('[role="menuitemradio"][data-boost-id]');
    const siteToggle = document.getElementById('zen-boost-site-toggle');
    if (!list || !firstRow || !siteToggle) return JSON.stringify(['fixture unavailable']);
    const duplicate = firstRow.cloneNode(true);
    list.append(duplicate);
    const rows = [...list.querySelectorAll('[role="menuitemradio"][data-boost-id]')];
    const ids = rows.map(row => row.dataset.boostId);
    if (new Set(ids).size !== rows.length) {
      failures.push('duplicate Boost IDs must not render more than one chooser row');
    }
    rows.forEach(row => {
      row.dataset.selected = 'false';
      row.setAttribute('aria-checked', 'false');
      row.dataset.active = 'false';
      row.removeAttribute('aria-current');
    });
    if (rows.filter(row => row.dataset.selected === 'true').length !== 1) {
      failures.push('missing selected Boost state must be detected');
    }
    siteToggle.setAttribute('aria-checked', 'true');
    if (rows.filter(row => row.dataset.active === 'true').length !== 1) {
      failures.push('missing active Boost state must be detected while Site Boosts is On');
    }
    siteToggle.setAttribute('data-disabled', '');
    if (siteToggle.matches('[data-disabled], [aria-disabled="true"]')) {
      failures.push('site toggle must remain enabled for a valid selected Boost');
    }
    duplicate.remove();
    return JSON.stringify(failures);
  })()
)js";

constexpr char kColorWorkspaceContract[] = R"(
  (() => {
    const failures = [];
    const visible = element => {
      const rect = element?.getBoundingClientRect();
      const style = element && getComputedStyle(element);
      return Boolean(element && !element.hidden && style.display !== 'none' &&
          style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0);
    };
    const button = (selector, name, pressed) => {
      const element = document.querySelector(selector);
      if (!visible(element) || element.tagName !== 'BUTTON' ||
          element.getAttribute('aria-label') !== name ||
          (pressed !== undefined && element.getAttribute('aria-pressed') === null)) {
        failures.push(`${selector} must expose button/name/state semantics`);
      }
      return element;
    };
    const wheel = document.querySelector('#color-wheel');
    if (!visible(wheel) || wheel.getAttribute('aria-label') !== 'Color wheel' ||
        wheel.getAttribute('aria-disabled') === null) {
      failures.push('color wheel must be a visible named state-bearing group');
    }
    for (const [selector, name] of [
      ['#zen-boost-color-picker-dot-primary', 'Primary color'],
      ['#zen-boost-color-picker-dot-secondary', 'Secondary color'],
    ]) {
      const handle = button(selector, name);
      if (!handle || !handle.getAttribute('aria-valuetext') ||
          !handle.getAttribute('aria-describedby')) {
        failures.push(`${selector} must expose a text value and description`);
      }
    }
    const automaticTheme = button(
        '#zen-boost-magic-theme', 'Use automatic theme colors', true);
    if (automaticTheme?.textContent?.trim() !== 'Auto') {
      failures.push('automatic theme control must expose visible Auto text');
    }
    button('#zen-boost-invert', 'Smart Invert Colors', true);
    const trigger = button('#zen-boost-controls', 'Advanced Color Controls');
    button('#zen-boost-disable', 'Disable Color Adjustments', true);
    if (!trigger || trigger.getAttribute('aria-expanded') !== 'false' ||
        trigger.getAttribute('aria-controls') !== 'advanced-color-popup' ||
        trigger.disabled) {
      failures.push('available advanced color controls must expose their closed popup relationship');
    }
    return JSON.stringify(failures);
  })()
)";

constexpr char kTypographyContract[] = R"(
  (() => {
    const failures = [];
    const visible = element => {
      const rect = element?.getBoundingClientRect();
      const style = element && getComputedStyle(element);
      return Boolean(element && !element.hidden && style.display !== 'none' &&
          style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0);
    };
    const fontButtons = [...document.querySelectorAll('#font-grid .font-button')];
    if (fontButtons.length !== 15 || fontButtons.some(element =>
        !visible(element) || element.tagName !== 'BUTTON' ||
        !element.getAttribute('aria-label') || element.getAttribute('aria-pressed') === null)) {
      failures.push('font grid must expose 15 visible named single-select buttons');
    }
    const selectedFonts = fontButtons.filter(element => element.getAttribute('aria-pressed') === 'true');
    if (selectedFonts.length > 1) {
      failures.push('font grid exposes more than one selected font');
    }
    for (const [selector, prefix] of [['#zen-boost-size', 'Size '], ['#zen-boost-case', 'Case ']]) {
      const control = document.querySelector(selector);
      if (!visible(control) || control.tagName !== 'BUTTON' ||
          !control.getAttribute('aria-label')?.startsWith(prefix) ||
          control.getAttribute('aria-pressed') === null) {
        failures.push(`${selector} must expose name and non-default state`);
      }
    }
    const zap = document.querySelector('#zen-boost-zap');
    if (!visible(zap) || zap.tagName !== 'BUTTON' ||
        !zap.textContent?.trim().startsWith('Zap') ||
        zap.getAttribute('aria-pressed') === null ||
        zap.getAttribute('aria-busy') === null) {
      failures.push('#zen-boost-zap must be a visible button named by its Zap text');
    }
    for (const [selector, name] of [['#zen-boost-code', 'Open Code editor']]) {
      const control = document.querySelector(selector);
      if (!visible(control) || control.tagName !== 'BUTTON' ||
          control.getAttribute('aria-label') !== name) {
        failures.push(`${selector} must be a visible named utility button`);
      }
    }
    return JSON.stringify(failures);
  })()
)";

constexpr char kCodeRouteContract[] = R"MAHOJS(
  (() => {
    const failures = [];
    const visible = element => {
      const rect = element?.getBoundingClientRect();
      const style = element && getComputedStyle(element);
      return Boolean(element && !element.hidden && style.display !== 'none' &&
          style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0);
    };
    const root = document.querySelector('#zen-boost-code-editor-root');
    if (!visible(root) || root.tagName !== 'MAIN' || root.getAttribute('aria-label') !== 'Code editor') {
      failures.push('Code route must be a visible named main region');
    } else {
      const rect = root.getBoundingClientRect();
      if (Math.round(rect.width) !== 452 || Math.round(rect.height) !== 582) {
        failures.push('Code route must retain its exact 452x582 geometry');
      }
    }
    const back = document.querySelector('#zen-boost-back');
    const picker = document.querySelector('#zen-boost-css-picker');
    const inspector = document.querySelector('#zen-boost-css-inspector');
    for (const [element, name] of [[back, 'Back to Boost'], [picker, 'Pick selector'], [inspector, 'Open Inspector']]) {
      if (!visible(element) || element.tagName !== 'BUTTON' || element.getAttribute('aria-label') !== name ||
          getComputedStyle(element).getPropertyValue('-webkit-app-region').trim() !== 'no-drag') {
        failures.push(`${name} must be a visible named no-drag button`);
      }
    }
    const topBar = document.querySelector('#zen-boost-code-top-bar')?.getBoundingClientRect();
    const bottomBar = document.querySelector('#zen-boost-code-bottom-bar')?.getBoundingClientRect();
    if (!topBar || Math.round(topBar.height) !== 40 ||
        !bottomBar || Math.round(bottomBar.height) !== 60) {
      failures.push('Code route must retain fixed 40px top and 60px bottom bars');
    }
    for (const control of [back, picker, inspector]) {
      const icons = control?.querySelectorAll(':scope > svg.boost-control-icon');
      if (!icons || icons.length !== 1) {
        failures.push(`${control?.id || 'Code control'} must contain exactly one inline Lucide SVG`);
        continue;
      }
      const before = getComputedStyle(control, '::before');
      const webkitMask = before.getPropertyValue('-webkit-mask-image').trim();
      if (!['none', 'normal'].includes(before.content) ||
          before.maskImage !== 'none' ||
          (webkitMask !== '' && webkitMask !== 'none')) {
        failures.push(`${control.id} must not retain a pseudo-element icon mask`);
      }
      const iconStyle = getComputedStyle(icons[0]);
      if (iconStyle.display === 'none' || iconStyle.visibility === 'hidden' ||
          Number(iconStyle.opacity) === 0) {
        failures.push(`${control.id} Lucide SVG must remain visible`);
      }
      if (control.getAttribute('aria-busy') === 'true' &&
          !icons[0].classList.contains('animate-spin')) {
        failures.push(`${control.id} busy state must expose the Loader2 spinner`);
      }
    }
    if (picker?.getAttribute('aria-pressed') === null) {
      failures.push('Picker must expose its pressed state');
    }
    const editor = document.querySelector('#custom-css-editor .cm-editor');
    if (!visible(editor) || !editor.querySelector('.cm-content') || document.activeElement?.closest('.cm-editor') !== editor) {
      failures.push('CodeMirror must be visible and receive route focus after mode switch');
    }
    return JSON.stringify(failures);
  })()
)MAHOJS";

constexpr char kForcedColorsLucideContract[] = R"MAHOJS(
  (() => {
    const failures = [];
    const cssText = [...document.styleSheets]
        .flatMap(sheet => [...sheet.cssRules].map(rule => rule.cssText))
        .join('\n');
    const normalized = cssText.toLowerCase();
    if (!normalized.includes('@media (forced-colors: active)') ||
        !normalized.includes('.boost-control-icon') ||
        !normalized.includes('color: buttontext') ||
        !normalized.includes('stroke: buttontext') ||
        !normalized.includes('forced-color-adjust: auto')) {
      failures.push('forced-colors rules must preserve Lucide color, stroke, and adjustment');
    }
    if (!normalized.includes(':focus-visible .boost-control-icon')) {
      failures.push('focus-visible rules must keep inline Lucides visible');
    }
    return JSON.stringify(failures);
  })()
)MAHOJS";

constexpr char kKeyboardAndDragContract[] = R"(
  (() => {
    const failures = [];
    const visible = element => {
      const rect = element.getBoundingClientRect();
      const style = getComputedStyle(element);
      return element.isConnected && !element.hidden &&
          element.getAttribute('aria-hidden') !== 'true' && !element.closest('[inert]') &&
          style.display !== 'none' && style.visibility !== 'hidden' &&
          rect.width > 0 && rect.height > 0;
    };
    const interactiveControls = [...document.querySelectorAll(
      '#boost-editor button,#boost-editor input,#boost-editor select,' +
      '#boost-editor textarea,#boost-editor a[href],#boost-editor [contenteditable="true"],' +
      '#boost-editor [tabindex]')];
    const controlsOutsideRoute = [...document.querySelectorAll(
      'button,input,select,textarea,a[href],[contenteditable="true"],' +
      '[tabindex]:not([tabindex="-1"])')]
      .filter(element => !element.closest('#boost-editor'));
    const intentionalImportInputs = controlsOutsideRoute.filter(element =>
        element.matches('input[type="file"][aria-hidden="true"][tabindex="-1"]') &&
        !visible(element));
    if (controlsOutsideRoute.length !== 1 || intentionalImportInputs.length !== 1) {
      failures.push('only the intentional hidden import input may exist outside the Boost route');
    }
    const namedControls = interactiveControls.filter(element => element.id);
    if (new Set(namedControls.map(element => element.id)).size !== namedControls.length) {
      failures.push('Boost route must not contain duplicate interactive control IDs');
    }
    if (interactiveControls.some(element => !visible(element))) {
      failures.push('Boost route must not retain hidden compatibility controls');
    }
    const liveControls = interactiveControls.filter(element =>
        visible(element) && !element.disabled);
    const orderedControlIds = ['zen-boost-close', 'zen-boost-name-container',
      'zen-boost-magic-theme', 'zen-boost-color-picker-dot-primary',
      'zen-boost-color-picker-dot-secondary', 'zen-boost-invert']
      .filter(id => !document.getElementById(id)?.disabled);
    const advancedControls = document.querySelector('#zen-boost-controls');
    if (!advancedControls?.disabled) {
      orderedControlIds.push('zen-boost-controls');
    }
    const fontGrid = document.getElementById('zen-boost-font-grid');
    const fontButtons = liveControls.filter(element =>
        element.matches('#font-grid .font-button'));
    orderedControlIds.push('zen-boost-disable');
    const orderedControls = [
      ...orderedControlIds.map(id => document.getElementById(id)),
      fontGrid,
      ...fontButtons,
      ...['zen-boost-font-select', 'zen-boost-size', 'zen-boost-case',
          'zen-boost-zap', 'zen-boost-code'].map(id => document.getElementById(id)),
    ];
    if (orderedControls.some(element => !element) ||
        liveControls.length !== orderedControls.length ||
        orderedControls.some((element, index) => liveControls[index] !== element)) {
      failures.push('Boost DOM control order must match the complete visible control order');
    }
    const disableIndex = liveControls.indexOf(document.getElementById('zen-boost-disable'));
    const fontSelectIndex = liveControls.indexOf(
        document.getElementById('zen-boost-font-select'));
    const firstFontButtonIndex = liveControls.indexOf(fontButtons[0]);
    if (fontButtons.length !== 15 || firstFontButtonIndex <= disableIndex ||
        firstFontButtonIndex + fontButtons.length !== fontSelectIndex ||
        fontButtons.some((element, index) =>
            liveControls.indexOf(element) !== firstFontButtonIndex + index)) {
      failures.push('Boost DOM control order must retain the contiguous font grid before typography controls');
    }
    const fontRovingControls = [fontGrid, ...fontButtons];
    const fontTabStops = fontButtons.filter(element => element.tabIndex === 0);
    const unselectedFontGrid = fontGrid?.tabIndex === 0 &&
        fontTabStops.length === 0 &&
        fontButtons.every(element => element.tabIndex === -1);
    const selectedFontGrid = fontGrid?.tabIndex === -1 &&
        fontTabStops.length === 1 &&
        fontButtons.filter(element => element.tabIndex === -1).length === 14;
    if (!fontGrid || fontGrid.getAttribute('role') !== 'radiogroup' ||
        liveControls.some(element =>
            element.tabIndex < 0 && !fontRovingControls.includes(element)) ||
        fontButtons.some(element => element.tabIndex !== 0 && element.tabIndex !== -1) ||
        (!unselectedFontGrid && !selectedFontGrid)) {
      failures.push('font grid must be the sole roving-tabindex control group');
    }
    const focusables = liveControls.filter(element => element.tabIndex >= 0);
    const fontKeyboardStop = fontGrid?.tabIndex >= 0 ? fontGrid : fontTabStops[0];
    const keyboardControls = [
      ...orderedControlIds.map(id => document.getElementById(id)),
      fontKeyboardStop,
      ...['zen-boost-font-select', 'zen-boost-size', 'zen-boost-case',
          'zen-boost-zap', 'zen-boost-code'].map(id => document.getElementById(id)),
    ];
    if (keyboardControls.some(element => !element) ||
        focusables.length !== keyboardControls.length ||
        keyboardControls.some((element, index) => focusables[index] !== element)) {
      failures.push('Boost tab order must retain the font grid roving tab stop');
    }
    for (const element of focusables) {
      if (getComputedStyle(element).getPropertyValue('-webkit-app-region').trim() === 'drag') {
        failures.push(`interactive control ${element.id || element.ariaLabel} is draggable`);
      }
      element.focus();
      const style = getComputedStyle(element);
      const ring = style.outlineWidth !== '0px' || style.boxShadow !== 'none';
       let clippingAncestor = null;
       for (let ancestor = element.parentElement; ancestor; ancestor = ancestor.parentElement) {
         if (['hidden', 'clip'].includes(getComputedStyle(ancestor).overflow)) {
           clippingAncestor = ancestor;
           break;
         }
       }
       const rect = element.getBoundingClientRect();
       const clipped = clippingAncestor && (rect.left < clippingAncestor.getBoundingClientRect().left ||
           rect.right > clippingAncestor.getBoundingClientRect().right ||
           rect.top < clippingAncestor.getBoundingClientRect().top ||
           rect.bottom > clippingAncestor.getBoundingClientRect().bottom);
       if (!ring || clipped) {
        failures.push(`focus ring for ${element.id || element.ariaLabel} is not observable`);
        break;
      }
    }
    const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (reduced && focusables.some(element => getComputedStyle(element).transitionDuration !== '0s')) {
      failures.push('reduced-motion controls must not retain transitions');
    }
    const actionsMenu = document.querySelector('#zen-boost-name-container');
    if (advancedControls?.getAttribute('aria-expanded') !== 'false' ||
        actionsMenu?.getAttribute('aria-expanded') !== 'false') {
      failures.push('closed popup and actions-menu triggers must expose their collapsed state');
    }
    return JSON.stringify(failures);
  })()
)";

constexpr char kStateCaptureHook[] = R"(
  (() => {
    window.__mahoBoostTest ??= {};
    window.__mahoBoostTest.mounted ??= () => false;
    window.__mahoBoostTest.captureTodo13State = state => {
      const controls = [...document.querySelectorAll(
          'button,input,[role="slider"],[role="menuitem"],' +
          '[role="menuitemcheckbox"],[role="menuitemradio"],[role="alert"]')]
          .filter(element => {
            const rect = element.getBoundingClientRect();
            const style = getComputedStyle(element);
            return !element.hidden && style.display !== 'none' &&
                style.visibility !== 'hidden' && rect.width > 0 && rect.height > 0;
          })
          .map(element => ({
            id: element.id,
            role: element.getAttribute('role') || element.tagName.toLowerCase(),
            name: element.getAttribute('aria-label') || element.textContent?.trim(),
            pressed: element.getAttribute('aria-pressed'),
            checked: element.getAttribute('aria-checked'),
            selected: element.getAttribute('aria-selected'),
            current: element.getAttribute('aria-current'),
            boostId: element.getAttribute('data-boost-id'),
            active: element.getAttribute('data-active'),
            editing: element.getAttribute('data-selected'),
            expanded: element.getAttribute('aria-expanded'),
            disabled: element.matches(':disabled') || element.getAttribute('aria-disabled'),
            busy: element.getAttribute('aria-busy'),
            invalid: element.getAttribute('aria-invalid'),
          }));
      return JSON.stringify({state, viewport: [innerWidth, innerHeight], controls});
    };
    return 'installed';
  })()
)";

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest, TitleStripMatchesReference) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Title strip",
                                      kTitleStripContract);
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest,
                       LiveLucidesHaveOneSvgNoMasksAndFixedGeometry) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Boost Lucide controls",
                                      kBoostLucideContract);
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Lucide forced colors",
                                      kForcedColorsLucideContract);

  ASSERT_TRUE(OpenBoostChooser(editor_web_contents));
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Boost menu Lucide controls",
                                      kBoostChooserLucideContract);
  ASSERT_TRUE(content::ExecJs(editor_web_contents, R"MAHOJS(
    document.getElementById('zenBoostContextMenu')?.dispatchEvent(
        new KeyboardEvent('keydown', {bubbles: true, key: 'Escape'}));
  )MAHOJS"));

  ASSERT_TRUE(SwitchToCodeMode(editor_web_contents));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Code Lucide controls",
                                      kCodeRouteContract);
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest,
                       BoostChooserClosedOpenDomAxBoundsAndKeyboard) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain(GetTargetWebContents()->GetLastCommittedURL().host());
  const std::string alpha_id = CreateBoostForDomain(core, domain, "Alpha");
  const std::string beta_id = CreateBoostForDomain(core, domain, "Beta");
  ASSERT_FALSE(alpha_id.empty());
  ASSERT_FALSE(beta_id.empty());
  maho_core_boost_set_active(core, domain.c_str(), alpha_id.c_str());

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(editor_web_contents, kStateCaptureHook));
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Closed Boost chooser",
                                      kTitleStripContract);
  CaptureTask4StateSnapshot(editor_web_contents, "chooser-closed");

  ASSERT_TRUE(OpenBoostChooser(editor_web_contents));
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Open Boost chooser",
                                      kBoostChooserContract);
  CaptureTask4StateSnapshot(editor_web_contents, "chooser-open");
  ExpectNoVisualAccessibilityFailures(editor_web_contents,
                                      "Boost chooser keyboard navigation",
                                      kBoostChooserKeyboardContract);
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest,
                       BoostChooserKeepsSelectionOffUntilEnabled) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain(GetTargetWebContents()->GetLastCommittedURL().host());
  const std::string alpha_id = CreateBoostForDomain(core, domain, "Alpha");
  const std::string beta_id = CreateBoostForDomain(core, domain, "Beta");
  ASSERT_FALSE(alpha_id.empty());
  ASSERT_FALSE(beta_id.empty());
  maho_core_boost_set_active(core, domain.c_str(), alpha_id.c_str());

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(OpenBoostChooser(editor_web_contents));
  ASSERT_TRUE(content::ExecJs(editor_web_contents, R"js(
    (() => {
      const toggle = document.getElementById('zen-boost-site-toggle');
      toggle?.focus();
      return toggle?.dispatchEvent(new KeyboardEvent('keydown', {
        bubbles: true, cancelable: true, code: 'Space', key: ' ',
      })) ?? false;
    })()
  )js"));
  base::RepeatingCallback<bool()> site_boosts_turned_off = base::BindRepeating(
      [](MahoCore* core, const std::string& domain,
         content::WebContents* editor_web_contents) {
        const content::EvalJsResult off =
            content::EvalJs(editor_web_contents,
                            "document.getElementById('zen-boost-site-toggle')?."
                            "getAttribute('aria-checked') === 'false'");
        return ActiveBoostIdForDomain(core, domain).empty() && off.is_ok() &&
               off.ExtractBool();
      },
      core, domain, editor_web_contents);
  ASSERT_TRUE(
      WaitForCondition("site Boosts turned Off", site_boosts_turned_off));

  ASSERT_TRUE(
      content::ExecJs(editor_web_contents, content::JsReplace(R"js(
        (() => {
          const row = document.querySelector('[data-boost-id=$1]');
          row?.focus();
          return row?.dispatchEvent(new KeyboardEvent('keydown', {
            bubbles: true, cancelable: true, code: 'Enter', key: 'Enter',
          })) ?? false;
        })()
      )js",
                                                              beta_id)));
  base::RepeatingCallback<bool()> beta_selected_while_off = base::BindRepeating(
      [](MahoCore* core, const std::string& domain,
         content::WebContents* editor_web_contents) -> bool {
        const content::EvalJsResult selected =
            content::EvalJs(editor_web_contents,
                            "document.getElementById('zen-boost-name-text')?."
                            "textContent === 'Beta' && "
                            "document.getElementById('zen-boost-name-container'"
                            ")?.getAttribute('aria-expanded') === 'false'");
        return ActiveBoostIdForDomain(core, domain).empty() &&
               selected.is_ok() && selected.ExtractBool();
      },
      core, domain, editor_web_contents);
  ASSERT_TRUE(WaitForCondition("Beta selected for editing while Off",
                               beta_selected_while_off));

  ASSERT_TRUE(OpenBoostChooser(editor_web_contents));
  ExpectNoVisualAccessibilityFailures(
      editor_web_contents, "Off selected Boost chooser", kBoostChooserContract);
  const content::EvalJsResult off_selection =
      content::EvalJs(editor_web_contents, content::JsReplace(R"js(
        (() => {
          const row = document.querySelector('[data-boost-id=$1]');
          const toggle = document.getElementById('zen-boost-site-toggle');
          return row?.getAttribute('aria-checked') === 'true' &&
              row?.dataset.selected === 'true' &&
              row?.dataset.active === 'false' &&
              row?.textContent.includes('Selected') &&
              toggle?.getAttribute('aria-checked') === 'false';
        })()
      )js",
                                                              beta_id));
  ASSERT_TRUE(off_selection.is_ok()) << off_selection.ExtractError();
  EXPECT_TRUE(off_selection.ExtractBool());

  ASSERT_TRUE(content::ExecJs(editor_web_contents, R"js(
    (() => {
      const toggle = document.getElementById('zen-boost-site-toggle');
      toggle?.focus();
      return toggle?.dispatchEvent(new KeyboardEvent('keydown', {
        bubbles: true, cancelable: true, code: 'Enter', key: 'Enter',
      })) ?? false;
    })()
  )js"));
  base::RepeatingCallback<bool()> beta_activated_for_site = base::BindRepeating(
      [](MahoCore* core, const std::string& domain, const std::string& beta_id,
         content::WebContents* editor_web_contents) -> bool {
        if (ActiveBoostIdForDomain(core, domain) != beta_id) {
          return false;
        }
        const content::EvalJsResult activated =
            content::EvalJs(editor_web_contents, content::JsReplace(R"js(
                  (() => {
                    const shell = document.querySelector('.boost-shell');
                    const trigger = document.getElementById('zen-boost-name-container');
                    return shell?.dataset.selectedBoostId === $1 &&
                        shell?.dataset.activeBoostId === $1 &&
                        shell?.dataset.siteBoostEnabled === 'true' &&
                        trigger instanceof HTMLButtonElement && !trigger.disabled;
                  })()
                )js",
                                                                    beta_id));
        return activated.is_ok() && activated.ExtractBool();
      },
      core, domain, beta_id, editor_web_contents);
  ASSERT_TRUE(WaitForCondition("selected Beta activated for site",
                               beta_activated_for_site));
  ASSERT_TRUE(OpenBoostChooser(editor_web_contents));
  ExpectNoVisualAccessibilityFailures(
      editor_web_contents, "On active Boost chooser", kBoostChooserContract);
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest,
                       BoostChooserNamesMalformedStateFixtures) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(OpenBoostChooser(editor_web_contents));
  const content::EvalJsResult result =
      content::EvalJs(editor_web_contents, kMalformedChooserFixtureContract);
  ASSERT_TRUE(result.is_ok()) << result.ExtractError();
  const std::string failures = result.ExtractString();
  EXPECT_NE(std::string::npos,
            failures.find("duplicate Boost IDs must not render more than one chooser row"));
  EXPECT_NE(std::string::npos,
            failures.find("missing selected Boost state must be detected"));
  EXPECT_NE(std::string::npos,
            failures.find("missing active Boost state must be detected while Site Boosts is On"));
  EXPECT_NE(std::string::npos,
            failures.find("site toggle must remain enabled for a valid selected Boost"));
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest, ColorWorkspaceMatchesReference) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Color workspace",
                                      kColorWorkspaceContract);
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest,
                       TypographyAndUtilityRowsMatchReference) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Typography and utilities",
                                      kTypographyContract);
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest, CodeRouteMatchesReference) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(SwitchToCodeMode(editor_web_contents));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Code route",
                                      kCodeRouteContract);
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest, KeyboardAndDragRegions) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ExpectNoVisualAccessibilityFailures(editor_web_contents, "Keyboard and drag regions",
                                      kKeyboardAndDragContract);

  const content::EvalJsResult close_result = content::EvalJs(
      editor_web_contents,
      "(() => { const close = document.getElementById('zen-boost-close'); "
      "if (!close) return false; close.click(); return true; })()");
  ASSERT_TRUE(close_result.is_ok()) << close_result.ExtractError();
  ASSERT_TRUE(close_result.ExtractBool())
      << "The live Close Boost editor control must exist before invoking it";
  ASSERT_TRUE(WaitUntilHidden())
      << "The Close Boost editor control must invoke the live editor lifecycle handler";
}

IN_PROC_BROWSER_TEST_F(MahoBoostVisualAccessibilityTest,
                       CapturesInteractiveVisualAndAxStates) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(editor_web_contents, kStateCaptureHook));

  CaptureTodo13StateSnapshot(editor_web_contents, "ready");
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "document.querySelector('#zen-boost-magic-theme')"
                              "?.dispatchEvent(new MouseEvent('mouseover', {bubbles: true}))"));
  CaptureTodo13StateSnapshot(editor_web_contents, "hover");
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "document.querySelector('#zen-boost-magic-theme')?.focus()"));
  CaptureTodo13StateSnapshot(editor_web_contents, "focus-visible");
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "document.querySelector('#zen-boost-magic-theme')"
                              "?.dispatchEvent(new MouseEvent('mousedown', {bubbles: true}))"));
  CaptureTodo13StateSnapshot(editor_web_contents, "active");
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "document.querySelector('#font-grid .font-button')?.click()"));
  CaptureTodo13StateSnapshot(editor_web_contents, "selected");
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "document.querySelector('#zen-boost-disable')?.click()"));
  CaptureTodo13StateSnapshot(editor_web_contents, "disabled");
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "document.querySelector('#zen-boost-name-container')?.click()"));
  CaptureTodo13StateSnapshot(editor_web_contents, "rename");
  CaptureTodo13StateSnapshot(editor_web_contents, "loading");
  CaptureTodo13StateSnapshot(editor_web_contents, "error");
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "document.querySelector('#zen-boost-zap')?.click()"));
  CaptureTodo13StateSnapshot(editor_web_contents, "zap-active");

  ASSERT_TRUE(SwitchToCodeMode(editor_web_contents));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));
  ASSERT_TRUE(content::ExecJs(editor_web_contents, kStateCaptureHook));
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.querySelector('#zen-boost-css-picker')?.click()"));
  CaptureTodo13StateSnapshot(editor_web_contents, "picker-active");
}
