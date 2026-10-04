// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_BROWSER_ACTION_HANDLER_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_BROWSER_ACTION_HANDLER_H_

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/values.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "ui/accessibility/ax_node_id_forward.h"

namespace maho {

// Reusable browser action implementation for MCP-compatible page actions.
//
// This component deliberately returns structured action results/errors only. It
// does not know about JSON-RPC framing, session lifecycle, lease ownership,
// allowed-domain policy, firewall wrapping, or tool advertisement; those remain
// owned by MahoMcpSession (or by future non-MCP callers at their own boundary).
class MahoMcpBrowserActionHandler {
 public:
  using RefTable = std::unordered_map<int, ui::AXNodeID>;
  using ScrollPositions = std::unordered_map<int, int>;

  struct Error {
    Error();
    Error(int code, std::string message);
    Error(const Error&);
    Error& operator=(const Error&);
    Error(Error&&) noexcept;
    Error& operator=(Error&&) noexcept;
    ~Error();

    int code = 0;
    std::string message;
  };

  struct Result {
    Result();
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&&) noexcept;
    Result& operator=(Result&&) noexcept;
    ~Result();

    std::optional<Error> error;
    base::DictValue value;
  };

  static Result HandleClick(MahoMcpBrowserDelegate* delegate,
                            const RefTable& refs,
                            int tab_id,
                            const base::DictValue* arguments) {
    Result validation = ValidateClickRequest(refs, arguments);
    if (validation.error.has_value()) {
      return validation;
    }
    int ref = arguments->FindInt("ref").value();
    ui::AXNodeID ax_id = validation.value.FindInt("ax_id").value();

    const bool force =
        arguments ? arguments->FindBool("force").value_or(false) : false;
    bool clicked = false;
    if (force) {
      clicked = delegate && delegate->ClickForced(tab_id, ax_id);
    } else {
      clicked = delegate && delegate->Click(tab_id, ax_id);
    }
    if (!clicked) {
      return MakeError(-32000,
                       "Action failed: click could not be dispatched");
    }

    base::DictValue result;
    result.Set("clicked", clicked);
    result.Set("ref", ref);
    if (force) {
      result.Set("forced", true);
    }
    return MakeSuccess(std::move(result));
  }

  static Result HandleType(MahoMcpBrowserDelegate* delegate,
                           const RefTable& refs,
                           int tab_id,
                           const base::DictValue* arguments) {
    Result validation =
        ValidateTypeRequest(delegate, refs, tab_id, arguments);
    if (validation.error.has_value()) {
      return validation;
    }
    int ref = arguments->FindInt("ref").value();
    ui::AXNodeID ax_id = validation.value.FindInt("ax_id").value();

    const std::string& text = *arguments->FindString("text");
    bool typed = delegate && delegate->Type(tab_id, ax_id, text);
    if (!typed) {
      return MakeError(-32000,
                       "Action failed: type could not be dispatched");
    }

    base::DictValue result;
    result.Set("typed", typed);
    result.Set("ref", ref);
    return MakeSuccess(std::move(result));
  }

  static Result ValidateClickRequest(const RefTable& refs,
                                     const base::DictValue* arguments) {
    if (!arguments || !arguments->FindInt("ref")) {
      return MakeError(-32602, "Invalid params: ref required");
    }
    const int ref = arguments->FindInt("ref").value();
    auto it = refs.find(ref);
    if (it == refs.end()) {
      return UnknownRefError();
    }
    base::DictValue value;
    value.Set("ax_id", static_cast<int>(it->second));
    return MakeSuccess(std::move(value));
  }

  static Result ValidateTypeRequest(MahoMcpBrowserDelegate* delegate,
                                    const RefTable& refs,
                                    int tab_id,
                                    const base::DictValue* arguments) {
    if (!arguments || !arguments->FindInt("ref") ||
        !arguments->FindString("text")) {
      return MakeError(-32602, "Invalid params: ref and text required");
    }
    const int ref = arguments->FindInt("ref").value();
    auto it = refs.find(ref);
    if (it == refs.end()) {
      return UnknownRefError();
    }
    const bool allow_credentials =
        arguments->FindBool("allow_credentials").value_or(false);
    if (!allow_credentials && delegate &&
        IsCredentialField(delegate->GetFieldMetadata(tab_id, it->second))) {
      return MakeError(
          -32002,
          "Credential fields (password, one-time-code, recovery) cannot be "
          "filled through generic typing");
    }
    base::DictValue value;
    value.Set("ax_id", static_cast<int>(it->second));
    return MakeSuccess(std::move(value));
  }

  static Result HandleSelect(MahoMcpBrowserDelegate* delegate,
                             const RefTable& refs,
                             int tab_id,
                             const base::DictValue* arguments) {
    if (!arguments || !arguments->FindInt("ref") ||
        !arguments->FindString("value")) {
      return MakeError(-32602, "Invalid params: ref and value required");
    }
    int ref = arguments->FindInt("ref").value();
    auto it = refs.find(ref);
    if (it == refs.end()) {
      return UnknownRefError();
    }

    const std::string& value = *arguments->FindString("value");
    bool selected = delegate && delegate->Select(tab_id, it->second, value);
    if (!selected) {
      return MakeError(-32000,
                       "Action failed: select could not be dispatched");
    }

    base::DictValue result;
    result.Set("selected", selected);
    result.Set("ref", ref);
    result.Set("value", value);
    return MakeSuccess(std::move(result));
  }

  static Result HandleScroll(MahoMcpBrowserDelegate* delegate,
                             const RefTable& refs,
                             ScrollPositions& scroll_positions,
                             int tab_id,
                             const base::DictValue* arguments) {
    if (!arguments || !arguments->FindString("direction")) {
      return MakeError(-32602, "Invalid params: direction required");
    }
    std::string direction = *arguments->FindString("direction");
    int pixels = arguments->FindInt("pixels").value_or(300);
    std::optional<int> ref = arguments->FindInt("ref");

    if (direction != "up" && direction != "down" && direction != "left" &&
        direction != "right") {
      return MakeError(-32602, "Invalid params: unknown direction");
    }

    std::optional<ui::AXNodeID> ax_id;
    if (ref.has_value()) {
      auto it = refs.find(ref.value());
      if (it == refs.end()) {
        return UnknownRefError();
      }
      ax_id = it->second;
    }

    if (delegate) {
      delegate->Scroll(tab_id, direction, pixels, ax_id);
    }

    int& current_scroll_y = scroll_positions[tab_id];
    if (direction == "down") {
      current_scroll_y += pixels;
    } else if (direction == "up") {
      current_scroll_y = std::max(0, current_scroll_y - pixels);
    }

    base::DictValue result;
    result.Set("scrolled", true);
    result.Set("new_scroll_y", current_scroll_y);
    return MakeSuccess(std::move(result));
  }

  static Result HandleHover(MahoMcpBrowserDelegate* delegate,
                            const RefTable& refs,
                            int tab_id,
                            const base::DictValue* arguments) {
    if (!arguments || !arguments->FindInt("ref")) {
      return MakeError(-32602, "Invalid params: ref required");
    }
    int ref = arguments->FindInt("ref").value();
    auto it = refs.find(ref);
    if (it == refs.end()) {
      return UnknownRefError();
    }

    if (delegate) {
      delegate->Hover(tab_id, it->second);
    }

    base::DictValue result;
    result.Set("hovered", true);
    result.Set("ref", ref);
    return MakeSuccess(std::move(result));
  }

  static Result HandleKeyPress(MahoMcpBrowserDelegate* delegate,
                               int tab_id,
                               const base::DictValue* arguments) {
    if (!arguments || !arguments->FindString("key")) {
      return MakeError(-32602, "Invalid params: key required");
    }
    std::string key = *arguments->FindString("key");
    const base::ListValue* modifiers = arguments->FindList("modifiers");

    bool valid_key =
        (key.size() == 1) ||
        (key == "Enter" || key == "Tab" || key == "Escape" ||
         key == "Backspace" || key == "Delete" || key == "ArrowUp" ||
         key == "ArrowDown" || key == "ArrowLeft" || key == "ArrowRight" ||
         key == "Home" || key == "End" || key == "PageUp" ||
         key == "PageDown" || key == "Space" || key == " " ||
         (key[0] == 'F' && key.size() > 1 && key.size() <= 3));
    if (!valid_key) {
      return MakeError(-32602, "Invalid params: unknown key");
    }

    std::vector<std::string> mod_list;
    if (modifiers) {
      for (const auto& mod : *modifiers) {
        if (!mod.is_string()) {
          return MakeError(-32602,
                           "Invalid params: modifiers must be list of strings");
        }
        std::string mod_str = mod.GetString();
        if (mod_str != "shift" && mod_str != "ctrl" && mod_str != "alt" &&
            mod_str != "meta") {
          return MakeError(-32602,
                           "Invalid params: unknown modifier: " + mod_str);
        }
        mod_list.push_back(mod_str);
      }
    }

    if (delegate) {
      delegate->KeyPress(tab_id, key, mod_list);
    }

    base::DictValue result;
    result.Set("key_pressed", true);
    result.Set("key", key);
    return MakeSuccess(std::move(result));
  }

  static Result HandleFileUpload(MahoMcpBrowserDelegate* delegate,
                                 int tab_id,
                                 const base::DictValue* arguments) {
    if (!arguments || !arguments->FindString("path")) {
      return MakeError(-32602, "Invalid params: path required");
    }
    const std::string& path = *arguments->FindString("path");
    if (path.empty()) {
      return MakeError(-32602, "Invalid params: path required");
    }

    const base::Value* selector_val = arguments->Find("selector");
    if (!selector_val) {
      selector_val = arguments->Find("css");
    }

    const std::string* selector_str = nullptr;
    if (selector_val) {
      if (!selector_val->is_string()) {
        return MakeError(-32602, "Invalid params: selector must be a string");
      }
      selector_str = &selector_val->GetString();
      if (selector_str->empty()) {
        return MakeError(-32602, "Invalid params: selector cannot be empty");
      }
    }

    bool selected = false;
    if (selector_str) {
      selected =
          delegate && delegate->SelectFileForInput(tab_id, *selector_str, path);
    } else {
      selected =
          delegate && delegate->SelectFileForPendingChooser(tab_id, path);
    }

    if (!selected) {
      return MakeError(-32000,
                       "Action failed: file upload could not be dispatched");
    }

    base::DictValue result;
    result.Set("file_selected", true);
    return MakeSuccess(std::move(result));
  }

  static Result HandleHistoryBack(MahoMcpBrowserDelegate* delegate,
                                  int tab_id) {
    if (!delegate || !delegate->GoBack(tab_id)) {
      return MakeError(-32000, "Action failed: no history back entry");
    }
    base::DictValue result;
    result.Set("navigated", true);
    result.Set("went_back", true);
    return MakeSuccess(std::move(result));
  }

  static bool IsCredentialField(const MahoMcpFieldMetadata& meta) {
    if (!meta.classified) {
      return true;
    }
    if (meta.is_protected) {
      return true;
    }

    static constexpr std::string_view kSensitiveAutocomplete[] = {
        "password", "one-time-code", "otp", "totp", "recovery",
    };
    for (std::string_view token : kSensitiveAutocomplete) {
      if (meta.autocomplete.find(token) != std::string::npos) {
        return true;
      }
    }

    static constexpr std::string_view kSensitiveName[] = {
        "password",    "passcode",      "one-time",      "one time",
        "onetime",     "otp",           "totp",          "2fa",
        "two-factor",  "two factor",    "recovery code", "recovery key",
        "backup code", "authenticator",
    };
    for (std::string_view phrase : kSensitiveName) {
      if (meta.accessible_name.find(phrase) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

 private:
  static Result MakeError(int code, std::string message) {
    Result result;
    result.error = Error{code, std::move(message)};
    return result;
  }

  static Result UnknownRefError() {
    return MakeError(-32000,
                     "Unknown ref: call browser_accessibility_snapshot first");
  }

  static Result MakeSuccess(base::DictValue value) {
    Result result;
    result.value = std::move(value);
    return result;
  }
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_BROWSER_ACTION_HANDLER_H_
