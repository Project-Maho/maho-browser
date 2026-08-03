// Copyright 2026 Maho Browser. All rights reserved.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_boost_injection_handler.h"
#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost_page_handler.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace {

class MahoBoostActionsTest : public MahoBoostInteractiveUiTest {};

bool ClickMenuItem(content::WebContents* editor_web_contents,
                   std::string_view item_id) {
  const auto clicked = content::EvalJs(
      editor_web_contents,
      content::JsReplace(
          R"js((async () => {
            const trigger = document.getElementById('zen-boost-name-container');
            if (!(trigger instanceof HTMLElement) ||
                trigger.matches(':disabled, [aria-disabled="true"]')) return false;
            const nextFrame = () => new Promise(resolve => requestAnimationFrame(resolve));
            const closeMenu = async () => {
              if (trigger.getAttribute('aria-expanded') !== 'true') return;
              trigger.dispatchEvent(new KeyboardEvent('keydown', {
                bubbles: true, cancelable: true, code: 'Escape', key: 'Escape',
              }));
              for (let frame = 0; frame < 5; ++frame) {
                await nextFrame();
                if (trigger.getAttribute('aria-expanded') !== 'true') return;
              }
            };
            const openMenu = async () => {
              if (trigger.getAttribute('aria-expanded') !== 'true') {
                trigger.dispatchEvent(new PointerEvent('pointerdown', {
                  bubbles: true, button: 0, buttons: 1,
                }));
              }
              for (let frame = 0; frame < 10; ++frame) {
                await nextFrame();
                if (trigger.getAttribute('aria-expanded') === 'true') return true;
              }
              return trigger.getAttribute('aria-expanded') === 'true';
            };
            for (let attempt = 0; attempt < 3; ++attempt) {
              if (!await openMenu()) {
                await closeMenu();
                continue;
              }
              const item = document.getElementById($1);
              if (item instanceof HTMLElement &&
                  !item.matches('[data-disabled], [aria-disabled="true"]')) {
                item.dispatchEvent(new PointerEvent('pointerdown', {
                  bubbles: true, button: 0, buttons: 1,
                }));
                item.dispatchEvent(new PointerEvent('pointerup', {
                  bubbles: true, button: 0,
                }));
                item.click();
                return true;
              }
              await closeMenu();
            }
            return false;
          })())js",
          std::string(item_id)));
  return clicked.is_ok() && clicked.ExtractBool();
}

// Picks a Boost from the title menu's `zen-boost-list` radio group, which is
// how the user chooses which Boost the editor edits.
bool SelectBoostInEditor(content::WebContents* editor_web_contents,
                         std::string_view boost_id) {
  const auto selected = content::EvalJs(
      editor_web_contents,
      content::JsReplace(
          R"js((async () => {
            const trigger = document.getElementById('zen-boost-name-container');
            if (!(trigger instanceof HTMLElement)) return false;
            const nextFrame = () => new Promise(resolve => requestAnimationFrame(resolve));
            if (trigger.getAttribute('aria-expanded') !== 'true') {
              trigger.dispatchEvent(new PointerEvent('pointerdown', {
                bubbles: true, button: 0, buttons: 1,
              }));
            }
            for (let frame = 0; frame < 20; ++frame) {
              await nextFrame();
              const items = Array.from(document.querySelectorAll(
                '#zen-boost-list [data-boost-id]'));
              const item = items.find(
                candidate => candidate.dataset.boostId === $1);
              if (item instanceof HTMLElement &&
                  !item.matches('[data-disabled], [aria-disabled="true"]')) {
                item.dispatchEvent(new PointerEvent('pointerdown', {
                  bubbles: true, button: 0, buttons: 1,
                }));
                item.dispatchEvent(new PointerEvent('pointerup', {
                  bubbles: true, button: 0,
                }));
                item.click();
                for (let settle = 0; settle < 20; ++settle) {
                  await nextFrame();
                  const current = Array.from(document.querySelectorAll(
                    '#zen-boost-list [data-boost-id]'))
                    .find(candidate => candidate.dataset.boostId === $1);
                  if (current instanceof HTMLElement &&
                      current.dataset.selected === 'true') {
                    return true;
                  }
                }
                return false;
              }
            }
            return false;
          })())js",
          std::string(boost_id)));
  return selected.is_ok() && selected.ExtractBool();
}

bool IsMenuItemEnabled(content::WebContents* editor_web_contents,
                       std::string_view item_id) {
  const auto enabled = content::EvalJs(
      editor_web_contents,
      content::JsReplace(
          R"js((async () => {
            const trigger = document.getElementById('zen-boost-name-container');
            if (!(trigger instanceof HTMLButtonElement) ||
                trigger.matches(':disabled, [aria-disabled="true"]')) return false;
            if (trigger.getAttribute('aria-expanded') !== 'true') {
              trigger.dispatchEvent(new PointerEvent('pointerdown', {
                bubbles: true, button: 0, buttons: 1,
              }));
            }
            let item_enabled = false;
            for (let frame = 0; frame < 10; ++frame) {
              await new Promise(resolve => requestAnimationFrame(resolve));
              const item = document.getElementById($1);
              if (item instanceof HTMLElement) {
                item_enabled = !item.matches('[data-disabled], [aria-disabled="true"]');
                break;
              }
            }
            if (trigger.getAttribute('aria-expanded') === 'true') {
              trigger.dispatchEvent(new KeyboardEvent('keydown', {
                bubbles: true, cancelable: true, code: 'Escape', key: 'Escape',
              }));
              for (let frame = 0; frame < 5; ++frame) {
                await new Promise(resolve => requestAnimationFrame(resolve));
                if (trigger.getAttribute('aria-expanded') !== 'true') break;
              }
            }
            return item_enabled;
          })())js",
          std::string(item_id)));
  return enabled.is_ok() && enabled.ExtractBool();
}

bool ClickBoostRow(content::WebContents* editor_web_contents,
                   std::string_view boost_id) {
  const auto clicked = content::EvalJs(
      editor_web_contents,
      content::JsReplace(
          R"js((async () => {
            const trigger = document.getElementById('zen-boost-name-container');
            if (!(trigger instanceof HTMLButtonElement) || trigger.disabled) return false;
            if (trigger.getAttribute('aria-expanded') !== 'true') {
              trigger.dispatchEvent(new PointerEvent('pointerdown', {
                bubbles: true, button: 0, buttons: 1,
              }));
            }
            for (let frame = 0; frame < 10; ++frame) {
              await new Promise(resolve => requestAnimationFrame(resolve));
              const row = [...document.querySelectorAll('[data-boost-id]')]
                .find(candidate => candidate.getAttribute('data-boost-id') === $1);
              if (row instanceof HTMLElement &&
                  !row.matches('[data-disabled], [aria-disabled="true"]')) {
                row.click();
                return true;
              }
            }
            return false;
          })())js",
          std::string(boost_id)));
  return clicked.is_ok() && clicked.ExtractBool();
}

std::string TargetDomain(content::WebContents* target_web_contents) {
  return target_web_contents
             ? std::string(target_web_contents->GetLastCommittedURL().host())
                             : std::string();
}

std::optional<std::string> CreateBoost(MahoCore* core,
                                       std::string_view domain,
                                       std::string_view name) {
  char* json = maho_core_boost_create(core, std::string(domain).c_str(),
                                      std::string(name).c_str());
  if (!json) {
    return std::nullopt;
  }
  std::string boost_json(json);
  maho_string_free(json);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(boost_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const std::string* id = parsed->GetDict().FindString("id");
  return id ? std::optional<std::string>(*id) : std::nullopt;
}

std::vector<std::string> BoostIdsForDomain(MahoCore* core,
                                           std::string_view domain) {
  std::vector<std::string> ids;
  char* json =
      maho_core_boost_list_for_domain(core, std::string(domain).c_str());
  if (!json) {
    return ids;
  }
  std::string boosts_json(json);
  maho_string_free(json);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(boosts_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return ids;
  }
  for (const base::Value& value : parsed->GetList()) {
    const base::DictValue* boost = value.GetIfDict();
    const std::string* id = boost ? boost->FindString("id") : nullptr;
    if (id) {
      ids.push_back(*id);
    }
  }
  return ids;
}

std::string BoostJson(MahoCore* core, std::string_view boost_id) {
  char* json = maho_core_boost_get(core, std::string(boost_id).c_str());
  if (!json) {
    return std::string();
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

void SetActiveBoost(MahoCore* core,
                    std::string_view domain,
                    const std::optional<std::string>& boost_id) {
  const std::string domain_string(domain);
  maho_core_boost_set_active(core, domain_string.c_str(),
                             boost_id ? boost_id->c_str() : nullptr);
}

bool HasEditorState(content::WebContents* editor_web_contents,
                    std::string_view selected_boost_id,
                    const std::optional<std::string>& active_boost_id) {
  const auto result = content::EvalJs(
      editor_web_contents,
      content::JsReplace(
          R"js((() => {
            const shell = document.querySelector('.boost-shell');
            return shell instanceof HTMLElement &&
              shell.dataset.selectedBoostId === $1 &&
              shell.dataset.activeBoostId === $2 &&
              shell.dataset.siteBoostEnabled === $3;
          })())js",
          std::string(selected_boost_id),
          active_boost_id.value_or(std::string()),
          active_boost_id ? "true" : "false"));
  return result.is_ok() && result.ExtractBool();
}

base::FilePath FindExportedBoostFile(const base::FilePath& directory) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  return base::FileEnumerator(directory, false, base::FileEnumerator::FILES,
                              FILE_PATH_LITERAL("*.json"))
      .Next();
}

std::string WithoutUpdatedAt(std::string boost_json) {
  constexpr std::string_view kUpdatedAt = ",\"updatedAt\":\"";
  const size_t field_start = boost_json.rfind(kUpdatedAt);
  if (field_start == std::string::npos) {
    return boost_json;
  }
  const size_t value_end = boost_json.find('"', field_start + kUpdatedAt.size());
  if (value_end == std::string::npos) {
    return boost_json;
  }
  boost_json.erase(field_start, value_end - field_start + 1);
  return boost_json;
}

std::string ActiveBoostJsonForTarget(content::WebContents* target_web_contents) {
  MahoCore* core = maho::GetCore();
  if (!core || !target_web_contents) return std::string();
  const std::string domain(target_web_contents->GetLastCommittedURL().host());
  char* json = maho_core_boost_get_active(core, domain.c_str());
  if (!json) return std::string();
  std::string result(json);
  maho_string_free(json);
  return result;
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       MultiBoostBootstrapUsesDeterministicOffSelection) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain = TargetDomain(GetTargetWebContents());
  ASSERT_FALSE(domain.empty());
  const std::optional<std::string> lower_id = CreateBoost(core, domain, "alpha");
  const std::optional<std::string> upper_id = CreateBoost(core, domain, "Alpha");
  const std::optional<std::string> beta_id = CreateBoost(core, domain, "beta");
  ASSERT_TRUE(lower_id);
  ASSERT_TRUE(upper_id);
  ASSERT_TRUE(beta_id);
  SetActiveBoost(core, domain, std::nullopt);
  const std::string selected_id = std::min(*lower_id, *upper_id);
  const std::string other_alpha_id =
      selected_id == *lower_id ? *upper_id : *lower_id;

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(WaitForCondition("deterministic saved Boost selected while site is Off", [
      editor_web_contents, selected_id]() {
    return HasEditorState(editor_web_contents, selected_id, std::nullopt);
  }));
  ASSERT_TRUE(content::ExecJs(editor_web_contents, R"js(
    document.getElementById('zen-boost-name-container')?.dispatchEvent(
        new PointerEvent('pointerdown', {
          bubbles: true, button: 0, buttons: 1,
        }));
  )js"));
  ASSERT_TRUE(WaitForCondition("chooser rows use selected-first name and ID ordering", [
      editor_web_contents, selected_id, other_alpha_id, beta_id]() {
    const auto rows = content::EvalJs(editor_web_contents, R"js(
      [...document.querySelectorAll('[data-boost-id]')]
        .map(row => row.getAttribute('data-boost-id') ?? '')
        .join(',')
    )js");
    return rows.is_ok() && rows.ExtractString() ==
        selected_id + "," + other_alpha_id + "," + *beta_id;
  }));
  EXPECT_TRUE(ActiveBoostJsonForTarget(GetTargetWebContents()).empty());
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       SelectWhileOffOnOffAndActiveNullObserverPreserveSelection) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain = TargetDomain(GetTargetWebContents());
  const std::optional<std::string> alpha_id = CreateBoost(core, domain, "Alpha");
  const std::optional<std::string> beta_id = CreateBoost(core, domain, "Beta");
  ASSERT_TRUE(alpha_id);
  ASSERT_TRUE(beta_id);
  SetActiveBoost(core, domain, std::nullopt);

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(ClickBoostRow(editor_web_contents, *beta_id));
  ASSERT_TRUE(WaitForCondition("selecting while Off changes editor only", [
      editor_web_contents, beta_id]() {
    return HasEditorState(editor_web_contents, *beta_id, std::nullopt);
  }));
  EXPECT_TRUE(ActiveBoostJsonForTarget(GetTargetWebContents()).empty());

  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-site-toggle"));
  ASSERT_TRUE(WaitForCondition("turning On activates the selected Boost", [
      this, editor_web_contents, beta_id]() {
    return HasEditorState(editor_web_contents, *beta_id, beta_id) &&
        ActiveBoostJsonForTarget(GetTargetWebContents()).find(*beta_id) !=
            std::string::npos;
  }));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-site-toggle"));
  ASSERT_TRUE(WaitForCondition("active-null observer preserves selected editor", [
      editor_web_contents, beta_id]() {
    return HasEditorState(editor_web_contents, *beta_id, std::nullopt);
  }));
  EXPECT_TRUE(ActiveBoostJsonForTarget(GetTargetWebContents()).empty());
  EXPECT_EQ(2u, BoostIdsForDomain(core, domain).size());
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       ImportDeleteFallbackStaysOffAndNeverDangles) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain = TargetDomain(GetTargetWebContents());
  const std::optional<std::string> alpha_id = CreateBoost(core, domain, "Alpha");
  const std::optional<std::string> beta_id = CreateBoost(core, domain, "Beta");
  ASSERT_TRUE(alpha_id);
  ASSERT_TRUE(beta_id);
  SetActiveBoost(core, domain, std::nullopt);
  const std::string exported_json = maho::core::ExportBoost(core, alpha_id->c_str());
  ASSERT_FALSE(exported_json.empty());

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  const auto imported = content::EvalJs(editor_web_contents, content::JsReplace(R"js(
    (() => {
      const input = document.querySelector('input[type=file]');
      if (!(input instanceof HTMLInputElement)) return false;
      const files = new DataTransfer();
      files.items.add(new File([$1], 'imported.json', {type: 'application/json'}));
      input.files = files.files;
      input.dispatchEvent(new Event('change', {bubbles: true}));
      return true;
    })()
  )js", exported_json));
  ASSERT_TRUE(imported.is_ok());
  ASSERT_TRUE(imported.ExtractBool());
  std::string imported_id;
  ASSERT_TRUE(WaitForCondition("import selects the new record without activation", [
      &imported_id, core, domain, editor_web_contents, alpha_id, beta_id]() {
    for (const std::string& id : BoostIdsForDomain(core, domain)) {
      if (id != *alpha_id && id != *beta_id) {
        imported_id = id;
      }
    }
    return !imported_id.empty() &&
        HasEditorState(editor_web_contents, imported_id, std::nullopt);
  }));
  EXPECT_TRUE(ActiveBoostJsonForTarget(GetTargetWebContents()).empty());

  ASSERT_TRUE(content::ExecJs(editor_web_contents, "window.confirm = () => true"));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-delete"));
  // Delete now requires an explicit second confirmation.
  ASSERT_TRUE(
      ClickMenuItem(editor_web_contents, "zen-boost-edit-delete-confirm"));
  const std::string fallback_id = *alpha_id;
  ASSERT_TRUE(WaitForCondition("delete selects deterministic fallback while staying Off", [
      core, domain, editor_web_contents, fallback_id, imported_id]() {
    const std::vector<std::string> ids = BoostIdsForDomain(core, domain);
    return std::find(ids.begin(), ids.end(), imported_id) == ids.end() &&
        HasEditorState(editor_web_contents, fallback_id, std::nullopt);
  }));
  EXPECT_TRUE(WaitUntilShowing());
  EXPECT_TRUE(ActiveBoostJsonForTarget(GetTargetWebContents()).empty());
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       ResetCancelsPendingMutationAndPreservesOff) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain = TargetDomain(GetTargetWebContents());
  const std::optional<std::string> boost_id = CreateBoost(core, domain, "Pending");
  ASSERT_TRUE(boost_id);
  SetActiveBoost(core, domain, std::nullopt);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-invert')?.click()"));
  ASSERT_TRUE(content::ExecJs(editor_web_contents, "window.confirm = () => true"));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-reset"));
  ASSERT_TRUE(WaitForCondition("Reset cancels unsent update and preserves Off", [
      core, editor_web_contents, boost_id]() {
    const std::string selected = BoostJson(core, *boost_id);
    return HasEditorState(editor_web_contents, *boost_id, std::nullopt) &&
        !selected.empty() &&
        selected.find("\"smartInvert\":true") == std::string::npos;
  }));
  EXPECT_TRUE(ActiveBoostJsonForTarget(GetTargetWebContents()).empty());
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       ResetWaitsForSentMutationThenConvergesAuthoritatively) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain = TargetDomain(GetTargetWebContents());
  const std::optional<std::string> boost_id = CreateBoost(core, domain, "Sent");
  ASSERT_TRUE(boost_id);
  SetActiveBoost(core, domain, boost_id);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(editor_web_contents, R"js(
    (async () => {
      const module = await import('./maho_boost.mojom-webui.js');
      const original = module.PageHandlerRemote.prototype.updateBoost;
      module.PageHandlerRemote.prototype.updateBoost = function(...args) {
        const sent = original.apply(this, args);
        window.__mahoBoostMutationSent = true;
        return new Promise((resolve, reject) => {
          window.__mahoReleaseBoostMutation = () => sent.then(resolve, reject);
        });
      };
    })()
  )js"));
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-invert')?.click()"));
  ASSERT_TRUE(WaitForCondition("mutation request is sent and response is held", [
      editor_web_contents]() {
    const auto sent = content::EvalJs(
        editor_web_contents, "window.__mahoBoostMutationSent === true");
    return sent.is_ok() && sent.ExtractBool();
  }));
  ASSERT_TRUE(content::ExecJs(editor_web_contents, "window.confirm = () => true"));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-reset"));
  ASSERT_TRUE(WaitForCondition("Reset waits behind the sent mutation", [
      editor_web_contents]() {
    const auto pending = content::EvalJs(editor_web_contents,
        "document.querySelector('[role=status]')?.textContent.includes('Resetting') ?? false");
    return pending.is_ok() && pending.ExtractBool();
  }));
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
      "window.__mahoReleaseBoostMutation?.(); true"));
  ASSERT_TRUE(WaitForCondition("Reset wins over held response and stays active", [
      this, editor_web_contents, boost_id]() {
    const std::string active = ActiveBoostJsonForTarget(GetTargetWebContents());
    return HasEditorState(editor_web_contents, *boost_id, boost_id) &&
        active.find(*boost_id) != std::string::npos &&
        active.find("\"smartInvert\":true") == std::string::npos;
  }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest, RenameShuffleResetImportExportDeleteRoundTrip) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-rename"));
  const auto renamed = content::EvalJs(editor_web_contents, R"js(
    (async () => {
       const input = document.getElementById('zen-boost-name-container');
       if (!(input instanceof HTMLInputElement)) return false;
       input.focus();
       const descriptor = Object.getOwnPropertyDescriptor(
           HTMLInputElement.prototype, 'value');
       if (!descriptor?.set) return false;
       descriptor.set.call(input, 'Renamed');
       input.dispatchEvent(new InputEvent('input', {
         bubbles: true, data: 'Renamed', inputType: 'insertText',
       }));
       await new Promise(resolve => requestAnimationFrame(resolve));
       await new Promise(resolve => requestAnimationFrame(resolve));
       if (input.value !== 'Renamed') return false;
        input.dispatchEvent(new KeyboardEvent('keydown', {
          bubbles: true, cancelable: true, code: 'Enter', key: 'Enter',
        }));
        return true;
    })()
  )js");
  ASSERT_TRUE(renamed.is_ok());
  ASSERT_TRUE(renamed.ExtractBool());
  ASSERT_TRUE(WaitForCondition("rename committed in editor and returned Boost", [
      this, editor_web_contents]() {
    const auto result = content::EvalJs(editor_web_contents,
        "document.getElementById('zen-boost-name-text')?.textContent === 'Renamed'");
    return result.is_ok() && result.ExtractBool() &&
            ActiveBoostJsonForTarget(GetTargetWebContents()).find(
                "\"name\":\"Renamed\"") != std::string::npos;
  }));
  HideBoostEditor();
  ASSERT_TRUE(WaitUntilHidden());
  editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(WaitForCondition("renamed Boost persisted after reopen", [
      editor_web_contents]() {
    const auto result = content::EvalJs(editor_web_contents,
        "document.getElementById('zen-boost-name-text')?.textContent === 'Renamed'");
    return result.is_ok() && result.ExtractBool();
  }));

  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-shuffle"));
  ASSERT_TRUE(WaitForCoreBoostCss("font-family:"));
  ASSERT_TRUE(WaitForTargetStyle("brightness("));
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.querySelector(\"#zen-boost-font-grid button[aria-label='Georgia']\")?.click()"));
  ASSERT_TRUE(WaitForCoreBoostCss("font-family: Georgia"));
  ASSERT_TRUE(WaitForTargetStyle("font-family: Georgia"));

  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-save"));
  ASSERT_TRUE(WaitForCondition("real export download", [
      this]() {
    return !FindExportedBoostFile(download_directory()).empty();
  }));

  const base::FilePath exported = FindExportedBoostFile(download_directory());
  ASSERT_FALSE(exported.empty());
  std::string exported_json;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::ReadFileToString(exported, &exported_json));
  }
  ASSERT_FALSE(exported_json.empty());
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.querySelector(\"#zen-boost-font-grid button[aria-label='Papyrus']\")?.click()"));
  ASSERT_TRUE(WaitForCoreBoostCss("font-family: Papyrus"));
  ASSERT_TRUE(WaitForTargetStyle("font-family: Papyrus"));

  const auto imported = content::EvalJs(editor_web_contents, content::JsReplace(R"js(
    (() => {
      const input = document.querySelector('input[type=file]');
      if (!(input instanceof HTMLInputElement)) return false;
      const file = new File([$1], $2, {type: 'application/json'});
      const files = new DataTransfer();
      files.items.add(file);
      input.files = files.files;
      input.dispatchEvent(new Event('change', {bubbles: true}));
      return true;
    })()
  )js", exported_json, exported.BaseName().AsUTF8Unsafe()));
  ASSERT_TRUE(imported.is_ok());
  ASSERT_TRUE(imported.ExtractBool());
  ASSERT_TRUE(WaitForCondition(
      "import restored exported font in editor", [
          editor_web_contents]() {
        const auto result = content::EvalJs(
            editor_web_contents,
            "document.querySelector(\"#zen-boost-font-grid button[aria-label='Georgia']\")?.getAttribute('aria-pressed') === 'true'");
        return result.is_ok() && result.ExtractBool();
      }));
  ASSERT_TRUE(WaitForCoreBoostCss("font-family: Papyrus"));
  ASSERT_TRUE(WaitForTargetStyle("font-family: Papyrus"));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-site-toggle"));
  ASSERT_TRUE(WaitForCondition("imported selection can remain Off", [
      editor_web_contents]() {
    const auto result = content::EvalJs(editor_web_contents,
        "document.querySelector('.boost-shell')?.dataset.siteBoostEnabled === 'false'");
    return result.is_ok() && result.ExtractBool();
  }));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-site-toggle"));
  ASSERT_TRUE(WaitForCoreBoostCss("font-family: Georgia"));
  ASSERT_TRUE(WaitForTargetStyle("font-family: Georgia"));
  const std::string imported_boost =
      ActiveBoostJsonForTarget(GetTargetWebContents());
  EXPECT_NE(std::string::npos, imported_boost.find("\"name\":\"Renamed\""));
  EXPECT_NE(std::string::npos,
            imported_boost.find("\"fontFamily\":\"Georgia\""));

  ASSERT_TRUE(content::ExecJs(editor_web_contents,
      "window.confirm = () => true"));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-reset"));
  ASSERT_TRUE(WaitForCondition("reset cleared stale target effects", [
      this]() {
    const auto result = content::EvalJs(GetTargetWebContents(),
        "!(document.getElementById('maho-boost-style')?.textContent ?? '').includes('font-family:')");
    return result.is_ok() && result.ExtractBool();
  }));

  ASSERT_TRUE(WaitForCondition("delete menu item to mount enabled after reset", [
      editor_web_contents]() {
    return IsMenuItemEnabled(editor_web_contents, "zen-boost-edit-delete");
  }));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-delete"));
  // Delete now requires an explicit second confirmation.
  ASSERT_TRUE(
      ClickMenuItem(editor_web_contents, "zen-boost-edit-delete-confirm"));
  ASSERT_TRUE(WaitUntilShowing());
  ASSERT_TRUE(WaitForCondition("delete removed target injection", [
      this]() {
    const auto result = content::EvalJs(GetTargetWebContents(),
        "!document.getElementById('maho-boost-style')");
    return result.is_ok() && result.ExtractBool();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest, MalformedImportAndDownloadFailuresSurfaceErrors) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  const auto original_name = content::EvalJs(editor_web_contents,
      "document.getElementById('zen-boost-name-text')?.textContent ?? ''");
  ASSERT_TRUE(original_name.is_ok());

  // Delete is not gated behind window.confirm, so no cancellation is possible
  // here. The editor keeps showing its Boost until an action actually runs.
  EXPECT_TRUE(WaitUntilShowing());
  EXPECT_EQ(original_name.ExtractString(), content::EvalJs(editor_web_contents,
      "document.getElementById('zen-boost-name-text')?.textContent ?? ''").ExtractString());

  const auto malformed = content::EvalJs(editor_web_contents, R"js(
    (() => {
      const input = document.querySelector('input[type=file]');
      if (!(input instanceof HTMLInputElement)) return false;
      const files = new DataTransfer();
      files.items.add(new File(['not-json'], 'malformed.json', {type: 'application/json'}));
      input.files = files.files;
      input.dispatchEvent(new Event('change', {bubbles: true}));
      return true;
    })()
  )js");
  ASSERT_TRUE(malformed.is_ok());
  ASSERT_TRUE(malformed.ExtractBool());
  ASSERT_TRUE(WaitForCondition("malformed import error", [
      editor_web_contents]() {
    const auto result = content::EvalJs(editor_web_contents,
        "document.querySelector('[role=alert]')?.textContent.includes('valid Boost') ?? false");
    return result.is_ok() && result.ExtractBool();
  }));
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
      "URL.createObjectURL = () => { throw new Error('download unavailable'); }"));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-save"));
  ASSERT_TRUE(WaitForCondition("download failure is visible", [
      editor_web_contents]() {
    const auto result = content::EvalJs(editor_web_contents,
        "document.querySelector('[role=alert]')?.textContent.includes('download unavailable') ?? false");
    return result.is_ok() && result.ExtractBool();
  }));
  EXPECT_TRUE(WaitUntilShowing());
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest, CancelledRenameAndImportPreserveBoostWhileResetApplies) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  ASSERT_TRUE(content::ExecJs(
      editor_web_contents, "document.getElementById('zen-boost-invert')?.click()"));
  ASSERT_TRUE(WaitForCoreBoostCss("invert(1)"));
  ASSERT_TRUE(WaitForCondition("reset menu item enabled after color edit", [
      editor_web_contents]() {
    return IsMenuItemEnabled(editor_web_contents, "zen-boost-edit-reset");
  }));
  const std::string boost_before =
      WithoutUpdatedAt(ActiveBoostJsonForTarget(GetTargetWebContents()));
  ASSERT_FALSE(boost_before.empty());

  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-rename"));
  const auto cancelled_rename = content::EvalJs(editor_web_contents, R"js(
    (async () => {
       const input = document.getElementById('zen-boost-name-container');
       if (!(input instanceof HTMLInputElement)) return false;
       input.focus();
       const descriptor = Object.getOwnPropertyDescriptor(
           HTMLInputElement.prototype, 'value');
      if (!descriptor?.set) return false;
      descriptor.set.call(input, 'Cancelled rename');
       input.dispatchEvent(new InputEvent('input', {
         bubbles: true, data: 'Cancelled rename', inputType: 'insertText',
       }));
       await new Promise(resolve => requestAnimationFrame(resolve));
       await new Promise(resolve => requestAnimationFrame(resolve));
       if (input.value !== 'Cancelled rename') return false;
        input.dispatchEvent(new KeyboardEvent('keydown', {
          bubbles: true, cancelable: true, code: 'Escape', key: 'Escape',
        }));
       return true;
    })()
  )js");
  ASSERT_TRUE(cancelled_rename.is_ok());
  ASSERT_TRUE(cancelled_rename.ExtractBool());
  ASSERT_TRUE(WaitForCondition("cancelled rename restored action trigger", [
      editor_web_contents]() {
    const auto result = content::EvalJs(editor_web_contents, R"js(
      (() => {
        const trigger = document.getElementById('zen-boost-name-container');
        return trigger instanceof HTMLButtonElement &&
          trigger.getAttribute('aria-expanded') === 'false';
      })()
    )js");
    return result.is_ok() && result.ExtractBool();
  }));
  EXPECT_EQ(boost_before,
            WithoutUpdatedAt(ActiveBoostJsonForTarget(GetTargetWebContents())));

  ASSERT_TRUE(WaitUntilShowing());
  editor_web_contents = WaitForEditorLoad();
  ASSERT_TRUE(editor_web_contents);

  // Reset is not gated behind window.confirm: invoking it applies immediately.
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-reset"));
  ASSERT_TRUE(WaitForCondition(
      "reset dropped the recorded color edit",
      [this]() {
        const std::string json =
            ActiveBoostJsonForTarget(GetTargetWebContents());
        return !json.empty() &&
               json.find("\"smartInvert\":false") != std::string::npos &&
               json.find("\"changeWasMade\":false") != std::string::npos;
      }));
  const std::string boost_after_reset =
      WithoutUpdatedAt(ActiveBoostJsonForTarget(GetTargetWebContents()));
  ASSERT_FALSE(boost_after_reset.empty());

  const auto import_cancelled = content::EvalJs(editor_web_contents, R"js(
    (() => {
      const input = document.querySelector('input[type=file]');
      if (!(input instanceof HTMLInputElement)) return false;
      input.files = new DataTransfer().files;
      input.dispatchEvent(new Event('change', {bubbles: true}));
      return true;
    })()
  )js");
  ASSERT_TRUE(import_cancelled.is_ok());
  ASSERT_TRUE(import_cancelled.ExtractBool());
  EXPECT_EQ(boost_after_reset,
            WithoutUpdatedAt(ActiveBoostJsonForTarget(GetTargetWebContents())));

  ASSERT_TRUE(WaitForCondition("delete menu item to remount enabled after cancelled actions", [
      editor_web_contents]() {
    return IsMenuItemEnabled(editor_web_contents, "zen-boost-edit-delete");
  }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest, CoreActionFailuresReturnEmptyOrFalseForUnknownBoost) {
  NavigateTo("/title1.html");
  ASSERT_TRUE(OpenEditorAndWait());

  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  constexpr char kMissingBoostId[] = "missing-boost-for-action-failure";
  EXPECT_EQ(nullptr, maho_core_boost_update(core, kMissingBoostId,
                                            "{\"name\":\"Missing\"}"));
  EXPECT_FALSE(maho_core_boost_delete(core, kMissingBoostId));
  EXPECT_TRUE(maho::core::ShuffleBoost(core, kMissingBoostId).empty());
  EXPECT_TRUE(maho::core::ResetBoost(core, kMissingBoostId).empty());
  EXPECT_TRUE(maho::core::ImportBoost(core, "127.0.0.1", "not-json").empty());
}


IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       PageHandlerRejectsForeignDomainBoostIds) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::optional<std::string> local =
      CreateBoost(core, "127.0.0.1", "Local");
  const std::optional<std::string> foreign =
      CreateBoost(core, "foreign.example", "Foreign");
  ASSERT_TRUE(local);
  ASSERT_TRUE(foreign);

  EXPECT_TRUE(MahoBoostPageHandler::IsBoostAuthorizedForDomainForTesting(
      "127.0.0.1", *local));
  EXPECT_FALSE(MahoBoostPageHandler::IsBoostAuthorizedForDomainForTesting(
      "127.0.0.1", *foreign));
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       SmartInvertAppliesToHttpErrorDocument) {
  const GURL error_url("http://127.0.0.1:1/oauth-callback");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::optional<std::string> boost_id =
      CreateBoost(core, "127.0.0.1", "Callback invert");
  ASSERT_TRUE(boost_id);
  SetActiveBoost(core, "127.0.0.1", *boost_id);
  char* updated = maho_core_boost_update(
      core, boost_id->c_str(), "{\"color\":{\"smartInvert\":true}}");
  ASSERT_NE(nullptr, updated);
  maho_string_free(updated);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), error_url));
  content::WebContents* target = GetTargetWebContents();
  ASSERT_TRUE(target);
  EXPECT_TRUE(WaitForTargetStyle("invert(1)"));
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       SmartInvertNormalizesPageCanvasBackground) {
  NavigateTo("/title1.html");
  content::WebContents* target = GetTargetWebContents();
  ASSERT_TRUE(target);
  ASSERT_TRUE(content::ExecJs(target, R"js(
    document.documentElement.style.background = '#ffffff';
    document.body.style.background = '#f0f0f2';
    document.body.style.backgroundImage =
        'linear-gradient(rgb(240, 240, 242), rgb(240, 240, 242))';
    const content = document.createElement('main');
    content.id = 'invert-content';
    content.style.background = '#ffffff';
    content.textContent = 'Example Domain';
    document.body.append(content);
  )js"));

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.getElementById('zen-boost-invert')?.click()"));
  ASSERT_TRUE(WaitForTargetStyle("invert(1)"));

  const auto backgrounds = content::EvalJs(target, R"js(
    (() => {
      const root = getComputedStyle(document.documentElement).backgroundColor;
      const body = getComputedStyle(document.body).backgroundColor;
      const bodyImage = getComputedStyle(document.body).backgroundImage;
      const content = getComputedStyle(
          document.getElementById('invert-content')).backgroundColor;
      return {body, bodyImage, content, root};
    })()
  )js");
  ASSERT_TRUE(backgrounds.is_ok());
  const base::DictValue& values = backgrounds.ExtractDict();
  const std::string* root_background = values.FindString("root");
  const std::string* body_background = values.FindString("body");
  const std::string* body_image = values.FindString("bodyImage");
  const std::string* content_background = values.FindString("content");
  ASSERT_TRUE(root_background);
  ASSERT_TRUE(body_background);
  ASSERT_TRUE(body_image);
  ASSERT_TRUE(content_background);
  EXPECT_EQ(*root_background, *body_background);
  EXPECT_EQ(*root_background, *content_background);
  EXPECT_NE("none", *body_image);
}

// The editor resets whichever Boost is selected, but the page only ever renders
// the Boost that is active for the domain. Reset must therefore clear what the
// user can actually see, not just the modal state.
IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       ResetClearsTheBoostStyleAppliedToThePage) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain = TargetDomain(GetTargetWebContents());
  const std::optional<std::string> boost_id =
      CreateBoost(core, domain, "Applied");
  ASSERT_TRUE(boost_id);
  SetActiveBoost(core, domain, *boost_id);

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  // Turn on smart invert so the composed CSS carries an observable filter.
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.getElementById('zen-boost-invert')?.click()"));
  ASSERT_TRUE(WaitForCoreBoostCss("invert"));

  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "window.confirm = () => true"));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-reset"));

  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  ASSERT_TRUE(WaitForCondition(
      "reset removed every composed style from the page",
      [target_web_contents]() {
        MahoCore* live_core = maho::GetCore();
        if (!live_core) {
          return false;
        }
        if (!maho::core::GetBoostInjectionCss(
                 live_core,
                 target_web_contents->GetLastCommittedURL().spec().c_str())
                 .empty()) {
          return false;
        }
        const auto style_removed = content::EvalJs(
            target_web_contents,
            "document.getElementById('maho-boost-style') === null");
        return style_removed.is_ok() && style_removed.ExtractBool();
      }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       ResetDefaultsSurviveOffToggleAndEditorReopen) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain = TargetDomain(GetTargetWebContents());
  const std::optional<std::string> boost_id =
      CreateBoost(core, domain, "Reset persistence");
  ASSERT_TRUE(boost_id);
  SetActiveBoost(core, domain, *boost_id);

  char* updated = maho_core_boost_update(
      core, boost_id->c_str(),
      R"json({"color":{"brightness":0.91,"contrast":0.12,"saturation":0.18,"magicTheme":true,"smartInvert":true},"typography":{"fontFamily":"Papyrus","caseMode":"upper","sizeMode":"k150"},"customCss":"body { outline: 7px solid red; }"})json");
  ASSERT_NE(nullptr, updated);
  maho_string_free(updated);
  char* zapped =
      maho_core_boost_append_zap(core, boost_id->c_str(), "#sponsored");
  ASSERT_NE(nullptr, zapped);
  maho_string_free(zapped);
  ASSERT_FALSE(BoostJson(core, *boost_id).empty());

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-reset"));
  ASSERT_TRUE(WaitForCondition("all edited fields reset to canonical defaults", [
      core, boost_id]() {
    const std::string json = BoostJson(core, *boost_id);
    return json.find("\"brightness\":0.5") != std::string::npos &&
           json.find("\"contrast\":0.75") != std::string::npos &&
           json.find("\"saturation\":0.5") != std::string::npos &&
           json.find("\"magicTheme\":false") != std::string::npos &&
           json.find("\"smartInvert\":false") != std::string::npos &&
           json.find("\"fontFamily\":\"\"") != std::string::npos &&
           json.find("\"caseMode\":\"none\"") != std::string::npos &&
           json.find("\"sizeMode\":\"k100\"") != std::string::npos &&
           json.find("\"customCss\":\"\"") != std::string::npos &&
           json.find("#sponsored") == std::string::npos;
  }));

  // Reset runs as a record transition that marks the editor busy (inert) until
  // its pending mutations settle and the snapshot reloads. A user click landing
  // in that window is intentionally ignored by the inert guard, so wait for the
  // editor to become interactive again before toggling color adjustments back on.
  ASSERT_TRUE(WaitForCondition("editor is interactive again after reset", [
      editor_web_contents]() {
    const auto ready = content::EvalJs(
        editor_web_contents,
        "document.getElementById('boost-editor')"
        "?.getAttribute('data-route-state') === 'active' &&"
        " !document.getElementById('zen-boost-disable')?.closest('[inert]')");
    return ready.is_ok() && ready.ExtractBool();
  }));
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.getElementById('zen-boost-disable')?.click()"));
  ASSERT_TRUE(WaitForCondition("Off toggle only re-enabled default color values", [
      core, boost_id]() {
    const std::string json = BoostJson(core, *boost_id);
    return json.find("\"colorBoostEnabled\":true") != std::string::npos &&
           json.find("\"brightness\":0.5") != std::string::npos &&
           json.find("\"contrast\":0.75") != std::string::npos &&
           json.find("\"saturation\":0.5") != std::string::npos &&
           json.find("Papyrus") == std::string::npos &&
           json.find("outline") == std::string::npos &&
           json.find("#sponsored") == std::string::npos;
  }));

  HideBoostEditor();
  ASSERT_TRUE(WaitUntilHidden());
  editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  const std::string reopened = BoostJson(core, *boost_id);
  EXPECT_NE(std::string::npos, reopened.find("\"brightness\":0.5"));
  EXPECT_NE(std::string::npos, reopened.find("\"contrast\":0.75"));
  EXPECT_NE(std::string::npos, reopened.find("\"saturation\":0.5"));
  EXPECT_EQ(std::string::npos, reopened.find("Papyrus"));
  EXPECT_EQ(std::string::npos, reopened.find("outline"));
  EXPECT_EQ(std::string::npos, reopened.find("#sponsored"));
}



// Reproduces the reported defect: with two Boosts on the domain the editor can
// have one selected for editing while a different one is active on the page.
// Reset then rewrites the selected Boost, the modal shows defaults, and the page
// keeps rendering the untouched active Boost.
IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       ResetAffectsThePageWhenSelectionDiffersFromActive) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::string domain = TargetDomain(GetTargetWebContents());
  const std::optional<std::string> active_id =
      CreateBoost(core, domain, "OnThePage");
  ASSERT_TRUE(active_id);
  const std::optional<std::string> other_id =
      CreateBoost(core, domain, "BeingEdited");
  ASSERT_TRUE(other_id);
  SetActiveBoost(core, domain, *active_id);

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);

  // Give the active Boost an observable style so the page renders it.
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.getElementById('zen-boost-invert')?.click()"));
  ASSERT_TRUE(WaitForCoreBoostCss("invert"));

  // Switch the editor to the other Boost and edit it too, so Reset is offered
  // for a Boost that is not the one the page is rendering.
  ASSERT_TRUE(SelectBoostInEditor(editor_web_contents, *other_id));
  ASSERT_TRUE(content::ExecJs(
      editor_web_contents,
      "document.getElementById('zen-boost-invert')?.click()"));
  ASSERT_TRUE(WaitForCondition(
      "the selected Boost recorded an edit so Reset becomes available",
      [core, other_id]() {
        const std::string json = BoostJson(core, *other_id);
        return !json.empty() &&
               json.find("\"changeWasMade\":true") != std::string::npos;
      }));
  ASSERT_TRUE(content::ExecJs(editor_web_contents,
                              "window.confirm = () => true"));
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-reset"));

  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  ASSERT_TRUE(WaitForCondition(
      "reset cleared every style the page is actually showing",
      [target_web_contents]() {
        MahoCore* live_core = maho::GetCore();
        if (!live_core) {
          return false;
        }
        if (!maho::core::GetBoostInjectionCss(
                 live_core,
                 target_web_contents->GetLastCommittedURL().spec().c_str())
                 .empty()) {
          return false;
        }
        const auto style_removed = content::EvalJs(
            target_web_contents,
            "document.getElementById('maho-boost-style') === null");
        return style_removed.is_ok() && style_removed.ExtractBool();
      }));
}


// Deleting a Boost is irreversible, so the first click must only arm a second,
// explicit confirmation. Choosing "Keep Boost" must leave the Boost untouched.
IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       CancellingDeleteConfirmationKeepsTheBoost) {
  NavigateTo("/title1.html");
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  const std::string boost_before =
      WithoutUpdatedAt(ActiveBoostJsonForTarget(GetTargetWebContents()));
  ASSERT_FALSE(boost_before.empty());

  // First click only arms the confirmation; nothing is deleted yet.
  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-delete"));
  EXPECT_TRUE(WaitUntilShowing());
  EXPECT_EQ(boost_before,
            WithoutUpdatedAt(ActiveBoostJsonForTarget(GetTargetWebContents())));

  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-delete-cancel"));
  EXPECT_TRUE(WaitUntilShowing());
  EXPECT_EQ(boost_before,
            WithoutUpdatedAt(ActiveBoostJsonForTarget(GetTargetWebContents())))
      << "cancelling the confirmation must not modify the Boost";

  // The plain Delete entry must be offered again rather than staying armed.
  ASSERT_TRUE(WaitForCondition(
      "delete confirmation reset after cancelling",
      [editor_web_contents]() {
        return IsMenuItemEnabled(editor_web_contents, "zen-boost-edit-delete");
      }));
}

// Confirming the second step actually deletes the Boost.
IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       ConfirmingDeleteRemovesTheBoost) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  const std::string domain = TargetDomain(GetTargetWebContents());
  const std::optional<std::string> keeper = CreateBoost(core, domain, "Keeper");
  ASSERT_TRUE(keeper);
  const std::string boost_json =
      ActiveBoostJsonForTarget(GetTargetWebContents());
  ASSERT_FALSE(boost_json.empty());

  ASSERT_TRUE(ClickMenuItem(editor_web_contents, "zen-boost-edit-delete"));
  ASSERT_TRUE(
      ClickMenuItem(editor_web_contents, "zen-boost-edit-delete-confirm"));
  ASSERT_TRUE(WaitForCondition(
      "confirmed delete removed the Boost",
      [this, boost_json]() {
        return ActiveBoostJsonForTarget(GetTargetWebContents()) != boost_json;
      }));
}

// github.com Boosts are inherited by its subdomains in the core URL policy.
// The Chromium injection bridge must use the same lookup so pages such as
// https://gist.github.com/starred receive the active parent-domain Boost.
IN_PROC_BROWSER_TEST_F(MahoBoostActionsTest,
                       ParentDomainBoostAppliesToGistStarred) {
  NavigateTo("/title1.html");
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const std::optional<std::string> boost_id =
      CreateBoost(core, "github.com", "GitHub");
  ASSERT_TRUE(boost_id);

  char* updated = maho_core_boost_update(
      core, boost_id->c_str(),
      R"json({"typography":{"fontFamily":"Georgia"}})json");
  ASSERT_TRUE(updated);
  maho_string_free(updated);
  SetActiveBoost(core, "github.com", boost_id);

  EXPECT_NE(std::string::npos,
            maho::core::GetBoostInjectionCss(
                core, "https://gist.github.com/starred").find(
                "font-family: Georgia"))
      << "gist.github.com must inherit the active github.com Boost";
}


}  // namespace
