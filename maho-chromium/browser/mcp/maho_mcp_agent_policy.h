// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_AGENT_POLICY_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_AGENT_POLICY_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/values.h"

namespace maho {

enum class MahoMcpControllerKind;

// Error codes shared by the MCP session and its clients.
inline constexpr int kMahoMcpErrorTaskSpaceUserInControl = -32016;
inline constexpr char kMahoMcpMessageTaskSpaceUserInControl[] =
    "TASK_SPACE_USER_IN_CONTROL";
inline constexpr int kMahoMcpErrorPageDialogOpened = -32017;
inline constexpr char kMahoMcpMessagePageDialogOpened[] = "PAGE_DIALOG_OPENED";
inline constexpr int kMahoMcpErrorNoPendingDialog = -32018;
inline constexpr char kMahoMcpMessageNoPendingDialog[] = "NO_PENDING_DIALOG";

// Non-disturbing operation. A tab opened by an agent controller (the bundled
// agent tool server, a third-party MCP host) lands in the background so the
// user's foreground tab and focus are untouched; only a direct user command
// from the Maho CLI opens in the foreground. |background_override| is the
// caller's explicit `background` argument when present.
bool ShouldForegroundAgentTab(MahoMcpControllerKind kind,
                              std::optional<bool> background_override);

// Keyboard input may move focus inside the browser only when the target
// window is already the active window and the page view does not have focus
// yet. It never activates a window or steals focus from another app.
bool ShouldFocusForKeyPress(bool window_active, bool view_has_focus);

// Agent task-space ownership. A space (keyed by tab id) is created owned by
// the agent session that opened it; asking the human for help hands it to
// the user, and the agent must take it back before mutating again.
enum class TaskSpaceOwner {
  kAgent,
  kUser,
};

enum class TaskSpaceState {
  kActive,
  kCompleted,
  kError,
};

enum class TaskSpaceMutationDecision {
  kAllowed,
  kUserInControl,
  kNotOwner,
  kClosed,
};

class MahoAgentTaskSpaceRegistry {
 public:
  struct Space {
    std::string session_id;
    TaskSpaceOwner owner = TaskSpaceOwner::kAgent;
    TaskSpaceState state = TaskSpaceState::kActive;
  };

  MahoAgentTaskSpaceRegistry();
  ~MahoAgentTaskSpaceRegistry();
  MahoAgentTaskSpaceRegistry(const MahoAgentTaskSpaceRegistry&) = delete;
  MahoAgentTaskSpaceRegistry& operator=(const MahoAgentTaskSpaceRegistry&) =
      delete;

  // Process-wide instance shared by MCP sessions and the browser UI.
  static MahoAgentTaskSpaceRegistry& Get();

  // Returns false when the tab already belongs to an active space.
  bool Create(int64_t tab_id, std::string_view session_id);
  bool Handoff(int64_t tab_id, std::string_view session_id);
  bool Takeover(int64_t tab_id, std::string_view session_id);
  bool Complete(int64_t tab_id, std::string_view session_id);
  bool Fail(int64_t tab_id, std::string_view session_id);
  void Remove(int64_t tab_id);

  // Tabs without a space are not governed by this registry.
  TaskSpaceMutationDecision CheckMutation(int64_t tab_id,
                                          std::string_view session_id) const;
  std::optional<Space> Find(int64_t tab_id) const;

 private:
  Space* FindActiveOwned(int64_t tab_id, std::string_view session_id)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);

  mutable base::Lock lock_;
  std::map<int64_t, Space> spaces_ GUARDED_BY(lock_);
};

// JS dialog policy. While a page dialog is pending, page-touching commands
// on that tab are blocked with PAGE_DIALOG_OPENED; tab bookkeeping, the
// dialog response itself and lease control stay available. A dialog never
// changes task-space ownership.
struct PendingPageDialog {
  std::string type;
  std::string message;
  std::string default_prompt;
  std::string url;
};

bool IsBlockedByPendingDialog(std::string_view tool_name);

// browser_download argument validation. |dest_dir| is relative to the agent
// download root; absolute paths and parent traversal are rejected.
struct DownloadRequestValidation {
  bool ok = false;
  std::string error;
};
DownloadRequestValidation ValidateDownloadRequest(
    std::string_view url,
    std::optional<std::string_view> dest_dir);

// browser_import_list result shape: one entry per importable browser profile.
struct ImportSourceInfo {
  std::string browser_type;
  std::string display_name;
  std::string profile_path;
  uint32_t services_supported = 0;
  bool requires_full_disk_access = false;
};
base::DictValue SerializeImportSources(
    const std::vector<ImportSourceInfo>& sources);

// Page-adapter routing for browser_type. An exact-origin adapter that
// declares `insert_text` gets text committed through the editor's insertion
// path instead of synthetic DOM typing.
enum class TypeRoute {
  kDomTyping,
  kInsertText,
};
TypeRoute DecideTypeRoute(std::string_view tool_name,
                          bool adapter_supports_insert_text);

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_AGENT_POLICY_H_
