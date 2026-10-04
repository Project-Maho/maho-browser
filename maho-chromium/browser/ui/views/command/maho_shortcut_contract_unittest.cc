// Copyright 2026 Maho Browser. All rights reserved.
//
// Contract test for the shortcut registry triple-sync documented in
// maho-chromium/docs/shortcut-registry-contract.md.
//
// Three lists must stay synchronized for every Maho keyboard shortcut:
//   1. The Rust registry (maho/crates/maho-core/src/shortcut_manager.rs)
//   2. The C++ dispatch chain (ExecuteCommandAction /
//      ExecutePrivateCommandAction in maho_command_action_handler.cc)
//   3. The C++ key codec (StringToVKey/VKeyToString in
//      maho_shortcut_interceptor.cc)
//
// If any one drifts, the key press is silently swallowed or never fires.
// That exact failure broke Ctrl+Tab, Ctrl+Shift+\, and Cmd+Plus in the
// 2026-08 regression this test was written to prevent.

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace maho {
namespace {

struct RegistryBinding {
  std::string action;
  std::string key;
  bool enabled = false;
};

// Reads the compiled-in default registry from a fresh in-memory maho-core.
std::vector<RegistryBinding> ReadRegistry() {
  std::vector<RegistryBinding> bindings;

  MahoCore* core = maho_core_new();
  if (!core) {
    ADD_FAILURE() << "maho_core_new() returned null";
    return bindings;
  }
  char* shortcuts = maho_core_get_shortcuts(core);
  const std::string json = shortcuts ? shortcuts : "[]";
  if (shortcuts) {
    maho_string_free(shortcuts);
  }
  maho_core_free(core);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    ADD_FAILURE() << "shortcuts JSON is not a list: " << json.substr(0, 200);
    return bindings;
  }
  for (const base::Value& entry : parsed->GetList()) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    RegistryBinding binding;
    if (const std::string* action = dict->FindString("action")) {
      binding.action = *action;
    }
    if (const base::DictValue* combo = dict->FindDict("keyCombo")) {
      if (const std::string* key = combo->FindString("key")) {
        binding.key = *key;
      }
    }
    binding.enabled = dict->FindBool("enabled").value_or(false);
    if (!binding.action.empty() && !binding.key.empty()) {
      bindings.push_back(std::move(binding));
    }
  }
  return bindings;
}

base::FilePath HandlerSourcePath() {
  // DIR_SRC_TEST_DATA_ROOT is the chromium/src root; the overlay is
  // symlinked in at chromium/src/maho. Same pattern as
  // maho_password_autofill_row_unittest.cc.
  base::FilePath source_root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source_root));
  return source_root.AppendASCII("maho")
      .AppendASCII("browser/ui/views/command")
      .AppendASCII("maho_command_action_handler.cc");
}

// Scans the dispatch chain source for `action_id == "..."` literals and
// `action_id.starts_with("...")` prefix matches. Whitespace-tolerant so
// clang-format churn cannot break the scan.
void CollectHandledIds(const std::string& source,
                       std::set<std::string>* exact,
                       std::set<std::string>* prefixes) {
  static constexpr std::string_view kActionId = "action_id";
  size_t pos = 0;
  while ((pos = source.find(kActionId, pos)) != std::string::npos) {
    pos += kActionId.size();
    size_t cursor = pos;
    while (cursor < source.size() &&
           base::IsAsciiWhitespace(source[cursor])) {
      ++cursor;
    }

    // Form 1: action_id == "literal"
    if (source.compare(cursor, 2, "==") == 0) {
      cursor += 2;
      while (cursor < source.size() &&
             base::IsAsciiWhitespace(source[cursor])) {
        ++cursor;
      }
      if (cursor < source.size() && source[cursor] == '"') {
        const size_t start = cursor + 1;
        const size_t end = source.find('"', start);
        if (end != std::string::npos) {
          exact->insert(source.substr(start, end - start));
          pos = end + 1;
          continue;
        }
      }
    }

    // Form 2: action_id.starts_with("prefix")
    if (source.compare(cursor, 1, ".") == 0) {
      static constexpr std::string_view kStartsWith = "starts_with(";
      const size_t call = source.find(kStartsWith, cursor);
      if (call == cursor + 1) {
        size_t quote = call + kStartsWith.size();
        while (quote < source.size() &&
               base::IsAsciiWhitespace(source[quote])) {
          ++quote;
        }
        if (quote < source.size() && source[quote] == '"') {
          const size_t start = quote + 1;
          const size_t end = source.find('"', start);
          if (end != std::string::npos) {
            prefixes->insert(source.substr(start, end - start));
            pos = end + 1;
            continue;
          }
        }
      }
    }
  }
}

using MahoShortcutContractTest = testing::Test;

// List 3: every key the Rust registry emits must survive a
// StringToVKey -> VKeyToString round trip. A key with no codec mapping can
// never be registered as a FocusManager accelerator and never resolved from
// a key event — the shortcut is unreachable on both dispatch paths.
TEST_F(MahoShortcutContractTest, EveryRegistryKeyRoundTripsThroughCodec) {
  const std::vector<RegistryBinding> bindings = ReadRegistry();
  ASSERT_GT(bindings.size(), 20u) << "registry looks empty; FFI drift?";

  for (const RegistryBinding& binding : bindings) {
    const ui::KeyboardCode code = StringToVKey(binding.key);
    EXPECT_NE(ui::VKEY_UNKNOWN, code)
        << "key \"" << binding.key << "\" (action \"" << binding.action
        << "\") has no VKEY mapping; extend StringToVKey()/VKeyToString() in "
           "maho_shortcut_interceptor.cc";
    if (code == ui::VKEY_UNKNOWN) {
      continue;
    }
    EXPECT_EQ(binding.key, VKeyToString(code))
        << "VKeyToString(StringToVKey(\"" << binding.key
        << "\")) != \"" << binding.key
        << "\"; the codec pair drifted out of sync";
  }
}

// List 1 -> List 2: every registry action (enabled or not — users can
// re-enable any binding in Settings) must be consumed by the C++ dispatch
// chain. An unmatched action is silently swallowed: the interceptor
// resolves the key, ExecuteCommandAction falls through to its
// "Unknown shortcut action" else, and the event dies without falling back
// to Chromium's native accelerator.
TEST_F(MahoShortcutContractTest, EveryRegistryActionIsHandled) {
  const std::vector<RegistryBinding> bindings = ReadRegistry();
  ASSERT_GT(bindings.size(), 20u) << "registry looks empty; FFI drift?";

  std::string source;
  ASSERT_TRUE(base::ReadFileToString(HandlerSourcePath(), &source))
      << "cannot read maho_command_action_handler.cc through the "
         "chromium/src/maho symlink";

  std::set<std::string> exact;
  std::set<std::string> prefixes;
  CollectHandledIds(source, &exact, &prefixes);

  // Structural sentinels: if the chain is rewritten into a shape the
  // scanner cannot see (e.g. a data table), fail loudly instead of
  // passing vacuously.
  EXPECT_GT(exact.size(), 40u)
      << "dispatch chain scan found suspiciously few action_id literals; "
         "did maho_command_action_handler.cc change shape?";
  for (const char* sentinel :
       {"new_tab", "close_tab", "mru_tab_switch_next", "mru_tab_switch_prev"}) {
    EXPECT_EQ(1u, exact.count(sentinel))
        << "sentinel action \"" << sentinel
        << "\" not found in the dispatch chain scan";
  }
  for (const char* sentinel : {"select_tab_", "select_space_"}) {
    EXPECT_EQ(1u, prefixes.count(sentinel))
        << "sentinel prefix \"" << sentinel
        << "\" not found in the dispatch chain scan";
  }

  for (const RegistryBinding& binding : bindings) {
    bool handled = exact.count(binding.action) > 0;
    if (!handled) {
      for (const std::string& prefix : prefixes) {
        if (base::StartsWith(binding.action, prefix,
                             base::CompareCase::SENSITIVE)) {
          handled = true;
          break;
        }
      }
    }
    EXPECT_TRUE(handled)
        << "registry action \"" << binding.action
        << "\" (key \"" << binding.key
        << "\") has no branch in ExecuteCommandAction()/"
           "ExecutePrivateCommandAction(); the interceptor will resolve the "
           "key and then silently swallow it — add a dispatch case";
  }
}

}  // namespace
}  // namespace maho
