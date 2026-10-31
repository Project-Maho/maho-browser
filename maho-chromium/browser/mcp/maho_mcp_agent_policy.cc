// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_agent_policy.h"

#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "url/gurl.h"

namespace maho {

bool ShouldForegroundAgentTab(MahoMcpControllerKind kind,
                              std::optional<bool> background_override) {
  if (background_override.has_value()) {
    return !*background_override;
  }
  switch (kind) {
    case MahoMcpControllerKind::kMahoCli:
    case MahoMcpControllerKind::kMahoCliRepl:
      return true;
    case MahoMcpControllerKind::kMahoBrowserMcp:
    case MahoMcpControllerKind::kThirdParty:
      return false;
  }
  return false;
}

bool ShouldFocusForKeyPress(bool window_active, bool view_has_focus) {
  return window_active && !view_has_focus;
}

MahoAgentTaskSpaceRegistry::MahoAgentTaskSpaceRegistry() = default;
MahoAgentTaskSpaceRegistry::~MahoAgentTaskSpaceRegistry() = default;

// static
MahoAgentTaskSpaceRegistry& MahoAgentTaskSpaceRegistry::Get() {
  static base::NoDestructor<MahoAgentTaskSpaceRegistry> instance;
  return *instance;
}

bool MahoAgentTaskSpaceRegistry::Create(int64_t tab_id,
                                        std::string_view session_id) {
  base::AutoLock auto_lock(lock_);
  if (session_id.empty()) {
    return false;
  }
  auto it = spaces_.find(tab_id);
  if (it != spaces_.end() && it->second.state == TaskSpaceState::kActive) {
    return false;
  }
  Space space;
  space.session_id = std::string(session_id);
  spaces_[tab_id] = std::move(space);
  return true;
}

MahoAgentTaskSpaceRegistry::Space* MahoAgentTaskSpaceRegistry::FindActiveOwned(
    int64_t tab_id,
    std::string_view session_id) {
  auto it = spaces_.find(tab_id);
  if (it == spaces_.end() || it->second.session_id != session_id ||
      it->second.state != TaskSpaceState::kActive) {
    return nullptr;
  }
  return &it->second;
}

bool MahoAgentTaskSpaceRegistry::Handoff(int64_t tab_id,
                                         std::string_view session_id) {
  base::AutoLock auto_lock(lock_);
  Space* space = FindActiveOwned(tab_id, session_id);
  if (!space || space->owner != TaskSpaceOwner::kAgent) {
    return false;
  }
  space->owner = TaskSpaceOwner::kUser;
  return true;
}

bool MahoAgentTaskSpaceRegistry::Takeover(int64_t tab_id,
                                          std::string_view session_id) {
  base::AutoLock auto_lock(lock_);
  Space* space = FindActiveOwned(tab_id, session_id);
  if (!space || space->owner != TaskSpaceOwner::kUser) {
    return false;
  }
  space->owner = TaskSpaceOwner::kAgent;
  return true;
}

bool MahoAgentTaskSpaceRegistry::Complete(int64_t tab_id,
                                          std::string_view session_id) {
  base::AutoLock auto_lock(lock_);
  Space* space = FindActiveOwned(tab_id, session_id);
  if (!space) {
    return false;
  }
  space->state = TaskSpaceState::kCompleted;
  return true;
}

bool MahoAgentTaskSpaceRegistry::Fail(int64_t tab_id,
                                      std::string_view session_id) {
  base::AutoLock auto_lock(lock_);
  Space* space = FindActiveOwned(tab_id, session_id);
  if (!space) {
    return false;
  }
  space->state = TaskSpaceState::kError;
  return true;
}

void MahoAgentTaskSpaceRegistry::Remove(int64_t tab_id) {
  base::AutoLock auto_lock(lock_);
  spaces_.erase(tab_id);
}

TaskSpaceMutationDecision MahoAgentTaskSpaceRegistry::CheckMutation(
    int64_t tab_id,
    std::string_view session_id) const {
  base::AutoLock auto_lock(lock_);
  auto it = spaces_.find(tab_id);
  if (it == spaces_.end()) {
    return TaskSpaceMutationDecision::kAllowed;
  }
  const Space& space = it->second;
  if (space.state != TaskSpaceState::kActive) {
    return TaskSpaceMutationDecision::kClosed;
  }
  if (space.owner == TaskSpaceOwner::kUser) {
    return TaskSpaceMutationDecision::kUserInControl;
  }
  if (space.session_id != session_id) {
    return TaskSpaceMutationDecision::kNotOwner;
  }
  return TaskSpaceMutationDecision::kAllowed;
}

std::optional<MahoAgentTaskSpaceRegistry::Space>
MahoAgentTaskSpaceRegistry::Find(int64_t tab_id) const {
  base::AutoLock auto_lock(lock_);
  auto it = spaces_.find(tab_id);
  if (it == spaces_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool IsBlockedByPendingDialog(std::string_view tool_name) {
  static constexpr std::string_view kAllowedWhileDialogPending[] = {
      "browser_dialog_respond", "browser_tab_list",      "browser_tab_get",
      "browser_tab_close",      "browser_tab_new",       "browser_tab_switch",
      "browser_acquire_lease",  "browser_heartbeat_lease",
      "browser_release_lease",  "browser_request_help",
      "browser_task_space",
  };
  for (std::string_view allowed : kAllowedWhileDialogPending) {
    if (allowed == tool_name) {
      return false;
    }
  }
  return true;
}

DownloadRequestValidation ValidateDownloadRequest(
    std::string_view url,
    std::optional<std::string_view> dest_dir) {
  DownloadRequestValidation result;
  const GURL gurl{std::string(url)};
  if (!gurl.is_valid() || !gurl.SchemeIsHTTPOrHTTPS()) {
    result.error = "url must be an absolute http(s) URL";
    return result;
  }
  if (dest_dir.has_value()) {
    const std::string_view dir = *dest_dir;
    if (dir.empty()) {
      result.error = "dest_dir must not be empty";
      return result;
    }
    if (dir.front() == '/' || dir.front() == '\\' ||
        (dir.size() > 1 && dir[1] == ':') || dir.find('\0') != dir.npos) {
      result.error = "dest_dir must be relative to the agent download root";
      return result;
    }
    size_t start = 0;
    while (start <= dir.size()) {
      size_t end = dir.find_first_of("/\\", start);
      if (end == std::string_view::npos) {
        end = dir.size();
      }
      if (dir.substr(start, end - start) == "..") {
        result.error = "dest_dir must not contain '..'";
        return result;
      }
      start = end + 1;
    }
  }
  result.ok = true;
  return result;
}

base::DictValue SerializeImportSources(
    const std::vector<ImportSourceInfo>& sources) {
  base::ListValue list;
  for (size_t i = 0; i < sources.size(); ++i) {
    const ImportSourceInfo& source = sources[i];
    base::DictValue entry;
    entry.Set("index", static_cast<int>(i));
    entry.Set("browser_type", source.browser_type);
    entry.Set("display_name", source.display_name);
    entry.Set("profile_path", source.profile_path);
    entry.Set("services_supported", static_cast<int>(source.services_supported));
    entry.Set("requires_full_disk_access", source.requires_full_disk_access);
    list.Append(std::move(entry));
  }
  base::DictValue result;
  result.Set("count", static_cast<int>(sources.size()));
  result.Set("sources", std::move(list));
  return result;
}

TypeRoute DecideTypeRoute(std::string_view tool_name,
                          bool adapter_supports_insert_text) {
  if (tool_name == "browser_type" && adapter_supports_insert_text) {
    return TypeRoute::kInsertText;
  }
  return TypeRoute::kDomTyping;
}

}  // namespace maho
