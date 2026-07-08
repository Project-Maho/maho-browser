// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_tab_id_helper.h"

#include "base/no_destructor.h"
#include "base/uuid.h"

#include <queue>
#include "base/synchronization/lock.h"

#include "base/json/json_reader.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoTabIdHelper);

namespace {
base::Lock& GetPendingLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}
std::queue<std::string>& GetPendingQueue() {
  static base::NoDestructor<std::queue<std::string>> queue;
  return *queue;
}

std::string QueryTabRole(const std::string& tab_id) {
  ::MahoCore* core = maho::GetCore();
  if (!core) {
    return "";
  }
  char* json_str = maho_core_get_tab_snapshot_by_id(core, tab_id.c_str());
  if (!json_str) {
    return "";
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return "";
  }

  const auto& dict = parsed->GetDict();
  if (const auto* role = dict.FindDict("role")) {
    if (const std::string* type = role->FindString("type")) {
      return *type;
    }
  }
  return "";
}
}  // namespace

// static
void MahoTabIdHelper::SetPendingRestoredTabId(const std::string& id) {
  if (id.empty()) {
    return;
  }
  if (QueryTabRole(id) == "favorite") {
    return;
  }
  base::AutoLock lock(GetPendingLock());
  auto& queue = GetPendingQueue();
  std::queue<std::string> temp = queue;
  while (!temp.empty()) {
    if (temp.front() == id) {
      return;
    }
    temp.pop();
  }
  queue.push(id);
}

// static
std::string MahoTabIdHelper::PopPendingRestoredTabIdForTesting() {
  base::AutoLock lock(GetPendingLock());
  auto& queue = GetPendingQueue();
  if (queue.empty()) {
    return "";
  }
  std::string id = queue.front();
  queue.pop();
  return id;
}

MahoTabIdHelper::MahoTabIdHelper(content::WebContents* web_contents)
    : content::WebContentsUserData<MahoTabIdHelper>(*web_contents) {
  base::AutoLock lock(GetPendingLock());
  auto& queue = GetPendingQueue();
  if (!queue.empty()) {
    stable_tab_id_ = queue.front();
    queue.pop();
  } else {
    stable_tab_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
}

MahoTabIdHelper::~MahoTabIdHelper() = default;

void MahoTabIdHelper::SetRestoredTabId(const std::string& id) {
  if (!id.empty()) {
    stable_tab_id_ = id;
    has_been_announced_ = false;
  }
}
